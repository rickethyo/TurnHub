#pragma once
#include <stddef.h>
#include <stdint.h>
namespace TurnHubAtlas {
// Progressive/baseline JPEG (at most 256 KiB, dimensions <=2048) to the same
// 480x320 baseline JPEG the phone sends. Caller frees the returned buffer.
// Firmware allocations are PSRAM only; failure leaves output null/size zero.
bool normalizePlaneImage(const uint8_t *input, size_t size, uint8_t *&output, size_t &outputSize);
}
