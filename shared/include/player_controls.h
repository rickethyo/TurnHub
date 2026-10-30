#pragma once
#include <stdint.h>
namespace TurnHubUi {
// Presentation/control IDs shared by the two touchscreens. Atlas maps these to
// existing Intents; these are not another rules engine.
enum class PlayerControl : uint8_t { Poison, Energy, Experience, Treasure, Rad,
  Cast1, Cast2, Blessing, Monarch, Initiative, Partner, Count };
constexpr uint8_t PLAYER_CONTROL_COUNT = static_cast<uint8_t>(PlayerControl::Count);
inline const char *playerControlLabel(uint8_t i) {
  static const char *const labels[] = {"Poison", "Energy", "Experience", "Treasure", "Rad",
      "Commander 1 casts", "Commander 2 casts", "City's blessing", "Monarch", "Initiative", "Partner commanders"};
  return i < PLAYER_CONTROL_COUNT ? labels[i] : "";
}
inline bool playerControlToggle(uint8_t i) { return i >= static_cast<uint8_t>(PlayerControl::Blessing); }
#pragma pack(push, 1)
struct PlayerControls {
  uint16_t allowed;
  int32_t values[PLAYER_CONTROL_COUNT];
};
#pragma pack(pop)
static_assert(sizeof(PlayerControls)==46,"Player controls wire layout changed");
inline uint8_t playerControlRows(const PlayerControls &s, uint8_t *out) {
  uint8_t n=0;
  for (uint8_t i=0;i<PLAYER_CONTROL_COUNT;++i) if (s.allowed & (1u<<i)) out[n++]=i;
  return n;
}
}
