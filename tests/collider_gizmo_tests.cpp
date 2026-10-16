#include "editor/ColliderGizmo.h"
#include "editor/ColliderGizmoUi.h"
#include <imgui_internal.h>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void near(float actual,float expected,const char* message){require(std::abs(actual-expected)<.0002f,message);}
void near(const Vec3& actual,const Vec3& expected,const char* message){require(math::length(actual-expected)<.0003,message);}
struct Project {
    std::filesystem::path root=std::filesystem::temp_directory_path()/("crimson-collider-gizmo-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Project(){std::filesystem::create_directories(root);}
    ~Project(){std::error_code error;const auto path=std::filesystem::weakly_canonical(root,error);const auto temp=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);
        if(!error && path.parent_path()==temp && path.filename().string().starts_with("crimson-collider-gizmo-"))std::filesystem::remove_all(path,error);}
};
Ray atCoordinate(float coordinate,const Mat4& world=Mat4{},unsigned axis=0){
    Vec3 origin;origin[axis]=coordinate;origin[axis==2?1:2]=5;
    Vec3 direction;direction[axis==2?1:2]=-1;
    return {math::transformPoint(world,origin),math::transformDirection(world,direction)};
}
void mathematics(){
    SceneCollider collider;collider.size={2,4,6};
    const auto handles=colliderGizmoHandles(collider);require(handles.size()==6,"Box must expose six face handles.");
    for(unsigned axis=0;axis<3;++axis){near(handles[axis*2].point[axis],collider.size[axis]*.5f,"Positive Box handle is not centered on its face.");near(handles[axis*2+1].point[axis],-collider.size[axis]*.5f,"Negative Box handle is not centered on its face.");}
    auto local=math::translation({3,4,5})*math::rotation(math::axisAngle({0,1,0},.6))*math::scaling({2,3,-1});local.at(0,1)+=.2f;
    const auto parent=math::translation({-7,2,4})*math::rotation(math::axisAngle({1,0,0},.3))*math::scaling({1,2,3});
    for(unsigned id=0;id<6;++id){
        const unsigned axis=id/2;const float sign=id%2?-1.f:1.f;
        const auto edit=resizedColliderHandle(collider,local,id,sign*(collider.size[axis]*.5f+1),false);require(edit.has_value(),"Box face resize failed.");
        near(edit->collider.size[axis],collider.size[axis]+1,"Ordinary Box resize changed the wrong distance.");
        Vec3 oldFace,newFace;oldFace[axis]=-sign*collider.size[axis]*.5f;newFace[axis]=-sign*edit->collider.size[axis]*.5f;
        near(math::transformPoint(parent*local,oldFace),math::transformPoint(parent*edit->local,newFace),"Box resize moved the opposite face through its affine parent.");
        const auto centered=resizedColliderHandle(collider,local,id,sign*(collider.size[axis]*.5f+1),true);
        require(centered && centered->local==local,"Alt resize changed the Box transform.");near(centered->collider.size[axis],collider.size[axis]+2,"Alt Box resize did not affect both faces.");
    }
    near(*colliderHandleCoordinate(atCoordinate(2.5f,parent*local),parent*local,0),2.5f,"Ray handle coordinate lost affine scale, reflection or shear.");
    require(!colliderHandleCoordinate({{0,0,0},{1,0,0}},Mat4{},0),"An axis-parallel ray produced an unstable drag.");
    require(!colliderHandleCoordinate(atCoordinate(2),math::scaling({0,1,1}),0),"A singular collider transform produced a drag.");
    require(!resizedColliderHandle(collider,local,0,std::numeric_limits<float>::quiet_NaN(),false),"A nonfinite pointer edit was accepted.");
    collider.shape=ColliderShape::Sphere;require(colliderGizmoHandles(collider).size()==1,"Sphere must expose one radius handle.");
    const auto sphere=resizedColliderHandle(collider,local,0,-1,false);require(sphere && sphere->local==local,"Sphere radius changed the transform.");near(sphere->collider.radius,.001f,"Sphere radius was not clamped positive.");
    collider.shape=ColliderShape::Cylinder;collider.height=4;collider.radius=.5f;
    require(colliderGizmoHandles(collider).size()==3,"Cylinder requires two height handles and one radius handle.");
    for(unsigned id=0;id<2;++id){const float sign=id?-1.f:1.f;const auto edit=resizedColliderHandle(collider,Mat4{},id,sign*3,false);
        near(edit->collider.height,5,"Cylinder height face edit was not one-sided.");near(math::transformPoint(edit->local,{0,-sign*edit->collider.height*.5f,0}),Vec3{0,-sign*2,0},"Cylinder resize moved the opposite end.");}
    collider.shape=ColliderShape::Capsule;collider.height=2;collider.radius=.5f;
    const auto wide=resizedColliderHandle(collider,Mat4{},2,2,false);near(wide->collider.height,4,"Capsule radius growth failed to preserve valid total height.");near(wide->collider.radius,2,"Capsule radius was not updated.");
    const auto shortShape=resizedColliderHandle(collider,Mat4{},0,-1,false);near(shortShape->collider.height,.002f,"Capsule height fell below two minimum radii.");near(shortShape->collider.radius,.001f,"Capsule height shrink failed to clamp the radius.");
    const auto huge=resizedColliderHandle(collider,Mat4{},2,200000,false);near(huge->collider.radius,50000,"Capsule radius exceeded its maximum total height.");near(huge->collider.height,100000,"Capsule total height exceeded the GUI bound.");
    collider.shape=ColliderShape::Trimesh;require(colliderGizmoHandles(collider).empty() && !resizedColliderHandle(collider,Mat4{},0,2,false),"Trimesh exposed editable primitive dimensions.");
}
void transactions(){
    Project project;SceneDocument document(project.root);const auto id=document.createCollider();document.select({id},id);
    const auto initial=*document.scene().find(id);const auto history=document.history().size();ColliderGizmoDrag drag;
    require(drag.begin(document,id,0,atCoordinate(.5f)),"Box handle did not begin its scene transaction.");
    require(document.transformEditRoots()==std::vector<EntityId>{id},"Collider transform gesture did not hold its simulation dependency roots.");
    require(drag.update(document,atCoordinate(.8f),false) && drag.update(document,atCoordinate(1.2f),false),"A live collider drag failed.");
    near(document.scene().find(id)->collider->size[0],1.7f,"Live collider changes are not visible to the Inspector.");require(document.history().size()==history,"A drag created history before release.");
    require(drag.finish(document) && !document.transactionActive() && document.history().size()==history+1,"A collider gesture was not one undo command.");
    const auto resized=*document.scene().find(id);require(document.undo() && sameEntity(*document.scene().find(id),initial),"Undo did not restore collider dimensions and center.");
    require(document.redo() && sameEntity(*document.scene().find(id),resized),"Redo lost collider dimensions or center.");
    const auto world=*document.scene().worldTransform(id);const auto radius=document.scene().find(id)->collider->size[0]*.5f;
    require(drag.begin(document,id,0,atCoordinate(radius,world)) && drag.update(document,atCoordinate(radius+1,world),false),"Cancel fixture could not drag.");
    drag.cancel(document);require(!drag.active() && !document.transactionActive() && sameEntity(*document.scene().find(id),resized) && document.history().size()==history+1,"Cancel did not restore exact collider values without history.");
    require(drag.begin(document,id,0,atCoordinate(radius,world)) && drag.finish(document) && document.history().size()==history+1,"An unchanged gesture added an undo step.");
    const auto other=document.createEmpty();document.select({id,other},id);require(!drag.begin(document,id,0,atCoordinate(radius,world)),"Multi-selection unexpectedly edited one collider.");
    document.select({id},id);require(drag.begin(document,id,0,atCoordinate(radius,world)),"Document replacement fixture could not begin.");
    document.newScene();require(document.begin("New document edit"),"New document transaction did not start.");
    require(!drag.update(document,atCoordinate(2),false) && document.transactionActive(),"A stale collider drag canceled the replacement document's transaction.");document.cancel();
}
class Harness {
public:
    explicit Harness(const std::filesystem::path& root,bool perspective=false):document(root),
        vp(perspective?math::perspective(.8,800./600,.1,100,ClipDepth::NegativeOneToOne)*math::lookAt({4,3,6},{0,0,0}):
            math::orthographic(-4,4,-3,3,.1,100,ClipDepth::NegativeOneToOne)*math::lookAt({0,0,6},{0,0,0})) {
        context_=ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={860,660};io.DeltaTime=1.f/60;io.ConfigInputTrickleEventQueue=false;
        unsigned char* pixels;int width,height;io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);io.Fonts->SetTexID(static_cast<ImTextureID>(1));
        entity=document.createCollider();document.select({entity},entity);tick();tick();
    }
    ~Harness(){drag.cancel(document);ImGui::DestroyContext(context_);}
    Vec2 screen(const Vec3& point) const {const auto projected=math::project(point,vp,viewport,ClipDepth::NegativeOneToOne);require(projected.has_value(),"UI fixture projection failed.");return {(*projected)[0],(*projected)[1]};}
    void mouse(const Vec2& point,bool down){auto& io=ImGui::GetIO();io.AddMousePosEvent(point[0],point[1]);io.AddMouseButtonEvent(ImGuiMouseButton_Left,down);}
    ColliderGizmoResult tick(){
        ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({850,650});
        ImGui::Begin("Viewport",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings);
        ImGui::SetCursorScreenPos({viewport[0],viewport[1]});ImGui::Image(ImTextureRef(static_cast<ImTextureID>(1)),{viewport[2],viewport[3]});
        const bool hovered=ImGui::IsItemHovered();
        const auto result=drawColliderGizmo(*ImGui::GetWindowDrawList(),document,drag,vp,viewport,visible,hovered && !blocked,ImGui::IsWindowFocused(),
            blocked || ImGui::IsKeyPressed(ImGuiKey_Escape,false));
        ImGui::End();ImGui::Render();require(ImGui::GetCurrentContext()->ErrorCountCurrentFrame==0,"Collider overlay left invalid ImGui layout or item state.");return result;
    }
    SceneDocument document;ColliderGizmoDrag drag;EntityId entity{};bool visible{true},blocked{};
    const Vec4 viewport{20,20,800,600};
    // Exact numerical UI assertions use a 100-pixels/metre grid. ImGui floors
    // mouse events to whole pixels; perspective is checked in screen space below.
    const Mat4 vp;
private:ImGuiContext* context_{};
};
void uiGestures(){
    Project project;Harness ui(project.root);const auto original=*ui.document.scene().find(ui.entity);const auto history=ui.document.history().size();
    const auto handle=ui.screen({.5f,0,0});ui.mouse(handle,true);require(ui.tick().started && ui.drag.active(),"The production ImGui handle did not claim its click.");
    ui.mouse(ui.screen({1.2f,0,0}),true);ui.tick();near(ui.document.scene().find(ui.entity)->collider->size[0],1.7f,"Private ImGui dragging did not update the collider Inspector value.");
    ui.mouse(ui.screen({1.2f,0,0}),false);ui.tick();require(!ui.drag.active() && ui.document.history().size()==history+1,"Private ImGui release did not commit one gesture.");
    require(ui.document.undo() && sameEntity(*ui.document.scene().find(ui.entity),original),"ImGui handle undo did not restore dimensions and local matrix.");
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Alt,true);ui.mouse(handle,true);require(ui.tick().started,"Alt handle fixture did not begin.");
    ui.mouse(ui.screen({1,0,0}),true);ui.tick();near(ui.document.scene().find(ui.entity)->collider->size[0],2,"ImGui Alt modifier did not resize from the center.");
    require(ui.document.scene().find(ui.entity)->local==original.local,"ImGui Alt modifier moved the collider center.");
    ui.mouse(ui.screen({1,0,0}),false);ui.tick();require(ui.document.undo(),"Alt gesture did not create an undo command.");
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Alt,false);ui.tick();
    ui.mouse(handle,true);ui.tick();ui.mouse(ui.screen({1,0,0}),true);ui.tick();ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape,true);
    require(ui.tick().canceled && sameEntity(*ui.document.scene().find(ui.entity),original) && !ui.document.transactionActive(),"Escape did not roll back the private ImGui gesture.");
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape,false);ui.mouse(handle,false);ui.tick();
    ui.mouse(handle,true);ui.tick();ui.mouse(ui.screen({1,0,0}),true);ui.tick();ui.visible=false;
    require(ui.tick().canceled && sameEntity(*ui.document.scene().find(ui.entity),original),"Hiding collision handles did not roll back the edit.");
    ui.visible=true;ui.mouse(handle,false);ui.tick();ui.mouse(handle,true);ui.tick();ui.mouse(ui.screen({1,0,0}),true);ui.tick();ImGui::GetIO().AddFocusEvent(false);
    require(ui.tick().canceled && sameEntity(*ui.document.scene().find(ui.entity),original),"Application focus loss committed an unfinished handle edit.");
    ImGui::GetIO().AddFocusEvent(true);ui.mouse(handle,false);ui.tick();ui.blocked=true;ui.mouse(handle,true);
    require(!ui.tick().started && !ui.document.transactionActive(),"Navigation or another input owner failed to block a collider handle.");
    ui.mouse(handle,false);ui.blocked=false;ui.tick();
    for(const auto shape:{ColliderShape::Sphere,ColliderShape::Cylinder,ColliderShape::Capsule}) {
        auto value=*ui.document.scene().find(ui.entity);value.collider->shape=shape;value.collider->radius=.5f;value.collider->height=1;
        require(ui.document.updateEntity(ui.entity,value),"Radius UI fixture failed to change its shape.");ui.tick();
        ui.mouse(handle,true);require(ui.tick().started,"A primitive radius handle failed to claim its ImGui click.");
        ui.mouse(ui.screen({1.25f,0,0}),true);ui.tick();ui.mouse(ui.screen({1.25f,0,0}),false);ui.tick();
        near(ui.document.scene().find(ui.entity)->collider->radius,1.25f,"Primitive radius handle did not update its authored value.");
        require(ui.document.scene().find(ui.entity)->local==value.local,"Radius handle moved its collider transform.");
        if(shape==ColliderShape::Capsule)near(ui.document.scene().find(ui.entity)->collider->height,2.5f,"Capsule radius UI did not grow its total height.");
    }
    auto mesh=*ui.document.scene().find(ui.entity);mesh.collider->shape=ColliderShape::Trimesh;require(ui.document.updateEntity(ui.entity,mesh),"Trimesh UI fixture failed.");
    ui.tick();ui.mouse(handle,true);require(!ui.tick().started && !ui.document.transactionActive(),"Trimesh exposed a primitive resize handle.");
}
void perspectiveGesture(){
    Project project;Harness ui(project.root,true);const auto history=ui.document.history().size();
    ui.mouse(ui.screen({.5f,0,0}),true);require(ui.tick().started,"Perspective radius-face handle did not claim its click.");
    const auto target=ui.screen({1.2f,0,0});ui.mouse(target,true);ui.tick();
    const auto current=ui.document.scene().find(ui.entity);const auto world=ui.document.scene().worldTransform(ui.entity);
    const auto projected=ui.screen(math::transformPoint(*world,{current->collider->size[0]*.5f,0,0}));
    require(std::hypot(projected[0]-std::floor(target[0]),projected[1]-std::floor(target[1]))<2.f,
        "Perspective collider handle did not follow the actual quantized cursor.");
    require(ui.document.transactionActive() && ui.document.history().size()==history,"Perspective dragging committed before release.");
    ui.mouse(target,false);ui.tick();require(!ui.document.transactionActive() && ui.document.history().size()==history+1,"Perspective release did not commit one undo command.");
}
}
int main(){try{mathematics();transactions();uiGestures();perspectiveGesture();std::cout<<"Collider handle math, scene transactions and private ImGui gestures passed.\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
