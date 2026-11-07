# UI action roles

Use the same role on every page and on host/guest devices. Themes change palette,
not the meaning of an action. `ActionButton` owns footer geometry, minimum 48 dp
targets and role colors; `AccentButton` and neutral/bad `ToneButton` delegate to it.

| Role | Component | Examples |
| --- | --- | --- |
| Commit the task | `AccentButton` or `ActionButton(PRIMARY)` | Save, Join, Start |
| Secondary / dismiss | `ToneButton` or `ActionButton(SECONDARY)` | Cancel, Done, Close, Back |
| Destructive | `ToneButton(tone = BAD)` or `ActionButton(DESTRUCTIVE)` | Delete, Disconnect, Release host |
| Open an option or destination | `NavigationRow` | Change game, Change game mode, Planar deck and rules, More destinations |
| Expand content | `DisclosureCard` | Rules, Finish game; expose expanded state and reveal content |
| Choose a value | Chips, radios or equal choice cards | Game mode, first player (including Random) |
| Persistent boolean | Labeled switch row | Reduced motion, including profile scope |
| Repeated gameplay edit | Existing shared stepper | Life, counters, numeric corrections |
| Protective hold | Existing labeled hold with progress and tap confirmation | Pass turn |

Use explicit verbs in confirmation buttons rather than “Confirm”. Use sentence
case. A dismiss-only Done is secondary; a button that commits a draft names the
operation. Carry destructive semantics through the trigger and confirmation,
with a safe secondary cancellation. Inline utility links and password visibility
controls can remain text actions. Status badges are not action buttons.

Saved setups use `SavedSetupActions` for both authorities. Saved commander chips
edit the draft; Save commits. First-player choices share one family and emphasis.
Selecting the local starter is deferred until the countdown completes, so only
another setup edit cancels it. Keep countdown cancellation on edits and app exit.

Validate narrow portrait, larger text, light/dark, Brass and High contrast. Labels
must wrap rather than hide important verbs. Preserve named accessible roles,
states and explicit wording; color never carries meaning alone.
