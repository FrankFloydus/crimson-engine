#include "GameInput.h"
#include <cmath>
#include <optional>
#include <utility>

namespace crimson {
namespace {
std::optional<Vec2> eventPosition(float x,float y) {
    // The native host supplies coordinates in the viewport's declared space.
    if(!std::isfinite(x) || !std::isfinite(y))return {};
    return Vec2{x,y};
}
std::optional<std::size_t> actionKey(PhysicalKeyCode code) {
    switch(code) {
    case PhysicalKeyCode::W:return 0;
    case PhysicalKeyCode::A:return 1;
    case PhysicalKeyCode::S:return 2;
    case PhysicalKeyCode::D:return 3;
    case PhysicalKeyCode::Space:return 4;
    default:return {};
    }
}
}
bool gameInputBlocksEditorEvent(const InputEvent& event,InputSurfaceId owner,bool gameplayOwned,bool relativeMouseCaptured) {
    if(!gameplayOwned)return false;
    if(event.type==InputEventType::KeyDown)return event.surface==owner && actionKey(event.physical).has_value();
    if(!relativeMouseCaptured)return false;
    switch(event.type) {
    case InputEventType::MouseMotion:return event.surface==owner;
    case InputEventType::MouseButtonDown:return event.surface==owner;
    case InputEventType::MouseWheel:return event.surface==owner;
    default:return false;
    }
}
GameInput::~GameInput() { releaseMouseCapture(); }
void GameInput::setMouseCapture(MouseCapture capture){cancel();mouseCapture_=std::move(capture);}
bool GameInput::contains(Vec2 position) const {
    return math::finite(position) && position[0]>=viewport_.x && position[1]>=viewport_.y &&
        position[0]<viewport_.x+viewport_.width && position[1]<viewport_.y+viewport_.height;
}
void GameInput::setViewport(const ViewportRegion& viewport,bool enabled,bool keyboardBlocked) {
    const bool valid=std::isfinite(viewport.x) && std::isfinite(viewport.y) &&
        std::isfinite(viewport.width) && std::isfinite(viewport.height) && viewport.width>0 && viewport.height>0;
    if(viewport.windowId!=viewport_.windowId || !enabled || !valid || keyboardBlocked || viewport.inputBlocked)cancel();
    viewport_=viewport;enabled_=enabled && valid;blocked_=keyboardBlocked || viewport.inputBlocked;
}
void GameInput::acquire() {
    if(enabled_ && !blocked_)armed_=true;
}
void GameInput::acquireFromImageClick() {
    const auto acquisition=std::exchange(imageAcquisition_,ImageAcquisition::None);
    if(acquisition!=ImageAcquisition::Canceled)acquire();
}
void GameInput::releaseMouseCapture() {
    if(captureWindowId_&&mouseCapture_)mouseCapture_(captureWindowId_,false);
    captureWindowId_=0;discardNextMotion_=false;
}
void GameInput::cancel() {
    releaseMouseCapture();held_.fill(false);lookDelta_={};jumpPending_=false;tracePending_=false;traceHeld_=false;armed_=false;++cancellationRevision_;
    if(imageAcquisition_==ImageAcquisition::Pending)imageAcquisition_=ImageAcquisition::Canceled;
}
void GameInput::syncMouseCapture() {
    if(!ownsKeyboard()) { releaseMouseCapture();return; }
    if(!viewport_.windowId||!mouseCapture_) { cancel();return; }
    if(captureWindowId_==viewport_.windowId){if(!mouseCapture_(captureWindowId_,true))cancel();return;}
    releaseMouseCapture();
    if(!mouseCapture_(viewport_.windowId,true)) { cancel();return; }
    captureWindowId_=viewport_.windowId;
    // Acquisition can include a platform cursor reposition. Never turn that
    // transition or pre-capture motion into a jump in the player's view.
    lookDelta_={};discardNextMotion_=true;
}
Vec2 GameInput::movement() const {
    return ownsKeyboard()?Vec2{float(held_[3])-float(held_[1]),float(held_[0])-float(held_[2])}:Vec2{};
}
Vec2 GameInput::consumeLookDelta() {
    const auto result=ownsKeyboard()?lookDelta_:Vec2{};lookDelta_={};return result;
}
bool GameInput::consumeJump() {
    const bool result=ownsKeyboard() && jumpPending_;jumpPending_=false;return result;
}
bool GameInput::consumeTrace() {
    const bool result=ownsKeyboard() && tracePending_;tracePending_=false;return result;
}
bool GameInput::handleEvent(const InputEvent& event) {
    if(event.type==InputEventType::SurfaceFocusLost || event.type==InputEventType::SurfaceHidden ||
       event.type==InputEventType::SurfaceMinimized || event.type==InputEventType::SurfaceCloseRequested ||
       event.type==InputEventType::SurfaceDestroyed) {
        if(event.surface==viewport_.windowId)cancel();
        return false;
    }
    if(event.type==InputEventType::MouseMotion) {
        if(!ownsKeyboard() || event.surface!=viewport_.windowId)return false;
        if(discardNextMotion_) { discardNextMotion_=false;return true; }
        const Vec2 delta{event.relative[0],event.relative[1]};
        const Vec2 accumulated{lookDelta_[0]+delta[0],lookDelta_[1]+delta[1]};
        if(math::finite(delta) && math::finite(accumulated))lookDelta_=accumulated;
        return true;
    }
    if(event.type==InputEventType::MouseButtonDown) {
        const auto pressTrace=[&](bool fire) {
            if(event.button!=MouseButton::Left)return;
            if(fire&&!traceHeld_)tracePending_=true;
            traceHeld_=true;
        };
        // During relative capture the platform cursor need not be in the image
        // (Play also acquires automatically). Its absolute position is irrelevant.
        if(ownsKeyboard() && captureWindowId_ && captureWindowId_==event.surface) {
            pressTrace(true);return true;
        }
        const bool alreadyOwned=ownsKeyboard();
        const auto position=eventPosition(event.position[0],event.position[1]);
        const bool imagePress=enabled_ && event.surface==viewport_.windowId && position && contains(*position);
        if(!imagePress || blocked_)cancel();
        if(imagePress && event.button==MouseButton::Left) {
            // The click that acquires Game is only an input handoff. A new
            // press while already owned performs the trace action.
            pressTrace(alreadyOwned&&!blocked_);
            imageAcquisition_=ImageAcquisition::Pending;
            acquire();
        }
        return ownsKeyboard() && imagePress;
    }
    if(event.type==InputEventType::MouseButtonUp || event.type==InputEventType::MouseWheel) {
        const auto window=event.type==InputEventType::MouseButtonUp?event.surface:event.surface;
        if(event.type==InputEventType::MouseButtonUp && event.button==MouseButton::Left && window==viewport_.windowId)traceHeld_=false;
        return ownsKeyboard() && window==viewport_.windowId;
    }
    if(event.type!=InputEventType::KeyDown && event.type!=InputEventType::KeyUp)return false;
    if(!ownsKeyboard() || event.surface!=viewport_.windowId)return false;
    if(event.physical==PhysicalKeyCode::Escape) { if(event.type==InputEventType::KeyDown)cancel();return true; }
    const auto index=actionKey(event.physical);
    if(!index)return false;
    const bool down=event.type==InputEventType::KeyDown;
    // Repeats cannot resurrect a key cleared by lost ownership, or queue
    // another jump while Space is held. A new physical press is required.
    if(down && event.repeat)return true;
    if(*index==4 && down && !held_[*index])jumpPending_=true;
    held_[*index]=down;
    return true;
}
}
