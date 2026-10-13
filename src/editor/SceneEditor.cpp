#include "SceneEditor.h"
#include "tools/ScenePanels.h"
#include "tools/AssetBrowser.h"
#include "assets/AssetLibrary.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cstdio>

namespace crimson {
namespace {
void layout(bool reset) {
    const auto* main = ImGui::GetMainViewport();
    const auto dock = ImGui::GetID("CrimsonDockSpace");
    auto* node = ImGui::DockBuilderGetNode(dock);
    if (reset || !node) {
        ImGui::DockBuilderRemoveNode(dock);
        ImGui::DockBuilderAddNode(dock,ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodePos(dock,main->WorkPos);
        ImGui::DockBuilderSetNodeSize(dock,main->WorkSize);
        ImGuiID center=dock,browser{},right{},outliner{},settings{};
        ImGui::DockBuilderSplitNode(center,ImGuiDir_Down,.25f,&browser,&center);
        ImGui::DockBuilderSplitNode(center,ImGuiDir_Right,.25f,&right,&center);
        ImGui::DockBuilderSplitNode(right,ImGuiDir_Up,.4f,&outliner,&settings);
        ImGui::DockBuilderDockWindow("Viewport",center);
        ImGui::DockBuilderDockWindow("Asset Browser",browser);
        ImGui::DockBuilderDockWindow("Outliner",outliner);
        ImGui::DockBuilderDockWindow("Settings",settings);
        ImGui::DockBuilderDockWindow("Inspector",settings);
        ImGui::DockBuilderFinish(dock);
    } else if (!ImGui::FindWindowSettingsByID(ImHashStr("Outliner"))) {
        // Add the new dock to the saved Settings node without resetting authored splits.
        if (const auto* settings = ImGui::FindWindowSettingsByID(ImHashStr("Settings")); settings && settings->DockId)
            ImGui::DockBuilderDockWindow("Outliner",settings->DockId);
    }
    if (!reset && !ImGui::FindWindowSettingsByID(ImHashStr("Inspector"))) {
        if (const auto* saved=ImGui::FindWindowSettingsByID(ImHashStr("Settings")); saved && saved->DockId) {
            ImGui::DockBuilderDockWindow("Inspector",saved->DockId);
            if (auto* inspectorDock=ImGui::DockBuilderGetNode(saved->DockId)) inspectorDock->SelectedTabId=ImHashStr("Inspector");
        }
    }
    ImGui::DockSpaceOverViewport(dock,main);
}
}
void SceneEditor::cancelInteraction(SceneDocument& document, Renderer& renderer) {
    viewport_.cancel(document,renderer);
    if (document.transactionActive()) document.cancel();
}
void SceneEditor::perform(SceneDocument& document, Camera& camera, Renderer& renderer) {
    static_cast<void>(camera); // EditorSession owns camera transitions.
    cancelInteraction(document,renderer);
    if (pending_ == Pending::New) { document.newScene(); documentReplaced_=true; }
    else if (pending_ == Pending::Open) documentReplaced_=document.load(pendingAsset_);
    else if (pending_ == Pending::Exit) exitAccepted_=true;
    pending_=Pending::None; pendingAsset_.clear();
}
void SceneEditor::request(Pending operation, const std::string& asset, SceneDocument& document,
                          Camera& camera, Renderer& renderer) {
    if (pending_ != Pending::None) return;
    if (viewport_.interacting()) cancelInteraction(document,renderer);
    else { document.commit(); viewport_.cancel(document,renderer); }
    pending_=operation; pendingAsset_=asset;
    if (document.dirty()) dirtyPopup_=true;
    else perform(document,camera,renderer);
}
void SceneEditor::save(SceneDocument& document, AssetLibrary& assets, bool saveAs) {
    if (viewport_.interacting()) return;
    if (document.transactionActive()) document.commit();
    if (saveAs || document.assetId().empty()) {
        saveAs_=saveAs; savePopup_=true;
        std::snprintf(sceneName_.data(),sceneName_.size(),"%s",document.displayName() == "Untitled" ? "New Scene" : document.displayName().c_str());
    } else if (document.save()) assets.refreshScenes();
}
void SceneEditor::modals(SceneDocument& document, Camera& camera, AssetLibrary& assets, Renderer& renderer) {
    if (dirtyPopup_) { ImGui::OpenPopup("Unsaved scene"); dirtyPopup_=false; }
    if (ImGui::BeginPopupModal("Unsaved scene",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Save changes to the current scene?");
        if (ImGui::Button("Save")) {
            ImGui::CloseCurrentPopup(); continueAfterSave_=true;
            save(document,assets,false);
            if (!savePopup_ && !document.dirty()) { perform(document,camera,renderer); continueAfterSave_=false; }
            else if (!savePopup_) { dirtyPopup_=true; continueAfterSave_=false; }
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard")) { perform(document,camera,renderer); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) { pending_=Pending::None; pendingAsset_.clear(); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    if (savePopup_) { ImGui::OpenPopup("Save scene"); savePopup_=false; }
    if (ImGui::BeginPopupModal("Save scene",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter = ImGui::InputText("Name",sceneName_.data(),sceneName_.size(),ImGuiInputTextFlags_EnterReturnsTrue);
        if ((ImGui::Button("Save") || enter) && sceneName_[0] && document.save(sceneName_.data(),saveAs_)) {
            assets.refreshScenes(); ImGui::CloseCurrentPopup();
            if (continueAfterSave_) perform(document,camera,renderer);
            continueAfterSave_=false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) { pending_=Pending::None; continueAfterSave_=false; ImGui::CloseCurrentPopup(); }
        if (!document.status().empty()) ImGui::TextWrapped("%s",document.status().c_str());
        ImGui::EndPopup();
    }
    if (deletePopup_) { ImGui::OpenPopup("Delete scene asset"); deletePopup_=false; }
    if (ImGui::BeginPopupModal("Delete scene asset",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Delete this saved scene? Models and textures remain available.");
        if (deleteAsset_ == document.assetId() && document.dirty()) ImGui::TextUnformatted("The current unsaved scene edits will also be discarded.");
        if (ImGui::Button("Delete")) {
            const bool active = deleteAsset_ == document.assetId();
            if (assets.deleteScene(deleteAsset_)) {
                if (active) { cancelInteraction(document,renderer); document.newScene(); documentReplaced_=true; }
                deleteAsset_.clear(); ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) { deleteAsset_.clear(); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}
EditorFrame SceneEditor::draw(SceneDocument& document, SceneSettings& settings, AssetLibrary& assets,
                              Renderer& renderer, SDL_Window* window, bool navigationActive,
                              uint32_t bgfxFrame, bool quitRequested) {
    bool reset=false;
    documentReplaced_=false;
    if (quitRequested) request(Pending::Exit,{},document,settings.camera,renderer);
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Scene")) {
            if (ImGui::MenuItem("New scene")) request(Pending::New,{},document,settings.camera,renderer);
            if (ImGui::MenuItem("Save","Ctrl+S",false,!viewport_.interacting())) save(document,assets,false);
            if (ImGui::MenuItem("Save as...","Ctrl+Shift+S",false,!viewport_.interacting())) save(document,assets,true);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            const auto undo="Undo " + document.history().undoLabel(), redo="Redo " + document.history().redoLabel();
            if (ImGui::MenuItem(undo.c_str(),"Ctrl+Z",false,document.history().canUndo() && !document.transactionActive())) document.undo();
            if (ImGui::MenuItem(redo.c_str(),"Ctrl+Y",false,document.history().canRedo() && !document.transactionActive())) document.redo();
            if (ImGui::MenuItem("Duplicate selection","Ctrl+D",false,!document.selection().ids.empty() && !document.transactionActive())) document.duplicateSelection();
            if (ImGui::MenuItem("Delete selection","Delete",false,!document.selection().ids.empty() && !document.transactionActive())) document.eraseSelection();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Layout")) { reset=ImGui::MenuItem("Reset layout"); ImGui::EndMenu(); }
        ImGui::Text("%s%s",document.displayName().c_str(),document.dirty() ? " *" : "");
        ImGui::EndMainMenuBar();
    }
    layout(reset);
    drawSceneInspector(document,assets,renderer,!viewport_.interacting());
    drawSceneSettings(document,assets,renderer,!viewport_.interacting());
    drawSceneOutliner(document,!viewport_.interacting());
    auto frame=viewport_.draw(settings.camera,document,assets,renderer,navigationActive,bgfxFrame);
    const auto browser=browser_.draw(assets,window);
    if (browser.newScene) request(Pending::New,{},document,settings.camera,renderer);
    if (!browser.openScene.empty()) request(Pending::Open,browser.openScene,document,settings.camera,renderer);
    if (!browser.deleteScene.empty()) { deleteAsset_=browser.deleteScene; deletePopup_=true; }
    const auto& io=ImGui::GetIO();
    const auto* navWindow=ImGui::GetCurrentContext()->NavWindow;
    const auto* rootWindow=navWindow ? navWindow->RootWindow : nullptr;
    const bool entityFocus=rootWindow && (std::string_view(rootWindow->Name)=="Viewport" ||
        std::string_view(rootWindow->Name)=="Outliner" || std::string_view(rootWindow->Name)=="Inspector");
    const bool keyboard = !io.WantTextInput && !ImGui::IsAnyItemActive() && !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopup) &&
        !navigationActive && !ImGui::IsMouseDown(ImGuiMouseButton_Right) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle) && ImGui::GetDragDropPayload() == nullptr;
    if (keyboard) {
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S,false)) save(document,assets,io.KeyShift);
        if (entityFocus && !document.transactionActive()) {
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z,false)) { if (io.KeyShift) document.redo(); else document.undo(); }
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y,false)) document.redo();
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D,false)) document.duplicateSelection();
            if (ImGui::IsKeyPressed(ImGuiKey_Delete,false)) document.eraseSelection();
            if (ImGui::IsKeyPressed(ImGuiKey_F,false) && !frame.viewport.hovered && frame.viewport.height > 0)
                viewport_.focus(settings.camera,document,assets,frame.viewport.width/frame.viewport.height);
        }
    }
    modals(document,settings.camera,assets,renderer);
    frame.viewport.inputBlocked |= io.WantTextInput || ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopup) ||
        ImGui::GetDragDropPayload() != nullptr || document.transactionActive();
    frame.documentReplaced=documentReplaced_;
    return frame;
}
}
