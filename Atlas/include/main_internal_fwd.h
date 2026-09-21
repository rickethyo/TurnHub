#pragma once

// Internal forward declarations for Atlas main.cpp.
// PlatformIO pre-includes this header so handlers near the top of main.cpp can
// call helpers that remain defined later in the same translation unit while
// the larger intent migration is being split into dedicated modules.
namespace {
void finishGameState();
}
