#include "profile_statistics.h"

namespace TurnHubProfileStats {
namespace {

using TurnHub::GameEngine;
using TurnHub::PlayerSeat;
using TurnHub::PlayerStats;
using TurnHubProfiles::LastGameResult;
using TurnHubProfiles::ProfileStats;

uint32_t averageMs(uint64_t total, uint32_t count) {
  return count == 0 ? 0 : static_cast<uint32_t>(total / count);
}

void appendLine(String &out, const __FlashStringHelper *label, const String &value) {
  out += label;
  out += value;
  out += '\n';
}

void appendNumber(String &out, const __FlashStringHelper *label, uint64_t value) {
  out += label;
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(value));
  out += buffer;
  out += '\n';
}

String recordDuration(uint32_t milliseconds) {
  return milliseconds == 0 ? String("—") : formatDuration(milliseconds);
}

}  // namespace

uint8_t recordCompletedGame(
    const GameEngine &game,
    ResolveProfileIdCallback resolveProfileId) {
  if (!game.gameOver() || resolveProfileId == nullptr) {
    return 0;
  }
  if (!TurnHubProfiles::ready() && !TurnHubProfiles::begin()) {
    return 0;
  }

  const uint32_t gameDurationMs = game.gameElapsedMs(millis());
  const uint8_t winner = game.winnerPlayerNumber();
  const uint8_t starter = game.starterPlayerNumber();
  uint8_t updated = 0;

  for (uint8_t index = 0; index < game.playerCount(); ++index) {
    const PlayerSeat *seat = game.playerAt(index);
    if (seat == nullptr) {
      continue;
    }

    const String profileId = resolveProfileId(*seat);
    if (!TurnHubProfiles::profileExists(profileId)) {
      continue;
    }

    ProfileStats persistent{};
    if (!TurnHubProfiles::loadStatsForProfile(profileId, persistent)) {
      continue;
    }

    const PlayerStats *current = game.statsForPlayer(seat->playerNumber);
    const uint32_t turns = current != nullptr ? current->turnsCompleted : 0;
    const uint32_t turnMs = current != nullptr ? current->totalTurnMs : 0;
    const uint32_t fastest = current != nullptr ? current->fastestTurnMs : 0;
    const uint32_t longest = current != nullptr ? current->longestTurnMs : 0;

    ++persistent.gamesPlayed;
    persistent.totalGameMs += gameDurationMs;
    if (seat->playerNumber == winner) {
      ++persistent.gamesWon;
      persistent.lastGameResult = LastGameResult::Win;
    } else if (game.isEliminated(seat->playerNumber)) {
      ++persistent.gamesEliminated;
      persistent.lastGameResult = LastGameResult::Eliminated;
    } else if (game.endedInDraw()) {
      persistent.lastGameResult = LastGameResult::Draw;
    } else {
      persistent.lastGameResult = LastGameResult::Loss;
    }

    if (seat->playerNumber == starter) {
      ++persistent.gamesStarted;
    }

    persistent.turnsCompleted += turns;
    persistent.totalTurnMs += turnMs;
    if (fastest > 0 &&
        (persistent.fastestTurnMs == 0 || fastest < persistent.fastestTurnMs)) {
      persistent.fastestTurnMs = fastest;
    }
    if (longest > persistent.longestTurnMs) {
      persistent.longestTurnMs = longest;
    }

    persistent.lastGameDurationMs = gameDurationMs;
    persistent.lastGameTurns = turns;
    persistent.lastGameTurnMs = turnMs;
    persistent.lastGameFastestTurnMs = fastest;
    persistent.lastGameLongestTurnMs = longest;

    if (TurnHubProfiles::saveStatsForProfile(profileId, persistent)) {
      ++updated;
    }
  }

  return updated;
}

const char *resultName(LastGameResult result) {
  switch (result) {
    case LastGameResult::Win: return "Win";
    case LastGameResult::Loss: return "Loss";
    case LastGameResult::Eliminated: return "Eliminated";
    case LastGameResult::Draw: return "Draw";
    default: return "None";
  }
}

String formatDuration(uint64_t milliseconds) {
  const uint64_t totalSeconds = milliseconds / 1000ULL;
  const uint64_t hours = totalSeconds / 3600ULL;
  const uint64_t minutes = (totalSeconds % 3600ULL) / 60ULL;
  const uint64_t seconds = totalSeconds % 60ULL;

  char buffer[32];
  if (hours > 0) {
    snprintf(
        buffer,
        sizeof(buffer),
        "%lluh %llum %llus",
        static_cast<unsigned long long>(hours),
        static_cast<unsigned long long>(minutes),
        static_cast<unsigned long long>(seconds));
  } else {
    snprintf(
        buffer,
        sizeof(buffer),
        "%llum %llus",
        static_cast<unsigned long long>(minutes),
        static_cast<unsigned long long>(seconds));
  }
  return String(buffer);
}

String buildTextReport(
    const String &profileId,
    const String &displayName,
    const ProfileStats &stats) {
  String out;
  out.reserve(1400);
  out += F("TurnHub Player Statistics\n");
  out += F("=========================\n\n");
  appendLine(out, F("Player: "), displayName.length() > 0 ? displayName : String("Unnamed profile"));
  appendLine(out, F("Profile ID: "), profileId);
  out += '\n';

  out += F("Lifetime\n--------\n");
  appendNumber(out, F("Games played: "), stats.gamesPlayed);
  appendNumber(out, F("Games won: "), stats.gamesWon);
  appendNumber(out, F("Non-wins: "), stats.gamesPlayed >= stats.gamesWon ? stats.gamesPlayed - stats.gamesWon : 0);
  appendNumber(out, F("Games started first: "), stats.gamesStarted);
  appendNumber(out, F("Games eliminated: "), stats.gamesEliminated);
  appendNumber(out, F("Completed turns: "), stats.turnsCompleted);
  appendLine(out, F("Total completed-turn time: "), formatDuration(stats.totalTurnMs));
  appendLine(out, F("Average completed turn: "), stats.turnsCompleted == 0 ? String("—") : formatDuration(averageMs(stats.totalTurnMs, stats.turnsCompleted)));
  appendLine(out, F("Fastest completed turn: "), recordDuration(stats.fastestTurnMs));
  appendLine(out, F("Longest completed turn: "), recordDuration(stats.longestTurnMs));
  appendLine(out, F("Total game time: "), formatDuration(stats.totalGameMs));
  appendLine(out, F("Average game time: "), stats.gamesPlayed == 0 ? String("—") : formatDuration(averageMs(stats.totalGameMs, stats.gamesPlayed)));

  const uint32_t winRateTenths = stats.gamesPlayed == 0
      ? 0
      : static_cast<uint32_t>((static_cast<uint64_t>(stats.gamesWon) * 1000ULL) / stats.gamesPlayed);
  char winRate[16];
  snprintf(winRate, sizeof(winRate), "%lu.%lu%%",
      static_cast<unsigned long>(winRateTenths / 10),
      static_cast<unsigned long>(winRateTenths % 10));
  appendLine(out, F("Win rate: "), String(winRate));

  out += F("\nMost Recent Game\n----------------\n");
  appendLine(out, F("Result: "), String(resultName(stats.lastGameResult)));
  appendLine(out, F("Game duration: "), stats.gamesPlayed == 0 ? String("—") : formatDuration(stats.lastGameDurationMs));
  appendNumber(out, F("Completed turns: "), stats.lastGameTurns);
  appendLine(out, F("Completed-turn time: "), formatDuration(stats.lastGameTurnMs));
  appendLine(out, F("Average completed turn: "), stats.lastGameTurns == 0 ? String("—") : formatDuration(averageMs(stats.lastGameTurnMs, stats.lastGameTurns)));
  appendLine(out, F("Fastest completed turn: "), recordDuration(stats.lastGameFastestTurnMs));
  appendLine(out, F("Longest completed turn: "), recordDuration(stats.lastGameLongestTurnMs));
  out += F("\nGenerated locally by TurnHub Atlas.\n");
  return out;
}

}  // namespace TurnHubProfileStats
