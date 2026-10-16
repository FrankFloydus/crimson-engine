#include "MaterialPreview.h"
#include "assets/AssetLibrary.h"
#include "assets/ContentStorage.h"
#include "assets/MaterialGraphReferences.h"
#include "crimson-renderer/Environment.h"
#include "crimson-renderer/ImGuiBgfx.h"
#include "tools/Tools.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace crimson {
void MaterialPreview::setNodePreview(std::optional<MaterialGraphPreviewTarget> target) {
    if(nodePreview_==target)return;
    if(!target || !nodePreview_ || target->node!=nodePreview_->node){grungeCurrent_={};grungeNode_=0;}
    nodePreview_=target;nodeDefinitionKey_.clear();
    if(!target)nodeCurrent_.reset();
}
void MaterialPreview::environmentMenu(AssetLibrary& assets) {
    ImGui::PushItemWidth(260);
    ImGui::Checkbox("Environment lighting",&environment_.enabled);
    ImGui::TextUnformatted("HDRI");
    drawEnvironmentImageField(environment_,assets);
    ImGui::SliderFloat("Intensity",&environment_.intensity,0,10,"%.2f");
    ImGui::SliderFloat("Rotation",&environment_.rotation,-180,180,"%.1f deg");
    drawToneMappingControl(environment_.toneMapping);
    drawGlowControls(environment_.glow);
    ImGui::Checkbox("Show background",&showBackground_);
    if(ImGui::MenuItem("Reset environment")){environment_=EnvironmentSettings{};showBackground_=true;}
    ImGui::PopItemWidth();
}
void MaterialPreview::draw(AssetLibrary& assets,Renderer& renderer,ResourceManager& resources,const MaterialAsset& material,double dt) {
    status_.clear();
    if(current_ && current_->material.id!=material.id) {
        current_.reset();nodeCurrent_.reset();nodePreview_.reset();nodeDefinitionKey_.clear();definitionKey_.clear();navigation_.reset();mesh_=MaterialPreviewMesh::Sphere;materialContext_={};cancelInteraction();
    }
    if(std::isfinite(dt)&&dt>0)materialContext_.gameSeconds+=std::min(dt,1.0);
    materialContext_.realSeconds=ImGui::GetTime();
    if(grungeDocumentId_!=material.id){grungeCurrent_={};grungeNode_=0;grungeDocumentId_=material.id;}
    if(nodePreview_) {
        const auto* node=findMaterialNode(material.graph,nodePreview_->node);
        const auto outputs=node?materialNodeOutputs(material.graph,*node):std::vector<MaterialPinDefinition>{};
        if(!node || std::none_of(outputs.begin(),outputs.end(),[&](const auto& pin){return pin.pin==nodePreview_->pin;}))setNodePreview({});
        else if(node->kind==MaterialNodeKind::Grunge) {
            cancelInteraction();
            if(grungeNode_!=node->id){grungeCurrent_={};grungeNode_=node->id;}
            try {
                auto texture=assets.grungePreview(node->grunge,1024);status_=texture.diagnostic;
                if(texture.sourceReady && bgfx::isValid(texture.handle))grungeCurrent_=std::move(texture);
                else if(status_.empty())status_="Generating texture...";
            } catch(const std::exception& error){status_=error.what();}
            if(!status_.empty())ImGui::TextWrapped("%s",status_.c_str());
            const auto available=ImGui::GetContentRegionAvail();const float edge=std::min(available.x,available.y);
            if(edge>0) {
                const auto origin=ImGui::GetCursorScreenPos();
                ImGui::SetCursorScreenPos({origin.x+(available.x-edge)*.5f,origin.y+(available.y-edge)*.5f});
                if(bgfx::isValid(grungeCurrent_.handle))ImGui::Image(ImTextureRef(ImGuiBgfx::textureId(grungeCurrent_.handle,true)),{edge,edge},
                    {0,0},{static_cast<float>(node->grunge.tiling),static_cast<float>(node->grunge.tiling)});
                else ImGui::Dummy({edge,edge});
                const auto& io=ImGui::GetIO();
                if(!io.WantTextInput && !ImGui::IsAnyItemActive() && !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopup) &&
                    ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyCtrl && !io.KeyShift && !io.KeyAlt)selectMaterial_=true;
            }
            return;
        }
    }
    const auto available=ImGui::GetContentRegionAvail(),framebufferScale=ImGui::GetWindowViewport()->FramebufferScale;
    const auto textureEdge=static_cast<std::uint32_t>(std::clamp(std::ceil(std::max(available.x*framebufferScale.x,available.y*framebufferScale.y)),1.f,16384.f));
    const auto source=assets.references().resolveEnvironment(environment_.map);
    const auto requestId=source.empty()?std::string(StudioEnvironmentId):source;
    const auto storageRevision=contentStorageRevision(assets.projectRoot());
    if(requestedEnvironment_!=requestId || !environmentHandle_ || storageRevision_!=storageRevision) {
        storageRevision_=storageRevision;
        requestedEnvironment_=requestId;environmentRevision_=0;
        ResourceRequestOptions options;options.variant="ibl";options.priority=ResourcePriority::Selected;
        const auto root=assets.projectRoot();
        if(!source.empty())options.dependencies={resolveAssetPath(root,source)};
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
        auto bindings=assets.materialPreviewBindings(material,textureEdge);
        if(!bindings.diagnostic.empty())diagnose(bindings.diagnostic);
        if(materialPreviewPacketReady(bindings) && (!bindings.graphPending || !current_ || !current_->bindings.graph)) {
            auto key=material.surface.graph?std::to_string(materialGraphRenderHash(material.surface)):serializeMaterialAsset(material);
            if(!current_ || definitionKey_!=key || !std::equal(current_->bindings.handles.begin(),current_->bindings.handles.end(),bindings.handles.begin(),[](auto a,auto b){return a.idx==b.idx;}) ||
                current_->bindings.flags!=bindings.flags || current_->bindings.signatures!=bindings.signatures ||
                current_->bindings.sourceFingerprints!=bindings.sourceFingerprints ||
                (current_->bindings.graph?current_->bindings.graph->signature:0)!=(bindings.graph?bindings.graph->signature:0)) {
                current_=std::make_shared<Prepared>(Prepared{material,std::move(bindings)});definitionKey_=std::move(key);
            }
        } else if(!bindings.graphPending)diagnose("Preparing material textures...");
    } catch(const std::exception& error) {diagnose(error.what());}
    if(nodePreview_) {
        const auto* node=findMaterialNode(material.graph,nodePreview_->node);
        const auto outputs=node?materialNodeOutputs(material.graph,*node):std::vector<MaterialPinDefinition>{};
        if(!node || std::none_of(outputs.begin(),outputs.end(),[&](const auto& pin){return pin.pin==nodePreview_->pin;}))setNodePreview({});
    }
    if(nodePreview_)try {
        if(!nodePrograms_)nodePrograms_=assets.createMaterialNodePreviewPrograms();
        if(!nodePrograms_)throw std::runtime_error("Node preview shader compilation is unavailable.");
        auto bindings=assets.materialNodePreviewBindings(*nodePrograms_,material,*nodePreview_,textureEdge);
        if(!bindings.diagnostic.empty())diagnose(bindings.diagnostic);
        if(materialPreviewPacketReady(bindings) && !bindings.graphPending && bindings.graph) {
            const auto key=std::to_string(materialGraphRenderHash(material.surface))+":"+std::to_string(nodePreview_->node)+":"+std::to_string(static_cast<unsigned>(nodePreview_->pin));
            if(!nodeCurrent_ || nodeDefinitionKey_!=key || nodeCurrent_->bindings.graph->signature!=bindings.graph->signature) {
                nodeCurrent_=std::make_shared<Prepared>(Prepared{material,std::move(bindings)});nodeDefinitionKey_=key;
            }
        }
        nodePrograms_->trim();
    } catch(const std::exception& error) {diagnose(error.what());}
    const auto displayed=nodePreview_ && nodeCurrent_?nodeCurrent_:current_;
    const bool particlePreview=displayed && usesParticleMaterialPreview(mesh_,displayed->bindings);
    constexpr const char* names[]{"Sphere","Cube","Rounded Cube","Cylinder","Plane","Sprite"};
    auto selected=static_cast<int>(mesh_);
    ImGui::SetNextItemWidth(std::min(180.f,ImGui::GetContentRegionAvail().x));
    if(ImGui::Combo("##Preview mesh",&selected,names,6))mesh_=static_cast<MaterialPreviewMesh>(selected);
    ImGui::SameLine();if(ImGui::Button("Reset view")){navigation_.reset();cancelInteraction();}
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))ImGui::SetTooltip("MMB tumble · Shift+MMB pan · Ctrl+MMB rotate environment\nWheel zoom · Double-click MMB resets view and environment rotation");
    if(!status_.empty())ImGui::TextWrapped("%s",status_.c_str());
    if(!renderer.glowStatus().empty())ImGui::TextWrapped("%s",renderer.glowStatus().c_str());
    const auto size=ImGui::GetContentRegionAvail();
    if(size.x<=0 || size.y<=0){cancelInteraction();return;}
    const auto scale=ImGui::GetWindowViewport()->FramebufferScale;
    try {
        renderer.resizeMaterialPreview(static_cast<std::uint32_t>(std::max(1.f,size.x*scale.x)),static_cast<std::uint32_t>(std::max(1.f,size.y*scale.y)));
    } catch(const std::exception& error){ImGui::TextWrapped("Preview preparation failed: %s",error.what());cancelInteraction();return;}
    const auto texture=renderer.materialPreviewTexture();
    // The main tools view precedes the preview views. Do not sample a freshly
    // allocated target until its first submitted preview frame has completed.
    if(bgfx::isValid(texture) && texture.idx==displayedTarget_ && displayed && environmentRevision_)ImGui::Image(ImTextureRef(ImGuiBgfx::textureId(texture)),size);
    else ImGui::Dummy(size);
    displayedTarget_=texture.idx;
    auto& io=ImGui::GetIO();
    const bool ownsInput=!io.WantTextInput && !ImGui::IsAnyItemActive() &&
        !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopup) && !ImGui::IsKeyPressed(ImGuiKey_Escape,false);
    if(ownsInput && ImGui::IsItemHovered()) {
        if(ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyCtrl && !io.KeyShift && !io.KeyAlt)selectMaterial_=true;
        if(ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Middle)) {navigation_.reset();environment_.rotation=0;cancelInteraction();}
        else if(ImGui::IsMouseClicked(ImGuiMouseButton_Middle)){ImGui::SetWindowFocus();gesture_=io.KeyCtrl?3:io.KeyShift?2:particlePreview?0:1;}
        if(io.MouseWheel)navigation_.zoom(io.MouseWheel);
    }
    if(!ownsInput || !ImGui::IsMouseDown(ImGuiMouseButton_Middle) || !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))cancelInteraction();
    const Vec2 delta{io.MouseDelta.x*scale.x,io.MouseDelta.y*scale.y};
    if(gesture_==1)navigation_.tumble(delta);
    if(gesture_==2)navigation_.translate(delta);
    if(gesture_==3)environment_.rotation=MaterialPreviewNavigation::rotateEnvironment(environment_.rotation,delta[0]);
    if(!displayed)return;
    SceneSettings settings;settings.camera.position={navigation_.pan[0],navigation_.pan[1],navigation_.distance};
    settings.camera.target={navigation_.pan[0],navigation_.pan[1],0};settings.camera.nearPlane=.1f;settings.camera.farPlane=30;
    settings.exposure=1;settings.environment=environment_;
    renderer.renderMaterialPreview(settings,displayed->material,displayed->bindings,mesh_,navigation_.objectTransform(),showBackground_ && environment_.enabled,displayed,materialContext_);
}
void MaterialPreview::release(Renderer& renderer) {
    cancelInteraction();renderer.releaseMaterialPreview();current_.reset();nodeCurrent_.reset();nodePrograms_.reset();nodePreview_.reset();nodeDefinitionKey_.clear();definitionKey_.clear();environmentHandle_={};
    requestedEnvironment_.clear();environmentRevision_=0;displayedTarget_=UINT16_MAX;navigation_.reset();mesh_=MaterialPreviewMesh::Sphere;materialContext_={};
    grungeCurrent_={};grungeDocumentId_.clear();grungeNode_=0;
}
}
