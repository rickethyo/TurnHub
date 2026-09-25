// Atlas factory reset: erase the whole NVS partition, then restart. Firmware-
// only (ESP-IDF flash calls); host tests stub eraseSettingsAndRestart() in
// test_globals.cpp. The decision and its checks live in
// handleFactoryResetIntent (table_intents.cpp).

#include <Arduino.h>
#include <nvs_flash.h>

#include "atlas_app.h"
#include "serial_log.h"

namespace TurnHubAtlas {

void eraseSettingsAndRestart() {
  // nvs_flash_erase() de-initializes NVS first; open handles become invalid,
  // so nothing may use NVS between here and the restart.
  const esp_err_t result = nvs_flash_erase();
  TurnHub::serialLog.println(result == ESP_OK ? "ATLAS|FACTORY_RESET|ATLAS|ERASED"
                                              : "ATLAS|FACTORY_RESET|ATLAS|ERASE_FAILED");
  delay(200);
  ESP.restart();
}

}  // namespace TurnHubAtlas
