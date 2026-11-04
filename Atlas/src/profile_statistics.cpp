#include "profile_statistics.h"

#include "game_profile.h"

namespace TurnHubProfileStats {
namespace {

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

void applyGameResult(ProfileStats &persistent, const GameResult &result) {
  ++persistent.gamesPlayed;
  persistent.eliminationsMade += result.eliminationsMade;
  persistent.lastGameEliminationsMade = result.eliminationsMade;
  persistent.lastGameProfile = result.gameProfile;
  persistent.totalGameMs += result.durationMs;
  persistent.lastGameResult = result.result;
  if (result.result == LastGameResult::Win) {
    ++persistent.gamesWon;
  } else if (result.result == LastGameResult::Eliminated) {
    ++persistent.gamesEliminated;
  } else if (result.result == LastGameResult::Conceded) {
    ++persistent.gamesConceded;
  }
  if (result.started) {
    ++persistent.gamesStarted;
  }
  if (result.gameProfile == static_cast<uint8_t>(TurnHub::GameProfile::StarCommander)) {
    ++persistent.starGamesPlayed;
    if (result.result == LastGameResult::Win) ++persistent.starGamesWon;
  }

  persistent.turnsCompleted += result.turns;
  persistent.totalTurnMs += result.turnMs;
  if (result.fastestTurnMs > 0 &&
      (persistent.fastestTurnMs == 0 || result.fastestTurnMs < persistent.fastestTurnMs)) {
    persistent.fastestTurnMs = result.fastestTurnMs;
  }
  if (result.longestTurnMs > persistent.longestTurnMs) {
    persistent.longestTurnMs = result.longestTurnMs;
  }

  persistent.lastGameDurationMs = result.durationMs;
  persistent.lastGameTurns = result.turns;
  persistent.lastGameTurnMs = result.turnMs;
  persistent.lastGameFastestTurnMs = result.fastestTurnMs;
  persistent.lastGameLongestTurnMs = result.longestTurnMs;
}

void mergeStats(ProfileStats &into, const ProfileStats &from) {
  const bool survivorPlayed = into.gamesPlayed > 0;
  ProfileStats last = survivorPlayed ? into : from;
  into.totalTurnMs += from.totalTurnMs;
  into.totalGameMs += from.totalGameMs;
  into.gamesPlayed += from.gamesPlayed;
  into.gamesWon += from.gamesWon;
  into.gamesStarted += from.gamesStarted;
  into.gamesEliminated += from.gamesEliminated;
  into.gamesConceded += from.gamesConceded;
  into.turnsCompleted += from.turnsCompleted;
  into.starGamesPlayed += from.starGamesPlayed;
  into.starGamesWon += from.starGamesWon;
  into.eliminationsMade += from.eliminationsMade;
  if (from.fastestTurnMs > 0 && (into.fastestTurnMs == 0 || from.fastestTurnMs < into.fastestTurnMs))
    into.fastestTurnMs = from.fastestTurnMs;
  if (from.longestTurnMs > into.longestTurnMs) into.longestTurnMs = from.longestTurnMs;
  into.lastGameDurationMs = last.lastGameDurationMs;
  into.lastGameTurns = last.lastGameTurns;
  into.lastGameTurnMs = last.lastGameTurnMs;
  into.lastGameFastestTurnMs = last.lastGameFastestTurnMs;
  into.lastGameLongestTurnMs = last.lastGameLongestTurnMs;
  into.lastGameResult = last.lastGameResult;
  into.lastGameProfile = last.lastGameProfile;
  into.lastGameEliminationsMade = last.lastGameEliminationsMade;
}

const char *resultName(LastGameResult result) {
  switch (result) {
    case LastGameResult::Win: return "Win";
    case LastGameResult::Loss: return "Loss";
    case LastGameResult::Eliminated: return "Eliminated";
    case LastGameResult::Draw: return "Draw";
    case LastGameResult::Conceded: return "Conceded";
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
  appendNumber(out, F("Games conceded: "), stats.gamesConceded);
  appendNumber(out, F("Opponents eliminated: "), stats.eliminationsMade);
  appendNumber(out, F("Star Commander games played: "), stats.starGamesPlayed);
  appendNumber(out, F("Star Commander games won: "), stats.starGamesWon);
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
  appendNumber(out, F("Opponents eliminated: "), stats.lastGameEliminationsMade);
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
