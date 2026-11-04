#include "assets.hpp"
#include <cstring>
#include "avatar_masks.h"
namespace th {
const lv_image_dsc_t* PresetAssets::avatar(uint8_t preset) const {
    static lv_image_dsc_t images[TurnHubAvatars::AVATAR_COUNT]{};
    if(preset == 0 || preset > TurnHubAvatars::AVATAR_COUNT) preset = 1; // Safe representative fallback; no second artwork store.
    auto& image = images[preset - 1];
    if(!image.data) {
        image.header.magic = LV_IMAGE_HEADER_MAGIC;
        image.header.cf = LV_COLOR_FORMAT_A8;
        image.header.w = image.header.h = TurnHubArtwork::PRESET_SIDE;
        image.header.stride = TurnHubArtwork::PRESET_SIDE;
        image.data_size = 64 * 64;
        image.data = TurnHubArtwork::presetMask(preset);
    }
    return &image;
}
} // namespace th
