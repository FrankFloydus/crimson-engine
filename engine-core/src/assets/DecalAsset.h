#pragma once
#include "math/Math.h"
#include "resources/AssetRef.h"
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace crimson {
struct MaterialAsset;
struct DecalAsset {
    std::string id,name{"New Decal"};
    AssetRef<MaterialAsset> material;
    // Unsaved v1 conversion/new material. Never serialized or edited here;
    // an independently saved material always supersedes this seed.
    std::shared_ptr<const MaterialAsset> stagedMaterial;
    bool operator==(const DecalAsset& other) const {return id==other.id&&name==other.name&&material==other.material;}
};
struct DecalAssetEntry {std::string id,name;};
DecalAsset defaultDecalAsset(const std::string& name="New Decal");
void validateDecalAsset(const DecalAsset&);
void canonicalizeDecalAsset(DecalAsset&);
std::vector<std::string> decalMaterialReferences(const DecalAsset&);
std::shared_ptr<const MaterialAsset> stagedDecalMaterial(const std::filesystem::path&,const DecalAsset&);
std::filesystem::path decalAssetPath(const std::filesystem::path&,const std::string& id);
std::string serializeDecalAsset(const DecalAsset&);
DecalAsset deserializeDecalAsset(std::string_view,const std::string& expectedId={});
DecalAsset loadDecalAsset(const std::filesystem::path&,const std::string& id);
void saveDecalAsset(const std::filesystem::path&,const DecalAsset&);
DecalAsset createDecalAsset(const std::filesystem::path&,const std::string& name="New Decal");
DecalAsset saveDecalAssetAs(const std::filesystem::path&,const DecalAsset&,const std::string& name={});
std::vector<DecalAssetEntry> scanDecalAssets(const std::filesystem::path&,std::vector<std::string>* diagnostics=nullptr);
}
