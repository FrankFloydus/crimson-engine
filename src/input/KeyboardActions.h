#pragma once
#include <SDL3/SDL_events.h>
#include <variant>

namespace crimson {
// Named editor shortcuts follow the active keyboard layout. Spatial controls
// use physical positions. Neither binding is text input or a modified symbol.
struct LogicalKey { SDL_Keycode code{}; };
struct PhysicalKey { SDL_Scancode code{SDL_SCANCODE_UNKNOWN}; };
struct KeyboardBinding {
    std::variant<LogicalKey,PhysicalKey> key;
    SDL_Keymod modifiers{SDL_KMOD_NONE};
};
inline unsigned shortcutModifiers(SDL_Keymod modifiers) noexcept {
    // Left/right variants are equivalent. Lock keys do not alter a shortcut;
    // AltGr remains a distinct modifier so text composition cannot trigger one.
    return ((modifiers&SDL_KMOD_SHIFT)?1u:0u) | ((modifiers&SDL_KMOD_CTRL)?2u:0u) |
        ((modifiers&SDL_KMOD_ALT)?4u:0u) | ((modifiers&SDL_KMOD_GUI)?8u:0u) |
        ((modifiers&SDL_KMOD_MODE)?16u:0u);
}
inline bool matchesKeyPress(const SDL_KeyboardEvent& event,const KeyboardBinding& binding) noexcept {
    if(event.type!=SDL_EVENT_KEY_DOWN || event.repeat ||
       shortcutModifiers(event.mod)!=shortcutModifiers(binding.modifiers))return false;
    if(const auto* logical=std::get_if<LogicalKey>(&binding.key))
        return logical->code!=SDLK_UNKNOWN && event.key==logical->code;
    const auto physical=std::get<PhysicalKey>(binding.key).code;
    return physical!=SDL_SCANCODE_UNKNOWN && event.scancode==physical;
}
}
