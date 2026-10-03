#pragma once

#include "runtime_diagnostics.h"

// Detailed traces are opt-in once the first stability build is accepted.
#ifndef TURNHUB_HTTP_TRACE
#define TURNHUB_HTTP_TRACE 0
#endif

namespace TurnHub {

// Synchronous loopTask requests only. The caller supplies a constant route,
// never a URI with query arguments, credentials, headers or a request body.
// No SD access, locks, printf workspaces or dynamic allocation in these hooks.
class HttpRequestTrace {
 public:
  explicit HttpRequestTrace(const char *route)
      : route_(route), startedMs_(millis()), entryMinimum_(taskStackMinimumFreeBytes()) {
#if TURNHUB_HTTP_TRACE
    static uint32_t sequence = 0;
    id_ = ++sequence;
    serialLog.print("ATLAS|HTTP|BEGIN|id=");
    serialLog.print(id_);
    serialLog.print("|route=");
    serialLog.print(route_);
    serialLog.print("|minimumFreeBytes=");
    serialLog.println(entryMinimum_);
#endif
  }
  ~HttpRequestTrace() {
    const uint32_t endedMs = millis();
    const uint32_t minimum = taskStackMinimumFreeBytes();
#if TURNHUB_HTTP_TRACE
    serialLog.print("ATLAS|HTTP|END|id=");
    serialLog.print(id_);
    serialLog.print("|route=");
    serialLog.print(route_);
    serialLog.print("|durationMs=");
    serialLog.print(endedMs - startedMs_);
    serialLog.print("|minimumFreeBytes=");
    serialLog.print(minimum);
    serialLog.print("|newMinimum=");
    serialLog.println(minimum < entryMinimum_ ? 1 : 0);
#endif
    warnLowLoopStack(minimum, endedMs);
  }
  HttpRequestTrace(const HttpRequestTrace &) = delete;
  HttpRequestTrace &operator=(const HttpRequestTrace &) = delete;
 private:
  const char *route_;
  uint32_t startedMs_;
  uint32_t entryMinimum_;
#if TURNHUB_HTTP_TRACE
  uint32_t id_ = 0;
#endif
};
}  // namespace TurnHub
