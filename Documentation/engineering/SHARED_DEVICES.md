# Shared devices

Android can host Magic (including Commander variants and Two-Headed Giant),
Yu-Gi-Oh!, Pokémon, Lorcana and Death May Die for devices on the same Wi-Fi,
from the lobby on. Choose **Game → Play together → Host game**. In standard
joining the host shows a join code like `abc-123` on Play together, the Game tab
and the Table tab; a guest chooses **Game → Play together**, types it, and picks
their player (or an Elder One display/investigator for Death May Die). A host whose device setting **Guests
must scan a QR code** is on shows a QR invitation instead, and guests must scan
it. Sharing lasts through the lobby, the match and rematches until the host
stops it, without deleting its game.

## Authority and feature gate

| Concern | Owner |
|---|---|
| Rules, match state and outcomes | Existing host `StandaloneGame` through `StandaloneTable` |
| Network sessions and authorization | `LanSessions`, called on the same main dispatcher as host UI |
| Commands | Owned-seat TCG life/score, counters, Commander damage received, pass/extension, monarch and explicit own win/concede/elimination; DMD markers, skills and turn handoff; existing game validators |
| Freshness/retry validation | Exact match/revision and per-session immutable action UUID receipts |
| Persistence/history/statistics | Host only; commit before network acknowledgement |
| Rendering | Existing table `PlayerPanel`/`PlayerMenu` for TCG personal controls; read-only full-table panels; DMD investigator controls/`ElderOneDisplay` |
| Contract | [LAN v1](../../protocol/lan-v1.md), separate from Atlas HTTP and ESP-NOW |
| Dependencies | Existing Android keystore/TLS, platform NSD (DNS-SD), coroutines, JSON, ZXing/CameraX; no new runtime library |
| Accessibility | Labeled buttons, scrollable cards, safe drawing insets, textual connection/turn state, live announcements and reduced-motion board |

Guests render the host's public match snapshot. They do not create a local match,
change their saved players, persist remote history or receive account credentials.
The host retains all player and game controls, including setup, lifecycle,
Planechase and Bag of Doom resolution. TCG guests use their selected player
panel and counter/Commander menu. **Table · read only** shows every player;
**My player** returns to their controls without leaving, rejoining or releasing
their seat. Table panels have disabled score/turn controls and no editing menus.
Two-Headed Giant guests have personal counters while life and poison remain
shared; either teammate may pass the team turn. Switching views keeps polling
and pending retry state. Guest panels draw each seat's color, avatar,
background and commander art as the host does (owner, 2026-10-20); account
identity and libraries remain private. A guest's win claim waits for the other
players to confirm it in turn order, as a Sigil's does (owner, 2026-10-20).
The host may show only one player's panel (**Show on this device** in the game
menu) when the group has no central display; a claim made there waits the same
way, and the current confirmer's panel joins the view while they answer.

Death May Die investigator devices switch between **My investigator** and **Elder One · read only**
(the Elder One display, minions and monsters included) without leaving or releasing their seat
(owner, 2026-10-20). Death May Die guests only control their claimed investigator, including off-turn corrections;
setup/reference changes stay on the host; guests can confirm their own death. Both devices
therefore see the same markers, chosen skills and card rules. Disconnects disable
guest controls and retain their last view until reconnection. A lost reply is
retried with its original command ID; it never becomes a second delta.

Hosting/joining requires local-network permission on Android 17+. Camera access
is requested only for scanning. The server binds a private Wi-Fi address, and
clients explicitly route over Wi-Fi when mobile data is the default.

Join security is a per-device host setting (owner, 2026-10-20); guests use
whatever the host chose. **Standard** announces the session ID and certificate
pin over DNS-SD (`_turnhub._tcp`), never the code; the guest discovers hosts and
offers the code to each. The code is the only secret: anyone on the Wi-Fi who
learns it can join, and a fake host on the same Wi-Fi could capture a typed code
(accepted for home play). Ten wrong codes within a minute lock joining for that
minute. **QR only** does not announce; its QR invitation pins the host's TLS
certificate and carries a 256-bit key. No cloud service, internet connection or
cleartext LAN allowance is used.

The app keeps the display awake for hosts/guests. Keep the host foreground;
backgrounding it blocks remote edits until it returns. Sessions survive rotation,
lobby changes and rematches, but not process death or Stop sharing. Player
seats follow the player's name: a guest whose player leaves the lobby is sent
back to choose a seat. Guest edits apply only while a match runs or is paused. Changing the hosted
game format ends sharing so existing capabilities cannot carry into another mode.
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

JVM tests cover invitation and join-code validation, code-guess lockout,
lobby/rematch seat continuity, real pinned TLS loopback transport,
bounded framing, role/seat authorization, concurrent/stale edits, retry IDs,
failed durable storage, completion/history deduplication and revoked/idle sessions.
Android build/JVM CI checks compilation and engine regressions. Real phones must
verify Wi-Fi routing/client isolation, keystore TLS support, large text/TalkBack,
rotation, lock/background/reconnect and host process death before acceptance.
