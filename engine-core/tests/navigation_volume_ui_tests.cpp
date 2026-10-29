#include "editor/NavigationVolumeGizmo.h"
#include "scene/SceneExtraction.h"
#include <imgui_internal.h>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void near(float a,float b,const char* message){require(std::abs(a-b)<.001f,message);}
struct Project {
    std::filesystem::path root=std::filesystem::temp_directory_path()/("crimson-nav-volume-ui-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Project(){std::filesystem::create_directories(root);}
    ~Project(){std::error_code error;const auto path=std::filesystem::weakly_canonical(root,error),temp=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);
        if(!error && path.parent_path()==temp && path.filename().string().starts_with("crimson-nav-volume-ui-"))std::filesystem::remove_all(path,error);}
};
Ray atCoordinate(float coordinate,const Mat4& world=Mat4{},unsigned axis=0) {
    Vec3 origin,direction;origin[axis]=coordinate;origin[axis==2?1:2]=5;direction[axis==2?1:2]=-1;
    return {math::transformPoint(world,origin),math::transformDirection(world,direction)};
}
void affineTransactions(const std::filesystem::path& root) {
    SceneDocument document(root);const auto parent=document.createEmpty();auto group=*document.scene().find(parent);
    group.local=math::translation({2,3,4})*math::rotation(math::axisAngle({0,1,0},.5))*math::scaling({2,1,3});require(document.updateEntity(parent,group),"Parent transform failed.");
    const auto id=document.createNavigationVolume("NavigationVolume",parent);document.select({id},id);
    auto initial=*document.scene().find(id);initial.navigationVolume->size={2,4,6};require(document.updateEntity(id,initial),"Volume dimensions fixture failed.");
    require(!initial.collider,"NavigationVolume unexpectedly creates physical collision.");
    NavigationVolumeGizmoDrag drag;
    for(unsigned handle=0;handle<6;++handle) {
        const auto before=*document.scene().find(id);const auto world=*document.scene().worldTransform(id);const auto history=document.history().size();
        const unsigned axis=handle/2;const float sign=handle%2?-1.f:1.f,face=sign*before.navigationVolume->size[axis]*.5f;
        require(drag.begin(document,id,handle,atCoordinate(face,world,axis)),"A volume face could not begin dragging.");
        require(drag.update(document,atCoordinate(face+sign,world,axis),false),"A volume face could not resize.");
        const auto resized=*document.scene().find(id);near(resized.navigationVolume->size[axis],before.navigationVolume->size[axis]+1,"Ordinary resize changed the wrong dimension.");
        Vec3 oldFace,newFace;oldFace[axis]=-face;newFace[axis]=-sign*resized.navigationVolume->size[axis]*.5f;
        require(math::length(math::transformPoint(world,oldFace)-math::transformPoint(*document.scene().worldTransform(id),newFace))<.001,"Resize moved the opposite face under affine parenting.");
        require(document.history().size()==history && drag.finish(document) && document.history().size()==history+1,"Volume resize was not one undo gesture.");
        require(document.undo() && sameEntity(*document.scene().find(id),before),"Undo lost exact volume dimensions or transform.");
        require(drag.begin(document,id,handle,atCoordinate(face,world,axis)) && drag.update(document,atCoordinate(face+sign,world,axis),true),"Alt resize failed.");
        near(document.scene().find(id)->navigationVolume->size[axis],before.navigationVolume->size[axis]+2,"Alt resize did not move both faces.");
        require(document.scene().find(id)->local==before.local,"Alt resize moved the center.");drag.cancel(document);
        require(sameEntity(*document.scene().find(id),before),"Cancel did not restore the authored bounds.");
        require(document.redo(),"The canceled gesture discarded the previously undone resize.");
    }
    const auto bounds=selectionBounds(document.scene(),std::vector<EntityId>{id},[](const auto&)->std::optional<Aabb>{return {};});
    require(bounds.valid && math::length(bounds.maximum-bounds.minimum)>6,"Selection framing ignored volume dimensions.");
    require(document.createNavigationVolume()==id,"A second active volume was created.");
}
class Harness {
public:
    explicit Harness(const std::filesystem::path& root):document(root) {
        context=ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={860,660};io.DeltaTime=1.f/60;io.ConfigInputTrickleEventQueue=false;
        unsigned char* pixels;int width,height;io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);io.Fonts->SetTexID(static_cast<ImTextureID>(1));
        id=document.createNavigationVolume();auto volume=*document.scene().find(id);volume.navigationVolume->size={1,1,1};document.updateEntity(id,volume);document.select({id},id);tick();tick();
    }
    ~Harness(){drag.cancel(document);ImGui::DestroyContext(context);}
    Vec2 screen(Vec3 point)const{const auto p=math::project(point,vp,viewport,ClipDepth::NegativeOneToOne);return {(*p)[0],(*p)[1]};}
    void mouse(Vec3 point,bool down){const auto p=screen(point);auto& io=ImGui::GetIO();io.AddMousePosEvent(p[0],p[1]);io.AddMouseButtonEvent(ImGuiMouseButton_Left,down);}
    NavigationVolumeGizmoResult tick() {
        ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({850,650});ImGui::Begin("Viewport",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings);
        ImGui::SetCursorScreenPos({viewport[0],viewport[1]});ImGui::Image(ImTextureRef(static_cast<ImTextureID>(1)),{viewport[2],viewport[3]});
        const auto result=drawNavigationVolumeGizmo(*ImGui::GetWindowDrawList(),document,drag,vp,viewport,ImGui::IsItemHovered() && !blocked,ImGui::IsWindowFocused(),blocked || ImGui::IsKeyPressed(ImGuiKey_Escape,false));
        ImGui::End();ImGui::Render();require(ImGui::GetCurrentContext()->ErrorCountCurrentFrame==0,"Volume handles left invalid ImGui item state.");return result;
    }
    SceneDocument document;NavigationVolumeGizmoDrag drag;EntityId id{};bool blocked{};
    const Vec4 viewport{20,20,800,600};const Mat4 vp=math::orthographic(-4,4,-3,3,.1,100,ClipDepth::NegativeOneToOne)*math::lookAt({0,0,6},{0,0,0});
private:ImGuiContext* context{};
};
void privateUi(const std::filesystem::path& root) {
    Harness ui(root);const auto original=*ui.document.scene().find(ui.id);const auto history=ui.document.history().size();
    ui.mouse({.5f,0,0},true);require(ui.tick().started,"NavigationVolume handle did not claim its click.");ui.mouse({1.2f,0,0},true);ui.tick();
    near(ui.document.scene().find(ui.id)->navigationVolume->size[0],1.7f,"Live ImGui volume drag did not update authored bounds.");
    ui.mouse({1.2f,0,0},false);ui.tick();require(!ui.drag.active() && ui.document.history().size()==history+1,"Release did not commit one resize.");
    require(ui.document.undo() && sameEntity(*ui.document.scene().find(ui.id),original),"Undo failed after ImGui resizing.");
    ui.mouse({.5f,0,0},true);ui.tick();ui.mouse({1,0,0},true);ui.tick();ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape,true);
    require(ui.tick().canceled && sameEntity(*ui.document.scene().find(ui.id),original),"Escape did not cancel the navigation resize.");
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape,false);ui.mouse({.5f,0,0},false);ui.tick();ui.blocked=true;ui.mouse({.5f,0,0},true);
    require(!ui.tick().started && !ui.document.transactionActive(),"A competing input owner failed to block volume handles.");
}
}
int main(){try{Project project;affineTransactions(project.root);privateUi(project.root);std::cout<<"NavigationVolume affine face resizing, undo, cancellation, framing and private ImGui input passed.\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
