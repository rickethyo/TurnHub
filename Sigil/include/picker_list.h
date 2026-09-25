#pragma once

// OLED profile picker: list navigation over Atlas's picker page. The OLED
// keeps its menu-list feel (Up/Down move, Select or Right choose, Left goes
// back) and turns the chosen row into the same PickerKey the e-ink compass
// sends, so Atlas sees one picker protocol. The cursor is local and resets
// on every new page. Pure logic, host-tested.

#include <stdint.h>

#include "protocol.h"
#include "sigil_menu.h"

namespace TurnHubSigil {

enum class PickerRow : uint8_t { Item0, Item1, Item2, More, Yes, Back };

struct PickerRows {
  PickerRow rows[TurnHubProtocol::PICKER_PAGE_ITEMS + 2];
  uint8_t count = 0;
};

// List: the page's names, More names (with more than one page), then Back
// (Cancel on the first page). Confirm: Yes, join, then Back.
inline PickerRows pickerRows(const TurnHubProtocol::ProfilePickerPacket &page) {
  PickerRows r;
  if (page.mode == TurnHubProtocol::PickerMode::Confirm) {
    r.rows[r.count++] = PickerRow::Yes;
  } else {
    for (uint8_t i = 0; i < page.itemCount && i < TurnHubProtocol::PICKER_PAGE_ITEMS; ++i) {
      r.rows[r.count++] = static_cast<PickerRow>(i);
    }
    if (page.pageCount > 1) r.rows[r.count++] = PickerRow::More;
  }
  r.rows[r.count++] = PickerRow::Back;
  return r;
}

// The compass key Atlas expects for a row (see ProfilePickerPacket).
inline TurnHubProtocol::PickerKeyCode pickerRowKey(PickerRow row) {
  using TurnHubProtocol::PickerKeyCode;
  switch (row) {
    case PickerRow::Item0: return PickerKeyCode::Up;
    case PickerRow::Item1: return PickerKeyCode::Right;
    case PickerRow::Item2: return PickerKeyCode::Down;
    case PickerRow::More:
    case PickerRow::Yes: return PickerKeyCode::Select;
    case PickerRow::Back: return PickerKeyCode::Left;
  }
  return PickerKeyCode::Left;
}

// Applies a key press. Moves `cursor` for Up/Down; for a choice, returns
// true and sets `send` to the PickerKey to send to Atlas.
inline bool pickerListKey(const TurnHubProtocol::ProfilePickerPacket &page, uint8_t &cursor,
    Key key, TurnHubProtocol::PickerKeyCode &send) {
  const PickerRows r = pickerRows(page);
  if (cursor >= r.count) cursor = 0;
  switch (key) {
    case Key::Up: if (cursor > 0) --cursor; return false;
    case Key::Down: if (cursor + 1 < r.count) ++cursor; return false;
    case Key::Left: send = TurnHubProtocol::PickerKeyCode::Left; return true;
    case Key::Right:
    case Key::Select: send = pickerRowKey(r.rows[cursor]); return true;
    default: return false;
  }
}

}  // namespace TurnHubSigil
