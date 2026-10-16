#pragma once
#include "MaterialPreviewNavigation.h"
#include "assets/MaterialPreviewGeometry.h"
#include "assets/MaterialAsset.h"
#include "crimson-renderer/Renderer.h"
#include "resources/ResourceManager.h"
#include "resources/TextureStreamer.h"
namespace crimson {
class AssetLibrary;
inline bool materialPreviewPacketReady(const ModelMaterialTextures& bindings) noexcept {
    return bindings.sourceReady&&bindings.drawable();
}
class MaterialPreview {
public:
    void environmentMenu(AssetLibrary&);
    void draw(AssetLibrary&,Renderer&,ResourceManager&,const MaterialAsset&,double dt);
    void setNodePreview(std::optional<MaterialGraphPreviewTarget> target);
    std::optional<MaterialGraphPreviewTarget> nodePreview() const {return nodePreview_;}
    void cancelInteraction() { gesture_=0; }
    bool takeMaterialSelectionRequest() {const bool value=selectMaterial_;selectMaterial_=false;return value;}
    void release(Renderer&);
private:
    MaterialPreviewNavigation navigation_;
    MaterialPreviewMesh mesh_{MaterialPreviewMesh::Sphere};
    EnvironmentSettings environment_;
    bool showBackground_{true};
    bool selectMaterial_{};
    int gesture_{};
    ResourceHandle<PreparedEnvironment> environmentHandle_;
    std::string requestedEnvironment_,status_,definitionKey_;
    std::uint64_t environmentRevision_{};
    std::uint64_t storageRevision_{};
    std::uint16_t displayedTarget_{UINT16_MAX};
    struct Prepared {MaterialAsset material;ModelMaterialTextures bindings;};
    std::shared_ptr<const Prepared> current_,nodeCurrent_;
    std::optional<MaterialGraphPreviewTarget> nodePreview_;
    std::unique_ptr<MaterialGraphPrograms> nodePrograms_;
    std::string nodeDefinitionKey_;
    MaterialEvaluationContext materialContext_;
    PreparedTexture grungeCurrent_;
    std::string grungeDocumentId_;
    MaterialNodeId grungeNode_{};
};
}
