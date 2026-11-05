# Staged Changes and Open Checks

The one list of what is agreed but not done: open work, hardware checks still
to run, and product decisions still to make. When an item lands, move any
lasting fact into its topic document and delete the line here; git keeps the
old entries. Longer designs for planned features are in
[Planned Designs](PLANNED_DESIGNS.md).

Implementation and acceptance are tracked separately below. Builds and host/JVM
tests do not close physical-device, accessibility or lifecycle checks.

## Track and scope

Master is the S3/LVGL track; current-generation hardware fixes belong on
`legacy/gen1`. See [Agent workflow](AGENT_WORKFLOW.md) for branch rules and
[the S3 hardware plan](hardware/ESP32_S3_REFRESH.md) for target status.
Pre-release compatibility policy belongs in `CLAUDE.md` and
[Firmware Updates](FIRMWARE_UPDATES.md).

## Open work

### Owner experience roadmap (2026-11-05)

These are product suggestions to refine during design, not immutable UI
requirements. The owner prioritizes the Atlas room as the first implementation
slice and authorizes building, flashing and testing it on connected unlocked
devices. Each slice gets its own reviewable implementation and acceptance round.

1. **Atlas automatically connects one shared room (first priority).** Android
   implementation and three-device smoke test are complete; contract and privacy
   boundaries live in [Phone-hosted games](../../protocol/hosted-v1.md#automatic-atlas-room).
   Remaining acceptance: prolonged outage/host loss, pairing revocation on hardware,
   concurrent Sigil attachment, and exactly-once final match records. Explicit hub
   handoff and iOS room counterpart remain later work. One hub
   phone runs both the rules and the existing Play together room; Atlas only
   relays discovery/attachment and hardware inputs. Pairing/connecting additional
   phones must attach them to that room, not automatically contend for the hub
   lease or leave them on unrelated local games with an Atlas busy error. The
   Game profile picker joins or attaches the selected profile through the same
   validated membership path as Play together. The hub can arrange the lobby
   first; later devices and Sigils attach without duplicating participants or
   statistics. No room-code entry for phones already paired to this Atlas.
   Preserve player/table/display roles, protected account settings, scoped
   credentials, revocation, reconnect behavior and explicit hub handoff. Define
   how the first hub is chosen and expose intentional takeover separately from
   joining. Test three real phones/tablets plus Sigil: concurrent connection,
   same/different profiles, host-prepared lobby, reconnect, host loss, stale room
   credentials, private statistics and exactly one game authority. Include the
   initial-permission race in QR account sharing if the same connection boundary
   needs it; otherwise make it the next focused repair.
2. **Controller identity and profile picking.** Let a Sigil release/change its
   player in setup without rebooting, keeping the person and other controllers
   intact. Simplify Game profile rows by removing repeated device/location text;
   show location where it helps a decision instead. Differentiate profiles with
   their avatar/colour/initials, retaining readable names and non-colour cues.
   Add optional display names with stable IDs unchanged; distinguish display
   names from account identity and decide how history records names at play time.
3. **Profile/account parity and new-device onboarding.** Review and remove
   obsolete account controls before adding missing ones. Preserve offline copies,
   PIN gates for settings/detailed statistics, retention preferences and device
   roles. Initial app setup should offer pulling profiles and suitable settings
   from an existing Atlas. To install device Admin, offer eligible accounts and
   require that account's PIN approval, or create a new Admin. Do not infer or
   transfer device Admin from an ordinary copied profile or Atlas pairing. Resolve
   this against the account-removal work below before deleting authorization
   routes it still needs. Reuse the resulting profile contract across products.
4. **Device-focused settings and contextual codes.** Group Atlas, Sigil and phone
   settings with the appropriate pairing, unpair/forget and reset actions in each
   device section. Use tabs only if they improve the small-screen layout. Remove
   the permanent QR-code destination; show each code at the step that needs it,
   with an accessible text alternative. Put themes under the relevant device;
   let Atlas request Sigil settings and reflect edits made on the Sigil. Keep a
   single canonical setting per device, with acknowledgements/retries rather
   than conflicting bidirectional stores. Destructive actions name their exact
   scope and retain confirmation. Implement phone and Atlas counterparts together.
5. **Atlas Players tab.** Add a dedicated profile/statistics view using the same
   stable-ID directory. Keep public/basic totals visible as appropriate and
   detailed statistics behind the profile's unlock gate. Account for no SD,
   disconnected hub, duplicate names, paging and touchscreen accessibility.
6. **Desktop exploration, not a build commitment.** Compare a Windows companion,
   browser interface and desktop-hosted hub for profile editing and statistics.
   Separate a read-only viewer from authenticated editing or game hosting;
   evaluate existing contracts, offline sync, PIN handling, packaging and cost
   before selecting a platform. Do not create another independent rules authority.

Immediate acceptance gate for slice 1: current HTTPS setup and physical trust,
loopback isolation and reconnect must work on the devices. Builds/tests do not
replace this gate. Smaller follow-ups may be reordered when they unblock room
acceptance; do not expand the first slice into a complete settings redesign.

### Phone-hosted games (owner, 2026-11-04)

The hub phone or tablet runs every game with the shared core; Atlas shows it,
drives the Sigils and keeps profiles, statistics and match history. Phone-less
games go away; Atlas's screen keeps setup and table views, sending every tap to
the hub. Contract: `protocol/hosted-v1.md`. Slices, each a flash-and-test round:

1. **Hosted contract and hub push** (implemented, `claude/master`): lease,
   state and input routes; read-only hosted view on the Atlas screen; the
   app's "Show on Atlas". Open check: push a running game from a phone and
   watch the Atlas screen follow; other paired phones discover the hub's room
   and select a profile on Game (the automatic room slice above).
2. **Sigils** (implemented, `claude/master`): a Sigil joins the hub's setup
   from its menu (bound by `localId`), shows the hub's game, and its menu
   choices and life keys are queued for the hub, which polls every 300 ms.
   Open checks: join two Sigils from setup, pass and change life from them
   and see the phone follow; measure Sigil to hub to Sigil latency (target
   under 200 ms). Not yet: Commander damage and partner pickers, player
   controls, elimination, rematch and seat switching on hosted Sigils.
3. **Atlas screen controls** (implemented, `claude/master`): the hosted game
   screen offers Start game (setup, two or more players), Pass and Pause (a
   living player's turn) and Resume, each queued for the hub for the active
   player; the player tiles only show. Open check: start, pass, pause and
   resume from the Atlas screen and see the phone follow. Not yet: tile life
   taps, the player screen and game options on a hosted game; "Connect a
   phone to start" with no hub waits for slice 6, while Atlas still runs its
   own games.
4. **Records and resume** (implemented, `claude/master`): finished hosted
   games reach Atlas's history and stats through the local-game import, which
   already runs while the hub is connected and signed in. Atlas keeps an
   unfinished game when its hub releases the table or goes quiet. Another
   phone that taps Show on Atlas with no game under way is offered "Resume
   <host>'s game" or "Show mine instead". Open checks: stop hosting mid-game
   on one phone, resume it on another, finish it and see one record on Atlas.
   Not yet: keeping the resumable game across an Atlas restart (RAM only).
5. and 6. **One app path, Atlas's engine deleted** (implemented, PR #196,
   2026-11-04): Atlas's `GameEngine`, `Lobby`, game Intents' handlers, game
   HTTP routes, venue tables, offline merge and recovery record are gone, and
   so are the app's Atlas-run Game tab, seat claims, offline queues, venue
   selector and Atlas Planes tab. `/api/v1/state` is `atlas-state-v1` (hub,
   battery, links). Open check: flash Atlas and a Sigil, install a Preview
   build, host a game from a phone and play it from the Sigil and the Atlas
   screen.

Follow-ups left by slices 5 and 6:

- Planechase on the Atlas screen (implemented 2026-11-05, owner ask): the
  Planes tab and full-screen card read the hub's state, its buttons queue
  planar commands, and the hub sends plane names, text and art while it
  hosts (`HostedPlaneSync.kt`, `protocol/hosted-v1.md`). Transfers retry while
  the plane stays unchanged and confirm saved artwork from the card index.
  Home-Wi-Fi Atlas also fetches missing hosted cards from Scryfall after a
  phone-delivery grace period, converting progressive JPEGs off the main loop.
  Open checks: install the app, flash Atlas, play a digital Planechase game, roll from the Atlas
  screen, open the card view (rotation and touch), and watch the art arrive.
  Verify direct fallback with phone transfer interrupted, plus router/internet
  loss, card removal and recovery while controls and Sigils remain responsive.
- Atlas holds no accounts (owner, 2026-11-05; approved design "Atlas as an
  accessory: phones own profiles"). Steps, each a flash-and-test round:
  1. **Pairing replaces sign-in** (implemented, `claude/master`): a phone
     pairs once (Allow on the Atlas screen, same code on both), its device
     key hosts games and sends planes, and the app pairs and shows its game
     on connect without signing in (`phone_links.h`, `AtlasPhonePairing.kt`,
     `protocol/hosted-v1.md` "Pairing"). Sign-in still works as a fallback.
     Open checks: pair a phone, host and send planes without an account,
     refuse one, reboot Atlas and host again, forget phones.
  2. **Copies, not accounts** (implemented in PR #207): the hub projects stable
     profile IDs into its state, sends ID/name/colour/preset-avatar copies with
     its pairing key, imports records and reads back totals and archived games.
     PINs and pictures stay on the phone. Identity/basic totals work without SD;
     appearance and detailed history need SD. Verify on hardware before the
     account-removal round: two same-name players, finished game, reconnect,
     same-ID history on a second phone, and missing/reinserted SD.
  3. **Remove Atlas accounts**: PINs, sessions, `/api/profiles/*` and sign-in
     go; a factory reset clears the old accounts (no migration before release).
  4. Docs and manuals (App and Hardware Manuals: pairing, no Atlas sign-in).
- `/api/accounts` still reports `nudgeMuted` and the Game Master moderation
  bits (Reset connections, Remove player), which only the app's People sheet
  shows now; decide whether Game Master still means anything on Atlas before
  changing the account layout.
- "Connect a phone to start" on the Atlas screen with no hub (slice 3).

## Games travel with the player (2026-11-05)

Owner ask: a player's profile and games follow them whoever hosts, and games
played apart merge into the host on first connect or reconnect. Slice 1 (Play
together `history` exchange, Android) is implemented; device acceptance is open
(two phones, a game played apart, reconnect after Wi-Fi loss). Next slices:

1. Settings on reconnect: the same exchange carries the profile's name, look and
   PIN verifier, newest `lastActivityMs` wins, so a change made apart lands
   without joining again.
2. Atlas parity: games the hub merged reach Atlas through the existing import;
   Atlas sends a linked profile's archived games (`games.jsonl`) back to a phone.
3. Duplicate accounts with different IDs: an explicit "Same person?" merge that
   keeps the oldest ID, or the Admin's. Never automatic on joining a room.
4. iOS (with its LAN client) and the owner's local-only apps use the same contract.

## Saved game setups on Atlas (next parity slice, 2026-10-31)

Android saves named lobby configurations locally (seats/identities, format, rules, timers, Planechase choices). Since 2026-11-04 the hub runs every game, so this may need only Atlas-side storage of the app's setups; revisit before building. Never replay scores/clocks/history or remap identities by name. Existing app lobby simplification and Advanced settings already share presentation across hosts.

## Persistent rooms: device acceptance and iOS counterpart

Android implements the version-2 room contract in
[Shared devices](SHARED_DEVICES.md). Verify two real devices through process
recreation, Wi-Fi address changes, same-profile reconnect without PIN, game-type
changes and simultaneous facing-panel taps.
Verify the UI with large text and TalkBack. The hub remains the game's
authority; the room does not create an additional takeover path.

iOS remains a foundation. Its future gameplay/transport slice must implement
version-2 persistent membership, secure reconnect storage, profile-first joining,
PIN-once links, read-only public views and account-free shared table controls.
LAN special setup/decision commands beyond normal gameplay remain host-only;
extend the shared command vocabulary and server validators together when adding
remote support. Explicit cross-ID account merging keeps the existing Admin
merge workflow; entering a room never silently combines private histories.
