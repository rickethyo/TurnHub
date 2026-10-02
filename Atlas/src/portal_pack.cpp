// Web portal pack installer (portal_pack.h). Pure logic over Files; the card
// implementation lives in sd_card.cpp.
#include "portal_pack.h"

#include <stdio.h>
#include <string.h>

namespace TurnHubPortal {
namespace {
using TurnHubFirmwarePackage::Descriptor;
using TurnHubFirmwarePackage::Version;

constexpr size_t ARCHIVE_HEADER_BYTES = 12;
constexpr size_t ENTRY_HEADER_BYTES = 6;

uint16_t u16(const uint8_t *p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t u32(const uint8_t *p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
      (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

bool join(char *out, size_t capacity, const char *dir, const char *name, const char *suffix) {
  const int n = snprintf(out, capacity, "%s/%s%s", dir, name, suffix);
  return n > 0 && static_cast<size_t>(n) < capacity;
}

}  // namespace

const char *archiveErrorName(ArchiveError error) {
  switch (error) {
    case ArchiveError::None: return "none";
    case ArchiveError::BadDescriptor: return "badDescriptor";
    case ArchiveError::BadMagic: return "badArchive";
    case ArchiveError::BadFileCount: return "badFileCount";
    case ArchiveError::BadPath: return "badPath";
    case ArchiveError::NoIndex: return "noIndex";
    case ArchiveError::Truncated: return "truncated";
    case ArchiveError::TrailingData: return "trailingData";
    case ArchiveError::Storage: return "storage";
  }
  return "unknown";
}

bool parseVersion(const char *text, Version &out) {
  if (text == nullptr) return false;
  unsigned parts[3] = {0, 0, 0};
  int part = 0;
  bool digit = false;
  for (const char *c = text; *c != '\0' && *c != '\n' && *c != '\r'; ++c) {
    if (*c >= '0' && *c <= '9') {
      parts[part] = parts[part] * 10 + static_cast<unsigned>(*c - '0');
      if (parts[part] > 255) return false;
      digit = true;
    } else if (*c == '.' && digit && part < 2) {
      ++part;
      digit = false;
    } else {
      return false;
    }
  }
  if (part != 2 || !digit) return false;
  out = {static_cast<uint8_t>(parts[0]), static_cast<uint8_t>(parts[1]), static_cast<uint8_t>(parts[2])};
  return true;
}

void formatVersion(const Version &version, char *out, size_t capacity) {
  snprintf(out, capacity, "%u.%u.%u", static_cast<unsigned>(version.major),
      static_cast<unsigned>(version.minor), static_cast<unsigned>(version.patch));
}

bool safePackPath(const char *path, size_t length) {
  if (length == 0 || length > MAX_PATH) return false;
  if (path[0] == '/' || path[length - 1] == '/') return false;
  size_t partStart = 0;
  for (size_t i = 0; i <= length; ++i) {
    if (i == length || path[i] == '/') {
      const size_t partLength = i - partStart;
      if (partLength == 0) return false;
      if (partLength == 1 && path[partStart] == '.') return false;
      if (partLength == 2 && path[partStart] == '.' && path[partStart + 1] == '.') return false;
      partStart = i + 1;
      continue;
    }
    const char c = path[i];
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
        c == '.' || c == '_' || c == '-';
    if (!ok) return false;
  }
  return true;
}

bool installedVersion(Files &files, Version &out) {
  char path[64];
  char text[16];
  if (!join(path, sizeof(path), LIVE_DIR, VERSION_FILE, "")) return false;
  return files.readText(path, text, sizeof(text)) && parseVersion(text, out);
}

bool Installer::fail(ArchiveError error) {
  if (fileOpen_) {
    files_.endFile();
    fileOpen_ = false;
  }
  if (error_ == ArchiveError::None) error_ = error;
  stage_ = Stage::Failed;
  return false;
}

bool Installer::header(const TurnHubFirmwarePackage::Header &header) {
  version_ = header.version;
  error_ = ArchiveError::None;
  bufferFill_ = 0;
  fileCount_ = 0;
  filesWritten_ = 0;
  sawIndex_ = false;
  fileOpen_ = false;
  // ROOT_DIR's parent is the store folder Atlas creates at mount; make sure.
  files_.makeDir("/turnhub");
  if (!files_.makeDir(ROOT_DIR) || !files_.removeTree(STAGE_DIR) || !files_.makeDir(STAGE_DIR)) {
    return fail(ArchiveError::Storage);
  }
  stage_ = Stage::Descriptor;
  return true;
}

bool Installer::ensureParents(const char *fullPath) {
  // Every folder between STAGE_DIR and the file.
  char dir[sizeof(path_) + 32];
  const size_t base = strlen(STAGE_DIR);
  const size_t total = strlen(fullPath);
  if (total >= sizeof(dir)) return false;
  for (size_t i = base + 1; i < total; ++i) {
    if (fullPath[i] != '/') continue;
    memcpy(dir, fullPath, i);
    dir[i] = '\0';
    if (!files_.makeDir(dir)) return false;
  }
  return true;
}

bool Installer::startEntryData() {
  char full[sizeof(path_) + 32];
  if (!join(full, sizeof(full), STAGE_DIR, path_, (flags_ & FLAG_GZIP) ? ".gz" : "")) {
    return fail(ArchiveError::BadPath);
  }
  if (!ensureParents(full) || !files_.beginFile(full)) return fail(ArchiveError::Storage);
  fileOpen_ = true;
  if (strcmp(path_, "index.html") == 0) sawIndex_ = true;
  stage_ = Stage::Data;
  return remaining_ == 0 ? finishEntry() : true;
}

bool Installer::finishEntry() {
  fileOpen_ = false;
  if (!files_.endFile()) return fail(ArchiveError::Storage);
  ++filesWritten_;
  bufferFill_ = 0;
  stage_ = filesWritten_ == fileCount_ ? Stage::Done : Stage::EntryHeader;
  return true;
}

bool Installer::consume(const uint8_t *&data, size_t &length) {
  // Collects a fixed-size record into buffer_; true once it is complete.
  auto collect = [&](size_t need) {
    const size_t take = length < need - bufferFill_ ? length : need - bufferFill_;
    memcpy(buffer_ + bufferFill_, data, take);
    bufferFill_ += take;
    data += take;
    length -= take;
    return bufferFill_ == need;
  };

  while (length > 0) {
    switch (stage_) {
      case Stage::Descriptor: {
        if (!collect(sizeof(Descriptor))) return true;
        Descriptor d;
        memcpy(&d, buffer_, sizeof(d));
        if (memcmp(d.magic, descriptorMagic_, sizeof(d.magic)) != 0 ||
            d.product != static_cast<uint8_t>(TurnHubFirmwarePackage::Product::Portal) ||
            TurnHubFirmwarePackage::compareVersions(d.version, version_) != 0) {
          return fail(ArchiveError::BadDescriptor);
        }
        bufferFill_ = 0;
        stage_ = Stage::ArchiveHeader;
        break;
      }
      case Stage::ArchiveHeader: {
        if (!collect(ARCHIVE_HEADER_BYTES)) return true;
        if (memcmp(buffer_, ARCHIVE_MAGIC, sizeof(ARCHIVE_MAGIC)) != 0 || u16(buffer_ + 10) != 0) {
          return fail(ArchiveError::BadMagic);
        }
        fileCount_ = u16(buffer_ + 8);
        if (fileCount_ == 0 || fileCount_ > MAX_FILES) return fail(ArchiveError::BadFileCount);
        bufferFill_ = 0;
        stage_ = Stage::EntryHeader;
        break;
      }
      case Stage::EntryHeader: {
        if (!collect(ENTRY_HEADER_BYTES)) return true;
        pathLength_ = buffer_[0];
        flags_ = buffer_[1];
        remaining_ = u32(buffer_ + 2);
        if (pathLength_ == 0 || pathLength_ > MAX_PATH || (flags_ & ~FLAG_GZIP) != 0) {
          return fail(ArchiveError::BadPath);
        }
        bufferFill_ = 0;
        stage_ = Stage::Path;
        break;
      }
      case Stage::Path: {
        const size_t take = length < pathLength_ - bufferFill_ ? length : pathLength_ - bufferFill_;
        memcpy(path_ + bufferFill_, data, take);
        bufferFill_ += take;
        data += take;
        length -= take;
        if (bufferFill_ < pathLength_) return true;
        path_[pathLength_] = '\0';
        if (!safePackPath(path_, pathLength_) || strcmp(path_, VERSION_FILE) == 0) {
          return fail(ArchiveError::BadPath);
        }
        if (!startEntryData()) return false;
        break;
      }
      case Stage::Data: {
        const size_t take = length < remaining_ ? length : remaining_;
        if (!files_.writeFile(data, take)) return fail(ArchiveError::Storage);
        data += take;
        length -= take;
        remaining_ -= static_cast<uint32_t>(take);
        if (remaining_ == 0 && !finishEntry()) return false;
        break;
      }
      case Stage::Done:
        return fail(ArchiveError::TrailingData);
      case Stage::Idle:
      case Stage::Failed:
        return false;
    }
  }
  return true;
}

bool Installer::image(const uint8_t *data, size_t length) {
  if (stage_ == Stage::Failed || stage_ == Stage::Idle) return false;
  return consume(data, length);
}

bool Installer::commit() {
  if (stage_ == Stage::Failed) return false;
  if (stage_ != Stage::Done) return fail(ArchiveError::Truncated);
  if (!sawIndex_) return fail(ArchiveError::NoIndex);

  char path[64];
  char text[16];
  formatVersion(version_, text, sizeof(text));
  if (!join(path, sizeof(path), STAGE_DIR, VERSION_FILE, "") || !files_.beginFile(path) ||
      !files_.writeFile(reinterpret_cast<const uint8_t *>(text), strlen(text)) || !files_.endFile()) {
    return fail(ArchiveError::Storage);
  }

  // Live -> old, stage -> live. If the second rename fails, put live back.
  if (!files_.removeTree(OLD_DIR)) return fail(ArchiveError::Storage);
  const bool hadLive = files_.exists(LIVE_DIR);
  if (hadLive && !files_.rename(LIVE_DIR, OLD_DIR)) return fail(ArchiveError::Storage);
  if (!files_.rename(STAGE_DIR, LIVE_DIR)) {
    if (hadLive) files_.rename(OLD_DIR, LIVE_DIR);
    return fail(ArchiveError::Storage);
  }
  files_.removeTree(OLD_DIR);
  stage_ = Stage::Idle;
  return true;
}

void Installer::abort() {
  if (fileOpen_) {
    files_.endFile();
    fileOpen_ = false;
  }
  if (stage_ != Stage::Idle) files_.removeTree(STAGE_DIR);
  stage_ = Stage::Idle;
}

}  // namespace TurnHubPortal
