#pragma once

#include "MaterialGraph.h"
#include <filesystem>
#include <string>

namespace crimson {

inline constexpr const char* DefaultMaterialId = "builtin:material/default";
inline constexpr const char* DefaultParticleMaterialId = "builtin:material/particle";
inline constexpr const char* GrayTextureId = "builtin:texture/gray";
inline constexpr const char* NeutralNormalTextureId = "builtin:texture/normal";
inline constexpr const char* StudioEnvironmentId = "builtin:environment/studio";

struct MaterialAsset {
    std::string id, name, originModel;
    ModelMaterial surface;
    MaterialShading shading{MaterialShading::StandardPbr};
    MaterialBlend blend{MaterialBlend::Opaque};
    MaterialGraph graph; // Authored authority; empty only at the legacy/import boundary.
};

// Upgrade an empty legacy graph, then refresh read-only compatibility values and
// the immutable graph transport attached to ModelMaterial. Never imports stale
// .surface edits when a graph already exists.
void canonicalizeMaterialAsset(MaterialAsset&);
MaterialGraph materialGraphFromLegacy(const ModelMaterial&,MaterialShading,MaterialBlend);
ModelMaterial materialGraphLegacySurface(const MaterialGraph&,const std::string& name={});
std::vector<std::string> materialTextureReferences(const MaterialAsset&);

std::string importedMaterialId(const std::string& modelId, std::size_t index);
MaterialAsset defaultMaterialAsset();
MaterialAsset defaultParticleMaterialAsset();
bool particleMaterialCompatible(const MaterialAsset& material);
bool modelMaterialCompatible(const MaterialAsset& material);
// One native sampler overrides all channels; legacy/imported materials keep
// their original slot samplers until an explicit shared-sampler edit.
ModelSampler materialSampler(const ModelMaterial&, const ModelTexture&);
std::filesystem::path materialAssetPath(const std::filesystem::path& projectRoot, const std::string& id);
void validateMaterialAsset(const MaterialAsset& material);
MaterialAsset loadMaterialAsset(const std::filesystem::path& projectRoot, const std::string& id);
std::string serializeMaterialAsset(const MaterialAsset& material);
void saveMaterialAsset(const std::filesystem::path& projectRoot, const MaterialAsset& material);
// New materials start Lit/Opaque with sRGB midgray, roughness 1 and metallic 0.
// Copies retain
// authored factors/slots but receive a new identity and no imported origin.
MaterialAsset createMaterialAsset(const std::filesystem::path& projectRoot, const std::string& name="New Material");
MaterialAsset copyMaterialAsset(const std::filesystem::path& projectRoot, const MaterialAsset& source, const std::string& name={});
// Idempotent compatibility promotion. Authored material files are never overwritten.
void promoteModelMaterials(const std::filesystem::path& projectRoot, const ModelAsset& model,
                          const std::function<bool(const std::string&)>& deleted={});

} // namespace crimson
