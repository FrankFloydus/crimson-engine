#pragma once
#include "AssetReferenceManager.h"
#include "MaterialAsset.h"
#include "ModelAsset.h"
#include "scene/Scene.h"
#include <memory>
#include <stop_token>

namespace crimson {
struct AssetReferenceReport {
    std::string targetId,targetName;
    std::vector<AssetUsage> uses,dependencies;
    std::vector<AssetEntry> catalog;
    std::uint64_t requestRevision{};
};
// Captured only for an explicit request. No mutable library or document escapes.
struct AssetReferenceIndexInput {
    std::filesystem::path root;
    std::vector<AssetEntry> catalog;
    std::vector<StoredAssetPath> locations;
    std::vector<std::shared_ptr<const MaterialAsset>> materials;
    std::vector<AssetUsage> liveUsages;
    std::string materialDocumentId;
    std::shared_ptr<const AssetReferenceManager> policy;
    ModelResolvers resolvers;
    std::uint64_t storageRevision{},requestRevision{};
};
void appendSceneAssetUsages(std::vector<AssetUsage>&,std::span<const EntityRecord>,
    const std::string& sceneId,const std::string& sceneName,bool active);
// Shared with deletion/retention indexing; this never publishes reference policy.
std::vector<AssetUsage> collectAssetReferenceUsages(const AssetReferenceIndexInput&,std::stop_token={});
AssetReferenceReport makeAssetReferenceReport(const AssetReferenceIndexInput&,const std::string& target,std::stop_token={});
}
