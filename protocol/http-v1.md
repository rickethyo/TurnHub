# Implemented Atlas client HTTP contract

Atlas remains authoritative. Browser and native clients share these routes and
must not implement game rules. Responses use `Cache-Control: no-store`. Connect
to Atlas's existing Wi-Fi network first. Discovery, automatic Wi-Fi connection,
BLE, events and the generic JSON Intent envelope remain unimplemented.

## Joining Atlas's network

Standalone Atlas serves this API at `http://192.168.4.1` on its own WPA2
access point, SSID `TurnHub-Atlas` (channel 6, no internet). A new or
factory-reset Atlas, with no owner-set password stored, uses the shipped
pre-setup passphrase `TurnHub-Setup` (`AtlasConfig::WIFI_DEFAULT_PASSWORD`).
It is public by design so a client can join before setup; setup must replace
it. Once the owner sets a password in the portal, the default no longer
applies. Clients must therefore fall back to asking the user. The Android app
does this: it tries a saved password, then this default, then prompts.

## Connection and first PASS

1. `GET /api/v1/info` returns public device identity (`THA-` plus station MAC),
   firmware, HTTP API version `1`, logical protocol `0.1`, the Atlas-Sigil
   radio version (informational; 3 since 2026-09-30), boot ID, revision and capability flags. MAC identity is not authentication.
   No Wi-Fi password, PIN, profile data or session token is included.
2. `GET /api/v1/state` returns one main-loop snapshot conforming to
   [state-v0.1.schema.json](state-v0.1.schema.json). This is public table state,
   consistent with `/api/status` and `/api/seats`. No profile statistics or
   credentials are included. Each player carries `displayName` (the profile
   name, or `null` for an unnamed guest; clients show "Player N"), so names
   arrive with the state they belong to (since 2026-09-29). Atlas re-reads
   names when a seat's occupant changes and at least every 5 s, so a rename
   may appear without a revision change. `/api/seats` still carries names and
   avatars.
3. Use `GET /api/profiles`, then `POST /api/session/login` with form fields
   `profileId` and `pin`, or `POST /api/profiles/register` with `name` and `pin`.
   `pin` is the profile's secret: a PIN of 4 to 8 digits or a password of 8 to
   64 UTF-8 bytes without control characters (since 2026-10-02).
   A profile with no secret yet (`hasPin:false`, e.g. made on a Sigil or the
   tablet) takes the `pin` of its first login as its secret (since 2026-10-07);
   clients ask for it twice first.
   Keep the returned token private; send it in `X-TurnHub-Token` on authenticated
   requests. `POST /api/session/join` joins the authenticated profile.
4. `GET /api/session/me` resolves the session's current `module`, `slot`, `player`
   and `participating` status, plus 1-based `game` (the resolved venue table). Physical companion attachment retains its existing
   [physical claim flow](../Documentation/engineering/PLAYERS_AND_ACCOUNTS.md).
5. Fetch state. Map semantic `PASS` to `POST /api/control/pass` with
   `application/x-www-form-urlencoded` fields, not the draft JSON envelope.
   Atlas resolves the actor from the session, never client-supplied seat IDs.
6. Success returns `ok`, `status: "ACCEPTED"`, `message`, `bootId` and `revision`.
   Fetch state again to render the authoritative outcome.

### Two games on one Atlas

Atlas runs two games side by side (since 2026-10-07, Atlas 0.7.0): Game 1
and Game 2, each with its own lobby, turn order, timer and life totals.
Profiles, statistics and pairing are shared, and a profile plays in one game
at a time. Every request acts on one game: a table tablet's chosen game; else
the game the session's profile is seated in; else the game the session
follows; without a session, `?game=N` if given, else Game 1.
`POST /api/session/game` with `game` (1 or 2) makes the session follow that
game. A profile in a lobby leaves it first; one playing in a game gets 409
until that game is over and back in its lobby. State and `/api/status` add
`game` (this snapshot's game) and `games` (each game's `game`, `state` and
`players`). `revision` is one counter across both games, so it never repeats
when a client follows another game.

### Atlas battery

State carries `battery`: `{percent, low, charging}` for Atlas's own cell
(estimated from its voltage; `low` at 15 %, `charging` while on USB and below 100 %), or
`null` with no cell. Like the clocks it is sampled, so it can change without a
revision change. The Android app draws it as a header gauge;
`GET /api/devices` also reports it under `atlas.battery` with `millivolts`.

Controls dispatch through the same `IntentDispatcher` as Sigils and Atlas's
button. PASS acceptance may arm **or cancel** the existing three-second grace
period; it does not mean the next turn has started. State confirms the later commit.

## Update notice

Atlas has no internet. A client that can read the public release feed
(`turnhub-firmware.json`) may report the newest versions with
`POST /api/updates/latest`, form fields `atlas`, `sigilEink` and `sigilOled`
(`major.minor.patch`, optional `-suffix`; at least one). No sign-in. Atlas keeps
the report in RAM until it restarts, blinks its on-board LED blue while Atlas or
any paired Sigil runs an older version, and says so on its screen.
`GET /api/updates` returns `reported`, `latest` (each version or `null`),
`atlasFirmware` and `updatesAvailable` (devices behind); `POST /api/updates/latest`
answers with the same body. The Android app reads the feed daily (every minute
in debug builds) and repeats the report every 30 s to follow `updatesAvailable`
for its "Update available" card.

## Revisions and reconnect

- Compare `(atlasId, bootId, revision)`. Boot ID is a public random epoch generated
  when the HTTP service starts, not an authentication credential.
- Revision is a 32-bit in-memory version of the published gameplay projection.
  Atlas observes completed dispatches and refreshes before reads. A meaningful
  projection change advances it; rejected and accepted no-op requests do not.
  Nested dispatches may increment it more than once. It is not an event count.
- The projection covers membership/participant identity, settings, lifecycle,
  active/starter/winner, completed turns, elimination, PASS/countdown state, win
  decisions, life, Commander damage and life approval records. It does not version
  names, account policies, pairing, button gestures or network health.
- Clock samples (`sampledAtMs`, `gameElapsedMs`, `turnElapsedMs`, remaining PASS
  time) may change at the same revision. Millisecond timestamps wrap at 32 bits;
  use unsigned subtraction within one boot. Pending life approval expires
  15,000 ms after `requestedAtMs` under Atlas's existing rules.
- Missing Commander entries mean zero damage. Each entry's two values represent
  commander 1 and commander 2 of `sourcePlayer`. Lobby life is `null`.
- `moduleId` retains the existing wire spelling for an Atlas controller handle:
  physical handles 0–7, virtual handles 8–23. It is not a durable device ID.
  Slots are 1/2. `participantId` is stable while that participant is at the table;
  re-resolve identity after reconnect or lifecycle changes.
- Always fetch fresh state after reconnect. If identity/boot changes or revision
  decreases (including wrap), discard cached state and stale commands. Sessions
  are volatile and expire after eight hours of inactivity. A 401 requires login
  again; rejoining still obeys existing lobby restrictions.
- Never automatically replay an ambiguous timed-out control. Request IDs are not
  deduplicated. Reconcile from state, then let the user issue a new action.

## Turn timer

Atlas owns the turn timer; clients display it. `settings.turnTimerMs` is the
captured per-turn countdown (0 = off; otherwise 15,000-3,600,000 in whole
seconds). `turnTimer.phase` is `NORMAL`, `WARNING` (10 s or less left), `EXPIRED`
(time ran out; the turn continues and Atlas never passes it) or `LONG_TURN`
(timer off, turn past five minutes). `turnTimer.remainingMs` is null when the
timer is off or no turn runs. Both are clock samples like `turnElapsedMs`: they
may change at the same revision, and clients may count down locally between
snapshots. Firmware before the timer omits both fields; treat that as off.

`GET /api/game/settings` (authenticated) returns `turnTimerMs` and
`turnTimer {presetsMs, minMs, maxMs, warningMs, longTurnMs}` alongside the
existing fields. `POST /api/game/settings` accepts `turnTimerMs`; omitted fields
keep their current values. `twoHeadedGiant` (`0` or `1`, `mtg` and `mtg_commander`
only, never `mtg_star_commander` or `mtg_bag_of_doom`; GET returns it with the suggested `teamLife` per profile) turns on
Two-Headed Giant: neighbours in turn order are teams of two with one shared life
total and one turn (see the state schema's `team`). Changing to another profile
without sending it turns it off. Any seated player may change settings (there is no
table host since 2026-09-25), only in the lobby (409 otherwise); invalid values
return 400. In `GET /api/session/me`, `host` now means "this seat may use table
actions", which is true for every seated player; `hostModuleId` in the state is
always `null`. Both fields remain for compatibility.

Table presence (2026-09-25): `GET /api/presence` returns `{verified,
remainingMs, setup, canRequest, codeRequired}`. `codeRequired` is the Admin's
table code setting (2026-10-08, off by default): while it is false every
signed-in account reads as verified (`remainingMs` 600000) and no request
returns `presenceRequired`. `GET /api/table-code` (Admin) returns
`{required}`; `POST /api/table-code?required=0|1` (Admin) changes it, and
turning it off while it is on returns `403 {"presenceRequired": true}` until
that Admin has entered a code. `POST /api/presence/request` (an Admin, or
anyone signed in before any Admin exists) shows a six-digit code on the Atlas
screen for 90 s. `POST /api/presence/confirm?code=NNNNNN` returns 200 when
verified (for 10 minutes), 400 for a wrong code, 409 when no code is showing
for that account, and 429 after five wrong codes. `POST /api/presence/lock`
ends verification. Protected requests without verification return
`403 {"presenceRequired": true}`. See
[Turn timer and cues](../Documentation/engineering/GAMEPLAY.md).

## First-run setup (2026-09-30)

One flow for the Android app and the portal
([First-run setup](../Documentation/engineering/FIRST_RUN_SETUP.md)).

- `GET /api/setup` (no sign-in): `{"stage":"welcome"|"finished"|"complete",
  "adminExists":bool,"passwordIsDefault":bool,"ssid":"TurnHub-Atlas",
  "uploadApprovalRequired":bool}`.
  Never carries the password. An Atlas from before this route answers 404;
  clients treat that as `complete`.
- `POST /api/setup/finish` with `uploadApprovalRequired=0|1` and `password` (8 to 63 characters, not the
  printed default): an Admin verified at the table, stage `welcome`, between
  games. Stores the Wi-Fi password, moves the stage to `finished`, answers
  `{"ok":true,"restarting":true,...}` and restarts Atlas onto the new
  password. 400 for a bad password, 403 (with `presenceRequired` when only
  the code is missing) and 409 when setup is already finished or the Intent
  is refused.
- Setup also uses `POST /api/profiles/register`, the presence routes above,
  `POST /api/accounts/setup`, `GET /api/devices`, `POST /api/firmware`
  (multipart field `firmware`), `POST /api/sigil-firmware`,
  `POST /api/sigil-update` and `GET /api/sigil-firmware`.

## Star Commander (2026-10-13)

`settings.profile` (state) and `gameProfile` (settings, standalone import,
`lastGameType`) accept `mtg_star_commander`: a five-player Commander variant with
every Commander counter, default starting life 40 and normal clockwise turns.

- **Seats decide relationships.** For player number `p` (1-5, `i = p - 1`) the
  allies are players `(i+1)%5 + 1` and `(i+4)%5 + 1`; the enemies are
  `(i+2)%5 + 1` and `(i+3)%5 + 1`. They follow the lobby seat order and are fixed
  once the match starts, also after a player is eliminated. Clients derive them
  from `playerNumber`; there is no separate field.
- **Start:** a Star Commander table needs exactly five players; Start is refused
  with 409 `"Star Commander needs exactly five players."`. Two-Headed Giant is
  never allowed with it (`twoHeadedGiant=1` is refused; choosing the profile
  turns Two-Headed Giant off).
- **Commander damage between allies** is refused by Atlas for every client
  (phone, tablet, Sigil): 409 `"Allies can't deal Commander damage to each other
  in Star Commander."`. Damage from an enemy works as in Commander. Ordinary
  life changes are never restricted.
- **Victory:** after every elimination path (elimination, concession, zero
  life, poison, Commander damage, a loss-rule change), once the whole batch of
  eliminations is applied, every living player whose two enemies are both out
  wins. Two allies can share a victory: `winnerPlayers` lists both and
  `winnerPlayer` is the lower number. A shared victory is not a draw. A batch
  that leaves nobody and no winner is a draw (`winnerPlayer: null`,
  `winnerPlayers: []`). Last-player-standing does not apply. Win claims
  (alternate win conditions) work as in Commander, with a single winner.
- **Statistics:** every winner is credited a win. `GET /api/session/stats`
  `lifetime` adds `starGamesPlayed` and `starGamesWon` (also counted in
  `gamesPlayed` and `gamesWon`).

`winnerPlayers` (state, always present since 2026-10-13) lists every winner in
ascending order: one player, both Two-Headed Giant teammates, or two Star
Commander allies; empty while nobody has won and for a draw. See
`examples/star-commander.response.json`.

## Draws

`state: "GAME_OVER"` with `winnerPlayer: null` means the match ended as a draw:
someone held End match on the Atlas touchscreen for 5 seconds during a running
or paused match (2026-09-24; originally the Atlas master button, which is gone). No new field was added; before this change no finished match
could lack a winner. Clients should say "Draw" rather than "Winner: none". Every
participant's statistics count one game played; the last-game result reads
`"Draw"` (or `"Eliminated"` for a player already out).

## Device administration (Admin permission)

Session-token authenticated; other accounts get 403.

- `POST /api/device/forget` with `module=<id>` or `all=1` forgets one or every
  paired Sigil (lobby only; refused with 409 while anyone is seated on that
  Sigil). Success: `{"ok":true,"message":...}`; refusal: 409 `{"error":...}`.
- `GET /api/pairing` returns `{"windowMs":15000,"sigilWindowMs":15000,
  "choicesMs":[15000,30000,60000]}`: Atlas's pairing window, the Sigil's fixed
  window and the allowed choices. `POST /api/pairing` with `windowMs` saves one
  of the choices (anything else: 409, nothing stored).

## People: accounts, roles and moderation

`GET /api/accounts` (session token) lists accounts: everyone for an Admin,
non-archived accounts for a Game Master, only the caller otherwise. Each entry
has `profileId`, `name`, `permissions` (bits: 1 Admin, 2 Game Master,
4 Developer, 8 GM reset connections, 16 GM remove from game, 32 Tablet
access), `archived` and
`avatar`; Game Masters and the account itself also get `nudgeMuted`. Admins
and Game Masters also get `hasPin`, `primary` (the initial Admin), `atTable`
and `reconnectRequired`, so clients can explain a blocked action up front.
`POST /api/accounts/permissions` (`profileId`, `permissions`) and
`POST /api/accounts/archive` (`profileId`, `archived=0|1`) need Admin;
`POST /api/accounts/moderate` (`profileId`, `action` = `pass`, `reset`,
`remove`, `mute`, `unmute`) needs Game Master. Atlas validates every rule:
moderation powers need Game Master, roles need a PIN, the initial Admin keeps
Admin and cannot be archived, and archiving waits until the account has left
the table.

## Sigil accessibility preferences

`GET /api/session/accessibility` and `POST /api/session/accessibility`
(authenticated with the session token) read and change the signed-in
profile's own Sigil accessibility preferences: `sigilSound` (form `0`/`1`),
`ledStyle` (`standard`, `reduced-motion`, `monochrome-safe`), `longPressMs`,
`winHoldMs` and `lifeApprovalMs` (how long other players' life-change requests
wait for this player: one of `lifeApprovalOptionsMs`, 15000, 30000 or 60000).
A state snapshot's `lifeRequest.windowMs` is the window fixed when that request
was made. Omitted POST fields keep their saved values. Hold times are whole
multiples of `stepMs` (250) within the returned `limits`, and the win hold must
be at least `minGapMs` (1000) longer than the long press; anything else returns
400 and nothing is stored. No session returns 401; an unreadable or unwritable
record returns 503. Both methods return the saved values plus `limits` and
`stored` (false when Atlas could not read the saved record and is showing the
defaults) - see [accessibility-v1.schema.json](accessibility-v1.schema.json) and
[the example](examples/accessibility.response.json).

These are profile settings, like `/api/session/policy`, not table state: they
change no Intent's meaning and take no revision check. Atlas applies them to
the player's physical Sigil within about two seconds (see
[Accessibility](../Documentation/engineering/ACCESSIBILITY.md#implemented-accessibility-settings)).

## Nudge

`POST /api/control/nudge` (a session control, no body) lets a living player who
is not up prod the active player while a game runs. Atlas plays the Nudge cue on
the active player's Sigil and sets the snapshot's `nudge` to
`{"seq","fromPlayer","toPlayer","ageMs"}` (null until the first nudge since
boot). `seq` grows with every nudge, so a client alerts `toPlayer` once per new
`seq`, and ignores an old one (`ageMs` large) it finds when it connects. One
nudge per player per 30 s, and none from an account a Game Master muted; those,
the active player nudging themselves and a game that isn't running return 409
`REJECTED`.

## Optional concurrency check on session controls

The existing `runControl` adapter accepts `expectedRevision` (canonical unsigned
decimal text) and `expectedBootId`. Supply both from the latest snapshot. A stale
revision, missing/wrong boot ID, or unavailable revision provider returns HTTP 409
with `status: "CONFLICT"` before dispatch. Invalid revision text returns HTTP 400.

Supported routes: `/api/control/pass`, `pause`, `concede`, `eliminate`, `win`, `confirm`, `deny`,
`starter`, `start`, `cancel-start`, `rematch`, `reset`, `nudge`. Omitting the check preserves
browser behavior. Life, counters, participation and settings retain their existing
contracts and do not yet accept this check.

After authentication and seat resolution, handler rejection returns HTTP 409 with
`ok: false`, `status: "REJECTED"`, `error`, `bootId` and `revision`. Earlier
authentication/validation failures retain existing JSON errors; not every error
contains a revision or semantic status. Fetch state after a conflict or rejection.

## Fixtures and remaining work

[Response fixtures](examples/) come from host scenarios with public identity,
epoch and revision normalized for readability. `pass.request.json` describes
the live HTTP adapter request, not an Intent body.
`intent-v0.1.schema.json` remains a draft. `/api/v1/intent` and `/api/v1/events`
do not exist and are advertised as unsupported.

The next Android slice can test info → login/join → snapshot → PASS → snapshot
→ reconnect through an `AtlasRepository`, with a fake transport and these fixtures.
Generic envelopes, richer per-intent statuses, deduplication and events remain
separate future work. No Android application ID is selected here.

## Personalization and artwork (2026-10-10)

- `GET /api/avatars`: stable preset IDs/keys/labels and historical 16px rows.
  Android renders vector masters; Atlas has generated 64px alpha masks; current
  Sigil assets are frozen. These tiny rows do not constrain uploaded artwork.
- `GET/POST /api/session/personalization`: color and avatar selection (`0`,
  preset ID, or `128` when an approved image exists), `card`, `customAvatar`
  (approved image path, even when a preset is selected), `pendingAvatar` (private
  pending path), and `uploadApprovalRequired` (device-wide, default true). Empty
  paths mean unavailable. Saves require working SD storage.
- Seats/accounts add `customAvatar` when an approved custom image is selected;
  the historical numeric seat `avatar` remains preset-or-zero. Only Admins see
  `pendingAvatar` in account listings. Artwork bytes never enter live state.
- `POST /api/session/avatar`: authenticated personal account, form requests.
  `action=start,size=<bytes>` reserves a JPEG transfer (32..49152 bytes), returns
  `revision` (8 lowercase hex) and `chunkBytes:768`. `action=chunk,revision,index,data`
  writes the next zero-based chunk; `data` is lowercase hex, exactly 768 bytes
  except the final chunk. `action=submit,revision,thumbnail` validates the complete
  square baseline JPEG (32..512px, RGB, not progressive), plus a 512-character hex
  RGB332 16px Atlas thumbnail, then publishes pending art when approval is on.
  With approval off at submission, publishes and selects custom art immediately.
  Existing pending images retain explicit review. Start/submit/remove
  responses return personalization where appropriate; chunk returns `{ok:true}`.
  `action=remove` clears art and selects None. 401 sign-in, 400 invalid envelope,
  409 busy/stale/order/card-change, 503 SD failure. One transfer, 120s idle timeout;
  no automatic retry after uncertain delivery. Re-start cleans interrupted data.
- `GET/POST /api/upload-approval`: Admin-only device setting, `required=0|1`
  on POST, `{required:bool}` on GET. Default on; persists in NVS. Changes use
  ConfigureUploadApproval. Invalid boolean 400, unauthorized 401/403, refused
  or failed persistence 409. Applies to new submissions, without approving old
  pending artwork. Images are shared on the local Atlas, not posted online.
- `POST /api/avatar/review`: Admin, `profileId,revision,action=approve|reject`.
  Exact pending revision required (409 if changed). Approval publishes and selects
  custom art; rejection retains previously approved art. Storage failures are 503.
- `GET /api/avatar?profileId=<id>&revision=<8-hex>[&pending=1]`: bounded JPEG,
  `image/jpeg`, exact Content-Length, private/no-store, nosniff. Pending requires
  owner/Admin; approved is public. Missing card/image, stale revision or corrupt
  chunks yield 404 and clients show a fallback. Transfer streams one SD chunk at
  a time. The 512px master serves app/tablet and future hardware derivatives;
  future S3 LCD firmware/radio transfer is not implemented.

## Tablet mode (2026-10-06)

One shared screen at the table acting for every seat. Engineering record:
`Documentation/engineering/PLAYERS_AND_ACCOUNTS.md` (Tablet mode).

1. Signed in, `POST /api/presence/request` with `purpose=tablet` (open to any
   account), then `POST /api/presence/confirm` with the `code` the Atlas
   screen shows, then `POST /api/tablet/enable`. `GET /api/session/me`
   reports `"tablet":true|false`. An account with the Tablet access role
   (permission bit 32 in `/api/session/me`) skips the code and calls
   `POST /api/tablet/enable` directly. With table-code verification off, enable
   works directly for any signed-in player. Success returns a new `token` and
   empty `profileId`, revokes the initiating token, and uses a RAM-only Shared
   tablet account (`tablet:true`, no profile or permissions). Other personal
   sessions/participants remain intact. This token has only the table route
   allowlist documented in PLAYERS_AND_ACCOUNTS.md. Personal/admin routes are
   403. `POST /api/tablet/disable` revokes it; subsequent session use is 401.
   Native close leaves the app signed out even if Atlas is unavailable.
   Without the grant every route below answers
   `403 {"ok":false,"tabletRequired":true,"error":...}`.
2. Lobby: `POST /api/tablet/seat` with `name` (creates a profile with no PIN;
   409 when the name exists) or `profileId` (plus `pin` when the answer was
   `403 {"pinRequired":true}`); answers `{"ok":true,"profileId":...}`.
   `POST /api/tablet/unseat` with `profileId`.
3. Every seat action carries `module` and `slot` from the state's players:
   - `POST /api/tablet/life`: `delta` (the seat's own life).
   - `POST /api/tablet/commander`: `delta`, `source`, `commander` (damage
     the seat received), as `/api/control/commander`.
   - `POST /api/tablet/life/respond`: `requestId`, `accept`.
   - `life` and `commander` take an optional `queuedMs` (0 to 86400000): a
     change the tablet kept while Atlas wasn't answering, tapped that long
     ago. Atlas applies it now under its usual rules and only logs the age
     (`ATLAS|TABLET|OFFLINE|...`); it has no wall clock to back-date with.
     The app sends such changes once on reconnect, only for the same boot,
     the same game and a player still in it, and never retries a refused one.
   - `POST /api/tablet/settings`: the `/api/game/settings` fields.
   - `POST /api/tablet/control`: `action` = `pass`, `pause`, `concede`, `eliminate`, `win`,
     `confirm`, `deny`, `starter`, `start`, `cancel-start`, `rematch`,
     `reset`, `back-to-setup`, `count-game`, or (lobby) `move-earlier`/`move-later` for turn order; answers like `/api/control/*` (status, revision, optional
     `expectedRevision`).


## Standalone tablet game import (2026-10-07)

With no Atlas at the table, the Android app can run a standalone game itself
(`Android/README.md`, "The one exception"). Each finished game is kept on the
device separately from delivery metadata. Sign in and map every player to an
exact profile on that Atlas. Android can deliver automatically when every player
has a previously verified Atlas-scoped account link; otherwise mapping is manual.
Names and unverified public-ID matches never authorize linking. Android retains
acknowledged/rejected history. Atlas 0.7.2 requires exact identities:

`POST /api/standalone/import` (signed-in session, `X-TurnHub-Token`; 401
otherwise), form fields:

- `atlasId`: required destination hardware ID, exactly this Atlas’s `/api/v1/info`
  identity. A foreign/missing ID is rejected before importing anything.
- `recordId`: 8-48 characters of letters, digits and `-` (the app uses a UUID).
- `gameProfile`: `generic`, `mtg`, `mtg_commander`, `yugioh`, `mtg_star_commander` or `mtg_bag_of_doom`.
- `durationMs`: 0 to 604800000. `players`: 2 to 10.
- `starter`, `winner`: a player index, or empty for none (no winner is a draw).
  `winner` is the first winner.
- `winners` (optional, 2026-10-13): every winner as comma-separated player
  indices (`0,1`), for a shared Star Commander victory; each is credited a win.
  It must include `winner` as its lowest index (with `winner` empty, the lowest
  listed index becomes `winner`). Empty or absent means `winner` alone.
- Per player `i` from 0, in turn order: `name<i>` (required), `profile<i>`
  (required exact profile ID chosen on the destination Atlas), `turns<i>`, `turnMs<i>`,
  `fastest<i>`, `longest<i>` (completed turns and their times), `out<i>`
  (1 for the first player out, 0 for anyone still in).

Atlas validates the whole mapping before writing a new receipt or statistics:
every profile must exist, be readable and not archived, with no duplicate ID.
Names are display labels and never an identity fallback. Answers
`{"ok":true,"duplicate":false,"credited":N,"unmatched":[names]}`; a record ID
Atlas already took answers `"duplicate":true` and changes nothing (also if a
profile was subsequently archived). 400 for malformed/foreign/invalid mapping:
the app keeps the result and reason for explicit review, with no automatic retry.
503 when Atlas couldn't save: pending delivery stays for retry with the same ID
and mapping. A successful acknowledgement retains the local result and marks
it imported; partial credit/unmatched players are displayed, not retried.

Atlas remembers the last 64 record IDs (hashed, NVS `sgimport`) and saves the ID
before any statistics, so a power cut can lose one game's statistics but does not count a replay twice while its receipt remains in that 64-ID
window. It does not guarantee arbitrary long-term replay protection. Imported games add to games played, won, eliminated and
started, completed turns and turn times, and set the last-game fields like a
live game.

### Native game-context guard (2026-10-10)

Raw native tablet/game actions can include `expectedGame` as the decimal 1-based
game number rendered by their screen. The route adapter compares it to the game
resolved from the current session before selecting a table or invoking the
handler. A mismatch returns HTTP 409 and performs no action. It supplements
revision/boot checks: two games may have matching revisions/player numbers.
`POST /api/session/game` remains the explicit follow/switch operation; it does
not use this guard. Authentication and semantic Intent validation remain required.

Browser `/tablet` and `/stats` now serve administration recovery. Native gameplay,
profile statistics and `/api/tablet/*` contracts remain available.

### Zero-life elimination and voluntary concessions (2026-10-10)

`GET /api/game/settings` and state `settings` expose `autoEliminateAtZeroLife`
(Boolean, default true). The settings POST and `/api/tablet/settings` accept
`autoEliminateAtZeroLife=0|1`. Seated players can change it in the lobby or during
running/paused play; only this field may change during play. Enabling applies
immediately to all living participants at zero or negative life, including
paused matches. An open win claim/elimination decision blocks changes. Lobby
changes persist as the next-game default; in-play changes persist in recovery
for the current match. Profile/life/timer/team changes remain lobby-only.

`POST /api/control/eliminate` and tablet control `action=eliminate` remove the
caller/selected seat with an elimination reason. Concede remains a voluntary
exit. State `players[].conceded` distinguishes it while `eliminated` remains
true for either exit. Both actions reject already-out seats and open decisions.
Statistics add lifetime `gamesConceded` and last result `Conceded`, separate from
`gamesEliminated`/`Eliminated`. Standalone import optionally accepts `concededN`
(0 or 1, default 0); 1 requires nonzero `outN`. Android retains the reason in
local history regardless of import.

### Magic monarch

`POST /api/control/monarch` declares the authenticated seated player monarch.
The tablet equivalent is action `monarch` at `/api/tablet/control`, using the
normal module, slot and game scope. Both require a living player in a running
or paused `mtg`/`mtg_commander`/`mtg_star_commander`/`mtg_bag_of_doom` game; invalid game/seat state returns 409.
Declaring monarch transfers the crown immediately, with at most one holder
per game. `GET /api/v1/state` reports nullable `monarchPlayer` (1-based individual
player number), absent on older firmware. When that player leaves play it
passes to the active player, or to the next living player in turn order when
the active player is leaving (Magic CR 724.4); it clears on reset or new match,
and survives pause and recovery. A pending win claim rejects a claim with 409. Players apply
Magic’s card rules themselves. No life, turn or victory change is implied.

### Magic counters and live loss rules

`POST /api/control/counter` changes the authenticated living player’s own
Magic counters. The shared-table equivalent is `/api/tablet/counter` with the
normal `module`, `slot` and game scope. Both converge on ChangeCounter.

| counter | Value fields | Meaning |
| --- | --- | --- |
| `poison`, `energy`, `experience` | `delta` (nonzero, ±1,000,000) | Add/correct a nonnegative bounded total. Poison is shared by 2HG teammates. |
| `commander-casts` | `delta`, `commander` (1 or 2) | Command-zone casts in Commander. Next tax = 2 × casts. |
| `city-blessing` | `value` (0 or 1) | Declare blessing or clear an erroneous entry; independent per player. |
| `initiative` | `value` (0 or 1) | Claim/transfer, or release one’s own initiative. |

Lobby/finished matches, eliminated seats, non-Magic formats and open table
decisions are rejected (409). Bad fields/ranges are 400; unauthenticated
personal requests are 401. Existing commander-damage routes are unchanged.

### Official rules and house rules (2026-10-14)

`GET /api/game/settings` and state `settings` expose `houseRules` (Boolean,
default false); the settings POST and `/api/tablet/settings` accept
`houseRules=0|1` in the lobby only. Without house rules the official modes keep
the official starting life and poison limit, as the standalone app does
(`OfficialRules.kt`): `mtg` 20 and 10, `mtg_commander` 40 and 10, Two-Headed
Giant `mtg` 30 and 15, `yugioh` 8000. Atlas saves those values whatever life or
limit the request carries (200), and refuses a live poison-limit change (409).
Start is refused (409) unless official Two-Headed Giant has exactly four players
and a Yu-Gi-Oh! Duel two. Custom modes (`generic`, `mtg_star_commander`, `mtg_bag_of_doom`,
Two-Headed Giant `mtg_commander`) stay editable. In Two-Headed Giant a
teammate is never a source of Commander damage (409).

With nobody chosen to go first, Atlas picks a random starter when the
countdown ends. `starter` on the chosen player again clears the choice.

### Back to setup and short games (2026-10-14)

`POST /api/control/back-to-setup` (tablet action `back-to-setup`): a seated
player, while running or paused and within the first three minutes of game
time (pauses excluded), returns the table to the lobby with the same players
and settings. Nothing is recorded; later it is 409.

A match that ends within one minute of game time is kept out of profile
statistics: state `shortGameUncounted` is true at game over until a seated
player sends `POST /api/control/count-game` (tablet action `count-game`), which
records it once. A rematch or reset leaves it uncounted. After a restart the
finished match can no longer be counted, so it is never counted twice.

`POST /api/game/settings` and `/api/tablet/settings` additionally accept
`autoEliminateAtPoisonLimit`, `autoEliminateAtCommanderDamage` (0 or 1) and
`poisonLimit` (1..1,000,000), in the lobby and during running/paused play.
Zero-life, poison and Commander rules are independent. All default on for
new matches; poison defaults 10, or 15 for 2HG. A team-mode change selects
the standard poison limit unless a limit is explicitly supplied. Enabling
a rule/changing a limit applies immediately. Commander loss is 21 from one
commander, never a pooled damage sum; only Commander games apply it.

State adds nullable `initiativePlayer` and per-player `counters` with poison,
energy, experience, exactly two `commanderCasts`, and `cityBlessing`. Settings
expose the three new fields. Old snapshots omit these fields (clients render
zero counters/no initiative and disabled new automatic rules). Clocks and
turns do not change when statuses are claimed. Recovery preserves these facts;
new matches clear counters/status, and a leaving holder's initiative passes
like the monarch (CR 725.4).


## Importing a phone-owned account

`POST /api/profiles/register` accepts optional `profileId` on Atlas 0.7.15+.
It must be exactly eight hexadecimal characters; Android generates uppercase
IDs while retaining a separate UUID for local history. Existing name/PIN rules,
capacity and session-slot checks apply. A malformed ID returns 400; any existing
ID (including marker-only or archived accounts) returns 409. No registration
request may overwrite an existing identity, credentials, permissions or statistics.
The profile marker is published only after its name and credential hash are saved.
Omitting `profileId` preserves Atlas-generated registration behavior.

To merge future imports into an existing account, Android authenticates through
`/api/session/login`, verifies `/api/session/me`, then saves a scoped link and
revokes the temporary token. This does not modify the active table login. It
retains both histories and sends local match records through the existing import
validator rather than copying computed lifetime totals. New accounts copy
personalization using ordinary APIs; card availability, size limits and Admin
image review remain enforced. Existing accounts retain their personalization.


## Elimination attribution

`POST /api/control/eliminate` and the shared-table equivalent
`POST /api/tablet/control` (`action=eliminate`) accept optional `eliminatedBy`:
`0` for no player credited, or a real opponent's 1-based player number. Atlas
validates source identity, refuses self/teammate credit, and captures it before
elimination completion. Omitted means unknown. Concessions award no credit.
Live state players expose nullable `eliminatedBy` with the same numbering.

Standalone imports accept up to **10** players and optional `twoHeadedGiant=0|1`.
Each `eliminatedByN` is a **0-based match index**, or empty for unknown; only a
non-conceding removed player may have an opponent credited. Attribution is
validated before any receipt or statistics writes. `winners` supports indices
0–9 without truncation. Profile statistics return `eliminationsMade` in both
`lifetime` and `lastGame`; lifetime counts individual opponents removed, not
matches, and is retained without an SD card.


## Bag of Doom

`gameProfile` / `settings.profile` / `lastGameType` accept `mtg_bag_of_doom`,
a custom Commander variant for at least five players without teams. See
[gameplay rules](../Documentation/engineering/GAMEPLAY.md#commander--bag-of-doom).
State adds optional `doomBag` (null outside a started Bag of Doom match):

| Field | Meaning |
| --- | --- |
| `atRisk`, `drawn` | 16-bit masks; bit `playerNumber - 1`. Living eligible names, and historical bag victims respectively. |
| `boundary` | Player whose turn ends the current cycle; 0 when off. |
| `warmup` | Opening complete cycles still required, 0–4. |
| `cycle` | Positive uint32 bag generation, incremented by each check or credited kill while active. |
| `pending`, `pendingTurn` | Check due; life edits still belong to the turn that just ended. |
| `sealed` | A private choice was reserved. Membership is frozen; retry resolves the same name. |
| `lastDraw`, `lastCheck` | Last completed result and cycle. `lastDraw=0` means everyone was safe; `lastCheck=0` means no check yet. |

`POST /api/control/doom` requires a signed-in living participant.
`POST /api/tablet/doom` requires Tablet access and an acting living
`moduleId`/`slot`. Both require **exact** `expectedBootId`, `expectedRevision`
and positive uint32 `cycle` from the displayed snapshot. Authentication failures
are 401/403, stale or inapplicable actions 409, malformed action fields 400.

| `action` | Extra form fields | Transition |
| --- | --- | --- |
| `eligibility` | `player` (1–16), `inBag` (0 or 1) | Correct a living player's membership before sealing. |
| `credit` | `player` (removed victim), `source` (credited opponent), both 1–16 | Credit an uncredited non-conceding, non-bag removal once; reset active bag/boundary. |
| `draw` | None | Resolve a due paused check after life approvals; authority chooses, saves, removes and refills. |

Clients never select a victim. Atlas saves a hidden reservation before revealing
the result; storage failure returns 409 and retry uses the same reservation.
Only `sealed` is projected. Resume/claim-win is refused while a check is due.
A resolved check remains paused until the usual resume action. No radio payload
changes. Each venue game retains its independent bag and freshness guards.

Standalone imports accept optional `bagDrawnN=0|1` for each zero-based player.
True requires this format, an `outOrder`, no concession and no `eliminatedByN`.
It records the exit reason without awarding an opponent an elimination.
