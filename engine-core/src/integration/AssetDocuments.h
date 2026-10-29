#pragma once

#include <crimson/renderer/forward/MaterialGraph.h>
#include <crimson/renderer/forward/BlendMaterial.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <filesystem>
#include <span>

class QMainWindow;
class QWidget;
class QImage;
namespace crimson {
enum class AssetKind;
class MaterialDocument;
struct MaterialAsset;
class RagdollDocument;
class AnimationDocument;
class ParticleEffectDocument;
class DecalDocument;
class QtDocumentProperties;
namespace integration {
class LevelSession;
enum class MaterialPreviewMode { TwoD, ThreeD };
struct BlendMaterialPreviewSelection {
    std::size_t layer{};
    std::optional<BlendMaterialChannel> channel;
    bool operator==(const BlendMaterialPreviewSelection&)const=default;
};
// Independent Engine asset documents hosted by Quader's existing QApplication.
// Domain controllers retain validation, history, conflicts and publication;
// these windows only adapt their authored snapshots to retained Qt controls.
class AssetDocuments final {
public:
    AssetDocuments(QMainWindow&,LevelSession&,std::function<void()> requestFrame);
    ~AssetDocuments();
    AssetDocuments(const AssetDocuments&)=delete;
    AssetDocuments& operator=(const AssetDocuments&)=delete;
    bool open(AssetKind,const std::string& id);
    bool openCollisionLayers();
    void update();
    void filesChanged(std::span<const std::filesystem::path>);
    bool confirmClose();
    bool finishActive();
    bool undoActive();
    bool redoActive();
    QMainWindow* window(AssetKind) const;
    MaterialDocument* materialDocument(const std::string& id) const;
    std::optional<MaterialGraphPreviewTarget> materialPreviewTarget(const std::string& id) const;
    std::optional<BlendMaterialPreviewSelection> blendMaterialPreview(const std::string& id) const;
    bool setBlendMaterialPreview(const std::string& id,BlendMaterialPreviewSelection);
    std::shared_ptr<const MaterialAsset> materialPreviewMaterial(const std::string& id);
    MaterialPreviewMode materialPreviewMode(const std::string& id) const;
    bool setMaterialPreviewMode(const std::string& id,MaterialPreviewMode);
    bool materialNodeImageSupported(const std::string& id,MaterialGraphPreviewTarget);
    QImage materialNodeImage(const std::string& id,MaterialGraphPreviewTarget,unsigned edge=256);
    RagdollDocument* ragdollDocument(const std::string& id) const;
    AnimationDocument* animationDocument(const std::string& id) const;
    ParticleEffectDocument* particleDocument(const std::string& id) const;
    DecalDocument* decalDocument(const std::string& id) const;
    QtDocumentProperties* properties(AssetKind) const;
    using DocumentOpened=std::function<void(AssetKind,const std::string&,QMainWindow&)>;
    using DocumentClosing=std::function<void(AssetKind,const std::string&)>;
    void setDocumentPreviewCallbacks(DocumentOpened,DocumentClosing);
    MaterialDocument* activeMaterialDocument() const;
    QMainWindow* activeMaterialWindow() const;
    // A null publication precedes every active document/window destruction.
    void setActiveMaterialChanged(std::function<void(MaterialDocument*,QMainWindow*)>);
    // The existing renderer host may install a composed preview without
    // transferring document or application ownership to that QWidget.
    bool setPreview(AssetKind,const std::string& id,QWidget*);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
}
