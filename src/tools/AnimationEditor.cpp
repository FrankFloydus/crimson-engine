#include "AnimationEditor.h"
#include "DocumentEditorMenu.h"
#include "PropertyReset.h"
#include "assets/AssetLibrary.h"
#include "crimson-renderer/ImGuiBgfx.h"
#include "resources/Paths.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace crimson {
namespace {
struct PreviewPins {
    std::shared_ptr<const PreparedEditorModel> model;
    std::shared_ptr<const ModelPose> pose;
    std::shared_ptr<const PreparedEnvironment> environment;
};
}
bool AnimationEditor::focused() const {
    const auto* window=ImGui::GetCurrentContext()->NavWindow;
    return isOpen()&&window&&window->RootWindow->ID==ImHashStr("###AnimationEditor");
}
void AnimationEditor::finishInteraction() {
    if(document_.transactionActive()&&document_.commit()){ImGui::ClearActiveID();suppressFinalUpdate_=true;propertySection_=0;}
}
void AnimationEditor::cancelInteraction() {
    previewGesture_=0;
    if(document_.transactionActive()){document_.cancel();ImGui::ClearActiveID();suppressFinalUpdate_=true;propertySection_=0;}
}
void AnimationEditor::request(Pending pending,std::string id,AssetLibrary* assets) {
    if(pending_!=Pending::None)return;
    if(document_.transactionActive()&&!document_.commit()){message_=document_.status();return;}
    pending_=pending;pendingId_=std::move(id);
    if(isOpen()&&document_.dirty())dirtyPopup_=true;
    else if(assets)perform(*assets);
    else if(pending==Pending::Exit){pending_=Pending::None;exitReady_=true;}
}
void AnimationEditor::requestExit(){if(!exitReady_)request(Pending::Exit,{},nullptr);}
void AnimationEditor::requestOpen(std::string id,AssetLibrary& assets) {
    if(isOpen()&&document_.assetId()==id){focusNext_=true;return;}
    request(Pending::Open,std::move(id),&assets);
}
void AnimationEditor::perform(AssetLibrary& assets) {
    const auto pending=pending_;pending_=Pending::None;continueAfterSave_=false;
    if(pending==Pending::Exit){exitReady_=true;return;}
    if(pending==Pending::Close) {
        document_.close();assets.protectAnimationDocument({});assets.setAnimationDocumentReferences({},{});assets.refreshAnimations();
        binding_.reset();boundSource_.reset();return;
    }
    if(pending==Pending::Open) {
        if(document_.load(pendingId_)) {
            assets.protectAnimationDocument(document_.assetId());publishedRevision_=0;
            publish(assets);focusNext_=true;externalChanged_=false;message_.clear();
            seconds_=0;playing_=true;framePending_=true;binding_.reset();boundSource_.reset();previewGesture_=0;
        }else message_=document_.status();
    }
    pendingId_.clear();
}
void AnimationEditor::publish(AssetLibrary& assets) {
    if(!isOpen())return;
    if(publishedId_!=document_.assetId()||publishedRevision_!=document_.revision()) {
        try {
            assets.publishAnimation(document_.committed());
            publishedId_=document_.assetId();publishedRevision_=document_.revision();
        }catch(const std::exception& error){message_=error.what();}
    }
    assets.setAnimationDocumentReferences(document_.modelReferences(),document_.modelReferences(true));
}
void AnimationEditor::save(AssetLibrary& assets,bool saveAs) {
    if(!isOpen())return;
    if(document_.transactionActive()&&!document_.commit()){message_=document_.status();return;}
    publish(assets);
    if(saveAs){std::snprintf(saveName_.data(),saveName_.size(),"%s",document_.definition().name.c_str());savePopup_=true;return;}
    if(document_.externalFileChanged()){externalPopup_=true;return;}
    if(document_.save()){assets.noteAssetWrite(document_.assetId());assets.refreshAnimations();message_.clear();}
    else message_=document_.status();
}
void AnimationEditor::filesChanged(std::span<const std::filesystem::path> paths) {
    if(!isOpen())return;
    try {
        const auto file=animationAssetPath(document_.projectRoot(),document_.assetId());
        for(const auto& path:paths)if(resourcePathKey(path)==resourcePathKey(file))externalChanged_=true;
    }catch(...){externalChanged_=true;}
}
void AnimationEditor::modals(AssetLibrary& assets) {
    if(dirtyPopup_){ImGui::OpenPopup("Unsaved animation");dirtyPopup_=false;}
    if(ImGui::BeginPopupModal("Unsaved animation",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Save changes to %s?",document_.definition().name.c_str());
        if(ImGui::Button("Save")) {
            continueAfterSave_=true;save(assets);
            if(!document_.dirty()){perform(assets);continueAfterSave_=false;ImGui::CloseCurrentPopup();}
            else if(externalPopup_||savePopup_)ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();if(ImGui::Button("Discard")) {
            if(document_.discard()){publish(assets);perform(assets);ImGui::CloseCurrentPopup();}
        }
        ImGui::SameLine();if(ImGui::Button("Cancel")){pending_=Pending::None;continueAfterSave_=false;ImGui::CloseCurrentPopup();}
        if(!message_.empty())ImGui::TextWrapped("%s",message_.c_str());
        ImGui::EndPopup();
    }
    if(savePopup_){ImGui::OpenPopup("Save animation as");savePopup_=false;}
    if(ImGui::BeginPopupModal("Save animation as",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        if(ImGui::IsWindowAppearing())ImGui::SetKeyboardFocusHere();
        const bool enter=ImGui::InputText("Name",saveName_.data(),saveName_.size(),ImGuiInputTextFlags_EnterReturnsTrue);
        if((ImGui::Button("Save")||enter)&&saveName_[0]) {
            const auto previous=document_.assetId();const auto previousSaved=document_.savedDefinition();
            if(document_.save(saveName_.data(),true)) {
                assets.protectAnimationDocument(document_.assetId());
                auto source=previousSaved;
                try{source=loadAnimationAsset(assets.projectRoot(),previous);}catch(...){ }
                assets.publishAnimation(std::make_shared<const AnimationAsset>(std::move(source)));
                publish(assets);assets.noteAssetWrite(document_.assetId());assets.refreshAnimations();assets.select(document_.assetId());
                if(continueAfterSave_)perform(assets);
                continueAfterSave_=false;ImGui::CloseCurrentPopup();
            }else message_=document_.status();
        }
        ImGui::SameLine();if(ImGui::Button("Cancel")){if(continueAfterSave_)pending_=Pending::None;continueAfterSave_=false;ImGui::CloseCurrentPopup();}
        if(!message_.empty())ImGui::TextWrapped("%s",message_.c_str());
        ImGui::EndPopup();
    }
    if(externalPopup_){ImGui::OpenPopup("Animation changed on disk");externalPopup_=false;}
    if(ImGui::BeginPopupModal("Animation changed on disk",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("The animation changed on disk.");
        if(ImGui::Button("Overwrite")) {
            if(document_.save()) {
                assets.noteAssetWrite(document_.assetId());assets.refreshAnimations();message_.clear();
                if(continueAfterSave_)perform(assets);
                continueAfterSave_=false;ImGui::CloseCurrentPopup();
            }else message_=document_.status();
        }
        ImGui::SameLine();if(ImGui::Button("Reload and discard edits")) {
            const auto id=document_.assetId();
            if(document_.load(id)) {
                publish(assets);message_.clear();if(continueAfterSave_)perform(assets);
                continueAfterSave_=false;ImGui::CloseCurrentPopup();
            }else message_=document_.status();
        }
        ImGui::SameLine();if(ImGui::Button("Keep editing")){pending_=Pending::None;continueAfterSave_=false;ImGui::CloseCurrentPopup();}
        ImGui::SameLine();if(ImGui::Button("Save As")){save(assets,true);ImGui::CloseCurrentPopup();}
        if(!message_.empty())ImGui::TextWrapped("%s",message_.c_str());
        ImGui::EndPopup();
    }
}
void AnimationEditor::properties(AssetLibrary& assets,const std::shared_ptr<const ModelAsset>& source,bool canceled) {
    int section=1;
    const auto field=[&](const char* label,bool changed,const AnimationDocument::Mutation& mutation) {
        if(!canceled&&(ImGui::IsItemActivated()||changed)&&!document_.transactionActive()) {
            if(document_.begin(std::string("Change ")+label))propertySection_=section;
        }
        if(!canceled&&changed)document_.update(mutation);
        if(ImGui::IsItemDeactivated()||(changed&&!ImGui::IsItemActive())) {
            if(document_.commit())propertySection_=0;
        }
    };
    if(ImGui::CollapsingHeader("Animation",ImGuiTreeNodeFlags_DefaultOpen)) {
        if(ImGui::Button(assets.displayName(document_.definition().sourceModel.id).c_str(),{ImGui::GetContentRegionAvail().x,0}))assets.select(document_.definition().sourceModel.id);
        if(source&&document_.definition().sourceClip<source->animations.size())ImGui::TextWrapped("%s",source->animations[document_.definition().sourceClip].name.c_str());
        auto value=document_.definition();
        float sourceDuration=value.endTime;
        if(source&&value.sourceClip<source->animations.size())sourceDuration=std::max(0.f,source->animations[value.sourceClip].end-source->animations[value.sourceClip].start);
        bool changed=resettableProperty(value.startTime,0.f,[&]{return ImGui::DragFloat("Start Time (s)",&value.startTime,.01f,0,value.endTime,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
        field("animation start",changed,[&](auto& asset){asset.startTime=value.startTime;});
        changed=resettableProperty(value.endTime,sourceDuration,[&]{return ImGui::DragFloat("End Time (s)",&value.endTime,.01f,value.startTime,sourceDuration,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
        field("animation end",changed,[&](auto& asset){asset.endTime=value.endTime;});
        changed=resettableProperty(value.loop,true,[&]{return ImGui::Checkbox("Loop",&value.loop);});
        field("animation loop",changed,[&](auto& asset){asset.loop=value.loop;});
        changed=resettableProperty(value.playbackRate,1.f,[&]{return ImGui::DragFloat("Playback Rate",&value.playbackRate,.01f,0,100,"%.2f",ImGuiSliderFlags_AlwaysClamp);});
        field("animation playback rate",changed,[&](auto& asset){asset.playbackRate=value.playbackRate;});
    }else if(propertySection_==1)finishInteraction();
    section=2;
    if(ImGui::CollapsingHeader("Root",ImGuiTreeNodeFlags_DefaultOpen)) {
        auto value=document_.definition();
        const auto rootLabel=value.rootNode>=0&&source&&std::size_t(value.rootNode)<source->nodes.size()?
            (source->nodes[value.rootNode].name.empty()?"Node "+std::to_string(value.rootNode):source->nodes[value.rootNode].name):std::string("None");
        bool changed=false;
        if(ImGui::BeginCombo("Root Bone",rootLabel.c_str())) {
            if(ImGui::Selectable("None",value.rootNode<0)){changed=value.rootNode!=-1;value.rootNode=-1;value.forceRootLock=false;}
            if(source)for(std::size_t index=0;index<source->nodes.size();++index) {
                ImGui::PushID(static_cast<int>(index));const auto& node=source->nodes[index];
                const auto name=node.name.empty()?"Node "+std::to_string(index):node.name;
                if(ImGui::Selectable(name.c_str(),value.rootNode==static_cast<std::int32_t>(index))){changed=value.rootNode!=static_cast<std::int32_t>(index);value.rootNode=static_cast<std::int32_t>(index);}
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        if(changed&&!canceled)document_.edit("Change animation root",[&](auto& asset){asset.rootNode=value.rootNode;asset.forceRootLock=value.forceRootLock;});
        ImGui::BeginDisabled(value.rootNode<0);
        changed=resettableProperty(value.forceRootLock,false,[&]{return ImGui::Checkbox("Force Root Lock",&value.forceRootLock);});
        field("force root lock",changed,[&](auto& asset){asset.forceRootLock=value.forceRootLock;});
        ImGui::EndDisabled();
        auto reference=static_cast<int>(value.rootLockReference);
        changed=resettableProperty(reference,0,[&]{return ImGui::Combo("Root Lock Reference",&reference,"First Frame\0Reference Pose\0Zero\0");},"First Frame");
        field("root lock reference",changed,[&](auto& asset){asset.rootLockReference=static_cast<AnimationRootLockReference>(reference);});
    }else if(propertySection_==2)finishInteraction();
}
void AnimationEditor::timeline(double duration) {
    if(ImGui::Button(playing_?"Pause":"Play"))playing_=!playing_;
    ImGui::SameLine();if(ImGui::Button("Restart")){seconds_=0;playing_=true;}
    ImGui::SameLine();if(ImGui::Button("Frame"))framePending_=true;
    ImGui::SameLine();ImGui::Text("%.3f / %.3f s",seconds_,duration);
    float time=static_cast<float>(seconds_);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if(ImGui::DragFloat("##AnimationTime",&time,.01f,0,static_cast<float>(duration),"Time: %.3f s",ImGuiSliderFlags_AlwaysClamp)){seconds_=std::clamp(double(time),0.,duration);playing_=false;}
    const auto origin=ImGui::GetCursorScreenPos();const float width=std::max(1.f,ImGui::GetContentRegionAvail().x),height=70.f;
    ImGui::InvisibleButton("Animation timeline",{width,height});
    if((ImGui::IsItemActive()&&ImGui::IsMouseDown(ImGuiMouseButton_Left))||
       (ImGui::IsItemHovered()&&ImGui::IsMouseClicked(ImGuiMouseButton_Left))) {
        seconds_=std::clamp(double((ImGui::GetIO().MousePos.x-origin.x)/width),0.,1.)*duration;playing_=false;
    }
    auto* draw=ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin,{origin.x+width,origin.y+height},ImGui::GetColorU32(ImGuiCol_FrameBg),ImGui::GetStyle().FrameRounding);
    const float ruler=origin.y+28;
    draw->AddLine({origin.x,ruler},{origin.x+width,ruler},ImGui::GetColorU32(ImGuiCol_Border));
    const double rawStep=duration/std::max(2.,double(width/85));
    const double magnitude=rawStep>0?std::pow(10.,std::floor(std::log10(rawStep))):1.;
    const double unit=rawStep/magnitude;const double step=(unit<=1?1:unit<=2?2:unit<=5?5:10)*magnitude;
    for(double tick=0;tick<=duration&&tick/step<1000;tick+=step) {
        const float x=origin.x+(duration>0?static_cast<float>(tick/duration)*width:0);
        draw->AddLine({x,ruler-5},{x,ruler+5},ImGui::GetColorU32(ImGuiCol_Border));
        char label[40];std::snprintf(label,sizeof(label),"%.2f",tick);
        draw->AddText({std::min(x+3,origin.x+std::max(0.f,width-40)),origin.y+6},ImGui::GetColorU32(ImGuiCol_Text),label);
    }
    const float playhead=origin.x+(duration>0?static_cast<float>(seconds_/duration)*width:0);
    const auto color=ImGui::GetColorU32(ImGuiCol_SliderGrabActive);
    draw->AddLine({playhead,origin.y+26},{playhead,origin.y+height},color,2);
    draw->AddTriangleFilled({playhead-5,ruler},{playhead+5,ruler},{playhead,ruler+7},color);
}
void AnimationEditor::preview(AssetLibrary& assets,Renderer& renderer,ResourceManager& resources,EditorResources& prepared,double dt) {
    previewStatus_.clear();
    const auto& animation=document_.definition();const auto source=assets.findModel(animation.sourceModel.id);
    const auto duration=std::max(0.,animationDuration(animation));seconds_=std::clamp(seconds_,0.,duration);
    if(playing_&&std::isfinite(dt)&&dt>0) {
        seconds_+=std::min(dt,1.)*animation.playbackRate;
        if(duration>0&&animation.loop)seconds_=std::fmod(seconds_,duration);
        else if(seconds_>=duration){seconds_=duration;playing_=false;}
    }
    timeline(duration);
    const auto available=ImGui::GetContentRegionAvail();
    if(available.x<=0||available.y<=0)return;
    const auto scale=ImGui::GetWindowViewport()->FramebufferScale;
    const auto width=static_cast<std::uint32_t>(std::clamp(available.x*scale.x,1.f,16384.f));
    const auto height=static_cast<std::uint32_t>(std::clamp(available.y*scale.y,1.f,16384.f));
    try {
        renderer.resizeAnimationPreview(width,height);
        if(!environment_) {
            ResourceRequestOptions options;options.variant="ibl";options.priority=ResourcePriority::Selected;
            const auto root=assets.projectRoot();
            environment_=resources.request<PreparedEnvironment>(AssetRef<PreparedEnvironment>{std::string(StudioEnvironmentId)},[root](std::stop_token token) {
                if(token.stop_requested())throw std::runtime_error("Animation preview environment preparation canceled.");
                auto bake=prepareEnvironmentCpu(root,{});
                std::size_t bytes=bake->data.diffuse.rgba.size()*sizeof(float)+bake->data.brdf.size()*sizeof(float);
                for(const auto& level:bake->data.specular)bytes+=level.rgba.size()*sizeof(float);
                return ResourceLoadResult<PreparedEnvironment>{bake,bytes,bake->key,{}};
            },std::move(options));
        }
        const auto environment=environment_.snapshot();
        if(environment&&environment.revision()!=environmentRevision_)
            if(renderer.publishAnimationPreviewEnvironment(*environment))environmentRevision_=environment.revision();
        std::shared_ptr<const ModelPose> pose;
        if(source) {
            if(!binding_||bindingRevision_!=document_.previewRevision()||boundSource_!=source) {
                binding_=prepareAnimationBinding(animation,*source,*source);bindingRevision_=document_.previewRevision();boundSource_=source;
            }
            pose=std::make_shared<const ModelPose>(evaluateAnimationPose(*binding_,*source,*source,seconds_,false));
            if(framePending_&&pose->bounds.valid) {
                settings_.camera=Camera{};settings_.camera.frameBounds(pose->bounds.minimum,pose->bounds.maximum,float(width)/height);
                framePending_=false;
            }
        }
        const auto model=source?prepared.prepare(source->id,Mat4{},settings_.camera,height,true,ResourcePriority::Selected,float(width)/height):nullptr;
        const auto texture=renderer.animationPreviewTexture();
        if(bgfx::isValid(texture)&&texture.idx==displayedTarget_&&model&&model->ready&&pose&&environmentRevision_)
            ImGui::Image(ImTextureRef(ImGuiBgfx::textureId(texture)),available);
        else ImGui::Dummy(available);
        displayedTarget_=texture.idx;
        const auto& io=ImGui::GetIO();
        const bool ownsInput=!io.WantTextInput&&!ImGui::IsAnyItemActive()&&!ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopup)&&!ImGui::IsKeyPressed(ImGuiKey_Escape,false);
        if(ownsInput&&ImGui::IsItemHovered()) {
            if(ImGui::IsMouseClicked(ImGuiMouseButton_Middle)){ImGui::SetWindowFocus();previewGesture_=io.KeyShift?2:1;}
            if(io.MouseWheel)settings_.camera.zoom(io.MouseWheel);
        }
        if(!ownsInput||!ImGui::IsMouseDown(ImGuiMouseButton_Middle)||!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))previewGesture_=0;
        if(previewGesture_==1)settings_.camera.orbit(io.MouseDelta.x,io.MouseDelta.y,settings_.camera.target);
        if(previewGesture_==2)settings_.camera.pan(io.MouseDelta.x,io.MouseDelta.y,available.x,available.y);
        if(!source)previewStatus_=assets.modelLoading(animation.sourceModel.id)?"Preparing animation model...":"Animation source model is unavailable.";
        else if(!model||!model->ready)previewStatus_="Preparing animation preview...";
        else if(!environmentRevision_)previewStatus_="Preparing preview environment...";
        if(environment_.state()==ResourceState::Failed)previewStatus_=environment_.diagnostic();
        if(model&&model->ready&&pose&&environmentRevision_) {
            auto pins=std::make_shared<PreviewPins>();pins->model=model;pins->pose=pose;pins->environment=environment.shared();
            std::array<RenderModelInstance,1> instances{{{0,{model->asset,Mat4{}},&model->textures,model->revision,false,false,&model->materials}}};
            instances.front().pose=pose;instances.front().castShadows=false;
            RenderScene scene{settings_,{},nullptr,nullptr,instances,{}, {},true,pins};
            renderer.renderAnimationPreview(scene);
        }
    }catch(const std::exception& error){previewStatus_=error.what();ImGui::Dummy(available);}
}
void AnimationEditor::draw(AssetLibrary& assets,Renderer& renderer,ResourceManager& resources,EditorResources& prepared,double dt) {
    if(pending_==Pending::Exit&&!document_.dirty())perform(assets);
    if(isOpen()&&assets.references().deleted(document_.assetId())) {
        document_.close();assets.protectAnimationDocument({});assets.setAnimationDocumentReferences({},{});message_="The open Animation asset was deleted.";
    }
    if(isOpen()&&externalChanged_&&!document_.transactionActive()&&pending_==Pending::None) {
        externalChanged_=false;
        if(document_.externalFileChanged()) {
            if(document_.dirty())externalPopup_=true;
            else if(!document_.load(document_.assetId()))message_=document_.status();
        }
    }
    if(!isOpen()) {
        renderer.releaseAnimationPreview();displayedTarget_=UINT16_MAX;environment_={};environmentRevision_=0;binding_.reset();boundSource_.reset();modals(assets);return;
    }
    if(focusNext_){ImGui::SetNextWindowFocus();focusNext_=false;}
    ImGui::SetNextWindowDockID(0,ImGuiCond_Always);
    ImGui::SetNextWindowSize({1100,700},ImGuiCond_FirstUseEver);ImGui::SetNextWindowSizeConstraints({720,420},{FLT_MAX,FLT_MAX});
    bool open=true;const auto title="Animation Editor - "+document_.definition().name+(document_.dirty()?" *":"")+"###AnimationEditor";
    const bool contents=ImGui::Begin(title.c_str(),&open,DocumentEditorWindowFlags);
    if(!open)request(Pending::Close,{},&assets);
    if(contents&&isOpen()) {
        const auto action=drawDocumentEditorMenuBar([&]{finishInteraction();},[&]{return DocumentEditorMenuState{
            canSave(),canSaveAs(),canUndo(),canRedo(),document_.undoLabel(),document_.redoLabel()};});
        if(action==DocumentEditorAction::Save)save(assets);
        if(action==DocumentEditorAction::SaveAs)save(assets,true);
        if(action==DocumentEditorAction::Close)request(Pending::Close,{},&assets);
        if(action==DocumentEditorAction::Undo)undo();
        if(action==DocumentEditorAction::Redo)redo();
        if(isOpen()) {
            const bool canceled=suppressFinalUpdate_||(document_.transactionActive()&&(ImGui::IsKeyPressed(ImGuiKey_Escape,false)||ImGui::GetIO().AppFocusLost));
            if(canceled)cancelInteraction();suppressFinalUpdate_=false;
            if(ImGui::BeginTable("Animation layout",2,ImGuiTableFlags_Resizable|ImGuiTableFlags_BordersInnerV)) {
                ImGui::TableSetupColumn("Properties",ImGuiTableColumnFlags_WidthFixed,340);ImGui::TableSetupColumn("Preview",ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableNextColumn();ImGui::BeginChild("Animation properties",{0,0});
                properties(assets,assets.findModel(document_.definition().sourceModel.id),canceled);
                if(!message_.empty())ImGui::TextWrapped("%s",message_.c_str());
                if(!document_.status().empty())ImGui::TextWrapped("%s",document_.status().c_str());
                if(!previewStatus_.empty())ImGui::TextWrapped("%s",previewStatus_.c_str());
                ImGui::EndChild();ImGui::TableNextColumn();ImGui::BeginChild("Animation preview",{0,0});
                preview(assets,renderer,resources,prepared,dt);ImGui::EndChild();ImGui::EndTable();
            }
        }
    }else finishInteraction();
    ImGui::End();publish(assets);modals(assets);
}
}
