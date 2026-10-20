# Shared devices

Android can host a Death May Die match for other TurnHub devices on the same
Wi-Fi. Start the match, choose **Game → Play together → Host game**, and share
the QR invitation. A guest chooses **Game → Play together**, scans/pastes the
invitation and selects the Elder One display or an investigator. The host can
disconnect guests or stop sharing without deleting its game.

## Authority and feature gate

| Concern | Owner |
|---|---|
| Rules, match state and outcomes | Existing host `StandaloneGame` through `StandaloneTable` |
| Network sessions and authorization | `LanSessions`, called on the same main dispatcher as host UI |
| Commands | Owned-seat wounds/stress/fire/insanity, skill level, next-turn staging, explicit pass recipient; existing game validators |
| Freshness/retry validation | Exact match/revision and per-session immutable action UUID receipts |
| Persistence/history/statistics | Host only; commit before network acknowledgement |
| Rendering | `SharedGameScreen`; read-only `ElderOneDisplay` or personal investigator controls |
| Contract | [LAN v1](../../protocol/lan-v1.md), separate from Atlas HTTP and ESP-NOW |
| Dependencies | Existing Android keystore/TLS, coroutines, JSON, ZXing/CameraX; no new runtime library |
| Accessibility | Labeled buttons, scrollable cards, safe drawing insets, textual connection/turn state, live announcements and reduced-motion board |

Guests render the host's public match snapshot. They do not create a local match,
change their saved players, persist remote history or receive account credentials.
The host still controls every investigator and the Elder One/episode board.
Guests only control their claimed investigator, including off-turn corrections;
setup/reference changes stay on the host; guests can confirm their own death. Both devices
therefore see the same markers, chosen skills and card rules. Disconnects disable
guest controls and retain their last view until reconnection. A lost reply is
retried with its original command ID; it never becomes a second delta.

Hosting/joining requires local-network permission on Android 17+. Camera access
is requested only for scanning; pasted invitations work without it. The server
binds a private Wi-Fi address, and clients explicitly route over Wi-Fi when mobile
data is the default. QR invitations pin the host's TLS certificate and grant a
session-scoped capability. Keep invitations within the table. No cloud service,
internet connection, network discovery or cleartext LAN allowance is used.

The app keeps the display awake for hosts/guests. Keep the host foreground;
backgrounding it blocks remote edits until it returns. Sessions survive rotation,
but not process death. A new match or hosting session needs a fresh invitation.
There is no election, Atlas adoption, live host transfer or automatic promotion.
[Shared table authority](TABLE_AUTHORITY.md) owns that separate design.

## Hosting as a paid capability

Hosting is isolated behind `LanHostingAccess`; joining does not consult it.
The current implementation enables hosting without a purchase. Store billing,
product IDs, price, purchase verification/restoration and distribution policy
remain unimplemented. The proposed product model is one paid host with free
guests, keeping solo games and tracking accessible. Do not show a purchase flow
or describe this as a paid product until its entitlement implementation exists.

## Verification boundary

JVM tests cover invitation validation, real pinned TLS loopback transport,
bounded framing, role/seat authorization, concurrent/stale edits, retry IDs,
failed durable storage, completion/history deduplication and revoked/idle sessions.
Android build/JVM CI checks compilation and engine regressions. Real phones must
verify Wi-Fi routing/client isolation, keystore TLS support, large text/TalkBack,
rotation, lock/background/reconnect and host process death before acceptance.
