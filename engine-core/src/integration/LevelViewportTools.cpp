#include "LevelViewportTools.h"
#include "assets/AssetLibrary.h"
#include "editor/ColliderGizmo.h"
#include "editor/NavigationVolumeGizmo.h"
#include "editor/CollisionWire.h"
#include "editor/DecalAuthoring.h"
#include "editor/ViewportRotationSnap.h"
#include "editor/ViewportSelection.h"
#include "scene/SceneExtraction.h"
#include "ui/qt_semantic_axis_colors.hpp"
#include <QApplication>
#include <QCursor>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace crimson::integration {
namespace {
Vec3 axisVector(unsigned axis){Vec3 value;value[axis]=1;return value;}
QColor axisColor(unsigned axis){return quader::qt_semantic_axis_color(static_cast<quader_editor_core::ViewAxis>(axis));}
float segmentDistance(QPointF point,QPointF a,QPointF b){const auto d=b-a;const auto length=d.x()*d.x()+d.y()*d.y();const auto t=length>0?std::clamp(QPointF::dotProduct(point-a,d)/length,0.,1.):0.;return float(std::hypot(point.x()-a.x()-t*d.x(),point.y()-a.y()-t*d.y()));}
struct Line{QPointF a,b;QColor color;float width{1.5f};};
}
struct LevelViewportTools::Impl {
    LevelViewportTools& overlay;QWidget& viewport;std::function<void()> request;
    SceneDocument* document{};AssetLibrary* assets{};Camera* camera{};
    ViewportTransformGesture transform;ColliderGizmoDrag collider;NavigationVolumeGizmoDrag volume;
    ViewportTransformOperation operation{ViewportTransformOperation::Translate};
    Mat4 proxy,viewProjection,gestureView;Vec4 rectangle;
    Vec3 initialPlaneVector;QPointF press,pointer;
    unsigned axis{},hoverAxis{~0u},hoverShape{~0u};EntityId hoverEntity{},marker{};
    float initialCoordinate{},axisLength{1};bool blocked{},shapeIsVolume{};
    std::vector<Line> lines;
    Impl(LevelViewportTools& owner,QWidget& source,std::function<void()> wake):overlay(owner),viewport(source),request(std::move(wake)){}
    bool active()const{return transform.active()||collider.active()||volume.active();}
    void cancel(){if(document){transform.cancel(*document);collider.cancel(*document);volume.cancel(*document);}if(QWidget::mouseGrabber()==&viewport)viewport.releaseMouse();}
    void finish(){if(document){if(transform.active())transform.finish(*document);if(collider.active())collider.finish(*document);if(volume.active())volume.finish(*document);}if(QWidget::mouseGrabber()==&viewport)viewport.releaseMouse();}
    void changed(){overlay.update();if(request)request();}
    std::optional<Ray> ray()const{return math::rayFromScreen(float(pointer.x()),float(pointer.y()),viewProjection,rectangle,ClipDepth::ZeroToOne);}
    void move(Qt::KeyboardModifiers modifiers){
        if(!document||!camera)return;const auto pickRay=ray();if(!pickRay)return;
        if(transform.active()){
            auto updated=proxy;const auto pivot=math::transformPoint(proxy,{});const auto direction=math::normalized(math::transformDirection(proxy,axisVector(axis)));
            if(operation==ViewportTransformOperation::Rotate){if(const auto point=math::intersectPlane(*pickRay,pivot,direction)){const auto vector=math::normalized(*point-pivot);double angle=std::atan2(math::dot(direction,math::cross(initialPlaneVector,vector)),math::dot(initialPlaneVector,vector));if(modifiers.testFlag(Qt::ShiftModifier)){const auto step=DefaultRotationSnapDegrees*std::numbers::pi/180.;angle=std::round(angle/step)*step;}updated=proxy*math::rotation(math::axisAngle(axisVector(axis),angle));}}
            else if(const auto coordinate=colliderHandleCoordinate(*pickRay,proxy,axis)){const float delta=*coordinate-initialCoordinate;if(operation==ViewportTransformOperation::Translate)updated=math::translation(direction*delta)*proxy;else{Vec3 scale{1,1,1};scale[axis]=std::max(.001f,1+delta/axisLength);updated=proxy*math::scaling(scale);}}
            transform.update(*document,updated,std::hypot(pointer.x()-press.x(),pointer.y()-press.y())>=QApplication::startDragDistance());
        }
        if(collider.active())collider.update(*document,*pickRay,modifiers.testFlag(Qt::AltModifier));
        if(volume.active())volume.update(*document,*pickRay,modifiers.testFlag(Qt::AltModifier));
    }
    void rebuild(){
        lines.clear();hoverAxis=hoverShape=~0u;hoverEntity=marker=NoEntity;
        if(!document||!assets||!camera||viewport.width()<=0||viewport.height()<=0)return;
        rectangle={0,0,float(viewport.width()),float(viewport.height())};viewProjection=camera->projectionMatrix(rectangle[2]/rectangle[3],ClipDepth::ZeroToOne)*camera->viewMatrix();
        if(active()&&(blocked||gestureView!=camera->viewMatrix()||(transform.active()&&!transform.valid(*document))||(collider.active()&&!collider.valid(*document))||(volume.active()&&!volume.valid(*document))))cancel();
        const auto addLine=[&](Vec3 a,Vec3 b,QColor color,float width=1.5f){if(const auto line=projectClippedSegment(a,b,viewProjection,rectangle,ClipDepth::ZeroToOne))lines.push_back({{(*line)[0][0],(*line)[0][1]},{(*line)[1][0],(*line)[1][1]},color,width});};
        const auto modelBounds=[&](const std::string& id)->std::optional<Aabb>{const auto model=assets->findModel(id);return model?std::optional{model->bounds}:std::nullopt;};
        std::vector<EntityId> selected;for(const auto id:document->selection().ids)if(document->scene().isVisible(id))selected.push_back(id);
        const auto bounds=selectionBounds(document->scene(),selected,modelBounds);
        Vec3 pivot=bounds.valid?(bounds.minimum+bounds.maximum)*.5f:Vec3{};
        if(selected.size()==1)if(const auto world=document->scene().worldTransform(selected.front()))pivot=math::transformPoint(*world,{});
        auto drawProxy=math::translation(pivot);std::optional<Mat4> decalFrame;
        if(selected.size()==1){decalFrame=decalEditFrame(document->scene(),selected.front());if(decalFrame)if(const auto oriented=decals::decalGizmoFrame(*decalFrame)){drawProxy=*oriented;pivot=math::transformPoint(drawProxy,{});}}
        const float depth=std::max(.01f,float(math::dot(pivot-camera->eye(),camera->forward())));
        const float length=std::max(.0001f,depth*std::tan(float(camera->verticalFov(rectangle[2]/rectangle[3])*std::numbers::pi/360))*160/rectangle[3]);
        float bestShape=8,bestAxis=8;
        if(!blocked&&selected.size()==1){const auto record=document->scene().find(selected.front());const auto world=document->scene().worldTransform(selected.front());if(record&&world&&(record->collider||record->navigationVolume)){
            SceneCollider shape=record->collider?*record->collider:SceneCollider{};if(record->navigationVolume)shape.size=record->navigationVolume->size;
            for(const auto& handle:colliderGizmoHandles(shape))if(const auto projected=math::project(math::transformPoint(*world,handle.point),viewProjection,rectangle,ClipDepth::ZeroToOne);projected&&(*projected)[2]>=0&&(*projected)[2]<=1){const QPointF point{(*projected)[0],(*projected)[1]};const auto distance=float(std::hypot(pointer.x()-point.x(),pointer.y()-point.y()));const bool hot=distance<8;lines.push_back({point-QPointF(4,0),point+QPointF(4,0),hot?QColor(255,220,70):QColor(255,166,26),3});if(distance<bestShape){bestShape=distance;hoverShape=handle.id;hoverEntity=record->id;shapeIsVolume=record->navigationVolume.has_value();}}
        }}
        if(!blocked&&!selected.empty())for(unsigned candidate=0;candidate<3;++candidate){
            if(decalFrame&&operation==ViewportTransformOperation::Rotate&&candidate!=2)continue;
            if(decalFrame&&operation==ViewportTransformOperation::Scale&&candidate==2)continue;
            const auto direction=math::normalized(math::transformDirection(drawProxy,axisVector(candidate)));float distance=1e9f;
            if(operation==ViewportTransformOperation::Rotate){const auto u=math::normalized(math::cross(direction,std::abs(direction[1])<.95f?Vec3{0,1,0}:Vec3{1,0,0})),v=math::cross(direction,u);for(unsigned segment=0;segment<64;++segment){const auto point=[&](unsigned i){const auto angle=2*std::numbers::pi_v<float>*i/64;return pivot+(u*std::cos(angle)+v*std::sin(angle))*length;};const auto a=point(segment),b=point(segment+1);addLine(a,b,axisColor(candidate));if(const auto line=projectClippedSegment(a,b,viewProjection,rectangle,ClipDepth::ZeroToOne))distance=std::min(distance,segmentDistance(pointer,{(*line)[0][0],(*line)[0][1]},{(*line)[1][0],(*line)[1][1]}));}}
            else{const auto tip=pivot+direction*length;addLine(pivot,tip,axisColor(candidate),2.5f);if(const auto line=projectClippedSegment(pivot,tip,viewProjection,rectangle,ClipDepth::ZeroToOne))distance=segmentDistance(pointer,{(*line)[0][0],(*line)[0][1]},{(*line)[1][0],(*line)[1][1]});}
            if(distance<bestAxis){bestAxis=distance;hoverAxis=candidate;}
        }
        if(!active()){proxy=drawProxy;axisLength=length;}
        for(const auto& box:selectionBoxes(document->scene(),selected,modelBounds)){std::array<Vec3,8> corners;for(unsigned i=0;i<8;++i)corners[i]=math::transformPoint(box.world,{i&1?box.local.maximum[0]:box.local.minimum[0],i&2?box.local.maximum[1]:box.local.minimum[1],i&4?box.local.maximum[2]:box.local.minimum[2]});for(unsigned i=0;i<8;++i)for(unsigned edge=0;edge<3;++edge)if(!(i&(1u<<edge)))addLine(corners[i],corners[i|(1u<<edge)],QColor(255,166,26));}
        float markerDistance=9;for(const auto& record:document->scene().records())if(viewportEntityHasCenterMarker(record)&&document->scene().isVisible(record.id)){const auto world=document->scene().worldTransform(record.id);if(!world)continue;const auto position=math::transformPoint(*world,{});if(const auto point=math::project(position,viewProjection,rectangle,ClipDepth::ZeroToOne);point&&(*point)[2]>=0&&(*point)[2]<=1){const QPointF p{(*point)[0],(*point)[1]};const auto distance=float(std::hypot(pointer.x()-p.x(),pointer.y()-p.y()));if(distance<markerDistance){markerDistance=distance;marker=record.id;}const QColor color=std::find(selected.begin(),selected.end(),record.id)!=selected.end()?QColor(255,166,26):QColor(210,210,210);lines.push_back({p-QPointF(5,0),p+QPointF(5,0),color});lines.push_back({p-QPointF(0,5),p+QPointF(0,5),color});if(record.spot||record.directional)addLine(position,position+directionalLightDirection(*world)*length,color);}}
    }
    bool pressHandle(Qt::KeyboardModifiers modifiers){
        if(blocked||!document||active())return false;
        if((hoverShape!=~0u||hoverAxis!=~0u||marker)&&overlay.beforeInteraction)overlay.beforeInteraction();
        if(document->transactionActive())return false;const auto pickRay=ray();if(!pickRay)return false;
        if(hoverShape!=~0u){const bool begun=shapeIsVolume?volume.begin(*document,hoverEntity,hoverShape,*pickRay):collider.begin(*document,hoverEntity,hoverShape,*pickRay);if(begun){gestureView=camera->viewMatrix();viewport.grabMouse();return true;}}
        if(hoverAxis!=~0u){const auto pivot=math::transformPoint(proxy,{});const auto direction=math::normalized(math::transformDirection(proxy,axisVector(hoverAxis)));const auto coordinate=colliderHandleCoordinate(*pickRay,proxy,hoverAxis);const auto plane=math::intersectPlane(*pickRay,pivot,direction);std::optional<Mat4> decal;if(document->selection().ids.size()==1)decal=decalEditFrame(document->scene(),document->selection().ids.front());if((operation==ViewportTransformOperation::Rotate?bool(plane):bool(coordinate))&&transform.begin(*document,document->selection().ids,proxy,operation,modifiers.testFlag(Qt::ShiftModifier),decal)){axis=hoverAxis;press=pointer;initialCoordinate=coordinate.value_or(0);initialPlaneVector=plane?math::normalized(*plane-pivot):Vec3{};gestureView=camera->viewMatrix();viewport.grabMouse();return true;}}
        if(marker){selectViewportHit(*document,marker,modifiers.testFlag(Qt::ControlModifier),modifiers.testFlag(Qt::ShiftModifier));return true;}return false;
    }
};
LevelViewportTools::LevelViewportTools(QWidget& viewport,std::function<void()> request):QWidget(&viewport),impl_(std::make_unique<Impl>(*this,viewport,std::move(request))){setObjectName("engineSceneToolsOverlay");setAttribute(Qt::WA_TransparentForMouseEvents);setAttribute(Qt::WA_NoSystemBackground);setFocusPolicy(Qt::NoFocus);viewport.installEventFilter(this);setGeometry(viewport.rect());show();raise();}
LevelViewportTools::~LevelViewportTools(){impl_->viewport.removeEventFilter(this);impl_->cancel();}
void LevelViewportTools::bind(SceneDocument* document,AssetLibrary* assets,Camera* camera){if(impl_->document!=document){impl_->cancel();impl_->document=document;}impl_->assets=assets;impl_->camera=camera;synchronize();}
void LevelViewportTools::synchronize(bool blocked){auto& state=*impl_;state.blocked=blocked;setGeometry(state.viewport.rect());raise();state.pointer=state.viewport.mapFromGlobal(QCursor::pos());state.rebuild();update();}
void LevelViewportTools::setOperation(ViewportTransformOperation operation){if(!impl_->active())impl_->operation=operation;synchronize(impl_->blocked);}
bool LevelViewportTools::active()const{return impl_->active();}
void LevelViewportTools::finish(){impl_->finish();impl_->changed();}
void LevelViewportTools::cancel(){impl_->cancel();impl_->changed();}
bool LevelViewportTools::eventFilter(QObject*,QEvent* event){auto& state=*impl_;if(additionalEvent&&additionalEvent(*event))return true;
    if(event->type()==QEvent::Resize||event->type()==QEvent::Show){setGeometry(state.viewport.rect());raise();}
    if(event->type()==QEvent::FocusOut||event->type()==QEvent::Hide||event->type()==QEvent::ParentChange||event->type()==QEvent::WinIdChange||event->type()==QEvent::DevicePixelRatioChange){state.cancel();state.changed();return false;}
    if(!state.document||!state.camera)return false;
    if(event->type()==QEvent::KeyPress){const auto& key=*static_cast<QKeyEvent*>(event);if(key.key()==Qt::Key_Escape&&active()){cancel();return true;}if(!state.blocked&&!active()&&key.modifiers()==Qt::NoModifier){if(key.key()==Qt::Key_T){setOperation(ViewportTransformOperation::Translate);return true;}if(key.key()==Qt::Key_R){setOperation(ViewportTransformOperation::Rotate);return true;}if(key.key()==Qt::Key_E){setOperation(ViewportTransformOperation::Scale);return true;}}}
    if(event->type()==QEvent::MouseButtonPress){const auto& mouse=*static_cast<QMouseEvent*>(event);if(mouse.button()!=Qt::LeftButton)return false;state.pointer=mouse.position();state.rebuild();if(state.pressHandle(mouse.modifiers())){state.viewport.setFocus(Qt::MouseFocusReason);state.changed();return true;}}
    if(event->type()==QEvent::MouseMove){const auto& mouse=*static_cast<QMouseEvent*>(event);state.pointer=mouse.position();const bool owned=active();if(owned)state.move(mouse.modifiers());state.rebuild();state.changed();return owned;}
    if(event->type()==QEvent::MouseButtonRelease&&static_cast<QMouseEvent*>(event)->button()==Qt::LeftButton&&active()){state.pointer=static_cast<QMouseEvent*>(event)->position();state.move(static_cast<QMouseEvent*>(event)->modifiers());state.finish();state.rebuild();state.changed();return true;}
    return false;
}
void LevelViewportTools::paintEvent(QPaintEvent*){QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);for(const auto& line:impl_->lines){painter.setPen(QPen(line.color,line.width,Qt::SolidLine,Qt::RoundCap));painter.drawLine(line.a,line.b);}if(additionalPaint)additionalPaint(painter);}
}
