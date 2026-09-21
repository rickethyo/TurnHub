#pragma once

#include <stddef.h>
#include <string.h>

namespace TurnHubIdentity {

// Opaque, type-distinct IDs. Parsing does not create an identity or authorize an
// actor. Atlas repositories must issue IDs and check uniqueness before use.
template <typename Tag, size_t Digits>
class HexId {
 public:
  static constexpr size_t length = Digits;
  static bool parse(const char *text, HexId &out) {
    if (text == nullptr || strlen(text) != Digits) return false;
    for (size_t i = 0; i < Digits; ++i) {
      const char c = text[i];
      if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') ||
            (c >= 'a' && c <= 'f'))) return false;
    }
    HexId parsed;
    memcpy(parsed.value_, text, Digits + 1);
    out = parsed;
    return true;
  }
  bool valid() const { return value_[0] != '\0'; }
  const char *c_str() const { return value_; }
  bool operator==(const HexId &other) const {
    return strcmp(value_, other.value_) == 0;
  }
  bool operator!=(const HexId &other) const { return !(*this == other); }

 private:
  char value_[Digits + 1] = {};
};

// Existing profile spelling is preserved exactly because it is part of NVS
// keys and PIN hash material. Newly issued IDs use uppercase hexadecimal.
using ProfileId = HexId<struct ProfileTag, 8>;
using GameProfileId = HexId<struct GameProfileTag, 32>;
using MatchId = HexId<struct MatchTag, 32>;
using ParticipantId = HexId<struct ParticipantTag, 32>;
using ControllerId = HexId<struct ControllerTag, 32>;
using DeviceId = HexId<struct DeviceTag, 32>;

}  // namespace TurnHubIdentity
