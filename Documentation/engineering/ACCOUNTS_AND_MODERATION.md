# Local accounts and moderation

Implemented locally on 2026-09-21; firmware build and focused native/browser checks
pass. Hardware playtesting remains required. No profile wipe or flash performed.

## Initial setup

On a new Atlas or an existing installation without an Admin, the portal shows a
setup banner. Create or sign into a PIN-protected account, hold the physical Atlas
master button, and select **Make my account the initial Admin**. This explicitly
establishes the administrator; neither a display name nor first registration
automatically grants privileges. The initial Admin cannot be demoted in this slice.
Subsequent privileged accounts are configured in Device Settings. Existing profile
identifiers, PINs, statistics and bindings remain compatible.

## Independent permissions

- Admin: account permission assignment, device naming, network configuration and
  firmware upload. Existing physical/state gates still apply to system changes.
- Game Master: force pass, nudge mute/unmute, and private moderation history.
- Developer: developer page and runtime diagnostics. Not implied by Admin.
- GM reset connections and GM remove from game: independent additional choices;
  either requires Game Master. Admin, Game Master and Developer can be combined.

A normal account has none of these permissions. Table host remains a per-game
capability, not an account permission. Permission checks run on Atlas for every
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

Connection-reset and game-removal counts persist separately. The record is saved
before disconnecting; storage failure rejects the action. Repeating an already
applied reset/removal is rejected without incrementing its count. Ordinary network
loss, voluntary logout and power cycles do not increment either count. These counts
are visible only to their account and Game Masters, never merely because the reader
is an Admin or Developer. Public seats, Sigil displays and normal statistics exports
do not include them. This is a counter implementation, not a detailed audit log or
a power-failure transaction journal.

## Portal organization

- Game: game setup, turns and life totals.
- Players: table roster, physical Sigil sign-in/attachment, Game Master controls.
- My Account: name, PIN, privacy, personal statistics, browser feedback and private
  moderation counts.
- Device Settings (Admin): device names, network, firmware and account permissions.
- Developer link (Developer): protected diagnostics.

The old System tab and duplicate header links have been removed. Sigil login and
attachment no longer live alongside device configuration.

Archived accounts remain visible to Admins at the bottom of the account list so they
can be restored. They are omitted from Game Master account results and moderation
controls, and cannot be used as a moderation target.

## Storage and API

Atlas NVS namespace `turnhub` owns `acctadmin` (initial Admin's eight-character ID
plus terminator), and `u<profileId>` (12-byte account record). Schema byte 1 is followed
by permission bits, nudge mute, reconnect-required, then two little-endian uint32
counts. Missing account records default to ordinary access; corrupt/future records
fail closed. The single bootstrap record establishes the initial Admin atomically.

- GET/POST `/api/accounts/setup`: setup state / physically confirmed bootstrap.
- GET `/api/accounts`: self, or account directory for Admin/GM; private fields filtered.
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
