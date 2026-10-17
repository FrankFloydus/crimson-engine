#include "SceneOutlinerColor.h"
#include <imgui.h>
#include <algorithm>

namespace crimson {
namespace {
bool present(const SceneDocument& document,const SceneOutlinerColorTarget& target) {
    const auto record=document.scene().find(target.entity);
    return record.has_value();
}
ImVec4 rgba(const Vec4& value){return {value[0],value[1],value[2],value[3]};}
}
SceneOutlinerRowColorScope::SceneOutlinerRowColorScope(std::optional<Vec4> color):colored_(color.has_value()) {
    if(!color)return;
    const auto low=ImGui::GetCursorScreenPos();
    const ImVec2 high{low.x+ImGui::GetContentRegionAvail().x,low.y+ImGui::GetFrameHeight()};
    ImGui::GetWindowDrawList()->AddRectFilled(low,high,ImGui::GetColorU32(rgba(*color)));
    const auto background=ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    const float alpha=std::clamp((*color)[3],0.f,1.f);
    const float red=(*color)[0]*alpha+background.x*(1-alpha);
    const float green=(*color)[1]*alpha+background.y*(1-alpha);
    const float blue=(*color)[2]*alpha+background.z*(1-alpha);
    // Perceived luminance in the editor's displayed color space is sufficient
    // for choosing readable black/white text over the composited row color.
    const bool dark=.2126f*red+.7152f*green+.0722f*blue<.5f;
    const ImVec4 text=dark?ImVec4{1,1,1,1}:ImVec4{0,0,0,1};
    contrast_=ImGui::GetColorU32(text);
    ImGui::PushStyleColor(ImGuiCol_Text,text);
    // The explicit fill stays visible in every state. Selection and hover use
    // an outline rather than replacing its semantic color with a blue header.
    for(const auto id:{ImGuiCol_Header,ImGuiCol_HeaderHovered,ImGuiCol_HeaderActive})ImGui::PushStyleColor(id,ImVec4{0,0,0,0});
}
SceneOutlinerRowColorScope::~SceneOutlinerRowColorScope(){if(colored_)ImGui::PopStyleColor(4);}
void SceneOutlinerRowColorScope::finish(bool selected) const {
    if(!colored_)return;
    auto* draw=ImGui::GetWindowDrawList();const auto low=ImGui::GetItemRectMin(),high=ImGui::GetItemRectMax();
    if(selected) {
        draw->AddRect(low,high,contrast_,0.f,ImDrawFlags_None,1.5f);
        draw->AddRectFilled(low,{low.x+3.f,high.y},contrast_);
    } else if(ImGui::IsItemHovered())draw->AddRect(low,high,contrast_,0.f,ImDrawFlags_None,1.f);
}

bool requestSceneOutlinerColor(SceneDocument& document,SceneOutlinerColorPopupState& state,
                               std::vector<SceneOutlinerColorTarget> targets) {
    if(targets.empty() || document.transactionActive())return false;
    for(const auto& target:targets)if(!present(document,target))return false;
    state={};state.document=&document;state.documentGeneration=document.documentGeneration();
    state.targets=std::move(targets);state.pending=true;
    const auto first=*document.scene().find(state.targets.front().entity);
    const auto existing=sceneOutlinerColor(first);
    if(existing)state.color=*existing;
    else {
        const auto initial=ImGui::GetStyleColorVec4(ImGuiCol_Header);
        state.color={initial.x,initial.y,initial.z,1.f};
    }
    return true;
}
bool drawSceneOutlinerColorPopup(SceneDocument& document,SceneOutlinerColorPopupState& state) {
    const bool current=state.document==&document && state.documentGeneration==document.documentGeneration();
    if(state.pending) {
        state.pending=false;
        if(current)ImGui::OpenPopup(SceneOutlinerColorPopup);
    }
    bool applied=false;
    if(ImGui::BeginPopup(SceneOutlinerColorPopup)) {
        if(!current || state.targets.empty() || ImGui::IsKeyPressed(ImGuiKey_Escape,false))ImGui::CloseCurrentPopup();
        else {
            ImGui::SetNextItemWidth(ImGui::GetFontSize()*16.f);
            if(ImGui::ColorPicker4("##color",state.color.data(),ImGuiColorEditFlags_AlphaBar|ImGuiColorEditFlags_AlphaPreviewHalf|
                ImGuiColorEditFlags_DisplayRGB|ImGuiColorEditFlags_NoSidePreview))state.error.clear();
            if(ImGui::Button("Apply")) {
                const bool valid=std::all_of(state.targets.begin(),state.targets.end(),[&](const auto& target){return present(document,target);});
                const bool validColor=math::finite(state.color)&&std::all_of(state.color.begin(),state.color.end(),[](const auto channel){return channel>=0&&channel<=1;});
                if(!validColor)state.error="Enter a color between 0 and 1.";
                else if(valid && !document.transactionActive()) {
                    applied=setSceneOutlinerColors(document,state.targets,state.color);
                    ImGui::CloseCurrentPopup();
                } else state.error="The selected rows are no longer available.";
            }
            ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();
            if(!state.error.empty())ImGui::TextUnformatted(state.error.c_str());
        }
        ImGui::EndPopup();
    }
    if(!ImGui::IsPopupOpen(SceneOutlinerColorPopup))state.targets.clear();
    return applied;
}
}
