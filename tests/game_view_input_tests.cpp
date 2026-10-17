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
        else if(interaction.clicked || focusAndAcquire)input.acquire();
        if(interaction.hovered && !interaction.blocked) {
            const auto pointer=ImGui::GetIO().MousePos;input.submitUiPointer({pointer.x,pointer.y},interaction.clicked,interaction.held);
        }
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
        event.key.windowID=9;event.key.scancode=key;input.handleEvent(event);tick();
    }
    void uiKey(ImGuiKey key,bool down) {ImGui::GetIO().AddKeyEvent(key,down);tick();}
    void motion(ImVec2 point) {
        ImGui::GetIO().AddMousePosEvent(point.x,point.y);
        SDL_Event event{};event.type=SDL_EVENT_MOUSE_MOTION;event.motion.windowID=9;event.motion.x=point.x;event.motion.y=point.y;
        input.handleEvent(event);tick();
    }
    void pressThenMove(ImVec2 press,ImVec2 moved) {
        auto& io=ImGui::GetIO();io.AddMousePosEvent(press.x,press.y);io.AddMouseButtonEvent(ImGuiMouseButton_Left,true);
        SDL_Event event{};event.type=SDL_EVENT_MOUSE_BUTTON_DOWN;event.button.windowID=9;event.button.button=SDL_BUTTON_LEFT;
        event.button.x=press.x;event.button.y=press.y;input.handleEvent(event);
        io.AddMousePosEvent(moved.x,moved.y);event={};event.type=SDL_EVENT_MOUSE_MOTION;event.motion.windowID=9;
        event.motion.x=moved.x;event.motion.y=moved.y;input.handleEvent(event);tick();
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
    ui.tick();
    require(!ui.input.ownsKeyboard(),"Passive image no longer reproduces the held-click ownership bug.");
    ui.mouse(ui.imagePoint,false);
    require(!ui.input.consumeMoveClick(),"Passive-image reproduction unexpectedly retained its clicked destination.");
}
void interactiveImageRetainsControl(bool docked) {
    GameUi ui(true,docked);
    ui.mouse(ui.imagePoint,true);
    require(ui.input.ownsKeyboard(),"Game image did not acquire gameplay input on mouse down.");
    ui.tick();ui.tick();
    require(ui.input.ownsKeyboard(),"Holding the Game image click canceled gameplay ownership.");
    const Vec2 clicked{ui.imagePoint.x,ui.imagePoint.y};
    require(ui.input.consumeMoveClick()==clicked && !ui.input.consumeMoveClick(),"A held image click was lost or replayed.");
    ui.mouse(ui.imagePoint,false);ui.tick();
    require(ui.input.ownsKeyboard() && ui.input.pointerPosition()==clicked,"Releasing the image click lost pointer facing.");
    const ImVec2 facing{ui.imagePoint.x+20,ui.imagePoint.y+30};ui.motion(facing);
    require(ui.input.pointerPosition()==Vec2{facing.x,facing.y} && !ui.input.consumeMoveClick(),"Mouse motion did not update facing independently of travel.");
    ui.key(SDL_SCANCODE_ESCAPE,true);
    require(!ui.input.ownsKeyboard(),"Escape did not release Game image input.");
    ui.mouse(ui.imagePoint,true);ui.tick();ui.mouse(ui.imagePoint,false);
    require(ui.input.ownsKeyboard(),"Game image did not reacquire control after Escape.");
    require(ui.input.consumeMoveClick()==clicked,"Reacquiring Game lost the first clicked destination.");
    ui.mouse(ui.cameraPoint,true);ui.mouse(ui.cameraPoint,false);
    require(!ui.input.ownsKeyboard() && !ui.input.consumeMoveClick() && !ui.input.pointerPosition(),"Camera picker failed to release gameplay input.");
    require(ui.region.inputBlocked,"An open Camera picker did not block gameplay input.");
}
void playAcquiresWithoutClick(bool docked) {
    GameUi ui(true,docked);
    ImGui::SetWindowFocus(nullptr);
    if(!docked)ImGui::SetWindowCollapsed("Game",true);
    ui.tick(true);
    require(ui.input.ownsKeyboard(),"Play did not focus and acquire Game without an image click.");
    require(!ImGui::FindWindowByName("Game")->Collapsed,"Play left the Game window collapsed.");
    ui.motion(ui.imagePoint);
    require(ui.input.pointerPosition()==Vec2{ui.imagePoint.x,ui.imagePoint.y} && !ui.input.consumeMoveClick(),
        "Automatic Play acquisition required a click for facing or fabricated a destination.");
}
void imageClickReleasesText(bool docked) {
    GameUi ui(true,docked);ui.focusText();
    require(!ui.input.ownsKeyboard(),"Text editor did not block gameplay input.");
    ui.mouse(ui.imagePoint,true);
    require(ui.input.consumeMoveClick()==Vec2{ui.imagePoint.x,ui.imagePoint.y},"The first click leaving text editing did not issue its destination.");
    ui.tick();ui.mouse(ui.imagePoint,false);
    require(ui.input.ownsKeyboard(),"Stale text-input capture prevented Game image acquisition.");
    require(ui.input.pointerPosition()==Vec2{ui.imagePoint.x,ui.imagePoint.y} && !ui.input.consumeMoveClick(),
        "Text-to-Game handoff lost facing or replayed its destination.");
}
void imageAcknowledgesPressCoordinates(bool docked) {
    GameUi ui(true,docked);const auto pressed=ui.imagePoint;const ImVec2 moved{pressed.x+50,pressed.y+40};
    ui.pressThenMove(pressed,moved);
    require(ui.input.consumeMoveClick()==Vec2{pressed.x,pressed.y} && ui.input.pointerPosition()==Vec2{moved.x,moved.y},
        "Game UI replaced its native press position with later motion in the same event batch.");
    ui.tick();ui.mouse(moved,false);
    require(!ui.input.consumeMoveClick(),"Game UI replayed a consumed click on release.");
}
void heldImageSteersUntilRelease(bool docked) {
    GameUi ui(true,docked);
    Camera camera;camera.position={0,10,10};camera.target={0,0,0};
    ui.input.setProjection(camera,1280,720);
    ui.mouse(ui.imagePoint,true);
    require(ui.input.consumeMoveRay(camera).has_value(),"Held gesture lost its initial click ray.");
    const auto first=ui.input.heldMoveRay(camera);
    require(first.has_value(),"Game image did not begin held movement on press.");
    const ImVec2 moved{ui.imagePoint.x+60,ui.imagePoint.y+30};ui.motion(moved);
    const auto steered=ui.input.heldMoveRay(camera);
    require(steered && math::length(steered->direction-first->direction)>.01f,
        "Holding the Game image did not steer with the cursor.");
    require(!ui.input.consumeMoveRay(camera),"Held movement fabricated another button-down click.");
    // Leaving the image pauses retargeting, without turning the gesture into
    // a window drag or requiring a second press when it comes back inside.
    ui.motion({-20,-20});
    require(!ui.input.heldMoveRay(camera),"Held movement used a cursor outside Game.");
    ui.motion(moved);
    require(ui.input.heldMoveRay(camera).has_value(),"Held movement did not resume on image re-entry.");
    ui.mouse(moved,false);ui.motion(ui.imagePoint);
    require(!ui.input.heldMoveRay(camera) && !ui.input.consumeMoveRay(camera),
        "Mouse movement after release continued to steer the character.");
}
void heldImageCancellationNeedsNewPress(bool docked) {
    GameUi ui(true,docked);Camera camera;
    ui.mouse(ui.imagePoint,true);
    require(ui.input.heldMoveRay(camera).has_value(),"Cancellation fixture did not start held movement.");
    ui.key(SDL_SCANCODE_ESCAPE,true);ui.motion(ui.imagePoint);ui.tick();
    require(!ui.input.heldMoveRay(camera) && !ui.input.ownsKeyboard(),
        "An existing image hold reacquired movement after Escape.");
    ui.mouse(ui.imagePoint,false);ui.mouse(ui.imagePoint,true);
    require(ui.input.heldMoveRay(camera).has_value(),"A fresh press did not reacquire held movement.");
    ui.mouse(ui.imagePoint,false);ui.mouse(ui.cameraPoint,true);
    require(!ui.input.heldMoveRay(camera),"Camera picker retained held movement.");
}
void imageAcknowledgementRespectsNativeCancellation(bool docked) {
    GameUi ui(true,docked);Camera camera;
    ui.pressThenCancel(ui.imagePoint);
    require(!ui.input.ownsKeyboard() && !ui.input.consumeMoveRay(camera) && !ui.input.heldMoveRay(camera),
        "Delayed image acknowledgement revived an Escape-canceled press.");
    ui.mouse(ui.imagePoint,false);ui.mouse(ui.imagePoint,true);
    require(ui.input.consumeMoveRay(camera).has_value() && ui.input.heldMoveRay(camera).has_value(),
        "A new image press remained suppressed after acknowledging cancellation.");
}
void trickledImagePressRespectsNativeRelease(bool docked) {
    GameUi ui(true,docked);Camera camera;
    ui.queuedShortClick(ui.imagePoint);
    require(ui.input.consumeMoveRay(camera).has_value(),"A trickled short press lost its destination.");
    require(!ui.input.heldMoveRay(camera),"Delayed image press acknowledgement revived an already released hold.");
    ui.tick();ui.tick();
    require(!ui.input.consumeMoveRay(camera) && !ui.input.heldMoveRay(camera),
        "Trickled release replayed its destination or restored holding.");
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
            passiveImageLosesControl(docked);interactiveImageRetainsControl(docked);playAcquiresWithoutClick(docked);imageClickReleasesText(docked);
            imageAcknowledgesPressCoordinates(docked);
            heldImageSteersUntilRelease(docked);heldImageCancellationNeedsNewPress(docked);
            imageAcknowledgementRespectsNativeCancellation(docked);trickledImagePressRespectsNativeRelease(docked);
            attributeDebugOwnsTabOnlyInGame(docked);
        }
        std::cout<<"Game image/input integration checks passed.\n";return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
