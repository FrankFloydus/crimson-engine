#include "tools/RagdollPreviewInteraction.h"
#include "tools/RagdollEditorLayout.h"
#include <imgui_internal.h>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

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
class WorkspaceUi {
public:
    WorkspaceUi() {
        context_=ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1600,1000};io.DeltaTime=1.f/60;io.ConfigInputTrickleEventQueue=false;io.ConfigWindowsMoveFromTitleBarOnly=false;
        unsigned char* pixels{};int width{},height{};io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);io.Fonts->SetTexID(static_cast<ImTextureID>(1));tick();tick();
    }
    ~WorkspaceUi(){ImGui::DestroyContext(context_);}
    void tick() {
        ImGui::NewFrame();ImGui::SetNextWindowPos({20,20},ImGuiCond_FirstUseEver);
        if(resizePending_){ImGui::SetNextWindowSize(size_);resizePending_=false;}
        ImGui::Begin("Ragdoll workspace fixture",nullptr,ImGuiWindowFlags_NoDocking);
        const auto origin=ImGui::GetCursorScreenPos(),available=ImGui::GetContentRegionAvail();workspace={origin,{origin.x+available.x,origin.y+available.y}};order.clear();
        previewDrawn=drawRagdollWorkspace(simulationHeight,[&] {
            order+='B';bones=ImGui::GetCurrentWindow()->Rect();bonesWindow=ImGui::GetCurrentWindow();
            for(int i=0;i<90;++i)ImGui::Text("Bone %d",i);
        },[&] {
            order+='S';simulation=ImGui::GetCurrentWindow()->Rect();simulationWindow=ImGui::GetCurrentWindow();controlsWindow=simulationWindow->ParentWindow;controls=controlsWindow->Rect();
            if(ImGui::Button("Simulate"))simulated=!simulated;const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();simulationButton={(a.x+b.x)*.5f,(a.y+b.y)*.5f};
            for(int i=0;i<50;++i)ImGui::Text("Simulation control %d",i);
        },[&] {
            order+='P';properties=ImGui::GetCurrentWindow()->Rect();propertiesWindow=ImGui::GetCurrentWindow();
            ImGui::DragFloat("Mass",&mass,.1f);const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();propertyPoint={(a.x+b.x)*.5f,(a.y+b.y)*.5f};
            for(int i=0;i<80;++i)ImGui::Text("Body property %d",i);
        },[&] {
            order+='V';preview=ImGui::GetCurrentWindow()->Rect();previewWindow=ImGui::GetCurrentWindow();observedMass=mass;observedSimulated=simulated;
            const auto p=ImGui::GetCursorScreenPos(),s=ImGui::GetContentRegionAvail();imagePoint={p.x+s.x*.5f,p.y+s.y*.5f};interaction=drawRagdollPreviewImage(ImTextureRef(static_cast<ImTextureID>(1)),s,true);imageId=ImGui::GetItemID();
        });
        ImGui::End();ImGui::Render();auto* parent=ImGui::FindWindowByName("Ragdoll workspace fixture");windowPosition=parent->Pos;parentScroll=parent->ScrollMax;
        dividerPoint={controls.GetCenter().x,(simulation.Max.y+properties.Min.y)*.5f};
        leftColumnDivider={bones.Max.x+ImGui::GetStyle().CellPadding.x,(bones.Min.y+bones.Max.y)*.5f};
        rightColumnDivider={controls.Min.x-ImGui::GetStyle().CellPadding.x,(controls.Min.y+controls.Max.y)*.5f};
    }
    void motion(ImVec2 point){ImGui::GetIO().AddMousePosEvent(point.x,point.y);tick();}
    void mouse(ImVec2 point,bool down){auto& io=ImGui::GetIO();io.AddMousePosEvent(point.x,point.y);io.AddMouseButtonEvent(ImGuiMouseButton_Left,down);tick();}
    void resize(ImVec2 size){size_=size;resizePending_=true;tick();tick();}
    void wheel(ImVec2 point,float amount){motion(point);ImGui::GetIO().AddMouseWheelEvent(0,amount);tick();tick();}
    bool previewDrawn{},simulated{},observedSimulated{};
    float simulationHeight{300},mass{1},observedMass{};
    std::string order;
    ImRect workspace,bones,simulation,properties,preview,controls;
    ImVec2 imagePoint{},propertyPoint{},simulationButton{},dividerPoint{},leftColumnDivider{},rightColumnDivider{},windowPosition{},parentScroll{};
    ImGuiID imageId{};
    RagdollPreviewInteraction interaction;
    ImGuiWindow *bonesWindow{},*simulationWindow{},*propertiesWindow{},*previewWindow{},*controlsWindow{};
private:
    ImGuiContext* context_{};
    ImVec2 size_{1260,780};
    bool resizePending_{true};
};
void workspaceGeometryAndEvaluationOrder() {
    WorkspaceUi ui;require(ui.previewDrawn&&ui.order=="BSPV","Workspace did not evaluate controls before its preview.");
    require(ui.bones.Min.x<ui.preview.Min.x&&ui.preview.Max.x<ui.controls.Min.x,"Workspace columns overlap or appear in the wrong order.");
    require(std::abs(ui.bones.Min.y-ui.preview.Min.y)<1&&std::abs(ui.preview.Min.y-ui.controls.Min.y)<1&&std::abs(ui.bones.Max.y-ui.preview.Max.y)<1&&std::abs(ui.preview.Max.y-ui.controls.Max.y)<1,"Workspace panes did not occupy the same full height.");
    require(ui.simulation.Max.y<ui.properties.Min.y&&ui.properties.Max.y<=ui.controls.Max.y+1,"Simulation and properties overlap or exceed their sidebar.");
    require(ui.bones.Min.y>=ui.workspace.Min.y&&ui.bones.Max.y<=ui.workspace.Max.y+1&&ui.parentScroll.y<1&&ui.controlsWindow->ScrollMax.y<1,"Workspace created parent scrolling or exceeded its available height.");
    require((ui.previewWindow->Flags&ImGuiWindowFlags_NoScrollbar)&&(ui.previewWindow->Flags&ImGuiWindowFlags_NoScrollWithMouse),"Preview permits scrolling instead of camera zoom.");
    const auto button=ui.simulationButton;ui.mouse(button,true);ui.mouse(button,false);
    require(ui.simulated&&ui.observedSimulated,"Simulation controls did not affect preview evaluation in the same frame.");
}
void workspaceNativeColumnAndDividerResizing() {
    WorkspaceUi ui;const auto originalWindow=ui.windowPosition;const float oldBones=ui.bones.GetWidth();auto point=ui.leftColumnDivider;
    ui.motion(point);ui.mouse(point,true);ui.motion({point.x+70,point.y});ui.mouse({point.x+70,point.y},false);ui.tick();
    require(ui.bones.GetWidth()>oldBones+45&&same(ui.windowPosition,originalWindow),"Native left column resizing did not resize the pane or moved the window.");
    const float oldRight=ui.controls.GetWidth();point=ui.rightColumnDivider;ui.motion(point);ui.mouse(point,true);ui.motion({point.x-55,point.y});ui.mouse({point.x-55,point.y},false);ui.tick();
    require(ui.controls.GetWidth()>oldRight+35&&ui.preview.GetWidth()>40,"Native right column resizing did not preserve a usable preview.");
    const float oldTop=ui.simulation.GetHeight(),oldBottom=ui.properties.GetHeight();point=ui.dividerPoint;
    ui.motion(point);ui.mouse(point,true);ui.motion({point.x,point.y+65});ui.mouse({point.x,point.y+65},false);ui.tick();
    require(ui.simulation.GetHeight()>oldTop+45&&ui.properties.GetHeight()<oldBottom-45&&same(ui.windowPosition,originalWindow),"Horizontal divider did not resize both panes or moved its window.");
    point=ui.dividerPoint;ui.motion(point);ui.mouse(point,true);ui.motion({point.x,point.y+2000});ui.mouse({point.x,point.y+2000},false);ui.tick();
    require(ui.properties.GetHeight()>=119&&ui.properties.Max.y<=ui.controls.Max.y+1,"Divider drag collapsed or pushed properties outside the sidebar.");
    ui.resize({900,430});require(ui.simulation.GetHeight()>=99&&ui.properties.GetHeight()>=119&&ui.preview.Max.y<=ui.workspace.Max.y+1&&ui.parentScroll.y<1,"Window resize lost pane minimums or created parent overflow.");
    ui.resize({900,220});require(ui.simulation.GetHeight()>30&&ui.properties.GetHeight()>40&&ui.properties.Max.y<=ui.controls.Max.y+1&&ui.preview.Max.y<=ui.workspace.Max.y+1,"Small-window divider bounds failed to adapt to available space.");
    ui.resize({900,40});require(std::isfinite(ui.simulationHeight)&&ui.simulationHeight>=0&&ui.previewDrawn==(ui.order.find('V')!=std::string::npos),"Clipped workspace lost its preview visibility contract.");
    ui.resize({1100,640});require(ui.order=="BSPV"&&ui.properties.Max.y<=ui.controls.Max.y+1&&ui.parentScroll.y<1,"Restoring a clipped workspace left unbalanced panes or stale overflow.");
}
void workspaceScrollingAndInputIsolation() {
    WorkspaceUi ui;require(ui.simulationWindow->ScrollMax.y>0&&ui.propertiesWindow->ScrollMax.y>0&&ui.bonesWindow->ScrollMax.y>0,"Workspace scroll fixtures did not overflow their own panes.");
    const float oldProperties=ui.propertiesWindow->Scroll.y;ui.wheel(ui.simulation.GetCenter(),-3);
    require(ui.simulationWindow->Scroll.y>0&&ui.propertiesWindow->Scroll.y==oldProperties&&ui.parentScroll.y<1,"Simulation scrolling moved another pane or the parent.");
    const float oldSimulation=ui.simulationWindow->Scroll.y;ui.wheel(ui.properties.GetCenter(),-3);
    require(ui.propertiesWindow->Scroll.y>0&&ui.simulationWindow->Scroll.y==oldSimulation,"Property scrolling moved the simulation pane.");
    ImGui::SetCurrentContext(ui.previewWindow->Ctx);ImGui::SetScrollY(ui.propertiesWindow,0);ui.tick();ui.tick();
    const auto original=ui.windowPosition,property=ui.propertyPoint;ui.mouse(property,true);ui.motion({property.x+35,property.y});
    require(ui.mass>1&&ui.observedMass==ui.mass,"Property drag did not reach the preview's same-frame state.");
    ui.motion(ui.imagePoint);require(!ui.interaction.active&&ui.interaction.blocked&&same(ui.windowPosition,original),"Property gesture was stolen by the central preview or moved the window.");ui.mouse(ui.imagePoint,false);
    const auto previewPoint=ui.imagePoint;ui.mouse(previewPoint,true);ui.motion({previewPoint.x+30,previewPoint.y+20});
    require(ui.interaction.active&&!ui.interaction.blocked&&same(ui.windowPosition,original),"Central preview failed to retain its own mouse gesture.");ui.mouse(previewPoint,false);
}
}
int main(){try{passiveImageMovesWindow();for(bool ready:{false,true})for(int button:{ImGuiMouseButton_Left,ImGuiMouseButton_Middle})interactiveImageOwnsHeldGestures(ready,button);firstClickTransfersTextFocus();wheelAndUnrelatedWidgets();titleBarRemainsMovable();cancellationBlocksGesture();workspaceGeometryAndEvaluationOrder();workspaceNativeColumnAndDividerResizing();workspaceScrollingAndInputIsolation();std::cout<<"Ragdoll preview input and workspace layout checks passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
