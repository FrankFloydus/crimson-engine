#pragma once

#include "MaterialGraph.h"
#include <crimson/renderer/forward/MaterialData.h>
#include <filesystem>
#include <optional>
#include <string>

namespace crimson {

inline constexpr const char* DefaultMaterialId = "e698f0ce-a518-596e-8710-ff12e009eb5d";
inline constexpr const char* DefaultParticleMaterialId = "e44b4255-ac0c-58ff-b98f-1e747aad688b";
inline constexpr const char* GrayTextureId = "fb217e2a-3d72-59b1-816e-2d3b8a2259fc";
inline constexpr const char* NeutralNormalTextureId = "7e7af03c-86e0-5f5a-b914-1cea7a1a0647";
inline constexpr const char* StudioEnvironmentId = "c07faadb-115f-5f0b-907c-df09ba978206";

// Upgrade an empty legacy graph, then refresh read-only compatibility values and
// the immutable graph transport attached to ModelMaterial. Never imports stale
// .surface edits when a graph already exists.
void canonicalizeMaterialAsset(MaterialAsset&);
MaterialGraph materialGraphFromLegacy(const ModelMaterial&,MaterialShading,MaterialBlend);
ModelMaterial materialGraphLegacySurface(const MaterialGraph&,const std::string& name={});

// Exact fixed-program lowering for model rendering. Unlike the legacy
// inspector transport this rejects unsupported expressions instead of dropping
// them. The caller must also match a normal map's UV to the primitive tangent UV.
std::optional<ModelMaterial> materialGraphFixedSurface(const MaterialGraph&);
std::vector<std::string> materialTextureReferences(const MaterialAsset&);

std::string importedMaterialId(const std::string& modelId, std::size_t index);
MaterialAsset defaultMaterialAsset();
MaterialAsset defaultParticleMaterialAsset();
// Materialize protected Engine defaults after storage initialization. Existing
// files are retained; bootstrap projects continue using the factory values.
void ensureEngineMaterialAssets(const std::filesystem::path& projectRoot);
bool particleMaterialCompatible(const MaterialAsset& material);
bool modelMaterialCompatible(const MaterialAsset& material);
bool decalMaterialCompatible(const MaterialAsset& material);
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
// Import creates a missing native sidecar once. Existing sidecars are authored
// authority, including imported-origin materials; reload/reimport never resets
// their graphs. Different model-content imports retain independent identities.
// created is called after each new sidecar is saved, including before a later
// sidecar fails, so callers can acknowledge writes and refresh their catalog.
void promoteModelMaterials(const std::filesystem::path& projectRoot, const ModelAsset& model,
                          const std::function<bool(const std::string&)>& deleted={},
                          const std::function<void(const std::string&)>& created={});

} // namespace crimson
