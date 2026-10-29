#include "input/Navigation.h"
#include "input/KeyboardActions.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
constexpr InputSurfaceId Owner=19;
constexpr ViewportRegion Region{10,20,600,400,true,false,Owner};
InputEvent button(MouseButton value,bool down=true,Vec2 position={100,120},InputSurfaceId owner=Owner){InputEvent event;event.type=down?InputEventType::MouseButtonDown:InputEventType::MouseButtonUp;event.surface=owner;event.button=value;event.position=position;return event;}
InputEvent motion(Vec2 delta,InputSurfaceId owner=Owner){InputEvent event;event.type=InputEventType::MouseMotion;event.surface=owner;event.position={-10000,10000};event.relative=delta;return event;}
InputEvent key(PhysicalKeyCode code,bool down=true,InputSurfaceId owner=Owner,bool repeat=false){InputEvent event;event.type=down?InputEventType::KeyDown:InputEventType::KeyUp;event.surface=owner;event.physical=code;event.logical=U'z';event.repeat=repeat;return event;}
InputEvent wheel(float value,bool flipped=false){InputEvent event;event.type=InputEventType::MouseWheel;event.surface=Owner;event.wheelY=value;event.wheelFlipped=flipped;return event;}
struct Fixture {
    Navigation navigation{Owner};Camera camera;InputSurfaceId captured{};unsigned releases{};bool focused{true};
    Fixture(){camera.position={0,1,5};camera.target={0,1,0};navigation.setMouseCapture([this](InputSurfaceId owner,bool enable){if(enable){if(!focused)return false;captured=owner;return true;}if(captured==owner){captured=0;++releases;}return true;});}
    ~Fixture(){navigation.cancel();}
    void tick(float seconds=1.f/60,const ViewportRegion& region=Region){navigation.update(camera,region,seconds,false);}
    void fly(){navigation.handleEvent(button(MouseButton::Right));tick();require(navigation.flying()&&captured==Owner,"The Qt-owned logical surface did not acquire relative editor flight.");}
};
void stationaryRightClickAndFirstDrag() {
    Fixture fixture;const auto eye=fixture.camera.eye(),forward=fixture.camera.forward();
    fixture.navigation.handleEvent(button(MouseButton::Right));fixture.navigation.handleEvent(motion({50,-30}));fixture.tick();
    require(fixture.navigation.flying()&&fixture.camera.eye()==eye&&fixture.camera.forward()==forward,
        "Capture-transition motion changed the camera on the acquisition frame.");
    fixture.navigation.handleEvent(motion({4,-2}));fixture.tick();
    require(fixture.camera.forward()!=forward,"The first real post-capture drag did not update editor look.");
    fixture.navigation.handleEvent(button(MouseButton::Right,false,{-9000,8000}));fixture.tick();
    require(!fixture.navigation.active()&&!fixture.captured&&!fixture.navigation.consumeContextClick(),
        "A dragged RMB gesture retained capture or opened the stationary context menu.");
    fixture.navigation.handleEvent(button(MouseButton::Right,true,{130,140}));fixture.tick();
    fixture.navigation.handleEvent(motion({0,0}));fixture.tick();
    fixture.navigation.handleEvent(button(MouseButton::Right,false,{-9000,8000}));fixture.tick();
    require(fixture.navigation.consumeContextClick()==Vec2{130,140}&&!fixture.navigation.consumeContextClick(),
        "A stationary RMB release lost its original image point or replayed its context edge.");
}
void physicalMovementAndWheel() {
    Fixture fixture;fixture.fly();auto before=fixture.camera.eye();
    fixture.navigation.handleEvent(key(PhysicalKeyCode::W));fixture.tick(.1f);
    require(fixture.camera.eye()!=before,"Physical W failed flight when its layout-aware logical key was Z.");
    fixture.navigation.handleEvent(key(PhysicalKeyCode::S));before=fixture.camera.eye();fixture.tick(.1f);
    require(fixture.camera.eye()==before,"Opposite held flight keys did not cancel.");
    fixture.navigation.handleEvent(key(PhysicalKeyCode::W,false));fixture.navigation.handleEvent(key(PhysicalKeyCode::S,false));
    fixture.navigation.handleEvent(wheel(.5f));fixture.tick();const auto half=fixture.navigation.flySpeed();
    require(std::abs(half-25*std::sqrt(1.25))<.001,"Fractional wheel input lost proportional flight speed.");
    fixture.navigation.handleEvent(wheel(.5f,true));fixture.tick();require(std::abs(fixture.navigation.flySpeed()-25)<.001,"Flipped fractional wheel input was not inverted once.");
    fixture.navigation.handleEvent(wheel(std::numeric_limits<float>::max()));fixture.tick();require(fixture.navigation.flySpeed()==1000,"Extreme finite speed input exceeded its upper bound.");
    fixture.navigation.handleEvent(button(MouseButton::Middle));fixture.tick();require(fixture.navigation.flySpeed()==25,"MMB did not reset flight speed without ending RMB ownership.");
    fixture.navigation.handleEvent(wheel(-std::numeric_limits<float>::max()));fixture.tick();require(fixture.navigation.flySpeed()==1,"Extreme finite speed input exceeded its lower bound.");
    fixture.navigation.handleEvent(wheel(std::numeric_limits<float>::quiet_NaN()));fixture.tick();require(fixture.navigation.flySpeed()==1,"Nonfinite wheel input changed the accepted flight speed.");
}
void cancellationAndSurfaceRouting() {
    for(const auto type:{InputEventType::SurfaceFocusLost,InputEventType::SurfaceHidden,InputEventType::SurfaceMinimized,InputEventType::SurfaceCloseRequested,InputEventType::SurfaceDestroyed}) {
        Fixture fixture;fixture.fly();fixture.navigation.handleEvent(key(PhysicalKeyCode::W));fixture.tick();
        InputEvent event;event.type=type;event.surface=Owner+1;fixture.navigation.handleEvent(event);
        require(fixture.navigation.flying()&&fixture.captured==Owner,"A foreign surface interrupted the editor capture owner.");
        event.surface=Owner;fixture.navigation.handleEvent(event);
        require(!fixture.navigation.active()&&!fixture.captured&&fixture.releases==1,
            "Surface interruption waited for a render/update frame to release native capture.");
        const auto eye=fixture.camera.eye();fixture.navigation.handleEvent(key(PhysicalKeyCode::W,true,Owner,true));fixture.fly();fixture.tick(.1f);
        require(fixture.camera.eye()==eye,"A key repeat revived movement after focus cancellation.");
    }
    Fixture fixture;fixture.fly();auto eye=fixture.camera.eye();
    fixture.navigation.handleEvent(key(PhysicalKeyCode::W,true,Owner+1));fixture.navigation.handleEvent(motion({20,10},Owner+1));fixture.tick(.1f);
    require(fixture.camera.eye()==eye,"A foreign surface supplied editor movement.");
    fixture.focused=false;fixture.tick();require(!fixture.navigation.active()&&!fixture.captured,"A failed Qt capture postcondition retained flight.");
    fixture.focused=true;fixture.fly();auto blocked=Region;blocked.inputBlocked=true;fixture.tick(.1f,blocked);
    require(!fixture.navigation.active()&&!fixture.captured,"A modal/covered viewport retained navigation input ownership.");
}
void routingAndLayoutAwareBindings() {
    require(!navigationBlocksEditorEvent(button(MouseButton::Right),Owner,false),"The acquisition edge was hidden from the Scene image.");
    for(const auto event:{key(PhysicalKeyCode::W),motion({3,4}),wheel(1),button(MouseButton::Left)}) {
        require(navigationBlocksEditorEvent(event,Owner,true),"Captured editor input could activate another widget.");
        require(!navigationBlocksEditorEvent(event,Owner+1,true),"Editor flight swallowed a foreign surface's input.");
    }
    require(!navigationBlocksEditorEvent(key(PhysicalKeyCode::W,false),Owner,true)&&
        !navigationBlocksEditorEvent(button(MouseButton::Right,false),Owner,true)&&
        !navigationBlocksEditorEvent(key(PhysicalKeyCode::F5),Owner,true),"Flight swallowed release or unrelated debug edges.");
    InputEvent event=key(PhysicalKeyCode::W);event.logical=U'p';
    require(matchesKeyPress(event,{LogicalKey{U'p'}})&&matchesKeyPress(event,{PhysicalKey{PhysicalKeyCode::W}}),
        "Logical editor shortcuts and physical movement did not retain independent identities.");
    event.modifiers=InputModifier::CapsLock;require(matchesKeyPress(event,{LogicalKey{U'p'}}),"Lock modifiers changed a named shortcut.");
    event.modifiers=InputModifier::AltGraph;require(!matchesKeyPress(event,{LogicalKey{U'p'}}),"AltGr text composition triggered an unmodified shortcut.");
    event.modifiers=InputModifier::Shift|InputModifier::Control;require(matchesKeyPress(event,{LogicalKey{U'p'},InputModifier::Shift|InputModifier::Control}),"Exact shortcut modifiers did not match.");
    event.repeat=true;require(!matchesKeyPress(event,{PhysicalKey{PhysicalKeyCode::W},event.modifiers}),"A repeated key produced another shortcut edge.");
}
}
int main(){try{stationaryRightClickAndFirstDrag();physicalMovementAndWheel();cancellationAndSurfaceRouting();routingAndLayoutAwareBindings();std::cout<<"Toolkit-neutral editor flight capture, stationary context, first drag, physical WASD, wheel bounds, interruption ownership and logical shortcuts passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
