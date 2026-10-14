#include "runtime/GameInput.h"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
SDL_Event key(SDL_Scancode code,SDL_WindowID window=9) {SDL_Event event{};event.type=SDL_EVENT_KEY_DOWN;event.key.windowID=window;event.key.scancode=code;return event;}
SDL_Event mouse(float x,float y,SDL_EventType type=SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_WindowID window=9,std::uint8_t button=SDL_BUTTON_LEFT) {
    SDL_Event event{};event.type=type;event.button.windowID=window;event.button.button=button;event.button.x=x;event.button.y=y;return event;
}
SDL_Event motion(float x,float y,SDL_WindowID window=9) {SDL_Event event{};event.type=SDL_EVENT_MOUSE_MOTION;event.motion.windowID=window;event.motion.x=x;event.motion.y=y;return event;}
void pointerAndClickEdges() {
    GameInput input;ViewportRegion region{10,20,300,200,true,false,9};input.setViewport(region,true,false);
    require(input.viewport().x==10 && input.viewport().windowId==9,"Game input lost viewport coordinates or native owner.");
    require(!input.handleEvent(motion(50,60)) && !input.pointerPosition(),"Unowned pointer motion acquired gameplay input.");
    input.acquire();
    require(input.ownsKeyboard() && !input.consumeMoveClick(),"Play acquisition fabricated a movement destination.");
    for(const auto code:{SDL_SCANCODE_W,SDL_SCANCODE_A,SDL_SCANCODE_S,SDL_SCANCODE_D})
        require(!input.handleEvent(key(code)) && !input.consumeMoveClick(),"WASD still drives gameplay input.");
    require(input.handleEvent(motion(50,60)) && input.pointerPosition()==Vec2{50,60},"Owned pointer motion did not supply facing coordinates.");
    require(!input.consumeMoveClick(),"Pointer motion fabricated a movement click.");
    require(input.handleEvent(mouse(70,80)),"Left click was not owned by Game.");
    input.handleEvent(mouse(70,80,SDL_EVENT_MOUSE_BUTTON_UP));
    require(input.consumeMoveClick()==Vec2{70,80} && !input.consumeMoveClick(),"A short click was lost or replayed across fixed ticks.");
    input.handleEvent(mouse(80,90));input.handleEvent(mouse(90,100));
    input.handleEvent(motion(110,120));
    require(input.pointerPosition()==Vec2{110,120} && input.consumeMoveClick()==Vec2{90,100},"Pointer facing replaced the latest clicked destination.");
    input.handleEvent(mouse(90,100));const auto revision=input.cancellationRevision();
    input.handleEvent(motion(400,300));
    require(input.ownsKeyboard() && !input.pointerPosition() && input.cancellationRevision()==revision &&
        input.consumeMoveClick()==Vec2{90,100},"Leaving the image canceled travel instead of only pointer facing.");
    input.handleEvent(motion(60,70));input.handleEvent(mouse(90,100));
    require(!input.handleEvent(motion(60,70,10)) && !input.pointerPosition() && input.consumeMoveClick()==Vec2{90,100},
        "Another native window supplied facing or canceled the queued destination.");
    input.handleEvent(mouse(60,70,SDL_EVENT_MOUSE_BUTTON_DOWN,9,SDL_BUTTON_RIGHT));
    require(!input.consumeMoveClick(),"Right click issued a movement destination.");
    input.handleEvent(motion(60,70));region.hovered=false;input.setViewport(region,true,false);
    require(!input.pointerPosition(),"A covered image retained cursor-facing input.");
}
void cancellationAndUiHandoff() {
    GameInput input;ViewportRegion region{10,20,300,200,true,false,9};input.setViewport(region,true,false);
    input.handleEvent(mouse(50,50));auto revision=input.cancellationRevision();
    input.setViewport(region,true,true);
    require(!input.ownsKeyboard() && !input.pointerPosition() && !input.consumeMoveClick() && input.cancellationRevision()>revision,
        "Text/widget capture retained a queued move or hid its cancellation.");
    require(!input.handleEvent(mouse(75,85)),"Prior blocked UI frame unexpectedly accepted its SDL click.");
    revision=input.cancellationRevision();input.setViewport(region,true,false);input.acquire();input.submitUiPointer({90,95},true);
    require(input.ownsKeyboard() && input.pointerPosition()==Vec2{90,95} && input.consumeMoveClick()==Vec2{75,85} &&
        input.cancellationRevision()==revision,"Authoritative image click did not recover text-to-Game handoff.");
    input.submitPointer({80,90},true);input.submitPointer({std::numeric_limits<float>::quiet_NaN(),90},false);
    require(!input.pointerPosition() && input.consumeMoveClick()==Vec2{80,90},"Invalid pointer motion corrupted the destination.");
    input.submitPointer({80,90},true);revision=input.cancellationRevision();input.handleEvent(mouse(500,500));
    require(!input.ownsKeyboard() && !input.consumeMoveClick() && input.cancellationRevision()>revision,
        "Clicking another panel did not cancel gameplay and its destination.");
    input.acquire();input.submitPointer({80,90},true);revision=input.cancellationRevision();
    SDL_Event focus{};focus.type=SDL_EVENT_WINDOW_FOCUS_LOST;focus.window.windowID=9;input.handleEvent(focus);
    input.acquire();input.submitPointer({100,110},true);
    require(input.cancellationRevision()>revision && input.consumeMoveClick()==Vec2{100,110},
        "Cancel/reacquire in one frame concealed cancellation or discarded its fresh destination.");
    require(input.handleEvent(key(SDL_SCANCODE_ESCAPE)) && !input.ownsKeyboard() && !input.pointerPosition(),
        "Escape failed to release gameplay ownership.");
    input.acquire();input.submitPointer({80,90},true);revision=input.cancellationRevision();
    region.windowId=10;input.setViewport(region,true,false);
    require(!input.ownsKeyboard() && !input.consumeMoveClick() && input.cancellationRevision()>revision,
        "Moving Game to a new native window retained stale input.");
    input.acquire();input.submitPointer({80,90},true);input.setViewport(region,false,false);
    require(!input.ownsKeyboard() && !input.consumeMoveClick(),"Disabled Game retained its destination.");
}
void uiAcknowledgementPreservesNativePress() {
    GameInput input;ViewportRegion region{10,20,300,200,true,false,9};input.setViewport(region,true,false);input.acquire();
    input.handleEvent(mouse(75,85));input.handleEvent(motion(160,170));input.submitUiPointer({160,170},true);
    require(input.consumeMoveClick()==Vec2{75,85} && input.pointerPosition()==Vec2{160,170},
        "UI acknowledgement replaced mouse-down coordinates with later cursor motion.");
    input.handleEvent(mouse(80,90));
    require(input.consumeMoveClick()==Vec2{80,90},"Native click was not available before its delayed UI acknowledgement.");
    input.submitUiPointer({180,190},true);
    require(!input.consumeMoveClick() && input.pointerPosition()==Vec2{180,190},
        "Delayed UI acknowledgement replayed a native press that was already consumed.");
    input.submitUiPointer({110,120},true);
    require(input.consumeMoveClick()==Vec2{110,120},"A UI-only click was lost after native acknowledgement completed.");
}
void displayedProjectionIsFrozenAtPress() {
    const ViewportRegion displayed{450,89,1930,1163,true,false,9};
    Camera camera;camera.position={5.222835f,6.438390f,4.948770f};camera.target={0,0,0};
    camera.fov=60;camera.fovAxis=CameraFovAxis::Vertical;camera.nearPlane=.1f;camera.farPlane=2035;
    const Vec3 destination{3,0,-2};
    for(const auto target:std::array<std::array<std::uint32_t,2>,3>{{{2895,1745},{965,582},{2300,1200}}}) {
        GameInput input;input.setViewport(displayed,true,false);input.acquire();input.setProjection(camera,target[0],target[1]);
        const auto pixel=math::project(destination,camera.projectionMatrix(static_cast<float>(target[0])/target[1],ClipDepth::ZeroToOne)*camera.viewMatrix(),
            {0,0,static_cast<float>(target[0]),static_cast<float>(target[1])},ClipDepth::ZeroToOne);
        require(pixel.has_value(),"Cannot project the Game destination fixture.");
        const Vec2 desktop{displayed.x+(*pixel)[0]/target[0]*displayed.width,displayed.y+(*pixel)[1]/target[1]*displayed.height};
        require(input.handleEvent(mouse(desktop[0],desktop[1])),"Displayed destination did not land inside its Game image.");
        input.handleEvent(motion(desktop[0]+50,desktop[1]+30));input.submitUiPointer({desktop[0]+50,desktop[1]+30},true);
        auto followed=camera;followed.position=followed.position+Vec3{4,1,3};followed.target=followed.target+Vec3{4,1,3};
        auto resized=displayed;resized.x=2560;resized.width=1200;resized.height=600;
        input.setViewport(resized,true,false);input.setProjection(followed,1800,900);
        const auto ray=input.consumeMoveRay(followed);
        const auto hit=ray?math::intersectPlane(*ray,{0,0,0},{0,1,0}):std::nullopt;
        require(ray && math::length(ray->origin-camera.position)<1e-5 && hit && math::length(*hit-destination)<.001,
            "A followed camera, resized/translated image or physical target scale changed the captured click ray.");
        require(!input.consumeMoveRay(followed),"Captured click ray replayed after consumption.");
    }
    GameInput fallback;fallback.setViewport(displayed,true,false);fallback.acquire();
    fallback.submitPointer({displayed.x+displayed.width*.5f,displayed.y+displayed.height*.5f},true);
    require(fallback.consumeMoveRay(camera).has_value(),"A click without a cached image lost its fallback ray.");
}
void heldGestureOwnershipAndRelease() {
    GameInput input;ViewportRegion region{10,20,300,200,true,false,9};Camera camera;camera.position={0,5,5};camera.target={};
    input.setViewport(region,true,false);input.acquire();input.setProjection(camera,600,400);
    input.submitUiPointer({160,120},false,true);
    require(!input.heldMoveRay(camera),"A button held outside Game acquired a movement gesture on entry.");
    input.handleEvent(mouse(160,120));require(input.consumeMoveRay(camera).has_value() && input.heldMoveRay(camera).has_value(),"Owned press failed to start holding.");
    input.handleEvent(motion(180,120));input.submitUiPointer({180,120},true,true);
    require(!input.consumeMoveRay(camera) && input.heldMoveRay(camera).has_value(),"Delayed native acknowledgement replayed its press or lost holding.");
    input.handleEvent(motion(500,120));require(!input.heldMoveRay(camera) && input.ownsKeyboard(),"Leaving the image continued held targeting or canceled ownership.");
    input.handleEvent(motion(180,120));require(input.heldMoveRay(camera).has_value(),"Reentering Game did not resume its owned held gesture.");
    input.handleEvent(mouse(500,120,SDL_EVENT_MOUSE_BUTTON_UP,10));
    input.handleEvent(motion(180,120));require(!input.heldMoveRay(camera) && input.ownsKeyboard(),"Release in another native window left a stuck hold or canceled its destination.");
    input.submitUiPointer({160,120},true,true);require(input.heldMoveRay(camera).has_value(),"UI-only image press did not begin holding.");
    input.handleEvent(key(SDL_SCANCODE_ESCAPE));input.acquire();input.submitUiPointer({160,120},false,true);
    require(!input.heldMoveRay(camera) && !input.consumeMoveRay(camera),"Held motion restarted movement after Escape without a new press.");
    input.submitUiPointer({160,120},true,true);input.setViewport(region,true,true);input.setViewport(region,true,false);input.acquire();input.submitUiPointer({160,120},false,true);
    require(!input.heldMoveRay(camera),"A blocked widget gesture resumed holding after capture ended.");
    input.handleEvent(mouse(160,120));input.handleEvent(mouse(160,120,SDL_EVENT_MOUSE_BUTTON_UP));
    require(input.consumeMoveRay(camera).has_value() && !input.heldMoveRay(camera),"Fast press/release lost its click or remained held.");
}
void heldRayUsesCurrentDisplayedProjection() {
    GameInput input;const ViewportRegion region{100,200,800,600,true,false,9};Camera camera;camera.position={0,8,8};camera.target={};camera.fovAxis=CameraFovAxis::Vertical;
    input.setViewport(region,true,false);input.acquire();input.setProjection(camera,1600,1200);
    input.handleEvent(mouse(500,500));
    auto moved=camera;moved.position=moved.position+Vec3{3,0,-2};moved.target=moved.target+Vec3{3,0,-2};input.setProjection(moved,1600,1200);
    const auto pressed=input.consumeMoveRay(moved),held=input.heldMoveRay(camera);
    const auto initial=pressed?math::intersectPlane(*pressed,{},{0,1,0}):std::nullopt;
    const auto current=held?math::intersectPlane(*held,{},{0,1,0}):std::nullopt;
    require(initial && math::length(*initial)<.001,"Holding changed the first click's frozen projection.");
    require(current && math::length(*current-Vec3{3,0,-2})<.001,"Stationary held cursor ignored a newly displayed follow-camera projection.");
    input.submitUiPointer({550,500},true,true);
    require(!input.consumeMoveRay(camera) && input.heldMoveRay(camera).has_value(),"UI acknowledgement duplicated the first click after projection changed.");
    input.submitUiPointer({550,500},false,false);
    require(!input.heldMoveRay(camera),"UI release left held targeting active.");
    input.setViewport(region,true,true);input.handleEvent(mouse(500,500));input.setViewport(region,true,false);input.acquire();input.submitUiPointer({500,500},true,true);
    require(input.consumeMoveRay(camera).has_value() && input.heldMoveRay(camera).has_value(),"Text-to-image UI recovery lost the held gesture.");
}
void delayedAcknowledgementRespectsCancellationAndRelease() {
    GameInput input;const ViewportRegion region{10,20,300,200,true,false,9};Camera camera;camera.position={0,5,5};camera.target={};
    input.setViewport(region,true,false);input.acquire();
    input.handleEvent(mouse(160,120));input.handleEvent(key(SDL_SCANCODE_ESCAPE));
    // EditorSession tentatively acquires Game when the delayed ImGui image
    // press appears. Its earlier native cancellation remains authoritative.
    input.acquire();input.submitUiPointer({160,120},true,true);
    require(!input.ownsKeyboard() && !input.consumeMoveRay(camera) && !input.heldMoveRay(camera),"Delayed UI acknowledgment revived an Escape-canceled native press.");
    input.acquire();input.submitUiPointer({160,120},false,true);
    require(!input.heldMoveRay(camera),"Trickled held state revived a canceled press after acknowledgment.");
    input.handleEvent(mouse(160,120));
    SDL_Event focus{};focus.type=SDL_EVENT_WINDOW_FOCUS_LOST;focus.window.windowID=9;input.handleEvent(focus);
    input.cancel();input.acquire();input.submitUiPointer({160,120},true,true);
    require(!input.ownsKeyboard() && !input.consumeMoveRay(camera),"Repeated cancellation forgot a pending native press's tombstone.");
    input.handleEvent(mouse(160,120));input.handleEvent(mouse(160,120,SDL_EVENT_MOUSE_BUTTON_UP));
    input.submitUiPointer({160,120},true,true);
    require(input.consumeMoveRay(camera).has_value() && !input.heldMoveRay(camera),"UI's trickled down acknowledgment revived a physically released hold or lost its click.");
    input.submitUiPointer({170,120},false,true);
    require(!input.consumeMoveRay(camera) && !input.heldMoveRay(camera),"Later trickled held state replayed a released native gesture.");
    input.handleEvent(mouse(160,120));input.cancel();
    input.handleEvent(mouse(170,120));input.submitUiPointer({170,120},true,true);
    require(input.consumeMoveClick()==Vec2{170,120} && input.heldMoveRay(camera).has_value(),"A fresh native press did not replace an earlier canceled acknowledgment.");
    input.setViewport(region,true,true);input.handleEvent(mouse(160,120));input.handleEvent(mouse(160,120,SDL_EVENT_MOUSE_BUTTON_UP));
    input.setViewport(region,true,false);input.acquire();input.submitUiPointer({170,120},true,true);
    require(input.consumeMoveClick()==Vec2{160,120} && !input.heldMoveRay(camera),"Blocked-to-image recovery revived a physically released hold or lost original press coordinates.");
}
}
int main(){try{pointerAndClickEdges();cancellationAndUiHandoff();uiAcknowledgementPreservesNativePress();displayedProjectionIsFrozenAtPress();heldGestureOwnershipAndRelease();heldRayUsesCurrentDisplayedProjection();delayedAcknowledgementRespectsCancellationAndRelease();std::cout<<"Game pointer input checks passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
