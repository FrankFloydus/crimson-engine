#pragma once
#include <SDL3/SDL_events.h>
#include <optional>

namespace crimson {
enum class PlayAction { None,Start,Stop };
struct PlayShortcutContext {
    bool playing{},transitionBlocked{},startBlocked{};
};
// SDL edges retain the event's layout-aware key and modifiers. UI ownership is
// checked at the press and again after drawing, before the session queues an
// explicit action for its next safe document boundary.
class PlayShortcuts {
public:
    void handleEvent(const SDL_Event&);
    PlayAction consume(const PlayShortcutContext&);
    void cancel() noexcept { start_.reset();stop_.reset(); }
private:
    std::optional<SDL_WindowID> start_,stop_;
};
}
