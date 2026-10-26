#pragma once
#include <SDL3/SDL_events.h>
#include <optional>

namespace crimson {
enum class PlayAction { None,Start,Stop,StartHere,TogglePause,Step };
struct PlayShortcutContext {
    bool playing{},transitionBlocked{},startBlocked{};
    bool paused{};
};
// SDL edges retain the event's layout-aware key and modifiers. UI ownership is
// checked at the press and again after drawing, before the session queues an
// explicit action for its next safe document boundary.
class PlayShortcuts {
public:
    void handleEvent(const SDL_Event&);
    PlayAction consume(const PlayShortcutContext&);
    void cancel() noexcept { start_.reset();stop_.reset();startHere_.reset();pause_.reset();step_.reset(); }
private:
    std::optional<SDL_WindowID> start_,stop_,startHere_,pause_,step_;
};
}
