#include "quader/assets/BakeService.h"

namespace quader {

BakeValidationResult BakeService::validate(const BakeJobDescriptor& descriptor) {
    if (descriptor.mesh == InvalidAssetId) {
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
