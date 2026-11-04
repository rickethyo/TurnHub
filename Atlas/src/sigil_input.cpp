// Physical Sigil adapter. Translates ESP-NOW events: Hello and update status
// refresh Atlas's view of the Sigil; while a hub phone hosts the game, menu
// choices and life keys are queued for it (hosted_sigils.cpp); with no hub
// there is no game to act on. The adapter never mutates device state
// directly; audit_adapters.py enforces that.
#include "atlas_app.h"
#include "sigil_update_service.h"
#include "runtime_diagnostics.h"
#include "serial_log.h"
#include "sigil_menu.h"
#include "hosted_sigils.h"
#include "web_api.h"

using TurnHub::serialLog;

namespace TurnHubAtlas {

namespace {

const char *activityKind(PacketType type) {
  switch (type) {
    case PacketType::SelectAction: return "sigil_menu";
    case PacketType::LifeAdjust:
    case PacketType::LifeResponse: return "sigil_life";
    default: return "sigil_event";
  }
}

// With no hub a Sigil has no choices (sigil_menu.cpp): a stale press only
// refreshes its menu.
void handleIdleSelectAction(uint8_t sigilId, int32_t value) {
  using TurnHubProtocol::SigilAction;
  const uint8_t raw = TurnHubProtocol::selectedAction(value);
  if (TurnHubProtocol::selectedRevision(value) != sigilMenuRevision(sigilId) ||
      raw >= static_cast<uint8_t>(SigilAction::Count) ||
      (sigilMenuFor(sigilId).actions & (1u << raw)) == 0) {
    serialLog.print("ATLAS|MENU|STALE|");
    serialLog.print(sigilId);
    serialLog.print("|");
    serialLog.println(raw);
    invalidateSigilMenu(sigilId);
    return;
  }
}

}  // namespace

void processSigilEvents() {
  if (!betweenGames(millis())) {
    sigilBus.closePairing();
    // A code check can't finish mid-game: tell each waiting Sigil no.
    if (sigilBus.pendingPairingCount() > 0) sigilBus.cancelPendingPairings();
  }
  SigilEvent event;
  while (sigilBus.poll(event)) {
    if (event.sigilId >= MAX_PHYSICAL_SIGILS) continue;
    if (event.type == PacketType::Hello) {
      leds.invalidate(event.sigilId);
      invalidateSigilMenu(event.sigilId);
      continue;
    }
    if (event.type == PacketType::SigilUpdateStatus) {
      noteSigilUpdateStatus(event.sigilId, event.value, millis());
      continue;
    }
    TurnHub::recordActivity(activityKind(event.type), String("sigil=") + String(event.sigilId));
    // Phone-hosted games: queued for the hub, never applied here.
    if (hostedForSigil(event.sigilId, millis())) {
      handleHostedSigilEvent(event.sigilId, event.type, event.value);
    } else if (event.type == PacketType::SelectAction) {
      handleIdleSelectAction(event.sigilId, event.value);
    }
  }
}

}  // namespace TurnHubAtlas
