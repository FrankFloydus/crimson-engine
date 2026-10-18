#include "CommandBar.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cstdio>

namespace crimson {
namespace {
int commandInputCallback(ImGuiInputTextCallbackData* data) {
    auto& console=*static_cast<CommandConsole*>(data->UserData);
    if(console.text()!=data->Buf)console.setText(data->Buf);
    bool replaced=false;
    if(data->EventFlag==ImGuiInputTextFlags_CallbackCompletion)replaced=console.acceptCompletion();
    else if(data->EventFlag==ImGuiInputTextFlags_CallbackHistory)
        replaced=data->EventKey==ImGuiKey_UpArrow?console.previousHistory():console.nextHistory();
    if(replaced){data->DeleteChars(0,data->BufTextLen);data->InsertChars(0,console.text().c_str());}
    return 0;
}
}
void CommandBar::releaseFocus() {
    if(inputId_&&ImGui::GetActiveID()==inputId_)ImGui::ClearActiveID();
    auto& context=*ImGui::GetCurrentContext();
    if(context.NavNextActivateId==inputId_){context.NavNextActivateId=0;context.NavNextActivateFlags=ImGuiActivateFlags_None;}
    completionsVisible_=false;
}
void CommandBar::requestFocus() {
    std::snprintf(input_.data(),input_.size(),"%s",console_.text().c_str());
    if(auto* state=ImGui::GetInputTextState(inputId_))state->ReloadUserBufAndMoveToEnd();
    // Queue activation at the selection/submission boundary. Deferring a
    // SetKeyboardFocusHere call until the next draw would cost another frame.
    ImGui::ActivateItemByID(inputId_);
    ImGui::GetCurrentContext()->NavNextActivateFlags=ImGuiActivateFlags_PreferInput|ImGuiActivateFlags_TryToPreserveState;
}
CommandBarFrame CommandBar::draw() {
    CommandBarFrame result;const auto* viewport=ImGui::GetMainViewport();
    const float height=ImGui::GetFrameHeight()+ImGui::GetStyle().WindowPadding.y*2;
    const auto flags=ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse;
    ImVec2 inputMinimum{},inputMaximum{};bool focused=false,hoveredCompletion=false;
    if(const auto* hovered=ImGui::GetCurrentContext()->HoveredWindow)
        hoveredCompletion=hovered->ID==ImHashStr("##CommandCompletions");
    ImGui::SetNextWindowViewport(viewport->ID);
    if(ImGui::BeginViewportSideBar("##CommandBar",ImGui::GetMainViewport(),ImGuiDir_Down,height,flags)) {
        ImGui::AlignTextToFramePadding();ImGui::TextUnformatted("Cmd");ImGui::SameLine();
        const float available=ImGui::GetContentRegionAvail().x;
        const float statusWidth=console_.status().empty()?0.f:std::min(ImGui::CalcTextSize(console_.status().c_str()).x,available*.45f)+ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetNextItemWidth(std::max(1.f,available-statusWidth));
        std::snprintf(input_.data(),input_.size(),"%s",console_.text().c_str());
        constexpr auto inputFlags=ImGuiInputTextFlags_EnterReturnsTrue|ImGuiInputTextFlags_CallbackCompletion|
            ImGuiInputTextFlags_CallbackHistory|ImGuiInputTextFlags_CallbackEdit;
        const bool submit=ImGui::InputTextWithHint("##Command","Enter Console Command",input_.data(),input_.size(),inputFlags,commandInputCallback,&console_);
        inputId_=ImGui::GetItemID();inputMinimum=ImGui::GetItemRectMin();inputMaximum=ImGui::GetItemRectMax();focused=ImGui::IsItemActive();
        if(console_.text()!=input_.data())console_.setText(input_.data());
        if(submit) {
            result.command=console_.submit();result.argument=console_.argument();input_.fill(0);
            if(result.command){releaseFocus();focused=false;}
            else {requestFocus();focused=true;}
        }
        if(!console_.status().empty()) {
            if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",console_.status().c_str());
            ImGui::SameLine();ImGui::TextDisabled("%s",console_.status().c_str());
            if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",console_.status().c_str());
        }
    }
    ImGui::End();
    const auto choices=console_.completions();
    const bool show=!choices.empty()&&(focused||(completionsVisible_&&hoveredCompletion));
    completionsVisible_=show;
    if(show) {
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::SetNextWindowPos({inputMinimum.x,inputMinimum.y-ImGui::GetStyle().ItemSpacing.y},ImGuiCond_Always,{0,1});
        ImGui::SetNextWindowSize({inputMaximum.x-inputMinimum.x,0});
        ImGui::SetNextWindowSizeConstraints({0,0},{FLT_MAX,ImGui::GetTextLineHeightWithSpacing()*10+ImGui::GetStyle().WindowPadding.y*2});
        const auto popupFlags=ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoSavedSettings|
            ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoFocusOnAppearing|ImGuiWindowFlags_NoNavFocus;
        if(ImGui::Begin("##CommandCompletions",nullptr,popupFlags)) {
            ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
            for(std::size_t index=0;index<choices.size();++index)if(ImGui::Selectable(choices[index].c_str())) {
                console_.acceptCompletion(index);requestFocus();focused=true;
            }
        }
        ImGui::End();
    }
    result.inputActive=focused;return result;
}
}
