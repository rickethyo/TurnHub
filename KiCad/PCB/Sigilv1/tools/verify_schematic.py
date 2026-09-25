"""Verify exported KiCad XML netlists against firmware and the rear-photo mapping.

Usage: python verify_schematic.py eink.xml oled.xml
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
JEWEL_HEADER = [('1','PWR','+5V','not recorded'), ('2','GND','GND','not recorded'),
    ('3','DIN','RING_DIN_R','not recorded'), ('4','DOUT',None,'not recorded')]
JEWEL_ROWS = [('Status ring data (via R1)', 26, 'J10', 'RING_DIN', 'STATUS_RING_PIN = 26', main)]
EINK_POWER = {'J1': '+5V'}
# OLED five-button d-pad header J4: each key a switch to GND.
DPAD_HEADER = [('1','UP','KEY_UP','not recorded'), ('2','DOWN','KEY_DOWN','not recorded'),
    ('3','LEFT','KEY_LEFT','not recorded'), ('4','RIGHT','KEY_RIGHT','not recorded'),
    ('5','SELECT','KEY_SELECT','not recorded'), ('6','GND','GND','not recorded')]
DPAD_ROWS = [
    ('D-pad Down', 27, 'J9', 'KEY_DOWN', '27 /* Down, J9 */', main),
    ('D-pad Up', 25, 'J11', 'KEY_UP', '25 /* Up, J11 */', main),
    ('D-pad Select', 32, 'J13', 'KEY_SELECT', '32 /* Select, J13 */', main),
    ('D-pad Left', 19, 'A12', 'KEY_LEFT', '19 /* Left, A12 */', main),
    ('D-pad Right', 21, 'A14', 'KEY_RIGHT', '21 /* Right, A14 */', main)]
VARIANTS = [
    # Display header in physical order: (pin number, silkscreen label, net, wire colour).
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
        ('OLED data', 23, 'A18', 'OLED_MOSI', 'c.mosi = 23', oled_config)] + DPAD_ROWS + JEWEL_ROWS),
]

def evidence_found(text, source):
    # Tolerate `constexpr int8_t EPD_DC = 16;` style declarations and spacing.
    name, _, value = text.partition(' = ')
    if not value: return text in source
    return re.search(re.escape(name) + r'\s*=\s*' + re.escape(value) + r'\s*;', source) is not None

report = ['# Sigil Rev A cross-check tables', '',
    'Verified against exported KiCad netlists, current firmware, and the user-supplied rear-photo sequence. YES means GPIO/socket/net consistency; it does not verify peripheral parts or mechanical dimensions.', '',
    'LEDs and the old buttons are not on either schematic while the controls are redesigned; their GPIOs are explicitly NC. The E-ink schematic carries the analog joystick (J4) and the NeoPixel status ring (J5) instead; the OLED schematic carries a five-button d-pad (J4).', '']
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
    has_dpad = project == 'Sigil_OLED'
    has_jewel = True  # Both drafts carry the status ring.
    power = dict(POWER, **(EINK_POWER if has_jewel else {}))
    for pos,net in power.items(): assert by_pin[('U1',pos)]=='/'+net, (project, pos)
    headers = [('J2', header)] + ([('J4', JOYSTICK_HEADER)] if has_joystick else []) \
        + ([('J5', JEWEL_HEADER)] if has_jewel else []) \
        + ([('J4', DPAD_HEADER)] if has_dpad else [])
    for ref, pins in headers:
        nodes = {n.get('pin'):n for n in xml.findall('nets/net/node') if n.get('ref')==ref}
        assert set(nodes)=={h[0] for h in pins}, (project, ref, sorted(nodes))
        for num,name,net,_ in pins:
            assert nodes[num].get('pinfunction').split('_')[0]==name, (project, ref, num, name, nodes[num].attrib)
            if net is None: assert by_pin[(ref,num)].startswith('unconnected-'), (project, ref, num)
            else: assert by_pin[(ref,num)]=='/'+net, (project, ref, num, net)
    if has_jewel:
        assert by_pin[('R1','1')]=='/RING_DIN' and by_pin[('R1','2')]=='/RING_DIN_R', (project, 'R1')
    assert by_pin[('J3','SIG')]=='/BUZZER' and by_pin[('J3','GND')]=='/GND'
    used = {r[2] for r in rows} | set(power)
    for p in set(mapping)-used: assert 'no_connect' in actual[p].get('pintype'), (project, p)
    refs = {c.get('ref') for c in xml.findall('components/comp')}
    assert refs == {'U1','J2','J3'} | ({'J4'} if has_joystick else set()) \
        | ({'J5','R1'} if has_jewel else set()) | ({'J4'} if has_dpad else set()), (project, refs)
    for n,pins in nets.items():
        if not n.startswith('unconnected-'): assert len(pins)>=2,(project,n,pins)
    assert xml.find("components/comp[@ref='U1']/footprint") is None

    report += [f'## {title} ({project}.kicad_sch)', '',
        '| Function | GPIO | DevKit socket position | Schematic net | Firmware evidence | Match? |', '|---|---|---|---|---|---|']
    for f,g,p,n,e,_ in rows: report.append(f'| {f} | GPIO{g} | {p} | {n} | `{e}` | YES |')
    report += ['| Ground | — | A13, A19, J6 | GND | Hardware ground | YES |',
               '| 3.3 V rail | — | J19 | +3V3 | DevKit supply; not a GPIO | N/A |']
    if has_jewel: report.append('| USB 5 V (status ring) | — | J1 | +5V | DevKit USB supply; not a GPIO | N/A |')
    report.append('')
    report.append('Unused (NC) sockets: ' + ', '.join(p for p in mapping if p not in used) + '.')
    report += ['', f'Display header J2, in physical order (pin 1 at the top of the module header). Wire colours are the breadboard jumpers in the owner photos (2026-09-24), not a harness specification.', '',
        '| Header pin | Silkscreen | Net | Wire colour |', '|---|---|---|---|']
    report += [f'| {num} | {name} | {net} | {colour} |' for num,name,net,colour in header]
    report.append('')
    if has_joystick:
        report += ['Joystick header J4, in module order. The "+5V" pin is fed from +3V3 on purpose: VRX/VRY swing to the supply and the ESP32 ADC must not see 5 V. Firmware: Sigil env `sigil`, the E-ink build; the directions and click are the five menu keys.', '',
            '| Header pin | Module label | Net |', '|---|---|---|']
        report += [f'| {num} | {name} | {net} |' for num,name,net,_ in JOYSTICK_HEADER]
        report.append('')
    if has_dpad:
        report += ['D-pad header J4: five momentary switches, each from its key to GND (internal pull-ups). Firmware: Sigil env `sigil-oled`; the keys drive the menu list.', '',
            '| Header pin | Key | Net |', '|---|---|---|']
        report += [f'| {num} | {name} | {net} |' for num,name,net,_ in DPAD_HEADER]
        report.append('')
    if has_jewel:
        report += ['Status ring J5: Adafruit NeoPixel Jewel 7 RGBW on USB 5 V. Data runs GPIO26 (J10, net RING_DIN) through R1 (330 ohm, at the ring) to DIN (net RING_DIN_R). Pin numbers are logical; the pads are labelled. Firmware caps brightness at 48/255.', '',
            '| Header pin | Pad label | Net |', '|---|---|---|']
        report += [f'| {num} | {name} | {net or "NC"} |' for num,name,net,_ in JEWEL_HEADER]
        report.append('')

report += ['## Socket positions', '', '| Socket position | DevKit silkscreen pin |', '|---|---|']
report += [f'| {p} | {n} |' for p,n in mapping.items()]
report += ['', 'Unused means no carrier connection; onboard flash, UART, BOOT and EN circuitry may still use these signals.', '',
    'Validation: 38 unique socket positions per schematic; display, joystick and status ring (E-ink), d-pad (OLED) and buzzer nets match firmware; power and all three grounds connected; every other socket explicitly NC; no buttons, LEDs or dangling named nets. Peripheral interfaces remain unresolved; see README.md.', '']
(ROOT/'CROSS_CHECK.md').write_text('\n'.join(report), encoding='utf-8')
print('PASS: both schematics match the socket mapping, firmware display/joystick/d-pad/ring/buzzer pins, power, NC pins and empty DevKit footprint')
