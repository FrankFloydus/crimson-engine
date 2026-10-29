#pragma once
#include "TextureMipCache.h"
namespace crimson {
// CPU-only validation/decoding. HDR is tone mapped only for browser thumbnails.
DecodedImage decodeManagedImage(const std::filesystem::path&);
}
