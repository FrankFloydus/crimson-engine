#include "MaterialPreview.h"
#include "assets/AssetLibrary.h"
#include "crimson-renderer/Environment.h"
#include "crimson-renderer/ImGuiBgfx.h"
#include "tools/Tools.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace crimson {
void MaterialPreview::environmentMenu(AssetLibrary& assets) {
    ImGui::PushItemWidth(260);
    ImGui::Checkbox("Enabled",&environment_.enabled);
    ImGui::TextUnformatted("HDRI");
    drawEnvironmentImageField(environment_,assets);
    ImGui::SliderFloat("Intensity",&environment_.intensity,0,10,"%.2f");
    ImGui::SliderFloat("Rotation",&environment_.rotation,-180,180,"%.1f deg");
    ImGui::Checkbox("Show background",&showBackground_);
    if(ImGui::MenuItem("Reset environment")){environment_=EnvironmentSettings{};showBackground_=true;}
    ImGui::PopItemWidth();
}
void MaterialPreview::draw(AssetLibrary& assets,Renderer& renderer,ResourceManager& resources,const MaterialAsset& material,double) {
    status_.clear();
    if(current_ && current_->material.id!=material.id) {
        current_.reset();definitionKey_.clear();navigation_.reset();mesh_=MaterialPreviewMesh::Sphere;cancelInteraction();
    }
    const auto source=assets.references().resolveEnvironment(environment_.map);
    const auto requestId=source.empty()?std::string(StudioEnvironmentId):source;
    if(requestedEnvironment_!=requestId || !environmentHandle_) {
        requestedEnvironment_=requestId;environmentRevision_=0;
        ResourceRequestOptions options;options.variant="ibl";options.priority=ResourcePriority::Preview;
        const auto root=assets.projectRoot();
        if(!source.empty())options.dependencies={root/pathFromUtf8(source)};
        environmentHandle_=resources.request<PreparedEnvironment>(AssetRef<PreparedEnvironment>{requestId},[root,source](std::stop_token token) {
            if(token.stop_requested())throw std::runtime_error("Material environment preparation canceled.");
            const auto bake=prepareEnvironmentCpu(root,source);
            std::size_t bytes=bake->data.diffuse.rgba.size()*sizeof(float)+bake->data.brdf.size()*sizeof(float);
            for(const auto& level:bake->data.specular)bytes+=level.rgba.size()*sizeof(float);
            return ResourceLoadResult<PreparedEnvironment>{bake,bytes,bake->key,{}};
        },std::move(options));
    }
    if(const auto prepared=environmentHandle_.snapshot();prepared && prepared.revision()!=environmentRevision_) {
        if(renderer.publishMaterialPreviewEnvironment(*prepared))environmentRevision_=prepared.revision();
        else status_="Could not prepare the preview environment; the previous environment is retained.";
    }
    if(environmentHandle_.state()==ResourceState::Failed)status_="Preview environment: "+environmentHandle_.diagnostic();
    else if(!environmentRevision_)status_="Preparing preview environment...";
    if(!environment_.map.empty() && source.empty())status_="The assigned HDRI is unavailable; using the built-in studio.";
    const auto diagnose=[&](const std::string& message){if(!status_.empty())status_+='\n';status_+=message;};
    try {
        auto bindings=assets.materialPreviewBindings(material);
        if(bindings.sourceReady && bindings.complete) {
            auto key=serializeMaterialAsset(material);
            if(!current_ || definitionKey_!=key || !std::equal(current_->bindings.handles.begin(),current_->bindings.handles.end(),bindings.handles.begin(),[](auto a,auto b){return a.idx==b.idx;}) ||
                current_->bindings.flags!=bindings.flags || current_->bindings.signatures!=bindings.signatures ||
                current_->bindings.sourceFingerprints!=bindings.sourceFingerprints) {
                current_=std::make_shared<Prepared>(Prepared{material,std::move(bindings)});definitionKey_=std::move(key);
            }
        } else diagnose("Preparing material textures...");
    } catch(const std::exception& error) {diagnose(error.what());}
    constexpr const char* names[]{"Sphere","Cube","Rounded Cube","Cylinder","Plane"};
    auto selected=static_cast<int>(mesh_);
    ImGui::SetNextItemWidth(std::min(180.f,ImGui::GetContentRegionAvail().x));
    if(ImGui::Combo("##Preview mesh",&selected,names,5))mesh_=static_cast<MaterialPreviewMesh>(selected);
    ImGui::SameLine();if(ImGui::Button("Reset view")){navigation_.reset();cancelInteraction();}
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))ImGui::SetTooltip("MMB tumble · Shift+MMB pan · Ctrl+MMB rotate environment\nWheel zoom · Double-click MMB resets view and environment rotation");
    if(!status_.empty())ImGui::TextWrapped("%s",status_.c_str());
    const auto size=ImGui::GetContentRegionAvail();
    if(size.x<=0 || size.y<=0){cancelInteraction();return;}
    const auto scale=ImGui::GetWindowViewport()->FramebufferScale;
    try {
        renderer.resizeMaterialPreview(static_cast<std::uint32_t>(std::max(1.f,size.x*scale.x)),static_cast<std::uint32_t>(std::max(1.f,size.y*scale.y)));
    } catch(const std::exception& error){ImGui::TextWrapped("Preview preparation failed: %s",error.what());cancelInteraction();return;}
    const auto texture=renderer.materialPreviewTexture();
    // The main tools view precedes the preview views. Do not sample a freshly
    // allocated target until its first submitted preview frame has completed.
    if(bgfx::isValid(texture) && texture.idx==displayedTarget_ && current_ && environmentRevision_)ImGui::Image(ImTextureRef(ImGuiBgfx::textureId(texture)),size);
    else ImGui::Dummy(size);
    displayedTarget_=texture.idx;
    auto& io=ImGui::GetIO();
    const bool ownsInput=!io.WantTextInput && !ImGui::IsAnyItemActive() &&
        !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopup) && !ImGui::IsKeyPressed(ImGuiKey_Escape,false);
    if(ownsInput && ImGui::IsItemHovered()) {
        if(ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Middle)) {navigation_.reset();environment_.rotation=0;cancelInteraction();}
        else if(ImGui::IsMouseClicked(ImGuiMouseButton_Middle)){ImGui::SetWindowFocus();gesture_=io.KeyCtrl?3:io.KeyShift?2:1;}
        if(io.MouseWheel)navigation_.zoom(io.MouseWheel);
    }
    if(!ownsInput || !ImGui::IsMouseDown(ImGuiMouseButton_Middle) || !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))cancelInteraction();
    const Vec2 delta{io.MouseDelta.x*scale.x,io.MouseDelta.y*scale.y};
    if(gesture_==1)navigation_.tumble(delta);
    if(gesture_==2)navigation_.translate(delta);
    if(gesture_==3)environment_.rotation=MaterialPreviewNavigation::rotateEnvironment(environment_.rotation,delta[0]);
    if(!current_)return;
    SceneSettings settings;settings.camera.position={navigation_.pan[0],navigation_.pan[1],navigation_.distance};
    settings.camera.target={navigation_.pan[0],navigation_.pan[1],0};settings.camera.nearPlane=.1f;settings.camera.farPlane=30;
    settings.exposure=1;settings.environment=environment_;
    renderer.renderMaterialPreview(settings,current_->material,current_->bindings,mesh_,navigation_.objectTransform(),showBackground_ && environment_.enabled,current_);
}
void MaterialPreview::release(Renderer& renderer) {
    cancelInteraction();renderer.releaseMaterialPreview();current_.reset();definitionKey_.clear();environmentHandle_={};
    requestedEnvironment_.clear();environmentRevision_=0;displayedTarget_=UINT16_MAX;navigation_.reset();mesh_=MaterialPreviewMesh::Sphere;
}
}
