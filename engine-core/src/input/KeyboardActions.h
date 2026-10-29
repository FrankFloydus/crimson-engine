#pragma once
#include "InputEvent.h"
#include <variant>

namespace crimson {
// Named editor shortcuts follow the active keyboard layout. Spatial controls
// use physical positions. Neither binding is text input or a modified symbol.
struct LogicalKey { char32_t code{}; };
struct PhysicalKey { PhysicalKeyCode code{PhysicalKeyCode::Unknown}; };
struct KeyboardBinding {
    std::variant<LogicalKey,PhysicalKey> key;
    InputModifiers modifiers{InputModifier::None};
};
inline unsigned shortcutModifiers(InputModifiers modifiers) noexcept {
    // Left/right variants are equivalent. Lock keys do not alter a shortcut;
    // AltGr remains a distinct modifier so text composition cannot trigger one.
    return modifiers&(InputModifier::Shift|InputModifier::Control|InputModifier::Alt|InputModifier::Meta|InputModifier::AltGraph);
}
inline bool matchesKeyPress(const InputEvent& event,const KeyboardBinding& binding) noexcept {
    if(event.type!=InputEventType::KeyDown || event.repeat ||
       shortcutModifiers(event.modifiers)!=shortcutModifiers(binding.modifiers))return false;
    if(const auto* logical=std::get_if<LogicalKey>(&binding.key))
        return logical->code!=LogicalKeyCode::Unknown && event.logical==logical->code;
    const auto physical=std::get<PhysicalKey>(binding.key).code;
    return physical!=PhysicalKeyCode::Unknown && event.physical==physical;
}
}
