// TurnHub hardware-in-the-loop test harness.
//
// One ESP32 acts as two menu Sigils against a real Atlas: its Wi-Fi station
// MAC and its soft-AP MAC each pair as a separate Sigil, and each can seat two
// players (Seat A and B), so one board plays up to four players. It speaks
// only the shared ESP-NOW contract (protocol.h) and drives the game through
// the same SelectAction choices a real menu Sigil sends. Like a 0.8.0 Sigil it
// also joins through the profile picker, changes its players' life with
// LifeAdjust and answers life requests with LifeResponse. Atlas stays the
// sole authority: the harness reads the menus Atlas offers and never decides
// an outcome. See README.md for the serial commands.

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "protocol.h"

namespace {

using TurnHubProtocol::GameDisplayPacket;
using TurnHubProtocol::LifeRequestFields;
using TurnHubProtocol::MenuStateFields;
using TurnHubProtocol::Packet;
using TurnHubProtocol::PickerKeyCode;
using TurnHubProtocol::PickerMode;
using TurnHubProtocol::ProfilePickerPacket;
using TurnHubProtocol::PacketType;
using TurnHubProtocol::SigilAction;
using TurnHubProtocol::HarnessRunState;
using TurnHubProtocol::HarnessStep;
using TurnHubProtocol::HarnessTest;

constexpr uint32_t SERIAL_BAUD = 115200;
// Must match Atlas's soft-AP channel (and Sigil's WIFI_CHANNEL).
constexpr uint8_t WIFI_CHANNEL = 6;
// 0.8.0 is the Sigil release that decodes MenuState2 (Leave, AdjustLife),
// the profile picker and life requests; Atlas gates those on it.
constexpr uint8_t FIRMWARE_MAJOR = 0;
constexpr uint8_t FIRMWARE_MINOR = 8;
constexpr uint8_t FIRMWARE_PATCH = 0;
// Menu Sigil with a shared (two-seat) display. The game display carries each
// seat's life, which the life checks read back; LedState keeps Atlas's light
// traffic to one packet per change instead of three channel packets.
constexpr uint8_t CAPABILITIES = TurnHubProtocol::CAPABILITY_MENU |
    TurnHubProtocol::CAPABILITY_GAME_DISPLAY | TurnHubProtocol::CAPABILITY_LED_STATE;
constexpr uint32_t HELLO_INTERVAL_MS = 2000;
constexpr uint32_t PAIR_REQUEST_INTERVAL_MS = 1000;
constexpr uint32_t PAIR_ATTEMPT_MS = 30000;
constexpr uint32_t SEND_TIMEOUT_MS = 60;
constexpr uint32_t ACK_TIMEOUT_MS = 600;
constexpr uint8_t UNASSIGNED = 0xFF;
constexpr uint8_t VIRTUAL_SIGILS = 2;
constexpr uint8_t RX_QUEUE_LENGTH = 24;

constexpr uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
constexpr char PREF_NAMESPACE[] = "th_harness";
constexpr char PREF_KEY[] = "pair";  // Atlas MAC + one Sigil ID per virtual Sigil.

// Atlas's timings (Atlas/include/atlas_app.h), with margin for radio latency.
constexpr uint32_t START_WAIT_MS = 3000 + 4000;
constexpr uint32_t PASS_WAIT_MS = 3000 + 4000;
constexpr uint32_t STEP_WAIT_MS = 5000;
// A LifeAdjust is sent once, like a real Sigil's batch after
// LIFE_ADJUST_COMMIT_MS; the game display should show it well within this.
constexpr uint32_t LIFE_WAIT_MS = 3000;
constexpr int32_t LIFE_TEST_DELTA = 3;

struct VirtualSigil {
  VirtualSigil(const char *n, wifi_interface_t i) : name(n), iface(i) {}
  const char *name;
  wifi_interface_t iface;
  uint8_t mac[6] = {};
  uint8_t sigilId = UNASSIGNED;
  bool pairing = false;
  int32_t pairingToken = 0;
  uint32_t lastPairRequestMs = 0;
  uint32_t lastHelloMs = 0;
  uint32_t lastHelloAckMs = 0;
  bool menuValid = false;
  MenuStateFields menu;
  uint32_t menuAtMs = 0;
  uint8_t lastAckType = 0;
  uint32_t lastAckMs = 0;
  uint32_t ackCount = 0;
  // What Atlas shows this Sigil: its seats (DisplayState), their life while a
  // game runs (GameDisplay), the profile picker and a pending life request.
  int32_t displayState = 0;
  bool gameDisplayValid = false;
  GameDisplayPacket gameDisplay{};
  bool pickerValid = false;
  ProfilePickerPacket picker{};
  LifeRequestFields lifeRequest;
  int32_t startingLife = 0;
  uint8_t passingPlayer = 0;
};

VirtualSigil sigils[VIRTUAL_SIGILS] = {{"V1", WIFI_IF_STA}, {"V2", WIFI_IF_AP}};
uint8_t activeSigils = VIRTUAL_SIGILS;
bool atlasKnown = false;
uint8_t atlasMac[6] = {};
bool verbose = false;
// Pause before each menu choice so a person can follow the run on Atlas and
// the portal (owner request, 2026-09-25). "pace <ms>" changes it; kept in NVS.
constexpr uint32_t DEFAULT_PACE_MS = 1500;
constexpr uint32_t MAX_PACE_MS = 10000;
uint32_t paceMs = DEFAULT_PACE_MS;
bool radioReady = false;
// How the harness answers a life request shown to one of its players (a
// portal user asking to change a harness player's life). Like confirming a
// win, it is only an answer; Atlas decides. "lifereq <mode>" changes it.
enum class LifeRequestPolicy : uint8_t { Approve, Deny, Ignore };
LifeRequestPolicy lifeRequestPolicy = LifeRequestPolicy::Approve;

struct RxFrame {
  uint8_t mac[6];
  uint8_t length;
  uint8_t data[sizeof(GameDisplayPacket)];
};
QueueHandle_t rxQueue = nullptr;
volatile bool sendDone = false;
volatile bool sendOk = false;

String inputLine;
bool abortRequested = false;
uint16_t passed = 0;
uint16_t failed = 0;
// The run Atlas sees (HarnessReport), and a test Atlas asked for from its
// touchscreen, started by loop() so it never runs inside the radio pump.
TurnHubProtocol::HarnessReportFields report;
bool running = false;
int8_t pendingTest = -1;

// --- Names ---------------------------------------------------------------------

const char *actionName(uint8_t action) {
  static const char *const NAMES[] = {"join", "cycle-starter", "random-starter", "add-seat-b",
      "remove-seat-b", "start", "cancel-start", "pass", "cancel-pass", "pause", "resume",
      "claim-win", "confirm-win", "deny-win", "begin-elimination", "next-target", "eliminate",
      "cancel-elimination", "rematch", "reset-table", "link-phone", "leave", "adjust-life"};
  static_assert(sizeof(NAMES) / sizeof(NAMES[0]) == static_cast<size_t>(SigilAction::Count),
      "One name per SigilAction");
  return action < static_cast<uint8_t>(SigilAction::Count) ? NAMES[action] : "none";
}

bool parseAction(const String &text, SigilAction &out) {
  for (uint8_t i = 0; i < static_cast<uint8_t>(SigilAction::Count); ++i) {
    if (text == actionName(i)) {
      out = static_cast<SigilAction>(i);
      return true;
    }
  }
  return false;
}

const char *policyName(LifeRequestPolicy policy) {
  switch (policy) {
    case LifeRequestPolicy::Approve: return "approve";
    case LifeRequestPolicy::Deny: return "deny";
    default: return "ignore";
  }
}

String macText(const uint8_t *mac) {
  char text[18];
  snprintf(text, sizeof(text), "%02X:%02X:%02X:%02X:%02X:%02X",
      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(text);
}

String menuText(const VirtualSigil &v) {
  if (!v.menuValid) return "none";
  String text;
  for (uint8_t i = 0; i < static_cast<uint8_t>(SigilAction::Count); ++i) {
    if ((v.menu.actions & (1u << i)) == 0) continue;
    if (text.length() > 0) text += ",";
    text += actionName(i);
  }
  return text.length() > 0 ? text : String("empty");
}

// The seats this Sigil shows (primary first) and, while a game runs, life.
uint8_t primaryPlayer(const VirtualSigil &v) {
  return TurnHubProtocol::displayPrimaryPlayer(v.displayState);
}

bool lifeOf(const VirtualSigil &v, uint8_t player, int32_t &life) {
  if (!v.gameDisplayValid || player == 0) return false;
  if (TurnHubProtocol::displayPrimaryPlayer(v.gameDisplay.state) == player) {
    life = v.gameDisplay.primary.life;
    return true;
  }
  if (TurnHubProtocol::displaySecondaryPlayer(v.gameDisplay.state) == player) {
    life = v.gameDisplay.secondary.life;
    return true;
  }
  return false;
}

String seatsText(const VirtualSigil &v) {
  const uint8_t a = TurnHubProtocol::displayPrimaryPlayer(v.displayState);
  const uint8_t b = TurnHubProtocol::displaySecondaryPlayer(v.displayState);
  if (a == 0) return "none";
  String text = "P" + String(a);
  int32_t life = 0;
  if (lifeOf(v, a, life)) text += "(" + String(life) + ")";
  if (b != 0) {
    text += ",P" + String(b);
    if (lifeOf(v, b, life)) text += "(" + String(life) + ")";
  }
  return text;
}

String pickerText(const VirtualSigil &v) {
  if (!v.pickerValid || v.picker.mode == PickerMode::Closed) return "closed";
  String text = v.picker.mode == PickerMode::Confirm ? "confirm:" : "page " +
      String(v.picker.page + 1) + "/" + String(v.picker.pageCount) + ":";
  for (uint8_t i = 0; i < v.picker.itemCount; ++i) {
    if (i > 0) text += ",";
    text += v.picker.items[i].name;
  }
  return text;
}

// --- Persistence ---------------------------------------------------------------

void savePairing() {
  Preferences prefs;
  if (!prefs.begin(PREF_NAMESPACE, false)) return;
  uint8_t blob[6 + VIRTUAL_SIGILS];
  memcpy(blob, atlasMac, 6);
  for (uint8_t i = 0; i < VIRTUAL_SIGILS; ++i) blob[6 + i] = sigils[i].sigilId;
  if (atlasKnown) {
    prefs.putBytes(PREF_KEY, blob, sizeof(blob));
  } else {
    prefs.remove(PREF_KEY);
  }
  prefs.end();
}

void loadPairing() {
  Preferences prefs;
  if (!prefs.begin(PREF_NAMESPACE, true)) return;
  uint8_t blob[6 + VIRTUAL_SIGILS];
  if (prefs.getBytesLength(PREF_KEY) == sizeof(blob) &&
      prefs.getBytes(PREF_KEY, blob, sizeof(blob)) == sizeof(blob)) {
    memcpy(atlasMac, blob, 6);
    atlasKnown = true;
    for (uint8_t i = 0; i < VIRTUAL_SIGILS; ++i) {
      sigils[i].sigilId = blob[6 + i] < TurnHubProtocol::MAX_SIGILS ? blob[6 + i] : UNASSIGNED;
    }
  }
  paceMs = min(prefs.getUInt("pace", DEFAULT_PACE_MS), MAX_PACE_MS);
  prefs.end();
}

// --- Radio ---------------------------------------------------------------------

// ESP-NOW keys peers by MAC alone, so the one Atlas (or broadcast) peer is
// re-pointed at the sending virtual Sigil's interface before each send, and
// sends are serialized on the send callback.
bool sendFrom(const VirtualSigil &v, const uint8_t *dest, const void *data, size_t length) {
  if (!radioReady) return false;
  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, dest, 6);
  peer.channel = 0;  // The current channel.
  peer.ifidx = v.iface;
  peer.encrypt = false;
  const esp_err_t peerResult =
      esp_now_is_peer_exist(dest) ? esp_now_mod_peer(&peer) : esp_now_add_peer(&peer);
  if (peerResult != ESP_OK) {
    Serial.printf("HARNESS|RADIO|PEER_ERROR|%s|%d\n", v.name, static_cast<int>(peerResult));
    return false;
  }
  sendDone = false;
  if (esp_now_send(dest, static_cast<const uint8_t *>(data), length) != ESP_OK) return false;
  const uint32_t start = millis();
  while (!sendDone && millis() - start < SEND_TIMEOUT_MS) delay(1);
  return sendDone && sendOk;
}

bool sendPacket(VirtualSigil &v, PacketType type, int32_t value, bool broadcast = false) {
  if (!broadcast && !atlasKnown) return false;
  const Packet packet = TurnHubProtocol::makePacket(type, v.sigilId, value);
  const bool ok = sendFrom(v, broadcast ? BROADCAST_MAC : atlasMac, &packet, sizeof(packet));
  if (verbose) {
    Serial.printf("HARNESS|TX|%s|type=%u|value=%ld|%s\n", v.name, static_cast<unsigned>(type),
        static_cast<long>(value), ok ? "OK" : "FAIL");
  }
  return ok;
}

void sendHello(VirtualSigil &v) {
  v.lastHelloMs = millis();
  if (!atlasKnown || v.sigilId == UNASSIGNED) return;
  sendPacket(v, PacketType::Hello,
      TurnHubProtocol::encodeHelloInfo(FIRMWARE_MAJOR, FIRMWARE_MINOR, FIRMWARE_PATCH,
          &v == &sigils[0] ? CAPABILITIES | TurnHubProtocol::CAPABILITY_HARNESS : CAPABILITIES));
}

VirtualSigil *sigilById(uint8_t sigilId) {
  for (uint8_t i = 0; i < activeSigils; ++i) {
    if (sigils[i].sigilId == sigilId) return &sigils[i];
  }
  return nullptr;
}

void handlePacket(const uint8_t *mac, const Packet &packet) {
  if (packet.version != TurnHubProtocol::VERSION) return;

  // Pairing replies are matched by the token each virtual Sigil sent.
  if (packet.type == PacketType::PairAccept) {
    for (uint8_t i = 0; i < activeSigils; ++i) {
      VirtualSigil &v = sigils[i];
      if (!v.pairing || packet.value != v.pairingToken ||
          packet.sigilId >= TurnHubProtocol::MAX_SIGILS) {
        continue;
      }
      if (atlasKnown && memcmp(mac, atlasMac, 6) != 0) {
        // A different Atlas: the harness follows one Atlas at a time.
        for (auto &other : sigils) other.sigilId = UNASSIGNED;
      }
      memcpy(atlasMac, mac, 6);
      atlasKnown = true;
      v.sigilId = packet.sigilId;
      v.pairing = false;
      v.menuValid = false;
      savePairing();
      Serial.printf("HARNESS|PAIR|ACCEPTED|%s|sigil=%u|atlas=%s\n", v.name,
          static_cast<unsigned>(v.sigilId), macText(mac).c_str());
      sendHello(v);
    }
    return;
  }

  if (!atlasKnown || memcmp(mac, atlasMac, 6) != 0) return;
  VirtualSigil *v = sigilById(packet.sigilId);
  if (v == nullptr) return;

  switch (packet.type) {
    case PacketType::Ack:
      if (packet.value == static_cast<int32_t>(PacketType::Hello)) {
        v->lastHelloAckMs = millis();
      } else {
        v->lastAckType = static_cast<uint8_t>(packet.value);
        v->lastAckMs = millis();
        ++v->ackCount;
      }
      break;
    case PacketType::MenuState:
    case PacketType::MenuState2:
      v->menu = packet.type == PacketType::MenuState2
          ? TurnHubProtocol::decodeMenuState2(packet.value)
          : TurnHubProtocol::decodeMenuState(packet.value);
      v->menuValid = true;
      v->menuAtMs = millis();
      if (verbose) {
        Serial.printf("HARNESS|MENU|%s|rev=%u|%s\n", v->name,
            static_cast<unsigned>(v->menu.revision), menuText(*v).c_str());
      }
      break;
    case PacketType::HarnessCommand: {
      // Only V1 carries CAPABILITY_HARNESS, so only it takes commands.
      if (v != &sigils[0]) break;
      const uint8_t kind = TurnHubProtocol::harnessCommandKind(packet.value);
      const uint8_t test = TurnHubProtocol::harnessCommandTest(packet.value);
      if (kind == static_cast<uint8_t>(TurnHubProtocol::HarnessCommandKind::Stop)) {
        if (running) abortRequested = true;
        Serial.println("HARNESS|ATLAS|STOP");
      } else if (running || pendingTest >= 0) {
        Serial.println("HARNESS|ATLAS|RUN|BUSY");
      } else if (test < static_cast<uint8_t>(HarnessTest::Count)) {
        pendingTest = static_cast<int8_t>(test);
        Serial.printf("HARNESS|ATLAS|RUN|%s\n", TurnHubProtocol::harnessTestName(test));
      }
      break;
    }
    case PacketType::FactoryReset:
      // The harness plays two Sigils on one board, so a factory reset clears
      // only that virtual Sigil's pairing (no NVS wipe, no restart).
      if (packet.value != TurnHubProtocol::FACTORY_RESET_CONFIRM) break;
      Serial.printf("HARNESS|FACTORY_RESET|%s\n", v->name);
      v->sigilId = UNASSIGNED;
      v->menuValid = false;
      savePairing();
      break;
    case PacketType::DisplayState:
      // A non-running state ends the game display (as on a real Sigil).
      v->displayState = packet.value;
      v->gameDisplayValid = false;
      break;
    case PacketType::LifeRequest: {
      const LifeRequestFields request = TurnHubProtocol::decodeLifeRequest(packet.value);
      const bool fresh = request.target != 0 &&
          (request.target != v->lifeRequest.target || request.tag != v->lifeRequest.tag);
      v->lifeRequest = request;
      if (!fresh) break;
      Serial.printf("HARNESS|LIFE|REQUEST|%s|target=P%u|from=P%u|delta=%ld|%s\n", v->name,
          static_cast<unsigned>(request.target), static_cast<unsigned>(request.requester),
          static_cast<long>(request.delta), policyName(lifeRequestPolicy));
      if (lifeRequestPolicy != LifeRequestPolicy::Ignore) {
        sendPacket(*v, PacketType::LifeResponse, TurnHubProtocol::encodeLifeResponse(request.target,
            lifeRequestPolicy == LifeRequestPolicy::Approve, request.tag));
      }
      break;
    }
    case PacketType::StartingLife:
      v->startingLife = packet.value;
      break;
    case PacketType::PassPending:
      v->passingPlayer = static_cast<uint8_t>(packet.value);
      break;
    case PacketType::SeatColor:
    case PacketType::LedState:
    case PacketType::InputTiming:
    case PacketType::DisplayNameChunk:
      break;  // Presentation only; the harness has no light or screen.
    case PacketType::Unpair:
      Serial.printf("HARNESS|PAIR|FORGOTTEN_BY_ATLAS|%s\n", v->name);
      v->sigilId = UNASSIGNED;
      v->menuValid = false;
      savePairing();
      break;
    default:
      if (verbose) {
        Serial.printf("HARNESS|RX|%s|type=%u|value=%ld\n", v->name,
            static_cast<unsigned>(packet.type), static_cast<long>(packet.value));
      }
      break;
  }
}

bool fromAtlas(const uint8_t *mac) {
  return atlasKnown && memcmp(mac, atlasMac, 6) == 0;
}

void handleGameDisplay(const uint8_t *mac, const uint8_t *data) {
  GameDisplayPacket snapshot;
  memcpy(&snapshot, data, sizeof(snapshot));
  if (!fromAtlas(mac) || !TurnHubProtocol::validGameDisplay(snapshot)) return;
  VirtualSigil *v = sigilById(snapshot.sigilId);
  if (v == nullptr) return;
  v->gameDisplay = snapshot;
  v->gameDisplayValid = true;
  v->displayState = snapshot.state;
}

void handlePicker(const uint8_t *mac, const uint8_t *data) {
  ProfilePickerPacket page;
  memcpy(&page, data, sizeof(page));
  if (!fromAtlas(mac) || !TurnHubProtocol::validProfilePicker(page)) return;
  VirtualSigil *v = sigilById(page.sigilId);
  if (v == nullptr) return;
  v->picker = page;
  v->pickerValid = true;
  if (verbose) Serial.printf("HARNESS|PICKER|%s|%s\n", v->name, pickerText(*v).c_str());
}

void onReceive(const uint8_t *mac, const uint8_t *data, int length) {
  if (rxQueue == nullptr || length <= 0 || length > static_cast<int>(sizeof(RxFrame::data))) return;
  RxFrame frame;
  memcpy(frame.mac, mac, 6);
  frame.length = static_cast<uint8_t>(length);
  memcpy(frame.data, data, length);
  xQueueSend(rxQueue, &frame, 0);
}

void onSent(const uint8_t *, esp_now_send_status_t status) {
  sendOk = status == ESP_NOW_SEND_SUCCESS;
  sendDone = true;
}

bool startRadio() {
  // AP+STA gives two interfaces, and so two MACs Atlas can pair. The soft-AP
  // is hidden and password-protected; it only exists to own the second MAC
  // and to hold the radio on Atlas's channel.
  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect();
  if (!WiFi.softAP("TurnHub-Harness", "harness-not-for-joining", WIFI_CHANNEL, 1, 1)) {
    Serial.println("HARNESS|RADIO|SOFTAP_ERROR");
    return false;
  }
  esp_wifi_get_mac(WIFI_IF_STA, sigils[0].mac);
  esp_wifi_get_mac(WIFI_IF_AP, sigils[1].mac);
  if (esp_now_init() != ESP_OK) {
    Serial.println("HARNESS|RADIO|ESP_NOW_ERROR");
    return false;
  }
  rxQueue = xQueueCreate(RX_QUEUE_LENGTH, sizeof(RxFrame));
  esp_now_register_recv_cb(onReceive);
  esp_now_register_send_cb(onSent);
  radioReady = rxQueue != nullptr;
  return radioReady;
}

// --- Service loop --------------------------------------------------------------

void pollSerialForAbort();
void sendReport();

// Keeps every virtual Sigil alive: received packets, Hello, pairing requests.
void pump() {
  RxFrame frame;
  while (rxQueue && xQueueReceive(rxQueue, &frame, 0) == pdTRUE) {
    if (frame.length == sizeof(Packet)) {
      Packet packet;
      memcpy(&packet, frame.data, sizeof(packet));
      handlePacket(frame.mac, packet);
    } else if (frame.length == sizeof(GameDisplayPacket)) {
      handleGameDisplay(frame.mac, frame.data);
    } else if (frame.length == sizeof(ProfilePickerPacket)) {
      handlePicker(frame.mac, frame.data);
    }
  }
  const uint32_t now = millis();
  for (uint8_t i = 0; i < activeSigils; ++i) {
    VirtualSigil &v = sigils[i];
    if (v.pairing) {
      if (now - v.lastPairRequestMs >= PAIR_REQUEST_INTERVAL_MS) {
        v.lastPairRequestMs = now;
        sendPacket(v, PacketType::PairRequest, v.pairingToken, true);
      }
    } else if (now - v.lastHelloMs >= HELLO_INTERVAL_MS) {
      sendHello(v);
      if (i == 0) sendReport();  // Atlas keeps showing the last result.
    }
  }
}

template <typename Predicate>
bool waitUntil(Predicate done, uint32_t timeoutMs) {
  const uint32_t start = millis();
  while (!done()) {
    if (abortRequested || millis() - start >= timeoutMs) return false;
    pump();
    pollSerialForAbort();
    delay(2);
  }
  return true;
}

void idle(uint32_t ms) {
  waitUntil([] { return false; }, ms);
}

bool online(const VirtualSigil &v) {
  return v.sigilId != UNASSIGNED && v.lastHelloAckMs != 0 &&
      millis() - v.lastHelloAckMs < 3 * HELLO_INTERVAL_MS;
}

bool has(const VirtualSigil &v, SigilAction action) {
  return v.menuValid && (v.menu.actions & TurnHubProtocol::sigilActionBit(action)) != 0;
}

VirtualSigil *offering(SigilAction action) {
  for (uint8_t i = 0; i < activeSigils; ++i) {
    if (has(sigils[i], action)) return &sigils[i];
  }
  return nullptr;
}

// Sends a menu choice from the menu Atlas last offered, and waits for Atlas's
// transport Ack (the Intent's outcome shows in the next menu).
bool choose(VirtualSigil &v, SigilAction action) {
  // The run waits here, then checks the choice is still on offer.
  idle(paceMs);
  if (!has(v, action)) return false;
  const uint32_t acksBefore = v.ackCount;
  Serial.printf("HARNESS|SELECT|%s|%s|rev=%u\n", v.name, actionName(static_cast<uint8_t>(action)),
      static_cast<unsigned>(v.menu.revision));
  if (!sendPacket(v, PacketType::SelectAction,
          TurnHubProtocol::encodeSelectAction(action, v.menu.revision))) {
    return false;
  }
  return waitUntil([&v, acksBefore] {
    return v.ackCount != acksBefore &&
        v.lastAckType == static_cast<uint8_t>(PacketType::SelectAction);
  }, ACK_TIMEOUT_MS);
}

bool pickerOpen(const VirtualSigil &v) {
  return v.pickerValid && v.picker.mode != PickerMode::Closed;
}

// A picker key names the page it was pressed on, like a SelectAction's menu
// revision. Up, Right and Down pick the page's three names in order.
bool pressPickerKey(VirtualSigil &v, PickerKeyCode key) {
  idle(paceMs);
  if (!pickerOpen(v)) return false;
  static const char *const KEYS[] = {"up", "down", "left", "right", "select"};
  Serial.printf("HARNESS|PICKER_KEY|%s|%s|rev=%u\n", v.name, KEYS[static_cast<uint8_t>(key)],
      static_cast<unsigned>(v.picker.revision));
  return sendPacket(v, PacketType::PickerKey, TurnHubProtocol::encodePickerKey(key, v.picker.revision));
}

PickerKeyCode keyForItem(uint8_t item) {
  static const PickerKeyCode KEYS[] = {PickerKeyCode::Up, PickerKeyCode::Right, PickerKeyCode::Down};
  return KEYS[item < 3 ? item : 0];
}

bool joined(const VirtualSigil &v) {
  return has(v, SigilAction::CycleStarter);
}

// Life changes go out as one LifeAdjust (a real Sigil batches its presses the
// same way), for one of this Sigil's own players.
bool sendLifeAdjust(VirtualSigil &v, uint8_t player, int32_t delta) {
  idle(paceMs);
  if (!has(v, SigilAction::AdjustLife) || player == 0 || delta == 0) return false;
  Serial.printf("HARNESS|LIFE|ADJUST|%s|P%u|%+ld\n", v.name, static_cast<unsigned>(player),
      static_cast<long>(delta));
  return sendPacket(v, PacketType::LifeAdjust, TurnHubProtocol::encodeLifeAdjust(player, delta));
}

// --- Reporting -----------------------------------------------------------------

// Tells Atlas how the run is going (V1 speaks for the harness).
void sendReport() {
  report.passed = static_cast<uint8_t>(min<uint16_t>(passed, 255));
  report.failed = static_cast<uint8_t>(min<uint16_t>(failed, 255));
  if (sigils[0].sigilId != UNASSIGNED) {
    sendPacket(sigils[0], PacketType::HarnessReport, TurnHubProtocol::encodeHarnessReport(report));
  }
}

bool step(bool ok, HarnessStep checkpoint, const String &detail = String()) {
  if (ok) {
    ++passed;
  } else {
    ++failed;
  }
  report.step = static_cast<uint8_t>(checkpoint);
  Serial.printf("HARNESS|%s|%s", ok ? "PASS" : "FAIL",
      TurnHubProtocol::harnessStepName(static_cast<uint8_t>(checkpoint)));
  if (detail.length() > 0) Serial.printf("|%s", detail.c_str());
  Serial.println();
  sendReport();
  return ok;
}

void summary(const char *scenario) {
  Serial.printf("HARNESS|SUMMARY|%s|passed=%u|failed=%u\n", scenario,
      static_cast<unsigned>(passed), static_cast<unsigned>(failed));
}

// For log lines: Atlas updates Sigils one after another, so let the other
// Sigil's menu arrive before printing both.
String allMenus() {
  idle(200);
  String text;
  for (uint8_t i = 0; i < activeSigils; ++i) {
    if (i > 0) text += "|";
    text += String(sigils[i].name) + "=" + menuText(sigils[i]);
  }
  return text;
}

// --- Scenarios -----------------------------------------------------------------

// Every virtual Sigil is paired, answered Hello and received a menu.
bool scenarioSmoke() {
  bool ok = true;
  for (uint8_t i = 0; i < activeSigils; ++i) {
    VirtualSigil &v = sigils[i];
    if (!step(v.sigilId != UNASSIGNED, HarnessStep::Paired, String(v.name) +
            (v.sigilId == UNASSIGNED ? "|reason=run 'pair'" : "|sigil=" + String(v.sigilId)))) {
      ok = false;
      continue;
    }
    ok &= step(waitUntil([&v] { return online(v); }, STEP_WAIT_MS), HarnessStep::HelloAck, v.name);
    ok &= step(waitUntil([&v] { return v.menuValid; }, STEP_WAIT_MS), HarnessStep::Menu,
        String(v.name) + "|" + menuText(v));
  }
  return ok;
}

bool gameOver() {
  return offering(SigilAction::Rematch) != nullptr;
}

// Join: a picker Sigil (0.8.0+, not V1, which carries CAPABILITY_HARNESS)
// first gets Atlas's profile picker; the harness picks Guest from it, so the
// run never plays as, or changes, a real profile.
bool join(VirtualSigil &v) {
  const bool asked = choose(v, SigilAction::Join) &&
      waitUntil([&v] { return joined(v) || pickerOpen(v); }, STEP_WAIT_MS);
  if (asked && pickerOpen(v)) {
    int8_t guest = -1;
    if (v.picker.mode == PickerMode::List) {
      for (uint8_t i = 0; i < v.picker.itemCount; ++i) {
        if (v.picker.items[i].flags & TurnHubProtocol::PICKER_ITEM_GUEST) guest = static_cast<int8_t>(i);
      }
    }
    const bool picked = guest >= 0 &&
        pressPickerKey(v, keyForItem(static_cast<uint8_t>(guest))) &&
        waitUntil([&v] { return joined(v) && !pickerOpen(v); }, STEP_WAIT_MS);
    if (!step(picked, HarnessStep::Picker, String(v.name) + "|" +
            (guest < 0 ? "reason=no Guest on the page|" : "") + pickerText(v))) {
      return false;
    }
  }
  return step(asked && joined(v), HarnessStep::Join, String(v.name) + "|" + menuText(v));
}

// A life change on each harness Sigil offered AdjustLife: take
// LIFE_TEST_DELTA from its shown player, check the game display, then give it
// back so the game's totals end where they started.
bool lifeCheck() {
  bool any = false;
  for (uint8_t i = 0; i < activeSigils; ++i) {
    VirtualSigil &v = sigils[i];
    if (!has(v, SigilAction::AdjustLife)) continue;
    any = true;
    const uint8_t player = primaryPlayer(v);
    int32_t before = 0;
    if (!step(waitUntil([&v, player, &before] { return lifeOf(v, player, before); }, STEP_WAIT_MS),
            HarnessStep::Life, String(v.name) + "|P" + String(player) + "|reason=no life on the game display")) {
      return false;
    }
    for (const int32_t delta : {-LIFE_TEST_DELTA, LIFE_TEST_DELTA}) {
      const int32_t expected = before + delta;
      int32_t now = before;
      const bool ok = sendLifeAdjust(v, player, delta) &&
          waitUntil([&v, player, expected, &now] { return lifeOf(v, player, now) && now == expected; },
              LIFE_WAIT_MS);
      if (!step(ok, HarnessStep::Life, String(v.name) + "|P" + String(player) + "|" + String(before) +
              (delta < 0 ? "" : "+") + String(delta) + "=" + String(now))) {
        return false;
      }
      before = expected;
    }
  }
  return step(any, HarnessStep::Life, any ? String() : "reason=AdjustLife not offered|" + allMenus());
}

// One whole game through the menus: join, seat B, start, turns, pause and
// resume, an elimination (3+ players), a win claim confirmed by everyone,
// then a seated Sigil resets the table (or offers a rematch).
bool scenarioGame(uint8_t players, uint8_t turns, bool rematch) {
  if (!scenarioSmoke()) return false;

  // The table must be in its lobby: every Sigil is offered Join or is joined.
  const bool inLobby = waitUntil([] {
    for (uint8_t i = 0; i < activeSigils; ++i) {
      if (!has(sigils[i], SigilAction::Join) && !has(sigils[i], SigilAction::CycleStarter)) return false;
    }
    return true;
  }, STEP_WAIT_MS);
  if (!step(inLobby, HarnessStep::Lobby, inLobby ? String() : "reason=table not in lobby|" + allMenus())) return false;

  // Seats: V1 A, V2 A, V1 B, V2 B (as many as asked for).
  for (uint8_t i = 0; i < activeSigils && i < players; ++i) {
    VirtualSigil &v = sigils[i];
    if (!has(v, SigilAction::Join)) {
      step(true, HarnessStep::Join, String(v.name) + "|already joined");
      continue;
    }
    if (!join(v)) return false;
  }
  for (uint8_t seat = activeSigils; seat < players; ++seat) {
    VirtualSigil &v = sigils[seat - activeSigils];
    if (has(v, SigilAction::RemoveSeatB)) continue;
    const bool added = choose(v, SigilAction::AddSeatB) &&
        waitUntil([&v] { return has(v, SigilAction::RemoveSeatB); }, STEP_WAIT_MS);
    if (!step(added, HarnessStep::SeatB, String(v.name) + "|" + menuText(v))) return false;
  }

  VirtualSigil *host = nullptr;
  waitUntil([&host] { return (host = offering(SigilAction::StartGame)) != nullptr; }, STEP_WAIT_MS);
  if (!step(host != nullptr, HarnessStep::Host, host ? String(host->name)
          : "reason=no harness Sigil is offered Start|" + allMenus())) {
    return false;
  }
  const bool started = choose(*host, SigilAction::StartGame) &&
      waitUntil([] { return offering(SigilAction::Pass) != nullptr; }, START_WAIT_MS);
  if (!step(started, HarnessStep::Start, allMenus())) return false;

  for (uint8_t turn = 1; turn <= turns; ++turn) {
    VirtualSigil *active = nullptr;
    waitUntil([&active] { return (active = offering(SigilAction::Pass)) != nullptr; }, STEP_WAIT_MS);
    if (active == nullptr) return step(false, HarnessStep::Turn, "turn=" + String(turn) + "|" + allMenus());
    VirtualSigil &a = *active;
    // Pass is queued (CancelPass offered) and commits after Atlas's grace.
    const bool ok = choose(a, SigilAction::Pass) &&
        waitUntil([&a] { return has(a, SigilAction::CancelPass); }, STEP_WAIT_MS) &&
        waitUntil([&a] { return !has(a, SigilAction::CancelPass); }, PASS_WAIT_MS);
    if (!step(ok, HarnessStep::Turn, "turn=" + String(turn) + "|from=" + a.name + "|" + allMenus())) return false;
  }

  if (!lifeCheck()) return false;

  VirtualSigil *pauser = offering(SigilAction::Pause);
  bool paused = pauser != nullptr && choose(*pauser, SigilAction::Pause) &&
      waitUntil([pauser] { return has(*pauser, SigilAction::Resume); }, STEP_WAIT_MS);
  if (!step(paused, HarnessStep::Pause, allMenus())) return false;
  const bool resumed = choose(*pauser, SigilAction::Resume) &&
      waitUntil([] { return offering(SigilAction::Pass) != nullptr; }, STEP_WAIT_MS);
  if (!step(resumed, HarnessStep::Resume, allMenus())) return false;

  if (players >= 3) {
    // V1 holds two seats: it pauses, says "I'm out" and eliminates one seat.
    VirtualSigil &out = sigils[0];
    bool ok = choose(out, SigilAction::Pause) &&
        waitUntil([&out] { return has(out, SigilAction::BeginElimination); }, STEP_WAIT_MS) &&
        choose(out, SigilAction::BeginElimination) &&
        waitUntil([&out] { return has(out, SigilAction::Eliminate); }, STEP_WAIT_MS) &&
        choose(out, SigilAction::Eliminate) &&
        waitUntil([] {
          return offering(SigilAction::Resume) != nullptr || offering(SigilAction::Pass) != nullptr;
        }, STEP_WAIT_MS);
    if (!step(ok, HarnessStep::Eliminate, String(out.name) + "|" + allMenus())) return false;
    if (offering(SigilAction::Pass) == nullptr) {
      VirtualSigil *resumer = offering(SigilAction::Resume);
      ok = resumer != nullptr && choose(*resumer, SigilAction::Resume) &&
          waitUntil([] { return offering(SigilAction::Pass) != nullptr; }, STEP_WAIT_MS);
      if (!step(ok, HarnessStep::ResumeAfterElimination, allMenus())) return false;
    }
  }

  // The active player claims the win; every other living player confirms,
  // in the order Atlas asks (one menu at a time).
  VirtualSigil *claimant = offering(SigilAction::ClaimWin);
  const bool claimed = claimant != nullptr && choose(*claimant, SigilAction::ClaimWin) &&
      waitUntil([] { return offering(SigilAction::ConfirmWin) != nullptr || gameOver(); }, STEP_WAIT_MS);
  if (!step(claimed, HarnessStep::ClaimWin, claimant ? String(claimant->name) : allMenus())) return false;
  uint8_t confirmations = 0;
  while (!gameOver()) {
    VirtualSigil *confirmer = nullptr;
    waitUntil([&confirmer] {
      return (confirmer = offering(SigilAction::ConfirmWin)) != nullptr || gameOver();
    }, STEP_WAIT_MS);
    if (gameOver()) break;
    if (confirmer == nullptr || !choose(*confirmer, SigilAction::ConfirmWin)) {
      return step(false, HarnessStep::ConfirmWin, allMenus());
    }
    ++confirmations;
    step(true, HarnessStep::ConfirmWin, String(confirmer->name));
    // The same Sigil may confirm again for its other seat with an unchanged
    // menu, so give Atlas a moment to move the question on.
    idle(300);
    if (confirmations > 2 * VIRTUAL_SIGILS) return step(false, HarnessStep::ConfirmWin, "reason=too many");
  }
  if (!step(waitUntil([] { return gameOver(); }, STEP_WAIT_MS), HarnessStep::GameOver, allMenus())) return false;

  VirtualSigil *finisher = offering(SigilAction::Rematch);
  if (rematch) {
    const bool ok = choose(*finisher, SigilAction::Rematch) &&
        waitUntil([] { return offering(SigilAction::StartGame) != nullptr; }, STEP_WAIT_MS);
    if (!step(ok, HarnessStep::RematchLobby, allMenus())) return false;
    // The last harness Sigil leaves the rematch lobby (both its seats), then
    // joins again, so the next game still has every player.
    VirtualSigil &leaver = sigils[activeSigils - 1];
    const bool left = choose(leaver, SigilAction::Leave) &&
        waitUntil([&leaver] { return has(leaver, SigilAction::Join); }, STEP_WAIT_MS);
    if (!step(left, HarnessStep::Leave, String(leaver.name) + "|" + menuText(leaver))) return false;
    return join(leaver);
  }
  const bool reset = choose(*finisher, SigilAction::ResetTable) && waitUntil([] {
    for (uint8_t i = 0; i < activeSigils; ++i) {
      if (!has(sigils[i], SigilAction::Join)) return false;
    }
    return true;
  }, STEP_WAIT_MS);
  return step(reset, HarnessStep::ResetTable, allMenus());
}

void startPairing() {
  bool any = false;
  for (uint8_t i = 0; i < activeSigils; ++i) {
    VirtualSigil &v = sigils[i];
    if (v.sigilId != UNASSIGNED) continue;
    v.pairing = true;
    v.pairingToken = static_cast<int32_t>(esp_random());
    v.lastPairRequestMs = millis() - PAIR_REQUEST_INTERVAL_MS;
    any = true;
  }
  if (!any) {
    Serial.println("HARNESS|PAIR|ALREADY_PAIRED|use 'forget' first to pair again");
    return;
  }
  Serial.println("HARNESS|PAIR|WAITING|tap 'Pair a Sigil' on the Atlas touchscreen now");
  const bool done = waitUntil([] {
    for (uint8_t i = 0; i < activeSigils; ++i) {
      if (sigils[i].pairing) return false;
    }
    return true;
  }, PAIR_ATTEMPT_MS);
  for (auto &v : sigils) v.pairing = false;
  Serial.println(done ? "HARNESS|PAIR|DONE" : "HARNESS|PAIR|TIMEOUT");
}

// A premade test (the list the Atlas touchscreen offers), reported to Atlas
// from start to finish.
void runTest(HarnessTest test) {
  const uint8_t id = static_cast<uint8_t>(test);
  passed = 0;
  failed = 0;
  abortRequested = false;
  running = true;
  report = TurnHubProtocol::HarnessReportFields();
  report.state = HarnessRunState::Running;
  report.test = id;
  sendReport();
  Serial.printf("HARNESS|RUN|%s\n", TurnHubProtocol::harnessTestName(id));
  switch (test) {
    case HarnessTest::RadioCheck:
      scenarioSmoke();
      break;
    case HarnessTest::QuickGame:
      scenarioGame(2, 3, false);
      break;
    case HarnessTest::FullGame:
      scenarioGame(4, 6, false);
      break;
    case HarnessTest::RematchGame:
      if (scenarioGame(3, 2, true)) scenarioGame(3, 2, false);
      break;
    case HarnessTest::Soak:
      for (uint8_t game = 1; game <= 5 && !abortRequested && failed == 0; ++game) {
        Serial.printf("HARNESS|RUN|soak|game=%u/5\n", game);
        scenarioGame(4, 4, false);
      }
      break;
    default:
      break;
  }
  report.state = abortRequested ? HarnessRunState::Stopped
      : failed > 0 ? HarnessRunState::Failed : HarnessRunState::Passed;
  running = false;
  sendReport();
  summary(TurnHubProtocol::harnessTestName(id));
  if (abortRequested) Serial.println("HARNESS|ABORTED");
  abortRequested = false;
}

// --- Commands ------------------------------------------------------------------

void printHelp() {
  Serial.println("HARNESS|HELP|status | pair | forget | sigils <1|2> | pace <ms> | verbose <on|off> | menu");
  Serial.println("HARNESS|HELP|select <V1|V2> <action> | pick <V1|V2> <up|down|left|right|select>");
  Serial.println("HARNESS|HELP|life <V1|V2> <delta> [player] | lifereq <approve|deny|ignore>");
  Serial.println("HARNESS|HELP|run smoke | run game [players 2-4] [turns] [rematch]");
  Serial.println("HARNESS|HELP|run soak [games] [players] | test [n] (the premade tests Atlas offers) | x (abort a run)");
}

void printStatus() {
  Serial.printf("HARNESS|STATUS|fw=%u.%u.%u|protocol=%u|channel=%u|radio=%s|atlas=%s|sigils=%u|pace=%lu|lifereq=%s\n",
      FIRMWARE_MAJOR, FIRMWARE_MINOR, FIRMWARE_PATCH, static_cast<unsigned>(TurnHubProtocol::VERSION),
      WIFI_CHANNEL, radioReady ? "ready" : "error", atlasKnown ? macText(atlasMac).c_str() : "none",
      static_cast<unsigned>(activeSigils), static_cast<unsigned long>(paceMs), policyName(lifeRequestPolicy));
  for (uint8_t i = 0; i < activeSigils; ++i) {
    const VirtualSigil &v = sigils[i];
    Serial.printf("HARNESS|STATUS|%s|mac=%s|sigil=%s|online=%s|seats=%s|picker=%s|menu=%s\n", v.name,
        macText(v.mac).c_str(), v.sigilId == UNASSIGNED ? "unpaired" : String(v.sigilId).c_str(),
        online(v) ? "yes" : "no", seatsText(v).c_str(), pickerText(v).c_str(), menuText(v).c_str());
  }
}

void resetCounts() {
  passed = 0;
  failed = 0;
  abortRequested = false;
}

String token(const String &text, uint8_t index) {
  int start = 0;
  for (uint8_t i = 0; i <= index; ++i) {
    while (start < static_cast<int>(text.length()) && text[start] == ' ') ++start;
    int end = text.indexOf(' ', start);
    if (end < 0) end = text.length();
    if (i == index) return text.substring(start, end);
    start = end;
  }
  return String();
}

long numberOr(const String &text, long fallback) {
  return text.length() > 0 ? text.toInt() : fallback;
}

void handleCommand(String command) {
  command.trim();
  if (command.length() == 0) return;
  const String verb = token(command, 0);

  if (verb == "help") {
    printHelp();
  } else if (verb == "status") {
    printStatus();
  } else if (verb == "pair") {
    startPairing();
  } else if (verb == "forget") {
    // Local only: Atlas keeps its slots until an admin forgets them in the
    // portal (Device Settings), which also sends Unpair.
    for (auto &v : sigils) {
      v.sigilId = UNASSIGNED;
      v.menuValid = false;
    }
    atlasKnown = false;
    savePairing();
    Serial.println("HARNESS|FORGET|DONE|local only; forget them on Atlas too");
  } else if (verb == "sigils") {
    const long count = numberOr(token(command, 1), VIRTUAL_SIGILS);
    activeSigils = static_cast<uint8_t>(constrain(count, 1L, static_cast<long>(VIRTUAL_SIGILS)));
    printStatus();
  } else if (verb == "pace") {
    if (token(command, 1).length() > 0) {
      paceMs = static_cast<uint32_t>(constrain(token(command, 1).toInt(), 0L,
          static_cast<long>(MAX_PACE_MS)));
      Preferences prefs;
      if (prefs.begin(PREF_NAMESPACE, false)) {
        prefs.putUInt("pace", paceMs);
        prefs.end();
      }
    }
    Serial.printf("HARNESS|PACE|ms=%lu\n", static_cast<unsigned long>(paceMs));
  } else if (verb == "verbose") {
    verbose = token(command, 1) != "off";
    Serial.printf("HARNESS|VERBOSE|%s\n", verbose ? "on" : "off");
  } else if (verb == "menu") {
    Serial.printf("HARNESS|MENU|%s\n", allMenus().c_str());
  } else if (verb == "select") {
    const String who = token(command, 1);
    SigilAction action;
    VirtualSigil *v = who == "V1" ? &sigils[0] : who == "V2" ? &sigils[1] : nullptr;
    if (v == nullptr || !parseAction(token(command, 2), action)) {
      Serial.println("HARNESS|SELECT|USAGE|select <V1|V2> <action>");
    } else if (action == SigilAction::AdjustLife) {
      Serial.println("HARNESS|SELECT|USAGE|adjust-life is not a menu choice; use life <V1|V2> <delta>");
    } else if (!has(*v, action)) {
      Serial.printf("HARNESS|SELECT|NOT_OFFERED|%s|%s\n", v->name, menuText(*v).c_str());
    } else {
      Serial.printf("HARNESS|SELECT|%s\n", choose(*v, action) ? "ACKED" : "NO_ACK");
    }
  } else if (verb == "pick") {
    const String who = token(command, 1);
    const String name = token(command, 2);
    VirtualSigil *v = who == "V1" ? &sigils[0] : who == "V2" ? &sigils[1] : nullptr;
    static const char *const KEYS[] = {"up", "down", "left", "right", "select"};
    int8_t key = -1;
    for (uint8_t i = 0; i < 5; ++i) {
      if (name == KEYS[i]) key = static_cast<int8_t>(i);
    }
    if (v == nullptr || key < 0) {
      Serial.println("HARNESS|PICK|USAGE|pick <V1|V2> <up|down|left|right|select>");
    } else if (!pickerOpen(*v)) {
      Serial.printf("HARNESS|PICK|CLOSED|%s\n", v->name);
    } else {
      pressPickerKey(*v, static_cast<PickerKeyCode>(key));
      idle(300);
      Serial.printf("HARNESS|PICK|%s|%s\n", v->name, pickerText(*v).c_str());
    }
  } else if (verb == "life") {
    const String who = token(command, 1);
    VirtualSigil *v = who == "V1" ? &sigils[0] : who == "V2" ? &sigils[1] : nullptr;
    const long delta = constrain(numberOr(token(command, 2), 0),
        -static_cast<long>(TurnHubProtocol::LIFE_ADJUST_MAX), static_cast<long>(TurnHubProtocol::LIFE_ADJUST_MAX));
    if (v == nullptr || delta == 0) {
      Serial.println("HARNESS|LIFE|USAGE|life <V1|V2> <delta> [player]");
    } else if (!has(*v, SigilAction::AdjustLife)) {
      Serial.printf("HARNESS|LIFE|NOT_OFFERED|%s|%s\n", v->name, menuText(*v).c_str());
    } else {
      const uint8_t player = static_cast<uint8_t>(numberOr(token(command, 3), primaryPlayer(*v)));
      sendLifeAdjust(*v, player, static_cast<int32_t>(delta));
      idle(500);
      Serial.printf("HARNESS|LIFE|%s|%s\n", v->name, seatsText(*v).c_str());
    }
  } else if (verb == "lifereq") {
    const String mode = token(command, 1);
    if (mode == "approve") lifeRequestPolicy = LifeRequestPolicy::Approve;
    else if (mode == "deny") lifeRequestPolicy = LifeRequestPolicy::Deny;
    else if (mode == "ignore") lifeRequestPolicy = LifeRequestPolicy::Ignore;
    Serial.printf("HARNESS|LIFEREQ|%s\n", policyName(lifeRequestPolicy));
  } else if (verb == "test") {
    const long id = numberOr(token(command, 1), -1);
    if (id < 0 || id >= static_cast<long>(HarnessTest::Count)) {
      for (uint8_t i = 0; i < static_cast<uint8_t>(HarnessTest::Count); ++i) {
        Serial.printf("HARNESS|TEST|%u|%s\n", i, TurnHubProtocol::harnessTestName(i));
      }
    } else {
      runTest(static_cast<HarnessTest>(id));
    }
  } else if (verb == "run") {
    const String scenario = token(command, 1);
    resetCounts();
    if (scenario == "smoke") {
      scenarioSmoke();
      summary("smoke");
    } else if (scenario == "game") {
      const uint8_t players = static_cast<uint8_t>(constrain(numberOr(token(command, 2), 4), 2L,
          static_cast<long>(2 * activeSigils)));
      const uint8_t turns = static_cast<uint8_t>(constrain(numberOr(token(command, 3), 6), 1L, 100L));
      const bool rematch = token(command, 4) == "rematch";
      Serial.printf("HARNESS|RUN|game|players=%u|turns=%u\n", players, turns);
      scenarioGame(players, turns, rematch);
      summary("game");
    } else if (scenario == "soak") {
      const long games = constrain(numberOr(token(command, 2), 5), 1L, 1000L);
      const uint8_t players = static_cast<uint8_t>(constrain(numberOr(token(command, 3), 4), 2L,
          static_cast<long>(2 * activeSigils)));
      long completed = 0;
      for (; completed < games && !abortRequested; ++completed) {
        Serial.printf("HARNESS|RUN|soak|game=%ld/%ld\n", completed + 1, games);
        if (!scenarioGame(players, 4, false)) break;
      }
      Serial.printf("HARNESS|SOAK|games=%ld\n", completed);
      summary("soak");
    } else {
      Serial.printf("HARNESS|UNKNOWN_SCENARIO|%s\n", scenario.c_str());
    }
    if (abortRequested) Serial.println("HARNESS|ABORTED");
  } else {
    Serial.printf("HARNESS|UNKNOWN_COMMAND|%s\n", command.c_str());
  }
}

// During a run, only "x" (abort) is read; other input waits.
void pollSerialForAbort() {
  while (Serial.available() > 0 && Serial.peek() == 'x') {
    Serial.read();
    abortRequested = true;
  }
}

void pollSerial() {
  while (Serial.available() > 0) {
    const char ch = static_cast<char>(Serial.read());
    if (ch == '\r') continue;
    if (ch == '\n') {
      handleCommand(inputLine);
      inputLine = "";
      continue;
    }
    if (inputLine.length() < 96) inputLine += ch;
  }
}

}  // namespace

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(250);
  Serial.println("HARNESS|BOOT");
  loadPairing();
  startRadio();
  printStatus();
  printHelp();
}

void loop() {
  pollSerial();
  pump();
  if (pendingTest >= 0) {
    const auto test = static_cast<HarnessTest>(pendingTest);
    pendingTest = -1;
    runTest(test);
  }
  delay(2);
}
