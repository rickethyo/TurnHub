#include "web_pages.h"
#include "web_assets.h"
#include <cstring>

namespace TurnHubWeb {
bool serveFile(WebServer &server, const char *path) {
  if (path == nullptr) return false;
  for (const Asset &asset : ASSETS) {
    if (strcmp(path, asset.path) != 0) continue;
    server.sendHeader("Cache-Control", strncmp(path, "assets/", 7) == 0
        ? "public, max-age=31536000, immutable" : "no-store");
    if (asset.gzip) server.sendHeader("Content-Encoding", "gzip");
    server.send_P(200, asset.type, reinterpret_cast<const char *>(asset.data), asset.size);
    return true;
  }
  return false;
}
} // namespace TurnHubWeb
