#pragma once

// The web portal pack: the Atlas portal's files, built by Atlas/web/build.py,
// signed as a .thfw package with product Portal, and unpacked onto the SD
// card. Design and rules: Documentation/engineering/WEB_PORTAL.md.
//
// Archive layout (the package image; little-endian):
//   Descriptor            16 bytes, product Portal, the pack version
//   "THWEBAR1"             8 bytes
//   uint16 fileCount, uint16 reserved (0)
//   fileCount times:
//     uint8 pathLength (1..MAX_PATH), uint8 flags (bit 0: data is gzip),
//     uint32 size, path bytes, data bytes
//
// The installer streams the archive into a staging folder while the package
// reader hashes it, and swaps it live only after the signature, the hash and
// the archive all check out, so a bad pack never replaces a working portal.
// Pure logic over the Files interface: host-tested (ota_scenarios.cpp), and
// sd_card.cpp supplies the card implementation.

#include <stddef.h>
#include <stdint.h>

#include "firmware_package.h"

namespace TurnHubPortal {

constexpr char ARCHIVE_MAGIC[8] = {'T', 'H', 'W', 'E', 'B', 'A', 'R', '1'};
constexpr size_t MAX_PATH = 96;
constexpr uint16_t MAX_FILES = 512;
constexpr uint8_t FLAG_GZIP = 0x01;
// The largest pack Atlas accepts (all files, compressed).
constexpr uint32_t MAX_PACK_BYTES = 16u * 1024u * 1024u;

// Card folders. Live is what Atlas serves; Stage receives an upload.
constexpr const char *ROOT_DIR = "/turnhub/portal";
constexpr const char *LIVE_DIR = "/turnhub/portal/live";
constexpr const char *STAGE_DIR = "/turnhub/portal/stage";
constexpr const char *OLD_DIR = "/turnhub/portal/old";
// Written last into the staged folder: "major.minor.patch".
constexpr const char *VERSION_FILE = "VERSION";

// The folder operations the installer needs. Paths are absolute card paths.
class Files {
 public:
  virtual ~Files() = default;
  virtual bool exists(const char *path) = 0;
  // Creates one folder; true when it exists afterwards.
  virtual bool makeDir(const char *path) = 0;
  // Deletes a folder and everything in it; true when it is gone (or never was).
  virtual bool removeTree(const char *path) = 0;
  virtual bool rename(const char *from, const char *to) = 0;
  // One file written at a time.
  virtual bool beginFile(const char *path) = 0;
  virtual bool writeFile(const uint8_t *data, size_t length) = 0;
  virtual bool endFile() = 0;
  // Reads a small text file into `out` (NUL-terminated); false if missing.
  virtual bool readText(const char *path, char *out, size_t capacity) = 0;
};

enum class ArchiveError : uint8_t {
  None = 0,
  BadDescriptor,
  BadMagic,
  BadFileCount,
  BadPath,
  NoIndex,
  Truncated,
  TrailingData,
  Storage,
};

const char *archiveErrorName(ArchiveError error);

// "1.2.3" <-> Version. parseVersion is strict (three numbers 0..255).
bool parseVersion(const char *text, TurnHubFirmwarePackage::Version &out);
void formatVersion(const TurnHubFirmwarePackage::Version &version, char *out, size_t capacity);

// A pack path is safe to join under the live folder: letters, digits, '.',
// '_', '-' and '/', no leading or trailing '/', no empty, '.' or '..' part.
bool safePackPath(const char *path, size_t length);

// The installed pack's version, from LIVE_DIR/VERSION.
bool installedVersion(Files &files, TurnHubFirmwarePackage::Version &out);

class Installer final : public TurnHubFirmwarePackage::Sink {
 public:
  // descriptorMagic: the 8-byte descriptor magic a pack must open with. Atlas
  // passes its own firmware descriptor's, because that magic may appear only
  // once in the firmware image (thfw.py refuses to package a build otherwise).
  Installer(Files &files, const char *descriptorMagic) : files_(files), descriptorMagic_(descriptorMagic) {}

  // Sink: called by the package reader after the signed header checks out.
  bool header(const TurnHubFirmwarePackage::Header &header) override;
  bool image(const uint8_t *data, size_t length) override;

  // After Reader::finish() succeeded: the archive must be complete. Swaps the
  // staged folder live and removes the old one.
  bool commit();
  // Drops whatever was staged (failed or abandoned upload).
  void abort();

  ArchiveError error() const { return error_; }
  uint16_t filesWritten() const { return filesWritten_; }
  const TurnHubFirmwarePackage::Version &version() const { return version_; }

 private:
  enum class Stage : uint8_t { Idle, Descriptor, ArchiveHeader, EntryHeader, Path, Data, Done, Failed };

  bool fail(ArchiveError error);
  bool consume(const uint8_t *&data, size_t &length);
  bool startEntryData();
  bool finishEntry();
  bool ensureParents(const char *fullPath);

  Files &files_;
  const char *descriptorMagic_;
  Stage stage_ = Stage::Idle;
  ArchiveError error_ = ArchiveError::None;
  TurnHubFirmwarePackage::Version version_ = {0, 0, 0};
  uint8_t buffer_[16] = {};
  size_t bufferFill_ = 0;
  uint16_t fileCount_ = 0;
  uint16_t filesWritten_ = 0;
  uint8_t pathLength_ = 0;
  uint8_t flags_ = 0;
  uint32_t remaining_ = 0;
  bool sawIndex_ = false;
  bool fileOpen_ = false;
  char path_[MAX_PATH + 1] = {};
};

}  // namespace TurnHubPortal
