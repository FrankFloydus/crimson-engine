////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/bake_service.hpp"

namespace quader {

BakeValidationResult BakeService::validate(const BakeJobDescriptor& descriptor) {
  if (descriptor.mesh == kInvalidAssetId) {
    return {false, "Bake job requires a mesh asset."};
  }
    if (descriptor.maps.empty()) {
        return {false, "Bake job requires at least one output map."};
    }
    if (descriptor.width == 0 || descriptor.height == 0) {
        return {false, "Bake output dimensions must be non-zero."};
    }
    return {true, {}};
}

} // namespace quader
