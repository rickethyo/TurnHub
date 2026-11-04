#pragma once
#include "lvgl.h"
#include <cstdint>
namespace th {
class AssetProvider {
public:
    virtual ~AssetProvider() = default;
    virtual const lv_image_dsc_t* avatar(uint8_t preset) const = 0;
};
class PresetAssets final : public AssetProvider {
public:
    const lv_image_dsc_t* avatar(uint8_t preset) const override;
};
} // namespace th
