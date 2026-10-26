#pragma once

#include <array>
#include <cstdint>

namespace crimson {
// Persistent component identities. Keep values stable when adding new kinds.
// Editor row identity is (EntityId, SceneComponentKind), never another entity.
enum class SceneComponentKind : std::uint8_t {
    Transform=0, Actor=1, Movement=2, Controller=3, Navigation=4, PlayerInput=5,
    // Identities 6 and 7 are retired (actor lighting and darkness reveal).
    Camera=8, CameraFollow=9, Model=10, Collider=11,
    Directional=12, Point=13, Environment=14, Particle=15, Quader=16, NavigationVolume=17, Enemy=18,
    Attributes=19, // Retired; never reuse this persistent identity.
    Tags=20, Effects=21, Decal=22, Subdivision=23, Spot=24
};
inline constexpr std::array AllSceneComponentKinds{
    SceneComponentKind::Transform,SceneComponentKind::Actor,SceneComponentKind::Movement,
    SceneComponentKind::Controller,SceneComponentKind::Navigation,SceneComponentKind::PlayerInput,
    SceneComponentKind::Camera,
    SceneComponentKind::CameraFollow,SceneComponentKind::Model,SceneComponentKind::Collider,
    SceneComponentKind::Directional,SceneComponentKind::Point,SceneComponentKind::Spot,SceneComponentKind::Environment,
    SceneComponentKind::Particle,SceneComponentKind::Quader,SceneComponentKind::NavigationVolume,SceneComponentKind::Enemy,
    SceneComponentKind::Tags,SceneComponentKind::Effects,SceneComponentKind::Decal,SceneComponentKind::Subdivision};
}
