#pragma once
#include "resources/TextureMipCache.h"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stop_token>
#include <string>
#include <vector>

namespace crimson {
inline constexpr unsigned AssetPreviewImageVersion=14,AssetPreviewIndexVersion=4;
struct PreviewMaterialFingerprint { std::string id; std::uint64_t fingerprint{}; };
struct PreviewAnimationFingerprint { std::string id; std::uint64_t fingerprint{}; };
struct PreviewDecalFingerprint { std::string id; std::uint64_t fingerprint{}; };
struct PreviewSourceLocation { std::string id; std::filesystem::path file; };
struct CachedAssetPreview {
    std::uint64_t signature{},shaderSignature{},policyFingerprint{};
    std::uint32_t edge{};
    std::vector<std::filesystem::path> files;
    std::vector<std::uint64_t> sourceFingerprints; // Optional exact render-time versions; not serialized.
    std::vector<PreviewMaterialFingerprint> materials;
    std::vector<PreviewAnimationFingerprint> animations;
    std::vector<PreviewSourceLocation> locations;
    std::shared_ptr<const DecodedImage> image;
    std::vector<PreviewDecalFingerprint> decals;
};
// Retain one original spelling for each case-insensitive absolute lexical path.
// Source keys are computed once per input, before sorting the descriptors.
std::vector<std::filesystem::path> uniquePreviewSourceFiles(std::vector<std::filesystem::path>);
// CPU-only persistent preview lookup. The stable descriptor is validated before
// decoding its PNG; it never requires a model, environment bake, or GPU owner.
std::filesystem::path assetPreviewImagePath(const std::filesystem::path& folder,const std::string& id,std::uint64_t signature);
std::filesystem::path assetPreviewIndexPath(const std::filesystem::path& folder,const std::string& id,std::uint32_t edge);
void writeAssetPreviewCache(const std::filesystem::path& root,const std::filesystem::path& folder,const std::string& id,
                           const CachedAssetPreview&,bool publishIndex,std::stop_token = {});
CachedAssetPreview readAssetPreviewCache(const std::filesystem::path& root,const std::filesystem::path& folder,const std::string& id,
                                        std::uint32_t edge,std::uint64_t shaderSignature,std::uint64_t policyFingerprint,std::stop_token = {});
} // namespace crimson
