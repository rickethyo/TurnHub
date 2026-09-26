"""Verify exported KiCad XML netlists against firmware and the rear-photo mapping.

Usage: python verify_schematic.py eink.xml oled.xml [adapter.xml]
  (export each with: kicad-cli sch export netlist --format kicadxml -o <xml> <sch>)
Writes CROSS_CHECK.md only after all assertions pass for both schematics.
"""
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parents[2]
main = (REPO/'Sigil/src/main.cpp').read_text()
epd_header = (REPO/'Sigil/include/epaper_display.h').read_text()
epd_source = (REPO/'Sigil/src/epaper_display.cpp').read_text()
oled_config = (REPO/'Sigil/include/oled_config.h').read_text()

mapping = {}
for row, seq in [('A','CLK SD0 SD1 GPIO15 GPIO2 GPIO0 GPIO4 GPIO16 GPIO17 GPIO5 GPIO18 GPIO19 GND GPIO21 RXD0 TXD0 GPIO22 GPIO23 GND'),('J','5V CMD SD3 SD2 GPIO13 GND GPIO12 GPIO14 GPIO27 GPIO26 GPIO25 GPIO33 GPIO32 GPIO35 GPIO34 SVN SVP EN 3V3')]:
    mapping.update({f'{row}{i}':n for i,n in enumerate(seq.split(),1)})
POWER = {'A13': 'GND', 'A19': 'GND', 'J6': 'GND', 'J19': '+3V3'}

# (function, GPIO, socket, net, firmware evidence, how to find it)
BUZZER = ('Buzzer signal', 33, 'J12', 'BUZZER', 'BUZZER_PIN = 33', main)
# Joystick header J4 (E-ink only): module order, "+5V" deliberately on +3V3.
JOYSTICK_HEADER = [('1','GND','GND','not recorded'), ('2','+5V','+3V3','not recorded'),
    ('3','VRX','JOY_X','not recorded'), ('4','VRY','JOY_Y','not recorded'),
    ('5','SW','JOY_SW','not recorded')]
JOYSTICK_ROWS = [
    ('Joystick SW (Select)', 32, 'J13', 'JOY_SW', '32 /* SW (J13) */', main),
    ('Joystick VRY', 35, 'J14', 'JOY_Y', 'JOYSTICK_Y_PIN = 35', main),
    ('Joystick VRX', 34, 'J15', 'JOY_X', 'JOYSTICK_X_PIN = 34', main)]
# NeoPixel Jewel 7 header J5 (E-ink only): data from GPIO26 through R1;
# Data Output is unconnected (None). USB 5V from J1 powers it.
JEWEL_HEADER = [('1','+5V','+5V','not recorded'), ('2','DIN','RING_DIN_R','not recorded'),
    ('3','GND','GND','not recorded')]
JEWEL_ROWS = [('Status ring data (via R1)', 26, 'J10', 'RING_DIN', 'STATUS_RING_PIN = 26', main)]
EINK_POWER = {'J1': '+5V'}
# OLED keys: five discrete pushbuttons, pin 1 on the key net, pin 2 on GND.
BUTTONS = [('SW1','Up','KEY_UP',25,'J11'), ('SW2','Down','KEY_DOWN',27,'J9'),
    ('SW3','Left','KEY_LEFT',19,'A12'), ('SW4','Right','KEY_RIGHT',21,'A14'),
    ('SW5','Select','KEY_SELECT',32,'J13')]
BUTTON_ROWS = [(f'{key} button ({ref})', gpio, sock, net, f'{gpio} /* {key}, {sock} */', main)
    for ref, key, net, gpio, sock in BUTTONS]
VARIANTS = [
    # Display header in physical order: (pin number, silkscreen label, net, wire color).
    ('Sigil_EInk', 'E-ink', 'EPD', [
        ('1','SDI','EPD_MOSI','blue'), ('2','SCLK','EPD_SCLK','purple'), ('3','CS','EPD_CS','gray'),
        ('4','D/C','EPD_DC','white'), ('5','RES','EPD_RST','black'), ('6','BUSY','EPD_BUSY','brown'),
        ('7','VCC','+3V3','red'), ('8','GND','GND','orange')], [
        ('EPD DC', 16, 'A8', 'EPD_DC', 'EPD_DC = 16', epd_header),
        ('EPD CS', 17, 'A9', 'EPD_CS', 'EPD_CS = 17', epd_header),
        ('EPD clock', 18, 'A11', 'EPD_SCLK', 'SPI.begin(18, 19, 23, EPD_CS)', epd_source),
        ('EPD BUSY', 21, 'A14', 'EPD_BUSY', 'EPD_BUSY = 21', epd_header),
        ('EPD reset', 22, 'A17', 'EPD_RST', 'EPD_RST = 22', epd_header),
        ('EPD data', 23, 'A18', 'EPD_MOSI', 'SPI.begin(18, 19, 23, EPD_CS)', epd_source)
    ] + JOYSTICK_ROWS + JEWEL_ROWS),
    ('Sigil_OLED', 'OLED', 'OLED', [
        ('1','GND','GND','olive'), ('2','VCC','+3V3','black'), ('3','CLK','OLED_SCLK','white'),
        ('4','MOSI','OLED_MOSI','gray'), ('5','RES','OLED_RST','purple'), ('6','DC','OLED_DC','blue'),
        ('7','CS','OLED_CS','green')], [
        ('OLED DC', 16, 'A8', 'OLED_DC', 'c.dc = 16', oled_config),
        ('OLED CS', 17, 'A9', 'OLED_CS', 'c.cs = 17', oled_config),
        ('OLED clock', 18, 'A11', 'OLED_SCLK', 'c.sclk = 18', oled_config),
        ('OLED reset', 22, 'A17', 'OLED_RST', 'c.reset = 22', oled_config),
        ('OLED data', 23, 'A18', 'OLED_MOSI', 'c.mosi = 23', oled_config)] + BUTTON_ROWS + JEWEL_ROWS
        + [('Display-type strap (to GND = OLED)', 4, 'A7', 'GND', 'HW_TYPE_STRAP_PIN = 4', main)]),
]

def evidence_found(text, source):
    # Tolerate `constexpr int8_t EPD_DC = 16;` style declarations and spacing.
    name, _, value = text.partition(' = ')
    if not value: return text in source
    return re.search(re.escape(name) + r'\s*=\s*' + re.escape(value) + r'\s*;', source) is not None

report = ['# Sigil Rev A cross-check tables', '',
    'Verified against exported KiCad netlists, current firmware, and the user-supplied rear-photo sequence. YES means GPIO/socket/net consistency; it does not verify peripheral parts or mechanical dimensions.', '',
    'Discrete LEDs and the old buttons are gone; their GPIOs are explicitly NC. The Jewel moves to its own adapter board (with C1, 470 uF) on a pigtail into J5; C2 (10 uF) on +3V3, and U2 (74AHCT1G125, decoupled by C3) lifts the ring data to 5 V. GPIO4 (A7) is the display-type strap: open on the E-ink board, tied to GND on the OLED board. The E-ink schematic carries the analog joystick (J4) and the NeoPixel status ring (J5) instead; the OLED schematic carries five discrete pushbuttons (SW1-SW5) and the same ring.', '']
for project, title, prefix, header, rows in VARIANTS:
    xml = ET.parse(sys.argv[1 if project == 'Sigil_EInk' else 2]).getroot()
    nets = {n.get('name'): {(p.get('ref'),p.get('pin')) for p in n.findall('node')} for n in xml.findall('nets/net')}
    by_pin = {p: name for name, pins in nets.items() for p in pins}
    actual = {n.get('pin'):n for n in xml.findall('nets/net/node') if n.get('ref')=='U1'}
    assert set(actual)==set(mapping), 'U1 must have exactly the 38 socket positions'
    for p,name in mapping.items():
        assert actual[p].get('pinfunction').split('_')[0]==name, (project,p,name,actual[p].attrib)
    rows = rows + [BUZZER]
    for function,gpio,pos,net,evidence,source in rows:
        assert evidence_found(evidence, source), (project, evidence)
        assert mapping[pos]=='GPIO'+str(gpio), (project, pos)
        assert by_pin[('U1',pos)]=='/'+net, (project, pos, by_pin.get(('U1',pos)))
    has_joystick = project == 'Sigil_EInk'
    has_buttons = project == 'Sigil_OLED'
    has_jewel = True  # Both drafts carry the status ring.
    power = dict(POWER, **(EINK_POWER if has_jewel else {}))
    for pos,net in power.items(): assert by_pin[('U1',pos)]=='/'+net, (project, pos)
    headers = [('J2', header)] + ([('J4', JOYSTICK_HEADER)] if has_joystick else []) \
        + ([('J5', JEWEL_HEADER)] if has_jewel else [])
    for ref, pins in headers:
        nodes = {n.get('pin'):n for n in xml.findall('nets/net/node') if n.get('ref')==ref}
        assert set(nodes)=={h[0] for h in pins}, (project, ref, sorted(nodes))
        for num,name,net,_ in pins:
            assert nodes[num].get('pinfunction').split('_')[0]==name, (project, ref, num, name, nodes[num].attrib)
            if net is None: assert by_pin[(ref,num)].startswith('unconnected-'), (project, ref, num)
            else: assert by_pin[(ref,num)]=='/'+net, (project, ref, num, net)
    if has_buttons:
        for ref, _, net, _, _ in BUTTONS:
            assert by_pin[(ref,'1')]=='/'+net and by_pin[(ref,'2')]=='/GND', (project, ref)
    if has_jewel:
        for num, net in [('1','GND'),('2','RING_DIN'),('3','GND'),('4','RING_DIN_5V'),('5','+5V')]:
            assert by_pin[('U2',num)]=='/'+net, (project, 'U2', num)
        assert by_pin[('C3','1')]=='/+5V' and by_pin[('C3','2')]=='/GND', (project, 'C3')
        assert by_pin[('R1','1')]=='/RING_DIN_5V' and by_pin[('R1','2')]=='/RING_DIN_R', (project, 'R1')
    assert by_pin[('J3','1')]=='/BUZZER' and by_pin[('J3','2')]=='/GND'
    assert by_pin[('C2','1')]=='/+3V3' and by_pin[('C2','2')]=='/GND', (project, 'C2')
    # E-ink leaves the GPIO4 strap open (checked NC below); OLED ties it to GND (a row).
    used = {r[2] for r in rows} | set(power)
    for p in set(mapping)-used: assert 'no_connect' in actual[p].get('pintype'), (project, p)
    refs = {c.get('ref') for c in xml.findall('components/comp')}
    for c in xml.findall('components/comp'):
        assert c.find('footprint') is not None, (project, c.get('ref'), 'needs a footprint')
    assert refs == {'U1','J2','J3','C2'} | ({'J4'} if has_joystick else set()) \
        | ({'J5','R1','U2','C3'} if has_jewel else set()) | ({r[0] for r in BUTTONS} if has_buttons else set()), (project, refs)
    for n,pins in nets.items():
        if not n.startswith('unconnected-'): assert len(pins)>=2,(project,n,pins)
    assert xml.findtext("components/comp[@ref='U1']/footprint")=='Sigil:ESP32_DevKit_38_Socket_Row25.4mm'

    report += [f'## {title} ({project}.kicad_sch)', '',
        '| Function | GPIO | DevKit socket position | Schematic net | Firmware evidence | Match? |', '|---|---|---|---|---|---|']
    for f,g,p,n,e,_ in rows: report.append(f'| {f} | GPIO{g} | {p} | {n} | `{e}` | YES |')
    report += ['| Ground | — | A13, A19, J6 | GND | Hardware ground | YES |',
               '| 3.3 V rail | — | J19 | +3V3 | DevKit supply; not a GPIO | N/A |']
    if not has_buttons: report.append('| Display-type strap (open = E-ink) | GPIO4 | A7 | NC | `HW_TYPE_STRAP_PIN = 4` | YES |')
    if has_jewel: report.append('| USB 5 V (status ring) | — | J1 | +5V | DevKit USB supply; not a GPIO | N/A |')
    report.append('')
    report.append('Unused (NC) sockets: ' + ', '.join(p for p in mapping if p not in used) + '.')
    report += ['', f'Display header J2, in physical order (pin 1 at the top of the module header). Wire colors are the breadboard jumpers in the owner photos (2026-09-24), not a harness specification.', '',
        '| Header pin | Silkscreen | Net | Wire color |', '|---|---|---|---|']
    report += [f'| {num} | {name} | {net} | {color} |' for num,name,net,color in header]
    report.append('')
    if has_joystick:
        report += ['Joystick header J4, in module order. The "+5V" pin is fed from +3V3 on purpose: VRX/VRY swing to the supply and the ESP32 ADC must not see 5 V. Firmware: Sigil env `sigil`, the E-ink build; the directions and click are the five menu keys.', '',
            '| Header pin | Module label | Net |', '|---|---|---|']
        report += [f'| {num} | {name} | {net} |' for num,name,net,_ in JOYSTICK_HEADER]
        report.append('')
    if has_buttons:
        report += ['Menu keys SW1-SW5: five discrete momentary pushbuttons, each from its GPIO (pin 1) to one shared GND rail (pin 2), using the ESP32 internal pull-ups; no resistors. On a 4-leg tactile switch, pins 1 and 2 are legs on opposite sides (across the gap). Firmware: Sigil env `sigil-oled`, `KEY_PINS` in `main.cpp`.', '',
            '| Switch | Key | GPIO | DevKit socket (breadboard) | Net (pin 1) | Pin 2 |', '|---|---|---|---|---|---|']
        report += [f'| {ref} | {key} | GPIO{gpio} | {sock} | {net} | GND |' for ref,key,net,gpio,sock in BUTTONS]
        report.append('')
    if has_jewel:
        report += ['Status ring cable J5 (JST-XH, 3 pins) to the Jewel adapter board, on USB 5 V. Data runs GPIO26 (J10, net RING_DIN) through U2 (74AHCT1G125, 5 V buffer, net RING_DIN_5V) and R1 (330 ohm) to DIN (net RING_DIN_R). Pin numbers are logical; the pads are labelled. Firmware caps brightness at 48/255.', '',
            '| J5 pin | Signal | Net |', '|---|---|---|']
        report += [f'| {num} | {name} | {net or "NC"} |' for num,name,net,_ in JEWEL_HEADER]
        report.append('')
    report += ['Parts and footprints (U1 is from published Inland DevKit dimensions; caliper-check before ordering):', '',
        '| Ref | Value | Footprint |', '|---|---|---|']
    report += [f"| {c.get('ref')} | {c.findtext('value')} | `{c.findtext('footprint') or 'none'}` |"
               for c in sorted(xml.findall('components/comp'), key=lambda c: c.get('ref'))]
    report.append('')

report += ['## Socket positions', '', '| Socket position | DevKit silkscreen pin |', '|---|---|']
report += [f'| {p} | {n} |' for p,n in mapping.items()]
report += ['', 'Unused means no carrier connection; onboard flash, UART, BOOT and EN circuitry may still use these signals.', '',
    'Validation: 38 unique socket positions per schematic; display, joystick and status ring (both), pushbuttons (OLED) display strap, U2/R1/C2/C3 and buzzer nets match firmware; power and all three grounds connected; every part has a footprint; every other socket explicitly NC; no dangling named nets. Peripheral interfaces remain unresolved; see README.md.', '']
# Jewel adapter (third netlist): Jewel pads, C1 and pigtail, same order as J5.
if len(sys.argv) > 3:
    xml = ET.parse(sys.argv[3]).getroot()
    nets = {n.get('name'): {(p.get('ref'),p.get('pin')) for p in n.findall('node')} for n in xml.findall('nets/net')}
    by_pin = {p: name for name, pins in nets.items() for p in pins}
    expect = {('J1','1'):'/+5V', ('J1','2'):'/GND', ('J1','3'):'/RING_DIN_R', ('J1','5'):'/GND',
              ('J2','1'):'/+5V', ('J2','2'):'/RING_DIN_R', ('J2','3'):'/GND',
              ('C1','1'):'/+5V', ('C1','2'):'/GND'}
    for k, net in expect.items(): assert by_pin[k]==net, ('adapter', k, by_pin.get(k))
    assert by_pin[('J1','4')].startswith('unconnected-'), 'adapter DOUT must be NC'
    assert [by_pin[('J2',n)] for n in '123'] == ['/'+net for _,_,net,_ in JEWEL_HEADER], 'pigtail order must match J5'
    for c in xml.findall('components/comp'): assert c.find('footprint') is not None, ('adapter', c.get('ref'))
    report += ['## Jewel adapter (Sigil_JewelAdapter.kicad_sch)', '',
        'The Jewel is soldered on pins on this board, LEDs up, with C1 (470 uF) and a 3-wire JST-XH pigtail in J2 whose order matches J5 on the Sigil boards. Pad positions: Adafruit-NeoPixel-Jewel-7 board file.', '',
        '| Ref | Pin | Name | Net |', '|---|---|---|---|']
    report += [f"| {n.get('ref')} | {n.get('pin')} | {n.get('pinfunction')} | {net.get('name').lstrip('/') if not net.get('name').startswith('unconnected') else 'NC'} |"
               for net in xml.findall('nets/net') for n in net.findall('node')]
    report.append('')

(ROOT/'CROSS_CHECK.md').write_text('\n'.join(report), encoding='utf-8')
print('PASS: both schematics match the socket mapping, firmware display/joystick/pushbutton/ring/buzzer pins, power, NC pins and the U1 socket footprint')
