#pragma once

#include "ModelAsset.h"
#include <filesystem>
#include <string>

namespace crimson {

inline constexpr const char* DefaultMaterialId = "builtin:material/default";
inline constexpr const char* GrayTextureId = "builtin:texture/gray";
inline constexpr const char* NeutralNormalTextureId = "builtin:texture/normal";
inline constexpr const char* StudioEnvironmentId = "builtin:environment/studio";

struct MaterialAsset {
    std::string id, name, originModel;
    ModelMaterial surface;
};

std::string importedMaterialId(const std::string& modelId, std::size_t index);
MaterialAsset defaultMaterialAsset();
void validateMaterialAsset(const MaterialAsset& material);
MaterialAsset loadMaterialAsset(const std::filesystem::path& projectRoot, const std::string& id);
std::string serializeMaterialAsset(const MaterialAsset& material);
void saveMaterialAsset(const std::filesystem::path& projectRoot, const MaterialAsset& material);
// Idempotent compatibility promotion. Authored material files are never overwritten.
void promoteModelMaterials(const std::filesystem::path& projectRoot, const ModelAsset& model,
                          const std::function<bool(const std::string&)>& deleted={});

} // namespace crimson
