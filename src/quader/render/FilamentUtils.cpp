#include "quader/render/FilamentUtils.h"

#include <algorithm>
#include <limits>

namespace quader {

filament::Box computeBounds(const MeshAsset& mesh) {
    if (mesh.vertices.empty()) {
        return {{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}};
    }

    Vec3 minValue{
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
    };
    Vec3 maxValue{
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
    };

    for (const MeshVertex& vertex : mesh.vertices) {
        minValue.x = std::min(minValue.x, vertex.position.x);
        minValue.y = std::min(minValue.y, vertex.position.y);
        minValue.z = std::min(minValue.z, vertex.position.z);
        maxValue.x = std::max(maxValue.x, vertex.position.x);
        maxValue.y = std::max(maxValue.y, vertex.position.y);
        maxValue.z = std::max(maxValue.z, vertex.position.z);
    }

    filament::Box box;
    box.set(toFilament(minValue), toFilament(maxValue));
    return box;
}

} // namespace quader
