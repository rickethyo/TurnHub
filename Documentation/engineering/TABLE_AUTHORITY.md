# Shared game engine and table authority

Status: design requested by the owner, 2026-10-13. The app must contain the
same game engine and hosting abilities as Atlas, prefer Atlas when connected,
and continue the whole match on the designated host phone when Atlas drops
off. Authority transfer and reconciliation are not implemented yet.

## Current foundation

Death May Die can use one Android standalone host with same-network guest
displays/investigator controls ([Shared devices](SHARED_DEVICES.md)). Those
guests have no engine or takeover rights. This does not implement Atlas handoff
or the portable shared engine described below.

Android's two table entry points share `TableLobby`, `TableLobbyActions`,
`TabletTable` and the live `TableControls` interface. Local and connected
adapters supply the same presentation model and action vocabulary. Authentication,
Sigil management, profile selection and storage remain authority-specific.
Local backgrounds are player-owned presentation, independent of match state.

The rules have one C++ implementation and the app's Kotlin reference. Slice 1
below has started (owner, 2026-10-29: "one game path"): [`core/`](../../core/README.md)
is the portable C++17 port of Android's `StandaloneGame` (*Experimental*: Magic
formats, Two-Headed Giant, Star Commander, Emperor, Bag of Doom, Yu-Gi-Oh!,
Pokémon prizes and Lorcana lore). Since 2026-10-30 (Atlas 0.7.19) Atlas plays on
it: `GameEngine` is a layer over `turnhub::Game` that keeps only Atlas-only
table features (life-change approvals, Commander hit receipts, the win claim's
pause, Sigil elimination attribution). The app does not link it yet (NDK next). Shared JSON scenarios in
`core/scenarios/` run against both the core and the Kotlin rules in CI. Connected play still uses Atlas as its sole
authority. Disconnection queues life and Commander-damage edits; other game
actions wait. Local matches stay separate from connected matches. Sharing UI
does not implement complete rule parity or live authority transfer.

## Target boundaries and feature gate

| Concern | Owner and boundary |
| --- | --- |
| Rules | One portable engine shared by Atlas and native apps |
| Match state | Exactly one authority for each match and authority epoch |
| Connected preference | Atlas takes authority after accepting the current checkpoint and handoff |
| Offline successor | One designated host phone, granted recovery rights before disconnection |
| Actions | Same validated semantic commands through `TableControls` and an authority coordinator |
| Validation | Common rule validators; adapter authentication and host-capability checks |
| Persistence | Current host owns checkpoints and command journal; host phone keeps acknowledged recovery checkpoints |
| Rendering | Shared lobby and table consume the same summary and never choose outcomes |
| Contract | New versioned checkpoint, command acknowledgement, delegation and handoff protocol; current HTTP snapshots are insufficient |
| Dependencies | Existing C++ source plus Android NDK/JNI and future iOS C interface; no proposed third-party runtime library |
| Accessibility | Text host/reconnect status, unchanged controls, no essential animation or timing gesture; conflict review has labeled choices |

Extract the actual C++ rules into a transport-free `shared/game` core, rather
than maintaining two algorithms. Include lobby ordering, modes, counters,
turn/pass timing, life approvals, pending win decisions, eligibility and
completion. Isolate Arduino types, global application state, identity lookup,
hardware, storage and callback side effects behind adapters. Supply explicit
clock values and random input so identical checkpoints and commands produce
identical results on every platform. Existing firmware host tests provide a
starting point, not proof that the current engine can be linked unchanged.

Android wraps this core through JNI; iOS can use a C ABI. Native packaging,
ABI coverage, licensing records and CI builds must be added with that slice.
The Kotlin standalone engine is replaced only after the shared core passes its
existing behavior tests and the connected engine's decision/authorization cases.

## Match identity and recoverable state

A global match UUID survives every transfer. Participant UUIDs identify players
independently of seat number, controller handle, phone or Atlas; verified Atlas
profile links authorize optional statistics attribution. Names never merge
identities. Recovery checkpoints contain:

- Engine/schema version, match UUID, participant identities, complete roster,
  turn order, game settings and mode.
- Life, all counters/status holders, Commander damage, team relationships,
  elimination/concession state, winner sets and per-player timing statistics.
- Lifecycle, logical game time, active/starter player, clock anchors and all
  pending decisions with IDs and remaining deadlines.
- Authority epoch/host ID, latest committed command sequence/hash and completion
  receipt. Credentials and UI artwork are outside the checkpoint.

Each command has a stable UUID, match UUID, epoch, actor/participant UUID and
expected base sequence. The host persists its effect before acknowledgement;
retrying that UUID returns the same outcome. The phone durably stores the
checkpoint/journal and the bounds of recovery rights before enabling takeover.
Unacknowledged actions stay visibly uncertain and retain their UUIDs.

## Authority and connectivity

The shared `AuthorityCoordinator` routes commands to the current host. With no
Atlas, the app runs the common engine and owns all match abilities. When Atlas
connects, the screen remains on the same match while host negotiation occurs.
An empty compatible Atlas can adopt that match, including elapsed clocks and
pending decisions, without resetting it. An Atlas running another match must
offer a match choice; connecting cannot overwrite either game. Incompatible
engine versions leave the current host in charge and explain the issue.

An Atlas-hosted match explicitly designates one recovery phone. Personal-player
phones and a second shared screen cannot promote themselves. Delegation must
fence Atlas as well as grant the phone rights: losing HTTP polling alone never
proves Atlas has stopped accepting Sigil or other-phone actions.

The delegation protocol needs a bounded lease with monotonically increasing
epochs. Atlas rejects game commands and suspends engine deadlines once its
lease expires; Sigils retain their presentation until a host is available.
The recovery phone may promote only after the conservative expiry bound of
every potentially accepted renewal has passed. Lease proposals, acknowledgement
IDs and maximum expiry bounds must be durable on the phone before they can
extend Atlas's lease. Late renewals cannot resurrect an expired epoch.
The implementation must specify and test monotonic-clock drift bounds,
request-age limits and ambiguous/lost renewal replies. If safe expiry cannot
be established, pause pending handoff rather than run two authorities.

After promotion the phone restores its last committed checkpoint, advances
the epoch, and handles all game actions locally. Logical elapsed time and
pending-decision deadlines advance consistently with the common engine;
handoff itself does not count as a turn or restart a timer. Process recreation
restores paused until persisted ownership and clock continuity are established.
An Atlas reboot restores its delegated match suspended and cannot reclaim
authority merely by creating a new boot ID.

While disconnected, Atlas hardware and other phones may be unreachable. Full
phone authority means complete game decisions on that phone, not a promise
that Sigils can communicate with it without a new transport.

## Reconnection and preference for Atlas

1. Detect the returning Atlas and temporarily stop accepting new game commands.
   The UI says the table is reconnecting; both copies remain durable.
2. Authenticate recovery rights and compare match UUID, engine version,
   authority epochs, committed command IDs and checkpoint ancestry.
3. For a single linear history, transfer the phone's offline suffix/full
   checkpoint to Atlas. Deduplicate uncertain commands by UUID. Preserve clocks,
   player identities and pending decisions; do not replay completed turns.
4. Atlas validates and durably accepts the checkpoint under a newer epoch,
   returning its sequence/hash and a fresh delegation grant. The phone stores
   that acceptance before demoting its local engine and resuming remote routing.
5. If the acknowledgement is lost, query/retry the same transfer ID. Neither
   host resumes on an ambiguous result. Atlas becomes preferred authority once
   the accepted handoff is established, not by discarding the offline game.

Atlas can have accepted commands after the phone's last received checkpoint
but before its lease expired. Those commands must be retained for reconciliation.
If histories diverge, automatically accept only a validated single lineage
or commands the common engine proves can be replayed without altering either
accepted outcome. Conflicting turns, decisions, eliminations or results pause
for explicit table review showing both branches. Never choose by wall-clock
timestamp, add life deltas blindly, or silently overwrite the phone's progress.
Independent matches never reconcile just because they share players.

## Completion and statistics

Persist a completion receipt keyed by match UUID/completion ID before crediting
statistics. Hosts must share the same final result hash and reconcile receipts
before issuing further statistics writes. Completion handlers cannot fire again
on restore, replay or authority transfer. Pending/rejected deliveries keep local
history. Atlas's current 64-receipt import window and checkpoint/statistics ordering
do not provide this guarantee; durable receipts and replay-safe statistics must
land before automatic cross-host completion is enabled.

## Implementation slices and acceptance

1. Shared engine extraction, explicit clocks/randomness, portable checkpoints
   and command/result fixtures. Run the same scenarios against Atlas and Android;
   include every mode, pending decision, invalid actor and all timer boundaries.
2. Replace Android's local rules with the common engine; keep the unified UI
   and current connected authority behavior. Verify native ABIs and Android
   rotation/process recreation/storage failure.
3. Versioned handoff/delegation endpoints and persisted coordinator. Verify
   local-to-empty-Atlas adoption, unrelated match choice and version mismatch.
4. Fenced takeover and reconnect. Fault-inject dropped/duplicated/reordered
   commands, delayed renewals, lost handoff acknowledgements, Atlas reboot,
   phone death, reconnect flapping, two potential hosts and concurrent Sigil
   actions. Assert at most one accepting host per epoch.
5. Durable completion receipts and reconciliation review. Verify finishing
   offline, retrying after power loss and reclaiming authority never credit a
   match twice. Record real phone/Atlas/Sigil and TalkBack/text-scale acceptance.

The authority invariant and client/persistence contracts change together with
the handoff implementation. Until then, the current sole-Atlas rule remains
the connected runtime contract.
