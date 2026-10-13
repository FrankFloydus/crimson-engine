#include "Navigation.h"
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <utility>

namespace crimson {
namespace {
using math::add;
using math::scale;
SDL_WindowID eventWindowId(const SDL_Event& event) {
    switch (event.type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP: return event.button.windowID;
        case SDL_EVENT_MOUSE_MOTION: return event.motion.windowID;
        case SDL_EVENT_MOUSE_WHEEL: return event.wheel.windowID;
        case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP: return event.key.windowID;
        default: return event.window.windowID;
    }
}
bool cancelsNavigation(const SDL_Event& event) {
    return event.type == SDL_EVENT_WINDOW_FOCUS_LOST || event.type == SDL_EVENT_WINDOW_HIDDEN ||
        event.type == SDL_EVENT_WINDOW_MINIMIZED || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED ||
        event.type == SDL_EVENT_WINDOW_DESTROYED ||
        (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE);
}
bool belongsTo(SDL_WindowID eventId,SDL_WindowID ownerId) {
    // SDL window events carry an ID. Zero is reserved for windowless synthetic checks.
    return eventId == 0 || eventId == ownerId;
}
Vec3 sub(const Vec3& a,const Vec3& b) { return math::subtract(a,b); }
bool contains(const ViewportRegion& region,float x,float y) { return region.width > 0 && region.height > 0 && std::isfinite(x) && std::isfinite(y) && x >= region.x && y >= region.y && x < region.x+region.width && y < region.y+region.height; }
std::optional<Vec3> sphereAnchor(const Camera& camera,const ViewportRegion& region,float x,float y) {
    const auto screenRay=camera.screenRay(x,y,region);
    if (!screenRay) return {};
    const auto& ray=screenRay->direction;
    const Vec3 offset = sub(camera.eye(),{0,1,0});
    const double b = math::dot(offset,ray), discriminant = b*b-math::dot(offset,offset)+1;
    if (discriminant < 0 || !std::isfinite(discriminant)) return {};
    const double root = std::sqrt(discriminant);
    const double nearest = -b-root, farthest = -b+root;
    const double distance = nearest > .0001f ? nearest : farthest;
    return distance > .0001f ? std::optional<Vec3>{add(camera.eye(),scale(ray,distance))} : std::nullopt;
}
}
Navigation::Navigation(SDL_Window* window) {
    baseWindowId_ = window ? SDL_GetWindowID(window) : 0;
    ownerWindowId_ = baseWindowId_;
}
void Navigation::setDesktopCoordinates(bool enabled) {
    if (desktopCoordinates_ == enabled) return;
    cancel();
    desktopCoordinates_ = enabled;
}
void Navigation::handleEvent(const SDL_Event& event) {
    if (!cancelsNavigation(event) && event.type != SDL_EVENT_MOUSE_BUTTON_DOWN && event.type != SDL_EVENT_MOUSE_BUTTON_UP &&
        event.type != SDL_EVENT_MOUSE_MOTION && event.type != SDL_EVENT_MOUSE_WHEEL && event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP) return;
    auto value = event;
    const auto sourceId = eventWindowId(event);
    if (desktopCoordinates_ && sourceId && (event.type == SDL_EVENT_MOUSE_MOTION ||
        event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP)) {
        auto* sourceWindow = SDL_GetWindowFromID(sourceId);
        int x{}, y{};
        if (!sourceWindow || !SDL_GetWindowPosition(sourceWindow,&x,&y)) return;
        if (event.type == SDL_EVENT_MOUSE_MOTION) { value.motion.x += static_cast<float>(x); value.motion.y += static_cast<float>(y); }
        else { value.button.x += static_cast<float>(x); value.button.y += static_cast<float>(y); }
    }
    events_.push_back({value,(baseWindowId_ || ownerWindowId_) ? SDL_GetModState() : SDL_KMOD_NONE});
}
void Navigation::releaseCapture() {
    if (auto* window = captureWindowId_ ? SDL_GetWindowFromID(captureWindowId_) : nullptr) {
        if (relativeActive_) {
            SDL_SetWindowRelativeMouseMode(window,false);
            float x = restoreX_, y = restoreY_;
            int windowX{}, windowY{};
            if (!desktopCoordinates_ || SDL_GetWindowPosition(window,&windowX,&windowY)) {
                if (desktopCoordinates_) { x -= static_cast<float>(windowX); y -= static_cast<float>(windowY); }
                SDL_WarpMouseInWindow(window,x,y);
            }
        }
    }
    if (captureActive_) SDL_CaptureMouse(false);
    relativeActive_ = captureActive_ = false; captureWindowId_ = 0;
}
void Navigation::cancel() {
    releaseCapture(); mode_ = Mode::None; anchorPending_ = false; keys_.fill(false); events_.clear();
}
void Navigation::motion(Camera& camera,const ViewportRegion& region,float x,float y) {
    const float dx = x-lastX_, dy = y-lastY_; lastX_ = x; lastY_ = y;
    if (!std::isfinite(dx) || !std::isfinite(dy) || (std::abs(dx) < 1e-6f && std::abs(dy) < 1e-6f)) return;
    if (anchorPending_) {
        anchorPending_ = false;
        const double depth = math::dot(sub(anchor_,camera.eye()),camera.forward());
        if (std::isfinite(depth) && depth > .0001f && depth <= camera.distance()+1e-5f) {
            camera.target = add(camera.eye(),scale(camera.forward(),depth));
            pivot_ = mode_ == Mode::Orbit ? anchor_ : camera.target;
        }
    }
    if (mode_ == Mode::Orbit) camera.orbit(dx,dy,pivot_);
    else if (mode_ == Mode::Pan) camera.pan(dx,dy,region.width,region.height);
}
void Navigation::update(Camera& camera,const ViewportRegion& region,float dt,bool sphereVisible) {
    auto queued = std::move(events_); events_.clear();
    const auto owner = region.windowId ? region.windowId : baseWindowId_;
    if (owner != ownerWindowId_) { cancel(); ownerWindowId_ = owner; }
    auto* window = owner ? SDL_GetWindowFromID(owner) : nullptr;
    if ((owner && !window) || region.width <= 0 || region.height <= 0 || (active() && region.inputBlocked)) { cancel(); return; }
    // Matching cancellation takes precedence over motion already queued in this frame.
    for (const auto& queuedEvent : queued)
        if (belongsTo(eventWindowId(queuedEvent.value),owner) && cancelsNavigation(queuedEvent.value)) { cancel(); return; }
    bool flyStarted = false;
    for (const auto& queuedEvent : queued) {
        const auto& event = queuedEvent.value;
        if (!belongsTo(eventWindowId(event),owner)) continue;
        if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
            const auto key = event.key.scancode;
            if (key >= 0 && key < SDL_SCANCODE_COUNT) keys_[static_cast<std::size_t>(key)] = event.type == SDL_EVENT_KEY_DOWN;
        } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            if (flying() && event.button.button == SDL_BUTTON_MIDDLE) { flySpeed_ = 25; continue; }
            if (active() || region.inputBlocked || !region.hovered || !contains(region,event.button.x,event.button.y)) continue;
            if (event.button.button != SDL_BUTTON_MIDDLE && event.button.button != SDL_BUTTON_RIGHT) continue;
            const bool shift = keys_[SDL_SCANCODE_LSHIFT] || keys_[SDL_SCANCODE_RSHIFT] || (queuedEvent.modifiers & SDL_KMOD_SHIFT) != 0;
            mode_ = event.button.button == SDL_BUTTON_RIGHT ? Mode::Fly : shift ? Mode::Pan : Mode::Orbit;
            lastX_ = restoreX_ = event.button.x; lastY_ = restoreY_ = event.button.y; pivot_ = camera.target;
            captureWindowId_ = owner;
            anchorPending_ = false;
            if (flying()) {
                flyStarted = true;
                if (window) {
                    if (!SDL_SetWindowRelativeMouseMode(window,true)) { mode_ = Mode::None; captureWindowId_ = 0; continue; }
                    relativeActive_ = true;
                    float discardedX{},discardedY{}; SDL_GetRelativeMouseState(&discardedX,&discardedY);
                }
            } else {
                if (sphereVisible) {
                    if (const auto hit = sphereAnchor(camera,region,lastX_,lastY_)) { anchor_ = *hit; anchorPending_ = true; }
                }
                if (window) captureActive_ = SDL_CaptureMouse(true);
            }
        } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
            if (flying()) { if (!flyStarted) camera.flyLook(event.motion.xrel,event.motion.yrel); }
            else if (active()) motion(camera,region,event.motion.x,event.motion.y);
        } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            const bool owns = flying() ? event.button.button == SDL_BUTTON_RIGHT : active() && event.button.button == SDL_BUTTON_MIDDLE;
            if (owns) {
                if (!flying()) motion(camera,region,event.button.x,event.button.y);
                releaseCapture(); mode_ = Mode::None; anchorPending_ = false;
            }
        } else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
            const float steps = event.wheel.y * (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1);
            if (!std::isfinite(steps)) continue;
            if (flying()) flySpeed_ = std::clamp(flySpeed_+steps,1.0f,1000.0f);
            else if (!region.inputBlocked && region.hovered) camera.zoom(steps);
        }
    }
    if (flying() && !flyStarted) camera.flyMove(float(keys_[SDL_SCANCODE_W])-float(keys_[SDL_SCANCODE_S]),float(keys_[SDL_SCANCODE_D])-float(keys_[SDL_SCANCODE_A]),dt,flySpeed_);
}
} // namespace crimson
