#pragma once

#include "ContentStorage.h"

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace crimson {

enum class AssetKind { Texture, Environment, Model, Scene, Material, Particle, NavigationAgentProfile, GameplayEffect, Animation, Decal };
enum class BrowserAssetFilter { All, Materials, Models, Textures, Scenes, Particles, NavigationProfiles, GameplayEffects, Animations, Decals };
struct AssetEntry { std::string id, name; AssetKind kind{AssetKind::Texture}; };
struct BrowserFolder { std::string id, parent, name, path; };
struct BrowserItem {
    std::string id, name;
    AssetKind kind{AssetKind::Texture};
    bool folder{false};
};

// Cards follow physical Content/Engine files and directories. Asset identities
// remain stable when their backing file or package is renamed or moved.
class BrowserCatalog {
public:
    static constexpr const char* ContentFolder="folder:content";
    static constexpr const char* EngineFolder="folder:engine";
    explicit BrowserCatalog(std::filesystem::path projectRoot);
    void synchronize(std::span<const AssetEntry> assets);
    std::vector<BrowserItem> items(const std::string& folder = {}) const;
    // Active search/type filtering includes matching assets below this folder;
    // its direct child folders remain available for navigation.
    std::vector<BrowserItem> items(const std::string& folder,std::string_view search,BrowserAssetFilter filter) const;
    // Borrowed presentation: consume before another query or catalog mutation.
    // Repeated UI frames do not copy, sort, resolve paths or touch the disk.
    const std::vector<BrowserItem>& itemsView(const std::string& folder,std::string_view search={},BrowserAssetFilter filter=BrowserAssetFilter::All) const;
    std::uint64_t revision() const { return revision_; }
    std::vector<BrowserFolder> breadcrumbs(const std::string& folder) const;
    bool isFolder(const std::string& id) const;
    bool contains(const std::string& id) const;
    std::string parent(const std::string& id) const;
    std::string name(const std::string& id) const;
    std::filesystem::path folderPath(const std::string& id) const;
    bool writableFolder(const std::string& id) const;
    bool mutableItem(const std::string& id) const;
    bool deletableItem(const std::string& id) const;
    std::string createFolder(const std::string& parent, std::string name);
    void rename(const std::string& id, std::string name);
    // Imports choose unique filenames in one transaction; ordinary editor moves
    // keep explicit collision errors. First extraction of package images always
    // preserves both the retained source and any existing destination image.
    void move(std::span<const std::string> ids, const std::string& destination, bool uniqueNames=false);
    std::vector<std::string> descendants(const std::string& folder) const;
    void erase(std::span<const std::string> ids);
    std::string serializeWithout(std::span<const std::string> ids) const;
    BrowserCatalog prepareErase(std::span<const std::string> ids) const;
    const std::string& status() const { return status_; }
private:
    struct Presentation { std::string folder, name; };
    std::filesystem::path root_, file_;
    std::unordered_map<std::string,AssetEntry> assets_;
    std::unordered_map<std::string,BrowserFolder> folders_;
    std::unordered_map<std::string,Presentation> presentation_;
    struct ItemMetadata { std::string name, catalogName, filename, identity; bool writable{},deletable{}; };
    std::unordered_map<std::string,ItemMetadata> metadata_;
    std::vector<BrowserItem> orderedItems_;
    std::unordered_map<std::string,std::vector<BrowserItem>> directItems_;
    mutable std::vector<BrowserItem> queryItems_;
    mutable std::string queryFolder_, queryText_;
    mutable BrowserAssetFilter queryFilter_{BrowserAssetFilter::All};
    mutable bool queryValid_{};
    std::uint64_t revision_{};
    std::string status_;
    bool writable_{true};
    void save() const;
    void rebuildViews();
    std::string serialize() const;
    std::filesystem::path itemPath(const std::string& id) const;
    bool packageImage(const std::filesystem::path& file) const;
    void relocate(std::vector<ContentPathMove> moves);
    void validateFolder(const std::string& id) const;
    void validateFolderName(const std::string& parent, const std::string& name, const std::string& except = {}) const;
};

// Selection rules are CPU-only and shared by mouse clicks, keyboard range
// selection, context menus, and drag sources. IDs are never visible row indices.
class BrowserSelection {
public:
    const std::vector<std::string>& ids() const { return ids_; }
    const std::string& primary() const { return primary_; }
    bool contains(const std::string& id) const;
    void clear();
    void set(const std::string& id);
    void click(const std::string& id, std::span<const std::string> visible, bool control, bool shift);
    void beginDrag(const std::string& id);
    void retain(std::span<const std::string> visible);
private:
    std::vector<std::string> ids_;
    std::string primary_, anchor_;
};

} // namespace crimson
