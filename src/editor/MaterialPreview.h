#pragma once
#include "MaterialPreviewNavigation.h"
#include "assets/MaterialPreviewGeometry.h"
#include "assets/MaterialAsset.h"
#include "crimson-renderer/Renderer.h"
#include "crimson-renderer/MaterialGraphGpu.h"
#include "resources/ResourceManager.h"
#include "resources/TextureStreamer.h"
namespace crimson {
class AssetLibrary;
enum class MaterialPreviewMode { TwoD, ThreeD };
inline bool materialPreviewPacketReady(const ModelMaterialTextures& bindings) noexcept {
    return bindings.sourceReady&&bindings.drawable();
}
// A pinned diagnostic output can be disconnected from the material Output.
// Its presentation identity must therefore follow its own active branch.
inline std::string materialNodePreviewDefinitionKey(const MaterialAsset& material,MaterialGraphPreviewTarget target) {
    const auto& output=materialOutputNode(material.graph).output;
    return material.id+":"+std::to_string(target.node)+":"+std::to_string(static_cast<unsigned>(target.pin))+":"+
        std::to_string(materialGraphValueHash(material.graph,output.shading,output.blend,target.node));
}
class MaterialPreview {
public:
    void environmentMenu(AssetLibrary&);
    void draw(AssetLibrary&,Renderer&,ResourceManager&,const MaterialAsset&,double dt);
    void setAntiAliasing(AntiAliasing value) { antiAliasing_=value; }
    void setNodePreview(std::optional<MaterialGraphPreviewTarget> target);
    void setMode(MaterialPreviewMode value) { mode_=value;cancelInteraction(); }
    MaterialPreviewMode mode() const { return mode_; }
    void setImageGraph(std::shared_ptr<const MaterialGraph> graph) { imageGraph_=std::move(graph); }
    std::optional<MaterialGraphPreviewTarget> nodePreview() const {return nodePreview_;}
    void cancelInteraction() { gesture_=0; }
    bool takeMaterialSelectionRequest() {const bool value=selectMaterial_;selectMaterial_=false;return value;}
    void release(Renderer&);
private:
    MaterialPreviewNavigation navigation_;
    MaterialPreviewMesh mesh_{MaterialPreviewMesh::Sphere};
    MaterialPreviewMode mode_{MaterialPreviewMode::ThreeD};
    EnvironmentSettings environment_;
    AntiAliasing antiAliasing_{AntiAliasing::Smaa};
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
    std::shared_ptr<const MaterialGraph> imageGraph_,classifiedImageGraph_;
    std::optional<MaterialGraphPreviewTarget> classifiedImageTarget_;
    PreparedTexture imageCurrent_;
    std::string imageDocumentId_;
    bool staticImage_{},displayingImage_{};
};
}
