////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/texture_asset.hpp"
#include "core/types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace quader {

/**
 * Enumerates BakeMapType values used by the assets layer.
 */
enum class BakeMapType {
    Curvature,
    Thickness,
    AmbientOcclusion,
    Normal,
    Position,
    Id,
    Mask
};

/**
 * Stores the Bake Job Descriptor data contract used by the asset document and registry layer.
 */
struct BakeJobDescriptor {
    AssetId mesh = InvalidAssetId;
    std::vector<BakeMapType> maps;
    std::uint32_t width = 1024;
    std::uint32_t height = 1024;
};

/**
 * Stores the Bake Validation Result data contract used by the asset document and registry layer.
 */
struct BakeValidationResult {
    bool valid = false;
    std::string message;
};

/**
 * Owns Bake Backend behavior within the asset document and registry layer.
 */
class BakeBackend {
public:
    virtual ~BakeBackend() = default;
    virtual const char* name() const = 0;
};

/**
 * Coordinates Bake Service responsibilities for the asset document and registry layer.
 */
class BakeService {
public:
    static BakeValidationResult validate(const BakeJobDescriptor& descriptor);
};

} // namespace quader
