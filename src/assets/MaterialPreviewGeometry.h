#pragma once

#include <memory>

namespace crimson {
struct ModelAsset;
struct ModelGeometryLevel;

// CPU-only, shared geometry for material previews. It is never a browser asset.
// Its sole GPU LOD is uploaded through the renderer's ordinary tracked cache.
std::shared_ptr<const ModelAsset> materialPreviewGeometry();
std::shared_ptr<const ModelGeometryLevel> materialPreviewGeometryLevel();
}
