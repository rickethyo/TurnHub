"""Rebuild Rev A's electrical draft. No mechanical geometry is inferred here."""
from pathlib import Path
import shutil
import subprocess
import uuid

ROOT = Path(__file__).resolve().parents[1]
NS = uuid.UUID('f3084516-8a7e-4624-9c15-ca3477e318b9')
def uid(key): return str(uuid.uuid5(NS, key))
def q(s): return '"' + s.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n') + '"'
def blocks(text):
    depth = 0; start = 0; quoted = False; escape = False
    for i, c in enumerate(text):
        if quoted:
            if escape: escape = False
            elif c == '\\': escape = True
            elif c == '"': quoted = False
        elif c == '"': quoted = True
        elif c == '(':
            if depth == 0: start = i
            depth += 1
        elif c == ')':
            depth -= 1
            if depth == 0: yield text[start:i+1]
def children(text): return list(blocks(text[text.index('(')+1:-1]))
def fx(size=1.27): return f'(effects (font (size {size} {size})))'
def prop(name, value, x, y, hide=False):
    return f'(property {q(name)} {q(value)} (at {x} {y} 0) {"(hide yes)" if hide else ""} {fx()})'

# Breadboard rotated 180 degrees: column A holds the 5V..3V3 side, J the CLK..GND side,
# and each row runs 29 (top of the rear view) down to 11.
J = 'CLK SD0 SD1 GPIO15 GPIO2 GPIO0 GPIO4 GPIO16 GPIO17 GPIO5 GPIO18 GPIO19 GND GPIO21 RXD0 TXD0 GPIO22 GPIO23 GND'.split()
A = '5V CMD SD3 SD2 GPIO13 GND GPIO12 GPIO14 GPIO27 GPIO26 GPIO25 GPIO33 GPIO32 GPIO35 GPIO34 SVN SVP EN 3V3'.split()
# position: net, firmware definition, GPIO
SIGNALS = {
    'A25': ('LED_RED', 'RED_LED', 13), 'A22': ('LED_GREEN', 'GREEN_LED', 14),
    'A21': ('LED_BLUE', 'BLUE_LED', 27), 'A20': ('BTN_PASS', 'PASS_BUTTON', 26),
    'A19': ('BTN_ACTION', 'ACTION_BUTTON', 25), 'A18': ('BUZZER', 'BUZZER_PIN', 33),
    'A17': ('BTN_PAUSE', 'PAUSE_WIN_BUTTON', 32),
    'J18': ('PAIR', 'PAIR_BUTTON', 19), 'J22': ('EPD_DC', 'EPD_DC', 16),
    'J21': ('EPD_CS', 'EPD_CS', 17), 'J19': ('EPD_SCLK', 'SPI.begin clock', 18),
    'J16': ('EPD_BUSY', 'EPD_BUSY', 21), 'J13': ('EPD_RST', 'EPD_RST', 22),
    'J12': ('EPD_MOSI', 'SPI.begin MOSI', 23),
}
NETS = {p: data[0] for p, data in SIGNALS.items()}
NETS.update({'J17': 'GND', 'J11': 'GND', 'A24': 'GND', 'A11': '+3V3'})

def pin(num, name, x, y, angle, kind='passive'):
    return f'(pin {kind} line (at {x} {y} {angle}) (length 5.08) (name {q(name)} {fx(1.0)}) (number {q(num)} {fx(1.0)}))'
def custom(name, pins, width, top, bottom, ref='U'):
    return f'''(symbol "Sigil:{name}" (pin_names (offset 1.016)) (in_bom yes) (on_board yes)
      {prop('Reference', ref, 0, top+5)} {prop('Value', name, 0, top+2.5)}
      {prop('Footprint', '', 0, 0, True)}
      (symbol "{name}_0_1" (rectangle (start {-width} {top}) (end {width} {bottom}) (stroke (width 0) (type default)) (fill (type background))))
      (symbol "{name}_1_1" {''.join(pins)}))'''

old = (ROOT / 'Sigilv1.kicad_sch').read_text()
lib = next(b for b in children(old) if b.startswith('(lib_symbols'))
libs = [b for b in children(lib) if any(b.startswith(f'(symbol "{n}"') for n in ('Device:LED', 'Device:R', 'Switch:SW_Push'))]
assert len(libs) == 3
pins = []
for row, names, x, angle in [('A', A, -43.18, 0), ('J', J, 43.18, 180)]:
    for i, name in enumerate(names):
        pos = f'{row}{29-i}'
        kind = 'power_out' if pos == 'A11' else 'passive'
        if pos in SIGNALS: kind = 'input' if pos in ('A20','A19','A17','J18','J16') else 'output'
        pins.append(pin(pos, name + (' / ' + NETS[pos] if pos in SIGNALS else ''), x, round(45.72-i*5.08, 2), angle, kind))
devkit = custom('ESP32_DevKit_38_RearReference', pins, 38.1, 50.8, -50.8)
libs.append(devkit)
epd_names = ['VCC', 'GND', 'DIN', 'CLK', 'CS', 'DC', 'RST', 'BUSY']
epd_nets = ['+3V3', 'GND', 'EPD_MOSI', 'EPD_SCLK', 'EPD_CS', 'EPD_DC', 'EPD_RST', 'EPD_BUSY']
epd = custom('EPD_Logical_Interface', [pin(n, n, -20.32, -i*5.08, 0, 'power_in' if n=='VCC' else 'output' if n=='BUSY' else 'passive' if n=='GND' else 'input') for i,n in enumerate(epd_names)], 15.24, 5.08, -40.64, 'J')
buzz = custom('Buzzer_Logical_Interface', [pin('SIG','SIG',-20.32,0,0,'input'),pin('GND','GND',-20.32,-10.16,0)],15.24,5.08,-15.24,'J')
libs += [epd, buzz]
(ROOT / 'Sigil.kicad_sym').write_text('(kicad_symbol_lib (version 20241209) (generator "Sigil")\n'+'\n'.join(s.replace('"Sigil:', '"',1) for s in [devkit,epd,buzz])+')\n')
(ROOT / 'sym-lib-table').write_text('(sym_lib_table (lib (name "Sigil") (type "KiCad") (uri "${KIPRJMOD}/Sigil.kicad_sym") (options "") (descr "Sigil Rev A interfaces; no verified footprints")))\n')

out = [f'(kicad_sch (version 20260306) (generator "eeschema") (uuid "{NS}") (paper "A3")',
       '(title_block (title "Sigil Rev A - removable DevKit carrier") (rev "A electrical draft") (comment 1 "Rear-photo socket numbering. Mechanical / peripheral verification pending."))',
       '(lib_symbols\n'+'\n'.join(libs)+')']
def note(text,x,y,size=1.27):
    out.append(f'(text {q(text)} (at {x} {y} 0) (effects (font (size {size} {size})) (justify left top)) (uuid "{uid(text)}"))')
def wire(x,y,x2,y2):
    out.append(f'(wire (pts (xy {x} {y}) (xy {x2} {y2})) (stroke (width 0) (type default)) (uuid "{uid(str((x,y,x2,y2)))}"))')
def label(net,x,y):
    out.append(f'(label {q(net)} (at {x} {y} 0) (effects (font (size 1.27 1.27)) (justify left bottom)) (uuid "{uid(net+str((x,y)))}"))')
def nc(x,y): out.append(f'(no_connect (at {x} {y}) (uuid "{uid("nc"+str((x,y)))}"))')
PIN_NUMBERS = {
    'Sigil:ESP32_DevKit_38_RearReference': [f'{r}{i}' for r in 'AJ' for i in range(29, 10, -1)],
    'Sigil:EPD_Logical_Interface': epd_names, 'Sigil:Buzzer_Logical_Interface': ['SIG', 'GND'],
    'Switch:SW_Push': ['1', '2'], 'Device:R': ['1', '2'], 'Device:LED': ['1', '2'],
}
def instance(lib,ref,value,x,y,angle=0,top=8,on=True):
    px = x+8.89 if ref.startswith(('R','D')) else x
    py = y-2.54 if ref.startswith(('R','D')) else y-top
    fields = prop('Reference',ref,px,py) + prop('Value',value,px,py+2.54)
    if angle == 90:
        fields = fields.replace(f'{py} 0)', f'{py} 90)').replace(f'{py+2.54} 0)', f'{py+2.54} 90)')
    # Explicit pin UUIDs; KiCad would otherwise invent random ones on every load.
    pin_uuids = ''.join(f'(pin {q(n)} (uuid "{uid(ref+"/"+n)}"))' for n in PIN_NUMBERS[lib])
    out.append(f'''(symbol (lib_id "{lib}") (at {x} {y} {angle}) (unit 1) (in_bom {"yes" if on else "no"}) (on_board {"yes" if on else "no"}) (dnp no)
      (uuid "{uid(ref)}") {fields} {prop('Footprint','',x,y,True)} {pin_uuids}
      (instances (project "Sigilv1" (path "/{NS}" (reference "{ref}") (unit 1)))))''')
instance('Sigil:ESP32_DevKit_38_RearReference','U1','REMOVABLE ESP32 DEVKIT / 2 x 19',88.9,101.6,top=58.42)
for row,names,x in [('A',A,45.72),('J',J,132.08)]:
    for i,name in enumerate(names):
        y=round(55.88+i*5.08,2); pos=f'{row}{29-i}'
        if pos in NETS:
            end = round(x+(-20.32 if row=='A' else 5.08),2)
            wire(x,y,end,y); label(NETS[pos],end,y)
        else: nc(x,y)
note('BACK / REAR PHOTO VIEW\nA29 top-left; J29 top-right (breadboard rotated 180 degrees)\nThis is NOT a footprint view.',28,21,1.5)
note('USB-powered DevKit; A29 / 5V unused on carrier.\nA11 supplies +3V3. All three GND sockets connected.\nOnboard USB-UART, regulator, BOOT and EN retained.',28,157)
note('CONTROLS - INPUT_PULLUP',175,40,1.5)
for ref,net,y in [('SW1','BTN_PASS',60.96),('SW2','BTN_ACTION',81.28),('SW4','PAIR',101.6),('SW5','BTN_PAUSE',121.92)]:
    instance('Switch:SW_Push',ref,net,193.04,y)
    wire(187.96,y,175.26,y); label(net,175.26,y)
    wire(198.12,y,205.74,y); label('GND',205.74,y)
note('PAIR: J18 / GPIO19 to J17 / GND\nPAUSE/WIN: A17 / GPIO32 to GND (SW3 retired)\nAll released HIGH; pressed LOW.\nNo carrier BOOT or reset circuitry.',175,130)
note('STATUS LEDS - active HIGH',248,40,1.5)
for i,net in enumerate(['LED_RED','LED_GREEN','LED_BLUE'],1):
    x=round(254+(i-1)*35.56,2)
    instance('Device:R',f'R{i}','330R',x,63.5,top=11)
    label(net,x,55.88); wire(x,55.88,x,59.69)
    wire(x,67.31,x,80.01)
    instance('Device:LED',f'D{i}',net.replace('LED_',''),x,83.82,90,top=-8)
    wire(x,87.63,x,101.6); label('GND',x,101.6)
note('330R retained from existing schematic;\nLED part/current verification remains open.',248,110)
instance('Sigil:EPD_Logical_Interface','J2','EPD LOGICAL ONLY',340.36,137.16,top=12.7,on=False)
for i,net in enumerate(epd_nets):
    y=round(137.16+i*5.08,2); wire(320.04,y,304.8,y); label(net,304.8,y)
note('Write-only display; no MISO connection.\nJ2 names are logical signals, NOT physical pin numbers.\nConfirm module, VCC rating and connector order.',294,185)
instance('Sigil:Buzzer_Logical_Interface','J3','BUZZER LOGICAL ONLY',238.76,152.4,top=12.7,on=False)
wire(218.44,152.4,198.12,152.4); label('BUZZER',198.12,152.4)
wire(218.44,162.56,198.12,162.56); label('GND',198.12,162.56)
note('J3 is an unresolved load interface.\nConfirm transducer/driver, current and protection.\nNo direct-drive suitability is assumed.',175,178)
note('SCHEMATIC REVIEW / RELEASE HOLDS\n1. U1 uses A11-A29 / J11-J29 after the 180-degree breadboard rotation (old An = J(30-n), old Jn = A(30-n)).\n   SigilBackMarked.png (BACK view) still shows the pre-rotation A1-A19 / J1-J19 labels.\n2. No DevKit footprint assigned: measure pitch, row spacing, outline, USB-C overhang, holes, socket height and keepouts.\n3. Future footprint: two 1x19 female sockets, unmistakable A29/J29 marks; verify insertion from carrier component side.\n4. Keep USB-C, BOOT and EN/reset accessible; preserve antenna/component clearances after measurement.\n5. Display connector and buzzer circuit are unresolved logical interfaces, excluded from PCB and BOM.\n6. Old GPIO4 auxiliary and GPIO32 display-detect removed; GPIO32 / A17 is now the Pause/Win button (SW5).\n7. Rev A is an electrical draft, NOT fabrication-ready. Power through DevKit USB; no second supply designed.',28,211)
out.append('(embedded_fonts no))')
(ROOT/'Sigilv1.kicad_sch').write_text('\n'.join(out)+'\n')
# Re-save in KiCad's own layout so the file matches what the editor writes and
# later hand edits diff cleanly. Skipped (compact output) without kicad-cli.
if shutil.which('kicad-cli'):
    subprocess.run(['kicad-cli', 'sch', 'upgrade', '--force', str(ROOT/'Sigilv1.kicad_sch')], check=True)

