#pragma once
#include "math/Math.h"
#include "resources/AssetRef.h"
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace crimson {
struct DecodedImage;
enum class DecalTextureChannel : unsigned char { R,G,B,A };
struct DecalAsset {
    std::string id,name{"New Decal"};
    bool colorEnabled{true};
    AssetRef<DecodedImage> colorTexture;
    Vec3 color{1,1,1};
    float colorStrength{1};
    AssetRef<DecodedImage> coverageTexture;
    DecalTextureChannel coverageChannel{DecalTextureChannel::R};
    bool normalEnabled{false};
    AssetRef<DecodedImage> normalTexture;
    float normalStrength{1}; // Channel coverage; a missing map always contributes zero.
    bool roughnessEnabled{false};
    AssetRef<DecodedImage> roughnessTexture;
    DecalTextureChannel roughnessChannel{DecalTextureChannel::R};
    float roughness{1},roughnessStrength{1};
    bool operator==(const DecalAsset&) const = default;
};
struct DecalAssetEntry {std::string id,name;};
DecalAsset defaultDecalAsset(const std::string& name="New Decal");
void validateDecalAsset(const DecalAsset&);
void canonicalizeDecalAsset(DecalAsset&);
std::vector<std::string> decalTextureReferences(const DecalAsset&); // Includes disabled authored maps.
// Empty explicit replacements disable affected channels; losing the shared
// coverage map must never turn its mask into constant-one coverage.
bool rewriteDecalTextureReferences(DecalAsset&,const std::function<std::string(const std::string&,bool normal)>&);
std::filesystem::path decalAssetPath(const std::filesystem::path&,const std::string& id);
std::string serializeDecalAsset(const DecalAsset&);
DecalAsset deserializeDecalAsset(std::string_view,const std::string& expectedId={});
DecalAsset loadDecalAsset(const std::filesystem::path&,const std::string& id);
void saveDecalAsset(const std::filesystem::path&,const DecalAsset&);
DecalAsset createDecalAsset(const std::filesystem::path&,const std::string& name="New Decal");
DecalAsset saveDecalAssetAs(const std::filesystem::path&,const DecalAsset&,const std::string& name={});
std::vector<DecalAssetEntry> scanDecalAssets(const std::filesystem::path&,std::vector<std::string>* diagnostics=nullptr);
}
