#pragma once
#include "assets/ModelAsset.h"
#include <crimson/renderer/forward/StreamingModel.h>
namespace crimson::streaming {
// Worker-only CPU preprocessing. Each cache level can be read independently.
ModelStream buildModelCache(const ModelAsset&, const std::filesystem::path& projectRoot,bool writeMetadata = true);
// A valid warm cache yields hierarchy/material/primitive metadata without decoding geometry.
std::shared_ptr<ModelAsset> loadModelMetadataCache(const std::filesystem::path& manifest,const std::filesystem::path& projectRoot);
void writeModelMetadataCache(const ModelAsset&,const ModelStream&,const std::filesystem::path& projectRoot);
std::shared_ptr<const ModelGeometryLevel> loadModelLod(const ModelLod&, std::uint32_t primitive = 0, std::uint32_t level = 0,
    bool prepareShadowRays = false,std::stop_token = {});
std::uint64_t modelContentKey(const ModelAsset&);
}
