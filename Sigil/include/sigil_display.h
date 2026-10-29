#pragma once

#include "protocol.h"
#include "device_theme.h"
#include "sigil_menu.h"

namespace TurnHubSigil {

// Life adjustment preview and requests supplied by Atlas, plus game-screen extras.
struct LifeOverlay {
  uint8_t avatar[2] = {0, 0};  // Seat A and B preset avatars (SeatColor).
  int32_t pending = 0;
  uint8_t pendingPlayer = 0;
  TurnHubProtocol::LifeRequestFields request;  // target 0 = none
  int32_t startingLife = 0;  // StartingLife supplied by Atlas.
  // Atlas offers Cancel pass: this Sigil's pass is in its grace period.
  bool passPending = false;
  // PassPending: the player whose pass is pending anywhere at the table (0 =
  // none), so every Sigil can show it, not only the passer's.
  uint8_t passingPlayer = 0;
  // UpdateNotice: 0 none, 1 an update for Atlas, 2 an update for a Sigil.
  // Drawn as a words line on every screen that has room (updateNoticeText).
  uint8_t update = 0;
  bool operator==(const LifeOverlay &o) const {
    return avatar[0] == o.avatar[0] && avatar[1] == o.avatar[1] &&
        startingLife == o.startingLife && passPending == o.passPending &&
        passingPlayer == o.passingPlayer && update == o.update &&
        pending == o.pending && pendingPlayer == o.pendingPlayer &&
        request.target == o.request.target && request.requester == o.request.requester &&
        request.tag == o.request.tag && request.delta == o.request.delta;
  }
  bool operator!=(const LifeOverlay &o) const { return !(*this == o); }
};

// The words for LifeOverlay::update, short enough for a 20-column row, or
// nullptr for none.
inline const char *updateNoticeText(uint8_t update) {
  return update == 1 ? "Update for Atlas" : (update == 2 ? "Update available" : nullptr);
}

// Presentation only. Atlas owns these snapshots; implementations must not send
// packets, interpret inputs, or mutate/persist game or pairing state.
// Calls are serialized by setup() and then the existing display task.
class SigilDisplay {
 public:
  virtual ~SigilDisplay() = default;
  virtual void begin() = 0;
  // Called only by setup or the display task; changing theme invalidates its caches.
  virtual void setTheme(TurnHubTheme::Id theme) { theme_ = theme; }
  virtual void setInverted(bool inverted) { inverted_ = inverted; }
  bool inverted() const { return inverted_; }
  TurnHubTheme::Id theme() const { return theme_; }
  bool ornamental() const { return theme_ == TurnHubTheme::Id::Brass; }
  virtual void showBooting() = 0;
  virtual void showHardwareTest() {}
  virtual void showUnpaired() = 0;
  virtual void showReady(uint8_t sigilId) = 0;
  // The paired Atlas stopped answering (atlas_link.h). Replaces every other
  // screen until Atlas is heard again and the last state is redrawn. Its
  // only action is the device menu (SigilMenu::setOffline), drawn instead
  // while open.
  virtual void showAtlasLost(uint8_t sigilId) = 0;
  // Pairing v2: the 4-digit code the owner compares with Atlas's screen
  // before confirming there (PAIRING_AND_SECURE_LINK.md). Replaces every other screen,
  // with no action menu, until Atlas confirms, rejects or the check lapses.
  virtual void showPairingCode(uint16_t code) = 0;
  // A firmware update (FIRMWARE_UPDATES.md): what is happening in words, and the
  // percent downloaded (or -1). Replaces every other screen, with no menu.
  virtual void showUpdate(const char *status, int8_t percent) = 0;
  virtual bool setSeatName(uint8_t slot, const char *name) = 0;
  virtual void showGame(const TurnHubProtocol::GameDisplayPacket &snapshot) = 0;
  virtual void showState(uint8_t sigilId, TurnHubProtocol::DisplayMode mode,
      uint8_t primaryPlayer, uint8_t secondaryPlayer, uint8_t turnNumber,
      uint8_t flags) = 0;

  // Atlas-owned modal pages replace the normal game screen.
  virtual void showCommander(const TurnHubProtocol::CommanderFlowPacket &page) { (void)page; }
  virtual void showPicker(const TurnHubProtocol::ProfilePickerPacket &page, uint8_t cursor) {
    (void)page; (void)cursor;
  }

  // Display task snapshot of the touch action labels and hold progress.
  void setMenuView(const MenuView &view) { menu_ = view; }
  const MenuView &menuView() const { return menu_; }
  void setLifeOverlay(const LifeOverlay &life) { life_ = life; }

  // LVGL timers and pressed-button feedback run on the display task.
  virtual uint32_t idleWorkDueInMs(uint32_t nowMs) const { (void)nowMs; return UINT32_MAX; }
  virtual void idleWork(uint32_t nowMs) { (void)nowMs; }
  // A bench serial command for the display; true if handled.
  virtual bool handleCommand(const char *line) { (void)line; return false; }

 protected:
  TurnHubTheme::Id theme_ = TurnHubTheme::Id::Graphite;
  bool inverted_ = false;
  MenuView menu_;
  LifeOverlay life_;
};

// Static lifetime, selected at build time. No driver headers reach main.cpp.
SigilDisplay &getSigilDisplay();

}  // namespace TurnHubSigil
