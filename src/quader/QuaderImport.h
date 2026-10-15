#pragma once

#include "QuaderReader.h"
#include "assets/AssetReferenceManager.h"
#include "assets/MaterialAsset.h"
#include <set>
#include <stop_token>

namespace crimson {

struct QuaderImportedMesh {
    std::string sourceKey,name,modelId,collisionModelId;
    Mat4 local;
    bool visible{true};
};
struct QuaderImportedLight {
    std::string sourceKey,name;
    Mat4 local;
    Vec3 color{1,1,1};
    float intensity{},radius{};
    bool point{},castShadows{},visible{true};
};
struct QuaderMaterialInput {
    std::string id;
    // nullopt means the destination did not exist while preparing. Raw-byte
    // identity also detects edits that retain file size or timestamp.
    std::optional<std::string> digest;
};
struct QuaderImportResult {
    std::string projectId,sceneId,sourceKey;
    std::vector<QuaderImportedMesh> meshes;
    std::vector<QuaderImportedLight> lights;
    std::vector<std::string> deferredMaterials;
    std::vector<AssetFileChange> files;
    std::vector<QuaderMaterialInput> materialInputs;
    std::vector<std::string> assetIds;
    std::vector<std::shared_ptr<const ModelAsset>> preparedModels;
    std::uint32_t skippedCollisions{},skippedSpots{},skippedLights{};
};
struct QuaderPrepareOptions {
    std::filesystem::path projectRoot;
    float scale{1};
    bool refreshMaterials{}; // Materials Sync: replace from Quader on each import.
    bool syncLights{true};
    std::optional<QuaderCompletedSave> completed;
    // Captured on the main thread. Deletion fallbacks keep their meaning for
    // old references; fresh source imports use a nondeleted asset generation.
    // Explicit nonbuiltin replacements remain authored import choices.
    std::unordered_map<std::string,std::string> materialOverrides;
    std::unordered_map<std::string,MaterialAsset> existingMaterials;
    std::unordered_map<std::string,std::array<std::string,2>> imageOverrides; // color/data, normal
    std::set<std::string> protectedMaterials,reservedIds;
};
// Stable fresh file identity after deletion. Existing deletion records keep
// their old meaning; repeated imports select the same nondeleted generation.
std::string availableQuaderAssetId(const std::string&,const std::set<std::string>& reservedIds);
QuaderImportResult prepareQuaderImport(const QuaderSourceScene&,const QuaderPrepareOptions&,std::stop_token={});

} // namespace crimson
