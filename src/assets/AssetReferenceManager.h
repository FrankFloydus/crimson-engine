#pragma once

#include "BrowserCatalog.h"
#include "AssetTransaction.h"
#include "resources/AssetId.h"
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace crimson {

struct AssetUsage {
    std::string targetId, ownerId, ownerName, slot;
    bool active{}, historical{};
    bool editorState{}; // Archived settings/session data are not usable authored owners.
};
struct SceneImpact { std::string id, name; std::size_t entities{}; };
struct AssetDeletionPlan {
    uint64_t revision{};
    std::vector<std::string> selectedIds, assetIds, folderIds;
    std::vector<AssetUsage> usages;
    // Surviving persisted owners, once per removed asset/owner. Live scene edits,
    // undo history and editor bookmarks remain in usages for impact handling.
    std::vector<AssetUsage> warningUsages;
    std::vector<SceneImpact> scenes;
    bool clearsActiveHistory{};
    std::string error;
    bool needsReferenceWarning() const { return !warningUsages.empty(); }
};

// Authored references and persistent replacement policy. This is deliberately
// separate from runtime resource pins, CPU/GPU caches and memory accounting.
// Deleted UUIDs remain reserved after their physical location is unregistered;
// surviving aliases retain source-kind provenance for replacement validation.
class AssetReferenceManager {
public:
    explicit AssetReferenceManager(std::filesystem::path projectRoot);
    std::string resolve(const std::string& id) const;
    std::string resolveMaterial(const std::string& id) const;
    std::string resolveModelMaterial(const std::string& id) const;
    std::string resolveTexture(const std::string& id, bool normal=false) const;
    std::string resolveEnvironment(const std::string& id) const;
    std::string resolveParticle(const std::string& id) const;
    std::string resolveDecal(const std::string& id) const;
    std::string resolveDecalMaterial(const std::string& id) const;
    std::string resolveNavigationAgentProfile(const std::string& id) const;
    std::string resolveGameplayEffect(const std::string& id) const;
    std::string resolveAnimation(const std::string& id) const;
    std::string resolveParticleMaterial(const std::string& id) const;
    std::string resolveParticleTexture(const std::string& id) const;
    bool deleted(const std::string& id) const { return deleted_.contains(id)||deleted_.contains(canonicalAssetId(id)); }
    uint64_t revision() const { return revision_; }
    bool valid() const { return writable_; }
    // Persistent fallbacks are authored owners even when no scene currently
    // uses their deleted source. Return the surviving targets of every chain.
    std::vector<std::string> replacementTargets() const;
    const std::string& status() const { return status_; }
    const std::vector<AssetUsage>& usages() const { return usages_; }
    void setUsages(std::vector<AssetUsage> usages);
    std::vector<AssetUsage> users(const std::string& id) const;
    std::vector<AssetUsage> authoredUsers(const std::string& id,
                                        std::span<const std::string> removedOwners = {}) const;
    void replace(std::span<const AssetEntry> deletedAssets,
                 const std::unordered_map<std::string,std::string>& replacements);
    std::string serialize() const;
    void save() const;
private:
    std::filesystem::path root_;
    std::unordered_set<std::string> deleted_;
    std::unordered_map<std::string,std::string> replacements_;
    std::vector<AssetUsage> usages_;
    std::string status_;
    uint64_t revision_{1};
    bool writable_{true};
};

} // namespace crimson
