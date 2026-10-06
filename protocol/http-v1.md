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
   Keep the returned token private; send it in `X-TurnHub-Token` on authenticated
   requests. `POST /api/session/join` joins the authenticated profile.
4. `GET /api/session/me` resolves the session's current `module`, `slot`, `player`
   and `participating` status. Physical companion attachment retains its existing
   [physical claim flow](../Documentation/engineering/PLAYERS_AND_ACCOUNTS.md).
5. Fetch state. Map semantic `PASS` to `POST /api/control/pass` with
   `application/x-www-form-urlencoded` fields, not the draft JSON envelope.
   Atlas resolves the actor from the session, never client-supplied seat IDs.
6. Success returns `ok`, `status: "ACCEPTED"`, `message`, `bootId` and `revision`.
   Fetch state again to render the authoritative outcome.

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
keep their current values. Any seated player may change settings (there is no
table host since 2026-09-25), only in the lobby (409 otherwise); invalid values
return 400. In `GET /api/session/me`, `host` now means "this seat may use table
actions", which is true for every seated player; `hostModuleId` in the state is
always `null`. Both fields remain for compatibility.

Table presence (2026-09-25): `GET /api/presence` returns `{verified,
remainingMs, setup, canRequest}`. `POST /api/presence/request` (an Admin, or
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
  "adminExists":bool,"passwordIsDefault":bool,"ssid":"TurnHub-Atlas"}`.
  Never carries the password. An Atlas from before this route answers 404;
  clients treat that as `complete`.
- `POST /api/setup/finish` with `password` (8 to 63 characters, not the
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
4 Developer, 8 GM reset connections, 16 GM remove from game), `archived` and
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

Supported routes: `/api/control/pass`, `pause`, `concede`, `win`, `confirm`, `deny`,
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

## Personalization and avatars (2026-09-25)

- `GET /api/avatars` (public): `{"size":16,"avatars":[{"id":1,"key":"die","label":"Die","rows":["....", ...]}]}`,
  the preset icons from `shared/include/avatars.h`. Each row is `size` characters, `#` ink, `.` background.
  Clients draw these rows so every screen shows the same icon.
- `GET /api/seats` entries carry `"avatar"`: the seat's preset id, or 0. Custom avatars never appear here.
- `GET`/`POST /api/session/personalization` (signed-in profile only): `color` (`#rrggbb` or `none`) and
  `avatar` (0 or a preset id). Both are saved on Atlas's microSD card; without one, `card` is false and
  saving answers 503.
- Planned: custom avatars (`AVATAR_CUSTOM`), visible to signed-in viewers only, with a possible Admin
  approval to make one public. Not implemented.
