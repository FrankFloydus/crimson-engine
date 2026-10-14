#include "GameInput.h"
#include <SDL3/SDL_video.h>
#include <cmath>

namespace crimson {
namespace {
bool sameWindow(std::uint32_t event,std::uint32_t owner) { return event==owner; }
std::optional<Vec2> eventPosition(float x,float y,SDL_WindowID id,bool desktop) {
    if(desktop && id) {
        int wx{},wy{};auto* window=SDL_GetWindowFromID(id);
        if(!window || !SDL_GetWindowPosition(window,&wx,&wy))return {};
        x+=static_cast<float>(wx);y+=static_cast<float>(wy);
    }
    if(!std::isfinite(x) || !std::isfinite(y))return {};
    return Vec2{x,y};
}
}
bool GameInput::contains(Vec2 position) const {
    return std::isfinite(position[0]) && std::isfinite(position[1]) &&
        position[0]>=viewport_.x && position[1]>=viewport_.y &&
        position[0]<viewport_.x+viewport_.width && position[1]<viewport_.y+viewport_.height;
}
void GameInput::setViewport(const ViewportRegion& viewport,bool enabled,bool keyboardBlocked) {
    if(viewport.windowId!=viewport_.windowId || !enabled)projection_.reset();
    if(viewport.windowId!=viewport_.windowId || !enabled || keyboardBlocked || viewport.inputBlocked ||
       viewport.width<=0 || viewport.height<=0)cancel();
    viewport_=viewport;enabled_=enabled;blocked_=keyboardBlocked || viewport.inputBlocked;
    if(pointer_ && !contains(*pointer_))pointer_.reset();
}
void GameInput::cancel(){
    pointer_.reset();moveClick_.reset();moveRay_.reset();leftHeld_=false;armed_=false;++cancellationRevision_;
    // ImGui may acknowledge this native press in a later frame. Keep its
    // cancellation until that acknowledgment, so it cannot revive the gesture.
    if(nativeClick_)nativeClickCanceled_=true;
}
std::optional<Ray> GameInput::pointerRay(Vec2 position,const Camera& fallback) const {
    if(projection_) {
        const auto& image=*projection_;
        const float x=(position[0]-image.viewport.x)/image.viewport.width*image.width;
        const float y=(position[1]-image.viewport.y)/image.viewport.height*image.height;
        return image.camera.screenRay(x,y,{0,0,static_cast<float>(image.width),static_cast<float>(image.height)});
    }
    return fallback.screenRay(position[0],position[1],viewport_);
}
void GameInput::submitPointer(Vec2 position,bool moveClick) {
    if(!ownsKeyboard() || !contains(position)) { pointer_.reset();return; }
    pointer_=position;
    if(moveClick) {
        moveClick_=position;moveRay_.reset();
        if(projection_)moveRay_=pointerRay(position,projection_->camera);
    }
}
void GameInput::submitUiPointer(Vec2 position,bool moveClick,bool leftHeld) {
    bool canHold=ownsKeyboard() && contains(position) && (leftHeld_ || moveClick);
    // SDL and ImGui report the same press at different stages (and sometimes
    // different frames). Never turn its UI acknowledgement into another goal
    // through a later follow-camera pose, or replace button-down coordinates
    // with the cursor's position after it has moved.
    if(moveClick && nativeClick_) {
        if(nativeClickCanceled_) {
            nativeClick_.reset();nativeClickSubmitted_=nativeClickCanceled_=nativeClickReleased_=false;
            cancel();return;
        }
        canHold=canHold && !nativeClickReleased_;
        if(!nativeClickSubmitted_)submitPointer(*nativeClick_,true);
        nativeClick_.reset();nativeClickSubmitted_=nativeClickCanceled_=nativeClickReleased_=false;
        submitPointer(position,false);
    } else submitPointer(position,moveClick);
    // A held widget alone cannot restart a gesture after Escape, capture or
    // focus cancellation. A fresh owned press is required to start it again.
    leftHeld_=leftHeld && canHold;
}
void GameInput::setProjection(const Camera& camera,std::uint32_t width,std::uint32_t height) {
    if(!width || !height || viewport_.width<=0 || viewport_.height<=0) { projection_.reset();return; }
    projection_=Projection{camera,viewport_,width,height};
}
std::optional<Vec2> GameInput::pointerPosition() const {
    return ownsKeyboard() && viewport_.hovered && pointer_ && contains(*pointer_)?pointer_:std::nullopt;
}
std::optional<Vec2> GameInput::consumeMoveClick() {
    const auto result=ownsKeyboard()?moveClick_:std::nullopt;moveClick_.reset();moveRay_.reset();return result;
}
std::optional<Ray> GameInput::consumeMoveRay(const Camera& fallback) {
    const auto result=ownsKeyboard() && moveClick_?
        (moveRay_?moveRay_:fallback.screenRay((*moveClick_)[0],(*moveClick_)[1],viewport_)):std::nullopt;
    moveClick_.reset();moveRay_.reset();return result;
}
std::optional<Ray> GameInput::heldMoveRay(const Camera& fallback) const {
    const auto pointer=pointerPosition();
    return leftHeld_ && pointer?pointerRay(*pointer,fallback):std::nullopt;
}
bool GameInput::handleEvent(const SDL_Event& event) {
    if(event.type==SDL_EVENT_WINDOW_FOCUS_LOST || event.type==SDL_EVENT_WINDOW_HIDDEN ||
       event.type==SDL_EVENT_WINDOW_MINIMIZED || event.type==SDL_EVENT_WINDOW_CLOSE_REQUESTED ||
       event.type==SDL_EVENT_WINDOW_DESTROYED) {
        if(sameWindow(event.window.windowID,viewport_.windowId))cancel();
        return false;
    }
    if(event.type==SDL_EVENT_MOUSE_BUTTON_UP && event.button.button==SDL_BUTTON_LEFT) {
        // SDL may deliver release to another owned native window after the
        // pointer leaves Game. Release ends holding without canceling its goal.
        leftHeld_=false;
        if(nativeClick_)nativeClickReleased_=true;
        return ownsKeyboard() && sameWindow(event.button.windowID,viewport_.windowId);
    }
    if(event.type==SDL_EVENT_MOUSE_MOTION) {
        const auto position=eventPosition(event.motion.x,event.motion.y,event.motion.windowID,desktopCoordinates_);
        if(!sameWindow(event.motion.windowID,viewport_.windowId) || !position || !contains(*position)) {
            pointer_.reset();return false;
        }
        submitPointer(*position,false);return ownsKeyboard();
    }
    if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN) {
        const auto position=eventPosition(event.button.x,event.button.y,event.button.windowID,desktopCoordinates_);
        const bool inside=enabled_ && !blocked_ && sameWindow(event.button.windowID,viewport_.windowId) &&
            position && contains(*position);
        if(!inside) {
            cancel();
            if(event.button.button==SDL_BUTTON_LEFT && enabled_ && sameWindow(event.button.windowID,viewport_.windowId) && position && contains(*position)) {
                nativeClick_=position;nativeClickSubmitted_=nativeClickCanceled_=nativeClickReleased_=false;
            }
            return false;
        }
        if(event.button.button==SDL_BUTTON_LEFT) {
            acquire();leftHeld_=true;submitPointer(*position,true);nativeClick_=position;nativeClickSubmitted_=true;
            nativeClickCanceled_=nativeClickReleased_=false;
        }
        return ownsKeyboard();
    }
    if(event.type!=SDL_EVENT_KEY_DOWN && event.type!=SDL_EVENT_KEY_UP)return false;
    if(!ownsKeyboard() || !sameWindow(event.key.windowID,viewport_.windowId))return false;
    if(event.key.scancode==SDL_SCANCODE_ESCAPE){cancel();return true;}
    return false;
}
}
