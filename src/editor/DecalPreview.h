#pragma once
#include "crimson-renderer/RenderTypes.h"
#include <crimson/renderer/forward/ModelData.h>
#include <crimson/renderer/forward/DecalRenderTypes.h>
#include <memory>
#include "assets/MaterialPreviewGeometry.h"
#include "resources/ResourceManager.h"
#include "decals/DecalSurface.h"
namespace crimson {
struct DecalPreviewSurface {
    std::shared_ptr<const ModelAsset> model;
    std::shared_ptr<const DecalPatchData> patch;
    Camera camera;
    Mat4 frame{};
};
// Fixed flat receiver and footprint shared by the document and asset card.
// This tiny authored preview has no source assets or worker-dependent geometry.
const DecalPreviewSurface& decalPreviewSurface();
// Uses the same conforming chart builder as placed decals. Invoke on a resource worker.
decals::DecalPatch buildDecalPreviewPatch(MaterialPreviewMesh,std::stop_token stop={});
DecalPreviewSurface decalPreviewGeometry(MaterialPreviewMesh);
std::shared_ptr<const DecalPatchData> decalPreviewRenderPatch(const decals::DecalPatch&,std::uint64_t fingerprint);
class DecalPreviewGeometry {
public:
    const DecalPreviewSurface& prepare(MaterialPreviewMesh,ResourceManager&);
    const std::string& diagnostic() const {return diagnostic_;}
    void clear(){handle_={};surface_={};revision_=0;diagnostic_.clear();}
private:
    MaterialPreviewMesh mesh_{MaterialPreviewMesh::Plane};
    ResourceHandle<decals::DecalPatch> handle_;
    DecalPreviewSurface surface_;
    std::uint64_t revision_{};
    std::string diagnostic_;
};
}
