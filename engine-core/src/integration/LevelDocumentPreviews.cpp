#include "LevelDocumentPreviews.h"
#include "LevelSession.h"
#include "AssetDocuments.h"
#include "widgets/QtDocumentProperties.h"
#include "assets/AssetLibrary.h"
#include "editor/AnimationDocument.h"
#include "editor/ParticleDocument.h"
#include "editor/DecalDocument.h"
#include "editor/DecalPreview.h"
#include "editor/RagdollDocument.h"
#include "editor/RagdollAuthoring.h"
#include "editor/CollisionWire.h"
#include "physics/PhysicsWorld.h"
#include "runtime/AnimationPlayback.h"
#include "particles/CpuParticleSystem.h"
#include "render/render_host.hpp"
#include "ui/qt_vulkan_composition_widget.hpp"
#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numbers>
#include <unordered_set>

namespace crimson::integration {
namespace {
using Json=nlohmann::json;
class PainterLayer final:public QWidget {
public:
    std::function<void(QPainter&)> paint;
    explicit PainterLayer(QWidget* parent):QWidget(parent){setAttribute(Qt::WA_TransparentForMouseEvents);setAttribute(Qt::WA_NoSystemBackground);setFocusPolicy(Qt::NoFocus);}
    void paintEvent(QPaintEvent*)override{QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);if(paint)paint(painter);}
};
class DocumentViewport final:public QWidget {
public:
    Camera camera;
    std::function<bool(QEvent&)> input;
    std::function<void()> changed;
    std::function<void(QPainter&)> paint;
    std::function<bool(std::uint32_t,std::uint32_t)> record;
    quader::RenderHost& host;quader::EnginePreviewKind kind;
    std::shared_ptr<const void> lifetime=std::make_shared<const int>(0);
    quader::QtVulkanCompositionWidget* composition;PainterLayer* overlay;
    DocumentViewport(QWidget* parent,quader::RenderHost& render,quader::EnginePreviewKind type):QWidget(parent),host(render),kind(type){setFocusPolicy(Qt::StrongFocus);setMouseTracking(true);setMinimumSize(160,120);auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);composition=new quader::QtVulkanCompositionWidget(this,host.composition_api());composition->set_render_system(&host);composition->setAttribute(Qt::WA_TransparentForMouseEvents);composition->setFocusPolicy(Qt::NoFocus);layout->addWidget(composition);overlay=new PainterLayer(this);overlay->paint=[this](QPainter& painter){if(paint)paint(painter);};overlay->show();composition->set_prepare_callback([this]{publish();return true;});composition->set_render_callback([this](const auto& target){return host.render_engine_preview(kind,target);});}
    ~DocumentViewport()override{shutdown();}
    void shutdown(){if(!lifetime)return;lifetime.reset();host.clear_engine_preview(kind);composition->set_prepare_callback({});composition->set_render_callback({});composition->set_render_system(nullptr);record={};input={};paint={};changed={};if(QWidget::mouseGrabber()==this)releaseMouse();}
    void markDirty(){++revision_;}
    void publish(){if(!lifetime||!isVisible()||!record)return;const auto ratio=devicePixelRatioF();const auto width=std::uint32_t(std::max(1,qRound(this->width()*ratio))),height=std::uint32_t(std::max(1,qRound(this->height()*ratio)));if(published_==revision_&&publishedWidth_==width&&publishedHeight_==height)return;host.publish_engine_preview(kind,lifetime,[this,width,height]{return record&&record(width,height);});published_=revision_;publishedWidth_=width;publishedHeight_=height;}
    void refresh(){markDirty();publish();composition->update();overlay->setGeometry(rect());overlay->raise();overlay->update();}
    ViewportRegion region()const{return {0,0,float(width()),float(height()),underMouse()};}
protected:
    bool event(QEvent* event)override{
        if(event->type()==QEvent::Resize||event->type()==QEvent::Show){overlay->setGeometry(rect());overlay->raise();}
        if(event->type()==QEvent::MouseButtonPress)setFocus(Qt::MouseFocusReason);
        if(input&&input(*event)){event->accept();markDirty();if(changed)changed();overlay->update();return true;}
        if(event->type()==QEvent::FocusOut||event->type()==QEvent::Hide||event->type()==QEvent::WindowDeactivate){gesture_=0;if(QWidget::mouseGrabber()==this)releaseMouse();}
        if(event->type()==QEvent::MouseButtonPress){const auto& mouse=*static_cast<QMouseEvent*>(event);if(mouse.button()==Qt::MiddleButton){gesture_=mouse.modifiers().testFlag(Qt::ShiftModifier)?2:1;last_=mouse.position();grabMouse();event->accept();return true;}}
        if(event->type()==QEvent::MouseButtonRelease&&static_cast<QMouseEvent*>(event)->button()==Qt::MiddleButton){gesture_=0;if(QWidget::mouseGrabber()==this)releaseMouse();return true;}
        if(event->type()==QEvent::MouseMove&&gesture_){const auto position=static_cast<QMouseEvent*>(event)->position(),delta=position-last_;last_=position;if(gesture_==1)camera.orbit(float(delta.x()),float(delta.y()),camera.target);else camera.pan(float(delta.x()),float(delta.y()),float(width()),float(height()));markDirty();if(changed)changed();overlay->update();return true;}
        if(event->type()==QEvent::Wheel){camera.zoom(float(static_cast<QWheelEvent*>(event)->angleDelta().y())/120);markDirty();if(changed)changed();overlay->update();return true;}
        return QWidget::event(event);
    }
private:int gesture_{};QPointF last_;std::uint64_t revision_{1},published_{};std::uint32_t publishedWidth_{},publishedHeight_{};
};
class PreviewPane:public QWidget {
public:
    using QWidget::QWidget;
    virtual void updatePreview(double,bool)=0;
    virtual bool continuous()const{return false;}
    virtual void setAntiAliasing(AntiAliasing){}
};
void setPhysicalNodes(RagdollAsset& asset,const ModelAsset& model,const std::vector<std::uint32_t>& nodes){if(nodes.empty()){asset.bodies.clear();return;}auto generated=asset;generateRagdollBodies(generated,model,nodes);for(auto& body:generated.bodies)for(const auto& previous:asset.bodies)if(previous.node==body.node){const auto parent=body.parent;body=previous;body.parent=parent;break;}asset=std::move(generated);}
class RagdollPane final:public PreviewPane {
public:
    LevelSession& session;RagdollDocument& document_;QtDocumentProperties& properties;QSplitter& original;
    DocumentViewport* view;QTreeWidget* bones;QComboBox *scope,*clips;QDoubleSpinBox *time,*drop;QPushButton *simulate;QCheckBox *proportions;QLabel* status;
    std::function<void()> request;
    SceneSettings settings_;std::shared_ptr<const ModelAsset> boundSource_;std::shared_ptr<const ModelPose> pose_;
    std::unique_ptr<PhysicsWorld> simulation_;Scene floor_;
    Mat4 previewWorld_;std::vector<std::uint8_t> manualPins_,scopePins_;std::vector<std::uint32_t> selectedNodes_;
    int selectedBody_{-1},selectedNode_{-1},startingClip_{-1},simulationMode_{};
    bool simulating_{},paused_{},dragging_{},showBones_{true},showShapes_{true},showJoints_{true},floorEnabled_{true},framePending_{true},rebuilding_{};
    float dropHeight_{.5f};double seconds_{},accumulator_{};std::uint64_t simulationRevision_{},shownRevision_{UINT64_MAX};
    Vec3 dragPoint_,dragNormal_;
    struct Gizmo{bool active{};std::size_t body{};unsigned axis{};RagdollBody baseline;Mat4 bone,proxy;QPointF press,screenAxis;float length{};RagdollAuthoringOperation operation{};bool locked{};std::string asset;std::uint64_t transaction{};} gizmo;
    RagdollAuthoringOperation operation{RagdollAuthoringOperation::Translate};
    RagdollPane(QMainWindow& window,LevelSession& level,RagdollDocument& document,QtDocumentProperties& fields,quader::RenderHost& host,std::function<void()> requestFrame)
        :PreviewPane(&window),session(level),document_(document),properties(fields),original(*qobject_cast<QSplitter*>(window.centralWidget())),request(std::move(requestFrame)){
        auto* layout=new QHBoxLayout(this);layout->setContentsMargins(0,0,0,0);auto* split=new QSplitter(this);layout->addWidget(split);
        auto* left=new QWidget(split);auto* leftLayout=new QVBoxLayout(left);auto* buttons=new QToolBar(left);auto* generate=buttons->addAction("Generate");auto* add=buttons->addAction("Add Body");auto* remove=buttons->addAction("Remove Body");leftLayout->addWidget(buttons);bones=new QTreeWidget(left);bones->setHeaderLabels({"Bones","Body"});bones->setIndentation(12);bones->setSelectionMode(QAbstractItemView::SingleSelection);leftLayout->addWidget(bones,1);split->addWidget(left);
        bones->setObjectName(QStringLiteral("CrimsonRagdollBones"));
        view=new DocumentViewport(split,host,quader::EnginePreviewKind::Ragdoll);view->changed=request;split->addWidget(view);
        auto* right=new QSplitter(Qt::Vertical,split);auto* controls=new QWidget(right);auto* controlLayout=new QVBoxLayout(controls);auto* row=new QHBoxLayout;simulate=new QPushButton("Simulate");auto* reset=new QPushButton("Reset");auto* frame=new QPushButton("Frame");row->addWidget(simulate);row->addWidget(reset);row->addWidget(frame);controlLayout->addLayout(row);
        auto* form=new QFormLayout;scope=new QComboBox;scope->addItems({"Whole Ragdoll","Selected Body","Selected Branch"});form->addRow("Test Mode",scope);clips=new QComboBox;clips->addItem("Reference Pose",-1);form->addRow("Starting Pose",clips);time=new QDoubleSpinBox;time->setDecimals(3);time->setRange(0,100000);time->setSingleStep(1./60);form->addRow("Time (s)",time);drop=new QDoubleSpinBox;drop->setRange(0,100);drop->setSingleStep(.05);drop->setValue(.5);form->addRow("Drop Height (m)",drop);controlLayout->addLayout(form);
        auto* pinRow=new QHBoxLayout;auto* pin=new QPushButton("Pin / Unpin");auto* clear=new QPushButton("Clear Pins");pinRow->addWidget(pin);pinRow->addWidget(clear);controlLayout->addLayout(pinRow);auto* visibility=new QHBoxLayout;for(auto pair:{std::pair{"Bones",&showBones_},std::pair{"Shapes",&showShapes_},std::pair{"Joints",&showJoints_}}){auto* toggle=new QCheckBox(pair.first);toggle->setChecked(true);visibility->addWidget(toggle);connect(toggle,&QCheckBox::toggled,this,[this,value=pair.second](bool enabled){*value=enabled;wake();});}controlLayout->addLayout(visibility);proportions=new QCheckBox("Lock Proportions");controlLayout->addWidget(proportions);auto* floorToggle=new QCheckBox("Floor");floorToggle->setChecked(true);controlLayout->addWidget(floorToggle);connect(floorToggle,&QCheckBox::toggled,this,[this](bool value){floorEnabled_=value;resetSimulation();wake();});status=new QLabel;status->setWordWrap(true);controlLayout->addWidget(status);controlLayout->addStretch();right->addWidget(controls);right->addWidget(&properties);right->setSizes({300,400});split->addWidget(right);split->setSizes({240,650,350});
        properties.bind([this]{auto value=Json::parse(serializeRagdollAsset(document_.definition()));Json result{{"name",value.at("name")},{"gravityFactor",value.at("gravityFactor")}};if(selectedBody_>=0&&std::size_t(selectedBody_)<document_.definition().bodies.size()){auto body=value.at("bodies").at(selectedBody_);body.erase("node");body.erase("parent");result["body"]=std::move(body);}return result;},[this](const Json& value,const std::string& label,bool final){try{finishGizmo();auto authored=Json::parse(serializeRagdollAsset(document_.definition()));authored["name"]=value.at("name");authored["gravityFactor"]=value.at("gravityFactor");if(value.contains("body")&&selectedBody_>=0&&std::size_t(selectedBody_)<document_.definition().bodies.size())for(const auto& [key,item]:value.at("body").items())authored["bodies"][selectedBody_][key]=item;const auto candidate=deserializeRagdollAsset(authored.dump(),document_.assetId());if(!document_.transactionActive())document_.begin(label);if(!document_.update([&](auto& asset){asset=candidate;})||(final&&!document_.commit()))throw std::runtime_error(document_.status());resetSimulation();}catch(const std::exception& error){document_.cancel();status->setText(error.what());}wake();},[this]{finishGizmo();if(document_.transactionActive())document_.commit();},[this]{cancelGizmo();document_.cancel();});
        connect(generate,&QAction::triggered,this,[this]{if(!boundSource_)return;finish();document_.edit("Generate ragdoll bodies",[&](auto& asset){generateRagdollBodies(asset,*boundSource_,selectedNodes_);});selectedBody_=-1;resetSimulation();wake();});
        connect(add,&QAction::triggered,this,[this]{if(!boundSource_||selectedNode_<0)return;finish();std::vector<std::uint32_t> nodes;for(const auto& body:document_.definition().bodies)nodes.push_back(body.node);nodes.push_back(std::uint32_t(selectedNode_));document_.edit("Add ragdoll body",[&](auto& asset){setPhysicalNodes(asset,*boundSource_,nodes);});for(std::size_t i=0;i<document_.definition().bodies.size();++i)if(int(document_.definition().bodies[i].node)==selectedNode_)selectedBody_=int(i);resetSimulation();wake();});
        connect(remove,&QAction::triggered,this,[this]{if(!boundSource_||selectedBody_<0)return;finish();std::vector<std::uint32_t> nodes;for(std::size_t i=0;i<document_.definition().bodies.size();++i)if(int(i)!=selectedBody_)nodes.push_back(document_.definition().bodies[i].node);document_.edit("Remove ragdoll body",[&](auto& asset){setPhysicalNodes(asset,*boundSource_,nodes);});selectedBody_=-1;resetSimulation();wake();});
        connect(bones,&QTreeWidget::itemSelectionChanged,this,[this]{if(rebuilding_)return;finish();const auto rows=bones->selectedItems();selectedNode_=rows.empty()?-1:rows.front()->data(0,Qt::UserRole).toInt();selectedBody_=-1;for(std::size_t i=0;i<document_.definition().bodies.size();++i)if(int(document_.definition().bodies[i].node)==selectedNode_)selectedBody_=int(i);properties.resetProjection();wake();});
        connect(bones,&QTreeWidget::itemChanged,this,[this](QTreeWidgetItem* item,int column){if(rebuilding_||column)return;const auto node=item->data(0,Qt::UserRole).toUInt();if(item->checkState(0)==Qt::Checked){if(std::find(selectedNodes_.begin(),selectedNodes_.end(),node)==selectedNodes_.end())selectedNodes_.push_back(node);}else std::erase(selectedNodes_,node);});
        connect(simulate,&QPushButton::clicked,this,[this]{if(simulating_){paused_=!paused_;endDrag();}else startSimulation();wake();});connect(reset,&QPushButton::clicked,this,[this]{finish();resetSimulation();wake();});connect(frame,&QPushButton::clicked,this,[this]{framePending_=true;wake();});connect(scope,&QComboBox::currentIndexChanged,this,[this](int value){resetSimulation();simulationMode_=value;wake();});connect(clips,&QComboBox::currentIndexChanged,this,[this]{resetSimulation();startingClip_=clips->currentData().toInt();seconds_=0;time->setValue(0);framePending_=true;wake();});connect(time,&QDoubleSpinBox::valueChanged,this,[this](double value){seconds_=value;wake();});connect(drop,&QDoubleSpinBox::valueChanged,this,[this](double value){dropHeight_=float(value);wake();});
        connect(pin,&QPushButton::clicked,this,[this]{if(selectedBody_<0||std::size_t(selectedBody_)>=document_.definition().bodies.size())return;endDrag();manualPins_.resize(document_.definition().bodies.size());const auto i=std::size_t(selectedBody_);const bool value=!manualPins_[i],scopePin=i<scopePins_.size()&&scopePins_[i];if(!simulation_||simulation_->setRagdollBodyPinned(1,i,value||scopePin))manualPins_[i]=value;wake();});connect(clear,&QPushButton::clicked,this,[this]{endDrag();for(std::size_t i=0;i<manualPins_.size();++i)if(manualPins_[i]){const bool scopePin=i<scopePins_.size()&&scopePins_[i];if(!simulation_||simulation_->setRagdollBodyPinned(1,i,scopePin))manualPins_[i]=0;}wake();});
        view->input=[this](QEvent& event){return input(event);};view->paint=[this](QPainter& painter){if(boundSource_&&pose_)overlays(*boundSource_,*pose_,view->region(),painter);paintGizmo(painter);};view->record=[this](std::uint32_t width,std::uint32_t height){settings_.camera=view->camera;return boundSource_&&pose_&&session.recordModelDocumentPreview(LevelModelPreviewKind::Ragdoll,boundSource_->id,pose_,previewWorld_,settings_,width,height);};
    }
    ~RagdollPane()override{cancelGizmo();endDrag();view->shutdown();properties.detach();original.addWidget(&properties);}
    void wake(){view->markDirty();if(request)request();view->overlay->update();}
    void endDrag(){if(simulation_)simulation_->endRagdollDrag();dragging_=false;}
    void resetSimulation(){endDrag();simulation_.reset();simulating_=paused_=false;accumulator_=0;previewWorld_=Mat4{};scopePins_.clear();}
    void finish(){finishGizmo();if(document_.transactionActive())document_.commit();endDrag();}
    void startSimulation();
    void finishGizmo(){if(gizmo.active&&gizmo.transaction==document_.transactionId())document_.commit();gizmo.active=false;}
    void cancelGizmo(){if(gizmo.active&&gizmo.transaction==document_.transactionId())document_.cancel();gizmo.active=false;}
    void rebuildBones();
    void updatePreview(double seconds,bool active)override;
    bool continuous()const override{return simulating_&&!paused_;}
    void setAntiAliasing(AntiAliasing value)override{settings_.antiAliasing=value;}
    void overlays(const ModelAsset&,const ModelPose&,const ViewportRegion&,QPainter&);
    bool input(QEvent&);
    void paintGizmo(QPainter&);
    std::optional<std::array<QPointF,4>> gizmoProjection()const;
    void moveGizmo(QPointF);
};

void RagdollPane::startSimulation(){const auto manual=manualPins_;resetSimulation();manualPins_=manual;try{finish();if(!boundSource_)return;pose_=std::make_shared<const ModelPose>(evaluateModelPose(*boundSource_,startingClip_>=0?std::optional<std::size_t>{std::size_t(startingClip_)}:std::nullopt,seconds_,false));validateRagdollSource(document_.definition(),*boundSource_);if(document_.definition().bodies.empty())throw std::runtime_error("Add physical bodies before starting simulation.");if(simulationMode_&&selectedBody_<0)throw std::runtime_error("Select a physical body before starting this test mode.");EntityRecord floor;floor.id=2;floor.name="Ragdoll preview floor";floor.local=math::translation({0,-.5f,0});floor.collider.emplace();floor.collider->size={100,1,100};floor_.apply(floorEnabled_?std::vector<EntityRecord>{floor}:std::vector<EntityRecord>{},{});simulation_=std::make_unique<PhysicsWorld>();simulation_->reconcileStatics(floor_);previewWorld_=math::translation({0,dropHeight_-(pose_->bounds.valid?std::min(0.f,pose_->bounds.minimum[1]):0.f),0});if(!simulation_->createRagdoll(1,std::make_shared<const RagdollAsset>(document_.definition()),boundSource_,*pose_,previewWorld_))throw std::runtime_error(simulation_->status());const auto mask=ragdollSimulationMask(document_.definition().bodies,std::size_t(selectedBody_),static_cast<RagdollSimulationScope>(simulationMode_));scopePins_=ragdollScopePinMask(document_.definition().bodies,mask,false);manualPins_.resize(scopePins_.size());for(std::size_t i=0;i<scopePins_.size();++i)if(scopePins_[i]||manualPins_[i])if(!simulation_->setRagdollBodyPinned(1,i,true))throw std::runtime_error(simulation_->status());simulationRevision_=document_.previewRevision();simulating_=true;status->clear();}catch(const std::exception& error){resetSimulation();status->setText(error.what());}}
void RagdollPane::rebuildBones(){rebuilding_=true;const QSignalBlocker block(bones);bones->clear();if(boundSource_){std::map<std::uint32_t,QTreeWidgetItem*> items;const auto& model=*boundSource_;const auto skin=document_.definition().skin;std::unordered_set<std::uint32_t> joints;if(skin<model.skins.size())joints.insert(model.skins[skin].joints.begin(),model.skins[skin].joints.end());for(std::size_t node=0;node<model.nodes.size();++node){const auto& source=model.nodes[node];auto* item=new QTreeWidgetItem(QStringList{QString::fromStdString(source.name.empty()?"Bone "+std::to_string(node):source.name),{}});item->setData(0,Qt::UserRole,unsigned(node));if(joints.contains(unsigned(node))){item->setFlags(item->flags()|Qt::ItemIsUserCheckable);item->setCheckState(0,std::find(selectedNodes_.begin(),selectedNodes_.end(),node)!=selectedNodes_.end()?Qt::Checked:Qt::Unchecked);}for(const auto& body:document_.definition().bodies)if(body.node==node)item->setText(1,"●");items.emplace(unsigned(node),item);}for(std::size_t node=0;node<model.nodes.size();++node){auto* item=items.at(unsigned(node));const auto parent=items.find(unsigned(model.nodes[node].parent));if(model.nodes[node].parent>=0&&parent!=items.end())parent->second->addChild(item);else bones->addTopLevelItem(item);if(int(node)==selectedNode_)item->setSelected(true);item->setExpanded(true);}if(clips->count()!=int(model.animations.size())+1){const QSignalBlocker clipBlock(clips);clips->clear();clips->addItem("Reference Pose",-1);for(std::size_t i=0;i<model.animations.size();++i)clips->addItem(QString::fromStdString(model.animations[i].name.empty()?"Animation "+std::to_string(i):model.animations[i].name),int(i));}}rebuilding_=false;}
void RagdollPane::updatePreview(double dt,bool active){if(!document_.isOpen())return;try{if(gizmo.active&&(gizmo.transaction!=document_.transactionId()||gizmo.asset!=document_.assetId()))cancelGizmo();const auto source=session.assets().findModel(document_.definition().sourceModel.id);if(source!=boundSource_){cancelGizmo();resetSimulation();boundSource_=source;shownRevision_=UINT64_MAX;framePending_=true;selectedBody_=-1;selectedNode_=-1;}if(shownRevision_!=document_.previewRevision()){if(simulating_&&simulationRevision_!=document_.previewRevision())resetSimulation();if(!gizmo.active){manualPins_.assign(document_.definition().bodies.size(),0);scopePins_.clear();}shownRevision_=document_.previewRevision();rebuildBones();}if(!active){endDrag();cancelGizmo();}if(boundSource_){validateRagdollSource(document_.definition(),*boundSource_);if(startingClip_>=int(boundSource_->animations.size()))startingClip_=-1;if(simulating_&&simulation_){if(active&&!paused_&&std::isfinite(dt)&&dt>0){accumulator_+=std::min(dt,.1);while(accumulator_>=1./60){simulation_->step(floor_,NoEntity,{},1.f/60);accumulator_-=1./60;}}pose_=simulation_->sampleRagdollPose(1,float(accumulator_*60));}else pose_=std::make_shared<const ModelPose>(evaluateModelPose(*boundSource_,startingClip_>=0?std::optional<std::size_t>{std::size_t(startingClip_)}:std::nullopt,seconds_,false));if(framePending_&&pose_&&pose_->bounds.valid){const auto bounds=math::transformBounds(pose_->bounds,previewWorld_);view->camera.frameBounds(bounds.minimum,bounds.maximum,float(std::max(1,view->width()))/std::max(1,view->height()));framePending_=false;}}simulate->setText(!simulating_?"Simulate":paused_?"Resume":"Pause");clips->setEnabled(!simulating_&&!gizmo.active);time->setEnabled(!simulating_&&!gizmo.active);drop->setEnabled(!simulating_);scope->setEnabled(!simulating_);properties.setEnabled(!simulating_);if(view->isVisible())view->refresh();}catch(const std::exception& error){status->setText(error.what());endDrag();cancelGizmo();}}

#include "RagdollOverlay.inc"

std::optional<std::array<QPointF,4>> RagdollPane::gizmoProjection()const{if(simulating_||!pose_||selectedBody_<0||std::size_t(selectedBody_)>=document_.definition().bodies.size())return {};const auto& body=document_.definition().bodies[std::size_t(selectedBody_)];if(body.node>=pose_->worldTransforms.size())return {};const auto bone=previewWorld_*math::orthonormalFrame(pose_->worldTransforms[body.node]);const auto matrix=gizmo.active?std::optional{gizmo.proxy}:ragdollBodyGizmoMatrix(body,bone,operation);if(!matrix)return {};const auto center=math::transformPoint(*matrix,{});const auto vp=view->camera.projectionMatrix(float(std::max(1,view->width()))/std::max(1,view->height()),ClipDepth::ZeroToOne)*view->camera.viewMatrix();const Vec4 region{0,0,float(view->width()),float(view->height())};const auto origin=math::project(center,vp,region,ClipDepth::ZeroToOne);if(!origin||(*origin)[2]<0||(*origin)[2]>1)return {};std::array<QPointF,4> points;points[0]={(*origin)[0],(*origin)[1]};const auto rigid=math::orthonormalFrame(*matrix);const float length=std::max(.03f,float(math::length(view->camera.position-center))*.12f);for(unsigned axis=0;axis<3;++axis){Vec3 direction;direction[axis]=1;const auto end=math::project(center+math::transformDirection(rigid,direction)*length,vp,region,ClipDepth::ZeroToOne);points[axis+1]=end?QPointF{(*end)[0],(*end)[1]}:points[0];}return points;}
void RagdollPane::paintGizmo(QPainter& painter){if(const auto points=gizmoProjection()){const std::array<QColor,3> colors{QColor(235,76,82),QColor(90,205,118),QColor(78,142,255)};for(unsigned axis=0;axis<3;++axis){painter.setPen(QPen(colors[axis],gizmo.active&&gizmo.axis==axis?4:2));painter.drawLine((*points)[0],(*points)[axis+1]);painter.setBrush(colors[axis]);if(operation==RagdollAuthoringOperation::Resize)painter.drawRect(QRectF((*points)[axis+1]-QPointF(4,4),QSizeF(8,8)));else painter.drawEllipse((*points)[axis+1],4,4);}}}
void RagdollPane::moveGizmo(QPointF pointer){if(!gizmo.active||gizmo.transaction!=document_.transactionId()||gizmo.asset!=document_.assetId()||gizmo.body>=document_.definition().bodies.size()){cancelGizmo();return;}const auto delta=pointer-gizmo.press;const auto squared=QPointF::dotProduct(gizmo.screenAxis,gizmo.screenAxis);const float amount=squared>1e-6?float(QPointF::dotProduct(delta,gizmo.screenAxis)/squared):0;auto manipulated=gizmo.proxy;if(gizmo.operation==RagdollAuthoringOperation::Translate){Vec3 axis;axis[gizmo.axis]=1;const auto direction=math::transformDirection(math::orthonormalFrame(gizmo.proxy),axis);for(unsigned row=0;row<3;++row)manipulated.at(row,3)+=direction[row]*amount*gizmo.length;}else if(gizmo.operation==RagdollAuthoringOperation::Resize){const float factor=std::max(.001f,1+amount);for(unsigned axis=0;axis<3;++axis)if(gizmo.locked||axis==gizmo.axis)for(unsigned row=0;row<3;++row)manipulated.at(row,axis)*=factor;}else{Vec3 axis;axis[gizmo.axis]=1;manipulated=gizmo.proxy*math::rotation(math::axisAngle(axis,(delta.x()-delta.y())*.01));}if(const auto changed=applyRagdollBodyGizmoTransform(gizmo.baseline,gizmo.bone,manipulated,gizmo.operation,gizmo.locked)){if(!document_.update([&](auto& asset){asset.bodies[gizmo.body]=*changed;}))cancelGizmo();}else cancelGizmo();}
bool RagdollPane::input(QEvent& event){if(event.type()==QEvent::FocusOut||event.type()==QEvent::WindowDeactivate||event.type()==QEvent::Hide){endDrag();cancelGizmo();return false;}if(event.type()==QEvent::KeyPress){const auto& key=static_cast<QKeyEvent&>(event);if(key.key()==Qt::Key_Escape){cancelGizmo();endDrag();return true;}if(!simulating_&&!gizmo.active&&key.modifiers()==Qt::NoModifier){if(key.key()==Qt::Key_T)operation=RagdollAuthoringOperation::Translate;else if(key.key()==Qt::Key_R)operation=RagdollAuthoringOperation::Rotate;else if(key.key()==Qt::Key_E)operation=RagdollAuthoringOperation::Resize;else return false;return true;}}
    if(event.type()!=QEvent::MouseButtonPress&&event.type()!=QEvent::MouseButtonRelease&&event.type()!=QEvent::MouseMove)return false;const auto& mouse=static_cast<QMouseEvent&>(event);const auto ray=view->camera.screenRay(float(mouse.position().x()),float(mouse.position().y()),view->region());if(event.type()==QEvent::MouseButtonRelease&&mouse.button()==Qt::LeftButton){if(gizmo.active){moveGizmo(mouse.position());finishGizmo();}endDrag();if(QWidget::mouseGrabber()==view)view->releaseMouse();return true;}if(event.type()==QEvent::MouseMove){if(gizmo.active){moveGizmo(mouse.position());return true;}if(dragging_&&ray){if(const auto target=math::intersectPlane(*ray,dragPoint_,dragNormal_))simulation_->updateRagdollDrag(*target);return true;}return false;}if(mouse.button()==Qt::MiddleButton){endDrag();return false;}if(mouse.button()!=Qt::LeftButton||!ray||!boundSource_||!pose_)return false;finish();
    if(const auto points=gizmoProjection()){int hovered=-1;double best=9;for(unsigned axis=0;axis<3;++axis){const auto delta=(*points)[axis+1]-(*points)[0],offset=mouse.position()-(*points)[0];const auto length=QPointF::dotProduct(delta,delta);const auto fraction=length>1e-6?std::clamp(QPointF::dotProduct(offset,delta)/length,0.,1.):0.;const auto distance=QPointF(offset-delta*fraction).manhattanLength();if(distance<best){best=distance;hovered=int(axis);}}if(hovered>=0){const auto index=std::size_t(selectedBody_);const auto& body=document_.definition().bodies[index];const auto bone=previewWorld_*math::orthonormalFrame(pose_->worldTransforms[body.node]);if(const auto matrix=ragdollBodyGizmoMatrix(body,bone,operation);matrix&&document_.begin(operation==RagdollAuthoringOperation::Translate?"Move ragdoll collider":operation==RagdollAuthoringOperation::Rotate?"Rotate ragdoll collider":"Resize ragdoll collider")){gizmo={true,index,unsigned(hovered),body,bone,*matrix,mouse.position(),(*points)[std::size_t(hovered)+1]-(*points)[0],std::max(.03f,float(math::length(view->camera.position-math::transformPoint(*matrix,{})))*.12f),operation,proportions->isChecked(),document_.assetId(),document_.transactionId()};view->grabMouse();return true;}}}
    if(simulation_){if(const auto hit=simulation_->pickRagdoll(*ray,view->camera.farPlane)){selectedBody_=int(hit->bodyIndex);selectedNode_=int(document_.definition().bodies[hit->bodyIndex].node);if(!paused_&&simulation_->beginRagdollDrag(hit->entity,hit->bodyIndex,hit->position)){dragging_=true;dragPoint_=hit->position;dragNormal_=view->camera.forward();view->grabMouse();}}}else{std::vector<Mat4> bones;for(const auto& matrix:pose_->worldTransforms)bones.push_back(previewWorld_*math::orthonormalFrame(matrix));if(const auto hit=pickRagdollBodies(document_.definition().bodies,bones,*ray,view->camera.farPlane)){selectedBody_=int(hit->body);selectedNode_=int(document_.definition().bodies[hit->body].node);}}properties.resetProjection();rebuildBones();return true;
}
class AnimationPane final:public PreviewPane {
    LevelSession& session;AnimationDocument& document;DocumentViewport* view;QDoubleSpinBox* time;QPushButton* play;QLabel* status;
    SceneSettings settings;std::shared_ptr<const ModelAsset> source;std::optional<PreparedAnimationBinding> binding;std::shared_ptr<const ModelPose> pose;
    std::uint64_t revision{UINT64_MAX};double seconds{};bool playing{},framePending{true};std::function<void()> request;
public:
    AnimationPane(QMainWindow& window,LevelSession& level,AnimationDocument& doc,quader::RenderHost& host,std::function<void()> wake):PreviewPane(&window),session(level),document(doc),request(std::move(wake)){
        auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);auto* controls=new QHBoxLayout;play=new QPushButton("Play");auto* restart=new QPushButton("Restart");auto* frame=new QPushButton("Frame");time=new QDoubleSpinBox;time->setDecimals(3);time->setSingleStep(1./60);controls->addWidget(play);controls->addWidget(restart);controls->addWidget(frame);controls->addWidget(time,1);layout->addLayout(controls);view=new DocumentViewport(this,host,quader::EnginePreviewKind::Animation);view->changed=request;layout->addWidget(view,1);status=new QLabel;status->setWordWrap(true);layout->addWidget(status);
        connect(play,&QPushButton::clicked,this,[this]{playing=!playing;if(request)request();});connect(restart,&QPushButton::clicked,this,[this]{seconds=0;playing=true;if(request)request();});connect(frame,&QPushButton::clicked,this,[this]{framePending=true;if(request)request();});connect(time,&QDoubleSpinBox::valueChanged,this,[this](double value){seconds=value;view->markDirty();if(request)request();});
        view->record=[this](std::uint32_t width,std::uint32_t height){settings.camera=view->camera;return source&&pose&&session.recordModelDocumentPreview(LevelModelPreviewKind::Animation,source->id,pose,Mat4{},settings,width,height);};
    }
    ~AnimationPane()override{view->shutdown();}
    void setAntiAliasing(AntiAliasing value)override{settings.antiAliasing=value;}
    bool continuous()const override{return playing;}
    void updatePreview(double dt,bool active)override{if(!document.isOpen())return;try{auto model=session.assets().findModel(document.definition().sourceModel.id);if(model!=source||revision!=document.previewRevision()){source=std::move(model);binding.reset();revision=document.previewRevision();}if(!source){status->setText("Preparing animation model…");return;}if(!binding)binding=prepareAnimationBinding(document.definition(),*source,*source);const auto duration=std::max(0.,animationDuration(document.definition()));seconds=std::clamp(seconds,0.,duration);if(playing&&active&&std::isfinite(dt)&&dt>0){seconds+=std::min(dt,1.)*document.definition().playbackRate;if(duration>0&&document.definition().loop)seconds=std::fmod(seconds,duration);else if(seconds>=duration){seconds=duration;playing=false;}}pose=std::make_shared<const ModelPose>(evaluateAnimationPose(*binding,*source,*source,seconds,false));if(framePending&&pose->bounds.valid){view->camera.frameBounds(pose->bounds.minimum,pose->bounds.maximum,float(std::max(1,view->width()))/std::max(1,view->height()));framePending=false;}{const QSignalBlocker block(time);time->setRange(0,duration);time->setValue(seconds);}play->setText(playing?"Pause":"Play");status->clear();if(view->isVisible())view->refresh();}catch(const std::exception& error){playing=false;status->setText(error.what());}}
};
class DecalPane final:public PreviewPane {
    LevelSession& session_;DecalDocument& document_;DocumentViewport* view_;
    SceneSettings settings_;MaterialPreviewMesh mesh_{MaterialPreviewMesh::Plane};
    std::function<void()> request_;
public:
    DecalPane(QMainWindow& window,LevelSession& session,DecalDocument& document,AssetDocuments& documents,
        quader::RenderHost& host,std::function<void()> request):PreviewPane(&window),session_(session),document_(document),request_(std::move(request)){
        auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);auto* controls=new QHBoxLayout;
        auto* meshes=new QComboBox;meshes->setObjectName(QStringLiteral("CrimsonDecalPreviewMesh"));
        meshes->addItems({"Plane","Sphere","Cylinder","Cube","Rounded Cube"});controls->addWidget(meshes);
        auto* reset=new QPushButton("Reset view");controls->addWidget(reset);auto* edit=new QPushButton("Edit Material");controls->addWidget(edit);controls->addStretch();layout->addLayout(controls);
        view_=new DocumentViewport(this,host,quader::EnginePreviewKind::Decal);view_->changed=request_;layout->addWidget(view_,1);
        settings_.environment.glow.enabled=false;resetCamera();
        connect(meshes,&QComboBox::currentIndexChanged,this,[this](int index){
            constexpr std::array kChoices{MaterialPreviewMesh::Plane,MaterialPreviewMesh::Sphere,MaterialPreviewMesh::Cylinder,MaterialPreviewMesh::Cube,MaterialPreviewMesh::RoundedCube};
            if(index>=0&&std::size_t(index)<kChoices.size()){mesh_=kChoices[std::size_t(index)];resetCamera();if(request_)request_();}
        });
        connect(reset,&QPushButton::clicked,this,[this]{resetCamera();if(request_)request_();});
        connect(edit,&QPushButton::clicked,this,[this,&documents]{const auto id=session_.assets().references().resolveDecalMaterial(document_.definition().material.id);if(!id.empty()&&documents.finishActive())documents.open(AssetKind::Material,id);});
        view_->input=[this](QEvent& event){if(event.type()==QEvent::MouseButtonDblClick&&static_cast<QMouseEvent&>(event).button()==Qt::MiddleButton){resetCamera();return true;}return false;};
        view_->record=[this](std::uint32_t width,std::uint32_t height){settings_.camera=view_->camera;return session_.recordDecalDocumentPreview(document_.definition(),document_.previewRevision(),mesh_,settings_,width,height);};
    }
    ~DecalPane()override{view_->shutdown();session_.clearDecalDocumentPreview();}
    void setAntiAliasing(AntiAliasing value)override{settings_.antiAliasing=value;}
    void updatePreview(double,bool)override{if(document_.isOpen()&&view_->isVisible())view_->refresh();}
private:
    void resetCamera(){view_->camera=decalPreviewGeometry(mesh_).camera;view_->markDirty();}
};
class ParticlePane final:public PreviewPane {
    LevelSession& session;ParticleEffectDocument& document;DocumentViewport* view;QPushButton* play;QLabel* status;SceneSettings settings;
    CpuParticleSystem simulation;std::vector<ParticlePresentation> presentation;std::uint64_t revision{UINT64_MAX};std::function<void()> request;
public:
    ParticlePane(QMainWindow& window,LevelSession& level,ParticleEffectDocument& doc,quader::RenderHost& host,std::function<void()> wake):PreviewPane(&window),session(level),document(doc),request(std::move(wake)){
        auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);auto* controls=new QHBoxLayout;play=new QPushButton("Pause");auto* restart=new QPushButton("Restart");auto* stop=new QPushButton("Stop");controls->addWidget(play);controls->addWidget(restart);controls->addWidget(stop);controls->addStretch();layout->addLayout(controls);view=new DocumentViewport(this,host,quader::EnginePreviewKind::Particle);view->changed=request;view->camera.position={4,3,5};view->camera.target={0,1,0};layout->addWidget(view,1);status=new QLabel;status->setWordWrap(true);layout->addWidget(status);
        connect(play,&QPushButton::clicked,this,[this]{if(simulation.state()==ParticlePlaybackState::Running)simulation.pause();else simulation.play();if(request)request();});connect(restart,&QPushButton::clicked,this,[this]{simulation.restart(document.definition(),document.definition().defaultSeed);revision=document.previewRevision();if(request)request();});connect(stop,&QPushButton::clicked,this,[this]{simulation.stop();if(request)request();});
        view->record=[this](std::uint32_t width,std::uint32_t height){settings.camera=view->camera;return session.recordParticleDocumentPreview(document.definition().emitter.material.id,presentation,settings,simulation.elapsed(),width,height);};
    }
    ~ParticlePane()override{view->shutdown();}
    void setAntiAliasing(AntiAliasing value)override{settings.antiAliasing=value;}
    bool continuous()const override{return simulation.state()==ParticlePlaybackState::Running;}
    void updatePreview(double dt,bool active)override{if(!document.isOpen())return;try{if(revision==UINT64_MAX)simulation.restart(document.definition(),document.definition().defaultSeed);else if(revision!=document.previewRevision())simulation.updateDefinition(document.definition());revision=document.previewRevision();if(active&&std::isfinite(dt)&&dt>0)simulation.advance(dt);presentation.clear();simulation.appendPresentation(presentation);play->setText(simulation.state()==ParticlePlaybackState::Running?"Pause":"Play");status->setText(QString::number(presentation.size())+" particles");if(view->isVisible())view->refresh();}catch(const std::exception& error){simulation.pause();status->setText(error.what());}}
};
} // namespace

struct LevelDocumentPreviews::Impl {
    AssetDocuments& documents;LevelSession& session;quader::RenderHost& host;std::function<void()> request;
    std::map<AssetKind,std::unique_ptr<PreviewPane>> panes;
    AntiAliasing antiAliasing{AntiAliasing::Smaa};
    Impl(AssetDocuments& docs,LevelSession& level,quader::RenderHost& render,std::function<void()> wake):documents(docs),session(level),host(render),request(std::move(wake)){
        documents.setDocumentPreviewCallbacks([this](AssetKind kind,const std::string& id,QMainWindow& window){if(panes.contains(kind))return;std::unique_ptr<PreviewPane> pane;if(kind==AssetKind::Ragdoll){if(auto* document=documents.ragdollDocument(id))if(auto* fields=documents.properties(kind))pane=std::make_unique<RagdollPane>(window,session,*document,*fields,host,request);}else if(kind==AssetKind::Animation){if(auto* document=documents.animationDocument(id))pane=std::make_unique<AnimationPane>(window,session,*document,host,request);}else if(kind==AssetKind::Particle){if(auto* document=documents.particleDocument(id))pane=std::make_unique<ParticlePane>(window,session,*document,host,request);}if(kind==AssetKind::Decal){if(auto* document=documents.decalDocument(id))pane=std::make_unique<DecalPane>(window,session,*document,documents,host,request);}if(pane){pane->setAntiAliasing(antiAliasing);documents.setPreview(kind,id,pane.get());panes.emplace(kind,std::move(pane));}},[this](AssetKind kind,const std::string&){panes.erase(kind);});
    }
    ~Impl(){documents.setDocumentPreviewCallbacks({},{});panes.clear();}
};
LevelDocumentPreviews::LevelDocumentPreviews(AssetDocuments& docs,LevelSession& session,quader::RenderHost& host,std::function<void()> request):impl_(std::make_unique<Impl>(docs,session,host,std::move(request))){}
LevelDocumentPreviews::~LevelDocumentPreviews()=default;
void LevelDocumentPreviews::update(double seconds,bool active){for(auto& [kind,pane]:impl_->panes){(void)kind;pane->updatePreview(seconds,active);}}
void LevelDocumentPreviews::setAntiAliasing(AntiAliasing value){impl_->antiAliasing=value;for(auto& [kind,pane]:impl_->panes){(void)kind;pane->setAntiAliasing(value);}}
bool LevelDocumentPreviews::continuous()const{for(const auto& [kind,pane]:impl_->panes){(void)kind;if(pane->isVisible())return true;}return false;}
}
