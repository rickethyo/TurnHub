#pragma once

#include "protocol.h"
#include "sigil_menu.h"

namespace TurnHubSigil {

// Presentation only. Atlas owns these snapshots; implementations must not send
// packets, interpret inputs, or mutate/persist game or pairing state.
// Calls are serialized by setup() and then the existing display task.
class SigilDisplay {
 public:
  virtual ~SigilDisplay() = default;
  virtual void begin() = 0;
  virtual void showBooting() = 0;
  virtual void showUnpaired() = 0;
  virtual void showReady(uint8_t sigilId) = 0;
  virtual bool setSeatName(uint8_t slot, const char *name) = 0;
  virtual void showGame(const TurnHubProtocol::GameDisplayPacket &snapshot) = 0;
  virtual void showState(uint8_t sigilId, TurnHubProtocol::DisplayMode mode,
      uint8_t primaryPlayer, uint8_t secondaryPlayer, uint8_t turnNumber,
      uint8_t flags) = 0;

  // The profile picker page (e-ink Sigils; see ProfilePickerPacket). It
  // replaces every other screen while Atlas keeps it open.
  virtual void showPicker(const TurnHubProtocol::ProfilePickerPacket &page) { (void)page; }

  // The action menu drawn with the next screen: an e-ink compass legend, or
  // the OLED's list while it is open. Set by the display task before show*().
  void setMenuView(const MenuView &view) { menu_ = view; }
  const MenuView &menuView() const { return menu_; }

 protected:
  MenuView menu_;
};

// Static lifetime, selected at build time. No driver headers reach main.cpp.
SigilDisplay &getSigilDisplay();

}  // namespace TurnHubSigil
