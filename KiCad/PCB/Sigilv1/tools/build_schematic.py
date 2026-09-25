"""Rebuild the two Rev A electrical drafts: Sigil_EInk and Sigil_OLED.

Both share the removable DevKit (U1), the power/ground sockets and the buzzer
interface; they differ in the display interface, and the E-ink draft also
carries the analog joystick (J4) that replaces the Pass/Action/Pause buttons
and the NeoPixel Jewel 7 status ring (J5, data through R1).
Discrete LEDs and the old buttons are absent while the controls are redesigned, so
their GPIOs are NC. No mechanical geometry is inferred here.
"""
from pathlib import Path
import shutil
import subprocess
import uuid

ROOT = Path(__file__).resolve().parents[1]
def q(s): return '"' + s.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n') + '"'
def fx(size=1.27): return f'(effects (font (size {size} {size})))'
def prop(name, value, x, y, hide=False):
    return f'(property {q(name)} {q(value)} (at {x} {y} 0) {"(hide yes)" if hide else ""} {fx()})'

# Socket rows in the rear-photo view: J1 top-left (5V), A1 top-right (CLK).
J = '5V CMD SD3 SD2 GPIO13 GND GPIO12 GPIO14 GPIO27 GPIO26 GPIO25 GPIO33 GPIO32 GPIO35 GPIO34 SVN SVP EN 3V3'.split()
A = 'CLK SD0 SD1 GPIO15 GPIO2 GPIO0 GPIO4 GPIO16 GPIO17 GPIO5 GPIO18 GPIO19 GND GPIO21 RXD0 TXD0 GPIO22 GPIO23 GND'.split()
COMMON_NETS = {'J12': 'BUZZER', 'A13': 'GND', 'A19': 'GND', 'J6': 'GND', 'J19': '+3V3'}

# Unmarked 5-pin analog thumbstick (KY-023 style), header in module order.
# Its "+5V" pin is wired to +3V3 on purpose: VRX/VRY are potentiometer wipers
# that swing to the supply, and the ESP32 ADC inputs must stay at or below
# 3.3 V. VRX/VRY use ADC1 input-only pins (ADC2 is unusable under ESP-NOW);
# SW uses GPIO32's internal pull-up. Firmware: Sigil env `sigil`, the E-ink build.
JOYSTICK = dict(
    symbol='Joystick_Module_Header', value='ANALOG JOYSTICK HEADER (5-PIN)',
    header=[('GND', 'GND'), ('+5V', '+3V3'), ('VRX', 'JOY_X'), ('VRY', 'JOY_Y'),
            ('SW', 'JOY_SW')],
    sockets={'J13': 'JOY_SW', 'J14': 'JOY_Y', 'J15': 'JOY_X'},
    note='Unmarked 5-pin analog thumbstick; pins in module order.\n'
         '"+5V" pin is fed 3.3 V: VRX/VRY swing to the supply and the\n'
         'ESP32 ADC must not see 5 V. SW is a switch to GND (internal pull-up).\n'
         'Click = PASS, push right = Action, push down = Pause/Win.\n'
         'Axis direction depends on mounting; firmware can swap/invert.\n'
         'Firmware: Sigil PlatformIO env sigil (E-ink build).')

# Adafruit NeoPixel Jewel 7, RGBW (SK6812-type): 5 V power from the DevKit's
# USB 5V (J1), data from GPIO26 (J10) through a 330 ohm series resistor (R1)
# at the ring. Data Output is unused (no chained pixels). Pin numbers are
# logical; the Jewel's pads are labelled. Firmware: Sigil env `sigil`.
JEWEL = dict(
    symbol='NeoPixel_Jewel7_RGBW', value='NEOPIXEL JEWEL 7 RGBW',
    header=[('PWR', '+5V'), ('GND', 'GND'), ('DIN', 'RING_DIN_R'), ('DOUT', None)],
    sockets={'J1': '+5V', 'J10': 'RING_DIN'},
    note='Adafruit NeoPixel Jewel 7, RGBW, powered from USB 5V (J1).\n'
         '3.3 V data into 5 V pixels usually works; if it glitches, add a\n'
         '74AHCT125 level shifter. R1 (300-500 ohm) sits at the ring.\n'
         'Recommended, not fitted on the breadboard: 100-1000 uF across\n'
         'PWR/GND. Full RGBW is ~80 mA per pixel (~560 mA total), more\n'
         'than USB supplies: firmware caps brightness at 48/255.\n'
         'Draws the status light (LedState); never the only signal.\n'
         'Firmware: Sigil PlatformIO env sigil (E-ink build).')

# Each variant: project name, root sheet UUID, the display module's header in
# physical order (pin 1 first) as (silkscreen label, net, jumper wire colour),
# and the U1 socket -> net map for the display. Header order and wire colours
# are read from the owner's photos of the breadboard wiring (2026-09-24).
VARIANTS = {
    'Sigil_EInk': dict(
        uuid='f3084516-8a7e-4624-9c15-ca3477e318b9',
        title='Sigil Rev A - removable DevKit carrier, e-ink display',
        symbol='EPD_Module_Header', value='INLAND E-PAPER HEADER (8-PIN)',
        header=[('SDI', 'EPD_MOSI', 'blue'), ('SCLK', 'EPD_SCLK', 'purple'),
                ('CS', 'EPD_CS', 'gray'), ('D/C', 'EPD_DC', 'white'),
                ('RES', 'EPD_RST', 'black'), ('BUSY', 'EPD_BUSY', 'brown'),
                ('VCC', '+3V3', 'red'), ('GND', 'GND', 'orange')],
        sockets={'A8': 'EPD_DC', 'A9': 'EPD_CS', 'A11': 'EPD_SCLK', 'A14': 'EPD_BUSY',
                 'A17': 'EPD_RST', 'A18': 'EPD_MOSI'},
        joystick=True,
        jewel=True,
        note='Inland e-paper driver board, write-only (GxEPD2_213_B74); no MISO.\n'
             'J2 pins follow the board header, pin 1 = SDI (top).\n'
             'Board switches P1 (3 / 0.47) and P2 (5VIN / 3.3VIN): positions\n'
             'not recorded; with the 3.3 V supply, check P2.'),
    'Sigil_OLED': dict(
        uuid='a61c2f0e-3b7d-4e55-9c1a-5d0e7b8f2c41',
        title='Sigil Rev A - removable DevKit carrier, OLED display',
        symbol='OLED_Module_Header', value='INLAND 1.3" OLED HEADER (7-PIN)',
        header=[('GND', 'GND', 'olive'), ('VCC', '+3V3', 'black'),
                ('CLK', 'OLED_SCLK', 'white'), ('MOSI', 'OLED_MOSI', 'gray'),
                ('RES', 'OLED_RST', 'purple'), ('DC', 'OLED_DC', 'blue'),
                ('CS', 'OLED_CS', 'green')],
        sockets={'A8': 'OLED_DC', 'A9': 'OLED_CS', 'A11': 'OLED_SCLK',
                 'A17': 'OLED_RST', 'A18': 'OLED_MOSI'},
        note='Inland 1.3" OLED V2.0 (KS0056), 4-wire SPI, 3.3 V.\n'
             'SH1106 128x64 inferred from the vendor example.\n'
             'J2 pins follow the board header, pin 1 = GND (top).\n'
             'GPIO21 (A14, the e-ink BUSY) is unused.'),
}

def pin(num, name, x, y, angle, kind='passive'):
    return f'(pin {kind} line (at {x} {y} {angle}) (length 5.08) (name {q(name)} {fx(1.0)}) (number {q(num)} {fx(1.0)}))'
def custom(name, pins, width, top, bottom, ref='U'):
    return f'''(symbol "Sigil:{name}" (pin_names (offset 1.016)) (in_bom yes) (on_board yes)
      {prop('Reference', ref, 0, top+5)} {prop('Value', name, 0, top+2.5)}
      {prop('Footprint', '', 0, 0, True)}
      (symbol "{name}_0_1" (rectangle (start {-width} {top}) (end {width} {bottom}) (stroke (width 0) (type default)) (fill (type background))))
      (symbol "{name}_1_1" {''.join(pins)}))'''
def display_symbol(v):
    kinds = {'VCC': 'power_in', 'GND': 'passive', 'BUSY': 'output'}
    pins = [pin(str(i+1), name, -20.32, round(-i*5.08, 2), 0, kinds.get(name, 'input'))
            for i, (name, _, _) in enumerate(v['header'])]
    return custom(v['symbol'], pins, 15.24, 5.08, round(-5.08*len(v['header']), 2), 'J')
def joystick_symbol():
    kinds = {'+5V': 'power_in', 'GND': 'passive', 'VRX': 'output', 'VRY': 'output', 'SW': 'passive'}
    pins = [pin(str(i+1), name, -20.32, round(-i*5.08, 2), 0, kinds[name])
            for i, (name, _) in enumerate(JOYSTICK['header'])]
    return custom(JOYSTICK['symbol'], pins, 15.24, 5.08, round(-5.08*len(JOYSTICK['header']), 2), 'J')
def jewel_symbol():
    kinds = {'PWR': 'power_in', 'GND': 'passive', 'DIN': 'input', 'DOUT': 'output'}
    pins = [pin(str(i+1), name, -20.32, round(-i*5.08, 2), 0, kinds[name])
            for i, (name, _) in enumerate(JEWEL['header'])]
    return custom(JEWEL['symbol'], pins, 15.24, 5.08, round(-5.08*len(JEWEL['header']), 2), 'J')

# Pin types describe the DevKit, not one carrier's use of it, so both
# schematics embed the identical library symbol.
devkit_pins = []
for row, names, x, angle in [('J', J, -43.18, 0), ('A', A, 43.18, 180)]:
    for i, name in enumerate(names):
        pos = f'{row}{i+1}'
        devkit_pins.append(pin(pos, name, x, round(45.72-i*5.08, 2), angle,
                               'power_out' if pos in ('J1', 'J19') else 'passive'))
devkit = custom('ESP32_DevKit_38_RearReference', devkit_pins, 38.1, 50.8, -50.8)
buzz = custom('Buzzer_Logical_Interface', [pin('SIG','SIG',-20.32,0,0,'input'),pin('GND','GND',-20.32,-10.16,0)],15.24,5.08,-15.24,'J')
displays = {name: display_symbol(v) for name, v in VARIANTS.items()}
joystick = joystick_symbol()
jewel = jewel_symbol()
resistor = custom('Resistor_Series', [pin('1', '~', -7.62, 0, 0), pin('2', '~', 7.62, 0, 180)],
                  2.54, 1.27, -1.27, 'R')
(ROOT / 'Sigil.kicad_sym').write_text('(kicad_symbol_lib (version 20241209) (generator "Sigil")\n' + '\n'.join(
    s.replace('"Sigil:', '"', 1) for s in [devkit, buzz, *displays.values(), joystick, jewel, resistor]) + ')\n')
(ROOT / 'sym-lib-table').write_text('(sym_lib_table (lib (name "Sigil") (type "KiCad") (uri "${KIPRJMOD}/Sigil.kicad_sym") (options "") (descr "Sigil Rev A interfaces; no verified footprints")))\n')


def build(project, v):
    NS = uuid.UUID(v['uuid'])
    def uid(key): return str(uuid.uuid5(NS, key))
    has_joystick = v.get('joystick', False)
    nets = dict(COMMON_NETS, **v['sockets'])
    has_jewel = v.get('jewel', False)
    if has_joystick: nets.update(JOYSTICK['sockets'])
    if has_jewel: nets.update(JEWEL['sockets'])
    controls = ('Joystick J4, status ring J5.' if has_joystick
                else 'Buttons and LEDs removed pending redesign.')
    out = [f'(kicad_sch (version 20260306) (generator "eeschema") (uuid "{NS}") (paper "A3")',
           f'(title_block (title {q(v["title"])}) (rev "A electrical draft") (comment 1 "Rear-photo socket numbering. {controls}"))',
           '(lib_symbols\n' + '\n'.join([devkit, buzz, displays[project]] + ([joystick] if has_joystick else [])
                                       + ([jewel, resistor] if has_jewel else [])) + ')']
    def note(text, x, y, size=1.27):
        out.append(f'(text {q(text)} (at {x} {y} 0) (effects (font (size {size} {size})) (justify left top)) (uuid "{uid(text)}"))')
    def wire(x, y, x2, y2):
        out.append(f'(wire (pts (xy {x} {y}) (xy {x2} {y2})) (stroke (width 0) (type default)) (uuid "{uid(str((x,y,x2,y2)))}"))')
    def label(net, x, y):
        out.append(f'(label {q(net)} (at {x} {y} 0) (effects (font (size 1.27 1.27)) (justify left bottom)) (uuid "{uid(net+str((x,y)))}"))')
    def nc(x, y): out.append(f'(no_connect (at {x} {y}) (uuid "{uid("nc"+str((x,y)))}"))')
    pin_numbers = {
        'Sigil:ESP32_DevKit_38_RearReference': [f'{r}{i}' for r in 'JA' for i in range(1, 20)],
        f'Sigil:{v["symbol"]}': [str(i+1) for i in range(len(v['header']))],
'Sigil:Buzzer_Logical_Interface': ['SIG', 'GND'],
        f'Sigil:{JOYSTICK["symbol"]}': [str(i+1) for i in range(len(JOYSTICK['header']))],
        f'Sigil:{JEWEL["symbol"]}': [str(i+1) for i in range(len(JEWEL['header']))],
        'Sigil:Resistor_Series': ['1', '2'],
    }
    def instance(lib, ref, value, x, y, top, on=True):
        fields = prop('Reference', ref, x, y-top) + prop('Value', value, x, y-top+2.54)
        # Explicit pin UUIDs; KiCad would otherwise invent random ones on every load.
        pin_uuids = ''.join(f'(pin {q(n)} (uuid "{uid(ref+"/"+n)}"))' for n in pin_numbers[lib])
        out.append(f'''(symbol (lib_id "{lib}") (at {x} {y} 0) (unit 1) (in_bom {"yes" if on else "no"}) (on_board {"yes" if on else "no"}) (dnp no)
      (uuid "{uid(ref)}") {fields} {prop('Footprint','',x,y,True)} {pin_uuids}
      (instances (project "{project}" (path "/{NS}" (reference "{ref}") (unit 1)))))''')

    instance('Sigil:ESP32_DevKit_38_RearReference', 'U1', 'REMOVABLE ESP32 DEVKIT / 2 x 19', 88.9, 101.6, 58.42)
    for row, names, x in [('J', J, 45.72), ('A', A, 132.08)]:
        for i in range(len(names)):
            y = round(55.88+i*5.08, 2); pos = f'{row}{i+1}'
            if pos in nets:
                end = round(x+(-20.32 if row == 'J' else 5.08), 2)
                wire(x, y, end, y); label(nets[pos], end, y)
            else: nc(x, y)
    note('BACK / REAR PHOTO VIEW\nJ1 (5V) top-left; A1 (CLK) top-right\nSocket IDs are immutable; this is NOT a footprint view.', 28, 21, 1.5)
    note(('USB-powered DevKit; J1 / 5V feeds only the status ring (J5).' if has_jewel
          else 'USB-powered DevKit; J1 / 5V unused on carrier.') + '\nJ19 supplies +3V3. All three GND sockets connected.\nOnboard USB-UART, regulator, BOOT and EN retained.\nPAIR is the onboard BOOT button (GPIO0, A6): no carrier wiring; A6 stays NC.', 28, 157)

    note('DISPLAY', 175, 40, 1.5)
    dx, dy = 238.76, 55.88
    instance(f'Sigil:{v["symbol"]}', 'J2', v['value'], dx, dy, 12.7, on=False)
    for i, (_, net, colour) in enumerate(v['header']):
        y = round(dy+i*5.08, 2); wire(round(dx-20.32, 2), y, 190.5, y); label(net, 190.5, y)
        note(colour + ' wire', 204.47, round(y-1.52, 2), 1.0)
    note('Wire colours are the breadboard jumpers in the owner photos\n(2026-09-24), not a harness specification.\n' + v['note'],
         175, round(dy+5.08*len(v['header'])+5.08, 2))

    note('BUZZER', 175, 125, 1.5)
    wire(218.44, 139.7, 198.12, 139.7); label('BUZZER', 198.12, 139.7)
    wire(218.44, 149.86, 198.12, 149.86); label('GND', 198.12, 149.86)
    instance('Sigil:Buzzer_Logical_Interface', 'J3', 'BUZZER LOGICAL ONLY', 238.76, 139.7, 12.7, on=False)
    note('J3 is an unresolved load interface.\nConfirm transducer/driver, current and protection.\nNo direct-drive suitability is assumed.', 175, 162)

    if has_joystick:
        note('CONTROLS / JOYSTICK', 290, 40, 1.5)
        jx, jy = 353.06, 55.88
        instance(f'Sigil:{JOYSTICK["symbol"]}', 'J4', JOYSTICK['value'], jx, jy, 12.7, on=False)
        for i, (_, net) in enumerate(JOYSTICK['header']):
            y = round(jy+i*5.08, 2); wire(round(jx-20.32, 2), y, 304.8, y); label(net, 304.8, y)
        note(JOYSTICK['note'], 290, round(jy+5.08*len(JOYSTICK['header'])+5.08, 2))

    if has_jewel:
        note('STATUS RING', 290, 125, 1.5)
        rx, ry = 353.06, 139.7
        instance(f'Sigil:{JEWEL["symbol"]}', 'J5', JEWEL['value'], rx, ry, 12.7, on=False)
        pin_x = round(rx-20.32, 2)
        for i, (name, net) in enumerate(JEWEL['header']):
            y = round(ry+i*5.08, 2)
            if net is None:
                nc(pin_x, y)
            elif name == 'DIN':
                # GPIO26 -> R1 -> DIN; R1's pins end at 309.88 and 325.12.
                instance('Sigil:Resistor_Series', 'R1', '330R', 317.5, y, 5.08, on=False)
                wire(309.88, y, 304.8, y); label('RING_DIN', 304.8, y)
                wire(325.12, y, 327.66, y); wire(327.66, y, pin_x, y); label(net, 327.66, y)
            else:
                wire(pin_x, y, 304.8, y); label(net, 304.8, y)
        note(JEWEL['note'], 290, round(ry+5.08*len(JEWEL['header'])+5.08, 2))

    hold6 = ('6. Discrete LEDs and the old buttons are removed; J4 is the joystick (its "+5V" pin MUST be fed 3.3 V)'
             + ('; J5 is the NeoPixel status ring on USB 5V.\n' if has_jewel else '.\n')
             if has_joystick else
             '6. Buttons and LEDs are removed while the controls are redesigned; their GPIOs are NC here.\n')
    note('SCHEMATIC REVIEW / RELEASE HOLDS\n'
         '1. U1 uses A1-A19 / J1-J19 from SigilBackMarked.png (BACK view); never exchange row identities.\n'
         '2. No DevKit footprint assigned: measure pitch, row spacing, outline, USB-C overhang, holes, socket height and keepouts.\n'
         '3. Future footprint: two 1x19 female sockets, unmistakable A1/J1 marks; verify insertion from carrier component side.\n'
         '4. Keep USB-C, BOOT and EN/reset accessible; preserve antenna/component clearances after measurement.\n'
         '5. Display/joystick/ring headers and R1 have no footprint and the buzzer is a logical interface; all are excluded from PCB and BOM.\n'
         + hold6 +
         '7. Rev A is an electrical draft, NOT fabrication-ready. Power through DevKit USB; no second supply designed.', 28, 211)
    out.append('(embedded_fonts no))')
    sch = ROOT / f'{project}.kicad_sch'
    sch.write_text('\n'.join(out) + '\n')
    # Re-save in KiCad's own layout so the file matches what the editor writes and
    # later hand edits diff cleanly. Skipped (compact output) without kicad-cli.
    if shutil.which('kicad-cli'):
        subprocess.run(['kicad-cli', 'sch', 'upgrade', '--force', str(sch)], check=True)


for project, v in VARIANTS.items():
    build(project, v)
