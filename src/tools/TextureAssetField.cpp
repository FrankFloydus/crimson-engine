#include "TextureAssetField.h"
#include "assets/AssetLibrary.h"
#include "crimson-renderer/ImGuiBgfx.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <vector>
namespace crimson {
void drawTextureAssetChoices(AssetLibrary& assets,ImGuiTextFilter& filter,const std::string& selected,const std::function<void(const std::string&)>& assign) {
    if(ImGui::IsWindowAppearing())ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(-1);
    if(ImGui::InputTextWithHint("##Search textures","Search textures...",filter.InputBuf,IM_ARRAYSIZE(filter.InputBuf)))filter.Build();
    if(ImGui::Selectable("None",selected.empty()))assign(std::string{});
    struct Choice {const TextureAsset* asset;std::string name;};
    std::vector<Choice> choices;choices.reserve(assets.assets().size());
    for(const auto& texture:assets.assets())if(!texture.hdr) {
        auto name=assets.displayName(texture.id);
        if(filter.PassFilter(name.c_str()))choices.push_back({&texture,std::move(name)});
    }
    const float edge=ImGui::GetTextLineHeight()*2.5f,padding=ImGui::GetStyle().FramePadding.y;
    const float height=edge+padding*2;
    ImGui::BeginChild("Texture choices",{std::max(360.f,ImGui::GetFontSize()*28),height*7});
    // Request thumbnail residency only for the rows that are actually shown.
    ImGuiListClipper clipper;clipper.Begin(static_cast<int>(choices.size()),height+ImGui::GetStyle().ItemSpacing.y);
    while(clipper.Step())for(int index=clipper.DisplayStart;index<clipper.DisplayEnd;++index) {
        const auto& choice=choices[static_cast<std::size_t>(index)];
        ImGui::PushID(choice.asset->id.c_str());
        if(ImGui::Selectable("##Texture",choice.asset->id==selected,ImGuiSelectableFlags_None,{0,height}))assign(choice.asset->id);
        const auto minimum=ImGui::GetItemRectMin(),maximum=ImGui::GetItemRectMax();
        auto* draw=ImGui::GetWindowDrawList();const ImVec2 well{minimum.x+padding,minimum.y+padding};
        draw->AddRectFilled(well,{well.x+edge,well.y+edge},ImGui::GetColorU32(ImGuiCol_FrameBg));
        if(const auto* texture=assets.find(choice.asset->id);texture&&bgfx::isValid(texture->thumbnail)) {
            const float scale=edge/static_cast<float>(std::max({1u,texture->width,texture->height}));
            const ImVec2 size{texture->width*scale,texture->height*scale};
            const ImVec2 image{well.x+(edge-size.x)*.5f,well.y+(edge-size.y)*.5f};
            draw->AddImage(ImGuiBgfx::textureId(texture->thumbnail),image,{image.x+size.x,image.y+size.y});
        }
        const ImVec2 text{well.x+edge+ImGui::GetStyle().ItemInnerSpacing.x,minimum.y+(height-ImGui::GetFontSize())*.5f};
        const auto nameSize=ImGui::CalcTextSize(choice.name.c_str());
        ImGui::RenderTextEllipsis(draw,text,{maximum.x-padding,text.y+ImGui::GetFontSize()},maximum.x-padding,
            choice.name.c_str(),nullptr,&nameSize);
        if(nameSize.x>maximum.x-padding-text.x&&ImGui::IsItemHovered())ImGui::SetTooltip("%s",choice.name.c_str());
        ImGui::PopID();
    }
    ImGui::EndChild();
}
// Pickers and delivery use the same strict image compatibility checks.
bool drawTextureAssetField(AssetLibrary& assets,const char* title,std::string& id,std::string& message,bool normal) {
    ImGui::PushID(title);
    bool changed=false;
    const auto assign=[&](const std::string& candidate) {
        const auto* texture=assets.find(candidate);
        if(!texture || texture->hdr) { message="Choose a regular image texture. HDR images are environment sources.";return; }
        if(candidate!=id){id=candidate;changed=true;}message.clear();
    };
    ImGui::TextUnformatted(title);
    ImGui::BeginGroup();
    const float height=ImGui::GetFrameHeight(),gap=ImGui::GetStyle().ItemInnerSpacing.x;
    const float width=std::max(height,ImGui::GetContentRegionAvail().x-height*2-gap*2);
    bool picker=ImGui::InvisibleButton("Texture asset",{width,height});
    const auto minimum=ImGui::GetItemRectMin(),maximum=ImGui::GetItemRectMax();
    auto* draw=ImGui::GetWindowDrawList();
    draw->AddRectFilled(minimum,maximum,ImGui::GetColorU32(ImGui::IsItemHovered()?ImGuiCol_FrameBgHovered:ImGuiCol_FrameBg),ImGui::GetStyle().FrameRounding);
    const auto resolved=id.empty()?std::string{}:assets.references().resolveTexture(id,normal);
    const auto* texture=resolved.empty()?nullptr:assets.find(resolved);
    float textX=minimum.x+3;
    if(texture && bgfx::isValid(texture->thumbnail)) {
        draw->AddImage(ImGuiBgfx::textureId(texture->thumbnail),{minimum.x+3,minimum.y+3},{minimum.x+height-3,maximum.y-3});textX+=height;
    }
    const auto name=id.empty()?std::string("None"):texture?assets.displayName(texture->id):"Missing: "+assets.displayName(id);
    draw->PushClipRect({textX,minimum.y},{maximum.x-3,maximum.y},true);
    draw->AddText({textX,minimum.y+(height-ImGui::GetFontSize())*.5f},ImGui::GetColorU32(ImGuiCol_Text),name.c_str());draw->PopClipRect();
    ImGui::SameLine(0,gap);picker|=ImGui::Button("...##texture",{height,height});
    ImGui::SameLine(0,gap);ImGui::BeginDisabled(id.empty());
    if(ImGui::Button("x##texture",{height,height})){id.clear();changed=true;}ImGui::EndDisabled();
    ImGui::EndGroup();
    if(ImGui::BeginDragDropTarget()) {
        if(const auto* payload=ImGui::AcceptDragDropPayload("CC_TEXTURE_ASSET")) {
            const auto* bytes=static_cast<const char*>(payload->Data);
            if(payload->DataSize>1 && bytes && bytes[payload->DataSize-1]=='\0' &&
               std::find(bytes,bytes+payload->DataSize-1,'\0')==bytes+payload->DataSize-1)
                assign(std::string(bytes,static_cast<size_t>(payload->DataSize-1)));
            else message="The texture payload is invalid.";
        }
        ImGui::EndDragDropTarget();
    }
    static ImGuiTextFilter filter;
    if(picker){filter.Clear();ImGui::OpenPopup("Select texture");}
    if(ImGui::BeginPopup("Select texture")) {
        drawTextureAssetChoices(assets,filter,id,[&](const std::string& choice) {
            if(choice.empty()){if(!id.empty()){id.clear();changed=true;}}
            else assign(choice);
            ImGui::CloseCurrentPopup();
        });
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}
}
