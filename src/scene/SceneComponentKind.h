#pragma once

#include <array>
#include <cstdint>

namespace crimson {
// Persistent component identities. Keep values stable when adding new kinds.
// Editor row identity is (EntityId, SceneComponentKind), never another entity.
enum class SceneComponentKind : std::uint8_t {
    Transform=0, Actor=1, Motor=2, Controller=3, Navigation=4, PlayerInput=5,
    LightRig=6, DarknessReveal=7, Camera=8, CameraFollow=9, Model=10, Collider=11,
    Directional=12, Point=13, Environment=14, Particle=15, Quader=16, NavigationVolume=17
};
inline constexpr std::array AllSceneComponentKinds{
    SceneComponentKind::Transform,SceneComponentKind::Actor,SceneComponentKind::Motor,
    SceneComponentKind::Controller,SceneComponentKind::Navigation,SceneComponentKind::PlayerInput,
    SceneComponentKind::LightRig,SceneComponentKind::DarknessReveal,SceneComponentKind::Camera,
    SceneComponentKind::CameraFollow,SceneComponentKind::Model,SceneComponentKind::Collider,
    SceneComponentKind::Directional,SceneComponentKind::Point,SceneComponentKind::Environment,
    SceneComponentKind::Particle,SceneComponentKind::Quader,SceneComponentKind::NavigationVolume};
}
