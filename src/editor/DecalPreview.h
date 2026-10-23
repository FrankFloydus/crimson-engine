#pragma once
#include "crimson-renderer/RenderTypes.h"
#include <crimson/renderer/forward/ModelData.h>
#include <crimson/renderer/forward/DecalRenderTypes.h>
#include <memory>
namespace crimson {
struct DecalPreviewSurface {
    std::shared_ptr<const ModelAsset> model;
    std::shared_ptr<const DecalPatchData> patch;
    Camera camera;
};
// Fixed flat receiver and footprint shared by the document and asset card.
// This tiny authored preview has no source assets or worker-dependent geometry.
const DecalPreviewSurface& decalPreviewSurface();
}
