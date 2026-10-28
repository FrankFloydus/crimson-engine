#include "runtime/GameInput.h"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
constexpr ViewportRegion Region{10,20,300,200,true,false,9};
SDL_Event key(SDL_Scancode code,bool down=true,SDL_WindowID window=9,bool repeat=false) {
    SDL_Event event{};event.type=down?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;
    event.key.windowID=window;event.key.scancode=code;event.key.repeat=repeat;return event;
}
SDL_Event mouse(float x,float y,SDL_EventType type=SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_WindowID window=9,std::uint8_t button=SDL_BUTTON_LEFT) {
    SDL_Event event{};event.type=type;event.button.windowID=window;event.button.button=button;event.button.x=x;event.button.y=y;return event;
}
SDL_Event motion(float dx,float dy,SDL_WindowID window=9) {
    SDL_Event event{};event.type=SDL_EVENT_MOUSE_MOTION;event.motion.windowID=window;
    event.motion.x=-1000;event.motion.y=5000;event.motion.xrel=dx;event.motion.yrel=dy;return event;
}
void idle(const GameInput& input,const char* message) {
    require(!input.ownsKeyboard() && input.movement()==Vec2{},message);
}
void heldMovementAndWindowRouting() {
    GameInput input;input.acquire();idle(input,"An unavailable Game acquired input.");
    input.setViewport(Region,true,false);
    require(input.viewport().x==10 && input.viewport().windowId==9,"Game lost its native image owner.");
    require(!input.handleEvent(key(SDL_SCANCODE_W)) && !input.handleEvent(motion(10,20)),"Unowned actions reached Game.");
    input.acquire();require(input.ownsKeyboard() && input.movement()==Vec2{},"Play acquisition fabricated movement.");
    require(input.handleEvent(key(SDL_SCANCODE_W)) && input.handleEvent(key(SDL_SCANCODE_D)),"Game did not consume its movement keys.");
    require(input.movement()==Vec2{1,1} && input.movement()==Vec2{1,1},"Continuous movement was consumed or used the wrong axes.");
    require(!input.handleEvent(key(SDL_SCANCODE_S,true,10)) && input.movement()==Vec2{1,1},"A different native window supplied movement.");
    input.handleEvent(key(SDL_SCANCODE_A));input.handleEvent(key(SDL_SCANCODE_S));
    require(input.movement()==Vec2{},"Opposite movement keys did not cancel independently.");
    input.handleEvent(key(SDL_SCANCODE_W,false));input.handleEvent(key(SDL_SCANCODE_D,false));
    require(input.movement()==Vec2{-1,-1},"Releasing one direction lost its opposite held key.");
    auto covered=Region;covered.hovered=false;input.setViewport(covered,true,false);
    require(input.movement()==Vec2{-1,-1},"Captured movement required hovering the image.");
    require(!input.handleEvent(key(SDL_SCANCODE_F1)),"FPS movement stole an unrelated debug key.");
    input.cancel();input.acquire();input.handleEvent(key(SDL_SCANCODE_W,true,9,true));
    require(input.movement()==Vec2{},"A key repeat revived movement canceled during lost ownership.");
    input.handleEvent(key(SDL_SCANCODE_W,false));input.handleEvent(key(SDL_SCANCODE_W));
    require(input.movement()==Vec2{0,1},"A fresh key press did not resume movement after cancellation.");
}
void jumpUsesPhysicalPressEdges() {
    GameInput input;input.setViewport(Region,true,false);input.acquire();
    input.handleEvent(key(SDL_SCANCODE_SPACE));input.handleEvent(key(SDL_SCANCODE_SPACE,false));
    require(input.consumeJump() && !input.consumeJump(),"A short jump press was lost or replayed across simulation ticks.");
    input.handleEvent(key(SDL_SCANCODE_SPACE));require(input.consumeJump(),"A fresh Space press did not request jumping.");
    input.handleEvent(key(SDL_SCANCODE_SPACE,true,9,true));input.handleEvent(key(SDL_SCANCODE_SPACE));
    require(!input.consumeJump(),"Holding Space queued repeated jumps.");
    input.handleEvent(key(SDL_SCANCODE_SPACE,false));input.handleEvent(key(SDL_SCANCODE_SPACE));
    input.cancel();input.acquire();
    require(!input.consumeJump(),"Cancel/reacquire retained a queued jump.");
    input.handleEvent(key(SDL_SCANCODE_SPACE,true,9,true));require(!input.consumeJump(),"A Space repeat rearmed jumping after cancellation.");
    input.handleEvent(key(SDL_SCANCODE_SPACE,false));input.handleEvent(key(SDL_SCANCODE_SPACE));
    require(input.consumeJump(),"A new physical jump remained suppressed after reacquisition.");
}
void traceUsesOwnedPhysicalPressEdges() {
    GameInput input;input.setViewport(Region,true,false);
    require(input.handleEvent(mouse(50,60))&&input.ownsKeyboard()&&!input.consumeTrace(),"The Game acquisition click fired a trace.");
    input.acquireFromImageClick();input.handleEvent(mouse(50,60));
    require(!input.consumeTrace(),"Holding or acknowledging the acquisition click fired a trace.");
    input.handleEvent(mouse(50,60,SDL_EVENT_MOUSE_BUTTON_UP));
    require(input.handleEvent(mouse(50,60)),"Game did not consume an owned LMB press.");
    input.handleEvent(mouse(50,60,SDL_EVENT_MOUSE_BUTTON_UP));
    require(input.consumeTrace()&&!input.consumeTrace(),"A short owned LMB press was lost or consumed twice.");
    input.handleEvent(mouse(50,60));require(input.consumeTrace(),"A new LMB press did not queue a trace.");
    input.handleEvent(mouse(50,60));input.handleEvent(mouse(50,60));
    require(!input.consumeTrace(),"Duplicate held LMB-down events generated repeated traces.");
    input.handleEvent(mouse(50,60,SDL_EVENT_MOUSE_BUTTON_UP,10));input.handleEvent(mouse(50,60));
    require(!input.consumeTrace(),"A foreign native window released the held trace button.");
    input.handleEvent(mouse(50,60,SDL_EVENT_MOUSE_BUTTON_UP));input.handleEvent(mouse(50,60,SDL_EVENT_MOUSE_BUTTON_UP));
    require(!input.consumeTrace(),"LMB release events fired a trace.");
    for(const auto button:{SDL_BUTTON_RIGHT,SDL_BUTTON_MIDDLE}) {
        input.handleEvent(mouse(50,60,SDL_EVENT_MOUSE_BUTTON_DOWN,9,button));
        input.handleEvent(mouse(50,60,SDL_EVENT_MOUSE_BUTTON_UP,9,button));
        require(!input.consumeTrace(),"Another mouse button queued the LMB trace action.");
    }
    require(!input.handleEvent(key(SDL_SCANCODE_F))&&!input.handleEvent(key(SDL_SCANCODE_F,false))&&!input.consumeTrace(),
        "The removed F binding consumed editor input or queued a trace.");
    input.handleEvent(mouse(50,60));input.cancel();input.acquire();
    require(!input.consumeTrace(),"Canceled input retained a queued LMB trace.");
    input.handleEvent(mouse(50,60));require(input.consumeTrace(),"A fresh owned LMB trace remained suppressed after cancellation.");
    require(!input.handleEvent(mouse(50,60,SDL_EVENT_MOUSE_BUTTON_DOWN,10))&&!input.consumeTrace(),"A foreign native window supplied a trace.");
    input.acquire();input.handleEvent(mouse(900,700));
    require(!input.ownsKeyboard()&&!input.consumeTrace(),"An uncaptured editor-panel click fired a trace or retained gameplay ownership.");
}
void relativeLookAccumulatesOnce() {
    GameInput input;input.setViewport(Region,true,false);input.acquire();
    require(input.handleEvent(motion(12,-3)) && input.handleEvent(motion(-2,7)),"Game did not consume owned relative motion.");
    require(!input.handleEvent(motion(999,999,10)),"Another native window supplied mouse look.");
    input.handleEvent(motion(std::numeric_limits<float>::quiet_NaN(),2));
    require(input.consumeLookDelta()==Vec2{10,4} && input.consumeLookDelta()==Vec2{},"Look did not accumulate raw right/down pixels exactly once.");
    input.handleEvent(motion(30,20));input.cancel();input.acquire();
    require(input.consumeLookDelta()==Vec2{},"Canceled mouse motion replayed after reacquisition.");
    auto resized=Region;resized.x=-1500;resized.width=600;resized.height=300;
    input.setViewport(resized,true,false);input.handleEvent(motion(5,6));
    require(input.consumeLookDelta()==Vec2{5,6},"Image location or framebuffer size rescaled relative look.");
}
void ownershipCancellationClearsActions() {
    for(const auto type:{SDL_EVENT_WINDOW_FOCUS_LOST,SDL_EVENT_WINDOW_HIDDEN,SDL_EVENT_WINDOW_MINIMIZED,
                         SDL_EVENT_WINDOW_CLOSE_REQUESTED,SDL_EVENT_WINDOW_DESTROYED}) {
        GameInput input;input.setViewport(Region,true,false);input.acquire();
        input.handleEvent(key(SDL_SCANCODE_W));input.handleEvent(key(SDL_SCANCODE_SPACE));input.handleEvent(mouse(50,60));input.handleEvent(motion(3,4));
        const auto revision=input.cancellationRevision();
        SDL_Event event{};event.type=type;event.window.windowID=10;input.handleEvent(event);
        require(input.ownsKeyboard() && input.cancellationRevision()==revision,"An unrelated native window canceled Game.");
        event.window.windowID=9;input.handleEvent(event);idle(input,"Window interruption retained owned movement.");
        require(input.cancellationRevision()>revision && !input.consumeJump() && !input.consumeTrace() && input.consumeLookDelta()==Vec2{},"Window interruption retained pending actions.");
    }
    for(int reason=0;reason<6;++reason) {
        GameInput input;input.setViewport(Region,true,false);input.acquire();
        input.handleEvent(key(SDL_SCANCODE_D));input.handleEvent(key(SDL_SCANCODE_SPACE));input.handleEvent(mouse(50,60));input.handleEvent(motion(3,4));
        auto region=Region;
        if(reason==1)region.inputBlocked=true;
        if(reason==2)region.windowId=10;
        if(reason==4)region.width=0;
        if(reason==5)region.height=std::numeric_limits<float>::quiet_NaN();
        const auto revision=input.cancellationRevision();input.setViewport(region,reason!=3,reason==0);
        idle(input,"Blocked, disabled, migrated or invalid Game retained movement.");
        require(input.cancellationRevision()>revision && !input.consumeJump() && !input.consumeTrace() && input.consumeLookDelta()==Vec2{},"Viewport interruption retained pending actions.");
    }
    GameInput input;input.setViewport(Region,true,false);input.acquire();input.handleEvent(key(SDL_SCANCODE_W));
    require(input.handleEvent(key(SDL_SCANCODE_ESCAPE)),"Escape was not consumed by the gameplay owner.");
    idle(input,"Escape retained gameplay ownership.");
}
void imageClickOnlyAcquiresAndHonorsCancellation() {
    GameInput input;input.setViewport(Region,true,true);
    require(!input.handleEvent(mouse(50,60)),"Blocked native press unexpectedly acquired Game.");
    input.setViewport(Region,true,false);input.acquireFromImageClick();
    require(input.ownsKeyboard() && input.movement()==Vec2{} && !input.consumeJump() && !input.consumeTrace() && input.consumeLookDelta()==Vec2{},
        "The first image click leaving text editing failed to acquire or fabricated an FPS action.");
    input.handleEvent(mouse(50,60,SDL_EVENT_MOUSE_BUTTON_UP));
    require(input.ownsKeyboard(),"Releasing the acquisition click released keyboard ownership.");
    input.handleEvent(mouse(60,70));input.handleEvent(key(SDL_SCANCODE_ESCAPE));
    input.acquireFromImageClick();idle(input,"Delayed UI acknowledgement revived an Escape-canceled native press.");
    input.handleEvent(mouse(60,70));input.acquireFromImageClick();
    require(input.ownsKeyboard(),"A fresh image press could not reacquire Game.");
    input.handleEvent(key(SDL_SCANCODE_W));input.handleEvent(mouse(900,700));
    idle(input,"Clicking another panel retained gameplay movement.");
    input.handleEvent(mouse(60,70,SDL_EVENT_MOUSE_BUTTON_DOWN,10));
    idle(input,"A different native window acquired Game.");
    input.handleEvent(mouse(60,70,SDL_EVENT_MOUSE_BUTTON_DOWN,9,SDL_BUTTON_RIGHT));
    idle(input,"Right click acquired gameplay input.");
}
void missingNativeOwnerCannotCapture() {
    GameInput input;auto region=Region;region.windowId=0;input.setViewport(region,true,false);input.acquire();
    input.handleEvent(key(SDL_SCANCODE_W,true,0));input.syncMouseCapture();
    idle(input,"Native capture retained input without an existing focused native window.");
}
void editorEventGateSeparatesFpsAndUi() {
    require(!gameInputBlocksEditorEvent(mouse(50,60),9,true,false),"The first acquisition click was hidden from the Game image.");
    for(const auto event:{mouse(50,60),mouse(-1000,5000),motion(5,6)}) {
        require(gameInputBlocksEditorEvent(event,9,true,true),"Captured mouse input could activate an editor widget.");
        require(!gameInputBlocksEditorEvent(event,10,true,true),"Game suppressed another native window's UI input.");
        require(!gameInputBlocksEditorEvent(event,9,false,true),"Released Game suppressed editor mouse input.");
    }
    SDL_Event wheel{};wheel.type=SDL_EVENT_MOUSE_WHEEL;wheel.wheel.windowID=9;
    require(gameInputBlocksEditorEvent(wheel,9,true,true),"Captured gameplay wheel input reached editor controls.");
    require(!gameInputBlocksEditorEvent(mouse(50,60,SDL_EVENT_MOUSE_BUTTON_UP),9,true,true),"Acquisition mouse release could leave ImGui's image held.");
    for(const auto code:{SDL_SCANCODE_W,SDL_SCANCODE_A,SDL_SCANCODE_S,SDL_SCANCODE_D,SDL_SCANCODE_SPACE}) {
        require(gameInputBlocksEditorEvent(key(code),9,true,true),"FPS press edges could activate an editor shortcut or widget.");
        require(!gameInputBlocksEditorEvent(key(code,false),9,true,true),"A key pressed before acquisition could remain stuck in ImGui.");
        require(!gameInputBlocksEditorEvent(key(code),9,false,false),"Released Game suppressed editor keyboard input.");
    }
    for(const auto code:{SDL_SCANCODE_P,SDL_SCANCODE_ESCAPE,SDL_SCANCODE_F,SDL_SCANCODE_F1,SDL_SCANCODE_TAB})
        require(!gameInputBlocksEditorEvent(key(code),9,true,true),"FPS input suppressed Play, release or debug shortcuts.");
}
}
int main() {
    try {
        heldMovementAndWindowRouting();jumpUsesPhysicalPressEdges();traceUsesOwnedPhysicalPressEdges();relativeLookAccumulatesOnce();
        ownershipCancellationClearsActions();imageClickOnlyAcquiresAndHonorsCancellation();missingNativeOwnerCannotCapture();
        editorEventGateSeparatesFpsAndUi();
        std::cout<<"FPS Game input checks passed.\n";return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
