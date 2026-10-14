#pragma once

#include "ModelAsset.h"
#include <filesystem>
#include <string>

namespace crimson {

inline constexpr const char* DefaultMaterialId = "builtin:material/default";
inline constexpr const char* DefaultParticleMaterialId = "builtin:material/particle";
inline constexpr const char* GrayTextureId = "builtin:texture/gray";
inline constexpr const char* NeutralNormalTextureId = "builtin:texture/normal";
inline constexpr const char* StudioEnvironmentId = "builtin:environment/studio";

enum class MaterialShading { StandardPbr, Unlit };
enum class MaterialBlend { Opaque, Mask, Alpha, Additive };

struct MaterialAsset {
    std::string id, name, originModel;
    ModelMaterial surface;
    MaterialShading shading{MaterialShading::StandardPbr};
    MaterialBlend blend{MaterialBlend::Opaque};
};

std::string importedMaterialId(const std::string& modelId, std::size_t index);
MaterialAsset defaultMaterialAsset();
MaterialAsset defaultParticleMaterialAsset();
bool particleMaterialCompatible(const MaterialAsset& material);
bool modelMaterialCompatible(const MaterialAsset& material);
std::filesystem::path materialAssetPath(const std::filesystem::path& projectRoot, const std::string& id);
void validateMaterialAsset(const MaterialAsset& material);
MaterialAsset loadMaterialAsset(const std::filesystem::path& projectRoot, const std::string& id);
std::string serializeMaterialAsset(const MaterialAsset& material);
void saveMaterialAsset(const std::filesystem::path& projectRoot, const MaterialAsset& material);
// User-owned sprite materials start white, unlit and additive. Copies retain
// authored factors/slots but receive a new identity and no imported origin.
MaterialAsset createMaterialAsset(const std::filesystem::path& projectRoot, const std::string& name="New Material");
MaterialAsset copyMaterialAsset(const std::filesystem::path& projectRoot, const MaterialAsset& source, const std::string& name={});
// Idempotent compatibility promotion. Authored material files are never overwritten.
void promoteModelMaterials(const std::filesystem::path& projectRoot, const ModelAsset& model,
                          const std::function<bool(const std::string&)>& deleted={});

} // namespace crimson
