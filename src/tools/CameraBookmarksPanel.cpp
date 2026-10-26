#include "CameraBookmarksPanel.h"
#include <imgui.h>
#include <algorithm>
#include <cstdio>

namespace crimson {
std::optional<Camera> CameraBookmarksPanel::draw(CameraBookmarks& bookmarks,const std::string& scene,const Camera& current,bool enabled,bool& visible) {
    if(!visible)return std::nullopt;
    if(scene_!=scene){scene_=scene;name_.fill(0);renameId_=0;}
    std::optional<Camera> recalled;
    if(ImGui::Begin("Camera Bookmarks",&visible)) {
        const bool canEdit=enabled&&!scene.empty()&&bookmarks.writable();
        ImGui::BeginDisabled(!canEdit);
        ImGui::SetNextItemWidth(-ImGui::CalcTextSize("Save View").x-ImGui::GetStyle().FramePadding.x*2-ImGui::GetStyle().ItemSpacing.x);
        ImGui::InputTextWithHint("##Bookmark name","Bookmark name",name_.data(),name_.size());
        ImGui::SameLine();
        if(ImGui::Button("Save View"))if(bookmarks.add(scene,name_.data(),current))name_.fill(0);
        ImGui::EndDisabled();
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled|ImGuiHoveredFlags_ForTooltip)&&scene.empty())ImGui::SetTooltip("Save the scene first.");
        enum class Action {None,Update,Remove,Rename};Action action{};std::uint64_t actionId{};
        if(ImGui::BeginTable("##Bookmark views",1,ImGuiTableFlags_RowBg|ImGuiTableFlags_ScrollY|ImGuiTableFlags_BordersInnerH,
            {0,std::max(ImGui::GetTextLineHeightWithSpacing()*3,ImGui::GetContentRegionAvail().y-ImGui::GetTextLineHeightWithSpacing()*2)})) {
            for(const auto& view:bookmarks.views(scene)) {
                ImGui::PushID(static_cast<int>(view.id>>32));ImGui::PushID(static_cast<int>(view.id));
                ImGui::TableNextRow();ImGui::TableNextColumn();
                ImGui::BeginDisabled(!enabled);
                if(ImGui::Selectable(view.name.c_str(),false,ImGuiSelectableFlags_SpanAllColumns))recalled=view.camera;
                if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))ImGui::SetTooltip("Restore this view in the free editor camera.");
                ImGui::EndDisabled();
                if(ImGui::BeginPopupContextItem("##Bookmark actions")) {
                    ImGui::BeginDisabled(!canEdit);
                    if(ImGui::MenuItem("Update from Current View")){action=Action::Update;actionId=view.id;}
                    if(ImGui::MenuItem("Rename...")) {
                        action=Action::Rename;actionId=view.id;
                        std::snprintf(rename_.data(),rename_.size(),"%s",view.name.c_str());
                    }
                    if(ImGui::MenuItem("Delete")){action=Action::Remove;actionId=view.id;}
                    ImGui::EndDisabled();ImGui::EndPopup();
                }
                ImGui::PopID();ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if(action==Action::Update)bookmarks.update(scene,actionId,current);
        if(action==Action::Remove)bookmarks.remove(scene,actionId);
        if(action==Action::Rename){renameId_=actionId;ImGui::OpenPopup("Rename Bookmark");}
        if(ImGui::BeginPopupModal("Rename Bookmark",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
            if(ImGui::IsWindowAppearing())ImGui::SetKeyboardFocusHere();
            const bool enter=ImGui::InputText("Name",rename_.data(),rename_.size(),ImGuiInputTextFlags_EnterReturnsTrue);
            if(!bookmarks.status().empty())ImGui::TextWrapped("%s",bookmarks.status().c_str());
            const bool allowed=canEdit&&bookmarks.recall(scene,renameId_).has_value();
            ImGui::BeginDisabled(!allowed);
            if((ImGui::Button("Rename")||enter)&&allowed&&bookmarks.rename(scene,renameId_,rename_.data()))ImGui::CloseCurrentPopup();
            ImGui::EndDisabled();ImGui::SameLine();
            if(ImGui::Button("Cancel")||ImGui::IsKeyPressed(ImGuiKey_Escape,false))ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if(!bookmarks.status().empty()) {
            ImGui::TextWrapped("%s",bookmarks.status().c_str());
            if(!bookmarks.writable()&&ImGui::Button("Reload Bookmarks"))bookmarks.reload();
        }
    }
    ImGui::End();return recalled;
}
}
