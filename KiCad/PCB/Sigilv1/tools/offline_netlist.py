"""Export a KiCad-XML-style netlist from a generated Sigil schematic without KiCad.

Usage: python offline_netlist.py Sigil_EInk.kicad_sch eink.xml

Only for schematics written by build_schematic.py: symbols are unrotated and
every connection is a wire end meeting a pin end, a label or another wire end.
It is a stand-in so verify_schematic.py can run on machines without kicad-cli;
it is not ERC. Prefer `kicad-cli sch export netlist` where KiCad is installed.
"""
import re
import sys
import xml.etree.ElementTree as ET


def parse(text):
    tokens = re.findall(r'\(|\)|"(?:\\.|[^"\\])*"|[^\s()]+', text)
    stack = [[]]
    for t in tokens:
        if t == '(': stack.append([])
        elif t == ')': done = stack.pop(); stack[-1].append(done)
        else: stack[-1].append(t[1:-1].replace('\\"', '"').replace('\\\\', '\\') if t.startswith('"') else t)
    return stack[0][0]

def find(node, key): return [c for c in node if isinstance(c, list) and c and c[0] == key]
def first(node, key): return find(node, key)[0]
def pt(x, y): return (round(float(x), 2), round(float(y), 2))

sch = parse(open(sys.argv[1], encoding='utf-8').read())
lib = {}  # lib_id -> [(number, name, type, dx, dy)]
for sym in find(first(sch, 'lib_symbols'), 'symbol'):
    pins = []
    for unit in find(sym, 'symbol'):
        for p in find(unit, 'pin'):
            at = first(p, 'at'); length = float(first(p, 'length')[1])
            # Connection point is the pin's `at`; symbol y is up, schematic y is down.
            pins.append((first(p, 'number')[1], first(p, 'name')[1], p[1], float(at[1]), float(at[2])))
    lib[sym[1]] = pins

parent = {}
def root(a):
    while parent.setdefault(a, a) != a: a = parent[a]
    return a
def join(a, b): parent[root(a)] = root(b)

for w in find(sch, 'wire'):
    xy = find(first(w, 'pts'), 'xy'); join(pt(*xy[0][1:3]), pt(*xy[1][1:3]))
labels = {}
for l in find(sch, 'label'):
    p = pt(*first(l, 'at')[1:3]); root(p); labels.setdefault(l[1], []).append(p)
for pts in labels.values():
    for p in pts[1:]: join(pts[0], p)
nc = {pt(*first(n, 'at')[1:3]) for n in find(sch, 'no_connect')}

comps, nodes = [], []
for s in find(sch, 'symbol'):
    lib_id = first(s, 'lib_id')[1]; at = first(s, 'at')
    props = {p[1]: p[2] for p in find(s, 'property')}
    comps.append((props['Reference'], props['Value'], props.get('Footprint', ''), lib_id))
    for num, name, kind, dx, dy in lib[lib_id]:
        p = pt(float(at[1]) + dx, float(at[2]) - dy)
        nodes.append((props['Reference'], num, name, kind + ('+no_connect' if p in nc else ''), p))

names = {root(ps[0]): n for n, ps in labels.items()}
out = ET.Element('export', version='E')
cs = ET.SubElement(out, 'components')
for ref, value, fp, lib_id in comps:
    c = ET.SubElement(cs, 'comp', ref=ref); ET.SubElement(c, 'value').text = value
    if fp: ET.SubElement(c, 'footprint').text = fp
nets = {}
for ref, num, name, kind, p in nodes:
    r = root(p)
    key = names.get(r)
    if key: key = '/' + key
    else:
        attached = [n for n in nodes if root(n[4]) == r]
        key = f'unconnected-({ref}-{name}-Pad{num})' if len(attached) == 1 else f'Net-({ref}-Pad{num})'
    nets.setdefault(key, []).append((ref, num, name, kind))
ns = ET.SubElement(out, 'nets')
for i, (n, members) in enumerate(sorted(nets.items()), 1):
    e = ET.SubElement(ns, 'net', code=str(i), name=n)
    for ref, num, name, kind in members:
        ET.SubElement(e, 'node', ref=ref, pin=num, pinfunction=name, pintype=kind)
ET.ElementTree(out).write(sys.argv[2])
