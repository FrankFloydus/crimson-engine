#include "integration/widgets/QtDocumentProperties.h"
#include "integration/widgets/QtMaterialGraph.h"
#include "editor/MaterialDocument.h"
#include "editor/ParticleDocument.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDialog>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTimer>
#include <QTreeWidgetItemIterator>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <exception>
#include <cstdlib>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
struct Project {
    std::filesystem::path root=std::filesystem::temp_directory_path()/("crimson-qt-document-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~Project(){std::error_code error;const auto resolved=std::filesystem::weakly_canonical(root,error),temp=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);if(!error&&resolved.parent_path()==temp&&resolved.filename().string().starts_with("crimson-qt-document-"))std::filesystem::remove_all(resolved,error);}
};
void propertyTransactions(){
    Project project;MaterialDocument document(project.root);require(document.create(),"Could not create property fixture");
    QtDocumentProperties properties;
    properties.bind([&]{const auto& output=materialOutputNode(document.definition().graph).output;return nlohmann::json{{"roughness",output.roughness},{"receiveFog",output.receiveFog}};},[&](const auto& json,const std::string& label,bool final){if(!document.transactionActive())require(document.begin(label),"Could not begin native property edit");require(document.update([&](auto& asset){auto& output=materialOutputNode(asset.graph).output;output.roughness=json.at("roughness").template get<float>();output.receiveFog=json.at("receiveFog").template get<bool>();}),"Native property update failed");if(final)require(document.commit(),"Native property commit failed");},[&]{if(document.transactionActive())require(document.commit(),"Native property finish failed");},[&]{document.cancel();});
    properties.refresh(document.previewRevision());
    auto* field=properties.findChild<QDoubleSpinBox*>();require(field,"Native number control missing");
    const auto committed=document.committed();const auto revision=document.revision();
    field->setValue(.3);field->setValue(.6);
    require(document.transactionActive()&&document.committed()==committed&&document.revision()==revision,"Unfinished Qt number gesture published shared state");
    require(QMetaObject::invokeMethod(field,"editingFinished",Qt::DirectConnection),"Could not deliver native editingFinished");
    require(!document.transactionActive()&&document.canUndo(),"Qt editingFinished did not commit");
    require(document.undo()&&!document.dirty()&&!document.canUndo(),"Qt numeric gesture was split into multiple history entries");
    properties.resetProjection();properties.refresh(document.previewRevision());field=properties.findChild<QDoubleSpinBox*>();
    field->setValue(.2);QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(field,&escape);
    require(!document.transactionActive()&&!document.dirty()&&!document.canUndo(),"Escape did not restore and cancel Qt property gesture");
    properties.refresh(document.previewRevision());auto* checkbox=properties.findChild<QCheckBox*>();require(checkbox,"Native checkbox missing");checkbox->click();require(document.canUndo()&&!materialOutputNode(document.definition().graph).output.receiveFog,"Native checkbox failed to publish one history command");
    require(document.undo()&&materialOutputNode(document.definition().graph).output.receiveFog,"Checkbox undo did not restore authored value");
}
void sendMouse(QWidget& widget,QEvent::Type type,QPoint point,Qt::MouseButton button,Qt::MouseButtons buttons,Qt::KeyboardModifiers modifiers=Qt::NoModifier){QMouseEvent event(type,QPointF(point),QPointF(widget.mapToGlobal(point)),button,buttons,modifiers);QApplication::sendEvent(&widget,&event);}
QWidget* propertyWidget(QtDocumentProperties& properties,const char* path){
    for(QTreeWidgetItemIterator item(&properties);*item;++item)if((*item)->data(0,Qt::UserRole).toString()==QLatin1String(path))return properties.itemWidget(*item,1);
    throw std::runtime_error(std::string("Native property path missing: ")+path);
}
void particleAppearanceTransactions(){
    Project project;ParticleEffectDocument document(project.root);require(document.create(),"Could not create native particle fixture");
    auto seed=document.definition();seed.emitter.sizeTrack.range=ParticleAppearanceRange{.1f,.9f};
    seed.emitter.sizeTrack.curve.keys={{1,0,.2f},{2,1,.8f}};
    seed.emitter.colorGradient.stops={{1,0,{2,.25f,.125f,.4f}},{2,1,{.1f,.3f,4,.9f}}};
    saveParticleAsset(project.root,seed);require(document.load(seed.id),"Could not load retained particle appearance payload");
    const auto original=document.definition();QtDocumentProperties properties;
    properties.bind([&]{return nlohmann::json::parse(serializeParticleAsset(document.definition()));},[&](const auto& json,const std::string& label,bool final){
        const auto candidate=deserializeParticleAsset(json.dump(),document.assetId());
        if(!document.transactionActive())require(document.begin(label),"Could not begin native particle edit");
        require(document.update([&](auto& asset){asset=candidate;}),"Strict particle draft validation rejected native controls");
        if(final)require(document.commit(),"Native particle control did not commit");
    },[&]{if(document.transactionActive())require(document.commit(),"Native particle gesture finish failed");},[&]{document.cancel();});
    const auto refresh=[&]{properties.resetProjection();properties.refresh(document.previewRevision());};
    const auto mode=[&](const char* path,const char* value){
        refresh();auto* control=qobject_cast<QComboBox*>(propertyWidget(properties,path));require(control,"Native particle mode is not a combo box");
        const auto index=control->findData(QLatin1String(value));require(index>=0,"Native particle mode option missing");control->setCurrentIndex(index);
    };
    constexpr auto sizeMode="/emitters/0/appearanceTracks/size/mode";
    mode(sizeMode,"random-range");
    require(document.definition().emitter.sizeTrack.mode==ParticleAppearanceMode::RandomRange&&
        document.definition().emitter.sizeTrack.range==original.emitter.sizeTrack.range&&
        document.definition().emitter.sizeTrack.curve==original.emitter.sizeTrack.curve,"Native Random Range lost retained curve/range values");
    require(document.undo()&&document.definition()==original&&!document.canUndo(),"Native particle mode change was not one undo command");
    mode(sizeMode,"curve");const auto curveBaseline=document.definition();refresh();
    auto* curve=propertyWidget(properties,"/emitters/0/appearanceTracks/size/curve/keys");require(curve,"Native particle curve canvas missing");curve->resize(320,130);
    const auto shared=document.committed();const auto revision=document.revision();
    sendMouse(*curve,QEvent::MouseButtonPress,{8,96},Qt::LeftButton,Qt::LeftButton);
    sendMouse(*curve,QEvent::MouseMove,{70,62},Qt::NoButton,Qt::LeftButton);
    require(document.transactionActive()&&document.committed()==shared&&document.revision()==revision&&
        document.definition().emitter.sizeTrack.curve.keys.front().age>.1f,"Native curve drag failed to keep its validated preview draft isolated");
    sendMouse(*curve,QEvent::MouseButtonRelease,{70,62},Qt::LeftButton,Qt::NoButton);
    require(!document.transactionActive()&&document.revision()==revision+1,"Native curve release did not publish exactly once");
    require(document.undo()&&document.definition()==curveBaseline&&document.undo()&&document.definition()==original&&!document.canUndo(),"Native curve drag or mode change split history");
    mode("/emitters/0/appearanceTracks/color/mode","gradient");const auto gradientBaseline=document.definition();refresh();
    auto* gradient=propertyWidget(properties,"/emitters/0/appearanceTracks/color/gradient/stops");require(gradient,"Native particle gradient canvas missing");gradient->resize(320,60);
    const auto accepted=document.committed();
    sendMouse(*gradient,QEvent::MouseButtonPress,{8,51},Qt::LeftButton,Qt::LeftButton);
    sendMouse(*gradient,QEvent::MouseMove,{85,51},Qt::NoButton,Qt::LeftButton);
    require(document.transactionActive()&&document.committed()==accepted&&document.definition()!=gradientBaseline,"Native gradient drag did not own an isolated transaction");
    QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(gradient,&escape);
    require(!document.transactionActive()&&document.definition()==gradientBaseline&&document.committed()==accepted,"Escape did not exactly cancel a gradient-stop gesture");
    refresh();auto* color=qobject_cast<QDoubleSpinBox*>(propertyWidget(properties,"/emitters/0/appearanceTracks/color/gradient/stops/0/color/0"));require(color,"Native linear HDR gradient channel missing");
    color->setValue(6.5);color->setValue(7.25);require(document.transactionActive()&&document.committed()==accepted,"HDR gradient field published an unfinished gesture");
    require(QMetaObject::invokeMethod(color,"editingFinished",Qt::DirectConnection),"Could not finish native HDR gradient field");
    const auto& stop=document.definition().emitter.colorGradient.stops.front();require(stop.color[0]==7.25f&&stop.color[3]==.4f&&document.definition().emitter.sizeTrack==original.emitter.sizeTrack,"HDR gradient edit altered alpha or inactive scalar recipes");
    require(document.undo()&&document.definition()==gradientBaseline&&document.undo()&&document.definition()==original&&!document.dirty()&&!document.canUndo(),"Native gradient field or mode change was not one command each");
}
void graphConnectionsAndClipboard(){
    bool unexpectedDialog=false;QTimer popupGuard;QObject::connect(&popupGuard,&QTimer::timeout,[&]{for(auto* widget:QApplication::topLevelWidgets())if(auto* popup=qobject_cast<QDialog*>(widget);popup&&popup->isVisible()){unexpectedDialog=true;popup->reject();}});popupGuard.start(10);
    Project project;MaterialDocument document(project.root);require(document.create(),"Could not create graph fixture");MaterialNodeId scalar{},uv{},output{};
    require(document.edit("Create nodes",[&](auto& asset){scalar=addMaterialNode(asset.graph,MaterialNodeKind::Scalar,{-300,0}).id;uv=addMaterialNode(asset.graph,MaterialNodeKind::UV,{-300,240}).id;auto& out=materialOutputNode(asset.graph);output=out.id;out.position={200,0};}),"Could not prepare graph nodes");
    QtMaterialGraph view(document);view.resize(1000,800);view.refresh();view.fitInView(view.scene()->itemsBoundingRect(),Qt::KeepAspectRatio);
    const auto from=view.pinPosition({scalar,MaterialPin::Value,false}),to=view.pinPosition({output,MaterialPin::Roughness,true});require(from&&to,"Native typed pin mapping missing");
    sendMouse(*view.viewport(),QEvent::MouseButtonPress,*from,Qt::LeftButton,Qt::LeftButton);sendMouse(*view.viewport(),QEvent::MouseButtonRelease,*to,Qt::LeftButton,Qt::NoButton);
    require(!unexpectedDialog,"Pin-to-pin drag incorrectly opened the node search popup");
    auto* connected=materialInputLink(document.definition().graph,output,MaterialPin::Roughness);require(connected&&connected->fromNode==scalar,"Native pin drag did not connect scalar to Roughness");
    const auto beforeInvalid=serializeMaterialGraph(document.definition().graph);const auto wrong=view.pinPosition({uv,MaterialPin::UV,false}),sameInput=view.pinPosition({output,MaterialPin::Roughness,true});require(wrong&&sameInput,"Mapping pin missing");
    sendMouse(*view.viewport(),QEvent::MouseButtonPress,*wrong,Qt::LeftButton,Qt::LeftButton);sendMouse(*view.viewport(),QEvent::MouseButtonRelease,*sameInput,Qt::LeftButton,Qt::NoButton);
    require(serializeMaterialGraph(document.definition().graph)==beforeInvalid,"Invalid Mapping to Scalar connection mutated authored graph");
    require(!unexpectedDialog,"Invalid typed pin link incorrectly opened search instead of rejecting the connection");
    const auto input=view.pinPosition({output,MaterialPin::Roughness,true});require(bool(input),"Roughness input mapping disappeared");sendMouse(*view.viewport(),QEvent::MouseButtonPress,*input,Qt::LeftButton,Qt::LeftButton,Qt::AltModifier);sendMouse(*view.viewport(),QEvent::MouseButtonRelease,*input,Qt::LeftButton,Qt::NoButton,Qt::AltModifier);require(!materialInputLink(document.definition().graph,output,MaterialPin::Roughness),"Native Alt-click did not break the input link");require(document.undo(),"Native input disconnection was not undoable");view.refresh();
    const std::array<MaterialNodeId,1> selection{scalar};view.selectNodes(selection);require(view.selectedNode()==scalar,"Native selection lost stable node identity");
    QKeyEvent copy(QEvent::KeyPress,Qt::Key_C,Qt::ControlModifier);QApplication::sendEvent(&view,&copy);const auto count=document.definition().graph.nodes.size();QKeyEvent paste(QEvent::KeyPress,Qt::Key_V,Qt::ControlModifier);QApplication::sendEvent(&view,&paste);
    require(document.definition().graph.nodes.size()==count+1,"Native graph clipboard paste failed");require(document.undo()&&document.definition().graph.nodes.size()==count,"Pasted graph selection was not one undo command");
    view.refresh();view.selectNodes(selection);QKeyEvent preview(QEvent::KeyPress,Qt::Key_P,Qt::ControlModifier);QApplication::sendEvent(&view,&preview);require(view.previewTarget()&&view.previewTarget()->node==scalar,"Graph-scoped Ctrl+P did not pin a node output");const std::array<MaterialNodeId,1> other{uv};view.selectNodes(other);require(view.previewTarget()&&view.previewTarget()->node==scalar,"Changing node selection retargeted the explicit preview");
    view.selectNodes(selection);QKeyEvent remove(QEvent::KeyPress,Qt::Key_Delete,Qt::NoModifier);QApplication::sendEvent(&view,&remove);require(!view.previewTarget(),"Deleting the pinned node retained a stale preview target");require(document.undo(),"Deleting a material node was not undoable");view.refresh();
}
}
int main(int argc,char** argv){std::set_terminate([]{try{if(auto error=std::current_exception())std::rethrow_exception(error);}catch(const std::exception& error){std::cerr<<"Unhandled native test exception: "<<error.what()<<std::endl;}std::_Exit(86);});
    qputenv("QT_QPA_PLATFORM","minimal");qputenv("QT_FORCE_STDERR_LOGGING","1");QApplication application(argc,argv);
    try{std::cerr<<"Qt property transaction fixture"<<std::endl;propertyTransactions();std::cerr<<"Qt particle appearance fixture"<<std::endl;particleAppearanceTransactions();std::cerr<<"Qt graph fixture"<<std::endl;graphConnectionsAndClipboard();std::cout<<"Native Qt document/particle transaction, Escape, typed connection, clipboard and history checks passed.\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
