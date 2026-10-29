#pragma once

#include "decals/DecalModelSurface.h"
#include "scene/SceneExtraction.h"
#include <map>

namespace crimson {
// Immutable geometry views from the native modeler. Original vertices stay in
// their existing owner; consumers request an alias through originalLod.
struct ExternalMeshSource {
    ExtractedModel occurrence;
    std::shared_ptr<const ModelAsset> model;
    decals::OriginalPrimitiveLoader originalLod;
    std::shared_ptr<const std::vector<ModelMaterial>> materials;
    Mat4 entityFromLocal;
    SceneDecalNativeSource nativeSource;
    std::uint64_t revision{},fingerprint{};
};
struct ExternalMeshSnapshot {
    std::uint64_t revision{};
    std::vector<ExternalMeshSource> meshes;
    std::map<std::pair<EntityId,std::uint64_t>,std::size_t> byOccurrence;
    std::shared_ptr<const void> sourceOwner,bindingsOwner;
    const ExternalMeshSource* find(EntityId entity,std::uint64_t source)const {
        const auto found=byOccurrence.find({entity,source});
        return found==byOccurrence.end()?nullptr:&meshes[found->second];
    }
};
}
