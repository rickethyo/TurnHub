# stb JPEG conversion

Unmodified upstream files pinned to [nothings/stb d14d46f6b44456eb88beac58cba833a4135d4c89](https://github.com/nothings/stb/tree/d14d46f6b44456eb88beac58cba833a4135d4c89). Licensed under the MIT option in [LICENSE](LICENSE).

Atlas uses only JPEG decoding (including progressive JPEG) and baseline JPEG encoding for its direct Scryfall fallback. The adapter is `../../src/plane_image.cpp`; it limits input size/dimensions and uses PSRAM on ESP32. No downloaded game artwork is bundled.

- `stb_image.h`: SHA-256 `594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3`
- `stb_image_write.h`: SHA-256 `cbd5f0ad7a9cf4468affb36354a1d2338034f2c12473cf1a8e32053cb6914a05`
- `LICENSE`: SHA-256 `bebfe904b14301657e4e5d655c811d51fd31b97c455b9cc2d8600d6bac6cff63`
