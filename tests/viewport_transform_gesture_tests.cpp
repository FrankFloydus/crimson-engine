#include "editor/ViewportTransformGesture.h"
#include "scene/CameraAuthoring.h"
#include <imgui_internal.h>
#include <ImGuizmo.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(const Mat4& actual,const Mat4& expected,const char* message) {
    for(std::size_t row=0;row<4;++row)for(std::size_t column=0;column<4;++column)
        require(std::abs(actual.at(row,column)-expected.at(row,column))<.0005f,message);
}
struct Project {
    std::filesystem::path root=std::filesystem::temp_directory_path()/("crimson-transform-gesture-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Project(){std::filesystem::create_directories(root);}
    ~Project() {
        std::error_code error;
        const auto path=std::filesystem::weakly_canonical(root,error);
        const auto temp=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);
        if(!error && path.parent_path()==temp && path.filename().string().starts_with("crimson-transform-gesture-"))
            std::filesystem::remove_all(path,error);
    }
};
EntityId place(SceneDocument& document,const char* name,const Mat4& local,EntityId parent=NoEntity) {
    const auto id=document.createEmpty(name);auto record=*document.scene().find(id);
    record.local=local;record.parent=parent;require(document.updateEntity(id,record),"Could not place transform fixture.");return id;
}
void pendingAndOrdinary(const std::filesystem::path& root) {
    SceneDocument document(root);const auto entity=place(document,"Object",math::translation({2,1,3}));
    document.select({entity},entity);const auto initial=*document.scene().find(entity);
    const auto history=document.history().size();const auto count=document.scene().records().size();
    const auto proxy=math::translation({2,1,3});ViewportTransformGesture drag;
    require(drag.begin(document,{entity},proxy,ViewportTransformOperation::Translate,true),"Shift handle press did not arm.");
    require(drag.active() && drag.duplicating() && !drag.editing() && !document.transactionActive(),"A handle press authored an early duplicate transaction.");
    require(drag.update(document,proxy,true),"Identity pending sample was rejected.");
    require(drag.update(document,math::translation({1e-7f,0,0})*proxy,true),"Jitter pending sample was rejected.");
    require(drag.update(document,math::translation({1,0,0})*proxy,false),"Subthreshold pending sample was rejected.");
    require(document.scene().records().size()==count && sameEntity(*document.scene().find(entity),initial),"Pending click or subthreshold pointer motion created a copy or moved the original.");
    require(drag.finish(document) && document.history().size()==history && document.selection().ids==std::vector<EntityId>{entity},"An unchanged Shift click created history or changed selection.");
    require(drag.begin(document,{entity},proxy,ViewportTransformOperation::Translate,false),"Normal handle press failed.");
    require(!drag.duplicating() && drag.update(document,math::translation({3,0,-2})*proxy,true),"Normal drag failed.");
    require(document.scene().records().size()==count && document.selection().primary==entity,"A non-Shift drag duplicated its owner.");
    near(document.scene().find(entity)->local,math::translation({3,0,-2})*initial.local,"Normal drag used the wrong baseline.");
    require(drag.finish(document) && document.history().size()==history+1 && document.undo(),"Normal drag did not create one undo command.");
    require(sameEntity(*document.scene().find(entity),initial),"Normal drag Undo did not restore the source exactly.");
    require(drag.begin(document,{entity},proxy,ViewportTransformOperation::Scale,true) && !drag.duplicating(),"Shift+Scale incorrectly latched duplication.");
    const auto scale=proxy*math::scaling({2,.5f,1.5f})* *math::inverse(proxy);
    require(drag.update(document,scale*proxy,true) && drag.finish(document),"Ordinary Shift scale failed.");
    require(document.scene().records().size()==count && document.selection().primary==entity,"Shift+Scale created copies.");
    require(document.undo() && sameEntity(*document.scene().find(entity),initial),"Shift scale Undo changed source data.");
}
void duplicateHierarchy(const std::filesystem::path& root) {
    SceneDocument document(root);const auto actor=document.createPlayer("Actor");
    const auto camera=resolveOwnedActorCamera(document.scene(),actor);
    const auto mesh=document.createBox("Visual");auto meshRecord=*document.scene().find(mesh);meshRecord.parent=actor;
    meshRecord.local=math::translation({1,2,3});require(document.updateEntity(mesh,meshRecord),"Could not attach visual mesh.");
    auto actorRecord=*document.scene().find(actor);actorRecord.local=math::translation({4,0,-2});
    require(document.updateEntity(actor,actorRecord),"Could not position player.");
    document.select({actor,mesh},mesh);
    const auto originalSelection=document.selection();const auto originalRecords=document.scene().records();
    const auto initialWorld=*document.scene().worldTransform(actor);
    const auto history=document.history().size();const auto proxy=math::translation(math::transformPoint(initialWorld,{}));
    const auto activeCamera=document.scene().properties().activeCamera;
    const auto controlledPlayer=document.scene().properties().playerCharacter;
    ViewportTransformGesture drag;
    require(drag.begin(document,{actor,mesh},proxy,ViewportTransformOperation::Translate,true),"Hierarchy Shift drag did not arm.");
    const auto firstDelta=math::translation({2,0,1});
    require(drag.update(document,firstDelta*proxy,true) && drag.editing(),"Meaningful Shift drag did not create copies.");
    const auto copiedRoots=document.transformEditRoots();require(copiedRoots.size()==1 && copiedRoots.front()!=actor,"Selected ancestor and descendant were cloned twice.");
    const auto copiedActor=copiedRoots.front();const auto copiedMesh=document.selection().primary;
    const auto copiedCamera=resolveOwnedActorCamera(document.scene(),copiedActor);
    require(copiedCamera && copiedCamera!=camera && copiedMesh!=mesh,"Copied hierarchy lost its child camera or selected descendant.");
    require(document.scene().find(copiedMesh)->parent==copiedActor && document.scene().find(copiedCamera)->cameraFollow->target==copiedActor,"Copied child ownership or Camera Follow references point at the originals.");
    require(document.scene().properties().activeCamera==activeCamera && document.scene().properties().playerCharacter==controlledPlayer,"Duplicate drag replaced explicit scene roles.");
    require(document.scene().records().size()==originalRecords.size()*2,"Duplicate drag did not clone exactly one whole hierarchy.");
    for(const auto& record:originalRecords)require(sameEntity(*document.scene().find(record.id),record),"Duplicate drag changed an original entity.");
    near(*document.scene().worldTransform(copiedActor),firstDelta*initialWorld,"Copied actor did not receive the first drag sample.");
    const auto finalDelta=math::translation({-3,1,5});
    require(drag.update(document,finalDelta*proxy,true),"Second copied drag sample failed.");
    require(document.scene().records().size()==originalRecords.size()*2,"A later sample duplicated the copies again.");
    near(*document.scene().worldTransform(copiedActor),finalDelta*initialWorld,"Copied samples accumulated instead of using the initial gesture baseline.");
    require(drag.finish(document) && document.history().size()==history+1,"Duplicate and move was not a single history command.");
    const auto finalCamera=*document.scene().find(copiedCamera);
    require(document.undo() && document.scene().records().size()==originalRecords.size(),"Undo did not remove the complete copied hierarchy.");
    require(document.selection().ids==originalSelection.ids && document.selection().primary==originalSelection.primary,"Undo did not restore the original selection.");
    require(document.redo() && document.scene().contains(copiedActor) && sameEntity(*document.scene().find(copiedCamera),finalCamera),"Redo changed copied IDs or camera settings.");
    near(*document.scene().worldTransform(copiedActor),finalDelta*initialWorld,"Redo lost the final copied pose.");
}
void rotateAffineAndCancel(const std::filesystem::path& root) {
    SceneDocument document(root);
    auto affine=math::translation({8,2,-1})*math::eulerRotationXYZ({.2f,-.3f,.1f})*math::scaling({-2,3,1.5f});
    affine.at(0,1)+=.35f;
    const auto parent=place(document,"Parent",affine);
    const auto first=place(document,"Object",math::translation({2,0,1})*math::eulerRotationXYZ({.1f,.2f,.3f}),parent);
    const auto second=place(document,"Other",math::translation({-2,1,3}));
    document.select({first,second},first);
    const auto firstLocal=document.scene().find(first)->local,secondLocal=document.scene().find(second)->local;
    const auto firstWorld=*document.scene().worldTransform(first),secondWorld=*document.scene().worldTransform(second);
    const auto pivot=(math::transformPoint(firstWorld,{})+math::transformPoint(secondWorld,{}))*.5f;
    const auto proxy=math::translation(pivot);const auto history=document.history().size();const auto count=document.scene().records().size();
    const auto delta=proxy*math::eulerRotationXYZ({0,1.2f,0})* *math::inverse(proxy);
    ViewportTransformGesture drag;
    require(drag.begin(document,{first,second},proxy,ViewportTransformOperation::Rotate,true) && drag.duplicating(),"Shift rotate did not latch duplication.");
    require(drag.update(document,delta*proxy,true),"Shift rotate through an affine parent failed.");
    const auto copies=document.selection().ids;
    require(copies.size()==2 && copies[0]!=first && copies[1]!=second && document.scene().records().size()==count+2,"Shift rotate did not select exactly the new copies.");
    near(*document.scene().worldTransform(copies[0]),delta*firstWorld,"Rotated copy lost parent scale, reflection or shear.");
    near(*document.scene().worldTransform(copies[1]),delta*secondWorld,"Multi-selection rotation used a different pivot for the second copy.");
    require(document.scene().find(first)->local==firstLocal && document.scene().find(second)->local==secondLocal,"Shift rotate moved original entities.");
    drag.cancel(document);
    require(!drag.active() && !document.transactionActive() && document.scene().records().size()==count && document.history().size()==history,"Canceled Shift rotate left copies or history.");
    require(document.selection().ids==std::vector<EntityId>{first,second} && document.selection().primary==first,"Canceled Shift rotate did not restore source selection.");
    require(drag.begin(document,{first,second},proxy,ViewportTransformOperation::Rotate,true) && drag.update(document,delta*proxy,true) && drag.finish(document),"Release did not commit a duplicate rotation.");
    require(document.history().size()==history+1 && document.undo() && document.scene().records().size()==count,"Duplicate rotation Undo retained copies.");
}
void invalidation(const std::filesystem::path& root) {
    SceneDocument document(root);const auto entity=place(document,"Object",Mat4{});document.select({entity},entity);
    ViewportTransformGesture drag;const auto initial=*document.scene().find(entity);const auto history=document.history().size();
    require(!drag.begin(document,{entity},math::scaling({0,1,1}),ViewportTransformOperation::Translate,true),"A singular proxy armed a transform.");
    require(drag.begin(document,{entity},Mat4{},ViewportTransformOperation::Translate,true),"Invalid sample fixture failed to arm.");
    auto invalid=Mat4{};invalid.at(0,3)=std::numeric_limits<float>::quiet_NaN();
    require(drag.update(document,invalid,true) && !drag.editing() && sameEntity(*document.scene().find(entity),initial),"A nonfinite sample created a duplicate.");
    drag.cancel(document);require(document.history().size()==history,"Canceling a pending invalid sample added history.");
    require(drag.begin(document,{entity},Mat4{},ViewportTransformOperation::Translate,true),"Pending selection fixture failed.");
    document.select({});require(!drag.update(document,math::translation({1,0,0}),true) && document.selection().ids.empty(),"A stale pending gesture overwrote a new selection.");
    document.select({entity},entity);
    require(drag.begin(document,{entity},Mat4{},ViewportTransformOperation::Translate,true) && drag.update(document,math::translation({1,0,0}),true),"Replacement fixture did not start.");
    document.newScene();require(document.begin("Replacement edit"),"Could not start replacement document transaction.");
    require(!drag.update(document,math::translation({2,0,0}),true) && document.transactionActive(),"A stale transform canceled the replacement document's edit.");document.cancel();
    const auto next=document.createEmpty();document.select({next},next);
    require(drag.begin(document,{next},Mat4{},ViewportTransformOperation::Translate,true) && drag.update(document,math::translation({1,0,0}),true),"Transaction ownership fixture did not start.");
    document.cancel();require(document.begin("Unrelated edit"),"Could not begin an unrelated edit.");
    drag.cancel(document);require(document.transactionActive(),"A stale gesture canceled a different transaction in the same document.");document.cancel();
}
// This private CPU-only ImGui context has no SDL/native window or renderer.
// ImGuizmo supplies real handle-produced matrices to the production gesture.
class GizmoUi final {
public:
    GizmoUi(const std::filesystem::path& root,ViewportTransformOperation operation):document(root),operation_(operation) {
        context_=ImGui::CreateContext();ImGuizmo::SetImGuiContext(context_);
        auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={880,680};io.DeltaTime=1.f/60;
        io.ConfigInputTrickleEventQueue=false;
        unsigned char* pixels{};int width{},height{};io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
        io.Fonts->SetTexID(static_cast<ImTextureID>(1));
        entity=document.createEmpty("Gizmo Object");document.select({entity},entity);tick();tick();
    }
    ~GizmoUi(){gesture.cancel(document);ImGuizmo::Enable(false);ImGui::DestroyContext(context_);}
    void tick() {
        ImGui::NewFrame();ImGuizmo::BeginFrame();
        ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({870,670});
        ImGui::Begin("Viewport",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings);
        ImGui::SetCursorScreenPos({20,20});ImGui::Image(ImTextureRef(static_cast<ImTextureID>(1)),{800,600});
        if(!ImGui::IsMouseDown(ImGuiMouseButton_Left))waitForRelease_=false;
        if(gesture.active() && (ImGui::GetIO().AppFocusLost || ImGui::IsKeyPressed(ImGuiKey_Escape,false))) {
            gesture.cancel(document);waitForRelease_=true;ImGuizmo::Enable(false);
        }
        if(!gesture.active()) {
            const auto world=document.scene().worldTransform(document.selection().primary).value_or(Mat4{});
            proxy_=math::translation(math::transformPoint(world,{}));baseline_=proxy_;
        }
        ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());ImGuizmo::SetRect(20,20,800,600);
        ImGuizmo::SetOrthographic(true);ImGuizmo::Enable(!waitForRelease_&&!ImGui::GetIO().AppFocusLost);
        const auto operation=operation_==ViewportTransformOperation::Rotate?ImGuizmo::ROTATE_Z:ImGuizmo::TRANSLATE_X;
        const bool changed=ImGuizmo::Manipulate(view_.data(),projection_.data(),operation,ImGuizmo::WORLD,proxy_.data());
        hovered=ImGuizmo::IsOver(operation);
        if(ImGuizmo::IsUsing()&&!gesture.active()) {
            require(gesture.begin(document,document.scene().topmost(document.selection().ids),baseline_,operation_,ImGui::GetIO().KeyShift),"Real ImGuizmo press did not arm its production gesture.");
        }
        if(gesture.active()&&changed) {
            const auto threshold=ImGui::GetIO().MouseDragThreshold;
            require(gesture.update(document,proxy_,ImGui::GetIO().MouseDragMaxDistanceSqr[0]>=threshold*threshold),"Real ImGuizmo matrix failed to update the production gesture.");
        }
        if(gesture.active()&&!ImGuizmo::IsUsing())require(gesture.finish(document),"Real ImGuizmo release failed to finish.");
        ImGui::End();ImGui::Render();
        require(ImGui::GetCurrentContext()->ErrorCountCurrentFrame==0,"Gizmo fixture left invalid ImGui state.");
    }
    Vec2 pickHandle() {
        for(int offset=12;offset<180;++offset) {
            mouse({420.f+offset,320},false);tick();
            if(hovered)return {420.f+offset,320};
        }
        throw std::runtime_error("Could not locate the actual ImGuizmo handle in the private viewport.");
    }
    void mouse(const Vec2& value,bool down) {
        auto& io=ImGui::GetIO();io.AddMousePosEvent(value[0],value[1]);io.AddMouseButtonEvent(ImGuiMouseButton_Left,down);
    }
    void shift(bool value){ImGui::GetIO().AddKeyEvent(ImGuiMod_Shift,value);}
    SceneDocument document;ViewportTransformGesture gesture;EntityId entity{};bool hovered{};
private:
    ImGuiContext* context_{};ViewportTransformOperation operation_{};bool waitForRelease_{};
    Mat4 proxy_{},baseline_{};
    const Mat4 view_=math::lookAt({0,0,6},{0,0,0});
    const Mat4 projection_=math::orthographic(-4,4,-3,3,.1,100,ClipDepth::NegativeOneToOne);
};
void realGizmoInput(const std::filesystem::path& root) {
    {
        GizmoUi ui(root,ViewportTransformOperation::Translate);const auto point=ui.pickHandle();
        const auto initial=*ui.document.scene().find(ui.entity);const auto history=ui.document.history().size();
        ui.shift(true);ui.mouse(point,true);ui.tick();
        require(ui.gesture.active()&&!ui.gesture.editing()&&ui.document.scene().records().size()==1,"A real Shift handle click created an early copy.");
        ui.mouse(point,false);ui.tick();require(ui.document.history().size()==history&&ui.document.scene().records().size()==1,"Releasing a real Shift click duplicated an entity.");
        const Vec2 moved{point[0]+45,point[1]};
        ui.mouse(point,true);ui.tick();ui.shift(false);ui.mouse(moved,true);ui.tick();
        require(ui.document.scene().records().size()==2&&ui.document.selection().primary!=ui.entity,"Releasing Shift after the handle press stopped duplication.");
        require(sameEntity(*ui.document.scene().find(ui.entity),initial),"Real Shift+drag moved the original.");
        ui.mouse(moved,false);ui.tick();
        require(ui.document.history().size()==history+1&&ui.document.undo(),"Real Shift+drag did not create one undo command.");
        ui.tick();const auto next=ui.pickHandle();const Vec2 nextMoved{next[0]+35,next[1]};
        ui.mouse(next,true);ui.tick();ui.shift(true);ui.mouse(nextMoved,true);ui.tick();
        require(ui.document.scene().records().size()==1&&ui.document.selection().primary==ui.entity,"Pressing Shift after the handle press duplicated mid-gesture.");
        ui.mouse(nextMoved,false);ui.tick();ui.shift(false);require(ui.document.undo(),"Ordinary real drag Undo failed.");
    }
    {
        GizmoUi ui(root,ViewportTransformOperation::Rotate);const auto point=ui.pickHandle();
        const auto initial=*ui.document.scene().find(ui.entity);const auto history=ui.document.history().size();
        const auto radius=point[0]-420.f;const Vec2 rotated{420.f+radius*.8f,320.f-radius*.6f};
        ui.shift(true);ui.mouse(point,true);ui.tick();ui.mouse(rotated,true);ui.tick();
        require(ui.document.scene().records().size()==2&&ui.document.selection().primary!=ui.entity,"Real Shift+Rotate did not create a rotated copy.");
        require(sameEntity(*ui.document.scene().find(ui.entity),initial),"Real Shift+Rotate changed the original.");
        const auto copy=ui.document.selection().primary;
        require(ui.document.scene().find(copy)->local!=initial.local,"Real rotate handle left its copy unrotated.");
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape,true);ui.tick();
        require(ui.document.scene().records().size()==1&&ui.document.selection().primary==ui.entity&&!ui.document.transactionActive()&&ui.document.history().size()==history,"Escape retained real rotated copies or history.");
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape,false);ui.mouse(rotated,false);ui.tick();
        const auto again=ui.pickHandle();ui.mouse(again,true);ui.tick();ui.mouse({420.f+(again[0]-420.f)*.8f,320.f-(again[0]-420.f)*.6f},true);ui.tick();
        require(ui.document.scene().records().size()==2,"Focus-loss fixture failed to duplicate.");
        ImGui::GetIO().AddFocusEvent(false);ui.tick();
        require(ui.document.scene().records().size()==1&&!ui.document.transactionActive()&&ui.document.history().size()==history,"Application focus loss committed real rotated copies.");
    }
}
}
int main() {
    try {
        Project project;pendingAndOrdinary(project.root);duplicateHierarchy(project.root);
        rotateAffineAndCancel(project.root);invalidation(project.root);realGizmoInput(project.root);
        std::cout<<"Viewport transform gesture checks passed.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
