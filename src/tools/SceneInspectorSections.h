#pragma once

#include "scene/SceneComponents.h"

namespace crimson {
// Capabilities are properties of the selected entity, never selectable rows.
// Camera Follow shares Camera's controls; Actor is ownership with no fields.
inline std::vector<SceneComponentKind> sceneInspectorSections(const EntityRecord& record) {
    constexpr std::array order{
        SceneComponentKind::Transform,SceneComponentKind::Movement,SceneComponentKind::Tags,SceneComponentKind::Effects,SceneComponentKind::Model,SceneComponentKind::Particle,
        SceneComponentKind::Controller,SceneComponentKind::Navigation,SceneComponentKind::Enemy,
        SceneComponentKind::PlayerInput,
        SceneComponentKind::Camera,SceneComponentKind::NavigationVolume,SceneComponentKind::Collider,SceneComponentKind::Directional,
        SceneComponentKind::Point,SceneComponentKind::Quader,SceneComponentKind::Environment};
    std::vector<SceneComponentKind> result;
    for(const auto section:order)if(hasSceneComponent(record,section))result.push_back(section);
    return result;
}
}
