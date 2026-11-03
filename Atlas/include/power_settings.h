#pragma once
#include <stdint.h>
#include "storage.h"

namespace TurnHub {

// Screen and sleep (owner 2026-11-03): how soon Atlas's backlight goes off,
// whether it may go off during a match, and whether Atlas deep sleeps by
// itself on the cell. Chosen at Atlas (Settings > Device > Sound, screen, sleep)
// or by an Admin in the app (GET/POST /api/power). Each field is an index
// into its choice list below; the screen-off timer only darkens the
// backlight, and a touch lights it again. Deep sleep loses RAM (the lobby
// empties, phones sign in again). The empty-cell sleep and the Sleep button
// are independent of these settings.
struct PowerSettings {
  uint8_t screenOff = 0;   // SCREEN_OFF_CHOICES
  uint8_t gameScreen = 0;  // GAME_SCREEN_CHOICES
  uint8_t autoSleep = 1;   // AUTO_SLEEP_CHOICES
  bool operator==(const PowerSettings &o) const {
    return screenOff == o.screenOff && gameScreen == o.gameScreen && autoSleep == o.autoSleep;
  }
};

// Screen off after this long with no touch and nothing new on screen. Auto is
// 2 minutes on USB and 30 seconds on the cell (owner 2026-11-02).
constexpr uint8_t SCREEN_OFF_CHOICE_COUNT = 7;
constexpr const char *SCREEN_OFF_CHOICES[SCREEN_OFF_CHOICE_COUNT] = {
    "Auto", "30 s", "1 min", "2 min", "5 min", "10 min", "Never"};
constexpr uint32_t SCREEN_OFF_CHOICE_MS[SCREEN_OFF_CHOICE_COUNT] = {
    0, 30000UL, 60000UL, 120000UL, 300000UL, 600000UL, 0};
constexpr uint32_t SCREEN_OFF_AUTO_USB_MS = 2UL * 60UL * 1000UL;
constexpr uint32_t SCREEN_OFF_AUTO_BATTERY_MS = 30UL * 1000UL;
constexpr uint8_t SCREEN_OFF_NEVER = 6;

// May the screen go off while a match is running (not paused)? "On battery"
// is the default: it stays on during a match on USB and goes off on the cell.
constexpr uint8_t GAME_SCREEN_CHOICE_COUNT = 3;
constexpr const char *GAME_SCREEN_CHOICES[GAME_SCREEN_CHOICE_COUNT] = {"On battery", "No", "Yes"};
enum class GameScreen : uint8_t { OnBattery = 0, StayOn = 1, MayGoOff = 2 };

// Deep sleep between games after this long with no player action, on the
// cell only (on USB Atlas never sleeps by itself). Off turns it off.
constexpr uint8_t AUTO_SLEEP_CHOICE_COUNT = 6;
constexpr const char *AUTO_SLEEP_CHOICES[AUTO_SLEEP_CHOICE_COUNT] = {
    "Off", "5 min", "10 min", "15 min", "30 min", "60 min"};
constexpr uint32_t AUTO_SLEEP_CHOICE_MS[AUTO_SLEEP_CHOICE_COUNT] = {
    0, 5UL * 60000UL, 10UL * 60000UL, 15UL * 60000UL, 30UL * 60000UL, 60UL * 60000UL};

inline bool validPowerSettings(const PowerSettings &s) {
  return s.screenOff < SCREEN_OFF_CHOICE_COUNT && s.gameScreen < GAME_SCREEN_CHOICE_COUNT &&
      s.autoSleep < AUTO_SLEEP_CHOICE_COUNT;
}

// The three indices packed in one Intent value: screenOff | gameScreen << 8 |
// autoSleep << 16.
inline int32_t packPowerSettings(const PowerSettings &s) {
  return static_cast<int32_t>(s.screenOff) | static_cast<int32_t>(s.gameScreen) << 8 |
      static_cast<int32_t>(s.autoSleep) << 16;
}
inline bool unpackPowerSettings(int32_t value, PowerSettings &s) {
  if (value < 0 || (value >> 24) != 0) return false;
  PowerSettings out;
  out.screenOff = static_cast<uint8_t>(value & 0xFF);
  out.gameScreen = static_cast<uint8_t>((value >> 8) & 0xFF);
  out.autoSleep = static_cast<uint8_t>((value >> 16) & 0xFF);
  if (!validPowerSettings(out)) return false;
  s = out;
  return true;
}

// How long the screen waits before going dark now; 0 means it stays on.
inline uint32_t screenOffIdleMs(const PowerSettings &s, bool onBattery, bool matchRunning) {
  if (matchRunning) {
    const GameScreen g = static_cast<GameScreen>(s.gameScreen);
    if (g == GameScreen::StayOn || (g == GameScreen::OnBattery && !onBattery)) return 0;
  }
  if (s.screenOff == SCREEN_OFF_NEVER) return 0;
  if (s.screenOff == 0) return onBattery ? SCREEN_OFF_AUTO_BATTERY_MS : SCREEN_OFF_AUTO_USB_MS;
  return SCREEN_OFF_CHOICE_MS[s.screenOff];
}

// The idle wait before deep sleep on the cell; 0 means never by itself.
inline uint32_t autoSleepIdleMs(const PowerSettings &s) {
  return s.autoSleep < AUTO_SLEEP_CHOICE_COUNT ? AUTO_SLEEP_CHOICE_MS[s.autoSleep] : 0;
}

TurnHubStorage::Status loadPowerSettings(PowerSettings &settings);
TurnHubStorage::Status savePowerSettings(const PowerSettings &settings);

// "power" image. Schema 1: {1, screenOff, gameScreen, autoSleep}.
constexpr char POWER_SETTINGS_KEY[] = "power";
constexpr size_t POWER_SETTINGS_SIZE = 4;

inline TurnHubStorage::Status readPowerSettings(TurnHubStorage::BlobStore &store, PowerSettings &settings) {
  using TurnHubStorage::Status;
  uint8_t data[POWER_SETTINGS_SIZE] = {};
  size_t size = 0;
  Status status = store.read(POWER_SETTINGS_KEY, nullptr, 0, size);
  if (status != Status::Ok) return status;
  if (size != sizeof(data)) return Status::Corrupt;
  status = store.read(POWER_SETTINGS_KEY, data, sizeof(data), size);
  if (status != Status::Ok) return status;
  if (size != sizeof(data)) return Status::Corrupt;
  if (data[0] != 1) return Status::UnsupportedSchema;
  PowerSettings out;
  out.screenOff = data[1];
  out.gameScreen = data[2];
  out.autoSleep = data[3];
  if (!validPowerSettings(out)) return Status::Corrupt;
  settings = out;
  return Status::Ok;
}

inline TurnHubStorage::Status writePowerSettings(TurnHubStorage::BlobStore &store, const PowerSettings &settings) {
  if (!validPowerSettings(settings)) return TurnHubStorage::Status::InvalidArgument;
  const uint8_t data[POWER_SETTINGS_SIZE] = {1, settings.screenOff, settings.gameScreen, settings.autoSleep};
  return store.write(POWER_SETTINGS_KEY, data, sizeof(data));
}

}  // namespace TurnHub
