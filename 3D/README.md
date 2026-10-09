# TurnHub enclosure print files

Current prototype enclosure STLs for Atlas and both Sigil display variants,
uploaded on 2026-10-09. This upload is the first rough-fit enclosure generation;
`v1` (Atlas) and `v12` (Sigil) are the model revisions in the filenames.
These revision labels are separate from the electronics generation and firmware
version.

## Atlas: `atlas_enclosure_v1_print_set/`

Print one shell and one base plate. The logo inlay is a separate cosmetic part.

| File | Part | Mesh extents X × Y × Z* |
| --- | --- | --- |
| [atlas_shell_v1.stl](atlas_enclosure_v1_print_set/atlas_shell_v1.stl) | Main enclosure shell | 98.00 × 90.95 × 43.34 |
| [atlas_base_plate_v1.stl](atlas_enclosure_v1_print_set/atlas_base_plate_v1.stl) | Base plate | 98.00 × 80.00 × 8.00 |
| [atlas_shell_logo_inlay_v1.stl](atlas_enclosure_v1_print_set/atlas_shell_logo_inlay_v1.stl) | TurnHub logo inlay | 48.26 × 12.00 × 0.60 |

The [`tests/`](atlas_enclosure_v1_print_set/tests/) folder contains smaller pieces
for checking individual interfaces before committing to a full shell:

| File | Fit to check |
| --- | --- |
| [test_screen_face_v1.stl](atlas_enclosure_v1_print_set/tests/test_screen_face_v1.stl) | Display opening and screen seating |
| [test_sd_v1.stl](atlas_enclosure_v1_print_set/tests/test_sd_v1.stl) | microSD access |
| [test_speaker_v1.stl](atlas_enclosure_v1_print_set/tests/test_speaker_v1.stl) | Speaker fit and opening |
| [test_usb_v1.stl](atlas_enclosure_v1_print_set/tests/test_usb_v1.stl) | USB access and cable clearance |

## Sigil: `sigil_enclosure_v12_print_set/`

Both variants share the sloped shell and back plate. Print **one screen plate
matching the installed display**, plus the shared parts. The logo inlay is a
separate cosmetic part.

| File | Part | Mesh extents X × Y × Z* |
| --- | --- | --- |
| [sloped_shell_v12.stl](sigil_enclosure_v12_print_set/sloped_shell_v12.stl) | Shared enclosure shell | 64.00 × 167.28 × 73.70 |
| [back_plate_v12.stl](sigil_enclosure_v12_print_set/back_plate_v12.stl) | Shared back plate | 64.00 × 155.00 × 11.60 |
| [screen_plate_eink_v12.stl](sigil_enclosure_v12_print_set/screen_plate_eink_v12.stl) | E-ink screen plate | 48.40 × 81.80 × 8.20 |
| [screen_plate_oled_v12.stl](sigil_enclosure_v12_print_set/screen_plate_oled_v12.stl) | OLED screen plate | 48.40 × 78.40 × 9.40 |
| [sloped_shell_logo_inlay_v12.stl](sigil_enclosure_v12_print_set/sloped_shell_logo_inlay_v12.stl) | TurnHub logo inlay | 56.12 × 14.00 × 0.60 |

## Printing and fit checks

*Extents above are measured from the STL coordinates in their exported
orientation, rounded to two decimals. STL files contain no unit metadata;
import as millimeters and compare the displayed size with this table and the
actual hardware before slicing. These are bounding dimensions, not assembly
clearances or dimensions after slicer rotation.*

1. Start at 100% scale. Select a print orientation in the slicer and inspect
   overhangs, supports, thin features and the first layer. The exported axes do
   not prescribe a print orientation.
2. Print the Atlas fit-test pieces first and check them against the actual
   board, screen, speaker, card and USB cable.
3. Dry-fit the shell and plates with the intended hardware. Check display
   seating, connector access, wiring clearance, joystick travel and click,
   status-ring clearance, and access to BOOT/reset before final assembly.
4. Record the printer, material, nozzle, layer height, supports and any fit
   adjustments when evaluating a print. This upload includes STLs only;
   validated slicer settings, fastener specifications and editable CAD sources
   are not included.

**Sigil joystick orientation:** the owner reports rotating the stick 180° to
fit the case. That reverses both cardinal axes. After assembly, test Up, Down,
Left, Right and click with the installed firmware. The orientation correction
is tracked in [joystick PR #84](https://github.com/rickethyo/TurnHub/pull/84);
that build prints `SIGIL|JOYSTICK|ORIENTATION` at boot to identify the active
axis mapping. Physical direction verification is still pending as of this
README's creation.

These are rough-fit prototype models. Mesh inspection does not establish final
mechanical fit; keep print and assembly findings with the next model revision.

## Related references

- [Hardware, boards and pin maps](../Documentation/engineering/HARDWARE.md)
- [Sigil displays and hardware profiles](../Sigil/DISPLAY.md)
- [Sigil electrical drafts](../KiCad/PCB/Sigilv1/README.md)
- [Earlier Sigil PCB fit-test model](../KiCad/PCB/Sigilv1/case/sigil_fit_test.stl)
  — a separate PCB fit test, not part of these enclosure sets.
