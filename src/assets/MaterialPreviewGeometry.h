#pragma once

#include <memory>

namespace crimson {
struct ModelAsset;
struct ModelGeometryLevel;

enum class MaterialPreviewMesh { Sphere, Cube, RoundedCube, Cylinder, Plane };

// CPU-only, shared geometry for material previews. It is never a browser asset.
// Its sole GPU LOD is uploaded through the renderer's ordinary tracked cache.
std::shared_ptr<const ModelAsset> materialPreviewGeometry(MaterialPreviewMesh kind=MaterialPreviewMesh::Sphere);
std::shared_ptr<const ModelGeometryLevel> materialPreviewGeometryLevel(MaterialPreviewMesh kind=MaterialPreviewMesh::Sphere);
}
