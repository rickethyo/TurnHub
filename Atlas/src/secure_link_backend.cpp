// Firmware-only (mbedTLS). See secure_link_backend.h.
#include "secure_link_backend.h"

#include <Arduino.h>

#include "secure_link_mbedtls.h"
#include "serial_log.h"

namespace TurnHubAtlas {

bool runSecureLinkSelfTest() {
  TurnHubSecureLink::MbedtlsCrypto crypto;
  const uint32_t startMs = millis();
  const TurnHubSecureLink::SelfTestStep step = TurnHubSecureLink::knownAnswerTest(crypto);
  const uint32_t elapsedMs = millis() - startMs;
  if (step == TurnHubSecureLink::SelfTestStep::Pass) {
    TurnHub::serialLog.printf("ATLAS|SECURE_LINK|SELF_TEST|PASS|%lums\n",
        static_cast<unsigned long>(elapsedMs));
    return true;
  }
  TurnHub::serialLog.printf("ATLAS|SECURE_LINK|SELF_TEST|FAIL|%u\n",
      static_cast<unsigned>(step));
  return false;
}

}  // namespace TurnHubAtlas
