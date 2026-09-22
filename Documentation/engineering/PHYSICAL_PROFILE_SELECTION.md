# Physical profile selection and controller assignment

Status: **Partially implemented locally**: Atlas profile-policy settings,
authorization checks, and Atlas-owned primary-seat persistence. The physical
picker and selectable startup remain planned.

## Observed gap

Michael joins through a browser. An unjoined Sigil remembers Michael from its
previous use. Pressing its join button resolves the saved MAC/slot binding back
to Michael and rejects the duplicate participant. Another person cannot choose
their own profile using that Sigil's buttons.

The duplicate-participant check must remain. The defect is using a remembered
profile as the only physical join choice. Do not fix this by duplicating Michael,
silently assigning a guest, clearing his profile, or moving his browser session.

Current implementation evidence:

- `controller_profiles.cpp::profileForSeat` reads persisted physical bindings.
- `main.cpp::handleTableIntent` rejects Join when that profile already participates.
- Physical attachment from an authenticated browser can replace an unjoined
  Sigil's binding today, but a standalone physical picker does not exist.
- Sigil has Pass and Action buttons and a 250x122 monochrome e-ink display.
  The display currently uses full-window rendering; do not assume partial
  refresh performance or introduce animation as a requirement.

## First implementation slice: profile policy (2026-09-21)

- Settings exposes independent physical-use and stats-privacy choices, saved by
  the authenticated owner through `POST /api/session/policy`. Caller-supplied
  profile IDs cannot select another owner's settings.
- Atlas stores an explicit three-byte versioned policy at `a<profileId>` in its
  existing `turnhub` namespace. Missing records preserve physical play and hide
  unauthenticated stats by default. Corrupt/unreadable/future records fail closed
  and are not overwritten. Existing profile IDs, bindings and stats remain intact.
- Disabling PIN-free physical use requires a saved PIN. New physical primary or
  secondary joins require a live same-profile browser session when that choice
  is disabled. Existing participation continues after logout/expiry or a policy
  change; authentication loss never ejects a player or interrupts a match.
- Protected profiles require PIN login for browser access even when PIN-free
  physical play is allowed. PIN-less legacy profiles retain physical browser
  bootstrap; pending claims recheck credentials and profile identity at approval.
- Atlas exposes a stats-visibility policy query for the future physical renderer:
  same-profile authentication unlocks it; any remaining live companion session
  keeps it unlocked; logout/revocation/expiry of the last session removes access.
  This query does not refresh session lifetime. Hidden statistics still accumulate.
  No Sigil stats screen or new unauthenticated stats endpoint is added in this slice.
- Device Settings now groups existing Atlas-persisted Sigil naming in the portal.
  Startup mode is not exposed until the coordinated Atlas/Sigil picker is ready.

Feature boundary: profile repository owns policy persistence; owner-authenticated
settings requests change policy; Atlas authorization queries gate join Intents
and future display serialization. Portal controls render the owner choices.
This slice adds no radio packets, Sigil persistence, game-engine rules, or external
dependencies. Forms use native labeled controls and persistent status feedback.

## Temporary Sigil seats (2026-09-22)

Both seat assignments live only in Atlas RAM. Restart/reconnect and game end
clear them; legacy remembered-seat keys are removed without deleting accounts,
credentials, or statistics. The portal no longer offers a remember-seat choice.
Sigils cache display names only in RAM and retain only their radio pairing in NVS.

Game completion records statistics against the match's captured profile IDs
before releasing seat assignments. Atlas retains the completed roster until the
host chooses Reset or Rematch. Rematch restores the captured physical assignments;
Reset opens an empty lobby. Neither operation records the same results twice.

Native tests cover cleared bindings, preserved statistics and rematch restoration.
Firmware builds pass; physical restart/display behavior still needs a bench check.

## Product contract

Profiles belong to people, not controllers. Physical assignments are temporary
and scoped to table participation; they do not survive a restart or reserve a Sigil.

- One profile has one participant at the table.
- A phone and physical Sigil may control the same participant concurrently.
- Selecting another person on an unjoined Sigil does not change Michael's
  participant, browser tokens, name, credentials, or statistics.
- Profile selection and attachment occur in the lobby. No mid-game reassignment.
- Selecting an already participating profile offers explicit attachment, subject
  to authorization, instead of creating another participant.
- If that participant already has another physical controller, retain the current
  rejection until a separate transfer flow is designed. Never silently steal it.
- Shared Sigil seats A and B select identities independently. Changing B must
  not change A. Leave/reassignment must respect existing shared-seat constraints.
- Guests have temporary slot participation. A released guest profile record is
  not treated as a remembered owner; cleanup/guest-directory presentation is
  separate from releasing the slot binding.

## Proposed physical interaction

Startup behavior is user-selectable: last-used profile or profile selector.
Remembering a profile never bypasses its physical-use policy or creates a second
participant. Startup mode belongs to that Sigil's Atlas-owned Device Settings.
The default and whether last-profile startup
selects or also confirms joining remain interaction decisions to settle.
Always provide a route to change player. In the picker, show the last-used name
first, plus Change player,
Guest (when supported), and Cancel. Pass cycles choices and Action selects;
display those labels on every menu. All essential actions use short presses.

The confirmation screen says `Join as Michael?`, or `Attach to Michael's seat?`
when he already participates. Provide an explicit Back option. The directory
shows a bounded page of names, with navigation and a selection marker; do not
require the complete directory to fit on the screen or in a radio packet.
Duplicate names need a stable distinguishing label without exposing credentials.

After acceptance, show Atlas's confirmed assignment. A rejected request leaves
the device unjoined and shows the reason with a route back to selection. Merely
highlighting a name never changes canonical participation or persistent state.

While a menu is active, button gestures must not leak into Join, Start, Win, or
other gameplay actions. Cancel pending long-press state on mode changes. Give
selection requests identities so retries and a held/released button cannot
confirm a different item or join twice. Drop obsolete menu responses.

Render only changed menu state and coalesce redundant refreshes. Confirm against
the displayed menu revision, not a newer unseen selection. Slow e-ink refresh
must not queue invisible choices. Measure responsiveness on the actual hardware.
The browser remains an accessible equivalent through the same application
service, including keyboard and assistive-technology support.

## Owner-selected access and privacy policy

Product decision, 2026-09-21: profile owners choose independently in the portal
or future app:

- **Allow physical use without a PIN?** Permission to use that profile from a
  physical Sigil without entering its PIN. This does not authenticate a browser
  or authorize changes to profile credentials/settings.
- **Hide stats without authentication?** Whether statistics remain hidden on
  unauthenticated presentation surfaces, including a Sigil if it gains a stats
  display. This controls visibility, never statistics accumulation.

Allowing PIN-free physical use and hiding unauthenticated stats is a supported
combination: play and statistics recording continue, but the physical display
does not reveal protected statistics. Atlas enforces visibility before sending
data; merely hiding received values on the Sigil is insufficient. This decision
does not itself require adding a Sigil statistics screen.

Physical selection must never block browser login or companion control. Login
as the same profile enables full profile/statistics display on its connected
Sigil, including when physical play began without a PIN. Another profile's login
does not unlock that display. All controllers still share one participant and
one statistics attribution.

Keep browsing, gameplay permission, and authenticated display permission
separate. When PIN-free physical use is disabled, require authentication before
physical join/attachment. Browser-assisted authorization remains a supported
path; direct two-button PIN entry is not yet a decided requirement.

The first slice defines missing-policy defaults and session-based display
authorization above. A disconnected browser's session remains valid until logout,
revocation, or its existing eight-hour inactivity expiry. Before adding a physical
stats display, define removal of previously rendered private data on e-ink when
authorization ends or assignment changes, including loss of Atlas connectivity.
Profile policy edits require owner authentication. User choice should govern
these product preferences even when supporting alternatives takes more work;
the same Atlas ownership and validation rules apply to every option.

If direct PIN entry is chosen, use the two buttons to select digits and explicit
Back/Cancel controls, mask completed digits, and rate-limit at Atlas per profile
across transports. Never put hashes in directory/menu responses or log PINs.
Review the actual ESP-NOW pairing/encryption configuration before transporting
credentials. Do not treat the browser's current hash format as a radio protocol.

## Device Settings ownership

Product decision, 2026-09-21: provide an Atlas-owned Device Settings area for
per-Sigil preferences. The Seat A remember choice is exposed from the Players
device card because it is an owner-authorized binding action; Seat B has no
persistence setting. Startup mode remains planned. Atlas stores and validates
device settings keyed to the physical device; profile access and stats-privacy
choices remain profile settings.

Sigils do not persist user settings. They render and apply the current settings
supplied by Atlas as disposable runtime state. Setting changes are requests to
Atlas, never local authoritative writes. After reconnect/reboot, Atlas supplies
the effective configuration before profile selection or attachment proceeds.
An unavailable Atlas cannot be replaced by a Sigil's remembered settings.

Minimum device identity and pairing bootstrap material needed to reconnect are
separate from user settings and remain subject to the pairing contract; they do
not authorize the Sigil to choose profiles, permissions, or game state. Device
Settings editing permissions, defaults, and reset/re-pair behavior still need
definition before implementation.

## Mandatory feature boundaries

| Concern | Owner and boundary |
| --- | --- |
| Profiles, credentials, totals | Existing Atlas repositories; IDs and statistics formats remain unchanged. |
| Device settings and last-used preference | Atlas device-settings owner; startup mode keyed by device, last-used profile keyed by device/slot; no Sigil-side settings persistence. |
| Physical-use and stats-visibility settings | Atlas profile repository; independently persisted owner choices, validated by Atlas policy/authentication services. |
| Authenticated Sigil display | Atlas resolves same-profile authentication and live assignment before serializing permitted data; authorization is not a remembered profile preference. |
| Participant identity and live assignment | Atlas application service; canonical profile-to-participant and controller-to-participant associations. |
| Browse/menu state | Bounded selection service on Atlas; Sigil renders a revisioned view and sends navigation/confirmation requests. |
| Authorization | Atlas policy/authentication service validates actor, profile, proof and request lifetime. |
| Join/attach/leave | Semantic Intent handlers validate lobby, capacity, assignment and shared-seat rules. |
| Gameplay | Existing engine receives resolved participants/actions; no menus, PINs, NVS or radio dependencies. |
| Transport | Radio/HTTP adapters encode requests/results and resolve trusted controller identity; clients cannot supply their own authority. |
| Presentation | Sigil e-ink and accessible browser render canonical outcomes; neither mutates the roster locally. |

No new third-party dependencies or assets are planned.

## Compatibility and implementation slices

1. Separate active controller assignment from persisted last-used preference in
   the application service. Lobby profile resolution must use captured live
   identity, not reread an editable preference. Retain participant ID, roster
   position, host/starter and browser authorization during an attachment.
   Cover secondary seats explicitly; the current `replaceController` path
   supports virtual-to-physical primary attachment only.
2. Read existing MAC/slot profile bindings as migration input for preferences.
   Preserve profile IDs, legacy credential compatibility and totals. No NVS
   erase or automatic profile creation during browsing. A missing/deleted
   preference falls back to the directory. A failed preference save must not
   corrupt or partially apply a live assignment; define/report that outcome.
3. Add bounded directory/menu requests, explicit confirm/cancel and accepted/
   rejected results with request identity and menu revision. Select by profile
   ID, never display name or an unvalidated client-supplied directory index.
   Revalidate eligibility at confirmation, since another controller may join
   while the menu is open. Handle disconnect/reset and stale responses safely.
4. Define version/capability negotiation before extending ESP-NOW packets. Both
   firmware builds must agree on payload lengths and validation. Older Sigils
   retain their existing behavior with browser-assisted reassignment; never
   interpret new menu packets as legacy gameplay. The draft JSON protocol is
   not the deployed ESP-NOW wire format.
5. Implement the two-button renderer and input mode on Sigil, then enable the
   agreed authorization policy and primary-seat join/attachment. Complete the
   A/B flow before claiming shared-seat support for the picker.
6. Add guests separately if needed; guest persistence/recovery policy is still
   future work. Never label an ordinary saved profile as a temporary guest.

Do not advertise the picker from Atlas alone. This milestone needs coordinated
Atlas and Sigil firmware and a real e-ink/button bench check.

## Focused acceptance cases

Keep verification proportional: targeted service/adapter tests, one build per
changed firmware target, then a short physical bench run. Avoid another broad
testing expansion unless a failure calls for it.

1. Michael joins by phone; his old Sigil remains unjoined. Another authorized
   player selects their own profile and joins on it. Michael is unchanged.
2. Michael explicitly attaches that Sigil instead. His participant ID and seat
   order remain unchanged; phone and buttons both control him; totals count once.
3. Cancel, denied authorization, full table, stale confirm, repeated packet and
   starting a game while a menu is open leave no partial assignment.
4. Shared A/B selection preserves the other person's identity and controls.
5. Restart retains profiles/totals/preferences without restoring an unauthorized
   live assignment. An old Sigil remains usable with the new Atlas.
6. E-ink latency does not select an unseen item; menu presses do not trigger
   gameplay; accessible browser attachment reaches the same validated outcome.
7. Exercise all combinations of PIN-free physical use and authenticated-only
   stats visibility. Hidden stats still accumulate once; unauthorized responses
   contain no protected values.
8. Same-profile browser login after physical joining enables the connected
   Sigil's full display without replacing the participant. Other-profile login
   does not. Verify display revocation against the agreed lifetime policy.
9. Both startup choices preserve access to Change player, authorization checks,
   duplicate prevention, and simultaneous browser/physical use.
10. Device settings survive through Atlas storage; Sigil restart/reconnect uses
    Atlas's configuration and never publishes a local preference as authority.
