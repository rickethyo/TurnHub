#pragma once

// "An update is available" (2026-10-01). Atlas has no internet, so the
// Android app reads the release feed on the phone's own network and reports
// the newest versions here (POST /api/updates/latest). Atlas compares them
// with its own firmware and every paired Sigil's, and its on-board LED blinks
// blue (the pairing rhythm; red stays for pairing) while any is newer. The
// Atlas screen says so in words. Information only: RAM, no Intent, and no
// authority; a wrong report can only light the LED. Pure logic, host-tested.

#include <stdint.h>
#include <stdlib.h>

namespace TurnHub {

struct FirmwareRelease {
  bool known = false;
  uint8_t major = 0;
  uint8_t minor = 0;
  uint8_t patch = 0;
};

// "major.minor.patch", each 0-255, optionally followed by a "-suffix".
inline bool parseFirmwareRelease(const char *text, FirmwareRelease &out) {
  if (text == nullptr) return false;
  uint8_t parts[3] = {};
  const char *p = text;
  for (uint8_t i = 0; i < 3; ++i) {
    if (*p < '0' || *p > '9') return false;
    uint16_t value = 0;
    for (uint8_t digits = 0; *p >= '0' && *p <= '9'; ++p) {
      if (++digits > 3) return false;
      value = static_cast<uint16_t>(value * 10 + (*p - '0'));
    }
    if (value > 255) return false;
    parts[i] = static_cast<uint8_t>(value);
    if (i < 2 && *p++ != '.') return false;
  }
  if (*p != '\0' && *p != '-') return false;
  out.known = true;
  out.major = parts[0];
  out.minor = parts[1];
  out.patch = parts[2];
  return true;
}

// The release is newer than the running major.minor.patch.
inline bool releaseNewer(const FirmwareRelease &release, uint8_t major, uint8_t minor, uint8_t patch) {
  if (!release.known) return false;
  if (release.major != major) return release.major > major;
  if (release.minor != minor) return release.minor > minor;
  return release.patch > patch;
}

// The newest version of each product, as last reported by an app.
struct LatestFirmware {
  FirmwareRelease atlas;
  FirmwareRelease sigilEink;
  FirmwareRelease sigilOled;
};

// Who is behind, for the words on every screen (2026-10-02). The same value
// goes to every Sigil (PacketType::UpdateNotice), so the whole table reads the
// same notice: "Update available for Atlas" when only Atlas is behind,
// "Update available" when any Sigil is.
enum class UpdateKind : uint8_t { None = 0, AtlasOnly = 1, Sigils = 2 };

inline UpdateKind updateKindFor(bool atlasBehind, uint8_t sigilsBehind) {
  if (sigilsBehind > 0) return UpdateKind::Sigils;
  return atlasBehind ? UpdateKind::AtlasOnly : UpdateKind::None;
}

inline const char *updateKindText(UpdateKind kind) {
  switch (kind) {
    case UpdateKind::AtlasOnly: return "Update available for Atlas";
    case UpdateKind::Sigils: return "Update available";
    default: return "";
  }
}

}  // namespace TurnHub
