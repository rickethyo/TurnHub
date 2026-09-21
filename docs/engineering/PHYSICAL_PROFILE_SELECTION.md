# Physical profile selection and controller assignment

Status: **Planned**, not implemented. Next identity milestone, before game-scoped
statistics. Recorded after the owner's successful compile/flash report and
physical-controller reuse observation on 2026-09-20.

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

## Product contract

Profiles belong to people, not controllers. A saved physical binding becomes a
last-used preference. It neither reserves that Sigil nor authorizes arbitrary
profile access. The live assignment is separate and scoped to table participation.

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
- Guests, if implemented, have temporary participation with no durable profile
  statistics. Never attribute guest results to the last-used person.

## Proposed physical interaction

In the lobby, an unjoined Sigil opens a picker rather than immediately joining
its remembered identity. Show the last-used name first, plus Change player,
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

## Authorization decision still open

The owner has not chosen whether physical possession is sufficient to select a
saved profile, or whether a protected profile requires its PIN on the Sigil.
Do not silently adopt either policy or broaden existing physical approval into
permission to impersonate any profile.

Keep browsing separate from authorization. An authenticated browser can already
authorize attaching its own profile with physical confirmation. For standalone
physical selection, define a policy service before enabling protected-profile
confirmation. A conservative implementation can keep that confirmation disabled
until the policy is settled; that is not completion of standalone profile login.

If PIN entry is chosen, use the two buttons to select digits and explicit
Back/Cancel controls, mask completed digits, and rate-limit at Atlas per profile
across transports. Never put hashes in directory/menu responses or log PINs.
Review the actual ESP-NOW pairing/encryption configuration before transporting
credentials. Do not treat the browser's current hash format as a radio protocol.

## Mandatory feature boundaries

| Concern | Owner and boundary |
| --- | --- |
| Profiles, credentials, totals | Existing Atlas repositories; IDs and statistics formats remain unchanged. |
| Last-used preference | Controller preference persistence, keyed by physical device and slot; advisory only. |
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
