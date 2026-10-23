#include "DecalEditor.h"
#include "DocumentEditorMenu.h"
#include "assets/AssetLibrary.h"
#include "TextureAssetField.h"
#include "PropertyReset.h"
#include "editor/DecalPreview.h"
#include "editor/EditorResources.h"
#include "assets/ContentStorage.h"
#include "assets/MaterialAsset.h"
#include "crimson-renderer/ImGuiBgfx.h"
#include "resources/Paths.h"
#include "resources/AssetId.h"
#include <limits>
#include <type_traits>
#include <algorithm>
#include <imgui.h>
#include <imgui_internal.h>
#include <cstdio>

namespace crimson {
bool DecalEditor::focused() const {
    const auto* window=ImGui::GetCurrentContext()->NavWindow;
    return isOpen() && window && window->RootWindow->ID==ImHashStr("###Decal");
}
void DecalEditor::finishInteraction() {
    if(document_.transactionActive()) {
        if(!document_.commit())return;
        ImGui::ClearActiveID();suppressFinalUpdate_=true;
    }
}
void DecalEditor::cancelInteraction() {
    if(document_.transactionActive()){document_.cancel();ImGui::ClearActiveID();suppressFinalUpdate_=true;}
}
void DecalEditor::request(Pending pending,std::string id,AssetLibrary* assets) {
    if(pending_!=Pending::None) return;
    if(document_.transactionActive() && !document_.commit()) { message_=document_.status();return; }
    pending_=pending;pendingId_=std::move(id);
    if(document_.isOpen() && document_.dirty()) dirtyPopup_=true;
    else if(assets) perform(*assets);
    else if(pending==Pending::Exit) { pending_=Pending::None;exitReady_=true; }
}
void DecalEditor::requestExit() { if(!exitReady_) request(Pending::Exit,{},nullptr); }
void DecalEditor::requestOpen(std::string id,AssetLibrary& assets) {
    if(document_.isOpen() && document_.assetId()==id) { focusNext_=true;return; }
    request(Pending::Open,std::move(id),&assets);
}
void DecalEditor::requestCreate(std::string folder,AssetLibrary& assets) {
    pendingFolder_=std::move(folder);request(Pending::Create,{},&assets);
}
void DecalEditor::perform(AssetLibrary& assets) {
    const auto pending=pending_;
    pending_=Pending::None;
    continueAfterSave_=false;
    if(pending==Pending::Exit){exitReady_=true;return;}
    if(pending==Pending::Close) {
        document_.close();assets.protectDecalDocument({});assets.setDecalDocumentReferences({},{});assets.refreshDecals();
        return;
    }
    message_.clear();
    bool loaded=false;
    if(pending==Pending::Open) {
        loaded=document_.load(pendingId_);
    }
    if(pending==Pending::Create) {
        loaded=document_.create();
        if(loaded) {
            assets.noteAssetWrite(document_.assetId());assets.refreshDecals();
            try {
                if(!pendingFolder_.empty()) {
                    const std::array<std::string,1> ids{document_.assetId()};
                    assets.browserCatalog().move(ids,pendingFolder_);assets.noteAssetWrite("assets/browser.json");
                }
                assets.select(document_.assetId());
            } catch(const std::exception& error) { message_=error.what(); }
        }
    }
    if(loaded) {
        cameraInitialized_=false;previewGesture_=0;displayedTarget_=UINT16_MAX;
        assets.protectDecalDocument(document_.assetId());publishedRevision_=0;
        publish(assets);assets.refreshDecals();focusNext_=true;externalChanged_=false;
    } else message_=document_.status();
    pendingId_.clear();pendingFolder_.clear();
}
void DecalEditor::save(AssetLibrary& assets,bool saveAs) {
    if(!document_.isOpen()) return;
    if(document_.transactionActive() && !document_.commit()) { message_=document_.status();return; }
    publish(assets);
    if(saveAs || !document_.editable()) {
        std::snprintf(saveName_.data(),saveName_.size(),"%s",document_.definition().name.c_str());savePopup_=true;return;
    }
    if(document_.externalFileChanged()) { externalPopup_=true;message_="The decal changed on disk. Choose Overwrite, Reload, or Save As.";return; }
    if(document_.save()) { assets.noteAssetWrite(document_.assetId());assets.refreshDecals();message_.clear(); }
    else message_=document_.status();
}
void DecalEditor::filesChanged(std::span<const std::filesystem::path> paths) {
    if(!document_.isOpen() || !document_.editable()) return;
    try {
        const auto file=resolveAssetPath(document_.projectRoot(),document_.assetId()).lexically_normal();
        for(const auto& path:paths)if(resourcePathKey(path)==resourcePathKey(file))externalChanged_=true;
    } catch(const std::exception& error) { message_=error.what(); }
}
void DecalEditor::modals(AssetLibrary& assets) {
    if(dirtyPopup_) { ImGui::OpenPopup("Unsaved Decal");dirtyPopup_=false; }
    if(ImGui::BeginPopupModal("Unsaved Decal",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Save changes to %s?",document_.definition().name.c_str());
        if(ImGui::Button("Save")) {
            continueAfterSave_=true;
            save(assets);
            if(!document_.dirty()) { perform(assets);continueAfterSave_=false;ImGui::CloseCurrentPopup(); }
            else if(externalPopup_ || savePopup_)ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if(ImGui::Button("Discard")) {
            if(document_.discard()) { publish(assets);perform(assets);ImGui::CloseCurrentPopup(); }
        }
        ImGui::SameLine();
        if(ImGui::Button("Cancel")) { pending_=Pending::None;continueAfterSave_=false;ImGui::CloseCurrentPopup(); }
        if(!message_.empty()) ImGui::TextWrapped("%s",message_.c_str());
        ImGui::EndPopup();
    }
    if(savePopup_) { ImGui::OpenPopup("Save Decal as");savePopup_=false; }
    if(ImGui::BeginPopupModal("Save Decal as",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        if(ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter=ImGui::InputText("Name",saveName_.data(),saveName_.size(),ImGuiInputTextFlags_EnterReturnsTrue);
        if((ImGui::Button("Save") || enter) && saveName_[0]) {
            const auto previous=document_.assetId();
            const auto previousSaved=document_.savedDefinition();
            if(document_.save(saveName_.data(),true)) {
                assets.protectDecalDocument(document_.assetId());
                if(isAssetUuid(previous)&&!isEngineAssetId(previous)) {
                    auto source=previousSaved;
                    try {source=loadDecalAsset(assets.projectRoot(),previous);}
                    catch(...) { /* Missing/corrupt sources retain their saved generation. */ }
                    assets.publishDecal(std::make_shared<const DecalAsset>(std::move(source)));
                }
                publish(assets);assets.noteAssetWrite(document_.assetId());assets.refreshDecals();assets.select(document_.assetId());
                if(continueAfterSave_)perform(assets);
                continueAfterSave_=false;
                ImGui::CloseCurrentPopup();
            } else message_=document_.status();
        }
        ImGui::SameLine();if(ImGui::Button("Cancel")) { if(continueAfterSave_)pending_=Pending::None;continueAfterSave_=false;ImGui::CloseCurrentPopup(); }
        if(!message_.empty()) ImGui::TextWrapped("%s",message_.c_str());
        ImGui::EndPopup();
    }
    if(externalPopup_) { ImGui::OpenPopup("Decal changed on disk");externalPopup_=false; }
    if(ImGui::BeginPopupModal("Decal changed on disk",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("The decal changed on disk.");
        if(ImGui::Button("Overwrite")) {
            if(document_.save()) {
                assets.noteAssetWrite(document_.assetId());assets.refreshDecals();message_.clear();
                if(continueAfterSave_)perform(assets);
                continueAfterSave_=false;ImGui::CloseCurrentPopup();
            }
            else message_=document_.status();
        }
        ImGui::SameLine();
        if(ImGui::Button("Reload and discard edits")) {
            const auto id=document_.assetId();
            if(document_.load(id)) {
                publish(assets);message_.clear();if(continueAfterSave_)perform(assets);
                continueAfterSave_=false;ImGui::CloseCurrentPopup();
            }
            else message_=document_.status();
        }
        ImGui::SameLine();if(ImGui::Button("Keep editing")) { pending_=Pending::None;continueAfterSave_=false;ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();if(ImGui::Button("Save As")) { save(assets,true);ImGui::CloseCurrentPopup(); }
        if(!message_.empty()) ImGui::TextWrapped("%s",message_.c_str());
        ImGui::EndPopup();
    }
}
void DecalEditor::publish(AssetLibrary& assets) {
    if(!document_.isOpen())return;
    assets.setDecalDocumentReferences(decalTextureReferences(document_.definition()),document_.historicalTextureReferences());
    if(publishedRevision_!=document_.revision() || publishedId_!=document_.assetId()) {
        if(document_.editable())assets.publishDecal(document_.committed());
        publishedRevision_=document_.revision();publishedId_=document_.assetId();
    }
}
void DecalEditor::properties(AssetLibrary& assets,bool canceled) {
    auto value=document_.definition();const DecalAsset defaults;
    const auto commit=[&](const char* label) {
        if(canceled)return;
        finishInteraction();document_.edit(label,[&](auto& definition){definition=value;});
    };
    const auto gesture=[&](bool changed,const char* label) {
        if(canceled)return;
        if((ImGui::IsItemActivated()||changed)&&!document_.transactionActive())document_.begin(label);
        if(changed)document_.update([&](auto& definition){definition=value;});
        if(ImGui::IsItemDeactivated()||(changed&&!ImGui::IsItemActive()))document_.commit();
    };
    const auto enabled=[&](const char* label,bool& field,bool initial) {
        if(resettableProperty(field,initial,[&]{return ImGui::Checkbox(label,&field);}))commit(label);
    };
    const auto strength=[&](const char* label,float& field,float initial) {
        gesture(resettableProperty(field,initial,[&]{return ImGui::SliderFloat(label,&field,0,1,"%.3f");}),label);
    };
    const auto texture=[&](const char* label,AssetRef<DecodedImage>& field,bool normal=false) {
        return drawTextureAssetField(assets,label,field.id,message_,normal);
    };
    const auto channel=[&](const char* label,DecalTextureChannel& field,DecalTextureChannel initial) {
        int selected=static_cast<int>(field);
        if(resettableProperty(selected,static_cast<int>(initial),[&]{return ImGui::Combo(label,&selected,"Red\0Green\0Blue\0Alpha\0");})) {
            field=static_cast<DecalTextureChannel>(selected);commit(label);
        }
    };
    ImGui::BeginDisabled(!document_.editable());
    if(ImGui::CollapsingHeader("Color",ImGuiTreeNodeFlags_DefaultOpen)) {
        enabled("Enabled##Color",value.colorEnabled,defaults.colorEnabled);
        if(texture("Color Texture",value.colorTexture)) {if(!value.colorTexture.empty())value.colorEnabled=true;commit("Change decal color texture");}
        gesture(resettableProperty(value.color,defaults.color,[&]{return ImGui::ColorEdit3("Color Multiplier",value.color.data(),ImGuiColorEditFlags_Float);}),"Change decal color");
        strength("Color Strength",value.colorStrength,defaults.colorStrength);
    }
    if(ImGui::CollapsingHeader("Coverage",ImGuiTreeNodeFlags_DefaultOpen)) {
        if(texture("Coverage Texture",value.coverageTexture))commit("Change decal coverage texture");
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Without a coverage texture, use color alpha when a color texture is assigned, otherwise full coverage.");
        if(!value.coverageTexture.empty())channel("Coverage Channel",value.coverageChannel,defaults.coverageChannel);
    }
    if(ImGui::CollapsingHeader("Normal",ImGuiTreeNodeFlags_DefaultOpen)) {
        enabled("Enabled##Normal",value.normalEnabled,defaults.normalEnabled);
        if(texture("Normal Texture",value.normalTexture,true)) {if(!value.normalTexture.empty())value.normalEnabled=true;commit("Change decal normal texture");}
        strength("Normal Strength",value.normalStrength,defaults.normalStrength);
    }
    if(ImGui::CollapsingHeader("Roughness",ImGuiTreeNodeFlags_DefaultOpen)) {
        enabled("Enabled##Roughness",value.roughnessEnabled,defaults.roughnessEnabled);
        if(texture("Roughness Texture",value.roughnessTexture)) {if(!value.roughnessTexture.empty())value.roughnessEnabled=true;commit("Change decal roughness texture");}
        if(value.roughnessTexture.empty())strength("Roughness",value.roughness,defaults.roughness);
        else channel("Roughness Channel",value.roughnessChannel,defaults.roughnessChannel);
        strength("Roughness Strength",value.roughnessStrength,defaults.roughnessStrength);
    }
    ImGui::EndDisabled();
    if(document_.transactionActive()&&!ImGui::IsAnyItemActive())finishInteraction();
}
void DecalEditor::preview(AssetLibrary& assets,Renderer& renderer,ResourceManager& resources,EditorResources& prepared) {
    previewStatus_.clear();
    const auto available=ImGui::GetContentRegionAvail();if(available.x<=0||available.y<=0)return;
    const auto scale=ImGui::GetWindowViewport()->FramebufferScale;
    const auto width=static_cast<std::uint32_t>(std::clamp(available.x*scale.x,1.f,16384.f));
    const auto height=static_cast<std::uint32_t>(std::clamp(available.y*scale.y,1.f,16384.f));
    const auto& surface=decalPreviewSurface();
    if(!cameraInitialized_){settings_.camera=surface.camera;settings_.environment.glow.enabled=false;cameraInitialized_=true;}
    try {
        renderer.resizeDecalPreview(width,height);
        if(!environment_) {
            ResourceRequestOptions options;options.variant="ibl";options.priority=ResourcePriority::Selected;
            const auto root=assets.projectRoot();
            environment_=resources.request<PreparedEnvironment>(AssetRef<PreparedEnvironment>{std::string(StudioEnvironmentId)},[root](std::stop_token stop) {
                if(stop.stop_requested())throw std::runtime_error("Decal preview environment preparation canceled.");
                const auto bake=prepareEnvironmentCpu(root,{});
                std::size_t bytes=bake->data.diffuse.rgba.size()*sizeof(float)+bake->data.brdf.size()*sizeof(float);
                for(const auto& level:bake->data.specular)bytes+=level.rgba.size()*sizeof(float);
                return ResourceLoadResult<PreparedEnvironment>{bake,bytes,bake->key,{}};
            },std::move(options));
        }
        const auto environment=environment_.snapshot();
        if(environment&&environment.revision()!=environmentRevision_)
            if(renderer.publishDecalPreviewEnvironment(*environment))environmentRevision_=environment.revision();
        const auto material=assets.preparedDecalMaterial(document_.definition(),document_.previewRevision(),1024,ResourcePriority::Selected);
        auto textures=assets.modelBindings(*surface.model,surface.model->materials);
        prepared.retainExternalModelForFrame(surface.model->id);
        const bool modelReady=renderer.prepareModel(*surface.model)&&assets.modelBindingsReady(textures);
        const auto texture=renderer.decalPreviewTexture();
        if(bgfx::isValid(texture)&&texture.idx==displayedTarget_&&modelReady&&environmentRevision_)
            ImGui::Image(ImTextureRef(ImGuiBgfx::textureId(texture)),available);
        else ImGui::Dummy(available);
        displayedTarget_=texture.idx;
        const auto& io=ImGui::GetIO();
        const bool ownsInput=!io.WantTextInput&&!ImGui::IsAnyItemActive()&&!ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopup)&&!ImGui::IsKeyPressed(ImGuiKey_Escape,false);
        if(ownsInput&&ImGui::IsItemHovered()) {
            if(ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Middle)){settings_.camera=surface.camera;previewGesture_=0;}
            else if(ImGui::IsMouseClicked(ImGuiMouseButton_Middle)){ImGui::SetWindowFocus();previewGesture_=io.KeyShift?2:1;}
            if(io.MouseWheel) {
                settings_.camera.zoom(io.MouseWheel);
                const auto distance=std::clamp(settings_.camera.distance(),1.5f,12.f);
                settings_.camera.position=math::subtract(settings_.camera.target,math::scale(settings_.camera.forward(),distance));
            }
        }
        if(!ownsInput||!ImGui::IsMouseDown(ImGuiMouseButton_Middle)||!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))previewGesture_=0;
        if(previewGesture_==1)settings_.camera.orbit(io.MouseDelta.x,io.MouseDelta.y,settings_.camera.target);
        if(previewGesture_==2)settings_.camera.pan(io.MouseDelta.x,io.MouseDelta.y,available.x,available.y);
        if(environment_.state()==ResourceState::Failed)previewStatus_=environment_.diagnostic();
        else if(!environmentRevision_)previewStatus_="Preparing preview environment...";
        if(material&&!material->diagnostic.empty())previewStatus_=material->diagnostic;
        if(modelReady&&environmentRevision_) {
            struct Pins {
                std::shared_ptr<const ModelAsset> model;
                std::shared_ptr<const PreparedEnvironment> environment;
                std::vector<ModelMaterialTextures> textures;
                std::vector<RenderDecal> decals;
            };
            auto pins=std::make_shared<Pins>();pins->model=surface.model;pins->environment=environment.shared();pins->textures=std::move(textures);
            if(material&&material->ready&&material->render)
                pins->decals.push_back({2,2,0,surface.patch,material->render});
            std::array<RenderModelInstance,1> instances{{{1,{surface.model,Mat4{}},&pins->textures,1,true,false,&surface.model->materials}}};
            instances.front().receiveDecals=true;instances.front().castShadows=false;
            const std::array<DirectionalLight,1> lights{{{{-.4f,-1,-.7f},{1,1,1},2,true}}};
            RenderScene scene{settings_,{},nullptr,nullptr,instances,lights,{},true,pins};scene.decals=pins->decals;
            renderer.renderDecalPreview(scene);
        }
    }catch(const std::exception& error){previewStatus_=error.what();ImGui::Dummy(available);}
}
void DecalEditor::draw(AssetLibrary& assets,Renderer& renderer,ResourceManager& resources,EditorResources& prepared) {
    if(!document_.isOpen()) {
        renderer.releaseDecalPreview();displayedTarget_=UINT16_MAX;environment_={};environmentRevision_=0;
        modals(assets);return;
    }
    if(externalChanged_&&!document_.transactionActive()) {
        externalChanged_=false;
        if(document_.externalFileChanged()) {
            if(document_.dirty())externalPopup_=true;
            else if(!document_.load(document_.assetId()))message_=document_.status();
        }
    }
    if(focusNext_){ImGui::SetNextWindowFocus();focusNext_=false;}
    ImGui::SetNextWindowSize({850,560},ImGuiCond_FirstUseEver);
    bool open=true;const auto title=document_.definition().name+(document_.dirty()?" *":"")+"###Decal";
    if(ImGui::Begin(title.c_str(),&open,DocumentEditorWindowFlags)) {
        const auto action=drawDocumentEditorMenuBar([&]{finishInteraction();},[&]{return DocumentEditorMenuState{
            document_.editable(),document_.isOpen(),document_.canUndo(),document_.canRedo(),document_.undoLabel(),document_.redoLabel()};});
        if(action==DocumentEditorAction::Save)save(assets);
        if(action==DocumentEditorAction::SaveAs)save(assets,true);
        if(action==DocumentEditorAction::Close)request(Pending::Close,{},&assets);
        if(action==DocumentEditorAction::Undo)undo();
        if(action==DocumentEditorAction::Redo)redo();
        const bool canceled=suppressFinalUpdate_||(document_.transactionActive()&&(ImGui::IsKeyPressed(ImGuiKey_Escape,false)||ImGui::GetIO().AppFocusLost));
        if(canceled)cancelInteraction();suppressFinalUpdate_=false;
        if(ImGui::BeginTable("Decal document",2,ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("Properties",ImGuiTableColumnFlags_WidthFixed,310);
            ImGui::TableSetupColumn("Preview",ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextColumn();ImGui::BeginChild("Decal properties");properties(assets,canceled);
            if(!message_.empty())ImGui::TextWrapped("%s",message_.c_str());
            if(!document_.status().empty())ImGui::TextWrapped("%s",document_.status().c_str());
            ImGui::EndChild();ImGui::TableNextColumn();ImGui::BeginChild("Decal preview");
            if(!previewStatus_.empty())ImGui::TextWrapped("%s",previewStatus_.c_str());
            preview(assets,renderer,resources,prepared);ImGui::EndChild();ImGui::EndTable();
        }
    }else finishInteraction();
    ImGui::End();if(!open)request(Pending::Close,{},&assets);publish(assets);modals(assets);
}
}
