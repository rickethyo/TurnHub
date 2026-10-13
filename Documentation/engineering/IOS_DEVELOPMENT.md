# iOS development framework

**Direction:** a native TurnHub app for iPhone and iPad. Standalone play opens
first, without an account, internet or Atlas. Atlas adds shared-table play,
Sigils, connected profiles/statistics and table administration. Multiple phones
playing one shared game require Atlas. This follows the product direction in
[Android/README.md](../../Android/README.md).

**Current implementation:** a SwiftUI development landing screen, the
`TurnHubCore` local Swift package, Atlas metadata decoding/compatibility checks,
endpoint validation, unit tests and unsigned simulator build CI.
Gameplay, storage and HTTP transport are not implemented. Build instructions
and the source layout belong to [iOS/README.md](../../iOS/README.md).

## Framework choices

| Decision | Starting choice | Reason |
| --- | --- | --- |
| UI | SwiftUI; iPhone and iPad in one app target | Native accessibility and adaptive layouts |
| Baseline | iOS/iPadOS 17; Xcode 16+, Swift 6 | Modern SwiftUI with explicit concurrency checking |
| Presentation | Main-actor observable models, unidirectional updates | Views display state and submit actions |
| Core | Local Swift package, Foundation only | Domain, contracts and storage can be tested without the UI |
| Networking | URLSession behind a transport/repository boundary | Native HTTP, cancellation and test substitution |
| Persistence | Versioned Codable records in Application Support, atomic writes | Small local library without a database dependency initially |
| Secrets | Keychain when authentication is added | Tokens and credentials stay out of preferences/history/logs |
| Dependencies | Apple frameworks and SwiftPM; no external runtime package | Small build and licensing surface |
| Project | Checked-in Xcode project and shared scheme | Open and build directly without a generator |

The OS baseline is a starting engineering choice, not a release support
promise. Confirm it against the intended devices before public distribution.
Do not migrate Android or introduce Kotlin Multiplatform in this foundation.
Revisit shared standalone logic after a working iOS game exposes real
maintenance costs.

## Ownership and boundaries

```text
SwiftUI views -> main-actor presentation model -> TableControls boundary
                                                |-> StandaloneStore -> local game
                                                |-> AtlasRepository -> HTTP -> Atlas

Snapshots/results -> presentation model -> SwiftUI views
```

The diagram describes the target architecture; only the shell and metadata/
endpoint values exist today. Add interfaces when their first implementation
needs them, rather than filling the package with unused abstractions.

- **Standalone:** the iOS domain owns a separate local game, player UUIDs,
  finished-match UUIDs, history and settings. Test actions and clock anchors
  with an injected time source. Persist accepted actions, not rendered timer
  ticks. Recover after process death without silently replacing unreadable data.
- **Connected:** Atlas owns every gameplay decision. The iOS repository sends
  existing semantic controls and replaces its display state with Atlas
  snapshots. Never replay offline controls or turn a lost connection into a
  local copy of the Atlas game.
- **Mode changes:** choose standalone or Atlas explicitly. Preserve the local
  game separately; connecting cannot upload a running match, transfer authority
  or take over navigation after a background discovery result.
- **Identity/import:** local player UUIDs and Atlas-scoped profile identities
  remain separate. Names never prove identity. Import only finished matches
  through the existing verified-link/mapping contract; retain local history
  through rejection and uncertain acknowledgements. Match Android's current
  behavior before adding automatic delivery.
- **UI:** views own layout and transient interactions. Presentation models
  own screen state and task lifetime; core actors/repositories serialize
  mutation, persistence and networking. Timer text is derived from anchors.

Follow [Architectural Invariants](ARCHITECTURAL_INVARIANTS.md),
[Gameplay](GAMEPLAY.md) and [Storage and Recovery](STORAGE_AND_RECOVERY.md).
The standalone exception applies to the native apps' separate local games;
it never grants authority over Atlas state.

## What is shared

| Source of truth | iOS use |
| --- | --- |
| [HTTP contract](../../protocol/http-v1.md), schemas and examples | Swift wire models and contract fixtures; existing form-based controls |
| [Gameplay reference](GAMEPLAY.md) and Android standalone scenarios | Behavior expectations for the local engine, not copied UI code |
| [Design source](../../design/README.md) | Generate Swift roles/resources from the existing tokens when table UI starts |
| [Accessibility](ACCESSIBILITY.md) | Equivalent actions and cues through native accessible controls |
| User manual | Add an offline generated iOS asset before feature-complete testing |

The draft JSON Intent envelope and event stream are not production APIs.
Use the implemented HTTP routes. Do not add an `IOS_APP` wire origin or a
new API merely to identify the client. The foundation uses system typography
and controls; it does not claim design-system parity yet. Preserve font
licenses when Inter/Cinzel and shared artwork are bundled.

## Development sequence and exit criteria

Implement each milestone as a reviewable vertical slice. The first useful
release target is a reliable standalone game; connected parity follows it.

| Stage | Work | Exit evidence |
| --- | --- | --- |
| 0 — Foundation (this slice) | Project, core package, shell, metadata/endpoint boundaries, CI, plan | Package tests and unsigned simulator build pass; iPhone/iPad shell smoke still requires Xcode/device checks |
| 1 — Standalone lobby | Reusable local players, UUID identity, player counts, game profile, starting life and timer setup; generated design roles | Lobby reachable immediately offline; duplicate names stay distinct; validation and accessible iPhone/iPad layouts verified |
| 2 — Complete local game | Life, Commander damage, supported counters/statuses, turns, pause/resume, timer cues, concede/eliminate, win/draw and rematch | Behavior tests match documented Android/Atlas rules; expiry never auto-passes; no network dependency; real screen interaction acceptance |
| 3 — Recovery and history | Atomic local game/library records, exactly one retained local record per match UUID, statistics and local personalization | Relaunch restores play; paused time stays paused; corrupt/future data is preserved; completed-match retry cannot duplicate local history |
| 4 — Read-only Atlas | Explicit connection on the current Wi-Fi, `/info` then `/state`, typed snapshots, visible errors, cancellation and lifecycle recovery | Real Atlas reachable on iPhone and iPad; denied permission and no-internet Wi-Fi handled; incompatible/malformed responses fail clearly |
| 5 — Connected play | Login/join, personal and shared-table views, existing form controls and pending decisions | Mixed Android/iOS/Sigil table works; snapshot confirms changes; disconnect/reconnect does not repeat actions; server permissions preserved |
| 6 — Profile/history parity | Local accounts/preferences, verified Atlas links, explicit/verified delivery, avatars and offline manual | No name-based linking; imports retain records through rejection/uncertain acknowledgement; asset fallback and privacy checks |
| 7 — Maintenance and distribution | Setup, permitted administration, firmware maintenance after S3 update contracts settle, release identity and notices | Physical acceptance, signing/privacy/accessibility review, then owner-directed TestFlight and App Store submission |

Dependencies are deliberate: stage 3 precedes any released local game;
stage 4 can be investigated alongside local development after the shell is
accepted; stage 5 cannot start before its transport and lifecycle behavior
pass. Do not promise full Android feature parity in the initial app release.
The accepted unfinished milestone list is tracked in
[Staged Changes](STAGED_CHANGES.md#ios-app); this document owns the detailed
development flow.

## Working loop

1. Pick one user-visible slice. State its owner, intent/action, validator,
   persistence, rendering clients, contract impact, dependencies and
   accessibility before implementation, as required by the feature gate.
2. Read the owning behavior reference and corresponding Android source/tests.
   Record discrepancies instead of treating incidental Android code as the spec.
3. Implement core behavior with deterministic inputs and only the tests needed
   to protect rules, data integrity, cancellation and authorization boundaries.
   For connection work, use an injected transport; fixtures are test data only.
4. Add the presentation model and native screen. Keep each mode's state owner
   explicit; avoid UI-only replicas of rules or silently queued network actions.
5. Run package tests and the simulator build. Exercise the changed flow on
   iPhone and iPad, including VoiceOver, large text, orientation, Reduce Motion,
   scene background/foreground and process relaunch as applicable.
6. Update the owning reference, milestone status and handoff evidence in the
   same PR. Record what actually ran; CI success is not physical acceptance.

## Apple platform work to resolve early

- **Network privacy:** ask for local-network access only after explicit Atlas
  entry; provide a clear `NSLocalNetworkUsageDescription` and denied-access
  recovery. Add Bonjour declarations only if discovery actually uses Bonjour.
  Do not copy Android's quiet discovery until iOS can do it without a first-run
  permission prompt or navigation change.
- **Wi-Fi:** first connect on the current network/manual Settings join. Then
  evaluate `NEHotspotConfigurationManager` and its entitlement/consent flow on
  physical devices. Atlas has no internet; cellular reachability is not proof
  that Atlas is reachable. Store a passphrase only with explicit need and consent.
- **HTTP/ATS:** Atlas currently serves cleartext HTTP to a numeric LAN address.
  Test the narrow ATS/local-network configuration on a real phone during stage
  4; a hostname exception must not be assumed to cover `192.168.4.1`. Document
  any required allowance and keep authenticated requests endpoint-scoped.
  No broad `NSAllowsArbitraryLoads` switch is added to this shell.
- **Lifecycle:** stop polling/cancel work when a scene leaves the foreground.
  Reconnect with identity/boot/revision checks and a fresh snapshot. No continuous
  background LAN polling promise. Do not deduplicate snapshots by revision
  alone: clock samples and names may change without a revision bump.
- **Feedback:** keep timer/decision cues redundant in text, sound and haptics.
  Define notification behavior from OS-supported delivery mechanisms; no cloud
  push or background connectivity is assumed for a local Atlas.
- **Devices:** start with an iPhone portrait layout and a shared iPad table;
  include landscape, Split View and Dynamic Type before accepting table UI.
- **Release:** developer team, permanent bundle ID, icon, privacy declarations,
  attribution notices and distribution are later release tasks. The foundation
  needs no signing credentials and CI does not publish an app.

## Foundation feature gate

| Question | Answer for this slice |
| --- | --- |
| Canonical state | SwiftUI owns development shell presentation; metadata values describe Atlas; no game state |
| Intent/action | None; no gameplay or network request exposed |
| Validator | Core decoder/compatibility check and endpoint value reject unsupported input |
| Persistence | None; future standalone store and Keychain ownership defined above |
| Rendering clients | SwiftUI landing screen on iPhone/iPad |
| Contract change | None; metadata uses existing schema and fixture |
| Dependencies | Apple SwiftUI/Foundation/XCTest, SwiftPM and Xcode; no third-party runtime library |
| Accessibility | Native text/layout, Dynamic Type and explicit status; device VoiceOver acceptance pending |
