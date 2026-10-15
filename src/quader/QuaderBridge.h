#pragma once

#include "QuaderImport.h"
#include "scene/Scene.h"
#include <functional>
#include <memory>
#include <span>

namespace crimson {

struct QuaderTarget { EntityId entity{}; QuaderLink link; bool hasImported{}; std::string consumedSaveKey; };
struct QuaderBridgeHooks {
    std::function<bool(const std::string&)> dirtyMaterial;
    std::function<std::string(const std::string&)> resolveMaterial;
    std::function<std::string(const std::string&,bool normal)> resolveTexture;
    std::function<bool(const std::string&)> reservedAsset;
    std::function<std::uint64_t()> assetPolicyRevision;
    // Must install the complete candidate in one document history transaction.
    std::function<bool(EntityId,const QuaderLink&,const QuaderImportResult&)> applyImport;
    // Notify the existing catalog/resource owners after files and scene commit.
    std::function<void(const QuaderImportResult&)> publishAssets;
};
class QuaderBridge {
public:
    explicit QuaderBridge(std::filesystem::path projectRoot);
    ~QuaderBridge();
    QuaderBridge(const QuaderBridge&)=delete;
    QuaderBridge& operator=(const QuaderBridge&)=delete;
    void requestImport(EntityId,const QuaderLink&,std::uint64_t documentGeneration);
    void cancel(EntityId);
    void update(std::span<const QuaderTarget>,std::uint64_t documentGeneration,bool suspended,const QuaderBridgeHooks&);
    bool busy(EntityId) const;
    std::string status(EntityId) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace crimson
