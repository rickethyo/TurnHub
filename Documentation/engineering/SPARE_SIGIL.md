# Spare Sigil

*Planned* (2026-10-06): written and host-tested; not yet tried on hardware.

A spare Sigil is a paired board that stays out of play until someone needs it.
It runs the `sigil-spare` build, one image for either board, and can be
brought back as an E-ink or OLED Sigil entirely over the air.

## Flow

1. In `tools\setup-boards.cmd`, pick the board and choose **Make spare**. The
   local list records it as `spare:<firmware it ran>`.
2. The next `flash-all` run uploads `sigil-spare` to it over USB (a plain
   PlatformIO upload in every mode: the spare image is never packaged or
   signed). Pairing and other NVS data are kept.
3. The spare announces itself to Atlas. To bring it back, use the Atlas
   portal's Sigil firmware page (the device shows `spare`): stage the package
   for its display and start the update. The spare reports which display its
   GPIO4 strap names, so only the matching package is offered.
4. At the next `flash-all` run with that board attached, the script hears the
   normal Sigil boot line, records the board as `sigil` or `sigil-oled`, and
   flashes it as that Sigil instead of turning it back into a spare.

A spare that is not paired pairs with the joystick hold or the BOOT button
while Atlas's pairing window is open. It has no screen to show a code, so it
sends `PairRequestSpare` and Atlas confirms it without the owner's code check
(owner's choice, 2026-10-06). Such a pairing is marked spare-only (NVS key
`p<slot>` beside the record): Atlas treats that Sigil as a spare whatever its
Hello says, and if it later announces itself as a normal Sigil (after its
update), Atlas forgets it, so it pairs again with the code check on its own
screen. A spare that kept a normal pairing from before needs no re-pair.

## Feature gate

1. **State owner:** Atlas owns everything, as for any Sigil. The spare holds
   only its pairing.
2. **Intent:** none new. Returning to service is the existing Sigil firmware
   update.
3. **Validator:** `SigilBus::handleReceive` drops every packet from a
   Sigil with `CAPABILITY_SPARE` except Hello and `SigilUpdateStatus`. The
   profile picker, the first-run "Sigil paired" check and the update count
   skip spares. On the Sigil, the spare build ignores every Atlas packet but
   Unpair, Factory reset and the update offer.
4. **Persistence owner:** unchanged. The spare's pairing stays in its NVS.
5. **Rendering clients:** the portal's Sigil firmware page labels a spare.
   The spare draws nothing, plays no sound and sends no input; its ring shows
   only the pairing and update animations.
6. **Protocol/contract change:** `CAPABILITY_SPARE` (0x40) in the Hello
   capability byte. With it, `CAPABILITY_DISPLAY_OLED` reports the GPIO4 strap.
   `PairRequestSpare` (52), the PairRequest2 layout under its own type.
   No HTTP schema change: `/api/devices` already carries `capabilities`.
7. **Third-party dependencies:** none new. The spare build leaves out both
   display libraries.
8. **Accessibility:** no player-facing change. A spare is identified in words
   on the portal, not by its ring.

## Limits

- The spare build has no screen driver, so an e-ink panel keeps whatever it
  showed last. The portal and the serial console (`SIGIL|SPARE|EINK` or
  `SIGIL|SPARE|OLED`) say what the board is.
- OTA accepts the same or a newer version only, so the package must be at
  least the spare's version (the spare uses the Sigil firmware version).
