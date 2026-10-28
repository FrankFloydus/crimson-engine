#include "GameInput.h"
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_video.h>
#include <cmath>
#include <optional>
#include <utility>

namespace crimson {
namespace {
std::optional<Vec2> eventPosition(float x,float y,SDL_WindowID id,bool desktop) {
    if(desktop && id) {
        int wx{},wy{};auto* window=SDL_GetWindowFromID(id);
        if(!window || !SDL_GetWindowPosition(window,&wx,&wy))return {};
        x+=static_cast<float>(wx);y+=static_cast<float>(wy);
    }
    if(!std::isfinite(x) || !std::isfinite(y))return {};
    return Vec2{x,y};
}
std::optional<std::size_t> actionKey(SDL_Scancode code) {
    switch(code) {
    case SDL_SCANCODE_W:return 0;
    case SDL_SCANCODE_A:return 1;
    case SDL_SCANCODE_S:return 2;
    case SDL_SCANCODE_D:return 3;
    case SDL_SCANCODE_SPACE:return 4;
    default:return {};
    }
}
}
bool gameInputBlocksEditorEvent(const SDL_Event& event,SDL_WindowID owner,bool gameplayOwned,bool relativeMouseCaptured) {
    if(!gameplayOwned)return false;
    if(event.type==SDL_EVENT_KEY_DOWN)return event.key.windowID==owner && actionKey(event.key.scancode).has_value();
    if(!relativeMouseCaptured)return false;
    switch(event.type) {
    case SDL_EVENT_MOUSE_MOTION:return event.motion.windowID==owner;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:return event.button.windowID==owner;
    case SDL_EVENT_MOUSE_WHEEL:return event.wheel.windowID==owner;
    default:return false;
    }
}
GameInput::~GameInput() { releaseMouseCapture(); }
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
    if(auto* window=captureWindowId_?SDL_GetWindowFromID(captureWindowId_):nullptr)
        SDL_SetWindowRelativeMouseMode(window,false);
    captureWindowId_=0;discardNextMotion_=false;
}
void GameInput::cancel() {
    releaseMouseCapture();held_.fill(false);lookDelta_={};jumpPending_=false;tracePending_=false;traceHeld_=false;armed_=false;++cancellationRevision_;
    if(imageAcquisition_==ImageAcquisition::Pending)imageAcquisition_=ImageAcquisition::Canceled;
}
void GameInput::syncMouseCapture() {
    if(!ownsKeyboard()) { releaseMouseCapture();return; }
    auto* window=viewport_.windowId?SDL_GetWindowFromID(viewport_.windowId):nullptr;
    if(!window || SDL_GetKeyboardFocus()!=window) { cancel();return; }
    if(captureWindowId_==viewport_.windowId && SDL_GetWindowRelativeMouseMode(window))return;
    releaseMouseCapture();
    if(!SDL_SetWindowRelativeMouseMode(window,true)) { cancel();return; }
    captureWindowId_=viewport_.windowId;
    // Acquisition can include a platform cursor reposition. Never turn that
    // transition or pre-capture motion into a jump in the player's view.
    float discardedX{},discardedY{};SDL_GetRelativeMouseState(&discardedX,&discardedY);
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
bool GameInput::handleEvent(const SDL_Event& event) {
    if(event.type==SDL_EVENT_WINDOW_FOCUS_LOST || event.type==SDL_EVENT_WINDOW_HIDDEN ||
       event.type==SDL_EVENT_WINDOW_MINIMIZED || event.type==SDL_EVENT_WINDOW_CLOSE_REQUESTED ||
       event.type==SDL_EVENT_WINDOW_DESTROYED) {
        if(event.window.windowID==viewport_.windowId)cancel();
        return false;
    }
    if(event.type==SDL_EVENT_MOUSE_MOTION) {
        if(!ownsKeyboard() || event.motion.windowID!=viewport_.windowId)return false;
        if(discardNextMotion_) { discardNextMotion_=false;return true; }
        const Vec2 delta{event.motion.xrel,event.motion.yrel};
        const Vec2 accumulated{lookDelta_[0]+delta[0],lookDelta_[1]+delta[1]};
        if(math::finite(delta) && math::finite(accumulated))lookDelta_=accumulated;
        return true;
    }
    if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN) {
        const auto pressTrace=[&](bool fire) {
            if(event.button.button!=SDL_BUTTON_LEFT)return;
            if(fire&&!traceHeld_)tracePending_=true;
            traceHeld_=true;
        };
        // During relative capture the platform cursor need not be in the image
        // (Play also acquires automatically). Its absolute position is irrelevant.
        if(ownsKeyboard() && captureWindowId_ && captureWindowId_==event.button.windowID) {
            pressTrace(true);return true;
        }
        const bool alreadyOwned=ownsKeyboard();
        const auto position=eventPosition(event.button.x,event.button.y,event.button.windowID,desktopCoordinates_);
        const bool imagePress=enabled_ && event.button.windowID==viewport_.windowId && position && contains(*position);
        if(!imagePress || blocked_)cancel();
        if(imagePress && event.button.button==SDL_BUTTON_LEFT) {
            // The click that acquires Game is only an input handoff. A new
            // press while already owned performs the trace action.
            pressTrace(alreadyOwned&&!blocked_);
            imageAcquisition_=ImageAcquisition::Pending;
            acquire();
        }
        return ownsKeyboard() && imagePress;
    }
    if(event.type==SDL_EVENT_MOUSE_BUTTON_UP || event.type==SDL_EVENT_MOUSE_WHEEL) {
        const auto window=event.type==SDL_EVENT_MOUSE_BUTTON_UP?event.button.windowID:event.wheel.windowID;
        if(event.type==SDL_EVENT_MOUSE_BUTTON_UP && event.button.button==SDL_BUTTON_LEFT && window==viewport_.windowId)traceHeld_=false;
        return ownsKeyboard() && window==viewport_.windowId;
    }
    if(event.type!=SDL_EVENT_KEY_DOWN && event.type!=SDL_EVENT_KEY_UP)return false;
    if(!ownsKeyboard() || event.key.windowID!=viewport_.windowId)return false;
    if(event.key.scancode==SDL_SCANCODE_ESCAPE) { if(event.type==SDL_EVENT_KEY_DOWN)cancel();return true; }
    const auto index=actionKey(event.key.scancode);
    if(!index)return false;
    const bool down=event.type==SDL_EVENT_KEY_DOWN;
    // Repeats cannot resurrect a key cleared by lost ownership, or queue
    // another jump while Space is held. A new physical press is required.
    if(down && event.key.repeat)return true;
    if(*index==4 && down && !held_[*index])jumpPending_=true;
    held_[*index]=down;
    return true;
}
}
