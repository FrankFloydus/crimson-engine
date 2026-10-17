#include "NavigationAgentProfileEditor.h"
#include "DocumentEditorMenu.h"
#include "assets/AssetLibrary.h"
#include "resources/Paths.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <cstdio>

namespace crimson {
bool NavigationAgentProfileEditor::focused() const {
    const auto* window=ImGui::GetCurrentContext()->NavWindow;
    return isOpen() && window && window->RootWindow->ID==ImHashStr("###NavigationAgentProfile");
}
void NavigationAgentProfileEditor::finishInteraction() {
    if(document_.transactionActive()) {
        if(!document_.commit())return;
        ImGui::ClearActiveID();suppressFinalUpdate_=true;
    }
}
void NavigationAgentProfileEditor::cancelInteraction() {
    if(document_.transactionActive()){document_.cancel();ImGui::ClearActiveID();suppressFinalUpdate_=true;}
}
void NavigationAgentProfileEditor::request(Pending pending,std::string id,AssetLibrary* assets) {
    if(pending_!=Pending::None) return;
    if(document_.transactionActive() && !document_.commit()) { message_=document_.status();return; }
    pending_=pending;pendingId_=std::move(id);
    if(document_.isOpen() && document_.dirty()) dirtyPopup_=true;
    else if(assets) perform(*assets);
    else if(pending==Pending::Exit) { pending_=Pending::None;exitReady_=true; }
}
void NavigationAgentProfileEditor::requestExit() { if(!exitReady_) request(Pending::Exit,{},nullptr); }
void NavigationAgentProfileEditor::requestOpen(std::string id,AssetLibrary& assets) {
    if(document_.isOpen() && document_.assetId()==id) { focusNext_=true;return; }
    request(Pending::Open,std::move(id),&assets);
}
void NavigationAgentProfileEditor::requestCreate(std::string folder,AssetLibrary& assets) {
    pendingFolder_=std::move(folder);request(Pending::Create,{},&assets);
}
void NavigationAgentProfileEditor::perform(AssetLibrary& assets) {
    const auto pending=pending_;
    pending_=Pending::None;
    continueAfterSave_=false;
    if(pending==Pending::Exit){exitReady_=true;return;}
    if(pending==Pending::Close) {
        document_.close();assets.protectNavigationAgentProfileDocument({});assets.refreshNavigationAgentProfiles();
        return;
    }
    message_.clear();
    bool loaded=false;
    if(pending==Pending::Open) {
        loaded=document_.load(pendingId_);
        if(!loaded && staged_)if(const auto seed=staged_(pendingId_))loaded=document_.loadSnapshot(*seed);
    }
    if(pending==Pending::Create) {
        loaded=document_.create();
        if(loaded) {
            assets.noteAssetWrite(document_.assetId());assets.refreshNavigationAgentProfiles();
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
        assets.protectNavigationAgentProfileDocument(document_.assetId());publishedRevision_=0;
        publish(assets);assets.refreshNavigationAgentProfiles();focusNext_=true;externalChanged_=false;
    } else message_=document_.status();
    pendingId_.clear();pendingFolder_.clear();
}
void NavigationAgentProfileEditor::save(AssetLibrary& assets,bool saveAs) {
    if(!document_.isOpen()) return;
    if(document_.transactionActive() && !document_.commit()) { message_=document_.status();return; }
    publish(assets);
    if(saveAs || !document_.editable()) {
        std::snprintf(saveName_.data(),saveName_.size(),"%s",document_.definition().name.c_str());savePopup_=true;return;
    }
    if(document_.externalFileChanged()) { externalPopup_=true;message_="The profile changed on disk. Choose Overwrite, Reload, or Save As.";return; }
    if(document_.save()) { assets.noteAssetWrite(document_.assetId());assets.refreshNavigationAgentProfiles();message_.clear(); }
    else message_=document_.status();
}
void NavigationAgentProfileEditor::filesChanged(std::span<const std::filesystem::path> paths) {
    if(!document_.isOpen() || !document_.editable()) return;
    try {
        const auto file=resolveAssetPath(document_.projectRoot(),document_.assetId()).lexically_normal();
        for(const auto& path:paths)if(resourcePathKey(path)==resourcePathKey(file))externalChanged_=true;
    } catch(...) { /* A staged migration profile has no file until Save. */ }
}
void NavigationAgentProfileEditor::modals(AssetLibrary& assets) {
    if(dirtyPopup_) { ImGui::OpenPopup("Unsaved navigation profile");dirtyPopup_=false; }
    if(ImGui::BeginPopupModal("Unsaved navigation profile",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
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
    if(savePopup_) { ImGui::OpenPopup("Save navigation profile as");savePopup_=false; }
    if(ImGui::BeginPopupModal("Save navigation profile as",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        if(ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter=ImGui::InputText("Name",saveName_.data(),saveName_.size(),ImGuiInputTextFlags_EnterReturnsTrue);
        if((ImGui::Button("Save") || enter) && saveName_[0]) {
            const auto previous=document_.assetId();
            const auto previousSaved=document_.savedDefinition();
            if(document_.save(saveName_.data(),true)) {
                assets.protectNavigationAgentProfileDocument(document_.assetId());
                if(previous!=DefaultNavigationAgentProfileId) {
                    auto source=previousSaved;
                    try {source=loadNavigationAgentProfile(assets.projectRoot(),previous);}
                    catch(...) { /* Missing/corrupt sources retain their saved generation. */ }
                    assets.publishNavigationAgentProfile(std::make_shared<const NavigationAgentProfile>(std::move(source)));
                }
                publish(assets);assets.noteAssetWrite(document_.assetId());assets.refreshNavigationAgentProfiles();assets.select(document_.assetId());
                if(continueAfterSave_)perform(assets);
                continueAfterSave_=false;
                ImGui::CloseCurrentPopup();
            } else message_=document_.status();
        }
        ImGui::SameLine();if(ImGui::Button("Cancel")) { if(continueAfterSave_)pending_=Pending::None;continueAfterSave_=false;ImGui::CloseCurrentPopup(); }
        if(!message_.empty()) ImGui::TextWrapped("%s",message_.c_str());
        ImGui::EndPopup();
    }
    if(externalPopup_) { ImGui::OpenPopup("Navigation profile changed on disk");externalPopup_=false; }
    if(ImGui::BeginPopupModal("Navigation profile changed on disk",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("The profile changed on disk.");
        if(ImGui::Button("Overwrite")) {
            if(document_.save()) {
                assets.noteAssetWrite(document_.assetId());assets.refreshNavigationAgentProfiles();message_.clear();
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
void NavigationAgentProfileEditor::publish(AssetLibrary& assets) {
    if(!document_.isOpen())return;
    if(publishedRevision_!=document_.revision() || publishedId_!=document_.assetId()) {
        if(document_.editable())assets.publishNavigationAgentProfile(document_.committed());
        publishedRevision_=document_.revision();publishedId_=document_.assetId();
    }
}
void NavigationAgentProfileEditor::draw(AssetLibrary& assets) {
    if(!document_.isOpen()) {modals(assets);return;}
    if(externalChanged_ && !document_.transactionActive()) {
        externalChanged_=false;
        if(document_.externalFileChanged()) {
            if(document_.dirty())externalPopup_=true;
            else if(!document_.load(document_.assetId()))message_=document_.status();
        }
    }
    if(focusNext_){ImGui::SetNextWindowFocus();focusNext_=false;}
    ImGui::SetNextWindowSize({460,300},ImGuiCond_FirstUseEver);
    bool open=true;
    const auto title=document_.definition().name+(document_.dirty()?" *":"")+"###NavigationAgentProfile";
    if(ImGui::Begin(title.c_str(),&open,DocumentEditorWindowFlags)) {
        const auto action=drawDocumentEditorMenuBar([&]{finishInteraction();},[&]{return DocumentEditorMenuState{
            document_.editable(),document_.isOpen(),document_.canUndo(),document_.canRedo(),document_.undoLabel(),document_.redoLabel()};});
        if(action==DocumentEditorAction::Save)save(assets);
        if(action==DocumentEditorAction::SaveAs)save(assets,true);
        if(action==DocumentEditorAction::Close)request(Pending::Close,{},&assets);
        if(action==DocumentEditorAction::Undo)undo();
        if(action==DocumentEditorAction::Redo)redo();
        const bool canceled=suppressFinalUpdate_ || (document_.transactionActive() &&
            (ImGui::IsKeyPressed(ImGuiKey_Escape,false) || ImGui::GetIO().AppFocusLost));
        if(canceled)cancelInteraction();suppressFinalUpdate_=false;
        if(ImGui::CollapsingHeader("Agent",ImGuiTreeNodeFlags_DefaultOpen)) {
            auto value=document_.definition().traversal;
            ImGui::BeginDisabled(!document_.editable());
            const auto field=[&](const char* label,float AgentTraversalSettings::* member,float speed,float minimum,float maximum,const char* format) {
                const bool changed=ImGui::DragFloat(label,&(value.*member),speed,minimum,maximum,format,ImGuiSliderFlags_AlwaysClamp);
                if(!canceled && (ImGui::IsItemActivated() || changed) && !document_.transactionActive())document_.begin(std::string("Change ")+label);
                if(!canceled && changed)document_.update([&](auto& profile){profile.traversal=value;});
                if(ImGui::IsItemDeactivated() || (changed && !ImGui::IsItemActive()))document_.commit();
            };
            field("Capsule Height (m)",&AgentTraversalSettings::height,.02f,2*value.radius,100.f,"%.3f");
            field("Capsule Radius (m)",&AgentTraversalSettings::radius,.01f,.001f,value.height*.5f,"%.3f");
            field("Maximum Slope (degrees)",&AgentTraversalSettings::maxSlope,.5f,0.f,89.f,"%.1f");
            field("Maximum Step (m)",&AgentTraversalSettings::maxStep,.01f,0.f,100.f,"%.3f");
            ImGui::EndDisabled();
        } else finishInteraction();
        if(!message_.empty())ImGui::TextWrapped("%s",message_.c_str());
        if(!document_.status().empty())ImGui::TextWrapped("%s",document_.status().c_str());
    } else finishInteraction();
    ImGui::End();
    if(!open)request(Pending::Close,{},&assets);
    publish(assets);modals(assets);
}
} // namespace crimson
