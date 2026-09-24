#pragma once

#ifdef TURNHUB_WOKWI

#include <Arduino.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "protocol.h"

// Wokwi does not currently simulate ESP-NOW. This header is force-included only
// by the sigil-wokwi PlatformIO environment and replaces the small ESP-NOW API
// surface used by the Sigil with an in-process Atlas test harness.
//
// Production builds do not include this file and continue to use real ESP-NOW.
namespace TurnHubWokwi {

using TurnHubProtocol::DisplayMode;
using TurnHubProtocol::Packet;
using TurnHubProtocol::PacketType;

using ReceiveCallback = void (*)(const uint8_t *, const uint8_t *, int);

constexpr uint8_t FAKE_ATLAS_MAC[6] = {0x02, 0x54, 0x48, 0x41, 0x00, 0x01};
constexpr uint8_t DEFAULT_SIGIL_ID = 0;

// Static storage keeps this header C++11-compatible. The shim is used by
// main.cpp; any unused copies produced by force-including the header elsewhere
// are isolated to those translation units.
static ReceiveCallback receiveCallback = nullptr;
static TaskHandle_t consoleTaskHandle = nullptr;
static uint8_t assignedSigilId = DEFAULT_SIGIL_ID;
static String seatNameA = "Player A";
static String seatNameB = "Player B";

// Mirrors the real Atlas (Atlas/src/sigil_bus.cpp): a Sigil is unknown until it
// pairs; pairing is accepted only while Atlas's 15-second window is open (the
// console `pair` command stands in for Atlas's Pair button); afterwards only
// Hello and control packets from the paired Sigil are acknowledged.
static bool sigilPaired = false;
static bool pairingOpen = false;
static uint32_t pairingOpenedMs = 0;
// Hold thresholds Atlas sends (InputTiming) to Sigils that advertise support;
// the real Atlas takes them from the seated players' accessibility settings.
static uint16_t longPressMs = TurnHubProtocol::DEFAULT_LONG_PRESS_MS;
static uint16_t winHoldMs = TurnHubProtocol::DEFAULT_WIN_HOLD_MS;
static bool sigilSupportsTiming = false;

inline bool pairingWindowActive() {
  if (pairingOpen && millis() - pairingOpenedMs >= TurnHubProtocol::PAIRING_WINDOW_MS) {
    pairingOpen = false;
    Serial.println("WOKWI|ATLAS|PAIRING|CLOSED");
  }
  return pairingOpen;
}

inline void injectPacket(PacketType type, int32_t value = 0) {
  if (receiveCallback == nullptr) {
    return;
  }

  const Packet packet = TurnHubProtocol::makePacket(type, assignedSigilId, value);
  receiveCallback(
      FAKE_ATLAS_MAC,
      reinterpret_cast<const uint8_t *>(&packet),
      sizeof(packet));
}

inline void sendAck(PacketType acknowledgedType) {
  injectPacket(PacketType::Ack, static_cast<int32_t>(acknowledgedType));
}

inline void sendHelloAck() {
  sendAck(PacketType::Hello);
}

inline void sendInputTiming() {
  if (!sigilSupportsTiming) {
    Serial.println("WOKWI|ATLAS|TIMING|SKIPPED|SIGIL_HAS_NO_CAPABILITY");
    return;
  }
  injectPacket(PacketType::InputTiming, TurnHubProtocol::encodeInputTiming(longPressMs, winHoldMs));
}

inline String sanitizedName(const String &input) {
  String out;
  out.reserve(TurnHubProtocol::DISPLAY_NAME_MAX_LENGTH);
  for (size_t i = 0;
       i < input.length() && out.length() < TurnHubProtocol::DISPLAY_NAME_MAX_LENGTH;
       ++i) {
    const char c = input[i];
    if (c >= 0x20 && c <= 0x7E) {
      out += c;
    }
  }
  return out;
}

inline void sendDisplayName(uint8_t slot, const String &name) {
  const String clean = sanitizedName(name);
  uint8_t chunkCount = static_cast<uint8_t>(
      (clean.length() + TurnHubProtocol::DISPLAY_NAME_CHUNK_CHARS - 1) /
      TurnHubProtocol::DISPLAY_NAME_CHUNK_CHARS);
  if (chunkCount == 0) {
    chunkCount = 1;
  }

  for (uint8_t chunk = 0; chunk < chunkCount; ++chunk) {
    const uint8_t offset = chunk * TurnHubProtocol::DISPLAY_NAME_CHUNK_CHARS;
    const char c0 = offset < clean.length() ? clean[offset] : '\0';
    const char c1 = offset + 1 < clean.length() ? clean[offset + 1] : '\0';
    const char c2 = offset + 2 < clean.length() ? clean[offset + 2] : '\0';
    const bool finalChunk = chunk + 1 == chunkCount;

    injectPacket(
        PacketType::DisplayNameChunk,
        TurnHubProtocol::encodeDisplayNameChunk(
            slot,
            chunk,
            finalChunk,
            c0,
            c1,
            c2));
  }
}

inline void sendProfile() {
  sendDisplayName(1, seatNameA);
  sendDisplayName(2, seatNameB);
}

inline DisplayMode parseDisplayMode(const String &value) {
  if (value.equalsIgnoreCase("ready")) return DisplayMode::Ready;
  if (value.equalsIgnoreCase("lobby")) return DisplayMode::Lobby;
  if (value.equalsIgnoreCase("starting")) return DisplayMode::Starting;
  if (value.equalsIgnoreCase("running")) return DisplayMode::Running;
  if (value.equalsIgnoreCase("paused")) return DisplayMode::Paused;
  if (value.equalsIgnoreCase("gameover")) return DisplayMode::GameOver;

  const int numeric = value.toInt();
  if (numeric >= static_cast<int>(DisplayMode::Ready) &&
      numeric <= static_cast<int>(DisplayMode::GameOver)) {
    return static_cast<DisplayMode>(numeric);
  }
  return DisplayMode::Ready;
}

inline void printHelp() {
  Serial.println();
  Serial.println("WOKWI ATLAS COMMANDS");
  Serial.println("  help");
  Serial.println("  pair                  open Atlas's 15 s pairing window (then press the Sigil's PAIR)");
  Serial.println("  id <0-7>              Sigil ID the next pairing assigns");
  Serial.println("  timing <longMs> <winMs>  hold thresholds, e.g. timing 3000 6000");
  Serial.println("  blue <0-255>");
  Serial.println("  red <0|1>");
  Serial.println("  green <0|1>");
  Serial.println("  buzz <frequencyHz> <durationMs>");
  Serial.println("  name <A|B> <player name>");
  Serial.println("  profile");
  Serial.println("  state <ready|lobby|starting|running|paused|gameover> <primary> <secondary> <turn> <flags>");
  Serial.println("        flags may be decimal or hex, e.g. 0x08 = ACTIVE");
  Serial.println("  raw <packetType> <value>");
  Serial.println();
}

inline void handleConsoleCommand(String line) {
  line.trim();
  if (line.length() == 0) {
    return;
  }

  const int firstSpace = line.indexOf(' ');
  String command = firstSpace < 0 ? line : line.substring(0, firstSpace);
  String arguments = firstSpace < 0 ? String() : line.substring(firstSpace + 1);
  command.toLowerCase();
  arguments.trim();

  if (command == "help" || command == "?") {
    printHelp();
    return;
  }

  if (command == "pair") {
    pairingOpen = true;
    pairingOpenedMs = millis();
    Serial.print("WOKWI|ATLAS|PAIRING|OPEN|");
    Serial.println(TurnHubProtocol::PAIRING_WINDOW_MS);
    return;
  }

  if (command == "id") {
    const int value = arguments.toInt();
    if (value < 0 || value >= TurnHubProtocol::MAX_SIGILS) {
      Serial.println("WOKWI|ERROR|ID must be 0-7");
      return;
    }
    if (sigilPaired) {
      // A real Atlas keeps a paired Sigil's ID; changing it here would strand it.
      Serial.println("WOKWI|ERROR|already paired; restart the simulation to pair with another ID");
      return;
    }
    assignedSigilId = static_cast<uint8_t>(value);
    Serial.print("WOKWI|ATLAS|NEXT_ID|");
    Serial.println(assignedSigilId);
    return;
  }

  if (command == "timing") {
    unsigned longMs = 0;
    unsigned winMs = 0;
    if (sscanf(arguments.c_str(), "%u %u", &longMs, &winMs) != 2 || longMs > 65535 || winMs > 65535 ||
        !TurnHubProtocol::validInputTiming(static_cast<uint16_t>(longMs), static_cast<uint16_t>(winMs))) {
      Serial.println("WOKWI|ERROR|timing <1000-4000> <3000-10000>, 250 ms steps, win >= long + 1000");
      return;
    }
    longPressMs = static_cast<uint16_t>(longMs);
    winHoldMs = static_cast<uint16_t>(winMs);
    sendInputTiming();
    return;
  }

  if (command == "blue") {
    injectPacket(PacketType::SetBlue, constrain(arguments.toInt(), 0, 255));
    return;
  }

  if (command == "red") {
    injectPacket(PacketType::SetRed, arguments.toInt() != 0 ? 1 : 0);
    return;
  }

  if (command == "green") {
    injectPacket(PacketType::SetGreen, arguments.toInt() != 0 ? 1 : 0);
    return;
  }

  if (command == "buzz") {
    int frequency = 0;
    int duration = 0;
    if (sscanf(arguments.c_str(), "%d %d", &frequency, &duration) != 2 ||
        frequency < 0 || frequency > 65535 || duration < 0 || duration > 65535) {
      Serial.println("WOKWI|ERROR|buzz <frequencyHz> <durationMs>");
      return;
    }
    injectPacket(
        PacketType::Buzzer,
        TurnHubProtocol::encodeTone(
            static_cast<uint16_t>(frequency),
            static_cast<uint16_t>(duration)));
    return;
  }

  if (command == "name") {
    const int separator = arguments.indexOf(' ');
    if (separator < 0) {
      Serial.println("WOKWI|ERROR|name <A|B> <player name>");
      return;
    }

    String slotText = arguments.substring(0, separator);
    String name = arguments.substring(separator + 1);
    slotText.trim();
    name.trim();

    const uint8_t slot = slotText.equalsIgnoreCase("A") ? 1 :
        (slotText.equalsIgnoreCase("B") ? 2 : 0);
    if (slot == 0) {
      Serial.println("WOKWI|ERROR|seat must be A or B");
      return;
    }

    if (slot == 1) {
      seatNameA = sanitizedName(name);
    } else {
      seatNameB = sanitizedName(name);
    }
    sendDisplayName(slot, slot == 1 ? seatNameA : seatNameB);
    return;
  }

  if (command == "profile") {
    sendProfile();
    return;
  }

  if (command == "state") {
    char modeText[16] = {};
    unsigned primary = 0;
    unsigned secondary = 0;
    unsigned turn = 0;
    int flags = 0;
    if (sscanf(
            arguments.c_str(),
            "%15s %u %u %u %i",
            modeText,
            &primary,
            &secondary,
            &turn,
            &flags) != 5 ||
        primary > 255 || secondary > 255 || turn > 255 ||
        flags < 0 || flags > 255) {
      Serial.println("WOKWI|ERROR|state <mode> <primary> <secondary> <turn> <flags>");
      return;
    }

    injectPacket(
        PacketType::DisplayState,
        TurnHubProtocol::encodeDisplayState(
            parseDisplayMode(String(modeText)),
            static_cast<uint8_t>(primary),
            static_cast<uint8_t>(secondary),
            static_cast<uint8_t>(turn),
            static_cast<uint8_t>(flags)));
    return;
  }

  if (command == "raw") {
    int type = 0;
    long value = 0;
    if (sscanf(arguments.c_str(), "%i %li", &type, &value) != 2 ||
        type < 0 || type > 255) {
      Serial.println("WOKWI|ERROR|raw <packetType> <value>");
      return;
    }
    injectPacket(static_cast<PacketType>(type), static_cast<int32_t>(value));
    return;
  }

  Serial.print("WOKWI|ERROR|UNKNOWN_COMMAND|");
  Serial.println(command);
  Serial.println("Type 'help' for commands.");
}

inline void consoleTask(void *) {
  String line;
  line.reserve(96);

  for (;;) {
    while (Serial.available() > 0) {
      const char c = static_cast<char>(Serial.read());
      if (c == '\r') {
        continue;
      }
      if (c == '\n') {
        handleConsoleCommand(line);
        line = "";
        continue;
      }
      if (line.length() < 95) {
        line += c;
      }
    }
    delay(10);
  }
}

inline esp_err_t espNowInit() {
  Serial.println("WOKWI|ESP_NOW|SIMULATED");
  if (consoleTaskHandle == nullptr) {
    const BaseType_t created = xTaskCreatePinnedToCore(
        consoleTask,
        "wokwi-atlas",
        4096,
        nullptr,
        1,
        &consoleTaskHandle,
        0);
    if (created != pdPASS) {
      consoleTaskHandle = nullptr;
      Serial.println("WOKWI|CONSOLE|TASK_ERROR");
    }
  }
  return ESP_OK;
}

inline bool espNowIsPeerExist(const uint8_t *) {
  return true;
}

inline esp_err_t espNowAddPeer(const esp_now_peer_info_t *) {
  return ESP_OK;
}

inline esp_err_t espNowRegisterRecvCb(ReceiveCallback callback) {
  receiveCallback = callback;
  Serial.println("WOKWI|ATLAS|READY");
  printHelp();
  return ESP_OK;
}

inline esp_err_t espNowSend(
    const uint8_t *,
    const uint8_t *data,
    size_t length) {
  if (data == nullptr || length != sizeof(Packet)) {
    return ESP_ERR_INVALID_ARG;
  }

  Packet packet{};
  memcpy(&packet, data, sizeof(packet));

  Serial.print("WOKWI|SIGIL_TX|");
  Serial.print(static_cast<unsigned>(packet.type));
  Serial.print('|');
  Serial.print(packet.sigilId);
  Serial.print('|');
  Serial.println(packet.value);

  if (packet.version != TurnHubProtocol::VERSION) {
    return ESP_OK;
  }

  if (packet.type == PacketType::PairRequest) {
    if (!pairingWindowActive()) {
      Serial.println("WOKWI|ATLAS|PAIRING|IGNORED|WINDOW_CLOSED (type 'pair' first)");
      return ESP_OK;
    }
    sigilPaired = true;
    Serial.print("WOKWI|ATLAS|PAIRING|ACCEPT|");
    Serial.println(assignedSigilId);
    injectPacket(PacketType::PairAccept, packet.value);
    return ESP_OK;
  }

  // Like the real Atlas, ignore Sigils that have not paired, and packets that
  // do not carry the paired Sigil's ID (Hello aside).
  if (!sigilPaired) {
    Serial.println("WOKWI|ATLAS|IGNORED|NOT_PAIRED");
    return ESP_OK;
  }
  if (packet.type != PacketType::Hello && packet.sigilId != assignedSigilId) {
    return ESP_OK;
  }

  switch (packet.type) {
    case PacketType::Hello: {
      const uint8_t capabilities = TurnHubProtocol::helloCapabilities(packet.value);
      const bool timing = (capabilities & TurnHubProtocol::CAPABILITY_INPUT_TIMING) != 0;
      Serial.printf("WOKWI|ATLAS|HELLO|FIRMWARE|%u.%u.%u|CAPABILITIES|0x%02X\n",
          TurnHubProtocol::helloFirmwareMajor(packet.value),
          TurnHubProtocol::helloFirmwareMinor(packet.value),
          TurnHubProtocol::helloFirmwarePatch(packet.value), capabilities);
      sendHelloAck();
      if (timing && !sigilSupportsTiming) {
        sigilSupportsTiming = true;
        sendInputTiming();  // The real Atlas sends timing to capable Sigils.
      }
      break;
    }
    case PacketType::Pass:
    case PacketType::ActionDown:
    case PacketType::ActionUp:
    case PacketType::ActionShort:
    case PacketType::ActionLong:
    case PacketType::ActionWin:
      sendAck(packet.type);
      break;
    case PacketType::DisplayProfileRequest:
      sendAck(packet.type);
      sendProfile();
      break;
    default:
      break;  // The real Atlas acknowledges nothing else.
  }

  return ESP_OK;
}

inline esp_err_t wifiSetPromiscuous(bool) {
  return ESP_OK;
}

inline esp_err_t wifiSetChannel(uint8_t, wifi_second_chan_t) {
  return ESP_OK;
}

}  // namespace TurnHubWokwi

// These macros are defined only after the real ESP-NOW/ESP-WiFi headers have
// been parsed, so their declarations/types remain available while calls from
// main.cpp are redirected to the Wokwi test harness.
#define esp_now_init TurnHubWokwi::espNowInit
#define esp_now_is_peer_exist TurnHubWokwi::espNowIsPeerExist
#define esp_now_add_peer TurnHubWokwi::espNowAddPeer
#define esp_now_register_recv_cb TurnHubWokwi::espNowRegisterRecvCb
#define esp_now_send TurnHubWokwi::espNowSend
#define esp_wifi_set_promiscuous TurnHubWokwi::wifiSetPromiscuous
#define esp_wifi_set_channel TurnHubWokwi::wifiSetChannel

#endif  // TURNHUB_WOKWI
