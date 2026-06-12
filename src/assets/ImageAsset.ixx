module;

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

export module quader.assets.image;

export namespace quader {

enum class ImageColorEncoding : uint8_t {
    srgb,
    linear,
};

struct Rgba8Image {
    uint32_t width = 0;
    uint32_t height = 0;
    ImageColorEncoding color_encoding = ImageColorEncoding::srgb;
    std::string source_path;
    std::vector<uint8_t> rgba;

    [[nodiscard]] bool has_pixels() const
    {
        return width > 0 && height > 0 &&
            rgba.size() == static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    }
};

[[nodiscard]] Rgba8Image load_rgba8_image_from_file(
    std::string_view source_path,
    ImageColorEncoding color_encoding);

} // namespace quader
