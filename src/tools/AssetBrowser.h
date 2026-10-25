#pragma once

#include "Tools.h"
#include "AssetBrowserFileDrop.h"
#include "assets/BrowserCatalog.h"
#include <array>
#include <vector>

namespace crimson {
class AssetBrowserPanel {
    friend struct EditorPerformanceProbe;
public:
    BrowserActions draw(AssetLibrary& assets, SDL_Window* window, int previewSize = 128, Renderer* renderer = nullptr);
    void handleExternalDrop(const SDL_Event&,AssetLibrary&,bool enabled=true,bool blocked=false);
    std::uint32_t thumbnailDemand() const { return thumbnailDemand_; }
private:
    std::uint32_t thumbnailDemand_{128};
    AssetBrowserFileDrop externalDrop_;
    const AssetLibrary* library_{};
    std::string folder_, renameId_, message_, deferredClick_, dragPrimary_;
    std::array<char,512> renameText_{};
    std::array<char,512> searchText_{};
    BrowserAssetFilter filter_{BrowserAssetFilter::All};
    std::uint64_t displayedRevision_{};
    std::string displayedFolder_, displayedSearch_;
    BrowserAssetFilter displayedFilter_{BrowserAssetFilter::All};
    std::vector<BrowserItem> displayedItems_;
    std::vector<std::string> visibleIds_;
    std::vector<BrowserFolder> breadcrumbs_;
    std::vector<std::string> dragIds_;
    std::vector<char> dragPayload_;
    bool focusRename_{}, dragModelsBatch_{}, dragTexturesBatch_{}, deferredControl_{}, deferredShift_{};
    void beginRename(const std::string& id, const std::string& name);
};
} // namespace crimson
