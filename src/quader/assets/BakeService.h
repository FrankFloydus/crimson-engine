#pragma once

#include "quader/assets/TextureAsset.h"
#include "quader/core/Types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace quader {

enum class BakeMapType {
    Curvature,
    Thickness,
    AmbientOcclusion,
    Normal,
    Position,
    Id,
    Mask
};

struct BakeJobDescriptor {
    AssetId mesh = InvalidAssetId;
    std::vector<BakeMapType> maps;
    std::uint32_t width = 1024;
    std::uint32_t height = 1024;
};

struct BakeValidationResult {
    bool valid = false;
    std::string message;
};

class BakeBackend {
public:
    virtual ~BakeBackend() = default;
    virtual const char* name() const = 0;
};

class BakeService {
public:
    static BakeValidationResult validate(const BakeJobDescriptor& descriptor);
};

} // namespace quader
