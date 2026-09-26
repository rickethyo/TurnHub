"""Rebuild the two Rev A electrical drafts: Sigil_EInk and Sigil_OLED.

Both share the removable DevKit (U1), the power/ground sockets and the buzzer
interface; they differ in the display interface, and the E-ink draft also
carries the analog joystick (J4) that replaces the Pass/Action/Pause buttons
and the NeoPixel Jewel 7 status ring (J5, data through R1); the OLED draft
carries five discrete pushbuttons (SW1-SW5) on a common ground. Both are the
Sigil's five menu keys.
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
         'Directions + click are the five menu keys (on-screen compass).\n'
         'Axis direction depends on mounting; firmware can swap/invert.\n'
         'Firmware: Sigil PlatformIO env sigil (E-ink build).')

# OLED Sigil keys: five discrete momentary pushbuttons (no d-pad module),
# each from its GPIO to a common GND, using the ESP32's internal pull-ups.
# (ref, key, net, GPIO, DevKit socket). Firmware: KEY_PINS in main.cpp.
BUTTONS = dict(
    symbol='Pushbutton_SPST_NO',
    keys=[('SW1', 'UP', 'KEY_UP', 25, 'J11'), ('SW2', 'DOWN', 'KEY_DOWN', 27, 'J9'),
          ('SW3', 'LEFT', 'KEY_LEFT', 19, 'A12'), ('SW4', 'RIGHT', 'KEY_RIGHT', 21, 'A14'),
          ('SW5', 'SELECT', 'KEY_SELECT', 32, 'J13')],
    note='Five discrete momentary pushbuttons (normally open), each from\n'
         'its GPIO to one shared GND rail; the ESP32 internal pull-ups need\n'
         'no resistors. Pin 1 = GPIO side, pin 2 = GND. On a 4-leg tactile\n'
         'switch use two legs on opposite sides (across the gap).\n'
         'Up/Down move the menu list, Select or Right choose, Left closes.\n'
         'Pair is the DevKit BOOT button. Firmware: Sigil env sigil-oled.')
BUTTONS['sockets'] = {sock: net for _, _, net, _, sock in BUTTONS['keys']}

# Adafruit NeoPixel Jewel 7, RGBW (SK6812-type): 5 V power from the DevKit's
# USB 5V (J1), data from GPIO26 (J10) through a 330 ohm series resistor (R1)
# at the ring. Data Output is unused (no chained pixels). Pin numbers are
# logical; the Jewel's pads are labelled. Firmware: Sigil env `sigil`.
JEWEL = dict(
    symbol='NeoPixel_Jewel7_RGBW', value='NEOPIXEL JEWEL 7 RGBW',
    header=[('PWR', '+5V'), ('GND', 'GND'), ('DIN', 'RING_DIN_R'), ('DOUT', None)],
    sockets={'J1': '+5V', 'J10': 'RING_DIN'},
    note='Adafruit NeoPixel Jewel 7, RGBW, powered from USB 5V (J1).\n'
         '3.3 V data worked on the breadboard, but it is below spec, so\n'
         '74AHCT1G125 (U2) buffers GPIO26 to 5 V logic, then R1 (330 ohm).\n'
         'C1 (470 uF, + to PWR) is the recommended bulk capacitor across\n'
         'PWR/GND at the ring. Full RGBW is ~80 mA per pixel (~560 mA total), more\n'
         'than USB supplies: firmware caps brightness at 48/255.\n'
         'Draws the status light (LedState); never the only signal.\n'
         'Firmware: Sigil PlatformIO env sigil or sigil-oled.')

# Carrier footprints for the first PCB: all through-hole, hand-solderable,
# 2.54 mm sockets so each module plugs in like it does on the breadboard.
# U1 (the DevKit) stays unassigned until its row spacing is measured.
FOOTPRINTS = {
    'J2_8': 'Connector_PinSocket_2.54mm:PinSocket_1x08_P2.54mm_Vertical',
    'J2_7': 'Connector_PinSocket_2.54mm:PinSocket_1x07_P2.54mm_Vertical',
    'J3': 'Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical',
    'J4': 'Connector_PinSocket_2.54mm:PinSocket_1x05_P2.54mm_Vertical',
    'J5': 'Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical',
    'R1': 'Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P7.62mm_Horizontal',
    'C1': 'Capacitor_THT:CP_Radial_D8.0mm_P3.50mm',
    'C2': 'Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P5.00mm',
    'SW': 'Button_Switch_THT:SW_PUSH_6mm',
    'U1': 'Sigil:ESP32_DevKit_38_Socket_Row25.4mm',
    'U2': 'Package_TO_SOT_SMD:SOT-23-5',
    'C3': 'Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P5.00mm',
}

# U2: 74AHCT1G125 single buffer lifts the ring's 3.3 V data to 5 V logic
# (SK6812 wants about 0.7 x 5 V). Pin numbers are the SOT-23-5 pads; all pins
# are drawn on the left. (number, name, net, pin type)
SHIFTER = [('1', 'OE#', 'GND', 'input'), ('2', 'A', 'RING_DIN', 'input'),
           ('3', 'GND', 'GND', 'passive'), ('4', 'Y', 'RING_DIN_5V', 'output'),
           ('5', 'VCC', '+5V', 'power_in')]

# Each variant: project name, root sheet UUID, the display module's header in
# physical order (pin 1 first) as (silkscreen label, net, jumper wire color),
# and the U1 socket -> net map for the display. Header order and wire colors
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
        # GPIO4 (A7) display-type strap: open = E-ink (firmware pull-up).
        strap='open',
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
             'GPIO21 (A14, the e-ink BUSY) is the Right button (SW4) here.',
        buttons=True,
        jewel=True,
        # GPIO4 (A7) display-type strap: tied to GND = OLED.
        strap='gnd'),
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
def button_symbol():
    # Two contacts and a plunger over them; the pins end at x = +/-2.54.
    name = BUTTONS['symbol']
    st = '(stroke (width 0) (type default)) (fill (type none))'
    return (f'(symbol "Sigil:{name}" (pin_names (offset 1.016) (hide yes)) (in_bom yes) (on_board yes)\n'
            f'  {prop("Reference", "SW", 0, 5.08)} {prop("Value", name, 0, -3.81)}\n'
            f'  {prop("Footprint", "", 0, 0, True)}\n'
            f'  (symbol "{name}_0_1" (circle (center -2.032 0) (radius 0.508) {st})'
            f' (circle (center 2.032 0) (radius 0.508) {st})'
            f' (polyline (pts (xy -2.54 1.524) (xy 2.54 1.524)) {st})'
            f' (polyline (pts (xy 0 1.524) (xy 0 3.048)) {st}))\n'
            f'  (symbol "{name}_1_1" {pin("1", "~", -7.62, 0, 0)}{pin("2", "~", 7.62, 0, 180)}))')
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
buzz = custom('Buzzer_Logical_Interface', [pin('1','SIG',-20.32,0,0,'input'),pin('2','GND',-20.32,-10.16,0)],15.24,5.08,-15.24,'J')
displays = {name: display_symbol(v) for name, v in VARIANTS.items()}
joystick = joystick_symbol()
jewel = jewel_symbol()
button = button_symbol()
resistor = custom('Resistor_Series', [pin('1', '~', -7.62, 0, 0), pin('2', '~', 7.62, 0, 180)],
                  2.54, 1.27, -1.27, 'R')
shifter = custom('74AHCT1G125', [pin(n, name, -20.32, round(-i*5.08, 2), 0, kind)
                                  for i, (n, name, _, kind) in enumerate(SHIFTER)],
                 10.16, 5.08, -25.4, 'U')
capacitor = custom('Capacitor', [pin('1', '~', -7.62, 0, 0), pin('2', '~', 7.62, 0, 180)],
                   2.54, 1.27, -1.27, 'C')
(ROOT / 'Sigil.kicad_sym').write_text('(kicad_symbol_lib (version 20241209) (generator "Sigil")\n' + '\n'.join(
    s.replace('"Sigil:', '"', 1) for s in [devkit, buzz, *displays.values(), joystick, jewel, resistor, capacitor, shifter, button]) + ')\n')
# U1 footprint, carrier top view with the DevKit plugged in face up and its
# micro-USB end at the top. That view mirrors the rear photo, so the A row
# (A1 = CLK) is on the LEFT and the J row (J1 = 5V) on the RIGHT; A1/J1 are at
# the USB end (J1 is 5V, J19 is 3V3 by the antenna). Rows 25.4 mm apart, pitch
# 2.54 mm, outline 55.0 x 27.5 mm centred on the pins (published Inland specs;
# Needs verification with calipers).
def devkit_footprint():
    L = ['(footprint "ESP32_DevKit_38_Socket_Row25.4mm" (version 20241229) (generator "Sigil") (layer "F.Cu")',
         '(descr "Two 1x19 2.54 mm female sockets, rows 25.4 mm apart, for the removable Inland ESP32-WROOM-32D DevKit (micro-USB). Needs verification.")',
         '(attr through_hole)',
         '(property "Reference" "U1" (at 12.7 -8.5 0) (layer "F.SilkS") (effects (font (size 1 1) (thickness 0.15))))',
         '(property "Value" "ESP32 DevKit 38" (at 12.7 53 0) (layer "F.Fab") (effects (font (size 1 1) (thickness 0.15))))']
    def line(x1, y1, x2, y2, layer, w):
        L.append(f'(fp_line (start {x1} {y1}) (end {x2} {y2}) (stroke (width {w}) (type solid)) (layer "{layer}"))')
    def text(t, x, y, layer='F.SilkS'):
        L.append(f'(fp_text user "{t}" (at {x} {y} 0) (layer "{layer}") (effects (font (size 1 1) (thickness 0.15))))')
    x0, x1, y0, y1 = -1.05, 26.45, round(22.86-27.5, 2), round(22.86+27.5, 2)
    for a, b, c, d in [(x0, y0, x1, y0), (x1, y0, x1, y1), (x1, y1, x0, y1), (x0, y1, x0, y0)]:
        line(a, b, c, d, 'F.Fab', 0.1); line(a-0.25, b-0.25 if b == y0 else b+0.25, c+0.25 if c == x1 else c-0.25, d-0.25 if d == y0 else d+0.25, 'F.CrtYd', 0.05)
    for a, b, c, d in [(x0-0.12, y0-0.12, x1+0.12, y0-0.12), (x1+0.12, y0-0.12, x1+0.12, y1+0.12),
                       (x1+0.12, y1+0.12, x0-0.12, y1+0.12), (x0-0.12, y1+0.12, x0-0.12, y0-0.12)]:
        line(a, b, c, d, 'F.SilkS', 0.12)
    text('USB', 12.7, -2.5); text('A1', -2.8, 0); text('J1', 28.2, 0)
    text('DevKit outline: verify', 12.7, 22.86, 'F.Fab')
    for row, x in [('A', 0), ('J', 25.4)]:
        for i in range(19):
            shape = 'rect' if i == 0 else 'circle'
            L.append(f'(pad "{row}{i+1}" thru_hole {shape} (at {x} {round(i*2.54, 2)}) (size 1.7 1.7) (drill 1.0) (layers "*.Cu" "*.Mask"))')
    return '\n'.join(L) + ')\n'
(ROOT / 'Sigil.pretty').mkdir(exist_ok=True)
(ROOT / 'Sigil.pretty' / 'ESP32_DevKit_38_Socket_Row25.4mm.kicad_mod').write_text(devkit_footprint())
(ROOT / 'fp-lib-table').write_text('(fp_lib_table (version 7) (lib (name "Sigil") (type "KiCad") (uri "${KIPRJMOD}/Sigil.pretty") (options "") (descr "Sigil carrier footprints")))\n')
(ROOT / 'sym-lib-table').write_text('(sym_lib_table (lib (name "Sigil") (type "KiCad") (uri "${KIPRJMOD}/Sigil.kicad_sym") (options "") (descr "Sigil Rev A interfaces; no verified footprints")))\n')


def build(project, v):
    NS = uuid.UUID(v['uuid'])
    def uid(key): return str(uuid.uuid5(NS, key))
    has_joystick = v.get('joystick', False)
    nets = dict(COMMON_NETS, **v['sockets'])
    has_jewel = v.get('jewel', False)
    has_buttons = v.get('buttons', False)
    if has_buttons: nets.update(BUTTONS['sockets'])
    if v.get('strap') == 'gnd': nets['A7'] = 'GND'
    if has_joystick: nets.update(JOYSTICK['sockets'])
    if has_jewel: nets.update(JEWEL['sockets'])
    controls = ('Joystick J4, status ring J5.' if has_joystick
                else 'Buttons SW1-SW5, status ring J5.' if has_buttons
                else 'Buttons and LEDs removed pending redesign.')
    out = [f'(kicad_sch (version 20260306) (generator "eeschema") (uuid "{NS}") (paper "A3")',
           f'(title_block (title {q(v["title"])}) (rev "A electrical draft") (comment 1 "Rear-photo socket numbering. {controls}"))',
           '(lib_symbols\n' + '\n'.join([devkit, buzz, displays[project]] + ([joystick] if has_joystick else [])
                                       + ([jewel, resistor, shifter] if has_jewel else []) + [capacitor]
                                       + ([button] if has_buttons else [])) + ')']
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
        'Sigil:Buzzer_Logical_Interface': ['1', '2'],
        'Sigil:Capacitor': ['1', '2'],
        'Sigil:74AHCT1G125': [n for n, _, _, _ in SHIFTER],
        f'Sigil:{JOYSTICK["symbol"]}': [str(i+1) for i in range(len(JOYSTICK['header']))],
        f'Sigil:{JEWEL["symbol"]}': [str(i+1) for i in range(len(JEWEL['header']))],
        'Sigil:Resistor_Series': ['1', '2'],
        f'Sigil:{BUTTONS["symbol"]}': ['1', '2'],
    }
    def instance(lib, ref, value, x, y, top, on=True, fp=''):
        fields = prop('Reference', ref, x, y-top) + prop('Value', value, x, y-top+2.54)
        # Explicit pin UUIDs; KiCad would otherwise invent random ones on every load.
        pin_uuids = ''.join(f'(pin {q(n)} (uuid "{uid(ref+"/"+n)}"))' for n in pin_numbers[lib])
        out.append(f'''(symbol (lib_id "{lib}") (at {x} {y} 0) (unit 1) (in_bom {"yes" if on else "no"}) (on_board {"yes" if on else "no"}) (dnp no)
      (uuid "{uid(ref)}") {fields} {prop('Footprint',fp,x,y,True)} {pin_uuids}
      (instances (project "{project}" (path "/{NS}" (reference "{ref}") (unit 1)))))''')

    instance('Sigil:ESP32_DevKit_38_RearReference', 'U1', 'REMOVABLE ESP32 DEVKIT / 2 x 19', 88.9, 101.6, 58.42, fp=FOOTPRINTS['U1'])
    for row, names, x in [('J', J, 45.72), ('A', A, 132.08)]:
        for i in range(len(names)):
            y = round(55.88+i*5.08, 2); pos = f'{row}{i+1}'
            if pos in nets:
                end = round(x+(-20.32 if row == 'J' else 5.08), 2)
                wire(x, y, end, y); label(nets[pos], end, y)
            else: nc(x, y)
    note('BACK / REAR PHOTO VIEW\nJ1 (5V) top-left; A1 (CLK) top-right\nSocket IDs are immutable; this is NOT a footprint view.', 28, 21, 1.5)
    note(('USB-powered DevKit; J1 / 5V feeds only the status ring (J5).' if has_jewel
          else 'USB-powered DevKit; J1 / 5V unused on carrier.') + '\nJ19 supplies +3V3. All three GND sockets connected.\nOnboard USB-UART, regulator, BOOT and EN retained.\nPAIR is the onboard BOOT button (GPIO0, A6): no carrier wiring; A6 stays NC.'
         + ('\nDISPLAY STRAP: A7 (GPIO4) tied to GND = OLED board.' if v.get('strap') == 'gnd'
            else '\nDISPLAY STRAP: A7 (GPIO4) left open = E-ink board (firmware pull-up).'), 28, 157)

    note('DISPLAY', 175, 40, 1.5)
    dx, dy = 238.76, 55.88
    instance(f'Sigil:{v["symbol"]}', 'J2', v['value'], dx, dy, 12.7, fp=FOOTPRINTS[f'J2_{len(v["header"])}'])
    for i, (_, net, color) in enumerate(v['header']):
        y = round(dy+i*5.08, 2); wire(round(dx-20.32, 2), y, 190.5, y); label(net, 190.5, y)
        note(color + ' wire', 204.47, round(y-1.52, 2), 1.0)
    note('Wire colors are the breadboard jumpers in the owner photos\n(2026-09-24), not a harness specification.\n' + v['note'],
         175, round(dy+5.08*len(v['header'])+5.08, 2))

    note('BUZZER', 175, 125, 1.5)
    wire(218.44, 139.7, 198.12, 139.7); label('BUZZER', 198.12, 139.7)
    wire(218.44, 149.86, 198.12, 149.86); label('GND', 198.12, 149.86)
    instance('Sigil:Buzzer_Logical_Interface', 'J3', 'BUZZER 2-PIN HEADER', 238.76, 139.7, 12.7, fp=FOOTPRINTS['J3'])
    note('J3 is a 2-pin header (1 SIG, 2 GND) for the buzzer on leads.\nOwner: a cheap passive piezo disc, driven directly by GPIO33\n(LEDC tone). A magnetic transducer would need a driver instead.', 175, 162)

    note('DECOUPLING', 175, 180, 1.5)
    # C2 at the display/joystick headers: pins end at 190.5 (+3V3) and 205.74 (GND).
    instance('Sigil:Capacitor', 'C2', '10uF', 198.12, 190.5, 5.08, fp=FOOTPRINTS['C2'])
    wire(190.5, 190.5, 182.88, 190.5); label('+3V3', 182.88, 190.5)
    wire(205.74, 190.5, 213.36, 190.5); label('GND', 213.36, 190.5)
    if has_jewel:
        # C3 decouples U2 on +5V; same layout as C2, one row down.
        instance('Sigil:Capacitor', 'C3', '100nF', 198.12, 200.66, 5.08, fp=FOOTPRINTS['C3'])
        wire(190.5, 200.66, 182.88, 200.66); label('+5V', 182.88, 200.66)
        wire(205.74, 200.66, 213.36, 200.66); label('GND', 213.36, 200.66)
        # U2 level shifter: pin ends at x 246.38, labels at 228.6.
        ux, uy = 266.7, 180.34
        instance('Sigil:74AHCT1G125', 'U2', '74AHCT1G125', ux, uy, 12.7, fp=FOOTPRINTS['U2'])
        for i, (_, _, net, _) in enumerate(SHIFTER):
            y = round(uy+i*5.08, 2); wire(246.38, y, 228.6, y); label(net, 228.6, y)
    note('C2 10 uF X7R on +3V3' + ('; C3 100 nF at U2.' if has_jewel else '.'), 175, 205)

    if has_joystick:
        note('CONTROLS / JOYSTICK', 290, 40, 1.5)
        jx, jy = 353.06, 55.88
        instance(f'Sigil:{JOYSTICK["symbol"]}', 'J4', JOYSTICK['value'], jx, jy, 12.7, fp=FOOTPRINTS['J4'])
        for i, (_, net) in enumerate(JOYSTICK['header']):
            y = round(jy+i*5.08, 2); wire(round(jx-20.32, 2), y, 304.8, y); label(net, 304.8, y)
        note(JOYSTICK['note'], 290, round(jy+5.08*len(JOYSTICK['header'])+5.08, 2))

    if has_buttons:
        note('CONTROLS / FIVE PUSHBUTTONS', 290, 40, 1.5)
        # Each switch's pins end at 322.58 (GPIO side) and 337.82 (GND).
        for i, (ref, key, net, gpio, sock) in enumerate(BUTTONS['keys']):
            y = round(55.88+i*10.16, 2)
            instance(f'Sigil:{BUTTONS["symbol"]}', ref, f'{key} (GPIO{gpio}, {sock})', 330.2, y, 5.08, fp=FOOTPRINTS['SW'])
            wire(322.58, y, 304.8, y); label(net, 304.8, y)
            wire(337.82, y, 345.44, y); label('GND', 345.44, y)
        note(BUTTONS['note'], 290, round(55.88+10.16*len(BUTTONS['keys'])+2.54, 2))

    if has_jewel:
        note('STATUS RING', 290, 125, 1.5)
        rx, ry = 353.06, 139.7
        instance(f'Sigil:{JEWEL["symbol"]}', 'J5', JEWEL['value'], rx, ry, 12.7, fp=FOOTPRINTS['J5'])
        pin_x = round(rx-20.32, 2)
        for i, (name, net) in enumerate(JEWEL['header']):
            y = round(ry+i*5.08, 2)
            if net is None:
                nc(pin_x, y)
            elif name == 'DIN':
                # GPIO26 -> R1 -> DIN; R1's pins end at 309.88 and 325.12.
                instance('Sigil:Resistor_Series', 'R1', '330R', 317.5, y, 5.08, fp=FOOTPRINTS['R1'])
                wire(309.88, y, 304.8, y); label('RING_DIN_5V', 304.8, y)
                wire(325.12, y, 327.66, y); wire(327.66, y, pin_x, y); label(net, 327.66, y)
            else:
                wire(pin_x, y, 304.8, y); label(net, 304.8, y)
        # C1 bulk capacitor across the ring supply, below the J5 pins.
        cy = round(ry+5.08*len(JEWEL['header'])+2.54, 2)
        instance('Sigil:Capacitor', 'C1', '470uF 10V', 317.5, cy, 5.08, fp=FOOTPRINTS['C1'])
        wire(309.88, cy, 304.8, cy); label('+5V', 304.8, cy)
        wire(325.12, cy, 327.66, cy); label('GND', 327.66, cy)
        note(JEWEL['note'], 290, round(cy+7.62, 2))

    hold6 = ('6. Discrete LEDs and the old buttons are removed; J4 is the joystick (its "+5V" pin MUST be fed 3.3 V)'
             + ('; J5 is the NeoPixel status ring on USB 5V.\n' if has_jewel else '.\n')
             if has_joystick else
             '6. Discrete LEDs and the old buttons are removed; SW1-SW5 are the five menu pushbuttons (GPIO to a common GND); J5 is the NeoPixel status ring on USB 5V.\n'
             if has_buttons else
             '6. Buttons and LEDs are removed while the controls are redesigned; their GPIOs are NC here.\n')
    note('SCHEMATIC REVIEW / RELEASE HOLDS\n'
         '1. U1 uses A1-A19 / J1-J19 from SigilBackMarked.png (BACK view); never exchange row identities.\n'
         '2. U1 footprint (Sigil.pretty) is from published Inland specs: 2.54 mm pitch, 25.4 mm rows, 55.0 x 27.5 mm. Caliper-check and test-fit before ordering.\n'
         '3. Future footprint: two 1x19 female sockets, unmistakable A1/J1 marks; verify insertion from carrier component side.\n'
         '4. Keep micro-USB, BOOT and EN/reset accessible; preserve antenna/component clearances after measurement.\n'
         '5. Footprints: 2.54 mm sockets/headers, axial R1, radial C1-C3, SOT-23-5 U2' + (', 6 mm switches' if has_buttons else '') + '; U1 uses published Inland DevKit rows (25.4 mm): caliper-check first.\n'
         + hold6 +
         '7. Rev A is an electrical draft, NOT fabrication-ready until U1 is checked against the real board. Power through DevKit USB; no second supply designed.', 28, 211)
    out.append('(embedded_fonts no))')
    sch = ROOT / f'{project}.kicad_sch'
    sch.write_text('\n'.join(out) + '\n')
    # Re-save in KiCad's own layout so the file matches what the editor writes and
    # later hand edits diff cleanly. Skipped (compact output) without kicad-cli.
    if shutil.which('kicad-cli'):
        subprocess.run(['kicad-cli', 'sch', 'upgrade', '--force', str(sch)], check=True)


for project, v in VARIANTS.items():
    build(project, v)
