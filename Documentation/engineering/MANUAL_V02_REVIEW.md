# Manual V0.2 implementation review

Reviewed on 2026-09-22 against commit `34a0363` and the local
`codex/manual-v02-gameplay` changes. The manuals describe the intended Prototype
1.0 experience; their wording is not evidence that each feature has shipped.

Sources: [V0.1 manual](../User%20Manual/TurnHub%20Manual%20V0.1.docx),
[V0.2 manual](../User%20Manual/TurnHub%20Manual%20V0.2.docx), and current Atlas source.
The content changes between these manuals are Game Master terminology and the
closing version number. V0.2's opening still says “Prototype Edition v0.1”.

## Gameplay and portal comparison

| Manual function | Current source status | Remaining work |
| --- | --- | --- |
| Local Atlas authority; physical and browser participants | Implemented through shared Intents | Hardware regression remains necessary after changes |
| Join, leave, start, turn passing, pause/resume, concession, confirmed win | Implemented | Continue mixed phone/Sigil acceptance |
| Starting life 20, 25, 40, 2000 | All accepted as custom values; format presets also exist | Optional preset presentation polish |
| Magic +10/-10 controls | Added in this local change | Bench acceptance |
| Changes to another player's life with a 15-second approval window | Added in this local change; only the recipient can approve/reject, Atlas handles timeout | Bench acceptance, especially a recipient reconnecting during the window |
| Commander damage | Added in this local change, separate counters per source and commander 1/2; damage and life change together | Bench acceptance; players still decide elimination |
| Pass to yourself with a distinct alert | No dedicated self-pass request/control in current Atlas | Define the semantic action and alert before adding UI |
| Nudges and browser alerts | General browser feedback exists; nudge sending is unimplemented, although mute preferences exist | Atlas nudge handler, recipient delivery, mute enforcement and UI |
| Profile name, PIN and personal settings | Implemented | Keep account, game host and Game Master roles distinct |
| Avatar selection | No current Atlas avatar field/control found | Profile representation, storage and portal selection |
| Game Master privileges | Force pass and configurable moderation exist | Manual leaves exact permissions open; retain explicit server-side permissions |
| Atlas status | Available in Device Settings and diagnostics according to permissions | Review navigation terminology with the owner |
| Restart and Shut Down | No standalone commands; changing Wi-Fi password restarts Atlas | Define Atlas system actions, state restrictions and supported shutdown behavior |
| QR connection | Manual describes it; no generated QR in the current Atlas portal | Decide printed label versus locally generated presentation |

## Other prototype gaps and wording decisions

The manual describes a deliberate 30-second pairing flow. Manual pairing is now
implemented with a 15-second window (see [Manual Pairing](MANUAL_PAIRING.md)), so
the manual text needs updating. Physical auxiliary controls and standalone
profile selection also need their hardware/firmware work. These are separate from
the web counter changes.

Current source still keeps the active game and new counters in RAM. Persistent
profiles/settings and browser reconnection should not be mistaken for recovery
after Atlas power loss. The pending recovery design must include committed
Commander counters and cancel transient life approvals when restoring a match.

The manual describes statistics-related features as opt-in, while current profile
policy says statistics always accumulate and separately controls visibility.
Whether collection itself needs an opt-in is a product decision; this change
preserves existing recording behavior.

The manual names Game / Player / System sections. Last night's portal organization
uses Game / Players / My Account / Device Settings with independent permissions.
This pass preserves that organization while adding the chosen gameplay functions.

## Verification and next acceptance

See [Life approval and Commander damage](LIFE_APPROVAL_AND_COMMANDER.md) for the
implemented boundaries, endpoints and automated evidence. No firmware has been
flashed by this task. The next bench check is a three-player game: request/accept,
request/reject, unanswered request, reconnect during a request, Commander damage
and correction, win claim/concession while requests are pending, and rematch.
