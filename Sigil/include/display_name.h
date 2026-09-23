#pragma once
#include <stddef.h>
#include <string.h>

namespace TurnHubSigil {
// Compare the bounded, rendered name, including clearing and truncation.
template <size_t N> bool updateDisplayName(char (&target)[N], const char *name) {
  char next[N] = {};
  if (name) strncpy(next, name, N - 1);
  if (!strcmp(target, next)) return false;
  memcpy(target, next, N);
  return true;
}
}
