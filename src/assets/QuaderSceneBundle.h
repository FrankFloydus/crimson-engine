#pragma once

#include "ContentStorage.h"
#include "AssetTransaction.h"
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace crimson {

// Individual immutable geometry generations retain their original UUIDs. The
// managed files are reconstructed from the tracked scene bundle as needed.
struct QuaderBundleModel {
    std::string id, legacyManifest;
    // manifest.json, model.gltf, source.gltf, buffers/0.bin.
    std::array<std::string,4> memberIds;
    std::string manifest, gltf, buffer;
    // Only generations used by the latest source import appear in the catalog.
    // Retired payloads remain available while scenes or undo history use them.
    bool current{true};
};
struct QuaderSceneBundle {
    std::string id, projectId, sceneId, name;
    std::vector<QuaderBundleModel> models;
};

std::string quaderSceneBundleId(std::string_view project, std::string_view scene);
std::string encodeQuaderSceneBundle(const QuaderSceneBundle& bundle);
QuaderSceneBundle decodeQuaderSceneBundle(std::string_view bytes);
std::filesystem::path quaderBundleCacheDirectory(const std::filesystem::path& root,
                                               std::string_view bundleId,
                                               std::string_view modelId);
std::vector<StoredAssetPath> quaderBundleCacheLocations(const std::filesystem::path& root,
                                                     const QuaderSceneBundle& bundle);
std::optional<std::filesystem::path> quaderBundleSource(const std::filesystem::path& root,
                                                      std::string_view modelId);
std::optional<std::pair<std::filesystem::path,QuaderSceneBundle>> findQuaderSceneBundle(
    const std::filesystem::path& root, std::string_view project, std::string_view scene);
std::filesystem::path quaderSceneContentDirectory(const std::filesystem::path& root,
                                                  const QuaderSceneBundle& bundle);
std::filesystem::path defaultQuaderSceneBundlePath(const std::filesystem::path& root,
                                                 const QuaderSceneBundle& bundle);
// Called after storage recovery/UUID conversion, before stores and watchers.
void initializeQuaderSceneBundles(const std::filesystem::path& root);
// Prepare explicit mesh deletion as part of the caller's reference/document
// transaction. An empty bundle is removed; a linked scene can recreate it.
// Unknown files in an expanded cache directory are never deletion targets.
struct PreparedQuaderBundleModelRemoval {
    std::vector<AssetFileChange> files;
    std::vector<std::string> modelIds, memberIds, removedBundleIds;
    std::vector<std::filesystem::path> emptyDirectories;
    std::vector<std::pair<std::filesystem::path,std::string>> baselines;
    std::vector<StoredAssetPath> memberLocations;
    std::uint64_t storageRevision{};
};
PreparedQuaderBundleModelRemoval prepareQuaderBundleModelRemoval(
    const std::filesystem::path& root, std::span<const std::string> modelIds);
void validateQuaderBundleModelRemoval(const std::filesystem::path& root,
                                     const PreparedQuaderBundleModelRemoval& prepared);
// Remove only retired generations whose member identities have no authored or
// history references. Bundle bytes, cache files and locations commit together;
// legacy aliases retain source identity provenance without reserving deletion.
std::vector<std::string> pruneUnusedQuaderBundleModels(const std::filesystem::path& root,
                                                      std::span<const std::string> referencedIds);

} // namespace crimson
