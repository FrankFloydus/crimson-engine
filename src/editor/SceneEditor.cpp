#include "SceneEditor.h"
#include "SceneShortcuts.h"
#include "tools/ScenePanels.h"
#include "tools/AssetBrowser.h"
#include "assets/AssetLibrary.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace crimson {
namespace {
void layout() {
    const auto* main = ImGui::GetMainViewport();
    const auto dock = ImGui::GetID("CrimsonDockSpace");
    auto* node = ImGui::DockBuilderGetNode(dock);
    if (!node) {
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
        ImGui::DockBuilderDockWindow("Inspector",settings);
        ImGui::DockBuilderFinish(dock);
    } else if (!ImGui::FindWindowSettingsByID(ImHashStr("Outliner"))) {
        // Add the new dock to the saved Settings node without resetting authored splits.
        if (const auto* settings = ImGui::FindWindowSettingsByID(ImHashStr("Settings")); settings && settings->DockId)
            ImGui::DockBuilderDockWindow("Outliner",settings->DockId);
    }
    if (!ImGui::FindWindowSettingsByID(ImHashStr("Inspector"))) {
        if (const auto* saved=ImGui::FindWindowSettingsByID(ImHashStr("Settings")); saved && saved->DockId) {
            ImGui::DockBuilderDockWindow("Inspector",saved->DockId);
            if (auto* inspectorDock=ImGui::DockBuilderGetNode(saved->DockId)) inspectorDock->SelectedTabId=ImHashStr("Inspector");
        }
    }
    static bool migrated=false;
    if (!migrated) {
        migrated=true;
        const auto* old=ImGui::FindWindowSettingsByID(ImHashStr("Settings"));
        const auto* inspector=ImGui::FindWindowSettingsByID(ImHashStr("Inspector"));
        if (old && inspector && old->DockId && old->DockId!=inspector->DockId) {
            bool shared=false;
            auto& windows=ImGui::GetCurrentContext()->SettingsWindows;
            for (auto* saved=windows.begin();saved;saved=windows.next_chunk(saved))
                if (saved->DockId==old->DockId && saved->ID!=old->ID) shared=true;
            if (auto* obsolete=ImGui::DockBuilderGetNode(old->DockId); obsolete && !obsolete->IsSplitNode() && !shared)
                ImGui::DockBuilderRemoveNode(obsolete->ID);
        }
    }
    ImGui::DockSpaceOverViewport(dock,main,ImGuiDockNodeFlags_AutoHideTabBar);
}
}
void SceneEditor::cancelInteraction(SceneDocument& document, Renderer& renderer) {
    viewport_.cancel(document,renderer);
    if (document.transactionActive()) document.cancel();
}
void SceneEditor::cancelOpen() {
    opening_={}; openingAsset_.clear(); openingDocumentId_.clear(); openingError_.clear(); openingPopup_=false;
}
void SceneEditor::pumpOpen(SceneDocument& document,Renderer& renderer) {
    if (!opening_) return;
    if (openingGeneration_!=document.documentGeneration() || openingRevision_!=document.scene().revision() ||
        openingDocumentId_!=document.assetId()) {
        openingError_="The current scene changed while the requested scene was loading. Open was canceled."; opening_={}; return;
    }
    if (opening_.state()==ResourceState::Failed) { openingError_=opening_.diagnostic(); opening_={}; return; }
    if (opening_.state()!=ResourceState::Ready || document.transactionActive() || viewport_.interacting()) return;
    if (const auto snapshot=opening_.snapshot()) {
        if (snapshot->id!=openingAsset_) { openingError_="The loaded scene has a different asset identity."; opening_={}; return; }
        viewport_.cancel(document,renderer);
        if (document.openPreparedSnapshot(*snapshot)) {
            documentReplaced_=true; cancelOpen(); ImGui::ClosePopupToLevel(0,true);
        } else { openingError_=document.status(); opening_={}; }
    }
}
void SceneEditor::perform(SceneDocument& document, Camera& camera, Renderer& renderer) {
    static_cast<void>(camera); // EditorSession owns camera transitions.
    cancelInteraction(document,renderer);
    if (pending_ == Pending::New) { document.newScene(); documentReplaced_=true; }
    else if (pending_ == Pending::Open) {
        if (editingAssets_ && editingAssets_->resources()) {
            try {
                const auto root=document.projectRoot();
                const auto id=pendingAsset_;
                ResourceRequestOptions options; options.variant="editor-open"; options.priority=ResourcePriority::Selected;
                options.dependencies={sceneAssetPath(root,id)}; options.forceReload=true;
                openingAsset_=id; openingDocumentId_=document.assetId(); openingGeneration_=document.documentGeneration();
                openingRevision_=document.scene().revision(); openingError_.clear(); openingPopup_=true;
                opening_=editingAssets_->resources()->request<SceneSnapshot>(AssetRef<SceneSnapshot>{id},[root,id](std::stop_token token) {
                    if (token.stop_requested()) throw std::runtime_error("Scene open canceled.");
                    auto snapshot=std::make_shared<SceneSnapshot>(readSceneSnapshot(root,id));
                    if (token.stop_requested()) throw std::runtime_error("Scene open canceled.");
                    return ResourceLoadResult<SceneSnapshot>{snapshot,sizeof(SceneSnapshot)+snapshot->records.size()*sizeof(EntityRecord),snapshot->fingerprint,{}};
                },std::move(options));
                if (!opening_) openingError_="The scene is unavailable or its resource was deleted.";
            } catch (const std::exception& error) { openingError_=error.what(); openingPopup_=true; }
        } else documentReplaced_=document.load(pendingAsset_);
    }
    else if (pending_ == Pending::Exit) exitAccepted_=true;
    pending_=Pending::None; pendingAsset_.clear();
}
void SceneEditor::request(Pending operation, const std::string& asset, SceneDocument& document,
                          Camera& camera, Renderer& renderer) {
    if (pending_ != Pending::None) return;
    if (opening_ || !openingError_.empty() || deletion_) {
        cancelOpen();
        if (editingAssets_) editingAssets_->cancelDeletion(); deletion_.reset(); replacements_.clear(); deletePopup_=false;
        ImGui::ClosePopupToLevel(0,true);
    }
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
    } else if (document.externalFileChanged()) saveConflictPopup_=true;
    else if (document.save()) { assets.noteAssetWrite(document.assetId()); assets.refreshScenes(); }
}
void SceneEditor::modals(SceneDocument& document, Camera& camera, AssetLibrary& assets, Renderer& renderer) {
    if (openingPopup_) { ImGui::OpenPopup("Open scene"); openingPopup_=false; }
    if (ImGui::BeginPopupModal("Open scene",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!openingError_.empty()) {
            ImGui::TextWrapped("Could not open the requested scene: %s",openingError_.c_str());
            if (ImGui::Button("Close")) { cancelOpen(); ImGui::CloseCurrentPopup(); }
        } else {
            ImGui::TextUnformatted("Opening scene...");
            if (ImGui::Button("Cancel")) { cancelOpen(); ImGui::CloseCurrentPopup(); }
        }
        ImGui::EndPopup();
    }
    if (dirtyPopup_) { ImGui::OpenPopup("Unsaved scene"); dirtyPopup_=false; }
    if (ImGui::BeginPopupModal("Unsaved scene",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Save changes to the current scene?");
        if (ImGui::Button("Save")) {
            ImGui::CloseCurrentPopup(); continueAfterSave_=true;
            save(document,assets,false);
            if (!savePopup_ && !saveConflictPopup_ && !document.dirty()) { perform(document,camera,renderer); continueAfterSave_=false; }
            else if (!savePopup_ && !saveConflictPopup_) { dirtyPopup_=true; continueAfterSave_=false; }
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
            assets.noteAssetWrite(document.assetId()); assets.refreshScenes(); ImGui::CloseCurrentPopup();
            if (continueAfterSave_) perform(document,camera,renderer);
            continueAfterSave_=false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) { pending_=Pending::None; continueAfterSave_=false; ImGui::CloseCurrentPopup(); }
        if (!document.status().empty()) ImGui::TextWrapped("%s",document.status().c_str());
        ImGui::EndPopup();
    }
    if (saveConflictPopup_) { ImGui::OpenPopup("Scene changed on disk"); saveConflictPopup_=false; }
    if (ImGui::BeginPopupModal("Scene changed on disk",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("The saved scene changed or was removed outside the editor.");
        ImGui::TextUnformatted("Choose how to preserve the version you are editing.");
        if (ImGui::Button("Overwrite")) {
            if (document.save({},false,true)) {
                assets.noteAssetWrite(document.assetId()); assets.refreshScenes(); ImGui::CloseCurrentPopup();
                if (continueAfterSave_) perform(document,camera,renderer);
                continueAfterSave_=false;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Save As")) { ImGui::CloseCurrentPopup(); save(document,assets,true); }
        ImGui::SameLine();
        if (ImGui::Button("Reload")) {
            cancelInteraction(document,renderer);
            if (document.reloadFromDisk()) {
                pending_=Pending::None; continueAfterSave_=false; externalScene_.reset(); ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) { pending_=Pending::None; continueAfterSave_=false; ImGui::CloseCurrentPopup(); }
        if (!document.status().empty()) ImGui::TextWrapped("%s",document.status().c_str());
        ImGui::EndPopup();
    }
    if (externalPopup_) { ImGui::OpenPopup("External scene changes"); externalPopup_=false; }
    if (ImGui::BeginPopupModal("External scene changes",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("The scene changed on disk while you have unsaved edits.");
        if (ImGui::Button("Reload and discard edits")) {
            cancelInteraction(document,renderer);
            if (externalScene_ && document.applyReloadSnapshot(*externalScene_)) {
                externalScene_.reset(); ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Keep editing")) { externalScene_.reset(); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("Save As")) { externalScene_.reset(); ImGui::CloseCurrentPopup(); save(document,assets,true); }
        if (!document.status().empty()) ImGui::TextWrapped("%s",document.status().c_str());
        ImGui::EndPopup();
    }
    if (deletePopup_) { ImGui::OpenPopup("Delete assets"); deletePopup_=false; }
    ImGui::SetNextWindowSizeConstraints(ImVec2(460,0),ImVec2(640,ImGui::GetMainViewport()->WorkSize.y*0.85f));
    if (ImGui::BeginPopupModal("Delete assets",nullptr,ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize)) {
        if (deletion_ && assets.deletionPending()) {
            if (assets.pumpDeletion(&document)) {
                documentReplaced_=document.assetId()!=deletionDocumentId_;
                deletion_.reset(); replacements_.clear(); deletionStatus_.clear(); ImGui::CloseCurrentPopup();
            } else if (!assets.deletionPending()) {
                // A failed validation leaves the dialog open. Refresh identities
                // and usages for the next deliberate confirmation, while the
                // library keeps the concrete failure in its status text.
                deletionStatus_=assets.status();
                deletion_=assets.planDeletion(deletion_->selectedIds,&document);
            } else {
                deletionStatus_=assets.status();
            }
        }
        if (deletion_) {
            const bool preparing=assets.deletionPending();
            const bool referenced=deletion_->needsReferenceWarning();
            const auto assetCount=deletion_->assetIds.size(),folderCount=deletion_->folderIds.size();
            if (assetCount && folderCount)
                ImGui::Text("Delete %zu asset%s and %zu folder%s?",assetCount,assetCount==1 ? "" : "s",folderCount,folderCount==1 ? "" : "s");
            else if (folderCount)
                ImGui::Text("Delete %zu folder%s?",folderCount,folderCount==1 ? "" : "s");
            else
                ImGui::Text("Delete %zu asset%s?",assetCount,assetCount==1 ? "" : "s");
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+540.0f);
            if (referenced) ImGui::TextUnformatted("Resources outside this deletion still use some selected assets. Expand the details to review replacements, or delete using the defaults.");
            if (std::find(deletion_->assetIds.begin(),deletion_->assetIds.end(),document.assetId())!=deletion_->assetIds.end() && document.dirty())
                ImGui::TextWrapped("The active scene's unsaved edits will be discarded.");
            if (deletion_->clearsActiveHistory) ImGui::TextWrapped("This global asset operation clears the active scene's undo history.");
            ImGui::BeginDisabled(preparing);
            // Confirmation stays count-only. Show just surviving external
            // dependencies, and only when the user asks to inspect them.
            if (referenced && ImGui::IsWindowAppearing()) ImGui::SetNextItemOpen(false);
            if (referenced && ImGui::CollapsingHeader("Dependencies and replacements")) {
                const auto rows=static_cast<float>(deletion_->warningUsages.size()*5+1);
                ImGui::BeginChild("Deletion details",ImVec2(560,std::min(rows*ImGui::GetFrameHeightWithSpacing(),ImGui::GetMainViewport()->WorkSize.y*0.45f)));
                for (const auto& id:deletion_->assetIds) {
                    const auto uses=std::count_if(deletion_->warningUsages.begin(),deletion_->warningUsages.end(),[&](const auto& use){return use.targetId==id;});
                    if (!uses) continue;
                    ImGui::PushID(id.c_str());
                    ImGui::SeparatorText(assets.displayName(id).c_str());
                    ImGui::Text("Used by %zu saved resource%s",static_cast<size_t>(uses),uses==1 ? "" : "s");
                    const auto entry=std::find_if(assets.catalog().begin(),assets.catalog().end(),[&](const auto& asset){return asset.id==id;});
                    if (entry!=assets.catalog().end() && entry->kind!=AssetKind::Scene) {
                        const char* fallback=entry->kind==AssetKind::Model ? "Delete model entities" : entry->kind==AssetKind::Environment ? "Built-in studio" :
                            entry->kind==AssetKind::Material ? "Default gray material" : "Default gray / neutral normal";
                        const auto chosen=replacements_.find(id);
                        const std::string chosenId=chosen==replacements_.end() ? std::string{} : chosen->second;
                        const auto label=chosenId.empty() ? std::string(fallback) : assets.displayName(chosenId);
                        if (ImGui::BeginCombo("Fallback",label.c_str())) {
                            if (ImGui::Selectable(fallback,chosenId.empty())) replacements_.erase(id);
                            for (const auto& candidate:assets.fallbackCandidates(id)) {
                                if (std::find(deletion_->assetIds.begin(),deletion_->assetIds.end(),candidate.id)!=deletion_->assetIds.end()) continue;
                                if (ImGui::Selectable(assets.displayName(candidate.id).c_str(),chosenId==candidate.id)) replacements_[id]=candidate.id;
                            }
                            ImGui::EndCombo();
                        }
                    }
                    if (ImGui::TreeNode("Used by")) {
                        for (const auto& use:deletion_->warningUsages) if (use.targetId==id)
                            ImGui::TextWrapped("%s: %s",use.ownerName.c_str(),use.slot.c_str());
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
                ImGui::EndChild();
            }
            ImGui::EndDisabled();
            if (!deletion_->error.empty()) ImGui::TextWrapped("%s",deletion_->error.c_str());
            ImGui::BeginDisabled(preparing || !deletion_->error.empty());
            if (ImGui::Button("Delete")) {
                const auto previous=document.assetId(); cancelInteraction(document,renderer);
                deletionDocumentId_=previous;
                if (assets.deleteAssets(*deletion_,replacements_,&document)) {
                    documentReplaced_=document.assetId()!=previous; deletion_.reset(); replacements_.clear(); deletionStatus_.clear(); ImGui::CloseCurrentPopup();
                } else {
                    deletionStatus_=assets.status();
                    if (!assets.deletionPending()) deletion_=assets.planDeletion(deletion_->selectedIds,&document);
                }
            }
            ImGui::EndDisabled(); ImGui::SameLine();
            if (ImGui::Button("Cancel")) { assets.cancelDeletion(); deletion_.reset(); replacements_.clear(); deletionStatus_.clear(); ImGui::CloseCurrentPopup(); }
            if (!deletionStatus_.empty()) ImGui::TextWrapped("%s",deletionStatus_.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::EndPopup();
    }
}
EditorFrame SceneEditor::draw(SceneDocument& document, SceneSettings& settings, AssetLibrary& assets,
                              Renderer& renderer, SDL_Window* window, bool navigationActive,
                              uint32_t bgfxFrame, bool quitRequested) {
    documentReplaced_=false;
    editingAssets_=&assets;
    pumpOpen(document,renderer);
    const auto previousPanels=panels_;
    bool resetLayout=false;
    document.setPreviewCamera(settings.camera);
    if (quitRequested) request(Pending::Exit,{},document,settings.camera,renderer);
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New scene")) request(Pending::New,{},document,settings.camera,renderer);
            if (ImGui::MenuItem("Save","Ctrl+S",false,!viewport_.interacting())) save(document,assets,false);
            if (ImGui::MenuItem("Save as...","Ctrl+Shift+S",false,!viewport_.interacting())) save(document,assets,true);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            // Opening the menu ends a property gesture before its Undo label
            // and enabled state are queried. Viewport gestures keep ownership.
            finishScenePanelEdit(document);
            const auto undo="Undo " + document.history().undoLabel(), redo="Redo " + document.history().redoLabel();
            if (ImGui::MenuItem(undo.c_str(),"Ctrl+Z",false,document.history().canUndo() && !document.transactionActive())) document.undo();
            if (ImGui::MenuItem(redo.c_str(),"Ctrl+Y",false,document.history().canRedo() && !document.transactionActive())) document.redo();
            if (ImGui::MenuItem("Duplicate selection","Ctrl+D",false,!document.selection().ids.empty() && !document.transactionActive())) document.duplicateSelection();
            if (ImGui::MenuItem("Delete selection","Delete",false,!document.selection().ids.empty() && !document.transactionActive())) document.eraseSelection();
            ImGui::Separator();
            if (ImGui::MenuItem("Preferences...")) preferences_.open();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Outliner",nullptr,&panels_.outliner);
            ImGui::MenuItem("Inspector",nullptr,&panels_.inspector);
            ImGui::MenuItem("Asset Browser","Ctrl+Space",&panels_.assetBrowser);
            ImGui::Separator();
            if (ImGui::MenuItem("Reset layout")) resetLayout=true;
            ImGui::EndMenu();
        }
        ImGui::Text("%s%s",document.displayName().c_str(),document.dirty() ? " *" : "");
        ImGui::EndMainMenuBar();
    }
    if (((previousPanels.inspector && !panels_.inspector) || (previousPanels.outliner && !panels_.outliner)) &&
        document.transactionActive() && !viewport_.interacting()) document.commit();
    if (resetLayout) { ImGui::DockBuilderRemoveNode(ImGui::GetID("CrimsonDockSpace")); panels_={}; }
    layout();
    preferences_.draw();
    const auto* context=ImGui::GetCurrentContext();
    const auto preferencesWindow=[&](const ImGuiWindow* window) {
        return preferences_.isOpen() && window && std::string_view(window->RootWindow->Name)=="Preferences";
    };
    const bool preferencesMouse=preferencesWindow(context->HoveredWindow) ||
        (context->ActiveId && preferencesWindow(context->ActiveIdWindow)) || preferencesWindow(context->MovingWindow);
    const bool preferencesKeyboard=preferencesMouse || preferencesWindow(context->NavWindow);
    const bool actionPending=opening_ || openingPopup_ || deletePopup_;
    if (panels_.inspector) drawSceneInspector(document,assets,renderer,!viewport_.interacting() && !actionPending);
    if (panels_.outliner) drawSceneOutliner(document,!viewport_.interacting() && !actionPending);
    auto frame=viewport_.draw(settings.camera,document,assets,renderer,navigationActive || actionPending || preferencesMouse,bgfxFrame,preferencesKeyboard);
    BrowserActions browser;
    if (panels_.assetBrowser) {
        browser=browser_.draw(assets,window,preferences_.values().browserPreviewSize,&renderer);
        preferences_.setBrowserPreviewSize(browser.previewSize);
    }
    frame.visiblePreviewIds=std::move(browser.visiblePreviewIds);
    if (browser.newScene) request(Pending::New,{},document,settings.camera,renderer);
    if (!browser.openScene.empty()) request(Pending::Open,browser.openScene,document,settings.camera,renderer);
    if (!browser.deleteAssetIds.empty()) {
        cancelOpen();
        cancelInteraction(document,renderer); deletion_=assets.planDeletion(browser.deleteAssetIds,&document);
        replacements_.clear(); deletionStatus_.clear(); deletePopup_=true;
    }
    const auto& io=ImGui::GetIO();
    const auto shortcuts=readSceneShortcuts({actionPending,preferencesKeyboard,navigationActive,
        document.transactionActive(),!frame.modelDropId.empty() || !frame.modelDropIds.empty()});
    if (shortcuts.toggleAssetBrowser) panels_.assetBrowser=!panels_.assetBrowser;
    if (shortcuts.save) save(document,assets,shortcuts.saveAs);
    if (shortcuts.undo) document.undo();
    if (shortcuts.redo) document.redo();
    if (shortcuts.duplicate) document.duplicateSelection();
    if (shortcuts.eraseSelection) document.eraseSelection();
    if (shortcuts.focusSelection && !frame.viewport.hovered && frame.viewport.height > 0)
        viewport_.focus(settings.camera,document,assets,frame.viewport.width/frame.viewport.height);
    if (externalScene_ && externalScene_->id!=document.assetId()) externalScene_.reset();
    if (externalScene_ && !viewport_.interacting() && !document.transactionActive() &&
        !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopup)) {
        if (externalScene_->fingerprint==document.loadedFingerprint()) externalScene_.reset();
        else if (document.dirty()) externalPopup_=true;
        else {
            viewport_.cancel(document,renderer);
            if (document.applyReloadSnapshot(*externalScene_)) externalScene_.reset();
        }
    }
    modals(document,settings.camera,assets,renderer);
    frame.viewport.inputBlocked |= io.WantTextInput || ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopup) ||
        ImGui::GetDragDropPayload() != nullptr || document.transactionActive() || actionPending || preferencesMouse;
    frame.documentReplaced=documentReplaced_;
    return frame;
}
}
