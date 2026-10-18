#include "tools/GameViewInteraction.h"
#include "runtime/GameInput.h"
#include <imgui_internal.h>
#include <iostream>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
// Input is queued only into a private CPU ImGui context and GameInput. There is
// no SDL initialization, OS window, desktop input or renderer in this test.
class GameUi {
public:
    GameUi(bool interactive,bool docked):interactive_(interactive),docked_(docked) {
        context_=ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;
        io.DisplaySize={1000,720};io.DeltaTime=1.f/60;io.ConfigInputTrickleEventQueue=false;
        if(docked_)io.ConfigFlags|=ImGuiConfigFlags_DockingEnable;
        unsigned char* pixels{};int width{},height{};io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
        io.Fonts->SetTexID(static_cast<ImTextureID>(1));
        tick();tick();
    }
    ~GameUi(){ImGui::DestroyContext(context_);}
    void tick(bool focusAndAcquire=false) {
        ImGui::NewFrame();
        if(textEditor_) {
            ImGui::SetNextWindowPos({700,20});ImGui::SetNextWindowSize({280,160});
            ImGui::Begin("Inspector text",nullptr,ImGuiWindowFlags_NoDocking);
            ImGui::InputText("Name",text_,sizeof(text_));
            const auto minimum=ImGui::GetItemRectMin(),maximum=ImGui::GetItemRectMax();
            textPoint_={minimum.x+20,(minimum.y+maximum.y)*.5f};
            ImGui::End();
        }
        if(docked_) {
            const auto id=ImGui::GetID("GameInputDock");
            if(!ImGui::DockBuilderGetNode(id)) {
                ImGui::DockBuilderAddNode(id,ImGuiDockNodeFlags_DockSpace);
                ImGui::DockBuilderSetNodePos(id,{20,20});ImGui::DockBuilderSetNodeSize(id,{620,600});
                ImGui::DockBuilderDockWindow("Game",id);ImGui::DockBuilderFinish(id);
            }
            ImGui::DockSpaceOverViewport(id,ImGui::GetMainViewport());
        } else { ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize({620,600}); }
        if(focusAndAcquire) { ImGui::SetNextWindowFocus();ImGui::SetNextWindowCollapsed(false); }
        if(!ImGui::Begin("Game")) { input.cancel();ImGui::End();ImGui::Render();return; }
        ImGui::SetNextItemWidth(160);
        if(ImGui::BeginCombo("Camera","Camera 1")) {
            ImGui::Selectable("Camera 1",true);ImGui::EndCombo();
        }
        const auto comboMin=ImGui::GetItemRectMin(),comboMax=ImGui::GetItemRectMax();
        cameraPoint={(comboMin.x+comboMax.x)*.5f,(comboMin.y+comboMax.y)*.5f};
        const auto origin=ImGui::GetCursorScreenPos(),size=ImGui::GetContentRegionAvail();
        GameViewInteraction interaction;
        if(interactive_)interaction=drawGameViewImage(ImTextureRef(static_cast<ImTextureID>(1)),size);
        else {
            // Reproduce the original production gate so the regression proves
            // that a passive Image loses ownership on an ordinary held click.
            ImGui::Image(ImTextureRef(static_cast<ImTextureID>(1)),size);
            interaction.hovered=ImGui::IsItemHovered();
            interaction.clicked=interaction.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
            if(interaction.clicked)ImGui::SetWindowFocus();
            interaction.blocked=ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive() ||
                ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopup) || ImGui::GetDragDropPayload()!=nullptr;
        }
        if(popup)ImGui::OpenPopup("Debug input gate");
        if(ImGui::BeginPopup("Debug input gate")){ImGui::TextUnformatted("Gate");ImGui::EndPopup();}
        debugCycle=gameAttributeDebugCycleRequested(debugEnabled);
        debugTabNavigation=ImGui::GetCurrentContext()->NavMoveSubmitted &&
            (ImGui::GetCurrentContext()->NavMoveFlags&ImGuiNavMoveFlags_IsTabbing)!=0;
        region={origin.x,origin.y,size.x,size.y,interaction.hovered,interaction.blocked,9};
        imagePoint={origin.x+size.x*.5f,origin.y+size.y*.5f};
        input.setViewport(region,true,interaction.blocked);
        if(!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))input.cancel();
        else if(interaction.clicked)input.acquireFromImageClick();
        else if(focusAndAcquire)input.acquire();
        ImGui::End();ImGui::Render();
    }
    void mouse(ImVec2 point,bool down) {
        auto& io=ImGui::GetIO();io.AddMousePosEvent(point.x,point.y);io.AddMouseButtonEvent(ImGuiMouseButton_Left,down);
        SDL_Event event{};event.type=down?SDL_EVENT_MOUSE_BUTTON_DOWN:SDL_EVENT_MOUSE_BUTTON_UP;
        event.button.windowID=9;event.button.button=SDL_BUTTON_LEFT;event.button.x=point.x;event.button.y=point.y;
        input.handleEvent(event);tick();
    }
    void key(SDL_Scancode key,bool down) {
        SDL_Event event{};event.type=down?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;
        event.key.windowID=9;event.key.scancode=key;
        ImGuiKey uiKey=ImGuiKey_None;
        switch(key) {
        case SDL_SCANCODE_W:uiKey=ImGuiKey_W;break;
        case SDL_SCANCODE_A:uiKey=ImGuiKey_A;break;
        case SDL_SCANCODE_S:uiKey=ImGuiKey_S;break;
        case SDL_SCANCODE_D:uiKey=ImGuiKey_D;break;
        case SDL_SCANCODE_SPACE:uiKey=ImGuiKey_Space;break;
        case SDL_SCANCODE_ESCAPE:uiKey=ImGuiKey_Escape;break;
        default:break;
        }
        if(uiKey!=ImGuiKey_None && !input.blocksEditorEvent(event))ImGui::GetIO().AddKeyEvent(uiKey,down);
        input.handleEvent(event);tick();
    }
    void uiKey(ImGuiKey key,bool down) {ImGui::GetIO().AddKeyEvent(key,down);tick();}
    void motion(ImVec2 point) {
        ImGui::GetIO().AddMousePosEvent(point.x,point.y);
        SDL_Event event{};event.type=SDL_EVENT_MOUSE_MOTION;event.motion.windowID=9;event.motion.x=point.x;event.motion.y=point.y;
        input.handleEvent(event);tick();
    }
    void relativeMotion(float dx,float dy) {
        SDL_Event event{};event.type=SDL_EVENT_MOUSE_MOTION;event.motion.windowID=9;
        event.motion.xrel=dx;event.motion.yrel=dy;input.handleEvent(event);tick();
    }
    void pressThenMove(ImVec2 press,ImVec2 moved) {
        auto& io=ImGui::GetIO();io.AddMousePosEvent(press.x,press.y);io.AddMouseButtonEvent(ImGuiMouseButton_Left,true);
        SDL_Event event{};event.type=SDL_EVENT_MOUSE_BUTTON_DOWN;event.button.windowID=9;event.button.button=SDL_BUTTON_LEFT;
        event.button.x=press.x;event.button.y=press.y;input.handleEvent(event);
        io.AddMousePosEvent(moved.x,moved.y);event={};event.type=SDL_EVENT_MOUSE_MOTION;event.motion.windowID=9;
        event.motion.x=moved.x;event.motion.y=moved.y;event.motion.xrel=moved.x-press.x;event.motion.yrel=moved.y-press.y;
        input.handleEvent(event);tick();
    }
    void pressThenCancel(ImVec2 point) {
        auto& io=ImGui::GetIO();io.AddMousePosEvent(point.x,point.y);io.AddMouseButtonEvent(ImGuiMouseButton_Left,true);
        SDL_Event event{};event.type=SDL_EVENT_MOUSE_BUTTON_DOWN;event.button.windowID=9;event.button.button=SDL_BUTTON_LEFT;
        event.button.x=point.x;event.button.y=point.y;input.handleEvent(event);
        event={};event.type=SDL_EVENT_KEY_DOWN;event.key.windowID=9;event.key.scancode=SDL_SCANCODE_ESCAPE;
        input.handleEvent(event);tick();
    }
    void queuedShortClick(ImVec2 point) {
        motion(point);
        auto& io=ImGui::GetIO();io.ConfigInputTrickleEventQueue=true;
        io.AddMouseButtonEvent(ImGuiMouseButton_Left,true);io.AddMouseButtonEvent(ImGuiMouseButton_Left,false);
        SDL_Event event{};event.type=SDL_EVENT_MOUSE_BUTTON_DOWN;event.button.windowID=9;event.button.button=SDL_BUTTON_LEFT;
        event.button.x=point.x;event.button.y=point.y;input.handleEvent(event);
        event.type=SDL_EVENT_MOUSE_BUTTON_UP;input.handleEvent(event);tick();
    }
    void focusText() {
        textEditor_=true;tick();tick();
        mouse(textPoint_,true);tick();mouse(textPoint_,false);tick();
        require(ImGui::GetIO().WantTextInput,"Text fixture did not acquire text input.");
    }
    GameInput input;
    bool debugEnabled{},debugTabNavigation{},popup{};
    int debugCycle{};
    ImVec2 imagePoint{},cameraPoint{};
    ViewportRegion region;
private:
    ImGuiContext* context_{};
    bool interactive_{},docked_{};
    bool textEditor_{};
    ImVec2 textPoint_{};
    char text_[64]{"Character"};
};
void passiveImageLosesControl(bool docked) {
    GameUi ui(false,docked);
    ui.mouse(ui.imagePoint,true);
    require(ui.input.ownsKeyboard(),"Passive image fixture did not initially acquire gameplay input.");
    ui.key(SDL_SCANCODE_W,true);ui.tick();
    require(!ui.input.ownsKeyboard() && ui.input.movement()==Vec2{},"Passive image no longer reproduces the held-click ownership bug.");
}
void interactiveImageRetainsFpsControl(bool docked) {
    GameUi ui(true,docked);
    ImGui::GetIO().ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
    ui.mouse(ui.imagePoint,true);
    require(ui.input.ownsKeyboard(),"Game image did not acquire gameplay input on mouse down.");
    ui.key(SDL_SCANCODE_W,true);ui.key(SDL_SCANCODE_D,true);ui.tick();ui.tick();
    require(ui.input.ownsKeyboard() && ui.input.movement()==Vec2{1,1},"Holding the Game image canceled WASD ownership.");
    ui.mouse(ui.imagePoint,false);ui.tick();
    require(ui.input.ownsKeyboard() && ui.input.movement()==Vec2{1,1},"Releasing the acquisition click released held movement.");
    ui.relativeMotion(18,-7);ui.relativeMotion(-3,2);
    require(ui.input.consumeLookDelta()==Vec2{15,-5} && ui.input.consumeLookDelta()==Vec2{},"UI ownership lost or replayed relative mouse look.");
    ui.key(SDL_SCANCODE_SPACE,true);
    require(!ImGui::IsKeyDown(ImGuiKey_Space),"Game jump activated ImGui's keyboard navigation.");
    ui.key(SDL_SCANCODE_SPACE,false);
    require(ui.input.consumeJump() && !ui.input.consumeJump(),"UI ownership lost or repeated a short jump press.");
    ui.key(SDL_SCANCODE_ESCAPE,true);
    require(!ui.input.ownsKeyboard() && ui.input.movement()==Vec2{},"Escape did not release Game and held movement.");
    ui.relativeMotion(30,10);require(ui.input.consumeLookDelta()==Vec2{},"Released Game retained mouse look.");
    ui.mouse(ui.imagePoint,true);ui.tick();ui.mouse(ui.imagePoint,false);
    require(ui.input.ownsKeyboard() && ui.input.movement()==Vec2{},"Game reacquisition restored a stale held key.");
    ui.key(SDL_SCANCODE_S,true);ui.mouse(ui.cameraPoint,true);ui.mouse(ui.cameraPoint,false);
    require(!ui.input.ownsKeyboard() && ui.input.movement()==Vec2{} && !ui.input.consumeJump(),"Camera picker failed to release FPS actions.");
    require(ui.region.inputBlocked,"An open Camera picker did not block gameplay input.");
}
void playAcquiresWithoutClick(bool docked) {
    GameUi ui(true,docked);
    ImGui::SetWindowFocus(nullptr);
    if(!docked)ImGui::SetWindowCollapsed("Game",true);
    ui.tick(true);
    require(ui.input.ownsKeyboard(),"Play did not focus and acquire Game without an image click.");
    require(!ImGui::FindWindowByName("Game")->Collapsed,"Play left the Game window collapsed.");
    ui.key(SDL_SCANCODE_W,true);ui.relativeMotion(7,4);
    require(ui.input.movement()==Vec2{0,1} && ui.input.consumeLookDelta()==Vec2{7,4},"Automatic Play acquisition required another click for FPS actions.");
}
void imageClickReleasesText(bool docked) {
    GameUi ui(true,docked);ui.focusText();
    require(!ui.input.ownsKeyboard(),"Text editor did not block gameplay input.");
    ui.mouse(ui.imagePoint,true);ui.tick();ui.mouse(ui.imagePoint,false);
    require(ui.input.ownsKeyboard() && ui.input.movement()==Vec2{} && !ui.input.consumeJump(),"Text-to-Game handoff failed to acquire or fabricated FPS actions.");
    ui.key(SDL_SCANCODE_W,true);ui.relativeMotion(9,-3);
    require(ui.input.movement()==Vec2{0,1} && ui.input.consumeLookDelta()==Vec2{9,-3},"Stale text-input capture prevented FPS actions after reacquisition.");
    ui.focusText();
    require(!ui.input.ownsKeyboard() && ui.input.movement()==Vec2{},"Editing text retained held FPS movement.");
}
void imageAcknowledgesAcquisitionOnly(bool docked) {
    GameUi ui(true,docked);const auto pressed=ui.imagePoint;const ImVec2 moved{pressed.x+50,pressed.y+40};
    ui.pressThenMove(pressed,moved);
    require(ui.input.ownsKeyboard() && ui.input.movement()==Vec2{} && !ui.input.consumeJump() && ui.input.consumeLookDelta()==Vec2{50,40},
        "Image click acknowledgement changed movement or replaced relative look.");
    ui.tick();ui.mouse(moved,false);
    require(ui.input.consumeLookDelta()==Vec2{},"UI acknowledgement replayed relative mouse look.");
}
void imageAcknowledgementRespectsNativeCancellation(bool docked) {
    GameUi ui(true,docked);ui.pressThenCancel(ui.imagePoint);
    require(!ui.input.ownsKeyboard() && ui.input.movement()==Vec2{} && !ui.input.consumeJump(),"Delayed image acknowledgement revived an Escape-canceled press.");
    ui.mouse(ui.imagePoint,false);ui.mouse(ui.imagePoint,true);
    require(ui.input.ownsKeyboard(),"A fresh image press remained suppressed after acknowledging cancellation.");
    ui.key(SDL_SCANCODE_W,true);ui.key(SDL_SCANCODE_ESCAPE,true);ui.tick();
    require(!ui.input.ownsKeyboard() && ui.input.movement()==Vec2{},"An existing image hold reacquired FPS ownership after Escape.");
}
void trickledShortClickAcquiresOnce(bool docked) {
    GameUi ui(true,docked);ui.queuedShortClick(ui.imagePoint);
    require(ui.input.ownsKeyboard() && ui.input.movement()==Vec2{} && !ui.input.consumeJump(),"A trickled short image click failed to acquire FPS input.");
    ui.tick();ui.tick();ui.key(SDL_SCANCODE_ESCAPE,true);ui.tick();
    require(!ui.input.ownsKeyboard(),"A trickled UI release reacquired Game after Escape.");
}
void attributeDebugOwnsTabOnlyInGame(bool docked) {
    GameUi ui(true,docked);ui.tick(true);ui.tick();
    ui.uiKey(ImGuiKey_Tab,true);require(ui.debugCycle==0,"Disabled attribute debug consumed Tab.");
    ui.uiKey(ImGuiKey_Tab,false);ui.debugEnabled=true;ui.tick(true);ui.tick();
    ui.uiKey(ImGuiKey_Tab,true);
    require(ui.debugCycle==1&&!ui.debugTabNavigation,"Focused attribute debug did not own forward Tab ahead of widget navigation.");
    for(unsigned index=0;index<40;++index){ui.tick();require(ui.debugCycle==0,"Held Tab repeated target cycling.");}
    ui.uiKey(ImGuiKey_Tab,false);ui.uiKey(ImGuiMod_Shift,true);ui.tick();ui.uiKey(ImGuiKey_Tab,true);
    require(ui.debugCycle==-1&&!ui.debugTabNavigation,"Shift+Tab did not cycle backward exclusively.");
    ui.uiKey(ImGuiKey_Tab,false);ui.uiKey(ImGuiMod_Shift,false);
    for(const auto modifier:{ImGuiMod_Ctrl,ImGuiMod_Alt,ImGuiMod_Super}) {
        ui.uiKey(modifier,true);ui.uiKey(ImGuiKey_Tab,true);
        require(ui.debugCycle==0,"Extra modifiers triggered attribute target cycling.");
        ui.uiKey(ImGuiKey_Tab,false);ui.uiKey(modifier,false);
    }
    ui.tick(true);ui.tick();ui.mouse(ui.imagePoint,true);ui.tick();ui.uiKey(ImGuiKey_Tab,true);
    require(ui.debugCycle==1&&ui.input.ownsKeyboard(),"A held Game-image gesture prevented target cycling or lost movement ownership.");
    ui.uiKey(ImGuiKey_Tab,false);ui.mouse(ui.imagePoint,false);
    ui.key(SDL_SCANCODE_ESCAPE,true);ui.tick();ui.uiKey(ImGuiKey_Tab,true);
    require(ui.debugCycle==1&&!ui.input.ownsKeyboard(),"Focused debug cycling incorrectly required or reacquired player movement ownership.");
    ui.uiKey(ImGuiKey_Tab,false);
    ui.focusText();ui.uiKey(ImGuiKey_Tab,true);
    require(ui.debugCycle==0,"Attribute debug stole Tab from a text field.");ui.uiKey(ImGuiKey_Tab,false);
    ui.tick(true);ui.tick();ui.popup=true;ui.tick();ui.uiKey(ImGuiKey_Tab,true);
    require(ui.debugCycle==0,"Attribute debug cycled through an open popup.");
}
}
int main() {
    try {
        for(const bool docked:{false,true}) {
            passiveImageLosesControl(docked);interactiveImageRetainsFpsControl(docked);playAcquiresWithoutClick(docked);imageClickReleasesText(docked);
            imageAcknowledgesAcquisitionOnly(docked);
            imageAcknowledgementRespectsNativeCancellation(docked);trickledShortClickAcquiresOnce(docked);
            attributeDebugOwnsTabOnlyInGame(docked);
        }
        std::cout<<"Game image/input integration checks passed.\n";return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
