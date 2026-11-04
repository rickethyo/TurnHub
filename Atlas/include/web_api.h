#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include "account_access.h"
#include "intent.h"
#include "update_notice.h"

namespace TurnHubWebApi {

using StateCallback = String (*)(const String &atlasId, const char *bootId);
using RevisionCallback = uint32_t (*)();
void configureClientState(StateCallback state, RevisionCallback revision);

// Register the device-management, session, profile, record and hosted-game
// endpoints on the Atlas WebServer.
void begin(WebServer &server);
bool requirePermission(WebServer &server,uint8_t permission);
// The same check without sending an error response: upload callbacks use it,
// because a response sent mid-upload leaves the browser waiting.
bool hasPermission(WebServer &server,uint8_t permission);
// The request's signed-in profile is verified at the table (presence code).
bool verifiedAtTable(WebServer &server);
// Archived accounts and accounts reset by moderation must sign in again.
bool connectionBlocked(const String &id);
// Ends every session belonging to the profile.
void revokeConnections(const String &id);
// Admin device and table management (ForgetPairing, ConfigurePairing,
// ConfigureSpeaker, ConfigureTableCode, PairConfirm, ResetTable, FactoryReset,
// UpdateSigil, AdvanceSetup; intent.h documents each value). actor is the signed-in
// account; Atlas re-checks its Admin permission.
using DeviceIntentCallback = bool (*)(const String &actor, TurnHub::IntentType type,
    int32_t value, String &message);
using PairingWindowCallback = uint32_t (*)();
void configureDevices(DeviceIntentCallback manage, PairingWindowCallback window);
using HardwareTestCallback = bool (*)(const String &test);
void configureHardwareTests(HardwareTestCallback callback);
// Atlas speaker volume (0 off to 3 high). Saving goes through the device
// callback as ConfigureSpeaker.
using SpeakerVolumeCallback = uint8_t (*)();
void configureSpeaker(SpeakerVolumeCallback volume);

// First-run setup: the current TurnHub::SetupStage, for GET /api/setup. The
// stage itself changes through the device callback (IntentType::AdvanceSetup).
using SetupStageCallback = uint8_t (*)();
void configureSetup(SetupStageCallback stage);
// Table presence (front_panel.cpp): a code the Atlas screen shows, entered on
// the phone, verifies that profile is at the table for a while. First-Admin
// setup, system settings, device names, OTA, Return to lobby and factory
// reset require it on top of the account permission, but only while an Admin
// has turned the table code on (off by default: then remainingMs reports
// every signed-in profile as verified). GET/POST /api/table-code reads and
// sets it (ConfigureTableCode through the device callback).
enum class PresenceResult : uint8_t { Verified, WrongCode, NoCode, TooManyAttempts };
struct PresenceHooks {
  uint32_t (*remainingMs)(const String &profileId) = nullptr;  // 0: not verified.
  bool (*request)(const String &profileId, bool setup) = nullptr;
  PresenceResult (*confirm)(const String &profileId, uint32_t code) = nullptr;
  void (*revoke)(const String &profileId) = nullptr;
  bool (*required)() = nullptr;  // The table code setting; null: off.
};
void configurePresence(const PresenceHooks &hooks);
// Newer firmware (update_notice.h, front_panel.cpp). POST /api/updates/latest
// (public: the release feed is public, and a report can only light the LED)
// takes `atlas` and `sigil` versions; GET /api/updates
// reports them and how many devices are behind.
struct UpdateNoticeHooks {
  void (*note)(const TurnHub::LatestFirmware &latest) = nullptr;
  const TurnHub::LatestFirmware *(*latest)() = nullptr;
  uint8_t (*available)() = nullptr;
};
void configureUpdateNotice(const UpdateNoticeHooks &hooks);

// Atlas policy queries. Session checks do not refresh authentication lifetime.
bool profileAuthenticated(const String &profileId);
bool physicalUseAllowed(const String &profileId);
bool physicalStatsVisible(const String &profileId);
}  // namespace TurnHubWebApi
