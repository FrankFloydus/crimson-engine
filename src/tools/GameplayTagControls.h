#pragma once
#include "gameplay/Attributes.h"
#include "PropertyReset.h"
#include <imgui.h>
#include <algorithm>
#include <array>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace crimson {
struct GameplayTagPickerFilter {
    GameplayTag root;
    const AttributeCatalog* attributes{};
};
struct GameplayTagPickerState { std::array<char,256> search{}; };

inline bool gameplayTagPickerAllows(const GameplayTag& tag,const GameplayTagRegistry& registry,
                                   const GameplayTagPickerFilter& filter={}) {
    return registry.contains(tag) && (!filter.root.valid() || tag.matches(filter.root)) &&
        (!filter.attributes || filter.attributes->find(tag));
}
inline bool gameplayTagSearchMatches(std::string_view name,std::string_view search) {
    const auto lower=[](char c){return c>='A'&&c<='Z'?static_cast<char>(c-'A'+'a'):c;};
    return std::search(name.begin(),name.end(),search.begin(),search.end(),
        [&](char a,char b){return lower(a)==lower(b);})!=name.end() || search.empty();
}
inline std::vector<GameplayTag> gameplayTagPickerChoices(const GameplayTagRegistry& registry,
    const GameplayTagPickerFilter& filter={},std::string_view search={}) {
    std::vector<GameplayTag> result;
    for(const auto& tag:registry.tags())if(gameplayTagPickerAllows(tag,registry,filter) &&
        gameplayTagSearchMatches(tag.name(),search))result.push_back(tag);
    return result;
}
struct GameplayTagPickerNode {
    GameplayTag tag;
    bool selectable{};
    std::vector<std::size_t> children;
};
inline std::vector<GameplayTagPickerNode> gameplayTagPickerHierarchy(const GameplayTagRegistry& registry,
    const GameplayTagPickerFilter& filter={}) {
    const auto choices=gameplayTagPickerChoices(registry,filter);std::vector<GameplayTagPickerNode> nodes;
    for(const auto& tag:registry.tags()) {
        if(filter.root.valid() && !tag.matches(filter.root))continue;
        if(std::any_of(choices.begin(),choices.end(),[&](const auto& choice){return choice.matches(tag);}))
            nodes.push_back({tag,gameplayTagPickerAllows(tag,registry,filter),{}});
    }
    for(std::size_t child=0;child<nodes.size();++child) {
        const auto parent=nodes[child].tag.parent();
        for(auto& node:nodes)if(node.tag==parent){node.children.push_back(child);break;}
    }
    return nodes;
}
// Chip identity is the complete tag path, independent of its position or label.
inline bool drawGameplayTagChip(const GameplayTag& tag,bool removable=true,const char* tooltip=nullptr) {
    ImGui::PushID(tag.name().c_str());ImGui::BeginGroup();
    ImGui::AlignTextToFramePadding();ImGui::TextUnformatted(tag.name().c_str());
    if(ImGui::IsItemHovered() && tooltip)ImGui::SetTooltip("%s",tooltip);
    ImGui::SameLine(0,ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::BeginDisabled(!removable);const bool removed=ImGui::SmallButton("x");
    ImGui::EndDisabled();ImGui::EndGroup();ImGui::PopID();return removed;
}
inline std::optional<GameplayTag> drawGameplayTagPickerPopup(GameplayTagPickerState& state,
    const GameplayTagRegistry& registry,const GameplayTagPickerFilter& filter={},
    const GameplayTagContainer* selected=nullptr) {
    std::optional<GameplayTag> picked;
    ImGui::SetNextWindowSizeConstraints({260,0},{520,420});
    if(ImGui::BeginPopup("Tag choices")) {
        ImGui::SetNextItemWidth(-1);ImGui::InputTextWithHint("##search","Search",state.search.data(),state.search.size());
        if(ImGui::BeginChild("Choices",{0,260},ImGuiChildFlags_None)) {
            if(state.search[0]) {
                const auto choices=gameplayTagPickerChoices(registry,filter,state.search.data());
                for(const auto& tag:choices) {
                    ImGui::PushID(tag.name().c_str());
                    if(ImGui::Selectable(tag.name().c_str(),selected && selected->has(tag,true)))picked=tag;
                    ImGui::PopID();
                }
                if(choices.empty())ImGui::TextDisabled("No matching tags");
            } else {
                const auto nodes=gameplayTagPickerHierarchy(registry,filter);
                std::function<void(std::size_t)> draw=[&](std::size_t index) {
                    const auto& node=nodes[index];const auto& name=node.tag.name();
                    const auto separator=name.find_last_of('.');const auto* label=name.c_str()+(separator==std::string::npos?0:separator+1);
                    ImGui::PushID(name.c_str());bool open=false;
                    if(!node.children.empty()) {
                        auto flags=ImGuiTreeNodeFlags_OpenOnArrow|ImGuiTreeNodeFlags_SpanAvailWidth|
                            ImGuiTreeNodeFlags_AllowOverlap|ImGuiTreeNodeFlags_NoTreePushOnOpen;
                        if(node.tag==filter.root)flags|=ImGuiTreeNodeFlags_DefaultOpen;
                        open=ImGui::TreeNodeEx("##branch",flags,"");ImGui::SameLine(0,0);
                    } else ImGui::Indent(ImGui::GetTreeNodeToLabelSpacing());
                    if(node.selectable) {if(ImGui::Selectable(label,selected && selected->has(node.tag,true)))picked=node.tag;}
                    else ImGui::TextUnformatted(label);
                    if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",name.c_str());
                    if(node.children.empty())ImGui::Unindent(ImGui::GetTreeNodeToLabelSpacing());
                    if(open) {ImGui::TreePush("Children");for(const auto child:node.children)draw(child);ImGui::TreePop();}
                    ImGui::PopID();
                };
                for(std::size_t index=0;index<nodes.size();++index) {
                    const auto parent=nodes[index].tag.parent();
                    if(std::none_of(nodes.begin(),nodes.end(),[&](const auto& node){return node.tag==parent;}))draw(index);
                }
                if(nodes.empty())ImGui::TextDisabled("No matching tags");
            }
        }
        ImGui::EndChild();
        if(picked)ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    return picked;
}
inline bool drawGameplayTagControl(const char* label,GameplayTag& value,GameplayTagPickerState& state,
    const GameplayTagPickerFilter& filter={},const GameplayTagRegistry& registry=nativeGameplayTags(),const GameplayTag& defaults={}) {
    ImGui::PushID(label);ImGui::TextUnformatted(label);bool changed=false;
    if(value.valid()) {
        if(drawGameplayTagChip(value)){value={};changed=true;}
        if(value.valid() && !registry.contains(value))ImGui::TextDisabled("Unregistered tag: %s",value.name().c_str());
        else if(value.valid() && !gameplayTagPickerAllows(value,registry,filter))ImGui::TextDisabled("Tag outside this field: %s",value.name().c_str());
    }
    changed|=resettableProperty(value,defaults,[&]{
        if(ImGui::SmallButton("Select...")){state.search.fill(0);ImGui::OpenPopup("Tag choices");}
        return false;
    },defaults.valid()?defaults.name().c_str():"None");
    if(const auto picked=drawGameplayTagPickerPopup(state,registry,filter);picked && *picked!=value){value=*picked;changed=true;}
    ImGui::PopID();return changed;
}
inline bool drawGameplayTagContainerControl(const char* label,GameplayTagContainer& value,GameplayTagPickerState& state,
    const GameplayTagPickerFilter& filter={},const GameplayTagRegistry& registry=nativeGameplayTags()) {
    ImGui::PushID(label);ImGui::TextUnformatted(label);bool changed=false;
    const std::vector<GameplayTag> explicitTags(value.tags().begin(),value.tags().end());
    const float start=ImGui::GetCursorPosX();bool first=true;
    for(const auto& tag:explicitTags) {
        const float width=ImGui::CalcTextSize(tag.name().c_str()).x+ImGui::GetStyle().ItemInnerSpacing.x+
            ImGui::CalcTextSize("x").x+ImGui::GetStyle().FramePadding.x*2;
        if(!first && ImGui::GetContentRegionAvail().x<width)ImGui::NewLine();
        if(drawGameplayTagChip(tag)){value.remove(tag);changed=true;}
        first=false;ImGui::SameLine();
    }
    if(!first){ImGui::NewLine();ImGui::SetCursorPosX(start);}
    changed|=resettableProperty(value,GameplayTagContainer{},[&]{
        if(ImGui::SmallButton("Add...")){state.search.fill(0);ImGui::OpenPopup("Tag choices");}
        return false;
    },"None");
    if(const auto picked=drawGameplayTagPickerPopup(state,registry,filter,&value)) {
        changed=(value.has(*picked,true)?value.remove(*picked):value.add(*picked)) || changed;
    }
    for(const auto& tag:value.tags()) {
        if(!registry.contains(tag))ImGui::TextDisabled("Unregistered tag: %s",tag.name().c_str());
        else if(!gameplayTagPickerAllows(tag,registry,filter))ImGui::TextDisabled("Tag outside this field: %s",tag.name().c_str());
    }
    ImGui::PopID();return changed;
}
}
