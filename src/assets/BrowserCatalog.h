#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace crimson {

enum class AssetKind { Texture, Environment, Model, Scene, Material };
struct AssetEntry { std::string id, name; AssetKind kind{AssetKind::Texture}; };
struct BrowserFolder { std::string id, parent, name; };
struct BrowserItem {
    std::string id, name;
    AssetKind kind{AssetKind::Texture};
    bool folder{false};
};

// Browser organization is independent of managed package/file identity. Moving
// or renaming a card never changes scene, material, or texture asset references.
class BrowserCatalog {
public:
    explicit BrowserCatalog(std::filesystem::path projectRoot);
    void synchronize(std::span<const AssetEntry> assets);
    std::vector<BrowserItem> items(const std::string& folder = {}) const;
    std::vector<BrowserFolder> breadcrumbs(const std::string& folder) const;
    bool isFolder(const std::string& id) const;
    bool contains(const std::string& id) const;
    std::string parent(const std::string& id) const;
    std::string name(const std::string& id) const;
    std::string createFolder(const std::string& parent, std::string name);
    void rename(const std::string& id, std::string name);
    void move(std::span<const std::string> ids, const std::string& destination);
    std::vector<std::string> descendants(const std::string& folder) const;
    void erase(std::span<const std::string> ids);
    std::string serializeWithout(std::span<const std::string> ids) const;
    BrowserCatalog prepareErase(std::span<const std::string> ids) const;
    const std::string& status() const { return status_; }
private:
    struct Presentation { std::string folder, name; };
    std::filesystem::path file_;
    std::unordered_map<std::string,AssetEntry> assets_;
    std::unordered_map<std::string,BrowserFolder> folders_;
    std::unordered_map<std::string,Presentation> presentation_;
    std::string status_;
    bool writable_{true};
    void save() const;
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
