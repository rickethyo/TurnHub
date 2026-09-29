#pragma once

// When the SD worker checks, drops and remounts the optional microSD card, so
// a card pulled or inserted at any time is noticed without a restart. Pure
// timing policy, host-tested; the SD library calls live in sd_card.cpp (the
// worker task), never on the gameplay loop.

#include <stdint.h>

namespace TurnHubAtlas {

enum class SdStep : uint8_t {
  Idle,     // Nothing due.
  Probe,    // Mounted: read one raw sector to check the card is still there.
  Unmount,  // A probe or card write failed: drop the mount (SD.end()).
  Mount,    // No usable card: try SD.begin(), store check and self-test.
};

enum class SdMountResult : uint8_t {
  Ok,
  NoCard,     // Nothing answered: retry soon, a card may go in any moment.
  CardError,  // A card answered but failed the store check or self-test.
};

class SdHotplug {
 public:
  // Presence check while mounted and nothing else touched the card.
  static constexpr uint32_t PROBE_MS = 3000;
  // Mount attempts while there is no card.
  static constexpr uint32_t RETRY_MS = 2000;
  // A card that answers but fails its checks is retried slowly, so a bad card
  // doesn't cost a self-test write every two seconds.
  static constexpr uint32_t BAD_CARD_RETRY_MS = 30000;

  // The boot mount's outcome.
  void start(SdMountResult boot, uint32_t nowMs) { mountAttempted(boot, nowMs); }

  SdStep next(uint32_t nowMs) const {
    if (unmountPending_) return SdStep::Unmount;
    if (nowMs - lastMs_ < (mounted_ ? PROBE_MS : retryMs_)) return SdStep::Idle;
    return mounted_ ? SdStep::Probe : SdStep::Mount;
  }

  // Any card access while mounted: a probe, or a log write. Success counts as
  // a presence check; failure means the card is gone or broken.
  void accessed(bool ok, uint32_t nowMs) {
    lastMs_ = nowMs;
    if (!ok && mounted_) unmountPending_ = true;
  }
  void unmounted(uint32_t nowMs) {
    mounted_ = false;
    unmountPending_ = false;
    retryMs_ = RETRY_MS;
    lastMs_ = nowMs;
  }
  void mountAttempted(SdMountResult result, uint32_t nowMs) {
    mounted_ = result == SdMountResult::Ok;
    unmountPending_ = false;
    retryMs_ = result == SdMountResult::CardError ? BAD_CARD_RETRY_MS : RETRY_MS;
    lastMs_ = nowMs;
  }
  bool mounted() const { return mounted_; }

 private:
  bool mounted_ = false;
  bool unmountPending_ = false;
  uint32_t retryMs_ = RETRY_MS;
  uint32_t lastMs_ = 0;
};

}  // namespace TurnHubAtlas
