#pragma once

#include "SceneDocument.h"
#include <array>

namespace crimson {
struct SceneComponentTarget {
    EntityId entity{};
    SceneComponentKind component{SceneComponentKind::Transform};
};
inline constexpr std::array EditableActorComponents{
    SceneComponentKind::Actor,SceneComponentKind::Controller,SceneComponentKind::Movement,
    SceneComponentKind::Navigation,SceneComponentKind::Enemy,SceneComponentKind::PlayerInput,
    SceneComponentKind::Camera,SceneComponentKind::CameraFollow,
    SceneComponentKind::Tags,SceneComponentKind::Effects,SceneComponentKind::Subdivision};
// Role recipes prepend their protected starting effect once. Later authored
// starting effects retain their application order.
void initializePlayerEffects(EntityRecord&);
void initializeEnemyEffects(EntityRecord&);
const char* sceneComponentName(SceneComponentKind);
bool hasSceneComponent(const EntityRecord&,SceneComponentKind);
std::vector<SceneComponentKind> sceneComponents(const EntityRecord&);
bool editableSceneComponent(SceneComponentKind);
// Required capabilities are added/removed together in one document command.
// Other capabilities and all entity children retain their authored values.
bool addSceneComponent(SceneDocument&,EntityId,SceneComponentKind);
bool removeSceneComponent(SceneDocument&,EntityId,SceneComponentKind);
bool removeSceneComponents(SceneDocument&,std::span<const SceneComponentTarget>);
}
