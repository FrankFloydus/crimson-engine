#include "integration/widgets/QtInspector.h"
#include "assets/AssetLibrary.h"
#include "crimson-renderer/Renderer.h"
#include "scene/SceneDocument.h"
#include <QApplication>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QScrollArea>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void inspectorAuthoring() {
    QTemporaryDir temporary;require(temporary.isValid(),"Cannot create isolated Inspector storage.");
    const std::filesystem::path root(temporary.path().toStdWString());AssetLibrary assets(root);
    // Inspector projection uses CPU metadata and never initializes a device.
    Renderer renderer;SceneDocument document(root);
    const auto spot=document.createSpotLight("Inspector spot");const auto player=document.createPlayer("Inspector Player");
    const auto model=document.createBox("Inspector box");const auto environment=document.createEnvironment("Inspector environment");
    QtInspector inspector;inspector.setCollisionLayerNames(defaultCollisionLayerNames());
    const auto select=[&](EntityId id){document.select({id});inspector.synchronize(document,assets,renderer);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);};
    const auto field=[&](const char* path)->QWidget*{
        auto* scroll=inspector.findChild<QScrollArea*>();require(scroll&&scroll->widget(),"The Inspector projection has no current body.");
        auto* result=scroll->widget()->findChild<QWidget*>(QStringLiteral("SceneField")+QLatin1String(path));require(result,"An Engine authored field is missing from the standalone Inspector.");return result;
    };
    select(spot);const auto original=*document.scene().find(spot);const auto history=document.history().size();
    auto* angle=field("/spot/outerAngle")->findChild<QDoubleSpinBox*>();require(angle,"Spot outer cone has no numeric control.");
    angle->setValue(95);require(!document.transactionActive()&&document.scene().find(spot)->spot->outerAngle==original.spot->outerAngle,"Invalid cone edits entered scene history.");
    angle->setValue(40);require(document.transactionActive()&&document.history().size()==history,"The Inspector published an unfinished numeric gesture.");
    QMetaObject::invokeMethod(angle,"editingFinished",Qt::DirectConnection);require(!document.transactionActive()&&document.history().canUndo(),"Inspector release did not commit its one transaction.");
    require(document.undo()&&document.scene().find(spot)->spot->outerAngle==original.spot->outerAngle,"Inspector cone Undo did not restore the original value.");
    select(spot);field("/spot/flicker/enabled");field("/spot/temperatureKelvin");
    select(player);field("/movement/stairSmoothingTime");field("/playerInput/traceTest/hitImpulse");field("/controller/profile");
    const auto camera=document.scene().properties().activeCamera;const auto cameraBefore=*document.scene().find(camera);const auto playerBefore=*document.scene().find(player);
    auto* eye=field("/cameraFollow/offset")->findChild<QDoubleSpinBox*>();require(eye,"The Player's real child camera is absent from inline authoring.");
    eye->setValue(1.3);require(document.transactionActive()&&document.scene().find(player)->local==playerBefore.local,"Inline eye authoring changed the Actor root.");
    inspector.cancel();require(!document.transactionActive()&&document.scene().find(camera)->local==cameraBefore.local&&document.scene().find(camera)->cameraFollow==cameraBefore.cameraFollow,"Inline eye cancellation lost its authored camera.");
    select(model);field("/modelRagdoll/asset");field("/modelRagdoll/startSimulated");field("/modelAnimation/asset");field("/modelMaterials/0");
    select(environment);field("/environment/environment/ssr/enabled");field("/environment/environment/specularAA");
    for(unsigned i=0;i<12;++i)select(i%2?spot:player);
    inspector.detach();require(!document.transactionActive(),"Detaching the level left an Inspector gesture alive.");
    document.newScene();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    require(!renderer.initialized(),"Inspector authoring unexpectedly initialized a GPU device.");
}
}
int main(int argc,char** argv){qputenv("QT_QPA_PLATFORM","minimal");QApplication application(argc,argv);try{inspectorAuthoring();std::cout<<"Standalone Engine Inspector field coverage, strict edits, history, inline camera and detach ownership passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
