#pragma once

#include "assets/AssetReferenceReport.h"
#include "editor/EditorActions.h"
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>

class QMainWindow;
class QWidget;

namespace crimson::integration {
class LevelSession;

// Native browser orchestration over the retained project catalog, immutable
// reference reports and transactional deletion. It owns no device or app loop.
class LevelAssetBrowser final {
public:
    LevelAssetBrowser(QMainWindow&,std::function<void()> requestFrame);
    ~LevelAssetBrowser();
    QWidget* widget() const;
    void bind(LevelSession*);
    void update(std::uint32_t rendererFrame,bool pauseThumbnails=false,int previewSize=128,
        std::span<const std::string> materialPreviews={});
    BrowserActions takeActions();
    bool reveal(const std::string&);
    bool ownsInput() const;
    bool commandPending() const;
    void setPrepareCommand(std::function<bool()>);
    void setNavigateScene(std::function<void(const std::string&,EntityId)>);
    bool requestReferences(const std::string&);
    void cancelReferences();
    std::shared_ptr<const AssetReferenceReport> referenceReport() const;
    bool referencesPending() const;
    bool referencesStale() const;
    bool requestDeletion(const std::vector<std::string>&);
    bool requestDuplicate(const std::string& id,const std::string& folder={});
    using DeletionConfirmation=std::function<bool(const AssetDeletionPlan&,std::unordered_map<std::string,std::string>&)>;
    void setDeletionConfirmation(DeletionConfirmation);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
