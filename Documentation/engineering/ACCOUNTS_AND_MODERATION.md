# Local accounts and moderation

Implemented locally on 2026-09-21; firmware build and focused native/browser checks
pass. Hardware playtesting remains required. No profile wipe or flash performed.

## Initial setup

On a new Atlas or an existing installation without an Admin, the portal shows a
setup banner. Create or sign into a PIN-protected account and select **Make my
account the initial Admin**. The Atlas screen shows a six-digit code and a QR
code; enter it on that phone, or scan it, to prove you are at the table (the
presence code, 2026-09-25). This explicitly
establishes the administrator; neither a display name nor first registration
automatically grants privileges. The initial Admin cannot be demoted in this slice.
Subsequent privileged accounts are configured in Device Settings. Existing profile
identifiers, PINs, statistics and bindings remain compatible.

## Independent permissions

- Admin: account permission assignment, device naming, network configuration and
  firmware upload. Existing physical/state gates still apply to system changes.
- Game Master: force pass and nudge mute/unmute. Game Masters do not see other
  accounts' moderation history.
- Developer: developer page and runtime diagnostics. Not implied by Admin.
- GM reset connections and GM remove from game: independent additional choices;
  either requires Game Master. Admin, Game Master and Developer can be combined.

A normal account has none of these permissions. There is no table host (owner
decision 2026-09-25). Any seated player may start (the countdown can be
cancelled by any seated player), pick the starter, change the next game's
settings, rematch or reset. Permission checks run on Atlas for every
protected request, not just in the UI. Privileged accounts cannot remove their PIN.
The restricted HTML routes first serve an authentication shell; actual page contents
require the session header. Tokens are never placed in URLs.

## Moderation

The Players tab exposes Game Master actions, with a confirmation explaining each
action. Requests enter the Moderate Intent handler, which rechecks permission and
target state. Force pass acts immediately, preserving the normal game restrictions
around pause and outstanding decisions.

Reset connections revokes every browser session and pending claim for the target
account, and suspends its Sigil controls until a successful PIN sign-in. Its seat,
life totals and game state remain intact. This is a logical suspension on Atlas;
the physical radio device is not rebooted or unpaired. A shared A/B Sigil is
suspended as a unit until the affected account signs in again, avoiding a shared
button bypass. This deliberately simple shared-device behavior can be refined later.

Remove from game additionally leaves the lobby or eliminates the participant using
the existing concession flow. Outstanding win/elimination decisions must finish
first. A primary lobby seat with a secondary player must remove that secondary seat
first. Removal does not delete the account or its statistics. The player can sign
in again; an eliminated player cannot resume active play in the current match.

Both disconnect actions currently require a PIN on the target account so that
reconnection can be authenticated. Legacy PIN-less physical profiles must set a PIN
first. This slice is connection reset/removal, not a permanent account ban.

Nudge mute is persisted now for the future nudge feature; there is no nudge sending
implementation yet. Future nudge handlers must enforce the stored flag on Atlas.

Connection-reset and game-removal counts are the account's private moderation
history. Since 2026-09-24 they live with the profile's statistics, not in the
account's access-control record. The count and the reconnect requirement are saved
before disconnecting; a storage failure rolls both back and rejects the action.
Repeating an already applied reset/removal is rejected without incrementing its
count. Ordinary network loss, voluntary logout and power cycles do not increment
either count.

The history is very private. Atlas serves it only to the account's own session,
and only when that session is PIN-verified: it signed in with the PIN, registered
with it, or set a new one. A session obtained by pressing Action on a Sigil sees
that the history exists but not the counts. No other account can read it, whatever
its permissions (Admin, Game Master or Developer). Public seats, Sigil displays,
the account list and statistics downloads never include it. This is a counter
implementation, not a detailed audit log or a power-failure transaction journal.

Feature gate (2026-09-24 move): state owner Atlas profile statistics; changed only
by the Moderate Intent handler; validated there; persisted in `o<profileId>`;
rendered by the statistics page only; no radio or client-contract change (the
`/api/session/stats` response gains a `moderation` object and `/api/accounts`
drops the counts); no new dependency; the statistics card uses text labels and a
sign-in link, and the locked state is announced as text, not color.

## Portal organization

- Game: game setup, turns and life totals.
- Players: table roster, physical Sigil sign-in/attachment, Game Master controls.
- My Account: name, PIN, privacy, appearance (per-browser theme), personal
  statistics and browser feedback. A card points to the statistics page for the
  private moderation history.
- Device Settings (Admin): device names, network, firmware (under the Atlas card) and
  account permissions.
- Account menu (top right once signed in): account settings, personal statistics,
  Developer diagnostics (Developer only) and log out. Signed-out visitors see a Sign in
  button in the same place.

The old System tab and duplicate header links have been removed. Sigil login and
attachment no longer live alongside device configuration.

Archived accounts remain visible to Admins at the bottom of the account list so they
can be restored. They are omitted from Game Master account results and moderation
controls, and cannot be used as a moderation target.

## Storage and API

Atlas NVS namespace `turnhub` owns `acctadmin` (initial Admin's eight-character ID
plus terminator), `u<profileId>` (12-byte account record: schema byte, permission
bits, flags for nudge mute and archive, reconnect-required, then two legacy
little-endian uint32 counts that are now always zero after migration) and
`o<profileId>` (9-byte moderation history: schema byte 1, connection resets, game
removals). Missing account records default to ordinary access; corrupt/future
records fail closed, and an unreadable history record blocks migration rather than
being overwritten. The single bootstrap record establishes the initial Admin
atomically.

- GET/POST `/api/accounts/setup`: setup state / physically confirmed bootstrap.
- GET `/api/accounts`: self, or account directory for Admin/GM. Never includes
  moderation counts.
- GET `/api/session/stats`: includes `moderation`, either
  `{"visible":true,"connectionResets":n,"gameRemovals":n}` for the owner's
  PIN-verified session, or `{"visible":false,"reason":"..."}`.
- POST `/api/accounts/permissions`: Admin assignment of independent permissions.
- POST `/api/accounts/moderate`: GM action, with reset/removal subpermission checks.
- `/api/session/me`: effective permissions for navigation.
- Protected system routes: device naming, network information/password, developer
  HTML/runtime diagnostics, firmware HTML and upload.
- GET `/api/diagnostics/log` (Developer): RAM serial-log download as text. The
  Wi-Fi password line is redacted in the download.

Focused checks cover setup confirmation, combined/independent permissions,
unauthorized access, last-initial-Admin protection, force pass, session revocation,
reconnect, removal, counter privacy/idempotence, record encoding/reopening and write
failure, plus account setup/navigation in the browser smoke test.
