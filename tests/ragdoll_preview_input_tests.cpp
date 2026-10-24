#include "tools/RagdollPreviewInteraction.h"
#include <imgui_internal.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool same(const ImVec2& a,const ImVec2& b){return std::abs(a.x-b.x)<.1f&&std::abs(a.y-b.y)<.1f;}

// All events enter a private CPU ImGui context. This test initializes no SDL,
// native window, renderer, desktop input or physics simulation.
class PreviewUi {
public:
    explicit PreviewUi(bool interactive=true,bool imageReady=true):interactive_(interactive),imageReady_(imageReady) {
        context_=ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;
        io.DisplaySize={1100,760};io.DeltaTime=1.f/60;io.ConfigInputTrickleEventQueue=false;
        io.ConfigWindowsMoveFromTitleBarOnly=false;
        unsigned char* pixels{};int width{},height{};io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
        io.Fonts->SetTexID(static_cast<ImTextureID>(1));tick();tick();
    }
    ~PreviewUi(){ImGui::DestroyContext(context_);}
    void tick() {
        ImGui::NewFrame();
        if(textWindow_) {
            ImGui::SetNextWindowPos({750,20},ImGuiCond_FirstUseEver);ImGui::SetNextWindowSize({300,170},ImGuiCond_FirstUseEver);
            ImGui::Begin("Ragdoll test text",nullptr,ImGuiWindowFlags_NoDocking);
            ImGui::InputText("Name",text_,sizeof(text_));const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();textPoint_={a.x+30,(a.y+b.y)*.5f};ImGui::End();
        }
        ImGui::SetNextWindowPos({20,20},ImGuiCond_FirstUseEver);ImGui::SetNextWindowSize({650,610},ImGuiCond_FirstUseEver);
        ImGui::Begin("Ragdoll input fixture",nullptr,ImGuiWindowFlags_NoDocking);
        const auto* window=ImGui::GetCurrentWindow();windowPosition=window->Pos;
        const auto title=window->TitleBarRect();titlePoint={title.Min.x+100,(title.Min.y+title.Max.y)*.5f};
        ImGui::Button("Unrelated property");const auto buttonA=ImGui::GetItemRectMin(),buttonB=ImGui::GetItemRectMax();buttonPoint={(buttonA.x+buttonB.x)*.5f,(buttonA.y+buttonB.y)*.5f};
        ImGui::BeginChild("Ragdoll preview child",{0,0});
        const auto origin=ImGui::GetCursorScreenPos(),size=ImGui::GetContentRegionAvail();
        imagePoint={origin.x+size.x*.5f,origin.y+size.y*.5f};outsidePoint={origin.x+size.x+35,origin.y+size.y*.5f};
        if(interactive_)interaction=drawRagdollPreviewImage(ImTextureRef(static_cast<ImTextureID>(1)),size,imageReady_);
        else {
            // Reproduce the original production behavior: Image has ID zero,
            // so an ordinary held image click starts moving its parent window.
            ImGui::Image(ImTextureRef(static_cast<ImTextureID>(1)),size);
            interaction.hovered=ImGui::IsItemHovered();interaction.active=ImGui::IsItemActive();
            interaction.blocked=ImGui::GetIO().WantTextInput||ImGui::IsAnyItemActive();
        }
        imageId=ImGui::GetItemID();activeId=ImGui::GetActiveID();
        activeMouse=ImGui::GetCurrentContext()->ActiveIdMouseButton;
        wheel=ImGui::GetIO().MouseWheel;
        previewFocused=ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        const auto boundsA=ImGui::GetItemRectMin(),boundsB=ImGui::GetItemRectMax();
        require(same(boundsA,origin)&&same(boundsB,{origin.x+size.x,origin.y+size.y}),"Preview image widget changed the requested image bounds.");
        ImGui::EndChild();ImGui::End();ImGui::Render();
        windowPosition=ImGui::FindWindowByName("Ragdoll input fixture")->Pos;
    }
    void motion(const ImVec2& point){ImGui::GetIO().AddMousePosEvent(point.x,point.y);tick();}
    void mouse(const ImVec2& point,int button,bool down){auto& io=ImGui::GetIO();io.AddMousePosEvent(point.x,point.y);io.AddMouseButtonEvent(button,down);tick();}
    void focusText(){textWindow_=true;tick();tick();motion(textPoint_);mouse(textPoint_,ImGuiMouseButton_Left,true);mouse(textPoint_,ImGuiMouseButton_Left,false);tick();require(ImGui::GetIO().WantTextInput,"Text fixture did not own text input.");}
    RagdollPreviewInteraction interaction;
    ImVec2 imagePoint{},outsidePoint{},titlePoint{},buttonPoint{},windowPosition{};
    ImGuiID imageId{},activeId{};
    int activeMouse{-1};
    float wheel{};
    bool previewFocused{};
private:
    ImGuiContext* context_{};
    bool interactive_{},imageReady_{},textWindow_{};
    ImVec2 textPoint_{};
    char text_[64]{"Enemy"};
};
void passiveImageMovesWindow() {
    PreviewUi ui(false);const auto original=ui.windowPosition;const auto press=ui.imagePoint;
    ui.motion(press);ui.mouse(press,ImGuiMouseButton_Left,true);ui.motion({press.x+70,press.y+45});ui.tick();
    require(!same(ui.windowPosition,original),"Passive image no longer reproduces accidental window movement.");
    ui.mouse({press.x+70,press.y+45},ImGuiMouseButton_Left,false);
}
void interactiveImageOwnsHeldGestures(bool ready,int button) {
    PreviewUi ui(true,ready);const auto original=ui.windowPosition;const auto press=ui.imagePoint;
    ui.motion(press);ui.mouse(press,button,true);
    require(ui.interaction.active&&!ui.interaction.blocked&&ui.imageId&&ui.activeId==ui.imageId,"Preview did not retain its own active item on mouse down.");
    require(ui.activeMouse==button,"Preview claimed the wrong initiating mouse button.");
    ui.motion({press.x+50,press.y+35});ui.tick();
    require(ui.interaction.active&&!ui.interaction.blocked&&same(ui.windowPosition,original),"Held preview input blocked itself or moved its window.");
    const auto outside=ui.outsidePoint;ui.motion(outside);ui.tick();
    require(!ui.interaction.hovered&&ui.interaction.active&&!ui.interaction.blocked&&same(ui.windowPosition,original),"Dragging outside the image lost ownership or moved its window.");
    ui.mouse(outside,button,false);ui.tick();
    require(!ui.interaction.active&&ui.activeId!=ui.imageId&&same(ui.windowPosition,original),"Releasing outside the image retained its active ID or moved its window.");
}
void firstClickTransfersTextFocus() {
    PreviewUi ui;ui.focusText();require(ui.interaction.blocked&&!ui.interaction.active,"An unrelated text field did not block the preview.");
    const auto press=ui.imagePoint;ui.motion(press);ui.mouse(press,ImGuiMouseButton_Left,true);
    require(ui.interaction.active&&!ui.interaction.blocked&&ui.previewFocused,"First image click did not transfer focus and clear stale text ownership.");
    ui.motion({press.x+20,press.y+10});require(ui.interaction.active&&!ui.interaction.blocked,"Text-to-preview focus handoff lost a held gesture.");
    ui.mouse(press,ImGuiMouseButton_Left,false);
}
void wheelAndUnrelatedWidgets() {
    PreviewUi ui;ui.motion(ui.imagePoint);ImGui::GetIO().AddMouseWheelEvent(0,1);ui.tick();
    require(ui.interaction.hovered&&!ui.interaction.active&&!ui.interaction.blocked&&ui.wheel>0,"Hovered preview did not admit mouse-wheel zoom.");
    const auto point=ui.buttonPoint;ui.mouse(point,ImGuiMouseButton_Left,true);
    require(!ui.interaction.active&&ui.interaction.blocked,"An unrelated active property widget did not block preview input.");
    ui.motion(ui.imagePoint);require(!ui.interaction.active&&ui.interaction.blocked,"Moving a property gesture over the image stole its active ID.");
    ui.mouse(ui.imagePoint,ImGuiMouseButton_Left,false);
}
void titleBarRemainsMovable() {
    PreviewUi ui;const auto original=ui.windowPosition,press=ui.titlePoint;
    ui.motion(press);ui.mouse(press,ImGuiMouseButton_Left,true);ui.motion({press.x+55,press.y+30});ui.tick();
    require(!same(ui.windowPosition,original),"Interactive preview disabled ordinary title-bar window movement.");
    ui.mouse({press.x+55,press.y+30},ImGuiMouseButton_Left,false);
}
void cancellationBlocksGesture() {
    PreviewUi ui;const auto press=ui.imagePoint;ui.motion(press);ui.mouse(press,ImGuiMouseButton_Left,true);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape,true);ui.tick();require(ui.interaction.blocked,"Escape did not block preview dragging.");
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape,false);ui.mouse(press,ImGuiMouseButton_Left,false);
    ui.mouse(ui.imagePoint,ImGuiMouseButton_Middle,true);require(ui.interaction.active&&!ui.interaction.blocked,"Preview did not reacquire an independent middle-button gesture.");
    ImGui::GetIO().AddFocusEvent(false);ui.tick();require(ui.interaction.blocked,"Application focus loss did not block preview input.");
    ImGui::GetIO().AddFocusEvent(true);ui.tick();ui.tick();
    require(!ui.interaction.active,"Focus return revived a stale held preview gesture.");
}
}
int main(){try{passiveImageMovesWindow();for(bool ready:{false,true})for(int button:{ImGuiMouseButton_Left,ImGuiMouseButton_Middle})interactiveImageOwnsHeldGestures(ready,button);firstClickTransfersTextFocus();wheelAndUnrelatedWidgets();titleBarRemainsMovable();cancellationBlocksGesture();std::cout<<"Ragdoll preview input checks passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
