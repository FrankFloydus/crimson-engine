#include "runtime/GameInput.h"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
constexpr ViewportRegion Region{10,20,300,200,true,false,9};
InputEvent key(PhysicalKeyCode code,bool down=true,InputSurfaceId window=9,bool repeat=false) {
    InputEvent event{};event.type=down?InputEventType::KeyDown:InputEventType::KeyUp;
    event.surface=window;event.physical=code;event.repeat=repeat;return event;
}
InputEvent mouse(float x,float y,InputEventType type=InputEventType::MouseButtonDown,InputSurfaceId window=9,MouseButton button=MouseButton::Left) {
    InputEvent event{};event.type=type;event.surface=window;event.button=button;event.position[0]=x;event.position[1]=y;return event;
}
InputEvent motion(float dx,float dy,InputSurfaceId window=9) {
    InputEvent event{};event.type=InputEventType::MouseMotion;event.surface=window;
    event.position[0]=-1000;event.position[1]=5000;event.relative[0]=dx;event.relative[1]=dy;return event;
}
void idle(const GameInput& input,const char* message) {
    require(!input.ownsKeyboard() && input.movement()==Vec2{},message);
}
void heldMovementAndWindowRouting() {
    GameInput input;input.acquire();idle(input,"An unavailable Game acquired input.");
    input.setViewport(Region,true,false);
    require(input.viewport().x==10 && input.viewport().windowId==9,"Game lost its native image owner.");
    require(!input.handleEvent(key(PhysicalKeyCode::W)) && !input.handleEvent(motion(10,20)),"Unowned actions reached Game.");
    input.acquire();require(input.ownsKeyboard() && input.movement()==Vec2{},"Play acquisition fabricated movement.");
    require(input.handleEvent(key(PhysicalKeyCode::W)) && input.handleEvent(key(PhysicalKeyCode::D)),"Game did not consume its movement keys.");
    require(input.movement()==Vec2{1,1} && input.movement()==Vec2{1,1},"Continuous movement was consumed or used the wrong axes.");
    require(!input.handleEvent(key(PhysicalKeyCode::S,true,10)) && input.movement()==Vec2{1,1},"A different native window supplied movement.");
    input.handleEvent(key(PhysicalKeyCode::A));input.handleEvent(key(PhysicalKeyCode::S));
    require(input.movement()==Vec2{},"Opposite movement keys did not cancel independently.");
    input.handleEvent(key(PhysicalKeyCode::W,false));input.handleEvent(key(PhysicalKeyCode::D,false));
    require(input.movement()==Vec2{-1,-1},"Releasing one direction lost its opposite held key.");
    auto covered=Region;covered.hovered=false;input.setViewport(covered,true,false);
    require(input.movement()==Vec2{-1,-1},"Captured movement required hovering the image.");
    require(!input.handleEvent(key(PhysicalKeyCode::F1)),"FPS movement stole an unrelated debug key.");
    input.cancel();input.acquire();input.handleEvent(key(PhysicalKeyCode::W,true,9,true));
    require(input.movement()==Vec2{},"A key repeat revived movement canceled during lost ownership.");
    input.handleEvent(key(PhysicalKeyCode::W,false));input.handleEvent(key(PhysicalKeyCode::W));
    require(input.movement()==Vec2{0,1},"A fresh key press did not resume movement after cancellation.");
}
void jumpUsesPhysicalPressEdges() {
    GameInput input;input.setViewport(Region,true,false);input.acquire();
    input.handleEvent(key(PhysicalKeyCode::Space));input.handleEvent(key(PhysicalKeyCode::Space,false));
    require(input.consumeJump() && !input.consumeJump(),"A short jump press was lost or replayed across simulation ticks.");
    input.handleEvent(key(PhysicalKeyCode::Space));require(input.consumeJump(),"A fresh Space press did not request jumping.");
    input.handleEvent(key(PhysicalKeyCode::Space,true,9,true));input.handleEvent(key(PhysicalKeyCode::Space));
    require(!input.consumeJump(),"Holding Space queued repeated jumps.");
    input.handleEvent(key(PhysicalKeyCode::Space,false));input.handleEvent(key(PhysicalKeyCode::Space));
    input.cancel();input.acquire();
    require(!input.consumeJump(),"Cancel/reacquire retained a queued jump.");
    input.handleEvent(key(PhysicalKeyCode::Space,true,9,true));require(!input.consumeJump(),"A Space repeat rearmed jumping after cancellation.");
    input.handleEvent(key(PhysicalKeyCode::Space,false));input.handleEvent(key(PhysicalKeyCode::Space));
    require(input.consumeJump(),"A new physical jump remained suppressed after reacquisition.");
}
void traceUsesOwnedPhysicalPressEdges() {
    GameInput input;input.setViewport(Region,true,false);
    require(input.handleEvent(mouse(50,60))&&input.ownsKeyboard()&&!input.consumeTrace(),"The Game acquisition click fired a trace.");
    input.acquireFromImageClick();input.handleEvent(mouse(50,60));
    require(!input.consumeTrace(),"Holding or acknowledging the acquisition click fired a trace.");
    input.handleEvent(mouse(50,60,InputEventType::MouseButtonUp));
    require(input.handleEvent(mouse(50,60)),"Game did not consume an owned LMB press.");
    input.handleEvent(mouse(50,60,InputEventType::MouseButtonUp));
    require(input.consumeTrace()&&!input.consumeTrace(),"A short owned LMB press was lost or consumed twice.");
    input.handleEvent(mouse(50,60));require(input.consumeTrace(),"A new LMB press did not queue a trace.");
    input.handleEvent(mouse(50,60));input.handleEvent(mouse(50,60));
    require(!input.consumeTrace(),"Duplicate held LMB-down events generated repeated traces.");
    input.handleEvent(mouse(50,60,InputEventType::MouseButtonUp,10));input.handleEvent(mouse(50,60));
    require(!input.consumeTrace(),"A foreign native window released the held trace button.");
    input.handleEvent(mouse(50,60,InputEventType::MouseButtonUp));input.handleEvent(mouse(50,60,InputEventType::MouseButtonUp));
    require(!input.consumeTrace(),"LMB release events fired a trace.");
    for(const auto button:{MouseButton::Right,MouseButton::Middle}) {
        input.handleEvent(mouse(50,60,InputEventType::MouseButtonDown,9,button));
        input.handleEvent(mouse(50,60,InputEventType::MouseButtonUp,9,button));
        require(!input.consumeTrace(),"Another mouse button queued the LMB trace action.");
    }
    require(!input.handleEvent(key(PhysicalKeyCode::F))&&!input.handleEvent(key(PhysicalKeyCode::F,false))&&!input.consumeTrace(),
        "The removed F binding consumed editor input or queued a trace.");
    input.handleEvent(mouse(50,60));input.cancel();input.acquire();
    require(!input.consumeTrace(),"Canceled input retained a queued LMB trace.");
    input.handleEvent(mouse(50,60));require(input.consumeTrace(),"A fresh owned LMB trace remained suppressed after cancellation.");
    require(!input.handleEvent(mouse(50,60,InputEventType::MouseButtonDown,10))&&!input.consumeTrace(),"A foreign native window supplied a trace.");
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
    for(const auto type:{InputEventType::SurfaceFocusLost,InputEventType::SurfaceHidden,InputEventType::SurfaceMinimized,
                         InputEventType::SurfaceCloseRequested,InputEventType::SurfaceDestroyed}) {
        GameInput input;input.setViewport(Region,true,false);input.acquire();
        input.handleEvent(key(PhysicalKeyCode::W));input.handleEvent(key(PhysicalKeyCode::Space));input.handleEvent(mouse(50,60));input.handleEvent(motion(3,4));
        const auto revision=input.cancellationRevision();
        InputEvent event{};event.type=type;event.surface=10;input.handleEvent(event);
        require(input.ownsKeyboard() && input.cancellationRevision()==revision,"An unrelated native window canceled Game.");
        event.surface=9;input.handleEvent(event);idle(input,"Window interruption retained owned movement.");
        require(input.cancellationRevision()>revision && !input.consumeJump() && !input.consumeTrace() && input.consumeLookDelta()==Vec2{},"Window interruption retained pending actions.");
    }
    for(int reason=0;reason<6;++reason) {
        GameInput input;input.setViewport(Region,true,false);input.acquire();
        input.handleEvent(key(PhysicalKeyCode::D));input.handleEvent(key(PhysicalKeyCode::Space));input.handleEvent(mouse(50,60));input.handleEvent(motion(3,4));
        auto region=Region;
        if(reason==1)region.inputBlocked=true;
        if(reason==2)region.windowId=10;
        if(reason==4)region.width=0;
        if(reason==5)region.height=std::numeric_limits<float>::quiet_NaN();
        const auto revision=input.cancellationRevision();input.setViewport(region,reason!=3,reason==0);
        idle(input,"Blocked, disabled, migrated or invalid Game retained movement.");
        require(input.cancellationRevision()>revision && !input.consumeJump() && !input.consumeTrace() && input.consumeLookDelta()==Vec2{},"Viewport interruption retained pending actions.");
    }
    GameInput input;input.setViewport(Region,true,false);input.acquire();input.handleEvent(key(PhysicalKeyCode::W));
    require(input.handleEvent(key(PhysicalKeyCode::Escape)),"Escape was not consumed by the gameplay owner.");
    idle(input,"Escape retained gameplay ownership.");
}
void imageClickOnlyAcquiresAndHonorsCancellation() {
    GameInput input;input.setViewport(Region,true,true);
    require(!input.handleEvent(mouse(50,60)),"Blocked native press unexpectedly acquired Game.");
    input.setViewport(Region,true,false);input.acquireFromImageClick();
    require(input.ownsKeyboard() && input.movement()==Vec2{} && !input.consumeJump() && !input.consumeTrace() && input.consumeLookDelta()==Vec2{},
        "The first image click leaving text editing failed to acquire or fabricated an FPS action.");
    input.handleEvent(mouse(50,60,InputEventType::MouseButtonUp));
    require(input.ownsKeyboard(),"Releasing the acquisition click released keyboard ownership.");
    input.handleEvent(mouse(60,70));input.handleEvent(key(PhysicalKeyCode::Escape));
    input.acquireFromImageClick();idle(input,"Delayed UI acknowledgement revived an Escape-canceled native press.");
    input.handleEvent(mouse(60,70));input.acquireFromImageClick();
    require(input.ownsKeyboard(),"A fresh image press could not reacquire Game.");
    input.handleEvent(key(PhysicalKeyCode::W));input.handleEvent(mouse(900,700));
    idle(input,"Clicking another panel retained gameplay movement.");
    input.handleEvent(mouse(60,70,InputEventType::MouseButtonDown,10));
    idle(input,"A different native window acquired Game.");
    input.handleEvent(mouse(60,70,InputEventType::MouseButtonDown,9,MouseButton::Right));
    idle(input,"Right click acquired gameplay input.");
}
void missingNativeOwnerCannotCapture() {
    GameInput input;auto region=Region;region.windowId=0;input.setViewport(region,true,false);input.acquire();
    input.handleEvent(key(PhysicalKeyCode::W,true,0));input.syncMouseCapture();
    idle(input,"Native capture retained input without an existing focused native window.");
}
void toolkitCaptureOwnsInput(){
    GameInput input;bool focused=true;InputSurfaceId captured{};unsigned releases{};
    input.setMouseCapture([&](InputSurfaceId id,bool enable){if(enable){if(!focused)return false;captured=id;return true;}if(captured==id){captured=0;++releases;}return true;});
    input.setViewport(Region,true,false);input.acquire();input.syncMouseCapture();
    require(captured==Region.windowId&&input.ownsKeyboard(),"A Qt-owned logical viewport could not capture Game without a foreign native window.");
    input.handleEvent(key(PhysicalKeyCode::W));input.handleEvent(key(PhysicalKeyCode::Space));
    require(input.movement()==Vec2{0,1}&&input.consumeJump(),"Qt capture lost physical WASD/jump edges.");
    input.handleEvent(motion(50,30));input.handleEvent(motion(4,-2));require(input.consumeLookDelta()==Vec2{4,-2},"Qt capture duplicated acquisition motion.");
    focused=false;input.syncMouseCapture();idle(input,"Qt focus loss retained Game actions.");
    require(captured==0&&releases==1,"Qt focus loss retained native capture.");
}
void editorEventGateSeparatesFpsAndUi() {
    require(!gameInputBlocksEditorEvent(mouse(50,60),9,true,false),"The first acquisition click was hidden from the Game image.");
    for(const auto event:{mouse(50,60),mouse(-1000,5000),motion(5,6)}) {
        require(gameInputBlocksEditorEvent(event,9,true,true),"Captured mouse input could activate an editor widget.");
        require(!gameInputBlocksEditorEvent(event,10,true,true),"Game suppressed another native window's UI input.");
        require(!gameInputBlocksEditorEvent(event,9,false,true),"Released Game suppressed editor mouse input.");
    }
    InputEvent wheel{};wheel.type=InputEventType::MouseWheel;wheel.surface=9;
    require(gameInputBlocksEditorEvent(wheel,9,true,true),"Captured gameplay wheel input reached editor controls.");
    require(!gameInputBlocksEditorEvent(mouse(50,60,InputEventType::MouseButtonUp),9,true,true),"Acquisition mouse release could leave editor widgets's image held.");
    for(const auto code:{PhysicalKeyCode::W,PhysicalKeyCode::A,PhysicalKeyCode::S,PhysicalKeyCode::D,PhysicalKeyCode::Space}) {
        require(gameInputBlocksEditorEvent(key(code),9,true,true),"FPS press edges could activate an editor shortcut or widget.");
        require(!gameInputBlocksEditorEvent(key(code,false),9,true,true),"A key pressed before acquisition could remain stuck in editor widgets.");
        require(!gameInputBlocksEditorEvent(key(code),9,false,false),"Released Game suppressed editor keyboard input.");
    }
    for(const auto code:{PhysicalKeyCode::P,PhysicalKeyCode::Escape,PhysicalKeyCode::F,PhysicalKeyCode::F1,PhysicalKeyCode::Tab})
        require(!gameInputBlocksEditorEvent(key(code),9,true,true),"FPS input suppressed Play, release or debug shortcuts.");
}
}
int main() {
    try {
        heldMovementAndWindowRouting();jumpUsesPhysicalPressEdges();traceUsesOwnedPhysicalPressEdges();relativeLookAccumulatesOnce();
        ownershipCancellationClearsActions();imageClickOnlyAcquiresAndHonorsCancellation();missingNativeOwnerCannotCapture();
        editorEventGateSeparatesFpsAndUi();toolkitCaptureOwnsInput();
        std::cout<<"FPS Game input checks passed.\n";return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
