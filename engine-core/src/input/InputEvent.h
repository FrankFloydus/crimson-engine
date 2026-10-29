#pragma once

#include "math/Math.h"
#include <cstddef>
#include <cstdint>

namespace crimson {
using InputSurfaceId=std::uint32_t;
enum class InputEventType : std::uint8_t {
    None,KeyDown,KeyUp,MouseMotion,MouseButtonDown,MouseButtonUp,MouseWheel,
    SurfaceFocusLost,SurfaceHidden,SurfaceMinimized,SurfaceCloseRequested,SurfaceDestroyed
};
// Host adapters translate native physical scan codes before producing an event.
// These identities intentionally have no platform/toolkit numeric equivalence.
enum class PhysicalKeyCode : std::uint8_t {
    Unknown,W,A,S,D,Space,Escape,LeftShift,RightShift,F,F1,F5,F6,F7,P,Tab,Count
};
enum class MouseButton : std::uint8_t {None,Left,Middle,Right,Back,Forward};
using InputModifiers=std::uint16_t;
namespace InputModifier {
inline constexpr InputModifiers None=0,Shift=1,Control=2,Alt=4,Meta=8,AltGraph=16,CapsLock=32,NumLock=64;
}
namespace LogicalKeyCode {
inline constexpr char32_t Unknown=0,Escape=U'\x1b',Tab=U'\t',Space=U' ',
    F1=0x110001,F5=0x110005,F6=0x110006,F7=0x110007;
}
struct InputEvent {
    InputEventType type{InputEventType::None};
    InputSurfaceId surface{};
    PhysicalKeyCode physical{PhysicalKeyCode::Unknown};
    char32_t logical{LogicalKeyCode::Unknown};
    InputModifiers modifiers{};
    bool repeat{};
    MouseButton button{MouseButton::None};
    Vec2 position{}; // Logical pixels in the host's declared viewport space.
    Vec2 relative{}; // Raw right/down logical-pixel motion, independent of DPI.
    float wheelY{};
    bool wheelFlipped{};
};
inline constexpr std::size_t physicalKeyIndex(PhysicalKeyCode key){return static_cast<std::size_t>(key);}
}
