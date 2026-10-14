#pragma once
#include "MaterialPreviewNavigation.h"
#include "assets/MaterialPreviewGeometry.h"
#include "assets/MaterialAsset.h"
#include "crimson-renderer/Renderer.h"
#include "resources/ResourceManager.h"
namespace crimson {
class AssetLibrary;
class MaterialPreview {
public:
    void environmentMenu(AssetLibrary&);
    void draw(AssetLibrary&,Renderer&,ResourceManager&,const MaterialAsset&,double dt);
    void cancelInteraction() { gesture_=0; }
    void release(Renderer&);
private:
    MaterialPreviewNavigation navigation_;
    MaterialPreviewMesh mesh_{MaterialPreviewMesh::Sphere};
    EnvironmentSettings environment_;
    bool showBackground_{true};
    int gesture_{};
    ResourceHandle<PreparedEnvironment> environmentHandle_;
    std::string requestedEnvironment_,status_,definitionKey_;
    std::uint64_t environmentRevision_{};
    std::uint16_t displayedTarget_{UINT16_MAX};
    struct Prepared {MaterialAsset material;ModelMaterialTextures bindings;};
    std::shared_ptr<const Prepared> current_;
};
}
