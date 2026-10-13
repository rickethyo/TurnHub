#!/usr/bin/env python3
"""Weighted Inter display fonts (semibold/bold) for the LVGL screens.

lv_font_conv only renders a variable font's default (regular) instance, so the
weights the app uses for names, tags and big numbers are rasterized here with
a headless Chromium canvas (see generate_icons.py) from the checked-in
design/fonts/Inter-Variable.ttf, then written as LVGL 4 bpp fmt_txt fonts.
Digits get equal advances, as in generate_fonts.py. --check verifies digests
with stdlib only.
"""
import argparse
import base64
import hashlib
import html as htmllib
import json
import re
import subprocess
import tempfile
from pathlib import Path

from generate_icons import browser

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'design/fonts/Inter-Variable.ttf'
OUT = ROOT / 'LVGL/assets'
ASCII = ''.join(chr(c) for c in range(32, 127))
DIGITS = '+-0123456789:'
# name, weight, px, characters, tracking (em)
SPECS = [
    ('th_eyebrow_13', 650, 13, ASCII, 0.08),
    ('th_semi_16', 600, 16, ASCII, 0.0),
    ('th_semi_20', 600, 20, ASCII, -0.01),
    ('th_bold_28', 700, 28, ASCII, -0.02),
    ('th_num_40', 700, 40, DIGITS, -0.02),
    ('th_num_44', 650, 44, DIGITS, -0.03),
    ('th_num_64', 700, 64, DIGITS, -0.03),
    ('th_num_96', 750, 96, DIGITS, -0.03),
]


def digest():
    h = hashlib.sha256(json.dumps(SPECS).encode() + SOURCE.read_bytes())
    h.update(Path(__file__).read_bytes())
    return h.hexdigest()


def rasterize(exe):
    font = base64.b64encode(SOURCE.read_bytes()).decode()
    page = '''<!doctype html><style>@font-face{font-family:InterV;src:url(data:font/ttf;base64,%s);font-weight:100 900}</style>
<body><pre id="out"></pre><script>
const specs = %s, result = {};
(async () => {
  for (const [name, weight, px] of specs) await document.fonts.load(`${weight} ${px}px InterV`);
  for (const [name, weight, px, chars] of specs) {
    const side = px * 3, c = document.createElement('canvas'); c.width = c.height = side;
    const g = c.getContext('2d', {willReadFrequently: true});
    g.font = `${weight} ${px}px InterV`; g.textBaseline = 'alphabetic';
    const m = g.measureText('Hg');
    const out = {ascent: m.fontBoundingBoxAscent, descent: m.fontBoundingBoxDescent, glyphs: []};
    const ox = px, oy = px * 2;
    for (const ch of chars) {
      g.clearRect(0, 0, side, side); g.fillStyle = '#000'; g.fillText(ch, ox, oy);
      const d = g.getImageData(0, 0, side, side).data;
      let x0 = side, y0 = side, x1 = -1, y1 = -1;
      for (let y = 0; y < side; y++) for (let x = 0; x < side; x++) if (d[(y * side + x) * 4 + 3] > 8) {
        if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
      const glyph = {ch, adv: g.measureText(ch).width, w: 0, h: 0, ox: 0, oy: 0, a: []};
      if (x1 >= 0) {
        glyph.w = x1 - x0 + 1; glyph.h = y1 - y0 + 1; glyph.ox = x0 - ox; glyph.oy = oy - (y1 + 1);
        for (let y = y0; y <= y1; y++) for (let x = x0; x <= x1; x++) glyph.a.push(d[(y * side + x) * 4 + 3]);
      }
      out.glyphs.push(glyph);
    }
    result[name] = out;
  }
  document.getElementById('out').textContent = JSON.stringify(result);
})();
</script></body>''' % (font, json.dumps(SPECS))
    with tempfile.TemporaryDirectory() as tmp:
        html = Path(tmp) / 'fonts.html'
        html.write_text(page)
        dom = subprocess.run([exe, '--headless', '--disable-gpu', f'--user-data-dir={tmp}/profile',
                              '--virtual-time-budget=60000', '--dump-dom', html.as_uri()],
                             capture_output=True, text=True, check=True).stdout
    match = re.search(r'<pre id="out">(.*?)</pre>', dom, re.S)
    if not match or not match[1]:
        raise SystemExit('Browser produced no font data')
    return json.loads(htmllib.unescape(match[1]))


def emit(name, px, tracking, data):
    glyphs = data['glyphs']
    digits = [g for g in glyphs if g['ch'].isdigit()]
    cell = max((g['adv'] for g in digits), default=0)
    bitmap, dsc = [], ['    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */']
    for g in glyphs:
        adv, ox = g['adv'], g['ox']
        if g['ch'].isdigit():  # Tabular figures: equal cells, glyph centered.
            ox += round((cell - adv) / 2); adv = cell
        adv += tracking * px
        index = len(bitmap)
        nibbles = [min(15, (v + 8) // 17) for v in g['a']]
        if len(nibbles) % 2: nibbles.append(0)
        bitmap.extend((nibbles[i] << 4) | nibbles[i + 1] for i in range(0, len(nibbles), 2))
        dsc.append(f"    {{.bitmap_index = {index}, .adv_w = {max(0, round(adv * 16))}, .box_w = {g['w']}, "
                   f".box_h = {g['h']}, .ofs_x = {ox}, .ofs_y = {g['oy']}}} /* {json.dumps(g['ch'])} */")
    codes = [ord(g['ch']) for g in glyphs]
    start = codes[0]
    ascent, descent = round(data['ascent']), round(data['descent'])
    if all(g['ch'] in DIGITS for g in glyphs):
        # Number-only fonts hug the figures so big totals center optically.
        ascent = max(g['h'] + g['oy'] for g in glyphs) + 1
        descent = max(0, -min(g['oy'] for g in glyphs if g['h'])) + 1
    upper = name.upper()
    lines = [f'// Generated by LVGL/tools/generate_display_fonts.py. OFL: design/fonts/OFL-Inter.txt',
             '#include "lvgl.h"',
             f'static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {{']
    lines += ['    ' + ', '.join(f'0x{b:02x}' for b in bitmap[i:i + 16]) + ',' for i in range(0, len(bitmap), 16)]
    lines += ['};', 'static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {', ',\n'.join(dsc), '};',
              'static const uint16_t unicode_list[] = {' + ', '.join(str(c - start) for c in codes) + '};',
              'static const lv_font_fmt_txt_cmap_t cmaps[] = {',
              f'    {{.range_start = {start}, .range_length = {codes[-1] - start + 1}, .glyph_id_start = 1, '
              f'.unicode_list = unicode_list, .glyph_id_ofs_list = NULL, .list_length = {len(codes)}, '
              '.type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY}', '};',
              'static const lv_font_fmt_txt_dsc_t font_dsc = {',
              '    .glyph_bitmap = glyph_bitmap, .glyph_dsc = glyph_dsc, .cmaps = cmaps, .kern_dsc = NULL,',
              '    .kern_scale = 0, .cmap_num = 1, .bpp = 4, .kern_classes = 0, .bitmap_format = 0,', '};',
              f'const lv_font_t {name} = {{',
              '    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt, .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,',
              f'    .line_height = {ascent + descent}, .base_line = {descent}, .subpx = LV_FONT_SUBPX_NONE,',
              f'    .underline_position = {-max(1, px // 10)}, .underline_thickness = {max(1, px // 14)},',
              '    .dsc = &font_dsc, .fallback = NULL, .user_data = NULL,', '};']
    return '\n'.join(lines) + '\n'


def generate(exe):
    data = rasterize(exe)
    manifest = {'source-sha256': digest()}
    for name, _, px, _, tracking in SPECS:
        out = OUT / f'{name}.c'
        out.write_text(emit(name, px, tracking, data[name]), newline='\n')
        manifest[out.name] = hashlib.sha256(out.read_bytes()).hexdigest()
    (OUT / 'display_fonts.sha256.json').write_text(json.dumps(manifest, indent=2) + '\n', newline='\n')
    print(f'Wrote {len(SPECS)} display fonts')


def check():
    manifest = json.loads((OUT / 'display_fonts.sha256.json').read_text())
    if manifest.pop('source-sha256') != digest():
        raise SystemExit('Display font specification changed: run LVGL/tools/generate_display_fonts.py')
    for name, expected in manifest.items():
        text = (OUT / name).read_text().replace('\r\n', '\n')
        if hashlib.sha256(text.encode()).hexdigest() != expected:
            raise SystemExit(f'Display font modified: {name}')
    print(f'Display fonts current: {len(manifest)} fonts')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--browser')
    args = parser.parse_args()
    check() if args.check else generate(browser(args.browser))
