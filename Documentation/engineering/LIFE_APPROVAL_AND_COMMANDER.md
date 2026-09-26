# Life approval and Commander damage

Manual V0.2 alignment work, 2026-09-22. Implemented locally; native/storage
regressions, adapter audit, two-browser portal checks and Atlas firmware build
pass. Not flashed or hardware-accepted.

## Agreed behavior

- A participant can change their own life immediately or request a signed life
  change for another living participant. The recipient can accept or reject it;
  Atlas automatically accepts an unanswered valid request after 15 seconds.
- Allow one pending request per recipient. Apply a delta to the current total,
  rechecking bounds at acceptance, rather than overwriting intervening edits.
- Only the recipient can respond. Request IDs prevent a stale browser response
  from approving a replacement request or a request from a previous match.
- Ordinary pause does not stop the response timer. Starting a win claim or an
  elimination decision cancels pending life requests. Concession cancels requests
  involving that player; match completion, reset and rematch clear pending work.
- In Commander games, players record damage received from a selected player's
  commander. Two separately numbered commander counters per source are available.
  Positive damage subtracts life; a negative correction restores life. Both totals
  change atomically. Counters cannot go below zero. No life or damage threshold
  automatically eliminates a player. These are bookkeeping controls.
- Manual V0.2's +10/-10 shortcuts are provided for Magic and Commander.

The owner explicitly chose linked Commander damage/life changes on 2026-09-22.

## Feature boundary

1. GameEngine owns life, received Commander counters and pending requests.
2. RequestLifeChange, RespondLifeChange, ExpireLifeChanges and ChangeCounter
   Intents enter the same Atlas dispatcher as existing ChangeLife.
3. Atlas resolves the authenticated participant and validates actor, target,
   current match, outstanding decisions, IDs, numeric bounds and game format.
4. These facts have the same RAM-only lifetime as the current engine's active
   match. This change does not introduce a second recovery format or alter saved
   profiles/statistics. Future match recovery must include committed counters;
   pending approvals should be canceled during recovery.
5. The browser renders Atlas state, requests changes, and displays the remaining
   time returned by Atlas. Closing the browser does not stop the Atlas timer.
6. Add HTTP request/response contracts and internal semantic Intents. No Sigil
   wire protocol or firmware change is needed for this browser slice.
7. No new dependencies or external assets are introduced.

## HTTP interface

- `GET /api/game/counters`: authenticated participant's received Commander
  counters and latest incoming/outgoing life request for each recipient. Returns
  Atlas-calculated remaining milliseconds and terminal outcomes. Unrelated
  participants' request records are not serialized.
- `POST /api/control/life/request`: signed `delta` and recipient `target`.
- `POST /api/control/life/respond`: exact `requestId` and `accept=0|1`.
- `POST /api/control/commander`: commander owner `source`, `commander=1|2`, and
  signed damage `delta`. The recipient is always the authenticated participant.
- Existing `POST /api/control/life` remains an own-life operation, even if a
  client submits an extra player/target field.

Life totals remain bounded to -1,000,000 through 1,000,000; Commander counters
remain bounded to 0 through 1,000,000. A response at or after the 15-second deadline
cannot override automatic acceptance. An invalidated total leaves the request
failed with no change. There is no automatic client retry of mutations.

Request IDs increase across games in the current boot. They are not persistent;
browser sessions also expire on Atlas reboot. Request records are bounded latest
outcomes, not a durable history. Commander totals are reset on a new match and are
not added to existing lifetime statistics.

## Accessibility and verification

Use labeled native inputs/buttons, signed changes and explicit text status.
Keep Accept/Reject controls stable during polling so keyboard focus survives.
Announce a new request without announcing every countdown tick. A recipient can
respond from any authenticated companion browser for that same participant.
Physical Sigil approval controls are not part of this slice; unanswered requests
still use the documented automatic acceptance.

The portal shows a persistent pending-request notice across its tabs and a
keyboard-accessible link to the response controls. Counter controls become
unavailable on a failed refresh; the page explains that Atlas's timer continues.

Verify authorization, companion sessions, timer rollover and deadline races,
concurrent edits, bounds/atomicity, decisions, concession, game completion and
rematch. Check the rendered mobile/desktop portal and keyboard interaction, run
the existing native/storage scenarios and adapter audit, then build Atlas. Flash
and real table acceptance remain separate.

Automated evidence: `run-gcc.ps1` passes the existing scenarios plus approval
authorization/replay, deadline races, rollover, pause/decision/cancellation,
concession/rematch and atomic Commander-boundary cases. `audit_adapters.py` checks
17 adapters. `portal_smoke.cjs` passes the existing account/game flows;
`counter_smoke.cjs` checks two independent browsers, keyboard response/focus,
cross-tab notification, corrections, reload, disconnect and responsive layout.
Mobile and desktop screenshots were visually inspected. This is not a full
accessibility conformance assessment or physical hardware verification.
