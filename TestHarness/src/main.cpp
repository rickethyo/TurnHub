// TurnHub hardware-in-the-loop test harness.
//
// One ESP32 acts as two menu Sigils against a real Atlas: its Wi-Fi station
// MAC and its soft-AP MAC each pair as a separate Sigil, and each can seat two
// players (Seat A and B), so one board plays up to four players. It speaks
// only the shared ESP-NOW contract (protocol.h) and drives the game through
// the same SelectAction choices a real menu Sigil sends. Atlas stays the sole
// authority: the harness reads the menus Atlas offers and never decides an
// outcome. See README.md for the serial commands.

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "protocol.h"

namespace {

using TurnHubProtocol::MenuStateFields;
using TurnHubProtocol::Packet;
using TurnHubProtocol::PacketType;
using TurnHubProtocol::SigilAction;

constexpr uint32_t SERIAL_BAUD = 115200;
// Must match Atlas's soft-AP channel (and Sigil's WIFI_CHANNEL).
constexpr uint8_t WIFI_CHANNEL = 6;
constexpr uint8_t FIRMWARE_MAJOR = 0;
constexpr uint8_t FIRMWARE_MINOR = 1;
constexpr uint8_t FIRMWARE_PATCH = 0;
// Menu Sigil with a shared (two-seat) display; no LED or game display, which
// keeps Atlas's radio traffic to the harness small.
constexpr uint8_t CAPABILITIES = TurnHubProtocol::CAPABILITY_MENU;
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

struct RxFrame {
  uint8_t mac[6];
  uint8_t length;
  uint8_t data[sizeof(TurnHubProtocol::GameDisplayPacket)];
};
QueueHandle_t rxQueue = nullptr;
volatile bool sendDone = false;
volatile bool sendOk = false;

String inputLine;
bool abortRequested = false;
uint16_t passed = 0;
uint16_t failed = 0;

// --- Names ---------------------------------------------------------------------

const char *actionName(uint8_t action) {
  static const char *const NAMES[] = {"join", "cycle-starter", "random-starter", "add-seat-b",
      "remove-seat-b", "start", "cancel-start", "pass", "cancel-pass", "pause", "resume",
      "claim-win", "confirm-win", "deny-win", "begin-elimination", "next-target", "eliminate",
      "cancel-elimination", "rematch", "reset-table", "link-phone"};
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
      TurnHubProtocol::encodeHelloInfo(FIRMWARE_MAJOR, FIRMWARE_MINOR, FIRMWARE_PATCH, CAPABILITIES));
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
      v->menu = TurnHubProtocol::decodeMenuState(packet.value);
      v->menuValid = true;
      v->menuAtMs = millis();
      if (verbose) {
        Serial.printf("HARNESS|MENU|%s|rev=%u|%s\n", v->name,
            static_cast<unsigned>(v->menu.revision), menuText(*v).c_str());
      }
      break;
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

// Keeps every virtual Sigil alive: received packets, Hello, pairing requests.
void pump() {
  RxFrame frame;
  while (rxQueue && xQueueReceive(rxQueue, &frame, 0) == pdTRUE) {
    if (frame.length == sizeof(Packet)) {
      Packet packet;
      memcpy(&packet, frame.data, sizeof(packet));
      handlePacket(frame.mac, packet);
    }
    // GameDisplay snapshots are not requested (no CAPABILITY_GAME_DISPLAY).
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

// --- Reporting -----------------------------------------------------------------

bool step(bool ok, const char *name, const String &detail = String()) {
  if (ok) {
    ++passed;
  } else {
    ++failed;
  }
  Serial.printf("HARNESS|%s|%s", ok ? "PASS" : "FAIL", name);
  if (detail.length() > 0) Serial.printf("|%s", detail.c_str());
  Serial.println();
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
    if (!step(v.sigilId != UNASSIGNED, "PAIRED", String(v.name) +
            (v.sigilId == UNASSIGNED ? "|reason=run 'pair'" : "|sigil=" + String(v.sigilId)))) {
      ok = false;
      continue;
    }
    ok &= step(waitUntil([&v] { return online(v); }, STEP_WAIT_MS), "HELLO_ACK", v.name);
    ok &= step(waitUntil([&v] { return v.menuValid; }, STEP_WAIT_MS), "MENU",
        String(v.name) + "|" + menuText(v));
  }
  return ok;
}

bool gameOver() {
  return offering(SigilAction::Rematch) != nullptr;
}

// One whole game through the menus: join, seat B, start, turns, pause and
// resume, an elimination (3+ players), a win claim confirmed by everyone,
// then the host resets the table (or offers a rematch).
bool scenarioGame(uint8_t players, uint8_t turns, bool rematch) {
  if (!scenarioSmoke()) return false;

  // The table must be in its lobby: every Sigil is offered Join or is joined.
  const bool inLobby = waitUntil([] {
    for (uint8_t i = 0; i < activeSigils; ++i) {
      if (!has(sigils[i], SigilAction::Join) && !has(sigils[i], SigilAction::CycleStarter)) return false;
    }
    return true;
  }, STEP_WAIT_MS);
  if (!step(inLobby, "LOBBY", inLobby ? String() : "reason=table not in lobby|" + allMenus())) return false;

  // Seats: V1 A, V2 A, V1 B, V2 B (as many as asked for).
  for (uint8_t i = 0; i < activeSigils && i < players; ++i) {
    VirtualSigil &v = sigils[i];
    if (!has(v, SigilAction::Join)) {
      step(true, "JOIN", String(v.name) + "|already joined");
      continue;
    }
    const bool joined = choose(v, SigilAction::Join) &&
        waitUntil([&v] { return has(v, SigilAction::CycleStarter); }, STEP_WAIT_MS);
    if (!step(joined, "JOIN", String(v.name) + "|" + menuText(v))) return false;
  }
  for (uint8_t seat = activeSigils; seat < players; ++seat) {
    VirtualSigil &v = sigils[seat - activeSigils];
    if (has(v, SigilAction::RemoveSeatB)) continue;
    const bool added = choose(v, SigilAction::AddSeatB) &&
        waitUntil([&v] { return has(v, SigilAction::RemoveSeatB); }, STEP_WAIT_MS);
    if (!step(added, "SEAT_B", String(v.name) + "|" + menuText(v))) return false;
  }

  VirtualSigil *host = nullptr;
  waitUntil([&host] { return (host = offering(SigilAction::StartGame)) != nullptr; }, STEP_WAIT_MS);
  if (!step(host != nullptr, "HOST", host ? String(host->name)
          : "reason=no harness Sigil may start (another controller joined first?)|" + allMenus())) {
    return false;
  }
  const bool started = choose(*host, SigilAction::StartGame) &&
      waitUntil([] { return offering(SigilAction::Pass) != nullptr; }, START_WAIT_MS);
  if (!step(started, "START", allMenus())) return false;

  for (uint8_t turn = 1; turn <= turns; ++turn) {
    VirtualSigil *active = nullptr;
    waitUntil([&active] { return (active = offering(SigilAction::Pass)) != nullptr; }, STEP_WAIT_MS);
    if (active == nullptr) return step(false, "TURN", "turn=" + String(turn) + "|" + allMenus());
    VirtualSigil &a = *active;
    // Pass is queued (CancelPass offered) and commits after Atlas's grace.
    const bool ok = choose(a, SigilAction::Pass) &&
        waitUntil([&a] { return has(a, SigilAction::CancelPass); }, STEP_WAIT_MS) &&
        waitUntil([&a] { return !has(a, SigilAction::CancelPass); }, PASS_WAIT_MS);
    if (!step(ok, "TURN", "turn=" + String(turn) + "|from=" + a.name + "|" + allMenus())) return false;
  }

  VirtualSigil *pauser = offering(SigilAction::Pause);
  bool paused = pauser != nullptr && choose(*pauser, SigilAction::Pause) &&
      waitUntil([pauser] { return has(*pauser, SigilAction::Resume); }, STEP_WAIT_MS);
  if (!step(paused, "PAUSE", allMenus())) return false;
  const bool resumed = choose(*pauser, SigilAction::Resume) &&
      waitUntil([] { return offering(SigilAction::Pass) != nullptr; }, STEP_WAIT_MS);
  if (!step(resumed, "RESUME", allMenus())) return false;

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
    if (!step(ok, "ELIMINATE", String(out.name) + "|" + allMenus())) return false;
    if (offering(SigilAction::Pass) == nullptr) {
      VirtualSigil *resumer = offering(SigilAction::Resume);
      ok = resumer != nullptr && choose(*resumer, SigilAction::Resume) &&
          waitUntil([] { return offering(SigilAction::Pass) != nullptr; }, STEP_WAIT_MS);
      if (!step(ok, "RESUME_AFTER_ELIMINATION", allMenus())) return false;
    }
  }

  // The active player claims the win; every other living player confirms,
  // in the order Atlas asks (one menu at a time).
  VirtualSigil *claimant = offering(SigilAction::ClaimWin);
  const bool claimed = claimant != nullptr && choose(*claimant, SigilAction::ClaimWin) &&
      waitUntil([] { return offering(SigilAction::ConfirmWin) != nullptr || gameOver(); }, STEP_WAIT_MS);
  if (!step(claimed, "CLAIM_WIN", claimant ? String(claimant->name) : allMenus())) return false;
  uint8_t confirmations = 0;
  while (!gameOver()) {
    VirtualSigil *confirmer = nullptr;
    waitUntil([&confirmer] {
      return (confirmer = offering(SigilAction::ConfirmWin)) != nullptr || gameOver();
    }, STEP_WAIT_MS);
    if (gameOver()) break;
    if (confirmer == nullptr || !choose(*confirmer, SigilAction::ConfirmWin)) {
      return step(false, "CONFIRM_WIN", allMenus());
    }
    ++confirmations;
    step(true, "CONFIRM_WIN", String(confirmer->name));
    // The same Sigil may confirm again for its other seat with an unchanged
    // menu, so give Atlas a moment to move the question on.
    idle(300);
    if (confirmations > 2 * VIRTUAL_SIGILS) return step(false, "CONFIRM_WIN", "reason=too many");
  }
  if (!step(waitUntil([] { return gameOver(); }, STEP_WAIT_MS), "GAME_OVER", allMenus())) return false;

  VirtualSigil *finisher = offering(SigilAction::Rematch);
  if (rematch) {
    const bool ok = choose(*finisher, SigilAction::Rematch) &&
        waitUntil([] { return offering(SigilAction::StartGame) != nullptr; }, STEP_WAIT_MS);
    return step(ok, "REMATCH_LOBBY", allMenus());
  }
  const bool reset = choose(*finisher, SigilAction::ResetTable) && waitUntil([] {
    for (uint8_t i = 0; i < activeSigils; ++i) {
      if (!has(sigils[i], SigilAction::Join)) return false;
    }
    return true;
  }, STEP_WAIT_MS);
  return step(reset, "RESET_TABLE", allMenus());
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

// --- Commands ------------------------------------------------------------------

void printHelp() {
  Serial.println("HARNESS|HELP|status | pair | forget | sigils <1|2> | pace <ms> | verbose <on|off> | menu");
  Serial.println("HARNESS|HELP|select <V1|V2> <action> | run smoke | run game [players 2-4] [turns] [rematch]");
  Serial.println("HARNESS|HELP|run soak [games] [players] | x (abort a run)");
}

void printStatus() {
  Serial.printf("HARNESS|STATUS|fw=%u.%u.%u|protocol=%u|channel=%u|radio=%s|atlas=%s|sigils=%u|pace=%lu\n",
      FIRMWARE_MAJOR, FIRMWARE_MINOR, FIRMWARE_PATCH, static_cast<unsigned>(TurnHubProtocol::VERSION),
      WIFI_CHANNEL, radioReady ? "ready" : "error", atlasKnown ? macText(atlasMac).c_str() : "none",
      static_cast<unsigned>(activeSigils), static_cast<unsigned long>(paceMs));
  for (uint8_t i = 0; i < activeSigils; ++i) {
    const VirtualSigil &v = sigils[i];
    Serial.printf("HARNESS|STATUS|%s|mac=%s|sigil=%s|online=%s|menu=%s\n", v.name, macText(v.mac).c_str(),
        v.sigilId == UNASSIGNED ? "unpaired" : String(v.sigilId).c_str(), online(v) ? "yes" : "no",
        menuText(v).c_str());
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
    } else if (!has(*v, action)) {
      Serial.printf("HARNESS|SELECT|NOT_OFFERED|%s|%s\n", v->name, menuText(*v).c_str());
    } else {
      Serial.printf("HARNESS|SELECT|%s\n", choose(*v, action) ? "ACKED" : "NO_ACK");
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
  delay(2);
}
