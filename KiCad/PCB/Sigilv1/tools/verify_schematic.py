"""Verify exported KiCad XML against firmware and the user's rear-photo mapping.

Usage: python verify_schematic.py exported.xml
Writes the two review tables only after all assertions pass.
"""
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parents[2]
main = (REPO/'Sigil/src/main.cpp').read_text()
header = (REPO/'Sigil/include/epaper_display.h').read_text()
display = (REPO/'Sigil/src/epaper_display.cpp').read_text()
xml = ET.parse(sys.argv[1]).getroot()
nets = {n.get('name'): {(p.get('ref'),p.get('pin')) for p in n.findall('node')} for n in xml.findall('nets/net')}
by_pin = {p: name for name, pins in nets.items() for p in pins}
rows = [
 ('PAIR',19,'J18','PAIR','PAIR_BUTTON'),
 ('Pass',26,'A20','BTN_PASS','PASS_BUTTON'),
 ('Action',25,'A19','BTN_ACTION','ACTION_BUTTON'),
 ('Pause / Win',32,'A17','BTN_PAUSE','PAUSE_WIN_BUTTON'),
 ('Red LED',13,'A25','LED_RED','RED_LED'),
 ('Green LED',14,'A22','LED_GREEN','GREEN_LED'),
 ('Blue LED',27,'A21','LED_BLUE','BLUE_LED'),
 ('Buzzer signal',33,'A18','BUZZER','BUZZER_PIN'),
 ('EPD DC',16,'J22','EPD_DC','EPD_DC'),
 ('EPD CS',17,'J21','EPD_CS','EPD_CS'),
 ('EPD clock',18,'J19','EPD_SCLK','SPI.begin(18, 19, 23, EPD_CS)'),
 ('EPD BUSY',21,'J16','EPD_BUSY','EPD_BUSY'),
 ('EPD reset',22,'J13','EPD_RST','EPD_RST'),
 ('EPD data',23,'J12','EPD_MOSI','SPI.begin(18, 19, 23, EPD_CS)'),
]
mapping = {}
for row, seq in [('J','CLK SD0 SD1 GPIO15 GPIO2 GPIO0 GPIO4 GPIO16 GPIO17 GPIO5 GPIO18 GPIO19 GND GPIO21 RXD0 TXD0 GPIO22 GPIO23 GND'),('A','5V CMD SD3 SD2 GPIO13 GND GPIO12 GPIO14 GPIO27 GPIO26 GPIO25 GPIO33 GPIO32 GPIO35 GPIO34 SVN SVP EN 3V3')]:
    mapping.update({f'{row}{30-i}':n for i,n in enumerate(seq.split(),1)})
actual = {n.get('pin'):n for n in xml.findall('nets/net/node') if n.get('ref')=='U1'}
assert set(actual)==set(mapping), 'U1 must have exactly the 38 socket positions'
for p,name in mapping.items():
    assert actual[p].get('pinfunction').split('_')[0]==name, (p,name,actual[p].attrib)
for function,gpio,pos,net,definition in rows:
    if definition.startswith('SPI'):
        assert definition in display
    else:
        assert re.search(r'\b'+definition+r'\s*=\s*'+str(gpio)+r'\s*;',main+'\n'+header)
    assert mapping[pos]=='GPIO'+str(gpio)
    assert by_pin[('U1',pos)]=='/'+net
for definition in ['PASS_BUTTON','ACTION_BUTTON','PAUSE_WIN_BUTTON','PAIR_BUTTON']:
    assert f'pinMode({definition}, INPUT_PULLUP)' in main
assert 'spiDetachMISO(SPI.bus(), 19)' in display
assert main.index('sigilDisplay.begin()') < main.index('pinMode(PAIR_BUTTON, INPUT_PULLUP)')
for ref,net in [('SW1','BTN_PASS'),('SW2','BTN_ACTION'),('SW4','PAIR'),('SW5','BTN_PAUSE')]:
    assert by_pin[(ref,'1')]=='/'+net and by_pin[(ref,'2')]=='/GND'
for pos in ['J17','J11','A24']: assert by_pin[('U1',pos)]=='/GND'
assert by_pin[('U1','A11')]=='/+3V3'
for i,net in enumerate(['LED_RED','LED_GREEN','LED_BLUE'],1):
    r,d=f'R{i}',f'D{i}'
    assert by_pin[(r,'1')]=='/'+net
    assert by_pin[(r,'2')]==by_pin[(d,'2')]
    assert by_pin[(d,'1')]=='/GND'
    assert xml.find(f"components/comp[@ref='{r}']/value").text=='330R'
for pin,net in zip(['VCC','GND','DIN','CLK','CS','DC','RST','BUSY'],['+3V3','GND','EPD_MOSI','EPD_SCLK','EPD_CS','EPD_DC','EPD_RST','EPD_BUSY']):
    assert by_pin[('J2',pin)]=='/'+net
assert by_pin[('J3','SIG')]=='/BUZZER' and by_pin[('J3','GND')]=='/GND'
used={r[2] for r in rows}|{'J17','J11','A24','A11'}
for p in set(mapping)-used: assert 'no_connect' in actual[p].get('pintype')
assert len(nets['/PAIR'])==2 and len(nets['/BTN_PAUSE'])==2
for n,pins in nets.items():
    if not n.startswith('unconnected-'): assert len(pins)>=2,(n,pins)
assert xml.find("components/comp[@ref='U1']/footprint") is None
report = ['# Sigil Rev A cross-check tables','',
 'Verified against exported KiCad netlist, current working firmware, and the user-supplied rear-photo sequence. YES means GPIO/socket/net consistency; it does not verify peripheral parts or mechanical dimensions.','',
 '## Table 1','', '| Function | GPIO | DevKit socket position | Schematic net | Firmware definition | Match? |', '|---|---|---|---|---|---|']
for f,g,p,n,d in rows: report.append(f'| {f} | GPIO{g} | {p} | {n} | `{d}`'+(f' = {g}' if not d.startswith('SPI') else '')+' | YES |')
report += ['| Ground | — | J17, J11, A24 | GND | Hardware ground | YES |', '| 3.3 V rail | — | A11 | +3V3 | DevKit supply; not a GPIO | N/A |','',
 '**PAIR: J18 / GPIO19 / PAIR; SW4 closes to GND, including J17. INPUT_PULLUP: released HIGH, pressed LOW. GPIO19 is detached from SPI MISO.**','',
 '**Pause / Win: A17 / GPIO32 / BTN_PAUSE; SW5 closes to GND. INPUT_PULLUP: released HIGH, pressed LOW. SW3 stays retired (it was the old GPIO4 auxiliary).**','',
 '## Table 2','', '| Socket position | DevKit silkscreen pin | Connected Sigil function | Used/Unused |', '|---|---|---|---|']
functions={r[2]:r[0] for r in rows}
for p,n in mapping.items():
    f=functions.get(p, 'Ground' if p in ('J17','J11','A24') else '3.3 V rail' if p=='A11' else '—')
    report.append(f'| {p} | {n} | {f} | {"Used" if p in used else "Unused (carrier NC)"} |')
report += ['', 'Unused means no carrier connection; onboard flash, UART, BOOT and EN circuitry may still use these signals.', '',
 'Validation: 38 unique socket positions; 14 firmware signal mappings; all four active-low switches; three 330R resistor/anode/cathode chains; all display logical signals; buzzer logical interface; all unused carrier pins explicitly NC. No dangling named nets. Peripheral interfaces remain unresolved; see README.md.','']
(ROOT/'CROSS_CHECK.md').write_text('\n'.join(report), encoding='utf-8')
print('PASS: socket mapping, firmware, netlist topology, polarity, NC pins and empty DevKit footprint')
