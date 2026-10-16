#pragma once

#include "scene/SceneComponents.h"
#include "scene/SceneOutlinerRows.h"
#include "SceneOutlinerColor.h"
#include <imgui.h>
#include <algorithm>

namespace crimson {
inline constexpr const char* SceneOutlinerRowPayload="CRIMSON_SCENE_ROW";
struct DraggedSceneOutlinerRow {
    const SceneDocument* document{};
    std::uint64_t documentGeneration{};
    EntityId owner{};
    SceneOutlinerRow row;
};
struct SceneOutlinerRowDrop {
    SceneOutlinerRow row;
    std::optional<SceneOutlinerRow> before;
};
struct SceneComponentRowResult { bool clicked{},open{}; };

// The actual ordered rows beneath one entity. Visual Root groups an actor's
// non-camera children; camera lens/follow are consolidated in its Inspector.
// Every entry is authored: no invisible end-of-list row adds layout height.
inline std::vector<SceneOutlinerRow> visibleSceneOutlinerRows(const EntityRecord& owner,
        std::span<const EntityRecord* const> orderedChildren) {
    std::vector<const EntityRecord*> directChildren;
    if(owner.actor) {
        directChildren.reserve(orderedChildren.size());
        for(const auto* child:orderedChildren)if(child->camera)directChildren.push_back(child);
        orderedChildren=directChildren;
    }
    auto result=sceneOutlinerRows(owner,orderedChildren);
    std::erase_if(result,[&](const auto& row) {
        return row.kind==SceneOutlinerRowKind::Component && owner.camera &&
            (row.component==SceneComponentKind::Camera || row.component==SceneComponentKind::CameraFollow);
    });
    return result;
}
inline std::vector<SceneOutlinerRow> visibleSceneOutlinerRows(const Scene& scene,EntityId owner) {
    auto result=sceneOutlinerRows(scene,owner);const auto record=scene.find(owner);
    if(!record)return {};
    std::erase_if(result,[&](const auto& row) {
        if(row.kind==SceneOutlinerRowKind::Component)
            return record->camera && (row.component==SceneComponentKind::Camera || row.component==SceneComponentKind::CameraFollow);
        const auto child=scene.find(row.child);
        return record->actor && child && !child->camera;
    });
    return result;
}

inline void setSceneOutlinerRowDragPayload(const SceneDocument& document,EntityId owner,SceneOutlinerRow row) {
    const DraggedSceneOutlinerRow payload{&document,document.documentGeneration(),owner,row};
    ImGui::SetDragDropPayload(SceneOutlinerRowPayload,&payload,sizeof(payload),ImGuiCond_Once);
}

// Components and actor-owned Camera children share this widget and drag path.
// Caller owns TreePop for an expanded row and may attach drop/context targets
// immediately afterward while this row remains the current ImGui item.
inline SceneComponentRowResult drawSceneOutlinerRow(const SceneDocument& document,EntityId owner,
        SceneOutlinerRow row,const char* label,bool selected,bool expandable=false,bool allowDrag=true,
        std::optional<Vec4> color=std::nullopt) {
    ImGui::PushID(static_cast<int>(owner>>32));ImGui::PushID(static_cast<int>(owner));
    ImGui::PushID(static_cast<int>(row.kind));
    if(row.kind==SceneOutlinerRowKind::Component)ImGui::PushID(static_cast<int>(row.component));
    else {ImGui::PushID(static_cast<int>(row.child>>32));ImGui::PushID(static_cast<int>(row.child));}
    auto flags=ImGuiTreeNodeFlags_SpanAvailWidth|ImGuiTreeNodeFlags_FramePadding|
        ImGuiTreeNodeFlags_OpenOnArrow|ImGuiTreeNodeFlags_OpenOnDoubleClick|ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if(selected)flags|=ImGuiTreeNodeFlags_Selected;
    if(!expandable)flags|=ImGuiTreeNodeFlags_Leaf;
    const auto childId=ImGui::GetID("##row");
    bool open{},clicked{};
    {
        SceneOutlinerRowColorScope appearance(color);
        open=ImGui::TreeNodeEx("##row",flags,"%s",label);
        clicked=ImGui::IsItemClicked()&&!ImGui::IsItemToggledOpen();appearance.finish(selected);
    }
    if(allowDrag && ImGui::BeginDragDropSource()) {
        setSceneOutlinerRowDragPayload(document,owner,row);
        ImGui::TextUnformatted(label);ImGui::EndDragDropSource();
    }
    if(row.kind==SceneOutlinerRowKind::Child)ImGui::PopID();
    ImGui::PopID();ImGui::PopID();ImGui::PopID();ImGui::PopID();
    if(expandable&&open)ImGui::TreePush(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(childId)));
    return {clicked,open};
}

inline SceneComponentRowResult drawSceneComponentRow(const SceneDocument& document,EntityId owner,
        SceneComponentKind component,bool selected,bool expandable=false,std::optional<Vec4> color=std::nullopt) {
    return drawSceneOutlinerRow(document,owner,SceneOutlinerRow::componentRow(component),
        sceneComponentName(component),selected,expandable,component!=SceneComponentKind::Transform,color);
}

inline bool validSceneOutlinerRowReorder(const SceneDocument& document,EntityId owner,
        const DraggedSceneOutlinerRow& dragged,std::optional<SceneOutlinerRow> before) {
    if(dragged.document!=&document || dragged.documentGeneration!=document.documentGeneration() ||
       dragged.owner!=owner || before==dragged.row)return false;
    const auto record=document.scene().find(owner);if(!record)return false;
    const auto present=[&](SceneOutlinerRow row) {
        if(row.kind==SceneOutlinerRowKind::Component)
            return row.component!=SceneComponentKind::Transform && hasSceneComponent(*record,row.component) &&
                !(record->camera && (row.component==SceneComponentKind::Camera || row.component==SceneComponentKind::CameraFollow));
        const auto child=document.scene().find(row.child);
        return child && child->parent==owner && !(record->actor&&!child->camera);
    };
    return present(dragged.row)&&(!before||present(*before));
}

// Upper/lower halves select before/after using existing bounds. The insertion
// line exists only during a valid drag and consumes no layout height.
inline std::optional<SceneOutlinerRowDrop> acceptSceneOutlinerRowReorder(const SceneDocument& document,
        EntityId owner,SceneOutlinerRow hovered,std::optional<SceneOutlinerRow> next) {
    std::optional<SceneOutlinerRowDrop> result;
    const auto low=ImGui::GetItemRectMin(),high=ImGui::GetItemRectMax();
    const bool upper=ImGui::GetIO().MousePos.y<(low.y+high.y)*.5f;
    const auto before=upper?std::optional{hovered}:next;
    if(!ImGui::BeginDragDropTarget())return result;
    if(const auto* payload=ImGui::AcceptDragDropPayload(SceneOutlinerRowPayload,
            ImGuiDragDropFlags_AcceptBeforeDelivery|ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
        if(payload->Data&&payload->DataSize==sizeof(DraggedSceneOutlinerRow)) {
            const auto dragged=*static_cast<const DraggedSceneOutlinerRow*>(payload->Data);
            if(validSceneOutlinerRowReorder(document,owner,dragged,before)) {
                const auto y=upper?low.y:high.y;
                ImGui::GetWindowDrawList()->AddLine({low.x,y},{high.x,y},ImGui::GetColorU32(ImGuiCol_DragDropTarget),2.f);
                if(payload->IsDelivery())result=SceneOutlinerRowDrop{dragged.row,before};
            }
        }
    }
    ImGui::EndDragDropTarget();return result;
}
}
