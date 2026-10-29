#pragma once
#include "assets/BrowserCatalog.h"
#include "assets/ContentStorage.h"
#include "resources/AssetId.h"
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace crimson {
// A checked location snapshot distinguishes a stale, removed source from a
// malformed current asset. Observers must not resolve removed UUIDs anew.
class AssetSourceSnapshot {
public:
    explicit AssetSourceSnapshot(const std::filesystem::path& root):root_(root) {
        for(const auto& source:storedAssetPaths(root))sources_.emplace(source.id,source.file);
    }
    std::optional<std::filesystem::path> find(const std::string& id) const {
        if(const auto found=sources_.find(id);found!=sources_.end())return found->second;
        if(isAssetUuid(id))return std::nullopt;
        return resolveAssetPath(root_,id); // Legacy/private paths remain checked.
    }
    bool matches(const std::string& id,const std::filesystem::path& file) const {
        const auto source=find(id);
        return source && source->lexically_normal()==file.lexically_normal();
    }
private:
    std::filesystem::path root_;
    std::unordered_map<std::string,std::filesystem::path> sources_;
};

struct AssetCatalogSource {
    std::string id;
    AssetKind kind{AssetKind::Texture};
    std::filesystem::path file;
};
struct AssetCatalogChanges {
    std::vector<AssetCatalogSource> added,removed;
};
// Retain literal source locations with catalog residency. A deletion may remove
// its index entries before the next frame observes the catalog change.
class AssetCatalogSnapshot {
public:
    AssetCatalogChanges update(const std::filesystem::path& root,std::span<const AssetEntry> catalog) {
        std::unordered_map<std::string,AssetCatalogSource> next;
        for(const auto& asset:catalog) {
            AssetCatalogSource source{asset.id,asset.kind,{}};
            if(!isEngineAssetId(asset.id))source.file=resolveAssetPath(root,asset.id);
            next.emplace(asset.id,std::move(source));
        }
        AssetCatalogChanges changes;
        for(const auto& [id,source]:next)if(!sources_.contains(id))changes.added.push_back(source);
        for(const auto& [id,source]:sources_)if(!next.contains(id))changes.removed.push_back(source);
        sources_=std::move(next);
        return changes;
    }
private:
    std::unordered_map<std::string,AssetCatalogSource> sources_;
};
}
