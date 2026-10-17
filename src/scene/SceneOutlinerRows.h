#pragma once

#include "SceneDocument.h"

namespace crimson {
// One row per authored entity, retaining its real hierarchy. Only an Actor's
// direct following camera is edited inline rather than as a duplicate row.
struct SceneOutlinerRow {
    EntityId entity{};
    EntityId parent{};
    bool operator==(const SceneOutlinerRow&) const = default;
};
bool isSceneGroup(const EntityRecord&);
// Stable hierarchy preorder. A child of an inline actor camera uses its nearest
// visible ancestor; model and particle attachments keep their real rows.
std::vector<SceneOutlinerRow> sceneOutlinerRows(const Scene&);
// A direct Actor-follow camera selects its actor. Authored models, emitters and
// other entities always retain their own authoring identity.
EntityId sceneEntityOwner(const Scene&,EntityId);
// The selected entity first. For an Actor (or its inline following camera), also
// include owned visual and camera properties in authored hierarchy order.
std::vector<EntityId> sceneEntityParts(const Scene&,EntityId);

struct SceneOutlinerColorTarget {
    EntityId entity{};
    bool operator==(const SceneOutlinerColorTarget&) const = default;
};
std::optional<Vec4> sceneOutlinerColor(const EntityRecord&);
bool setSceneOutlinerColors(SceneDocument&,std::span<const SceneOutlinerColorTarget>,std::optional<Vec4> color);
}
