#pragma once
#include <stdint.h>

// Shared destinations; each device exposes controls appropriate to its role.
namespace TurnHubUi {
enum class Page : uint8_t { Home, Game, Table, Settings };
constexpr uint8_t PAGE_COUNT = 4;
inline const char *pageTitle(Page page) {
  switch (page) {
    case Page::Home: return "Home";
    case Page::Game: return "Game";
    case Page::Table: return "Table";
    case Page::Settings: return "Settings";
  }
  return "Home";
}
}  // namespace TurnHubUi
