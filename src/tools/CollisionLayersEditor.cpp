#include "CollisionLayersEditor.h"
#include "DocumentEditorMenu.h"
#include "resources/ResourceManager.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <cstdio>

namespace crimson {
bool CollisionLayersEditor::focused() const {
    const auto* window=ImGui::GetCurrentContext()->NavWindow;
    return open_ && window && window->RootWindow->ID==ImHashStr("###CollisionLayers");
}
void CollisionLayersEditor::finishInteraction() {
    if(!document_.transactionActive())return;
    if(!document_.commit())return;
    if(activeItem_ && ImGui::GetActiveID()==activeItem_)ImGui::ClearActiveID();
    activeSlot_=32;activeItem_=0;suppressFinalUpdate_=true;
}
void CollisionLayersEditor::cancelInteraction() {
    if(!document_.transactionActive())return;
    document_.cancel();
    if(activeItem_ && ImGui::GetActiveID()==activeItem_)ImGui::ClearActiveID();
    activeSlot_=32;activeItem_=0;suppressFinalUpdate_=true;
}
void CollisionLayersEditor::undo(){finishInteraction();document_.undo();}
void CollisionLayersEditor::redo(){finishInteraction();document_.redo();}
void CollisionLayersEditor::requestOpen(){open_=true;focusNext_=true;}
void CollisionLayersEditor::request(Pending pending) {
    if(pending_!=Pending::None)return;
    finishInteraction();if(document_.transactionActive())return;
    pending_=pending;if(document_.dirty())dirtyPopup_=true;else perform();
}
void CollisionLayersEditor::requestExit(){if(!exitReady_)request(Pending::Exit);}
void CollisionLayersEditor::perform() {
    const auto pending=pending_;pending_=Pending::None;continueAfterSave_=false;
    if(pending==Pending::Close)open_=false;
    if(pending==Pending::Exit)exitReady_=true;
}
void CollisionLayersEditor::save() {
    finishInteraction();if(document_.transactionActive())return;
    if(document_.externalFileChanged()){externalPopup_=true;return;}
    document_.save();
}
void CollisionLayersEditor::filesChanged(std::span<const std::filesystem::path> paths) {
    for(const auto& path:paths)if(resourcePathKey(path)==resourcePathKey(document_.filePath()))externalChanged_=true;
}
void CollisionLayersEditor::modals() {
    if(dirtyPopup_){ImGui::OpenPopup("Unsaved collision layers");dirtyPopup_=false;}
    if(ImGui::BeginPopupModal("Unsaved collision layers",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Save changes to collision layer names?");
        if(ImGui::Button("Save")) {
            continueAfterSave_=true;save();
            if(!document_.dirty()){perform();ImGui::CloseCurrentPopup();}
            else if(externalPopup_)ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();if(ImGui::Button("Discard")) {
            if(document_.discard()){perform();ImGui::CloseCurrentPopup();}
        }
        ImGui::SameLine();if(ImGui::Button("Cancel")){pending_=Pending::None;continueAfterSave_=false;ImGui::CloseCurrentPopup();}
        if(!document_.status().empty())ImGui::TextWrapped("%s",document_.status().c_str());
        ImGui::EndPopup();
    }
    if(externalPopup_){ImGui::OpenPopup("Collision layers changed on disk");externalPopup_=false;}
    if(ImGui::BeginPopupModal("Collision layers changed on disk",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Collision layer names changed on disk.");
        if(ImGui::Button("Overwrite")) {
            if(document_.save(true)){if(continueAfterSave_)perform();continueAfterSave_=false;ImGui::CloseCurrentPopup();}
        }
        ImGui::SameLine();if(ImGui::Button("Reload and discard edits")) {
            if(document_.reload(true)){if(continueAfterSave_)perform();continueAfterSave_=false;ImGui::CloseCurrentPopup();}
        }
        ImGui::SameLine();if(ImGui::Button("Keep editing")){pending_=Pending::None;continueAfterSave_=false;ImGui::CloseCurrentPopup();}
        if(!document_.status().empty())ImGui::TextWrapped("%s",document_.status().c_str());
        ImGui::EndPopup();
    }
}
void CollisionLayersEditor::draw() {
    if(externalChanged_ && !document_.transactionActive()) {
        externalChanged_=false;
        if(document_.externalFileChanged()) {
            if(document_.dirty())externalPopup_=true;
            else document_.reload();
        }
    }
    if(!open_){modals();return;}
    if(focusNext_){ImGui::SetNextWindowFocus();focusNext_=false;}
    ImGui::SetNextWindowSize({460,600},ImGuiCond_FirstUseEver);bool open=true;
    const auto title=std::string("Collision Layers")+(document_.dirty()?" *":"")+"###CollisionLayers";
    if(ImGui::Begin(title.c_str(),&open,DocumentEditorWindowFlags)) {
        const auto action=drawDocumentEditorMenuBar([&]{finishInteraction();},[&]{return DocumentEditorMenuState{
            true,false,document_.canUndo(),document_.canRedo(),document_.undoLabel(),document_.redoLabel()};});
        if(action==DocumentEditorAction::Save)save();
        if(action==DocumentEditorAction::Close)request(Pending::Close);
        if(action==DocumentEditorAction::Undo)undo();
        if(action==DocumentEditorAction::Redo)redo();
        const bool canceled=suppressFinalUpdate_ || (document_.transactionActive() &&
            (ImGui::IsKeyPressed(ImGuiKey_Escape,false) || ImGui::GetIO().AppFocusLost));
        if(canceled)cancelInteraction();suppressFinalUpdate_=false;
        if(ImGui::CollapsingHeader("Layers",ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::BeginChild("Layer names",{0,-ImGui::GetFrameHeightWithSpacing()*2});
            for(unsigned slot=0;slot<32;++slot) {
                if(activeSlot_!=slot)std::snprintf(text_[slot].data(),text_[slot].size(),"%s",document_.names()[slot].c_str());
                ImGui::PushID(static_cast<int>(slot));ImGui::AlignTextToFramePadding();ImGui::Text("%u",slot+1);ImGui::SameLine();ImGui::SetNextItemWidth(-1);
                const auto hint="Layer "+std::to_string(slot+1);
                const bool changed=ImGui::InputTextWithHint("##name",hint.c_str(),text_[slot].data(),text_[slot].size());
                if(!canceled && (ImGui::IsItemActivated() || changed)) {
                    if(activeSlot_!=slot) {
                        finishInteraction();
                        if(document_.begin("Rename collision layer")){activeSlot_=slot;activeItem_=ImGui::GetItemID();suppressFinalUpdate_=false;}
                    }
                    if(changed && activeSlot_==slot)document_.updateName(slot,text_[slot].data());
                }
                if(activeSlot_==slot && ImGui::IsItemDeactivated())finishInteraction();
                ImGui::PopID();
            }
            ImGui::EndChild();
        } else finishInteraction();
        if(!document_.status().empty())ImGui::TextWrapped("%s",document_.status().c_str());
    } else finishInteraction();
    ImGui::End();if(!open)request(Pending::Close);modals();
}
}
