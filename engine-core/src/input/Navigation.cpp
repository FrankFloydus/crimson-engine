#include "Navigation.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <utility>

namespace crimson {
namespace {
using math::add;
using math::scale;
InputSurfaceId eventWindowId(const InputEvent& event) {
    return event.surface;
}
bool cancelsNavigation(const InputEvent& event) {
    return event.type == InputEventType::SurfaceFocusLost || event.type == InputEventType::SurfaceHidden ||
        event.type == InputEventType::SurfaceMinimized || event.type == InputEventType::SurfaceCloseRequested ||
        event.type == InputEventType::SurfaceDestroyed ||
        (event.type == InputEventType::KeyDown && event.physical == PhysicalKeyCode::Escape);
}
bool belongsTo(InputSurfaceId eventId,InputSurfaceId ownerId) {
    // Host events carry an ID. Zero is reserved for windowless synthetic checks.
    return eventId == 0 || eventId == ownerId;
}
bool flyMovementKey(PhysicalKeyCode key) {
    return key==PhysicalKeyCode::W || key==PhysicalKeyCode::A || key==PhysicalKeyCode::S || key==PhysicalKeyCode::D;
}
constexpr float ContextClickDragDistance=4;
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
bool navigationBlocksEditorEvent(const InputEvent& event,InputSurfaceId owner,bool flying) {
    if(!flying)return false;
    switch(event.type) {
    case InputEventType::MouseMotion:return event.surface==owner;
    case InputEventType::MouseWheel:return event.surface==owner;
    case InputEventType::MouseButtonDown:return event.surface==owner;
    case InputEventType::KeyDown:
        return event.surface==owner && (event.physical==PhysicalKeyCode::W || event.physical==PhysicalKeyCode::A ||
            event.physical==PhysicalKeyCode::S || event.physical==PhysicalKeyCode::D);
    default:return false;
    }
}
Navigation::Navigation(InputSurfaceId owner) {
    baseWindowId_ = owner;
    ownerWindowId_ = baseWindowId_;
}
void Navigation::setMouseCapture(MouseCapture capture){cancel();mouseCapture_=std::move(capture);}
void Navigation::handleEvent(const InputEvent& event) {
    if (!cancelsNavigation(event) && event.type != InputEventType::MouseButtonDown && event.type != InputEventType::MouseButtonUp &&
        event.type != InputEventType::MouseMotion && event.type != InputEventType::MouseWheel && event.type != InputEventType::KeyDown && event.type != InputEventType::KeyUp) return;
    auto value = event;
    const auto sourceId = eventWindowId(event);
    // A minimized/unfocused application may skip drawing and update(). Release
    // the existing owner immediately rather than retaining native capture.
    if((active() || contextClick_) && belongsTo(sourceId,ownerWindowId_) && cancelsNavigation(event)) { cancel();return; }
    if(contextClick_ && belongsTo(sourceId,ownerWindowId_) &&
       (event.type==InputEventType::MouseButtonDown || event.type==InputEventType::MouseWheel ||
        (event.type==InputEventType::KeyDown && flyMovementKey(event.physical))))contextClick_.reset();
    events_.push_back({value,event.modifiers});
}
void Navigation::releaseCapture() {
    if(relativeActive_&&mouseCapture_)mouseCapture_(captureWindowId_,false);
    relativeActive_ = captureActive_ = false; captureWindowId_ = 0;
}
void Navigation::cancel() {
    releaseCapture(); mode_ = Mode::None; anchorPending_ = false; keys_.fill(false); events_.clear();
    contextClickCandidate_=false;contextClickMotion_=0;contextClick_.reset();
}
std::optional<Vec2> Navigation::consumeContextClick() { return std::exchange(contextClick_,std::nullopt); }
void Navigation::motion(Camera& camera,const ViewportRegion& region,float x,float y,bool preserveRoll) {
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
    if (mode_ == Mode::Orbit) camera.orbit(dx,dy,pivot_,preserveRoll);
    else if (mode_ == Mode::Pan) camera.pan(dx,dy,region.width,region.height);
}
void Navigation::update(Camera& camera,const ViewportRegion& region,float dt,bool sphereVisible,bool preserveRoll) {
    auto queued = std::move(events_); events_.clear();
    const auto owner = region.windowId ? region.windowId : baseWindowId_;
    if (owner != ownerWindowId_) { cancel(); ownerWindowId_ = owner; }
    if (region.width <= 0 || region.height <= 0 || ((active() || contextClick_) && region.inputBlocked)) { cancel(); return; }
    if(relativeActive_&&mouseCapture_&&!mouseCapture_(owner,true)){cancel();return;}
    // Matching cancellation takes precedence over motion already queued in this frame.
    for (const auto& queuedEvent : queued)
        if (belongsTo(eventWindowId(queuedEvent.value),owner) && cancelsNavigation(queuedEvent.value)) { cancel(); return; }
    bool flyStarted = false;
    for (const auto& queuedEvent : queued) {
        const auto& event = queuedEvent.value;
        if (!belongsTo(eventWindowId(event),owner)) continue;
        if (event.type == InputEventType::KeyDown || event.type == InputEventType::KeyUp) {
            const auto key = event.physical;
            if(event.type==InputEventType::KeyDown&&event.repeat)continue;
            if (physicalKeyIndex(key) < keys_.size()) keys_[static_cast<std::size_t>(key)] = event.type == InputEventType::KeyDown;
            if(event.type==InputEventType::KeyDown && flyMovementKey(key)){contextClickCandidate_=false;contextClick_.reset();}
        } else if (event.type == InputEventType::MouseButtonDown) {
            contextClick_.reset();
            if(active())contextClickCandidate_=false;
            if (flying() && event.button == MouseButton::Middle) { flySpeed_ = 25; continue; }
            if (active() || region.inputBlocked || !region.hovered || !contains(region,event.position[0],event.position[1])) continue;
            if (event.button != MouseButton::Middle && event.button != MouseButton::Right) continue;
            const bool shift = keys_[physicalKeyIndex(PhysicalKeyCode::LeftShift)] || keys_[physicalKeyIndex(PhysicalKeyCode::RightShift)] || (queuedEvent.modifiers & InputModifier::Shift) != 0;
            mode_ = event.button == MouseButton::Right ? Mode::Fly : shift ? Mode::Pan : Mode::Orbit;
            lastX_ = restoreX_ = event.position[0]; lastY_ = restoreY_ = event.position[1]; pivot_ = camera.target;
            captureWindowId_ = owner;
            anchorPending_ = false;
            if (flying()) {
                flyStarted = true;
                contextClickMotion_=0;
                contextClickCandidate_=!keys_[physicalKeyIndex(PhysicalKeyCode::W)] && !keys_[physicalKeyIndex(PhysicalKeyCode::A)] &&
                    !keys_[physicalKeyIndex(PhysicalKeyCode::S)] && !keys_[physicalKeyIndex(PhysicalKeyCode::D)];
                if (owner) {
                    if (!mouseCapture_||!mouseCapture_(owner,true)) { mode_ = Mode::None; captureWindowId_ = 0; contextClickCandidate_=false; continue; }
                    relativeActive_ = true;
                }
            } else {
                if (sphereVisible) {
                    if (const auto hit = sphereAnchor(camera,region,lastX_,lastY_)) { anchor_ = *hit; anchorPending_ = true; }
                }
            }
        } else if (event.type == InputEventType::MouseMotion) {
            if (flying()) {
                contextClickMotion_+=std::hypot(event.relative[0],event.relative[1]);
                if(!std::isfinite(contextClickMotion_) || contextClickMotion_>ContextClickDragDistance)contextClickCandidate_=false;
                if (!flyStarted) camera.flyLook(event.relative[0],event.relative[1],preserveRoll);
            }
            else if (active()) motion(camera,region,event.position[0],event.position[1],preserveRoll);
        } else if (event.type == InputEventType::MouseButtonUp) {
            const bool owns = flying() ? event.button == MouseButton::Right : active() && event.button == MouseButton::Middle;
            if (owns) {
                if(flying() && contextClickCandidate_)contextClick_=Vec2{restoreX_,restoreY_};
                contextClickCandidate_=false;
                if (!flying()) motion(camera,region,event.position[0],event.position[1],preserveRoll);
                releaseCapture(); mode_ = Mode::None; anchorPending_ = false;
            }
        } else if (event.type == InputEventType::MouseWheel) {
            contextClickCandidate_=false;contextClick_.reset();
            const float steps = event.wheelY * (event.wheelFlipped == true ? -1 : 1);
            if (!std::isfinite(steps)) continue;
            if (flying()) {
                // Keep fractional detents proportional and bound the exponent
                // before pow so extreme finite events saturate without overflow.
                const double factor=std::pow(1.25,std::clamp(double(steps),-64.0,64.0));
                flySpeed_=static_cast<float>(std::clamp(double(flySpeed_)*factor,1.0,1000.0));
            }
            else if (!region.inputBlocked && region.hovered) camera.zoom(steps);
        }
    }
    if (flying() && !flyStarted) camera.flyMove(float(keys_[physicalKeyIndex(PhysicalKeyCode::W)])-float(keys_[physicalKeyIndex(PhysicalKeyCode::S)]),float(keys_[physicalKeyIndex(PhysicalKeyCode::D)])-float(keys_[physicalKeyIndex(PhysicalKeyCode::A)]),dt,flySpeed_);
}
} // namespace crimson
