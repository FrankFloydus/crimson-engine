#pragma once
#include "render/viewport_overlay_frame.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace quader {
// Original Quader icon artwork, shared by both native renderer adapters.
// Bytes are immutable straight-alpha sRGB RGBA8 with their original top-left
// row convention; renderer-owned mip preparation never edits this publication.
struct OverlayIconImage {
    std::uint32_t width{},height{};
    std::shared_ptr<const std::vector<std::byte>> rgba8;
};
const OverlayIconImage& overlay_icon_image(ViewportOverlayIconKind);
}
