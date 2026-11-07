# Implemented Atlas client HTTP contract

Atlas runs no game (owner, 2026-11-04): the hub phone or tablet runs every game
with the shared rules core, and Atlas is the table accessory that shows it,
drives the Sigils and keeps profiles, statistics, records and settings. The
hosted-game routes are in [hosted-v1.md](hosted-v1.md); this file covers the
rest. Responses use `Cache-Control: no-store`. Discovery, events and the
generic JSON Intent envelope remain unimplemented.

The HTTP API is carried over HTTPS on every network interface. Plain HTTP is
not an external Atlas endpoint. During first setup, the Android app uses a
restricted TLS bootstrap and does not use the wider API until the user compares
the presented key fingerprint with Atlas and confirms it in the app.

## Joining Atlas's network

During first-time provisioning only, Atlas serves this API at
https://192.168.4.1 on the WPA2 setup hotspot TurnHub-Atlas. Welcome shows a
standard Wi-Fi QR with its current password (default TurnHub-Setup). After
setup, Atlas uses home-network HTTPS and never starts a hotspot, even during
router outages. See [Atlas setup](../Documentation/engineering/FIRST_RUN_SETUP.md).

## Connection and sign-in

1. `GET /api/v1/info` returns public device identity (`THA-` plus station MAC),
   firmware, HTTP API version `1`, logical protocol `1`, the Atlas-Sigil
   radio version (informational), boot ID, revision and capability flags
   ([info-v1.schema.json](info-v1.schema.json)). MAC identity is not authentication.
   `home` (since 2026-11-03) is Atlas's address on the home network with its
   HTTPS port and `tlsPin` (lower-case hex SHA-256 of the key's
   SubjectPublicKeyInfo), or `null` when Atlas is not on one. Over HTTPS on the
   home network the API is the same; clients trust exactly that key, learned
   only over a physically verified TLS connection to the setup endpoint
   ([Home Wi-Fi](../Documentation/engineering/HOME_WIFI.md)).
   No Wi-Fi password, PIN, profile data or session token is included.
2. `GET /api/v1/state` returns Atlas's own status
   ([atlas-state-v1.schema.json](atlas-state-v1.schema.json)): identity, boot
   ID and `revision`; `hub`, the hosted game's hub (`name`, `gameId`, `state`,
   `players`) or `null` when no hub holds the table; `battery`; and `links`.
   It never carries a game: players, life and turns come from the hub.
3. Use `GET /api/profiles`, then `POST /api/session/login` with form fields
   `profileId` and `pin`, or `POST /api/profiles/register` with `name` and `pin`.
   Omit `pin` or send it empty for a gameplay-only session. Supplying the
   correct secret unlocks private stats and admin permissions. A gameplay-only
   session cannot change an existing PIN or mint private-history link keys.
   `pin` is the profile's secret: a PIN of 4 to 8 digits or a password of 8 to
   64 UTF-8 bytes without control characters (since 2026-10-02).
   A profile with no secret yet (`hasPin:false`, e.g. made on a Sigil or the
   Atlas screen) takes the `pin` of its first login as its secret (since
   2026-10-07); clients ask for it twice first.
   Keep the returned token private; send it in `X-TurnHub-Token` on authenticated
   requests.
4. `GET /api/session/me` returns the session's `profileId`, `name`,
   `permissions`, `hasPin` and `policy`.

### Atlas battery

State carries `battery`: `{percent, low, charging}` for Atlas's own cell
(estimated from its voltage; `low` at 15 %, `charging` while on USB and below 100 %), or
`null` with no cell. It is sampled, so it can change without a
revision change. The Android app draws it as a header gauge;
`GET /api/devices` also reports it under `atlas.battery` with `millivolts`.

State also carries `links`: `{hotspot, home, sigils}` (Atlas 2026-11-03), the
same links as the Atlas header's icons: its own hotspot is up, it is on the
home network, and how many Sigils are online. Sampled like `battery`; the app
draws the same icons beside its battery gauge.

## Update notice

Atlas's hotspot has no internet. An Admin may let Atlas check the feed itself
over a home network (off by default; `/api/network/home`, see
`Documentation/engineering/HOME_WIFI.md`). A client that can read the public release feed
(`turnhub-firmware.json`) may report the newest versions with
`POST /api/updates/latest`, form fields `atlas` and `sigil`
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
- `revision` is a 32-bit in-memory counter of Atlas's own published state
  (the hub's lease and hosted game). It is not an event count. `battery` and
  `links` are sampled and may change at the same revision.
- Always fetch fresh state after reconnect. If identity/boot changes or revision
  decreases (including wrap), discard cached state. Sessions are volatile and
  expire after eight hours of inactivity. A 401 requires login again.
- Never automatically replay an ambiguous timed-out request. Request IDs are not
  deduplicated. Reconcile from state, then let the user act again.

### Android request scheduling and short outages

The app permits one in-flight request per Atlas origin on each phone, shared
across transport instances, polling, hosting, profile copies and artwork. Waiting
is cancellable, idle scheduling entries are removed, and different Atlases do not
block each other. Normal connect/read budgets are eight seconds; the existing
longer artwork budgets remain. Requests explicitly close their connection after
the response. TLS pin verification and session resumption remain unchanged.

A transient timeout, unreachable response or busy/server error after connecting
retries quietly with the last snapshot. Three failed polls, or a failed poll whose
last snapshot is at least ten seconds old, marks it offline. Firmware-update
outage holding still applies. TLS identity and other non-transient errors remain
visible immediately. A successful read clears the failure and revalidates the
identity/boot/revision as usual. This scheduling never retries a game write.

Connection labels reserve the same width, and cached telemetry remains visible
while reconnecting. Connection failures no longer insert a row above the game;
shared-room feedback uses a reserved row outside play controls. Routine artwork
status is available in More > Atlas > Atlas connection.

## First-run setup (2026-09-30)

One flow for the Android app
([First-run setup](../Documentation/engineering/FIRST_RUN_SETUP.md)).

- `GET /api/setup` (no sign-in): `{"stage":"welcome"|"finished"|"complete",
  "adminExists":bool,"passwordIsDefault":bool,"ssid":"TurnHub-Atlas",
  "uploadApprovalRequired":bool}`.
  Never carries the password. An Atlas from before this route answers 404;
  clients treat that as `complete`.
- `POST /api/setup/finish` accepts `uploadApprovalRequired=0|1`. Requires a
  verified Admin, Welcome, between games, and enabled/configured/connected
  home Wi-Fi with a ready HTTPS key. The client provisions home Wi-Fi through
  `/api/network/home` and learns its endpoint/key before Finish. It saves the
  stage, replies with `restarting:true` and restarts with the hotspot off. No
  hotspot password change is required. Bad approval is 400, missing access
  is 403, incomplete networking or a refused stage is 409.
- Setup also uses `POST /api/profiles/register`, the presence routes above,
  `POST /api/accounts/setup`, `GET /api/devices`, `POST /api/firmware`
  (multipart field `firmware`), `POST /api/sigil-firmware`,
  `POST /api/sigil-update` and `GET /api/sigil-firmware`.

## Device administration (Admin permission)

Session-token authenticated; other accounts get 403.

- `POST /api/device/forget` with `module=<id>` or `all=1` forgets one or every
  paired Sigil (lobby only; refused with 409 while anyone is seated on that
  Sigil). Success: `{"ok":true,"message":...}`; refusal: 409 `{"error":...}`.
- `GET /api/pairing` returns `{"windowMs":15000,"sigilWindowMs":15000,
  "choicesMs":[15000,30000,60000]}`: Atlas's pairing window, the Sigil's fixed
  window and the allowed choices. `POST /api/pairing` with `windowMs` saves one
  of the choices (anything else: 409, nothing stored).
- `GET /api/power` returns `{"screenOff":0,"screenOffChoices":["Auto","30 s",
  ...],"gameScreen":0,"gameScreenChoices":["On battery","No","Yes"],
  "autoSleep":1,"autoSleepChoices":["Off","5 min",...]}`: Atlas's screen-off
  timer, whether the screen may go off during a match, and deep sleep on
  battery, each an index into its labels. `POST /api/power` with any of
  `screenOff`, `gameScreen`, `autoSleep` (indices; omitted fields keep their
  value) saves them: 400 for an unknown index, 409 when saving fails.

## People: accounts and roles

`GET /api/accounts` (session token) lists accounts: everyone for an Admin,
non-archived accounts for a Game Master, only the caller otherwise. Each entry
has `profileId`, `name`, `permissions` (bits: 1 Admin, 2 Game Master,
4 Developer, 8 GM reset connections, 16 GM remove from game, 32 Tablet
access), `archived` and
`avatar`; Admins
and Game Masters also get `hasPin`, `primary` (the initial Admin), `atTable`
and `reconnectRequired`, so clients can explain a blocked action up front.
`POST /api/accounts/permissions` (`profileId`, `permissions`) and
`POST /api/accounts/archive` (`profileId`, `archived=0|1`) need Admin;
Atlas validates every rule: roles need a PIN, the initial Admin keeps
Admin and cannot be archived, and archiving waits until the account has left
the table.

`POST /api/accounts/merge` (`profileId`, `into`; Admin, Atlas 0.7.18+) merges
two accounts of one person: `profileId`'s statistics and moderation history are
added to `into`, then `profileId` is archived and remembers `into`. Both must be
different, unarchived and away from the table, and the initial Admin can't be
merged away. Afterwards `/api/profiles/sync` for the merged ID answers 409
`{"mergedInto","linkKey"}` (a key for the kept account), and imported games
naming it count for the kept account, deduplicated across both IDs. A power cut
between saving the kept account and archiving the other can leave both live with
the totals on each (*Needs verification*).

## Sigil accessibility preferences

`GET /api/session/accessibility` and `POST /api/session/accessibility`
(authenticated with the session token) read and change the signed-in
profile's own Sigil accessibility preferences: `sigilSound` (form `0`/`1`),
`ledStyle` (`standard`, `reduced-motion`, `monochrome-safe`), `longPressMs`,
`winHoldMs` and `lifeApprovalMs` (how long other players' life-change requests
wait for this player: one of `lifeApprovalOptionsMs`, 15000, 30000 or 60000).
Omitted POST fields keep their saved values. Hold times are whole
multiples of `stepMs` (250) within the returned `limits`, and the win hold must
be at least `minGapMs` (1000) longer than the long press; anything else returns
400 and nothing is stored. No session returns 401; an unreadable or unwritable
record returns 503. Both methods return the saved values plus `limits` and
`stored` (false when Atlas could not read the saved record and is showing the
defaults) - see [accessibility-v1.schema.json](accessibility-v1.schema.json) and
[the example](examples/accessibility.response.json).

These are profile settings, like `/api/session/policy`, not table state: they
take no revision check. Atlas applies them to
the player's physical Sigil within about two seconds (see
[Accessibility](../Documentation/engineering/ACCESSIBILITY.md#implemented-accessibility-settings)).

## Fixtures and remaining work

[Response fixtures](examples/) come from host scenarios with public identity,
epoch and revision normalized for readability: `info`, `state-idle` (no hub),
`state-hosted` (a hub holds the table) and `accessibility`, plus the shared
match record. `intent-v0.1.schema.json` remains a draft. `/api/v1/intent` and
`/api/v1/events` do not exist and are advertised as unsupported.

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

## Game import (2026-10-07)

The app runs every game itself (a hosted game, or a phone-only one with no
Atlas at the table). Each finished game is kept on the
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
  (exact profile ID chosen on the destination Atlas, or empty for a guest who
  is not credited; at least one player must have one), `turns<i>`, `turnMs<i>`,
  `fastest<i>`, `longest<i>` (completed turns and their times), `out<i>`
  (1 for the first player out, 0 for anyone still in).

Atlas validates the whole mapping before writing a new receipt or statistics:
every profile must exist, be readable and not archived, with no duplicate ID.
Names are display labels and never an identity fallback. Answers
`{"ok":true,"duplicate":false,"credited":N,"already":A,"guests":G,"unmatched":[names]}`.
Receipts are per (record, profile) (2026-10-28): sending a record again after
another of its players was linked credits only the newly mapped players;
`already` counts mapped players this record had credited before (skipped, also
if their profile was archived since). When no mapped player is new, Atlas
answers `"duplicate":true` and changes nothing. 400 for malformed/foreign/invalid mapping:
the app keeps the result and reason for explicit review, with no automatic retry.
503 when Atlas couldn't save: pending delivery stays for retry with the same ID
and mapping. A successful acknowledgement retains the local result and marks
it imported; partial credit/unmatched players are displayed, not retried.

Atlas remembers the last 128 (record, profile) receipts (hashed, NVS `sgimport`)
and saves them before any statistics, so a power cut can lose one game's statistics but does not count a replay twice while its receipts remain in that
window. It does not guarantee arbitrary long-term replay protection. Imported games add to games played, won, eliminated and
started, completed turns and turn times, and set the last-game fields like a
live game.

Optional per-player fields (`i` from 0), each validated before any receipt or
statistics write:

- `concededN` (0 or 1, default 0); 1 requires nonzero `outN`.
- `eliminatedByN`: the credited opponent's 0-based match index, or empty for
  unknown; only a non-conceding removed player may have one, never a teammate.
- `selfEliminatedN=0|1`; true requires an exit order and no concession,
  bag-drawn flag or opponent credit.
- `bagDrawnN=0|1` (Bag of Doom only); true requires an `outOrder`, no
  concession and no `eliminatedByN`. It records the exit reason without
  awarding an opponent an elimination.
- `twoHeadedGiant=0|1` for the whole game.

Imports accept up to 10 players; `winners` supports indices 0 to 9. Profile
statistics return `eliminationsMade` in both `lifetime` and `lastGame`.

### Match records and CSV (2026-10-29)

A finished game is one match record, the same on every device
(`game-record-v1.schema.json`, built by the shared core's `gameRecord` and the
app's `StandaloneGame.record()`; example `examples/match-record.json`). Records export as CSV, one row
per player per game, with the header and columns of `examples/match-record.csv`:
times are UTC (`started_at_utc`) and milliseconds; `seat`, `eliminated_by_seat`
count from 1; booleans are `true`/`false`; empty means none. Text is RFC 4180
quoted, and a name starting with `=`, `+`, `-` or `@` gets a leading `'` so a
spreadsheet shows it rather than running it. The app and Atlas produce the
same text (checked against the example by both test suites).

**Atlas match history** (*Implemented, host tests only*): Atlas keeps records
on its microSD card, one JSON line per game in `turnhub/games.jsonl` and the
same game's CSV rows in `turnhub/games.csv` (header written once), readable
over the USB-drive mode. A record is kept once per record ID; a torn last line
is skipped. Without a card nothing is kept and the NVS totals still count.

- `POST /api/standalone/import` takes an optional `record`: the app's whole
  record as JSON text (at most 16 KB). It must have the import's `recordId`
  and player count, or the import is refused (400) before any credit. Atlas
  keeps it with each player's mapped Atlas profile ID (null for a guest),
  the imported names and no `localId`. The answer adds `history`: `kept`,
  `already_kept`, `no_card`, `invalid`, `write_failed` or `not_sent`.
- `GET /api/profiles/history?profileId=&skip=&limit=` (that profile's own
  session, or `linkKey` as for profile sync; 401 otherwise) answers
  `{"ok":true,"records":[...],"more":bool}`: the records the profile played
  in, oldest first, `limit` 1 to 20 (default 10) after `skip`. 503 without a
  card.

A hosted game's record reaches the history through the hub's finish report ([hosted-v1.md](hosted-v1.md#resume-and-records)).

## Importing a phone-owned account

`POST /api/profiles/register` accepts optional `profileId` on Atlas 0.7.15+.
It must be exactly eight hexadecimal characters; Android generates uppercase
IDs while retaining a separate UUID for local history. Existing name/PIN rules,
capacity and session-slot checks apply. A malformed ID returns 400; any existing
ID (including marker-only or archived accounts) returns 409. No registration
request may overwrite an existing identity, credentials, permissions or statistics.
The profile marker is published only after its name and credential hash are saved.
Omitting `profileId` preserves Atlas-generated registration behavior.

On Atlas 0.7.18+ (2026-10-29), `profileId` with an empty `pin` uploads a
**PIN-less** local account: it needs a signed-in session (`X-TurnHub-Token`,
401 otherwise) and answers `{"ok":true,"profileId":"…","hasPin":false}`
without a login token. The same ID rules apply (400 malformed, 409 taken). The
first `/api/session/login` supplying a nonempty PIN to that profile sets its PIN, as for a profile made
on a Sigil or the tablet (owner decision 2026-10-28). That answer also carries a
`linkKey` for the uploading phone (below).

### Profile sync (Atlas 0.7.18+, 2026-10-29)

A phone keeps each linked profile in sync without signing in, using a link key:

- `POST /api/profiles/link-key` (signed-in session): `{"ok":true,"profileId","linkKey"}`
  for the session's own profile. A PIN-less upload's answer carries one too.
  Atlas keeps hashes of the newest two keys per profile; a third retires the oldest.
- `POST /api/profiles/sync`: `profileId`, `linkKey` (401 if not current),
  `now` (the phone's wall-clock ms; Atlas takes the time from it unless NTP on
  home Wi-Fi synced recently), `activity` (when the phone's copy last changed), `name`,
  optional `color` (`#rrggbb` or `none`), `avatar` (0 or a preset id) and
  `reduceMotion` (`0`/`1`). The newer copy wins as a whole: if the phone's
  `activity` is later, Atlas takes its settings; otherwise Atlas's stand. A
  change made on Atlas before any phone told it the time since boot counts as
  newest. Statistics never sync this way (matches travel through the import).
  Answers `applied` (`phone`, `atlas` or `same`), Atlas's resulting `activity`,
  `name`, `color`, `avatar`, `reduceMotion`, `hasPin`, and the core
  `gamesPlayed`/`gamesWon`. PINs are not synced (each side keeps its own).
  Optional `createdAtMs` is the profile's original UTC creation time (0
  unknown); the earliest known time wins on both sides and survives merges and
  account copies, and the answer carries Atlas's `createdAtMs`.
- `POST /api/time`: `utcMs` (UTC ms, 2020 to 2100). The app sends it on
  connect so Atlas can date bookkeeping (profile creation, activity); it is
  never a game timer or an authorization deadline. NTP on home Wi-Fi takes
  precedence for six hours after a sync. Answers `{"ok":true}`, or 400.

To merge future imports into an existing account, Android authenticates through
`/api/session/login`, verifies `/api/session/me`, then saves a scoped link and
revokes the temporary token. This does not modify the active table login. It
retains both histories and sends local match records through the existing import
validator rather than copying computed lifetime totals. New accounts copy
personalization using ordinary APIs; card availability, size limits and Admin
image review remain enforced. Existing accounts retain their personalization.


### Plane catalog and saved commanders

Plane catalog (Atlas 2026-10-30): `GET /api/planes` answers `version` and
`count`; `POST /api/planes` (any signed-in session) takes `version`
(up to 24 characters) in the query and the planes as a `text/plain` body, one
line per card
`<36-character Scryfall id><TAB><name, up to 40><TAB><1 phenomenon | 0>`, up to
192 cards. Atlas keeps it in RAM at once and answers `{ok, count, queued}`;
with a card in, its SD worker then writes the records (`pl0`…, `plv`), and
Atlas reloads them at boot or card insertion. No app sends them since
2026-11-04: the Atlas-run game's Planes tab was removed, and the hosted
game's plane card on the Atlas screen is staged (`STAGED_CHANGES.md`).

Plane cards (Atlas 2026-11-03, for the Atlas screen's plane card): `GET
/api/planes` also answers `cards` (ids whose text Atlas keeps) and `art` (ids
with art too). `POST /api/planes/card` (any signed-in session) takes
one catalog plane's `id`, `type` and `artist` (up to 47 printable ASCII
characters each), `text` (rules, up to 639, newlines allowed) and `chaos` (the
chaos ability, up to 319) in the query, and the art as a `text/plain` body: a
base64 baseline JPEG, 480x320, at most 96 KB, or empty for text only. Both
routes take their large part as a plain body because Atlas's web server
URL-decodes form fields one character at a time: a 64 KB art field held it
for about 30 s, freezing the screen (2026-11-03). The app keeps the JPEG under
about 46 KB. Atlas answers 202 `{ok, queued:true, art}` once the card is
queued and its SD worker writes it to the microSD card's `planes` folder
(8.3 names from a hash of the id) in the background; 400 for bad base64, 409
for an unknown plane, bad text, a non-JPEG or no card, and 503 while the
queue (three cards) is full. `GET /api/planes` answers `cards` and `art` from
the worker's index of that folder, read once per card mount, never from the
card itself, and 503 while a new card's index is still being read. The card
work ran on Atlas's main loop before (2026-11-03): about 320 lookups froze the
screen for up to 110 s. On 503 the app sends the same request again after
1.5 s, for up to about 90 s. Optional `index` and `total` (this card's place
in the batch) let the Atlas screen show the progress as each card is
written.

Saved commanders: up to six per profile on the microSD card (`d<profileId>`).
`GET /api/profiles/commanders` (the session's own, or `?profileId=`) answers
`commanders: [{slot, name, art}]`, newest first. Choosing a commander for a
game is the hub's; the match record carries it.
