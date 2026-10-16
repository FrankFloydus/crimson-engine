#pragma once
#include "crimson-renderer/RenderTypes.h"
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>
#include <array>
#include <vector>

namespace crimson {
class Navigation {
public:
    explicit Navigation(SDL_Window* window);
    ~Navigation() { cancel(); }
    // ImGui native viewports use desktop positions; SDL events remain window-local.
    void setDesktopCoordinates(bool enabled);
    void handleEvent(const SDL_Event& event);
    void update(Camera& camera, const ViewportRegion& region, float dt, bool sphereVisible = true, bool preserveRoll = false);
    void cancel();
    bool active() const { return mode_ != Mode::None; }
    bool flying() const { return mode_ == Mode::Fly; }
    float flySpeed() const { return flySpeed_; }
private:
    enum class Mode { None, Orbit, Pan, Fly };
    struct Event { SDL_Event value{}; SDL_Keymod modifiers{}; };
    void releaseCapture();
    void motion(Camera& camera, const ViewportRegion& region, float x, float y, bool preserveRoll);
    SDL_WindowID baseWindowId_{}, ownerWindowId_{}, captureWindowId_{};
    std::vector<Event> events_;
    std::array<bool, SDL_SCANCODE_COUNT> keys_{};
    Mode mode_{Mode::None};
    bool desktopCoordinates_{false}, captureActive_{false}, relativeActive_{false}, anchorPending_{false};
    float lastX_{}, lastY_{}, restoreX_{}, restoreY_{}, flySpeed_{25.0f};
    Vec3 pivot_{};
    Vec3 anchor_{};
};
} // namespace crimson
