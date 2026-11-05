#include "plane_image.h"
#include "plane_art_fit.h"
#include <stdlib.h>
#include <string.h>
#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

namespace {
void *imageAlloc(size_t size) {
#if defined(ESP_PLATFORM)
  return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
  return malloc(size);
#endif
}
void *imageRealloc(void *ptr, size_t size) {
#if defined(ESP_PLATFORM)
  return heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
  return realloc(ptr, size);
#endif
}
}

#define STBI_MALLOC imageAlloc
#define STBI_REALLOC imageRealloc
#define STBI_FREE free
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_THREAD_LOCALS
#define STBI_NO_SIMD
#define STBI_MAX_DIMENSIONS 2048
#define STB_IMAGE_IMPLEMENTATION
#include "../third_party/stb/stb_image.h"
#define STBIW_MALLOC imageAlloc
#define STBIW_REALLOC imageRealloc
#define STBIW_FREE free
#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../third_party/stb/stb_image_write.h"

namespace TurnHubAtlas {
namespace {
constexpr int W = 480, H = 320;
constexpr size_t OUTPUT_LIMIT = 96 * 1024;
struct Encoded { uint8_t *data; size_t size = 0; bool overflow = false; };
void appendJpeg(void *context, void *bytes, int count) {
  auto *out = static_cast<Encoded *>(context);
  if (count < 0 || static_cast<size_t>(count) > OUTPUT_LIMIT - out->size) { out->overflow = true; return; }
  if (out->overflow) return;
  memcpy(out->data + out->size, bytes, count); out->size += count;
}
}

bool normalizePlaneImage(const uint8_t *input, size_t size, uint8_t *&output, size_t &outputSize) {
  output = nullptr; outputSize = 0;
  if (!input || size < 4 || size > 256 * 1024 || input[0] != 0xFF || input[1] != 0xD8) return false;
  int w = 0, h = 0, channels = 0;
  if (!stbi_info_from_memory(input, static_cast<int>(size), &w, &h, &channels)) return false;
  const auto fit = planeArtFit(w, h);
  if (!fit.w) return false;
  uint8_t *source = stbi_load_from_memory(input, static_cast<int>(size), &w, &h, &channels, 3);
  if (!source) return false;
  auto *scaled = static_cast<uint8_t *>(imageAlloc(W * H * 3));
  auto *encoded = static_cast<uint8_t *>(imageAlloc(OUTPUT_LIMIT));
  if (!scaled || !encoded) { stbi_image_free(source); free(scaled); free(encoded); return false; }
  for (int y = 0; y < H; ++y) {
    const int sy = fit.y + y * fit.h / H;
    for (int x = 0; x < W; ++x) {
      const int sx = fit.x + x * fit.w / W;
      memcpy(scaled + (y * W + x) * 3, source + (sy * w + sx) * 3, 3);
    }
  }
  stbi_image_free(source);
  bool ok = false;
  Encoded out{encoded, 0, false};
  const int qualities[] = {85, 70, 55, 40};
  for (int quality : qualities) {
    out.size = 0; out.overflow = false;
    ok = stbi_write_jpg_to_func(appendJpeg, &out, W, H, 3, scaled, quality) && !out.overflow;
    if (ok) break;
  }
  free(scaled);
  if (!ok) { free(encoded); return false; }
  output = encoded; outputSize = out.size;
  return true;
}
}  // namespace TurnHubAtlas
