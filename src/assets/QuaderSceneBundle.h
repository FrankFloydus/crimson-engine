#pragma once

#include "ContentStorage.h"
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
std::filesystem::path defaultQuaderSceneBundlePath(const std::filesystem::path& root,
                                                 const QuaderSceneBundle& bundle);
// Called after storage recovery/UUID conversion, before stores and watchers.
void initializeQuaderSceneBundles(const std::filesystem::path& root);

} // namespace crimson
