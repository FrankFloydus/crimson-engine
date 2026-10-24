#include "ragdoll_fixture.h"
#include "tools/RagdollBodyGizmo.h"
#include "tools/RagdollPreviewInteraction.h"
#include <ImGuizmo.h>
#include <imgui_internal.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace crimson;
using namespace crimson::ragdoll_test;
bool same(const ImVec2& a,const ImVec2& b){return std::abs(a.x-b.x)<.1f&&std::abs(a.y-b.y)<.1f;}
// This drives the production image item and gizmo with a private CPU ImGui
// context. There is no SDL, renderer, operating-system input or native window.
class GizmoUi {
public:
    explicit GizmoUi(RagdollAuthoringOperation operation=RagdollAuthoringOperation::Translate):document(project.root),operation(operation) {
        auto definition=asset();definition.bodies[0].shape=RagdollShape::Box;definition.bodies[0].position={};definition.bodies[0].rotation={};definition.bodies[0].size={.6f,.4f,.3f};
        require(document.create(std::move(definition)),"Could not create gizmo document.");baseline=document.definition();
        context=ImGui::CreateContext();ImGuizmo::SetImGuiContext(context);ImGuizmo::Enable(true);
        auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1100,760};io.DeltaTime=1.f/60;io.ConfigInputTrickleEventQueue=false;io.ConfigWindowsMoveFromTitleBarOnly=false;
        unsigned char* pixels{};int width{},height{};io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);io.Fonts->SetTexID(static_cast<ImTextureID>(1));
        camera.position={0,0,5};camera.target={0,0,0};camera.fovAxis=CameraFovAxis::Vertical;tick();tick();
    }
    ~GizmoUi(){ImGuizmo::SetImGuiContext(context);gizmo.cancel(document);ImGuizmo::Enable(false);ImGuizmo::Enable(true);ImGui::DestroyContext(context);}
    void tick() {
        ImGui::NewFrame();ImGuizmo::BeginFrame();
        ImGui::SetNextWindowPos({20,20},ImGuiCond_FirstUseEver);ImGui::SetNextWindowSize({900,680},ImGuiCond_FirstUseEver);
        ImGui::Begin("Ragdoll gizmo fixture",nullptr,ImGuiWindowFlags_NoDocking);
        ImGui::Button("Unrelated property");const auto pa=ImGui::GetItemRectMin(),pb=ImGui::GetItemRectMax();propertyPoint={(pa.x+pb.x)*.5f,(pa.y+pb.y)*.5f};
        ImGui::BeginChild("Preview",{0,0});const auto origin=ImGui::GetCursorScreenPos(),size=ImGui::GetContentRegionAvail();region={origin.x,origin.y,size.x,size.y};
        interaction=drawRagdollPreviewImage(ImTextureRef(static_cast<ImTextureID>(1)),size,true);
        imageId=ImGui::GetItemID();const auto before=ImGui::GetActiveID();
        claimed=gizmo.draw(document,selected,bone,camera,region,operation,lock,interaction.blocked||block);
        activeIdPreserved=before==ImGui::GetActiveID();activeId=ImGui::GetActiveID();
        if(foreignGizmo) {
            // A second production helper must neither claim nor cancel this
            // helper's native gesture when it is recorded later in the frame.
            const auto prior=gizmo.active();const bool foreignClaimed=other.draw(document,selected,bone,camera,region,operation,false,false);
            foreignPreserved=!prior||(!foreignClaimed&&gizmo.active()&&ImGuizmo::IsUsingAny());
        }
        const auto proxy=ragdollBodyGizmoMatrix(document.definition().bodies[selected],bone,operation);
        if(proxy)if(const auto projected=math::project({(*proxy)[12],(*proxy)[13],(*proxy)[14]},camera.projectionMatrix(size.x/size.y,ClipDepth::NegativeOneToOne)*camera.viewMatrix(),{origin.x,origin.y,size.x,size.y},ClipDepth::NegativeOneToOne))center={(*projected)[0],(*projected)[1]};
        ImGui::EndChild();ImGui::End();ImGui::Render();windowPosition=ImGui::FindWindowByName("Ragdoll gizmo fixture")->Pos;
    }
    void motion(ImVec2 point){ImGui::GetIO().AddMousePosEvent(point.x,point.y);tick();}
    void mouse(ImVec2 point,bool down,int button=ImGuiMouseButton_Left){auto& io=ImGui::GetIO();io.AddMousePosEvent(point.x,point.y);io.AddMouseButtonEvent(button,down);tick();}
    ImVec2 handle() {
        const auto middle=center;
        if(operation==RagdollAuthoringOperation::Rotate) {
            for(float radius=18;radius<140;radius+=2){const ImVec2 candidate{middle.x+radius*.70710678f,middle.y-radius*.70710678f};motion(candidate);if(claimed)return candidate;}
        } else for(float offset=18;offset<140;offset+=2){const ImVec2 candidate{middle.x+offset,middle.y};motion(candidate);if(claimed)return candidate;}
        throw std::runtime_error("Could not locate a production gizmo handle.");
    }
    void begin(ImVec2 point) {
        motion(point);mouse(point,true);
        require(gizmo.active()&&document.transactionActive()&&claimed,"Production gizmo did not acquire the image press.");
        require(activeIdPreserved&&activeId==imageId,"Gizmo stole or cleared the preview's ImGui mouse owner.");
    }
    Project project;
    RagdollDocument document;
    RagdollAsset baseline;
    RagdollBodyGizmo gizmo,other;
    Camera camera;
    ViewportRegion region;
    Mat4 bone;
    RagdollAuthoringOperation operation;
    std::size_t selected{};
    bool lock{},block{},claimed{},activeIdPreserved{},foreignGizmo{},foreignPreserved{true};
    RagdollPreviewInteraction interaction;
    ImVec2 center{},propertyPoint{},windowPosition{};
    ImGuiID imageId{},activeId{};
private:
    ImGuiContext* context{};
};
void realGestureCommitsOneCommand(RagdollAuthoringOperation operation) {
    GizmoUi ui(operation);const auto point=ui.handle(),originalWindow=ui.windowPosition;const auto committed=ui.document.committed();
    ui.begin(point);
    const ImVec2 delta=operation==RagdollAuthoringOperation::Rotate?ImVec2{25,35}:ImVec2{35,0};
    ui.motion({point.x+delta.x*.5f,point.y+delta.y*.5f});ui.motion({point.x+delta.x,point.y+delta.y});
    require(ui.document.definition()!=ui.baseline,"Production gizmo movement did not update the body.");
    require(ui.document.committed()==committed&&!ui.document.canUndo(),"Unfinished gizmo gesture published history or shared data.");
    require(ui.activeIdPreserved&&ui.activeId==ui.imageId&&same(ui.windowPosition,originalWindow),"Held gizmo gesture moved its window or lost image ownership.");
    validateRagdollAsset(ui.document.definition());const auto edited=ui.document.definition();
    require(edited.bodies[1]==ui.baseline.bodies[1],"Gizmo gesture changed an unselected physical body.");
    ui.mouse({point.x+delta.x,point.y+delta.y},false);ui.tick();
    require(!ui.gizmo.active()&&!ui.document.transactionActive()&&ui.document.canUndo(),"Gizmo release did not commit its document gesture.");
    require(ui.document.undo()&&ui.document.definition()==ui.baseline&&!ui.document.canUndo(),"Gizmo gesture did not undo exactly as one command.");
    require(ui.document.redo()&&ui.document.definition()==edited,"Gizmo redo lost the exact edited body.");
}
void cancellationAndHeldMouseGate(bool focusLoss) {
    GizmoUi ui;const auto point=ui.handle();ui.begin(point);ui.motion({point.x+30,point.y});
    if(ui.document.definition()==ui.baseline)throw std::runtime_error(std::string(focusLoss?"Focus-loss":"Escape")+" cancellation fixture did not change the body (active="+std::to_string(ui.gizmo.active())+", status="+ui.gizmo.diagnostic()+").");
    if(focusLoss)ImGui::GetIO().AddFocusEvent(false);else ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape,true);ui.tick();
    require(!ui.gizmo.active()&&!ui.document.transactionActive()&&!ui.document.canUndo()&&ui.document.definition()==ui.baseline,"Gizmo cancellation changed history or failed exact restoration.");
    if(focusLoss){ImGui::GetIO().AddFocusEvent(true);ui.tick();}else {ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape,false);ui.motion({point.x+45,point.y});require(!ui.gizmo.active(),"Held mouse restarted a canceled gizmo gesture.");}
    ui.mouse(point,false);ui.tick();
}
void clickWithoutMotionIsNotAnEdit() {
    GizmoUi ui;const auto point=ui.handle();ui.begin(point);ui.mouse(point,false);ui.tick();
    require(!ui.gizmo.active()&&!ui.document.transactionActive()&&!ui.document.canUndo()&&ui.document.definition()==ui.baseline,"Clicking a gizmo without motion created a history command.");
}
void foreignInputAndGizmoOwnership() {
    GizmoUi ui;const auto handle=ui.handle();ui.block=true;ui.mouse(handle,true);ui.motion({handle.x+30,handle.y});require(!ui.gizmo.active()&&ui.document.definition()==ui.baseline,"Blocked preview input activated a gizmo.");ui.mouse(handle,false);ui.block=false;
    ui.foreignGizmo=true;ui.begin(handle);ui.motion({handle.x+30,handle.y});require(ui.foreignPreserved,"A later gizmo owner canceled the active production gizmo.");ui.mouse({handle.x+30,handle.y},false);
    GizmoUi property;const auto point=property.propertyPoint;property.mouse(point,true);property.motion(property.center);
    require(!property.gizmo.active()&&property.document.definition()==property.baseline,"Property gesture entered the preview gizmo.");property.mouse(property.center,false);
}
}
int main(){try{for(const auto operation:{RagdollAuthoringOperation::Translate,RagdollAuthoringOperation::Rotate,RagdollAuthoringOperation::Resize})realGestureCommitsOneCommand(operation);cancellationAndHeldMouseGate(false);cancellationAndHeldMouseGate(true);clickWithoutMotionIsNotAnEdit();foreignInputAndGizmoOwnership();std::cout<<"Production ragdoll gizmo UI gesture checks passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
