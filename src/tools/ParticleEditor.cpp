#include "ParticleEditor.h"
#include "MaterialEditor.h"
#include "assets/AssetLibrary.h"
#include "crimson-renderer/ImGuiBgfx.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace crimson {
bool ParticleEditor::isOpen() const { return document_.isOpen(); }
bool ParticleEditor::focused() const {
    const auto* window=ImGui::GetCurrentContext()->NavWindow;
    return isOpen() && window && window->RootWindow->ID==ImHashStr("###ParticleEffect");
}
bool ParticleEditor::canUndo() const { return document_.canUndo(); }
bool ParticleEditor::canRedo() const { return document_.canRedo(); }
void ParticleEditor::undo() { document_.undo(); }
void ParticleEditor::redo() { document_.redo(); }
void ParticleEditor::finishInteraction() {
    if(document_.transactionActive() && !document_.commit())return;
    const auto* active=ImGui::GetCurrentContext()->ActiveIdWindow;
    if(active && active->RootWindow->ID==ImHashStr("###ParticleEffect"))ImGui::ClearActiveID();
}
void ParticleEditor::framePreview() {
    preview_.camera=Camera{};
    const auto& emitter=document_.definition().emitter;
    Vec3 minimum{},maximum{};
    const double lifetime=emitter.lifetime;
    for(std::size_t axis=0;axis<3;++axis) {
        const double velocity=emitter.initialVelocity[axis],gravity=emitter.gravity[axis];
        const auto includeTime=[&](double time) {
            const auto position=static_cast<float>(velocity*time+.5*gravity*time*time);
            minimum[axis]=std::min(minimum[axis],position);maximum[axis]=std::max(maximum[axis],position);
        };
        includeTime(lifetime);
        if(gravity!=0) { const auto turn=-velocity/gravity;if(turn>0 && turn<lifetime)includeTime(turn); }
    }
    const float padding=std::max(.5f,emitter.size);
    preview_.camera.frameBounds(minimum-Vec3{padding,padding,padding},maximum+Vec3{padding,padding,padding},1.4f);
    preview_.exposure=1;
}
void ParticleEditor::request(Pending pending,std::string id,AssetLibrary* assets) {
    if(pending_!=Pending::None) return;
    if(document_.transactionActive() && !document_.commit()) { message_=document_.status();return; }
    pending_=pending;pendingId_=std::move(id);
    if(document_.isOpen() && document_.dirty()) dirtyPopup_=true;
    else if(assets) perform(*assets);
    else if(pending==Pending::Exit) { pending_=Pending::None;exitReady_=true; }
}
void ParticleEditor::requestExit() { if(!exitReady_) request(Pending::Exit,{},nullptr); }
void ParticleEditor::requestOpen(std::string id,AssetLibrary& assets) {
    if(document_.isOpen() && document_.assetId()==id) { focusNext_=true;return; }
    request(Pending::Open,std::move(id),&assets);
}
void ParticleEditor::requestCreate(std::string folder,AssetLibrary& assets) {
    pendingFolder_=std::move(folder);request(Pending::Create,{},&assets);
}
void ParticleEditor::perform(AssetLibrary& assets) {
    const auto pending=pending_;
    pending_=Pending::None;
    continueAfterSave_=false;
    if(pending==Pending::Exit){exitReady_=true;return;}
    if(pending==Pending::Close) {
        document_.close();simulation_.stop();assets.protectParticleDocument({});assets.setParticleDocumentReferences({},{});assets.refreshParticles();
        return;
    }
    message_.clear();
    bool loaded=false;
    if(pending==Pending::Open) loaded=document_.load(pendingId_);
    if(pending==Pending::Create) {
        loaded=document_.create();
        if(loaded) {
            assets.noteAssetWrite(document_.assetId());assets.refreshParticles();
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
        assets.protectParticleDocument(document_.assetId());publishedRevision_=0;previewRevision_=0;
        publish(assets);assets.refreshParticles();framePreview();focusNext_=true;externalChanged_=false;materialDiagnostic_.clear();
    } else message_=document_.status();
    pendingId_.clear();pendingFolder_.clear();
}
void ParticleEditor::publish(AssetLibrary& assets) {
    if(!document_.isOpen()) return;
    assets.setParticleDocumentReferences({document_.definition().emitter.material.id},document_.historicalMaterialReferences());
    if(publishedRevision_!=document_.revision() || publishedId_!=document_.assetId()) {
        assets.publishParticle(document_.committed());publishedRevision_=document_.revision();publishedId_=document_.assetId();
    }
}
void ParticleEditor::save(AssetLibrary& assets,bool saveAs) {
    if(!document_.isOpen()) return;
    if(document_.transactionActive() && !document_.commit()) { message_=document_.status();return; }
    publish(assets);
    if(saveAs) {
        std::snprintf(saveName_.data(),saveName_.size(),"%s",document_.definition().name.c_str());savePopup_=true;return;
    }
    if(document_.externalFileChanged()) { externalPopup_=true;message_="The effect changed on disk. Choose Overwrite, Reload, or Save As.";return; }
    if(document_.save()) { assets.noteAssetWrite(document_.assetId());assets.refreshParticles();message_.clear(); }
    else message_=document_.status();
}
void ParticleEditor::filesChanged(std::span<const std::filesystem::path> paths) {
    if(!document_.isOpen()) return;
    for(const auto& path:paths) if(path.filename()==pathFromUtf8(document_.assetId()).filename())
        externalChanged_=true;
}
void ParticleEditor::modals(AssetLibrary& assets) {
    if(dirtyPopup_) { ImGui::OpenPopup("Unsaved particle effect");dirtyPopup_=false; }
    if(ImGui::BeginPopupModal("Unsaved particle effect",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
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
    if(savePopup_) { ImGui::OpenPopup("Save particle effect as");savePopup_=false; }
    if(ImGui::BeginPopupModal("Save particle effect as",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        if(ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter=ImGui::InputText("Name",saveName_.data(),saveName_.size(),ImGuiInputTextFlags_EnterReturnsTrue);
        if((ImGui::Button("Save") || enter) && saveName_[0]) {
            const auto previous=document_.assetId();
            if(document_.save(saveName_.data(),true)) {
                assets.protectParticleDocument(document_.assetId());
                try { assets.publishParticle(std::make_shared<ParticleEffectAsset>(loadParticleAsset(assets.projectRoot(),previous))); }
                catch(const std::exception& error) { message_=error.what(); }
                publish(assets);assets.noteAssetWrite(document_.assetId());assets.refreshParticles();assets.select(document_.assetId());
                if(continueAfterSave_)perform(assets);
                continueAfterSave_=false;
                ImGui::CloseCurrentPopup();
            } else message_=document_.status();
        }
        ImGui::SameLine();if(ImGui::Button("Cancel")) { if(continueAfterSave_)pending_=Pending::None;continueAfterSave_=false;ImGui::CloseCurrentPopup(); }
        if(!message_.empty()) ImGui::TextWrapped("%s",message_.c_str());
        ImGui::EndPopup();
    }
    if(externalPopup_) { ImGui::OpenPopup("Particle effect changed on disk");externalPopup_=false; }
    if(ImGui::BeginPopupModal("Particle effect changed on disk",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("The effect changed outside this editor. The working revision is retained until you choose to reload.");
        if(ImGui::Button("Overwrite")) {
            if(document_.save()) {
                assets.noteAssetWrite(document_.assetId());assets.refreshParticles();message_.clear();
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
void ParticleEditor::materialControls(AssetLibrary& assets,MaterialEditor& materials) {
    ImGui::TextUnformatted("Material");
    const auto materialId=assets.references().resolveParticleMaterial(document_.definition().emitter.material.id);
    const auto material=assets.findMaterial(materialId);
    const auto assign=[&](const std::string& id) {
        const auto candidate=assets.findMaterial(id);
        if(!candidate || !particleMaterialCompatible(*candidate)){message_="Choose an unlit Alpha or Additive sprite material.";return;}
        if(document_.transactionActive() && !document_.commit())return;
        document_.edit("Assign particle material",[&](auto& asset){asset.emitter.material.id=id;});message_.clear();
    };
    ImGui::BeginGroup();
    const float height=ImGui::GetFrameHeight(),gap=ImGui::GetStyle().ItemInnerSpacing.x;
    const float width=std::max(height,ImGui::GetContentRegionAvail().x-height*2-gap*2);
    bool picker=ImGui::InvisibleButton("Particle material",{width,height});
    const auto minimum=ImGui::GetItemRectMin(),maximum=ImGui::GetItemRectMax();auto* draw=ImGui::GetWindowDrawList();
    draw->AddRectFilled(minimum,maximum,ImGui::GetColorU32(ImGui::IsItemHovered()?ImGuiCol_FrameBgHovered:ImGuiCol_FrameBg),ImGui::GetStyle().FrameRounding);
    float textX=minimum.x+3;
    if(material) {
        const auto imageId=assets.references().resolveParticleTexture(material->surface.baseColorTexture.image.id);const auto* image=assets.find(imageId);
        if(image && bgfx::isValid(image->thumbnail)){draw->AddImage(ImGuiBgfx::textureId(image->thumbnail),{minimum.x+3,minimum.y+3},{minimum.x+height-3,maximum.y-3});textX+=height;}
    }
    const auto label=material?assets.displayName(materialId):"Missing: "+assets.displayName(document_.definition().emitter.material.id);
    draw->PushClipRect({textX,minimum.y},{maximum.x-3,maximum.y},true);
    draw->AddText({textX,minimum.y+(height-ImGui::GetFontSize())*.5f},ImGui::GetColorU32(ImGuiCol_Text),label.c_str());draw->PopClipRect();
    ImGui::SameLine(0,gap);picker|=ImGui::Button("...##material",{height,height});
    ImGui::SameLine(0,gap);ImGui::BeginDisabled(document_.definition().emitter.material.id==DefaultParticleMaterialId);
    if(ImGui::Button("x##material",{height,height}))assign(DefaultParticleMaterialId);
    ImGui::EndDisabled();ImGui::EndGroup();
    if(ImGui::BeginDragDropTarget()) {
        if(const auto* payload=ImGui::AcceptDragDropPayload("CC_MATERIAL_ASSET")) {
            const auto* bytes=static_cast<const char*>(payload->Data);
            if(payload->DataSize>1 && bytes && bytes[payload->DataSize-1]=='\0' && std::find(bytes,bytes+payload->DataSize-1,'\0')==bytes+payload->DataSize-1)
                assign(std::string(bytes,static_cast<size_t>(payload->DataSize-1)));
            else message_="The material payload is invalid.";
        }
        ImGui::EndDragDropTarget();
    }
    static ImGuiTextFilter filter;
    if(picker){filter.Clear();ImGui::OpenPopup("Select sprite material");}
    if(ImGui::BeginPopup("Select sprite material")) {
        filter.Draw("Search");ImGui::BeginChild("Material choices",{320,ImGui::GetFontSize()*12});
        for(const auto& entry:assets.catalog())if(entry.kind==AssetKind::Material && filter.PassFilter(assets.displayName(entry.id).c_str())) {
            const auto candidate=assets.findMaterial(entry.id);if(!candidate || !particleMaterialCompatible(*candidate))continue;
            ImGui::PushID(entry.id.c_str());
            if(ImGui::Selectable(assets.displayName(entry.id).c_str(),entry.id==materialId)){assign(entry.id);ImGui::CloseCurrentPopup();}
            ImGui::PopID();
        }
        ImGui::EndChild();ImGui::EndPopup();
    }
    ImGui::BeginDisabled(!material);
    if(ImGui::SmallButton("Edit Material"))materials.requestOpen(materialId,assets);
    ImGui::EndDisabled();
}
void ParticleEditor::draw(AssetLibrary& assets,Renderer& renderer,double dt,MaterialEditor& materials) {
    if(pending_==Pending::Exit && !document_.dirty()) perform(assets);
    if(document_.isOpen() && assets.references().deleted(document_.assetId())) {
        document_.close();assets.protectParticleDocument({});assets.setParticleDocumentReferences({},{});simulation_.stop();message_="The open particle asset was deleted.";
    }
    if(document_.isOpen() && externalChanged_ && !document_.transactionActive() && pending_==Pending::None) {
        externalChanged_=false;
        if(document_.externalFileChanged()) {
            if(document_.dirty()) externalPopup_=true;
            else { const auto id=document_.assetId();if(!document_.load(id)) message_=document_.status(); }
        }
    }
    if(!isOpen()) { renderer.releaseParticlePreview();modals(assets);return; }
    if(focusNext_) {
        ImGui::SetNextWindowFocus();focusNext_=false;
    }
    ImGui::SetNextWindowDockID(0,ImGuiCond_Always);
    ImGui::SetNextWindowSize({900,600},ImGuiCond_FirstUseEver);
    bool visible=true;
    const auto title=document_.definition().name+(document_.dirty()?" *":"")+"###ParticleEffect";
    const bool contents=ImGui::Begin(title.c_str(),&visible,ImGuiWindowFlags_NoDocking);
    if(!visible) request(Pending::Close,{},&assets);
    if(contents && isOpen()) {
        if(ImGui::Button("Save")) save(assets);
        ImGui::SameLine();if(ImGui::Button("Save As...")) save(assets,true);
        ImGui::SameLine();ImGui::BeginDisabled(!canUndo());if(ImGui::Button("Undo")) undo();ImGui::EndDisabled();
        ImGui::SameLine();ImGui::BeginDisabled(!canRedo());if(ImGui::Button("Redo")) redo();ImGui::EndDisabled();
        const bool canceled=document_.transactionActive() && ImGui::IsKeyPressed(ImGuiKey_Escape,false);
        if(canceled) document_.cancel();
        if(ImGui::BeginTable("Particle authoring",2,ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("Emitter",ImGuiTableColumnFlags_WidthFixed,310);
            ImGui::TableSetupColumn("Preview",ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextColumn();
            ImGui::BeginChild("Particle properties",{0,0});
            const auto control=[&](const char* label,const auto& draw) {
                auto candidate=document_.definition();
                const bool changed=draw(candidate.emitter);
                if(!canceled && (ImGui::IsItemActivated() || changed) && !document_.transactionActive()) document_.begin(label);
                if(!canceled && changed) document_.update([&](auto& asset){asset.emitter=candidate.emitter;});
                if(ImGui::IsItemDeactivated() && document_.transactionActive()) document_.commit();
            };
            ImGui::TextUnformatted("Emitter 1 · Point · World space");
            if(ImGui::CollapsingHeader("Emission",ImGuiTreeNodeFlags_DefaultOpen)) {
                control("Change emission rate",[](auto& e){return ImGui::DragFloat("Rate (particles/s)",&e.rate,.5f,0,MaximumParticleRate,"%.1f",ImGuiSliderFlags_AlwaysClamp);});
                int capacity=static_cast<int>(document_.definition().emitter.capacity);
                control("Change particle capacity",[&](auto& e){const bool changed=ImGui::DragInt("Capacity",&capacity,1,1,static_cast<int>(MaximumParticleCapacity),"%d",ImGuiSliderFlags_AlwaysClamp);if(changed)e.capacity=static_cast<uint32_t>(capacity);return changed;});
                if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip("Changing capacity rebuilds the particle pool and clears live particles.\nPlayback time and Pause/Stop state are retained.");
            }
            if(ImGui::CollapsingHeader("Initialize",ImGuiTreeNodeFlags_DefaultOpen)) {
                control("Change lifetime",[](auto& e){return ImGui::DragFloat("Lifetime (s)",&e.lifetime,.01f,.001f,3600,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
                control("Change initial velocity",[](auto& e){return ImGui::DragFloat3("Initial velocity (m/s)",e.initialVelocity.data(),.05f);});
                if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip("Launch velocity at birth, transformed by the emitter. Positive Y launches upward.\nSet it to zero for particles that start at rest and fall under gravity.");
                if(ImGui::SmallButton("Clear initial velocity"))
                    document_.edit("Clear initial velocity",[](auto& asset){asset.emitter.initialVelocity={0,0,0};});
            }
            if(ImGui::CollapsingHeader("Motion",ImGuiTreeNodeFlags_DefaultOpen)) {
                control("Change gravity",[](auto& e){return ImGui::DragFloat3("Gravity (m/s²)",e.gravity.data(),.05f);});
                if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip("Continuous acceleration in world space. Negative Y pulls downward.\nGravity changes velocity over time; it does not replace the launch velocity.");
            }
            if(ImGui::CollapsingHeader("Render",ImGuiTreeNodeFlags_DefaultOpen)) {
                materialControls(assets,materials);
                control("Change particle size",[](auto& e){return ImGui::DragFloat("Size (m)",&e.size,.005f,.001f,1000,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
                control("Change particle color",[](auto& e){return ImGui::ColorEdit3("Tint",e.color.data());});
                control("Change particle opacity",[](auto& e){return ImGui::SliderFloat("Opacity",&e.opacity,0,1,"%.3f");});
                control("Change particle intensity",[](auto& e){return ImGui::DragFloat("Intensity",&e.intensity,.05f,0,10000,"%.2f",ImGuiSliderFlags_AlwaysClamp);});
            }
            if(document_.transactionActive() && !ImGui::IsAnyItemActive()) document_.commit();
            if(!document_.status().empty()) ImGui::TextWrapped("%s",document_.status().c_str());
            if(!message_.empty()) ImGui::TextWrapped("%s",message_.c_str());
            if(!materialDiagnostic_.empty())ImGui::TextWrapped("%s",materialDiagnostic_.c_str());
            ImGui::EndChild();
            ImGui::TableNextColumn();
            if(previewRevision_!=document_.previewRevision()) {
                const auto& definition=document_.definition();
                const bool identityChanged=simulation_.definition().id!=definition.id || simulation_.definition().emitter.id!=definition.emitter.id;
                if(!previewRevision_ || identityChanged) simulation_.restart(definition,definition.defaultSeed);
                else simulation_.updateDefinition(definition);
                previewRevision_=document_.previewRevision();
            }
            if(ImGui::Button(simulation_.state()==ParticlePlaybackState::Paused?"Resume":"Play")) simulation_.play();
            ImGui::SameLine();if(ImGui::Button("Pause")) simulation_.pause();
            ImGui::SameLine();if(ImGui::Button("Restart")) simulation_.restart(document_.definition(),document_.definition().defaultSeed);
            ImGui::SameLine();if(ImGui::Button("Stop")) simulation_.stop();
            ImGui::SameLine();if(ImGui::Button("Frame")) framePreview();
            simulation_.advance(dt);
            const auto& stats=simulation_.stats();
            ImGui::Text("%.2f s · %zu live / %u · Seed %llu",simulation_.elapsed(),stats.live,document_.definition().emitter.capacity,static_cast<unsigned long long>(simulation_.seed()));
            if(stats.droppedSpawns || stats.droppedCatchUpTicks)
                ImGui::TextWrapped("Dropped: %llu births, %llu catch-up ticks",static_cast<unsigned long long>(stats.droppedSpawns),static_cast<unsigned long long>(stats.droppedCatchUpTicks));
            if(stats.clearedCapacityParticles)
                ImGui::Text("Capacity edits cleared %llu particles",static_cast<unsigned long long>(stats.clearedCapacityParticles));
            const auto& renderStats=renderer.particlePreviewStats();
            if(renderStats.dropped || renderStats.invalid)
                ImGui::Text("Rendering: %zu omitted, %zu invalid",renderStats.dropped,renderStats.invalid);
            if(!renderer.particleStatus().empty()) ImGui::TextWrapped("%s",renderer.particleStatus().c_str());
            ImGui::TextDisabled("MMB orbit · Shift+MMB pan · Wheel zoom. Grid is a visual guide.");
            const auto size=ImGui::GetContentRegionAvail();
            if(size.x>0 && size.y>0) {
                const auto scale=ImGui::GetWindowViewport()->FramebufferScale;
                try { renderer.resizeParticlePreview(static_cast<uint32_t>(std::max(1.f,size.x*scale.x)),static_cast<uint32_t>(std::max(1.f,size.y*scale.y))); }
                catch(const std::exception& error) { message_=std::string("Preview preparation failed: ")+error.what(); }
                const auto texture=renderer.particlePreviewTexture();
                if(bgfx::isValid(texture))ImGui::Image(ImTextureRef(ImGuiBgfx::textureId(texture)),size);
                else ImGui::Dummy(size);
                auto& io=ImGui::GetIO();
                const bool ownsInput=!io.WantTextInput && !ImGui::IsAnyItemActive() && !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopup) && !ImGui::IsKeyPressed(ImGuiKey_Escape,false);
                if(ownsInput && ImGui::IsItemHovered()) {
                    if(ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) previewGesture_=io.KeyShift?2:1;
                    if(io.MouseWheel) preview_.camera.zoom(io.MouseWheel);
                }
                if(!ImGui::IsMouseDown(ImGuiMouseButton_Middle) || !ownsInput) previewGesture_=0;
                if(previewGesture_==1) preview_.camera.orbit(io.MouseDelta.x,io.MouseDelta.y,preview_.camera.target);
                if(previewGesture_==2) preview_.camera.pan(io.MouseDelta.x,io.MouseDelta.y,size.x,size.y);
                presentation_.clear();simulation_.appendPresentation(presentation_);packets_.clear();packets_.reserve(presentation_.size());
                for(const auto& p:presentation_) packets_.push_back({p.position,p.size,p.color});
                const auto binding=assets.preparedParticleMaterial(document_.definition().emitter.material.id);
                if(!binding || !binding->ready)packets_.clear();
                materialDiagnostic_=binding?binding->diagnostic:"Particle material is unavailable.";
                renderer.renderParticlePreview(preview_,packets_,binding?std::span<const RenderParticleMaterial>(&binding->render,1):std::span<const RenderParticleMaterial>{});
            }
            ImGui::EndTable();
        }
    } else previewGesture_=0;
    ImGui::End();
    publish(assets);modals(assets);
}
}
