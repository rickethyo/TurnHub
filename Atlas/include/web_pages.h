#pragma once
#include <WebServer.h>

namespace TurnHubWeb {
// Serves a generated flash asset; false without sending when the path is absent.
// Pages are never cached. Content-hashed assets can be cached indefinitely.
bool serveFile(WebServer &server, const char *path);
} // namespace TurnHubWeb
