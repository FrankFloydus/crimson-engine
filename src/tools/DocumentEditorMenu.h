#pragma once

#include <imgui.h>
#include <string>

namespace crimson {

inline constexpr ImGuiWindowFlags DocumentEditorWindowFlags=ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_MenuBar;

enum class DocumentEditorAction { None,Save,SaveAs,Close,Undo,Redo };

struct DocumentEditorMenuState {
    bool canSave{},canSaveAs{},canUndo{},canRedo{};
    std::string undoLabel,redoLabel;
};

// Asset editors share presentation, while their explicit document owners retain
// transactions, persistence, dirty/conflict dialogs, and shortcut routing.
// Read state after finishing an active edit so it is immediately undoable.
template<class FinishInteraction,class ReadState,class EnvironmentMenu>
DocumentEditorAction drawDocumentEditorMenuBar(FinishInteraction&& finishInteraction,ReadState&& readState,
                                              EnvironmentMenu&& environmentMenu) {
    auto action=DocumentEditorAction::None;
    if(!ImGui::BeginMenuBar())return action;
    if(ImGui::BeginMenu("File")) {
        finishInteraction();
        const auto state=readState();
        if(ImGui::MenuItem("Save","Ctrl+S",false,state.canSave))action=DocumentEditorAction::Save;
        if(ImGui::MenuItem("Save As...","Ctrl+Shift+S",false,state.canSaveAs))action=DocumentEditorAction::SaveAs;
        if(ImGui::MenuItem("Close"))action=DocumentEditorAction::Close;
        ImGui::EndMenu();
    }
    if(ImGui::BeginMenu("Edit")) {
        finishInteraction();
        const auto state=readState();
        const auto undo=state.canUndo && !state.undoLabel.empty()?"Undo "+state.undoLabel:"Undo";
        const auto redo=state.canRedo && !state.redoLabel.empty()?"Redo "+state.redoLabel:"Redo";
        if(ImGui::MenuItem(undo.c_str(),"Ctrl+Z",false,state.canUndo))action=DocumentEditorAction::Undo;
        if(ImGui::MenuItem(redo.c_str(),"Ctrl+Y",false,state.canRedo))action=DocumentEditorAction::Redo;
        ImGui::EndMenu();
    }
    if(ImGui::BeginMenu("Environment")) {
        environmentMenu();
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
    return action;
}

} // namespace crimson
