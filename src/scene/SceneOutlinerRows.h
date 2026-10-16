#pragma once

#include "SceneDocument.h"

namespace crimson {
enum class SceneOutlinerRowKind { Component,Child };
struct SceneOutlinerRow {
    SceneOutlinerRowKind kind{SceneOutlinerRowKind::Component};
    SceneComponentKind component{SceneComponentKind::Transform};
    EntityId child{};
    bool operator==(const SceneOutlinerRow&) const = default;
    static SceneOutlinerRow componentRow(SceneComponentKind value) {return {SceneOutlinerRowKind::Component,value,NoEntity};}
    static SceneOutlinerRow childRow(EntityId value) {return {SceneOutlinerRowKind::Child,SceneComponentKind::Transform,value};}
};
// Transform belongs to the owning entity row. All actual children are returned;
// actor visual children may be displayed within the Visual Root by the editor.
std::vector<SceneOutlinerRow> sceneOutlinerRows(const Scene&,EntityId owner);
// The editor already owns an indexed/sorted snapshot; avoid scanning the scene
// registry for each visible row by building directly from those child pointers.
std::vector<SceneOutlinerRow> sceneOutlinerRows(const EntityRecord& owner,
                                             std::span<const EntityRecord* const> orderedChildren);
// Reorder rows within their current owner, preserving actual entity hierarchy,
// transforms, camera follow/lens values and scene roles. Missing before = end.
bool reorderSceneOutlinerRow(SceneDocument&,EntityId owner,SceneOutlinerRow moving,
                             std::optional<SceneOutlinerRow> before=std::nullopt);
struct SceneOutlinerColorTarget {
    EntityId entity{};
    std::optional<SceneComponentKind> component;
    bool operator==(const SceneOutlinerColorTarget&) const = default;
};
std::optional<Vec4> sceneOutlinerColor(const EntityRecord&,std::optional<SceneComponentKind> component=std::nullopt);
bool setSceneOutlinerColors(SceneDocument&,std::span<const SceneOutlinerColorTarget>,std::optional<Vec4> color);
}
