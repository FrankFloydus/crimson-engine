#include "input/Navigation.h"
#include "input/NavigationUiInput.h"
#include <imgui_internal.h>
#include <SDL3/SDL_mouse.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
SDL_Event button(Uint32 type,Uint8 button,ImVec2 point,SDL_WindowID owner=0){
    SDL_Event event{};event.type=type;event.button.windowID=owner;event.button.button=button;event.button.x=point.x;event.button.y=point.y;return event;
}
SDL_Event motion(ImVec2 point,float dx=0,float dy=0,SDL_WindowID owner=0){
    SDL_Event event{};event.type=SDL_EVENT_MOUSE_MOTION;event.motion.windowID=owner;event.motion.x=point.x;event.motion.y=point.y;event.motion.xrel=dx;event.motion.yrel=dy;return event;
}
SDL_Event key(SDL_Scancode code,bool down=true,SDL_WindowID owner=0){
    SDL_Event event{};event.type=down?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;event.key.windowID=owner;event.key.scancode=code;return event;
}
SDL_Event wheel(float steps,SDL_WindowID owner=0){
    SDL_Event event{};event.type=SDL_EVENT_MOUSE_WHEEL;event.wheel.windowID=owner;event.wheel.y=steps;return event;
}
SDL_Event windowEvent(Uint32 type,SDL_WindowID owner=0){SDL_Event event{};event.type=type;event.window.windowID=owner;return event;}
ImGuiKey imguiKey(SDL_Scancode code){
    switch(code){case SDL_SCANCODE_W:return ImGuiKey_W;case SDL_SCANCODE_A:return ImGuiKey_A;
    case SDL_SCANCODE_S:return ImGuiKey_S;case SDL_SCANCODE_D:return ImGuiKey_D;
    case SDL_SCANCODE_ESCAPE:return ImGuiKey_Escape;case SDL_SCANCODE_F5:return ImGuiKey_F5;default:return ImGuiKey_None;}
}
// The same production routing and mouse scope, with actual ImGui controls.
// This fixture creates no SDL/native window, renderer, GPU or desktop input.
class NavigationUi {
public:
    explicit NavigationUi(bool fixed):fixed_(fixed){
        context_=ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;
        io.DisplaySize={1000,700};io.DeltaTime=1.f/60;io.ConfigInputTrickleEventQueue=false;
        unsigned char* pixels{};int width{},height{};io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);io.Fonts->SetTexID(static_cast<ImTextureID>(1));
        camera.position={0,1,5};camera.target={0,1,0};tick();tick();
    }
    ~NavigationUi(){navigation.cancel();ImGui::DestroyContext(context_);}
    void event(const SDL_Event& event){
        if(!fixed_||!navigation.blocksEditorEvent(event)){
            auto& io=ImGui::GetIO();
            switch(event.type){
            case SDL_EVENT_MOUSE_MOTION:io.AddMousePosEvent(event.motion.x,event.motion.y);break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:case SDL_EVENT_MOUSE_BUTTON_UP:{
                const int index=event.button.button==SDL_BUTTON_LEFT?0:event.button.button==SDL_BUTTON_RIGHT?1:2;
                io.AddMouseButtonEvent(index,event.type==SDL_EVENT_MOUSE_BUTTON_DOWN);break;}
            case SDL_EVENT_MOUSE_WHEEL:io.AddMouseWheelEvent(event.wheel.x,event.wheel.y);break;
            case SDL_EVENT_KEY_DOWN:case SDL_EVENT_KEY_UP:
                if(const auto uiKey=imguiKey(event.key.scancode);uiKey!=ImGuiKey_None)io.AddKeyEvent(uiKey,event.type==SDL_EVENT_KEY_DOWN);break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:io.AddFocusEvent(false);break;
            case SDL_EVENT_WINDOW_FOCUS_GAINED:io.AddFocusEvent(true);break;
            default:break;
            }
        }
        navigation.handleEvent(event);
    }
    void tick(){
        NavigationUiMouseScope mouseScope(fixed_&&navigation.flying());
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize({450,600});
        ImGui::Begin("Scene viewport",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings);
        const auto origin=ImGui::GetCursorScreenPos();const auto size=ImGui::GetContentRegionAvail();
        ImGui::Image(ImTextureRef(static_cast<ImTextureID>(1)),size);
        sceneHovered=ImGui::IsItemHovered();scenePoint={origin.x+size.x*.5f,origin.y+size.y*.5f};
        ViewportRegion region{origin.x,origin.y,size.x,size.y,sceneHovered,
            ImGui::GetIO().WantTextInput||ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopup),ownerOverride};
        ImGui::End();
        ImGui::SetNextWindowPos({520,20});ImGui::SetNextWindowSize({300,600});
        ImGui::Begin("Outliner controls",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings);
        if(ImGui::Button("Outliner action",{220,50}))++actions;
        const auto minimum=ImGui::GetItemRectMin(),maximum=ImGui::GetItemRectMax();
        controlPoint={(minimum.x+maximum.x)*.5f,(minimum.y+maximum.y)*.5f};controlHovered=ImGui::IsItemHovered();
        if(ImGui::IsKeyPressed(ImGuiKey_F5,false))++debugKeys;
        ImGui::End();
        rightDown=ImGui::IsMouseDown(ImGuiMouseButton_Right);wDown=ImGui::IsKeyDown(ImGuiKey_W);uiWheel=ImGui::GetIO().MouseWheel;
        navigation.update(camera,region,1.f/60,false);
        ImGui::Render();require(ImGui::GetCurrentContext()->ErrorCountCurrentFrame==0,"Scene navigation left invalid private ImGui state.");
    }
    void fly(){
        event(motion(scenePoint));tick();event(button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_RIGHT,scenePoint));tick();
        require(navigation.flying()&&rightDown,"Initial RMB acquisition was hidden from the Scene image.");
    }
    void restorePointer(){event(motion(controlPoint));tick();require(controlHovered,"Released navigation retained a hidden UI pointer.");}
    Navigation navigation{nullptr};Camera camera;ImVec2 scenePoint{},controlPoint{};
    SDL_WindowID ownerOverride{};
    bool sceneHovered{},controlHovered{},rightDown{},wDown{};int actions{},debugKeys{};float uiWheel{};
private:
    ImGuiContext* context_{};bool fixed_{};
};
void referenceReproducesLeak(){
    NavigationUi ui(false);ui.fly();ui.event(motion(ui.controlPoint,8,-3));ui.tick();
    require(ui.controlHovered,"The independent old-input control did not reproduce hidden-cursor hover on an unrelated panel.");
    ui.event(button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT,ui.controlPoint));ui.tick();
    ui.event(button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_LEFT,ui.controlPoint));ui.tick();
    require(ui.actions==1,"The independent old-input control did not reproduce widget activation during captured navigation.");
}
void flightOwnsPointerAndActions(){
    NavigationUi ui(true);ui.fly();const auto beforeLook=ui.camera.forward();
    ui.event(motion(ui.controlPoint,8,-3));ui.tick();
    require(!ui.controlHovered&&ui.navigation.flying(),"Relative Scene motion hovered UI or cancelled its navigation owner.");
    require(ui.camera.forward()!=beforeLook,"Filtering UI motion discarded the camera's raw relative deltas.");
    // Even a stale backend position cannot hover while the cursor is captured.
    ImGui::GetIO().AddMousePosEvent(ui.controlPoint.x,ui.controlPoint.y);ui.tick();
    require(!ui.controlHovered,"An idle captured frame retained stale Outliner hover.");
    ui.event(button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT,ui.controlPoint));ui.tick();
    ui.event(button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_LEFT,ui.controlPoint));ui.tick();
    require(ui.actions==0,"A captured Scene gesture activated another panel's widget.");
    const auto beforeMove=ui.camera.eye();ui.event(key(SDL_SCANCODE_W));ui.event(wheel(1));ui.tick();
    require(ui.camera.eye()!=beforeMove&&!ui.wDown,"Captured WASD failed camera movement or leaked into ImGui.");
    require(ui.uiWheel==0&&std::abs(ui.navigation.flySpeed()-31.25f)<.001f,"Captured fly wheel reached UI or lost its speed adjustment.");
    ui.event(key(SDL_SCANCODE_F5));ui.tick();require(ui.debugKeys==1,"Pointer capture disabled unrelated keyboard commands.");
    ui.event(key(SDL_SCANCODE_F5,false));ui.event(key(SDL_SCANCODE_W,false));
    ui.event(button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_RIGHT,ui.scenePoint));ui.tick();
    require(!ui.navigation.active()&&!ui.rightDown&&!ui.wDown,"RMB/key release remained held after Scene navigation.");
    require(!ui.controlHovered,"The capture-release frame leaked a stale hovered panel.");
    ui.restorePointer();require((ImGui::GetIO().ConfigFlags&ImGuiConfigFlags_NoMouse)==0,"Scene mouse suppression escaped its frame scope.");
}
void cancellationAndOwnership(){
    for(const auto type:{SDL_EVENT_WINDOW_FOCUS_LOST,SDL_EVENT_WINDOW_HIDDEN,SDL_EVENT_WINDOW_MINIMIZED,SDL_EVENT_WINDOW_CLOSE_REQUESTED,SDL_EVENT_WINDOW_DESTROYED}){
        NavigationUi ui(true);ui.fly();ui.event(key(SDL_SCANCODE_W));ui.tick();
        ui.event(windowEvent(type,17));require(ui.navigation.flying(),"A foreign native window cancelled Scene capture.");
        ui.event(windowEvent(type));require(!ui.navigation.active(),"An owner interruption waited for a drawable UI frame to release capture.");
        ui.event(windowEvent(SDL_EVENT_WINDOW_FOCUS_GAINED));ui.tick();const auto stopped=ui.camera.eye();ui.tick();
        require(ui.camera.eye()==stopped,"Interrupted navigation retained held movement.");ui.restorePointer();
    }
    {
        NavigationUi ui(true);ui.fly();ui.event(key(SDL_SCANCODE_ESCAPE));require(!ui.navigation.active(),"Escape did not release Scene capture immediately.");
        ui.tick();ui.event(key(SDL_SCANCODE_ESCAPE,false));ui.restorePointer();
    }
    {
        NavigationUi ui(true);ui.fly();ui.ownerOverride=17;ui.tick();require(!ui.navigation.active(),"A Scene native-window replacement retained the old capture.");
        ui.ownerOverride=0;ui.restorePointer();
    }
    {
        NavigationUi ui(true);ui.event(motion(ui.scenePoint));ui.tick();ui.event(button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_MIDDLE,ui.scenePoint));ui.tick();
        require(ui.navigation.active()&&!ui.navigation.flying(),"The ordinary MMB navigation control did not start.");
        ui.event(motion(ui.controlPoint));ui.tick();require(ui.controlHovered,"Scene fly ownership changed ordinary MMB pointer behavior.");
        ui.event(button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_MIDDLE,ui.controlPoint));ui.tick();
    }
}
void pureNativeRouting(){
    const ImVec2 point{30,40};
    for(const auto& event:{motion(point,2,3,9),button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT,point,9),wheel(1,9),key(SDL_SCANCODE_W,true,9),key(SDL_SCANCODE_A,true,9),key(SDL_SCANCODE_S,true,9),key(SDL_SCANCODE_D,true,9)}){
        require(navigationBlocksEditorEvent(event,9,true),"Scene flight did not own its native pointer/WASD event.");
        require(!navigationBlocksEditorEvent(event,10,true),"Scene flight suppressed another native window's UI input.");
        require(!navigationBlocksEditorEvent(event,9,false),"Released Scene navigation suppressed ordinary UI input.");
    }
    for(const auto& event:{button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_RIGHT,point,9),button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_LEFT,point,9),key(SDL_SCANCODE_W,false,9),key(SDL_SCANCODE_ESCAPE,true,9),key(SDL_SCANCODE_F5,true,9),windowEvent(SDL_EVENT_WINDOW_FOCUS_LOST,9)})
        require(!navigationBlocksEditorEvent(event,9,true),"Scene capture hid release, cancellation or unrelated keyboard input from ImGui.");
    NavigationUi ui(true);ImGui::GetIO().ConfigFlags|=ImGuiConfigFlags_NoMouse;
    {NavigationUiMouseScope scope(true);}require((ImGui::GetIO().ConfigFlags&ImGuiConfigFlags_NoMouse)!=0,"Navigation stole an existing UI mouse owner.");
    ImGui::GetIO().ConfigFlags&=~ImGuiConfigFlags_NoMouse;
}
}
int main(){try{referenceReproducesLeak();flightOwnsPointerAndActions();cancellationAndOwnership();pureNativeRouting();
    std::cout<<"Scene relative navigation isolates actual UI hover/actions, preserves camera/keyboard/release and restores pointer ownership.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
