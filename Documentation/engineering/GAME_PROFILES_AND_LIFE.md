# Game profiles and life counters

Status: implemented and in use (owner, 2026-10-02).

Implementation scope, 2026-09-21: Michael requested life counters and the Python
game-profile selection flow. This first slice offers Generic (40), Magic (20),
Commander (40), and Yu-Gi-Oh! (8000), with custom starting life from 0 to 1,000,000.
The initial slice selected life presets only. Commander damage and cross-player
life approvals were added locally on 2026-09-22; see
[Life approval and Commander damage](LIFE_APPROVAL_AND_COMMANDER.md) for the
extended behavior, shared Intent boundary, API and verification. It remains a
bookkeeping tool, not a rules engine. The sections below record the original slice.

## Feature boundaries

1. Atlas owns the next-game settings; GameEngine captures them and owns per-player
   life when a match starts. Profiles here are game formats, not person identities.
2. Host-only `ConfigureGame` changes lobby settings; `ChangeLife` changes the
   authenticated participant's own total. Both use the Intent dispatcher.
3. Validate host/lobby for configuration; current participant, active match,
   living player, no pending table decision, and numeric bounds for life edits.
   Zero/negative life never automatically eliminates anyone.
4. A dedicated Atlas settings repository persists versioned next-game settings.
   Active life stays in RAM with current game state; recovery is still unimplemented.
5. Portal renders profile selection, public life totals, and labeled own-life
   controls. Multiple phones and an attached Sigil refer to the same participant.
6. Add HTTP settings/life requests and state fields. No radio packet changes or
   Sigil-local settings. Existing lifetime statistics remain unpartitioned; do not
   imply historical totals belong to the newly selected format.
7. No external dependencies or assets. Use existing storage and authentication.
8. Native labeled controls, explicit save/error text, keyboard access, large life
   buttons; numeric totals convey information without relying on color.

Changing lobby settings cannot rewrite a running match. Rematches start with fresh
life totals. Failed/unsupported settings reads must not silently overwrite data.

## Using the feature

Join the table, then any seated player can save Game profile and life on the Game tab.
Magic uses 20 life, Commander 40, Generic 40, and Yu-Gi-Oh! 8000; custom starting
life is supported. Game shows everyone's totals and the signed-in player's own
adjustment controls. Standard buttons use 1/5-point changes, or 100/1000 for
Yu-Gi-Oh!, with a custom signed delta available. Since 2026-10-01 every
client counts Yu-Gi-Oh! in hundreds: the Android app's life pad and request
dialog, and the Atlas screen's Player screen (100/1000). A Sigil steps by 100
in any game that starts at 1000 life or more (it learns the starting life from
`StartingLife`; `lifeUnitFor` in `life_adjust.h`), and its status ring counts
steps. Changes are accepted while
running or paused, except during a table decision or after elimination/game over.
Zero/negative totals do not eliminate players. Hidden performance statistics do
not hide public in-game life totals.

HTTP: `GET /api/game/settings` reports effective settings and caller editability;
seated-player-authenticated `POST /api/game/settings` takes `gameProfile` and `startingLife`.
`POST /api/control/life` takes `delta` and resolves its target from authentication.
`/api/session/me` and `/api/seats` report `lifeAvailable` and `life`.

Storage: `gamecfg` in Atlas's `turnhub` namespace. Schema 1 is six bytes: profile
enum 0..3, unsigned 32-bit little-endian starting life (reads as turn timer off).
Schema 2 (written since 2026-09-24) appends the 32-bit turn timer; see
[Turn timer and cues](TURN_TIMER_AND_CUES.md). Missing records use Generic 40, timer
off; corrupt/unsupported records block setup/start rather than overwrite data.
Life deltas never write flash. Existing statistics remain unchanged and unpartitioned.

Validation covers owner/host authorization, numeric bounds, storage errors,
unchanged running settings, companion sessions, paused/negative life, eliminated
players, and rematch reset. Browser verification covers preset selection, custom
life, save/reload, public totals, and mobile layout.

## Phone-only instability investigation

History: the owner reported Atlas resets with phones and no Sigils
(2026-09-21). Bounded portal polling and heap/reset diagnostics followed, and
the 2026-09-29 playtest logs traced the panics to low heap with phones
connected; the 2026-09-30 RAM reduction doubled free heap (see
[playtest notes](PLAYTEST_NOTES_2026_09_29.md), section G).
