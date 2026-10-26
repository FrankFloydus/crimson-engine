#pragma once
#include "assets/AssetReferenceReport.h"
#include "resources/ResourceManager.h"
#include <array>
#include <stop_token>

namespace crimson {
class AssetLibrary;
class SceneDocument;
struct AssetReferenceNavigation {
    std::string assetId,sceneId;
    std::uint64_t entityId{},documentGeneration{};
};
class AssetReferencesPanel {
public:
    AssetReferencesPanel()=default;
    ~AssetReferencesPanel(){close();}
    AssetReferencesPanel(const AssetReferencesPanel&)=delete;
    AssetReferencesPanel& operator=(const AssetReferencesPanel&)=delete;
    void open(std::string assetId,AssetLibrary&,const SceneDocument*,ResourceManager&);
    void close();
    bool isOpen() const {return open_;}
    AssetReferenceNavigation draw(AssetLibrary&,const SceneDocument*,ResourceManager&);
    // Publication/freshness boundary is separate from drawing for CPU tests.
    void update(const AssetLibrary&,const SceneDocument*);
    std::shared_ptr<const AssetReferenceReport> report() const {return report_;}
    bool stale() const {return stale_;}
    bool loading() const {return bool(request_)&&!report_;}
    const std::string& diagnostic() const {return diagnostic_;}
private:
    void request(AssetLibrary&,const SceneDocument*,ResourceManager&);
    void prepareRows();
    void filterRows();
    friend struct AssetReferencePanelTestAccess;
    struct DisplayRow {std::string name,search;AssetKind kind{};bool known{};};
    bool open_{},focus_{},stale_{};
    std::string target_,diagnostic_,sceneId_;
    std::uint64_t revision_{},documentGeneration_{};
    std::stop_source cancellation_;
    ResourceHandle<AssetReferenceReport> request_;
    std::shared_ptr<const AssetReferenceReport> report_;
    std::array<char,256> search_{};
    std::string filteredQuery_;
    bool filterDirty_{true};
    std::array<std::vector<DisplayRow>,2> displayRows_;
    std::array<std::vector<std::size_t>,2> matches_;
};
}
