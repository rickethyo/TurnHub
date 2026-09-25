#pragma once

#include "protocol.h"
#include "sigil_menu.h"

namespace TurnHubSigil {

// Life shown over the game screen: a change not yet sent (the e-ink leaves
// that to the status ring), and a life request waiting for this Sigil's
// answer (Right approves, Left denies).
struct LifeOverlay {
  int32_t pending = 0;
  uint8_t pendingPlayer = 0;
  TurnHubProtocol::LifeRequestFields request;  // target 0 = none
  bool operator==(const LifeOverlay &o) const {
    return pending == o.pending && pendingPlayer == o.pendingPlayer &&
        request.target == o.request.target && request.requester == o.request.requester &&
        request.tag == o.request.tag && request.delta == o.request.delta;
  }
  bool operator!=(const LifeOverlay &o) const { return !(*this == o); }
};

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

  // The profile picker page (see ProfilePickerPacket). It replaces every
  // other screen while Atlas keeps it open. cursor is the OLED list row
  // (picker_list.h); the e-ink compass ignores it.
  virtual void showPicker(const TurnHubProtocol::ProfilePickerPacket &page, uint8_t cursor) {
    (void)page; (void)cursor;
  }

  // The action menu drawn with the next screen: an e-ink compass legend, or
  // the OLED's list while it is open. Set by the display task before show*().
  void setMenuView(const MenuView &view) { menu_ = view; }
  const MenuView &menuView() const { return menu_; }
  void setLifeOverlay(const LifeOverlay &life) { life_ = life; }

  // Deferred panel upkeep (the e-ink's clean-up refresh after partial
  // updates): milliseconds until idleWork() is due, or UINT32_MAX for none.
  // The display task wakes for it; both run on the display task.
  virtual uint32_t idleWorkDueInMs(uint32_t nowMs) const { (void)nowMs; return UINT32_MAX; }
  virtual void idleWork(uint32_t nowMs) { (void)nowMs; }
  // A bench serial command for the display ("epd ..."); true if handled.
  virtual bool handleCommand(const char *line) { (void)line; return false; }

 protected:
  MenuView menu_;
  LifeOverlay life_;
};

// Static lifetime, selected at build time. No driver headers reach main.cpp.
SigilDisplay &getSigilDisplay();

}  // namespace TurnHubSigil
