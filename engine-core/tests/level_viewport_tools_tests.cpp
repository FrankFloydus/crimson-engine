#include "integration/LevelViewportTools.h"
#include "assets/AssetLibrary.h"
#include "editor/ColliderGizmo.h"
#include "editor/NavigationVolumeGizmo.h"
#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <numbers>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(float actual,float expected,const char* message){require(std::abs(actual-expected)<.003f,message);}
struct Fixture {
    std::filesystem::path root=std::filesystem::temp_directory_path()/("crimson-qt-tools-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    SceneDocument document{root};AssetLibrary assets{root};Camera camera;
    QWidget viewport;integration::LevelViewportTools tools{viewport};
    Fixture(){std::filesystem::create_directories(root);viewport.setAttribute(Qt::WA_DontShowOnScreen);viewport.resize(800,600);camera.position={0,0,6};camera.target={0,0,0};tools.bind(&document,&assets,&camera);}
    ~Fixture(){tools.bind(nullptr,nullptr,nullptr);std::error_code error;const auto path=std::filesystem::weakly_canonical(root,error);const auto temp=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);if(!error&&path.parent_path()==temp&&path.filename().string().starts_with("crimson-qt-tools-"))std::filesystem::remove_all(path,error);}
    QPointF screen(Vec3 world){const auto point=math::project(world,camera.projectionMatrix(800.f/600,ClipDepth::ZeroToOne)*camera.viewMatrix(),{0,0,800,600},ClipDepth::ZeroToOne);require(bool(point),"Could not project native Qt handle fixture.");return {(*point)[0],(*point)[1]};}
    void mouse(QEvent::Type type,Vec3 world,Qt::KeyboardModifiers modifiers={}){const auto point=screen(world);const auto button=type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton;const auto held=type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton;QMouseEvent event(type,point,point,button,held,modifiers);QApplication::sendEvent(&viewport,&event);}
    void escape(){QKeyEvent event(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(&viewport,&event);}
};
void nativeTransforms(){Fixture f;const auto id=f.document.createEmpty();f.document.select({id});f.tools.synchronize();const auto history=f.document.history().size();const auto initial=*f.document.scene().find(id);
    f.mouse(QEvent::MouseButtonPress,{.4f,0,0});require(f.tools.active()&&!f.document.transactionActive(),"Qt axis press did not retain a pending transform gesture.");
    f.mouse(QEvent::MouseMove,{1.4f,0,0});near(f.document.scene().find(id)->local.at(0,3),1,"Qt axis drag lost its world-space ray coordinate.");require(f.document.history().size()==history,"Qt transform published history before release.");
    f.mouse(QEvent::MouseButtonRelease,{1.4f,0,0});require(!f.tools.active()&&f.document.history().size()==history+1,"Qt release did not commit exactly one scene transform.");require(f.document.undo()&&sameEntity(*f.document.scene().find(id),initial),"Qt transform undo did not restore exact authored data.");
    f.tools.synchronize();f.mouse(QEvent::MouseButtonPress,{.4f,0,0});f.mouse(QEvent::MouseMove,{1.1f,0,0});f.escape();require(!f.tools.active()&&!f.document.transactionActive()&&sameEntity(*f.document.scene().find(id),initial),"Qt Escape did not cancel the complete transform baseline.");
    f.tools.synchronize();f.mouse(QEvent::MouseButtonPress,{.4f,0,0},Qt::ShiftModifier);f.mouse(QEvent::MouseMove,{1.4f,0,0},Qt::ShiftModifier);f.mouse(QEvent::MouseButtonRelease,{1.4f,0,0},Qt::ShiftModifier);require(f.document.scene().records().size()==2,"Qt Shift Translate did not duplicate through the canonical transaction.");require(f.document.undo()&&f.document.scene().records().size()==1,"Duplicate-transform undo retained its temporary entity.");
    f.tools.synchronize();f.mouse(QEvent::MouseButtonPress,{.4f,0,0});f.mouse(QEvent::MouseMove,{1.3f,0,0});QEvent loss(QEvent::FocusOut);QApplication::sendEvent(&f.viewport,&loss);require(!f.tools.active()&&!f.document.transactionActive()&&sameEntity(*f.document.scene().find(id),initial),"Native focus loss committed an unfinished Qt handle drag.");
}
void nativeShapeHandles(){Fixture f;const auto id=f.document.createCollider();f.document.select({id});f.tools.synchronize();const auto initial=*f.document.scene().find(id);const auto history=f.document.history().size();
    f.mouse(QEvent::MouseButtonPress,{.5f,0,0});f.mouse(QEvent::MouseMove,{1.2f,0,0});f.mouse(QEvent::MouseButtonRelease,{1.2f,0,0});near(f.document.scene().find(id)->collider->size[0],1.7f,"Qt Collider face handle did not preserve its opposite face.");require(f.document.history().size()==history+1&&f.document.undo(),"Collider Qt release did not own one undo command.");
    f.tools.synchronize();f.mouse(QEvent::MouseButtonPress,{.5f,0,0},Qt::AltModifier);f.mouse(QEvent::MouseMove,{1,0,0},Qt::AltModifier);f.mouse(QEvent::MouseButtonRelease,{1,0,0},Qt::AltModifier);near(f.document.scene().find(id)->collider->size[0],2,"Qt Alt Collider drag did not resize around the original center.");require(f.document.scene().find(id)->local==initial.local,"Qt Alt Collider drag moved the original center.");
    require(f.document.undo(),"Could not restore native shape fixture.");f.tools.synchronize();f.mouse(QEvent::MouseButtonPress,{.5f,0,0});f.mouse(QEvent::MouseMove,{1.2f,0,0});f.tools.bind(nullptr,nullptr,nullptr);require(!f.document.transactionActive()&&sameEntity(*f.document.scene().find(id),initial),"Replacing a Qt viewport binding retained its previous document gesture.");
}
void nativeRotateScaleAndVolume(){
    {Fixture f;const auto id=f.document.createEmpty();f.document.select({id});const float length=6*std::tan(f.camera.verticalFov(800.f/600)*std::numbers::pi_v<float>/360)*160/600;
        f.tools.setOperation(ViewportTransformOperation::Rotate);const float component=length/std::sqrt(2.f);
        f.mouse(QEvent::MouseButtonPress,{component,component,0},Qt::ShiftModifier);require(f.tools.active(),"Qt rotation ring did not own its click.");f.mouse(QEvent::MouseMove,{-component,component,0},Qt::ShiftModifier);f.mouse(QEvent::MouseButtonRelease,{-component,component,0},Qt::ShiftModifier);near(f.document.scene().find(id)->local.at(0,0),0,"Qt Shift rotation did not preserve the snapped quarter turn.");near(f.document.scene().find(id)->local.at(1,0),1,"Qt rotation reversed the selected world axis.");require(f.document.undo(),"Qt rotation did not create a complete undo command.");
        f.tools.setOperation(ViewportTransformOperation::Scale);f.mouse(QEvent::MouseButtonPress,{.4f,0,0});f.mouse(QEvent::MouseMove,{.4f+length,0,0});f.mouse(QEvent::MouseButtonRelease,{.4f+length,0,0});near(f.document.scene().find(id)->local.at(0,0),2,"Qt scale handle did not apply its axis-relative factor.");near(f.document.scene().find(id)->local.at(1,1),1,"Qt single-axis scale changed a different axis.");}
    {Fixture f;const auto id=f.document.createNavigationVolume();auto record=*f.document.scene().find(id);record.navigationVolume->size={1,1,1};record.local=Mat4{};require(f.document.updateEntity(id,record),"Could not size native Navigation Volume fixture.");f.document.select({id});f.tools.synchronize();const auto history=f.document.history().size();f.mouse(QEvent::MouseButtonPress,{.5f,0,0});require(f.tools.active(),"Native Navigation Volume face did not own its click.");f.mouse(QEvent::MouseMove,{1.2f,0,0});f.mouse(QEvent::MouseButtonRelease,{1.2f,0,0});near(f.document.scene().find(id)->navigationVolume->size[0],1.7f,"Native Navigation Volume sizing did not use its own capability.");require(!f.document.scene().find(id)->collider&&f.document.history().size()==history+1,"Navigation Volume Qt drag created a Collider or multiple history entries.");require(f.document.undo()&&sameEntity(*f.document.scene().find(id),record),"Navigation Volume Qt undo lost exact dimensions/placement.");}
}
void exactOwnerCancellation(){Fixture f;const auto id=f.document.createCollider();f.document.select({id});ColliderGizmoDrag drag;const Ray ray{{.5f,0,6},{0,0,-1}};require(drag.begin(f.document,id,0,ray),"Collider ownership fixture did not begin.");f.document.cancel();require(f.document.begin("Unrelated replacement edit"),"Could not open the replacement transaction.");drag.cancel(f.document);require(f.document.transactionActive(),"A stale Collider owner canceled a different transaction in the same document.");f.document.cancel();}
}
int main(int argc,char** argv){std::cerr<<"Qt tools: application startup\n";QApplication app(argc,argv);try{std::cerr<<"Qt tools: transform ownership\n";nativeTransforms();std::cerr<<"Qt tools: collider handles\n";nativeShapeHandles();std::cerr<<"Qt tools: rotation, scale and navigation bounds\n";nativeRotateScaleAndVolume();std::cerr<<"Qt tools: stale transaction\n";exactOwnerCancellation();std::cout<<"Native Qt scene transform, duplicate, shape, focus and document ownership checks passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
