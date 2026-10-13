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
void Navigation::handleEvent(const SDL_Event& event) {
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST || event.type == SDL_EVENT_WINDOW_HIDDEN ||
        (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE)) {
        cancelRequested_ = true; events_.clear(); return;
    }
    if (cancelRequested_) return;
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP || event.type == SDL_EVENT_MOUSE_MOTION ||
        event.type == SDL_EVENT_MOUSE_WHEEL || event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP)
        events_.push_back({event,window_ ? SDL_GetModState() : SDL_KMOD_NONE});
}
void Navigation::releaseCapture() {
    if (window_) {
        if (relativeActive_) { SDL_SetWindowRelativeMouseMode(window_,false); SDL_WarpMouseInWindow(window_,restoreX_,restoreY_); }
        SDL_CaptureMouse(false);
    }
    relativeActive_ = false;
}
void Navigation::cancel() {
    releaseCapture(); mode_ = Mode::None; anchorPending_ = false; keys_.fill(false); events_.clear(); cancelRequested_ = false;
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
    if (cancelRequested_ || region.width <= 0 || region.height <= 0 || (active() && region.inputBlocked)) { cancel(); return; }
    bool flyStarted = false;
    auto queued = std::move(events_); events_.clear();
    for (const auto& queuedEvent : queued) {
        const auto& event = queuedEvent.value;
        if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
            const auto key = event.key.scancode;
            if (key >= 0 && key < SDL_SCANCODE_COUNT) keys_[static_cast<std::size_t>(key)] = event.type == SDL_EVENT_KEY_DOWN;
        } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            if (flying() && event.button.button == SDL_BUTTON_MIDDLE) { flySpeed_ = 25; continue; }
            if (active() || region.inputBlocked || !contains(region,event.button.x,event.button.y)) continue;
            if (event.button.button != SDL_BUTTON_MIDDLE && event.button.button != SDL_BUTTON_RIGHT) continue;
            const bool shift = keys_[SDL_SCANCODE_LSHIFT] || keys_[SDL_SCANCODE_RSHIFT] || (queuedEvent.modifiers & SDL_KMOD_SHIFT) != 0;
            mode_ = event.button.button == SDL_BUTTON_RIGHT ? Mode::Fly : shift ? Mode::Pan : Mode::Orbit;
            lastX_ = restoreX_ = event.button.x; lastY_ = restoreY_ = event.button.y; pivot_ = camera.target;
            anchorPending_ = false;
            if (flying()) {
                flyStarted = true;
                if (window_) {
                    if (!SDL_SetWindowRelativeMouseMode(window_,true)) { mode_ = Mode::None; continue; }
                    relativeActive_ = true;
                    float discardedX{},discardedY{}; SDL_GetRelativeMouseState(&discardedX,&discardedY);
                    const bool* current = SDL_GetKeyboardState(nullptr);
                    for (const auto key : {SDL_SCANCODE_W,SDL_SCANCODE_A,SDL_SCANCODE_S,SDL_SCANCODE_D}) keys_[key] = current[key];
                }
            } else {
                if (sphereVisible) {
                    if (const auto hit = sphereAnchor(camera,region,lastX_,lastY_)) { anchor_ = *hit; anchorPending_ = true; }
                }
                if (window_) SDL_CaptureMouse(true);
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
