#include "editor/CameraPilot.h"
#include "scene/SceneDocument.h"
#include "scene/SceneExtraction.h"
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool value,const std::string& why){if(!value)throw std::runtime_error(why);}
void near(const Vec3& a,const Vec3& b,const char* why){require(math::length(a-b)<.0002,why);}
void nearMatrix(const Mat4& a,const Mat4& b,const char* why){for(unsigned i=0;i<16;++i)require(std::isfinite(a[i])&&std::abs(a[i]-b[i])<.0003f,why);}
void sameCamera(const Camera& a,const Camera& b,const char* why) {
    require(a.position==b.position&&a.target==b.target&&a.up==b.up&&a.fov==b.fov&&a.nearPlane==b.nearPlane&&a.farPlane==b.farPlane&&a.fovAxis==b.fovAxis,why);
}
struct Project {
    std::filesystem::path root=std::filesystem::temp_directory_path()/("crimson-camera-pilot-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Project(){std::filesystem::create_directories(root);}
    ~Project(){std::error_code error;const auto target=std::filesystem::weakly_canonical(root,error),parent=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);
        if(!error&&target.parent_path()==parent&&target.filename().string().starts_with("crimson-camera-pilot-"))std::filesystem::remove_all(target,error);}
};
Camera editorView(){Camera value;value.position={12,7,18};value.target={2,3,-1};value.up={.1f,1,.2f};value.fov=63;value.nearPlane=.03f;value.farPlane=1300;return value;}
void translate(Camera& view,const Vec3& delta){view.position=view.position+delta;view.target=view.target+delta;}
void selectionAndModes() {
    Project project;SceneDocument document(project.root);const auto first=document.createCamera("First"),second=document.createCamera("Second");
    auto record=*document.scene().find(second);record.visible=false;record.local=math::translation({4,5,6});require(document.updateEntity(second,record),document.status());
    CameraPilot pilot;auto view=editorView();const auto free=view;const auto history=document.history().size();const auto active=document.scene().properties().activeCamera;
    require(!pilot.selectedCamera()&&!pilot.piloting()&&!pilot.freeCamera(),"New pilot controller retained a scene camera.");
    require(pilot.selectCamera(document,first,view)&&!pilot.piloting()&&!document.transactionActive(),pilot.status());
    sameCamera(view,*resolveSceneCamera(document.scene(),first),"Selecting a camera did not resolve its scene view.");sameCamera(*pilot.freeCamera(),free,"Selecting a camera lost the free editor view.");
    const auto selected=view;translate(view,{9,0,0});require(!pilot.apply(document,view,true),"Read-only camera accepted authored navigation.");
    sameCamera(view,selected,"Read-only navigation drifted away from the scene camera.");
    require(document.history().size()==history&&document.scene().properties().activeCamera==active,"Read-only camera selection edited scene/history or active Game camera.");
    require(!pilot.selectCamera(document,999,view)&&pilot.selectedCamera()==first,"Rejected camera selection changed the current mode.");sameCamera(view,selected,"Rejected camera selection changed the view.");
    require(pilot.selectCamera(document,second,view)&&pilot.selectedCamera()==second,"Hidden camera was unavailable for explicit selection.");
    sameCamera(*pilot.freeCamera(),free,"Switching scene cameras replaced the saved free view.");
    require(pilot.setPiloting(document,view,true)&&pilot.piloting(),pilot.status());
    require(!pilot.selectCamera(document,999,view)&&pilot.piloting()&&pilot.selectedCamera()==second,"Rejected selection stopped a valid pilot session.");
    require(pilot.setPiloting(document,view,false)&&pilot.selectedCamera()==second&&!pilot.piloting(),pilot.status());
    sameCamera(view,*resolveSceneCamera(document.scene(),second),"Stopping pilot did not retain the scene camera POV.");
    pilot.useEditorCamera(document,view);sameCamera(view,free,"Editor Camera did not restore the exact saved free view.");
    require(!pilot.selectedCamera()&&!pilot.freeCamera()&&document.history().size()==history,"Returning to Editor Camera authored an edit or retained state.");
}
void gesturesAndCancellation() {
    Project project;SceneDocument document(project.root);const auto camera=document.createCamera();require(document.save("Pilot gestures"),document.status());
    CameraPilot pilot;auto view=editorView();require(pilot.selectCamera(document,camera,view)&&pilot.setPiloting(document,view,true),pilot.status());
    const auto original=*document.scene().find(camera);const auto history=document.history().size();
    require(pilot.apply(document,view,true)&&pilot.apply(document,view,false)&&!document.transactionActive()&&document.history().size()==history&&!document.dirty(),
        "Stationary navigation created a transaction or history command.");
    view.target=view.position+view.forward()*17;const auto pivotDistance=view.distance();
    require(pilot.apply(document,view,true)&&!document.transactionActive(),"Changing only orbit distance authored a pose.");
    translate(view,{1,2,3});require(pilot.apply(document,view,true)&&pilot.gestureActive()&&document.transactionActive(),pilot.status());
    const auto token=document.transactionId();require(token&&document.history().size()==history,"First gesture motion published history early.");
    translate(view,{2,0,-1});require(pilot.apply(document,view,true)&&document.transactionId()==token,pilot.status());
    const auto navigated=view;require(pilot.sync(document,view),pilot.status());sameCamera(view,navigated,"Own pilot sync reset the orbit target/pivot.");
    require(std::abs(view.distance()-pivotDistance)<.0001f,"Pilot updates changed navigation target distance.");
    require(pilot.apply(document,view,false)&&!pilot.gestureActive()&&!document.transactionActive()&&document.history().size()==history+1&&document.dirty(),
        "A completed navigation gesture did not create exactly one undo command.");
    near(resolveSceneCamera(document.scene(),camera)->position,navigated.position,"Pilot did not author the camera position.");
    require(document.save()&&!document.dirty()&&pilot.piloting(),"Idle pilot mode blocked Save or changed the mode.");
    require(document.undo()&&pilot.sync(document,view)&&!pilot.piloting(),"Undo did not end piloting safely.");
    require(sameEntity(*document.scene().find(camera),original),"One Undo did not restore the complete gesture-start camera.");
    require(document.redo()&&pilot.sync(document,view),document.status());
    require(pilot.setPiloting(document,view,true),pilot.status());const auto before=*document.scene().find(camera);const auto count=document.history().size();
    translate(view,{4,0,0});require(pilot.apply(document,view,true),pilot.status());pilot.cancelGesture(document,view);
    require(pilot.piloting()&&!pilot.gestureActive()&&!document.transactionActive()&&document.history().size()==count&&sameEntity(*document.scene().find(camera),before),
        "Cancel did not restore only the active gesture while retaining pilot mode.");
    translate(view,{0,1,0});require(pilot.apply(document,view,false)&&!document.transactionActive()&&document.history().size()==count+1,
        "One-shot/wheel navigation did not create one completed command.");
    translate(view,{0,0,-2});require(pilot.apply(document,view,true),pilot.status());const auto stopped=view;
    require(pilot.setPiloting(document,view,false)&&!document.transactionActive()&&!pilot.piloting()&&pilot.selectedCamera()==camera,
        "Stop Pilot did not finish the active gesture and retain scene-camera selection.");
    near(resolveSceneCamera(document.scene(),camera)->position,stopped.position,"Stop Pilot discarded the authored pose.");
}
Mat4 viewResidual(const Camera& view,Mat4 world) {
    auto frame=view.viewMatrix();for(unsigned axis=0;axis<3;++axis){frame[12+axis]=0;world[12+axis]=0;}return frame*world;
}
void affineParentAndFollow() {
    Project project;SceneDocument document(project.root);const auto parent=document.createEmpty("Affine parent"),target=document.createEmpty("Follow target"),camera=document.createCamera();
    auto group=*document.scene().find(parent);group.local=math::translation({3,2,-5})*math::eulerRotationXYZ({.15f,-.3f,.2f})*math::scaling({-2,3,.7f});group.local.at(0,1)+=.4f;
    require(document.updateEntity(parent,group),document.status());auto record=*document.scene().find(camera);record.parent=parent;
    record.local=math::translation({2,4,8})*math::eulerRotationXYZ({-.2f,.5f,.4f})*math::scaling({1.2f,.8f,1.5f});record.local.at(1,2)+=.25f;
    require(document.updateEntity(camera,record)&&document.setCameraFollow(camera,target),document.status());
    const auto original=*document.scene().find(camera);const auto oldWorld=*document.scene().worldTransform(camera);const auto oldView=*resolveSceneCamera(document.scene(),camera);
    const auto residual=viewResidual(oldView,oldWorld);const auto history=document.history().size();CameraPilot pilot;auto view=editorView();
    require(pilot.selectCamera(document,camera,view)&&pilot.setPiloting(document,view,true),pilot.status());
    for(unsigned frame=1;frame<=50;++frame) {
        const auto rotation=math::eulerRotationXYZ({.001f*frame,.004f*frame,-.002f*frame});
        view.position=oldView.position+Vec3{.03f*frame,.01f*frame,-.02f*frame};
        view.target=view.position+math::transformDirection(rotation,oldView.forward())*13;
        view.up=math::transformDirection(rotation,oldView.up);
        require(pilot.apply(document,view,true),pilot.status());const auto current=*document.scene().find(camera);const auto world=*document.scene().worldTransform(camera);
        require(current.camera==original.camera||current.camera->followTarget==target,"Pilot replaced the camera follow target.");
        require(current.camera->verticalFov==original.camera->verticalFov&&current.camera->nearPlane==original.camera->nearPlane&&current.camera->farPlane==original.camera->farPlane,
            "Navigation changed camera lens settings.");
        near(resolveSceneCamera(document.scene(),camera)->position,view.position,"Follow target snapped the piloted camera back.");
        nearMatrix(viewResidual(view,world),residual,"Pilot navigation accumulated affine stretch/shear/reflection drift.");
        require(math::handedness(world)==math::handedness(oldWorld)&&document.scene().find(parent)->local==group.local,
            "Pilot navigation changed the parent or reflection sign.");
    }
    require(pilot.apply(document,view,false)&&document.history().size()==history+1, "Affine navigation did not coalesce into one command.");
    const auto changed=*document.scene().find(camera);require(changed.camera->followOffset!=original.camera->followOffset,"Pilot movement did not update the authored follow offset.");
    require(document.undo()&&pilot.sync(document,view)&&sameEntity(*document.scene().find(camera),original),"Affine/follow gesture Undo lost authored residual or follow data.");
    nearMatrix(*document.scene().worldTransform(camera),oldWorld,"Affine/follow Undo changed the original world transform.");
}
void externalChangesAndTokens() {
    Project project;SceneDocument document(project.root);const auto camera=document.createCamera(),parent=document.createEmpty("Parent");CameraPilot pilot;auto view=editorView();const auto free=view;
    require(pilot.selectCamera(document,camera,view)&&pilot.setPiloting(document,view,true),pilot.status());
    auto changed=*document.scene().find(camera);changed.local=math::translation({20,5,2});changed.camera->verticalFov=71;require(document.updateEntity(camera,changed),document.status());
    translate(view,{9,0,0});const auto history=document.history().size();require(!pilot.apply(document,view,false)&&!pilot.piloting()&&sameEntity(*document.scene().find(camera),changed)&&document.history().size()==history,
        "Stale navigation overwrote a camera edit published after frame synchronization.");
    sameCamera(view,*resolveSceneCamera(document.scene(),camera),"External camera changes did not refresh the selected POV.");
    require(pilot.setPiloting(document,view,true),pilot.status());translate(view,{1,0,0});require(pilot.apply(document,view,true),pilot.status());const auto owned=document.transactionId();
    document.cancel();require(document.beginTransform("Replacement edit",{camera}),document.status());const auto replacement=document.transactionId();require(replacement>owned,"Scene transaction tokens were reused after cancellation.");
    require(document.update([&](auto& records,auto&){for(auto& record:records)if(record.id==camera)record.name="External camera name";}),document.status());
    require(pilot.sync(document,view)&&!pilot.piloting()&&document.transactionId()==replacement&&document.transactionActive(),
        "Pilot termination committed or canceled another owner's transaction.");
    require(document.scene().find(camera)->name=="External camera name"&&document.commit(),document.status());
    require(pilot.setPiloting(document,view,true),pilot.status());translate(view,{0,1,0});require(pilot.apply(document,view,true),pilot.status());
    require(document.update([&](auto& records,auto&){for(auto& record:records)if(record.id==camera)record.name="Changed during owned edit";}),document.status());
    require(pilot.sync(document,view)&&!pilot.piloting()&&!document.transactionActive()&&document.scene().find(camera)->name=="Changed during owned edit",
        "An external change within the owned edit was overwritten or left a stale transaction open.");
    auto cameraRecord=*document.scene().find(camera);cameraRecord.parent=parent;require(document.updateEntity(camera,cameraRecord),document.status());pilot.sync(document,view);
    require(pilot.setPiloting(document,view,true),pilot.status());auto group=*document.scene().find(parent);group.local=math::scaling({0,1,1});require(document.updateEntity(parent,group),document.status());
    require(pilot.sync(document,view)&&!pilot.piloting()&&!pilot.setPiloting(document,view,true)&&!document.transactionActive(),
        "A singular parent remained writable through camera piloting.");
    group.local=Mat4{};require(document.updateEntity(parent,group),document.status());pilot.sync(document,view);require(pilot.setPiloting(document,view,true),pilot.status());
    const auto before=*document.scene().find(camera);view.position[0]=std::numeric_limits<float>::quiet_NaN();
    require(!pilot.apply(document,view,true)&&!pilot.piloting()&&!document.transactionActive()&&sameEntity(*document.scene().find(camera),before),"Invalid navigation authored a camera pose or retained pilot ownership.");
    document.select({camera});require(document.eraseSelection()&&!pilot.sync(document,view)&&!pilot.selectedCamera(),document.status());sameCamera(view,free,"Deleting the selected camera did not restore the free editor camera.");
    const auto another=document.createCamera();require(pilot.selectCamera(document,another,view)&&pilot.setPiloting(document,view,true),pilot.status());document.newScene();
    require(!pilot.sync(document,view)&&!pilot.selectedCamera()&&!document.transactionActive(),"Document replacement retained a stale pilot identity.");sameCamera(view,free,"Document replacement lost the saved free editor camera.");
}
void createAtViewport() {
    Project project;SceneDocument document(project.root);auto view=editorView();constexpr float aspect=.65f;const auto history=document.history().size();
    const auto id=document.createCamera(view,aspect,"Viewport camera");require(id&&document.history().size()==history+1&&!document.transactionActive()&&document.scene().properties().activeCamera==id,document.status());
    const auto camera=*resolveSceneCamera(document.scene(),id);near(camera.position,view.position,"Create Camera lost the current viewport position.");
    nearMatrix(camera.viewMatrix(),view.viewMatrix(),"Create Camera lost viewport orientation/roll.");
    require(camera.fov==view.verticalFov(aspect)&&camera.nearPlane==view.nearPlane&&camera.farPlane==view.farPlane,"Create Camera did not convert shorter-axis framing to matching vertical FOV.");
    const auto record=*document.scene().find(id);require(document.undo()&&!document.scene().contains(id)&&document.redo()&&sameEntity(*document.scene().find(id),record),"Creating a viewport camera did not Undo/Redo as one command.");
    translate(view,{4,0,0});const auto second=document.createCamera(view,1.7f,"Another camera");require(second&&document.scene().properties().activeCamera==id,"Creating another camera replaced the active Game camera.");
    const auto count=document.history().size();const auto records=document.scene().records();
    require(!document.createCamera(view,0)&&!document.createCamera(view,-1)&&!document.createCamera(view,1,""),"Malformed camera creation was accepted.");
    auto invalid=view;invalid.target=invalid.position;require(!document.createCamera(invalid,1),"A degenerate viewport direction created a camera.");
    invalid=view;invalid.nearPlane=invalid.farPlane;require(!document.createCamera(invalid,1),"Invalid clipping planes created a camera.");
    require(document.history().size()==count&&!document.transactionActive()&&document.scene().records().size()==records.size(),"Rejected camera creation changed history, ownership or entity count.");
}
}
int main(){try{selectionAndModes();gesturesAndCancellation();affineParentAndFollow();externalChangesAndTokens();createAtViewport();
    std::cout<<"Camera selection, pilot gestures, affine/follow transforms, cancellation, identity guards and viewport creation checks passed.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
