# TurnHub Verification Backlog

Facts and decisions that are still open: historical details to recover,
production decisions, and research. Prototype v1 field-test gates live in
[Prototype v1 verification](PROTOTYPE_V1_VERIFICATION.md); agreed but
unimplemented work lives in [Staged Changes](STAGED_CHANGES.md).

When an item is settled, record it in the relevant reference document and
delete it here.

Settled and removed 2026-10-02 (the owner reports the current hardware is
verified through playtesting and bench testing): the development boards and
pin maps ([Board Inventory](BOARD_INVENTORY.md), [Hardware Reference](HARDWARE_REFERENCE.md)),
pairing, re-pairing, unpair and factory reset ([Manual Pairing](MANUAL_PAIRING.md)),
device identity and authenticated pairing ([Secure Link](SECURE_LINK.md)),
the protocol version and capability bits ([Protocol and Pairing](PROTOCOL_AND_PAIRING.md)),
the Intent migration, recovery wiring and storage boundary, and signed Atlas
and Sigil OTA ([Sigil OTA](SIGIL_OTA.md)).

## Historical hardware (Generation 0 and the Raspberry Pi phase)

- [ ] Identify the board used for the first standalone prototype, and its pinout.
- [ ] Confirm the potentiometer wiring and timer range of the earliest
  adjustable timer, and when the DIP-switch timer replaced or joined it.
- [ ] Verify the earliest buzzer pin and type, and the original two-player LED
  wiring, including any red/green inversion before the Pi generation.
- [ ] Confirm the Raspberry Pi model used in the wired phase (history says 3B).
- [ ] Recover the wired ESP32 Module 0 pinout, whether Modules 0 and 1 used
  identical controls throughout, and the Pi-to-module USB/serial topology.
- [ ] Confirm when pause-on-module-disconnect and reconnect recovery arrived.
- [ ] Photograph and label surviving prototypes from each generation.

## Production decisions

- [ ] Production Sigil transport. ESP-NOW with the encrypted Secure Link is
  the prototype's transport; BLE and other local options have not been
  compared on latency, power, Sigil count, venue interference and complexity.
- [ ] Maximum physical Sigil count for product hardware (firmware allows 8).
- [ ] Two players per Sigil (shared e-ink seats): product feature, optional
  mode, or prototype only.
- [ ] Protocol version negotiation and firmware-compatibility messages, once
  released hardware needs backward compatibility.
- [ ] Atlas: separate USB-C power and data ports, input voltage and regulator
  requirements, battery/backup power, and whether authenticity needs a secure
  element.
- [ ] Sigil: battery chemistry and form factor; onboard, external or
  battery-swap charging.

## Mechanical and manufacturing

- [ ] First reproducible Atlas and Sigil electrical revisions before assigning
  `ATLAS-REV-A` / `SIGIL-REV-A`, with BOMs and schematics tied to each.
- [ ] Enclosure mounting points around actual PCB geometry; docking/contact
  geometry only after power and connectors settle.
- [ ] Design-for-assembly and repair review with the small-batch PCBA maker.

## Documentation

- [ ] Dated architecture diagrams for each generation.
- [ ] A decision log for major product decisions and reversals.
