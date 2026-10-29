#include "LevelPanels.h"
#include "LevelSession.h"
#include "native_scene_authoring.hpp"
#include "native_scene_projection.hpp"
#include "api/quader_editor_api.hpp"
#include "editor/session/project_session.hpp"
#include "editor/session/scene_session.hpp"
#include "ui/qt_viewport_panel.hpp"
#include "ui/inspector_panel.hpp"
#include "LevelViewportTools.h"
#include "LevelDocumentPreviews.h"
#include "browser/LevelAssetBrowser.h"
#include "browser/QtAssetBrowser.h"
#include "settings/LevelPreferences.h"
#include "paint/LevelVertexPaint.h"
#include "AssetDocuments.h"
#include "widgets/QtInspector.h"
#include "editor/MaterialDocument.h"
#include "editor/CameraPilot.h"
#include "editor/CameraBookmarks.h"
#include "assets/AssetLibrary.h"
#include "input/Navigation.h"
#include "input/InputEvent.h"
#include "runtime/GameInput.h"
#include "runtime/PlaySession.h"
#include "scene/SceneExtraction.h"
#include "scene/SceneOutlinerRows.h"
#include "tools/scene_inspector_context.h"
#include "ui/qt_vulkan_composition_widget.hpp"
#include "ui/qt_relative_pointer_input.hpp"
#include "ui/qt_dock_title_presentation.hpp"
#include "ui/qt_panel_section_header.hpp"
#include "ui/qt_material_preview_surface.hpp"
#include "ui/ui_panel.hpp"
#include "core/scene_environment_catalog.hpp"
#include "render/render_host.hpp"
#include <nlohmann/json.hpp>
#include <QAction>
#include <QActionGroup>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QInputDialog>
#include <QImage>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPointer>
#include <QPainter>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QScopedValueRollback>
#include <QStatusBar>
#include <QStackedWidget>
#include <QToolBar>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWindow>
#include <algorithm>
#include <array>
#include <atomic>
#include <limits>
#include <numbers>
#include <unordered_map>
#include <tuple>

namespace crimson::integration {
namespace {
class MaterialImageView final:public QWidget {
public:
    using QWidget::QWidget;
    void setImage(QImage image){if(!image.isNull()&&image.cacheKey()!=image_.cacheKey()){image_=std::move(image);update();}}
    void clearImage(){image_={};update();}
protected:
    void paintEvent(QPaintEvent*)override {
        QPainter painter(this);painter.fillRect(rect(),palette().brush(QPalette::Base));
        if(image_.isNull())return;
        const auto size=image_.size().scaled(this->size(),Qt::KeepAspectRatio);
        const QRect target(QPoint((width()-size.width())/2,(height()-size.height())/2),size);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);painter.drawImage(target,image_);
    }
private:QImage image_;
};
class MaterialFlatInputGuard final:public QObject {
public:
    std::function<bool()> flat;
    explicit MaterialFlatInputGuard(QObject* parent):QObject(parent){}
protected:
    bool eventFilter(QObject*,QEvent* event)override {
        if(!flat||!flat())return false;
        switch(event->type()) {
        case QEvent::MouseButtonPress:case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick:case QEvent::MouseMove:case QEvent::Wheel:
            event->accept();return true;
        default:return false;
        }
    }
};
InputModifiers modifiers(Qt::KeyboardModifiers value){InputModifiers result{};if(value.testFlag(Qt::ShiftModifier))result|=InputModifier::Shift;if(value.testFlag(Qt::ControlModifier))result|=InputModifier::Control;if(value.testFlag(Qt::AltModifier))result|=InputModifier::Alt;if(value.testFlag(Qt::MetaModifier))result|=InputModifier::Meta;if(value.testFlag(Qt::GroupSwitchModifier))result|=InputModifier::AltGraph;return result;}
PhysicalKeyCode scan(const QKeyEvent& event){
    switch(event.nativeScanCode()&0xff){case 0x11:return PhysicalKeyCode::W;case 0x1e:return PhysicalKeyCode::A;case 0x1f:return PhysicalKeyCode::S;case 0x20:return PhysicalKeyCode::D;case 0x39:return PhysicalKeyCode::Space;case 0x01:return PhysicalKeyCode::Escape;case 0x2a:return PhysicalKeyCode::LeftShift;case 0x36:return PhysicalKeyCode::RightShift;default:break;}
    switch(event.key()){case Qt::Key_W:return PhysicalKeyCode::W;case Qt::Key_A:return PhysicalKeyCode::A;case Qt::Key_S:return PhysicalKeyCode::S;case Qt::Key_D:return PhysicalKeyCode::D;case Qt::Key_Space:return PhysicalKeyCode::Space;case Qt::Key_Escape:return PhysicalKeyCode::Escape;case Qt::Key_Shift:return PhysicalKeyCode::LeftShift;case Qt::Key_F:return PhysicalKeyCode::F;case Qt::Key_F1:return PhysicalKeyCode::F1;case Qt::Key_F5:return PhysicalKeyCode::F5;case Qt::Key_F6:return PhysicalKeyCode::F6;case Qt::Key_F7:return PhysicalKeyCode::F7;case Qt::Key_P:return PhysicalKeyCode::P;case Qt::Key_Tab:return PhysicalKeyCode::Tab;default:return PhysicalKeyCode::Unknown;}
}
char32_t logicalKey(const QKeyEvent& event){switch(event.key()){case Qt::Key_Escape:return LogicalKeyCode::Escape;case Qt::Key_Tab:return LogicalKeyCode::Tab;case Qt::Key_F1:return LogicalKeyCode::F1;case Qt::Key_F5:return LogicalKeyCode::F5;case Qt::Key_F6:return LogicalKeyCode::F6;case Qt::Key_F7:return LogicalKeyCode::F7;default:break;}const auto text=event.text().toUcs4();return text.isEmpty()?LogicalKeyCode::Unknown:char32_t(text.front());}
MouseButton button(Qt::MouseButton value){switch(value){case Qt::LeftButton:return MouseButton::Left;case Qt::RightButton:return MouseButton::Right;case Qt::MiddleButton:return MouseButton::Middle;case Qt::BackButton:return MouseButton::Back;case Qt::ForwardButton:return MouseButton::Forward;default:return MouseButton::None;}}
class EngineCommandFilter final:public QObject {
public:
    std::function<bool(QObject*,QEvent*)> callback;
    bool eventFilter(QObject* target,QEvent* event)override{return callback&&callback(target,event);}
};
// Reuses Quader's raw/fallback stream and Qt capture postcondition exactly;
// these widgets only adapt that stream to the Engine's pure input vocabulary.
class LevelViewport final:public QWidget {
public:
    LevelViewport(quader::RenderHost& host,bool game,QWidget* parent=nullptr):QWidget(parent),game_(game){
        static std::atomic_uint32_t identities{1};id_=identities.fetch_add(1);
        setFocusPolicy(Qt::StrongFocus);setMouseTracking(true);setAcceptDrops(!game);setMinimumSize(128,96);
        auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);
        composition=new quader::QtVulkanCompositionWidget(this,host.composition_api());composition->set_render_system(&host);composition->setFocusPolicy(Qt::NoFocus);composition->setAttribute(Qt::WA_TransparentForMouseEvents);layout->addWidget(composition);
        relative_.set_capture_lost_callback([this]{interrupt();});
    }
    ~LevelViewport()override{release(false);if(composition){composition->set_prepare_callback({});composition->set_render_callback({});composition->set_render_system(nullptr);}}
    quader::QtVulkanCompositionWidget* composition{};
    std::function<void(const InputEvent&)> dispatch;
    std::function<void()> changed;
    std::function<void(const std::vector<std::string>&,QPointF)> dropped;
    std::uint32_t inputId()const{return id_;}
    ViewportRegion region()const{return {0,0,float(width()),float(height()),underMouse()||captured_,false,id_};}
    QSize pixels()const{return {qRound(width()*devicePixelRatioF()),qRound(height()*devicePixelRatioF())};}
    bool capture(bool enabled) {
        if(!enabled){release(true);return true;}
        if(!hasFocus()||!isVisible()||!window()->isActiveWindow()||QApplication::activeModalWidget()||QApplication::activePopupWidget()){release(false);return false;}
        if(captured_)return true;
        restore_=QCursor::pos();grabMouse(Qt::BlankCursor);
        auto* native=windowHandle()?windowHandle():window()->windowHandle();
        const auto handle=native?reinterpret_cast<void*>(native->winId()):nullptr;
        if(QWidget::mouseGrabber()!=this||!quader::platform_pointer_capture_transition_achieved(true,quader::platform_pointer_capture_postcondition(handle,true))){releaseMouse();return false;}
        captured_=true;last_=QPointF(mapFromGlobal(QCursor::pos()));(void)relative_.start_raw_input(handle);return true;
    }
    void drain() {
        if(!captured_)return;const auto delta=relative_.take_delta();
        if((delta.x||delta.y)&&dispatch){InputEvent event{};event.type=InputEventType::MouseMotion;event.surface=id_;event.relative={delta.x,delta.y};dispatch(event);}
        if(relative_.capture_requires_recenter()) {
            const QPoint center(width()/2,height()/2);const auto global=mapToGlobal(center);
            if(QCursor::pos()!=global){last_=QPointF(center);suppress_=center;QCursor::setPos(global);}
        }
    }
protected:
    bool event(QEvent* event)override{
        if(event->type()==QEvent::ShortcutOverride){const auto* key=static_cast<QKeyEvent*>(event);if(key->key()!=Qt::Key_F4||!(key->modifiers()&Qt::AltModifier)){event->accept();return true;}}
        if(event->type()==QEvent::FocusOut||event->type()==QEvent::Hide||event->type()==QEvent::ParentChange||event->type()==QEvent::WinIdChange||event->type()==QEvent::DevicePixelRatioChange){interrupt();}
        if(event->type()==QEvent::Resize||event->type()==QEvent::Show||event->type()==QEvent::DevicePixelRatioChange){if(changed)changed();}
        if(event->type()==QEvent::KeyPress||event->type()==QEvent::KeyRelease){auto& key=*static_cast<QKeyEvent*>(event);if(key.key()==Qt::Key_F4&&(key.modifiers()&Qt::AltModifier))return QWidget::event(event);InputEvent packet{};packet.type=event->type()==QEvent::KeyPress?InputEventType::KeyDown:InputEventType::KeyUp;packet.surface=id_;packet.physical=scan(key);packet.logical=logicalKey(key);packet.modifiers=modifiers(key.modifiers());packet.repeat=key.isAutoRepeat();if(dispatch)dispatch(packet);event->accept();return true;}
        if(event->type()==QEvent::MouseButtonPress||event->type()==QEvent::MouseButtonRelease){const auto& mouse=*static_cast<QMouseEvent*>(event);if(event->type()==QEvent::MouseButtonPress)setFocus(Qt::MouseFocusReason);InputEvent packet{};packet.type=event->type()==QEvent::MouseButtonPress?InputEventType::MouseButtonDown:InputEventType::MouseButtonUp;packet.surface=id_;packet.button=button(mouse.button());packet.position={float(mouse.position().x()),float(mouse.position().y())};packet.modifiers=modifiers(mouse.modifiers());if(dispatch)dispatch(packet);last_=mouse.position();if(changed)changed();event->accept();return true;}
        if(event->type()==QEvent::MouseMove){const auto position=static_cast<QMouseEvent*>(event)->position();if(captured_){if(suppress_&&position==*suppress_)suppress_.reset();else relative_.add_fallback_delta({float(position.x()-last_.x()),float(position.y()-last_.y())});}else if(dispatch){InputEvent packet{};packet.type=InputEventType::MouseMotion;packet.surface=id_;packet.position={float(position.x()),float(position.y())};packet.relative={float(position.x()-last_.x()),float(position.y()-last_.y())};packet.modifiers=modifiers(static_cast<QMouseEvent*>(event)->modifiers());dispatch(packet);}last_=position;event->accept();return true;}
        if(event->type()==QEvent::Wheel){const auto& wheel=*static_cast<QWheelEvent*>(event);InputEvent packet{};packet.type=InputEventType::MouseWheel;packet.surface=id_;packet.wheelY=float(wheel.angleDelta().y())/120.f;packet.modifiers=modifiers(wheel.modifiers());if(dispatch)dispatch(packet);if(changed)changed();event->accept();return true;}
        if(event->type()==QEvent::DragEnter){auto& drag=*static_cast<QDragEnterEvent*>(event);if(drag.mimeData()->hasFormat(QtAssetMime)){drag.acceptProposedAction();return true;}}
        if(event->type()==QEvent::Drop){auto& drop=*static_cast<QDropEvent*>(event);if(drop.mimeData()->hasFormat(QtAssetMime)&&dropped){try{std::vector<std::string> ids;for(const auto& row:nlohmann::json::parse(drop.mimeData()->data(QtAssetMime).toStdString()))if(!row.value("folder",false)){const auto kind=static_cast<AssetKind>(row.at("kind").get<unsigned>());if(kind==AssetKind::Model||kind==AssetKind::Material||kind==AssetKind::Decal||kind==AssetKind::Particle)ids.push_back(row.at("id").get<std::string>());}if(!ids.empty()){dropped(ids,drop.position());setFocus(Qt::MouseFocusReason);drop.acceptProposedAction();return true;}}catch(const std::exception&){} }}
        return QWidget::event(event);
    }
private:
    void release(bool restore){const bool was=captured_;captured_=false;(void)relative_.stop_raw_input();relative_.clear();if(QWidget::mouseGrabber()==this)releaseMouse();if(was&&restore&&hasFocus()&&window()->isActiveWindow())QCursor::setPos(restore_);suppress_.reset();}
    void interrupt(){release(false);if(dispatch){InputEvent event{};event.type=InputEventType::SurfaceFocusLost;event.surface=id_;dispatch(event);}}
    quader::QtRelativePointerInput relative_;
    std::uint32_t id_{};bool game_{},captured_{};QPoint restore_;QPointF last_;std::optional<QPointF> suppress_;
};
}

struct LevelPanels::Impl {
    QMainWindow& main;quader::RenderHost& host;quader::QuaderEditorApi& editor;quader::ProjectSession& project;
    std::function<void()> request,beforePlay;std::function<void(quader::SceneDocument&)> observeNativeAssets;
    LevelSession* session{};std::unique_ptr<NativeSceneAuthoring> authoring;
    QMenu* menu{};QMenu* ownedCreateMenu{};QToolBar* toolbar{};QDockWidget* gameDock{};LevelViewport* game{};QtInspector* inspector{};
    std::unique_ptr<LevelAssetBrowser> browser;std::unique_ptr<LevelPreferences> preferences;
    std::unique_ptr<AssetDocuments> documents;std::unique_ptr<LevelDocumentPreviews> documentPreviews;
    QPointer<quader::QtMaterialPreviewSurface> materialPreview;
    QPointer<QWidget> materialPreviewContainer;QPointer<QMainWindow> materialPreviewWindow;
    QPointer<QStackedWidget> previewPages;QPointer<MaterialImageView> previewImage;
    QPointer<QAction> previewTwoD,previewThreeD;
    QPointer<QComboBox> previewMeshes,previewBlendLayer,previewBlendChannel;
    std::shared_ptr<const MaterialAsset> preview_material;
    MaterialPreviewMode previewMode{MaterialPreviewMode::ThreeD};MaterialDocument* previewDocument{};
    std::string previewAsset;std::uint64_t previewRevision{};std::optional<MaterialGraphPreviewTarget> preview_target;
    QAction *playAction{},*pauseAction{},*stepAction{},*stopAction{};QLabel* status{};
    std::string commandStatus;
    std::pair<std::string,std::string> pendingRagdoll;
    EngineCommandFilter commands;bool preparing{},wasPlaying{},gameFocusPending{},viewportSelectionPending{};quader::SceneSession* presentedNative{};
    std::uint64_t updateEpoch{};
    using PreparedKey=std::tuple<std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,bool,bool,bool,Mat4,Mat4>;
    std::optional<PreparedKey> preparedKey;
    std::vector<QPointer<QAction>> createActions;

    Impl(QMainWindow& window,quader::RenderHost& render,quader::QuaderEditorApi& api,quader::ProjectSession& owner,std::function<void()> wake)
        :main(window),host(render),editor(api),project(owner),request(std::move(wake)){
        menu=main.menuBar()->addMenu("Game");menu->setObjectName("gameMenu");
        toolbar=new QToolBar("Play",&main);toolbar->setObjectName("enginePlayToolbar");main.addToolBar(toolbar);
        playAction=toolbar->addAction("Play");pauseAction=toolbar->addAction("Pause");stepAction=toolbar->addAction("Step");stopAction=toolbar->addAction("Stop");
        playAction->setObjectName("engineActionPlay");pauseAction->setObjectName("engineActionPause");stepAction->setObjectName("engineActionStep");stopAction->setObjectName("engineActionStop");
        for(auto* action:{playAction,pauseAction,stepAction,stopAction})menu->addAction(action);
        QObject::connect(playAction,&QAction::triggered,toolbar,[this]{launchPlay();});
        QObject::connect(pauseAction,&QAction::triggered,toolbar,[this]{if(session&&session->setPaused(!session->paused())&&!session->paused())focusGame();refresh();});
        QObject::connect(stepAction,&QAction::triggered,toolbar,[this]{if(session)session->step();refresh();});
        QObject::connect(stopAction,&QAction::triggered,toolbar,[this]{if(session)session->stop();gameFocusPending=false;commandStatus.clear();if(authoring)authoring->synchronize();refresh();if(request)request();});
        inspector=new QtInspector;inspector->setComponentsOnly(true);inspector->setObjectName("sceneEngineComponents");
        browser=std::make_unique<LevelAssetBrowser>(main,request,false);preferences=std::make_unique<LevelPreferences>(main,request);
        browser->setPrepareCommand([this]{return session&&!session->playing()&&!QApplication::activeModalWidget()&&settleTools()&&(!documents||documents->finishActive());});
        game=new LevelViewport(host,true);gameDock=new QDockWidget("Game",&main);gameDock->setObjectName("GameViewport");gameDock->setWidget(game);
        quader::install_palette_aware_dock_title_presentation(*gameDock);main.addDockWidget(Qt::RightDockWidgetArea,gameDock);gameDock->hide();menu->addAction(gameDock->toggleViewAction());
        auto* settings=menu->addAction("Preferences…");QObject::connect(settings,&QAction::triggered,menu,[this]{preferences->open();});
        status=new QLabel(toolbar);status->setObjectName("enginePlayStatus");status->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);status->setMaximumWidth(520);toolbar->addWidget(status);
        game->changed=request;game->dispatch=[this](const InputEvent& event){if(session){if(event.type==InputEventType::MouseButtonDown)session->gameInput().setViewport(game->region(),session->playing()&&!session->paused(),!game->hasFocus()||QApplication::activeModalWidget());session->gameInput().handleEvent(event);}};
        game->composition->set_render_callback([this](const auto& target){return session&&host.render_engine_viewport(true,target);});
        auto* create=main.menuBar()->findChild<QMenu*>("menu_create");
        if(!create){
            ownedCreateMenu=new QMenu("Create",&main);ownedCreateMenu->setObjectName("menu_create");create=ownedCreateMenu;
            const auto* object=main.menuBar()->findChild<QMenu*>("menu_object");
            main.menuBar()->insertMenu(object?object->menuAction():menu->menuAction(),create);
        }
        if(create){
            if(!create->actions().empty())createActions.push_back(create->addSeparator());
            const auto add=[this,create](const char* label,std::function<void(SceneDocument&)> command){auto* action=create->addAction(label);createActions.push_back(action);QObject::connect(action,&QAction::triggered,create,[this,command]{if(session&&!session->playing()&&settleTools()){if(authoring)authoring->synchronize();command(session->document());if(authoring)authoring->synchronize();refresh();if(request)request();}});};
            add("Point Light",[](auto& scene){scene.createPointLight();});add("Directional Light",[](auto& scene){scene.createDirectionalLight();});add("Spot Light",[](auto& scene){scene.createSpotLight();});
            add("Player",[](auto& scene){scene.createPlayer();});add("Enemy",[](auto& scene){scene.createEnemy();});add("Camera",[](auto& scene){scene.createCamera();});
            add("Environment",[](auto& scene){scene.createEnvironment();});add("Collider",[](auto& scene){scene.createCollider();});add("Navigation Volume",[](auto& scene){scene.createNavigationVolume();});
        }
        commands.callback=[this](QObject* object,QEvent* event){
            if(!session)return false;
            if(event->type()==QEvent::Show){if(auto* widget=qobject_cast<QWidget*>(object);nativePane(widget))widget->setAcceptDrops(true);}
            if(event->type()==QEvent::DragEnter||event->type()==QEvent::DragMove||event->type()==QEvent::Drop)return nativeDrop(object,event);
            if(event->type()==QEvent::MouseButtonPress){auto* mouse=static_cast<QMouseEvent*>(event);auto* widget=qobject_cast<QWidget*>(object);if(mouse->button()==Qt::LeftButton&&!session->playing()&&authoring&&authoring->session()&&nativePane(widget)&&editor.mode().current()==quader::QuaderEditorMode::Object&&editor.viewports().selected_tool(nativePaneId(widget))==quader::ViewportToolId::Select){if(settleTools()){auto& native=*authoring->session();const auto pane=nativePaneId(widget);const auto camera=viewCamera(native,pane);const auto size=native.viewport_workspace().camera(pane).viewport_size();const auto point=localPoint(widget,mouse->position(),pane);viewportSelectionPending=session->selectViewport({float(point.x()),float(point.y())},camera,{size.x,size.y},mouse->modifiers().testFlag(Qt::ControlModifier),mouse->modifiers().testFlag(Qt::ShiftModifier));}}return false;}
            if(!ownsInput()||QApplication::activeModalWidget()||QApplication::activePopupWidget())return false;
            if(event->type()!=QEvent::ShortcutOverride&&event->type()!=QEvent::KeyPress)return false;
            const auto& key=*static_cast<QKeyEvent*>(event);if(key.key()!=Qt::Key_Escape||key.modifiers()!=Qt::ShiftModifier)return false;
            if(event->type()==QEvent::KeyPress&&!key.isAutoRepeat())stopAction->trigger();event->accept();return true;
        };
        QApplication::instance()->installEventFilter(&commands);refresh();
    }
    ~Impl(){QApplication::instance()->removeEventFilter(&commands);bind(nullptr);for(const auto& action:createActions)if(action)delete action;delete ownedCreateMenu;delete inspector;delete menu;delete toolbar;delete gameDock;}
    bool settleTools(){commandStatus.clear();if(!inspector->finish()){commandStatus=session&&!session->document().status().empty()?session->document().status():"Finish the active component edit before starting another command.";return false;}if(authoring&&!authoring->finish()){commandStatus=session&&!session->document().status().empty()?session->document().status():"The scene edit could not be accepted.";return false;}const auto result=editor.tools().activate("select");if(!result.ok())commandStatus=result.error_message();return result.ok();}
    void launchPlay(){
        if(!session)return;
        try {
            if(!settleTools()){refresh();if(request)request();return;}
            if(authoring)authoring->synchronize();
            if(beforePlay)beforePlay();
            if(session->play()||session->playPending()){
                gameDock->show();gameDock->raise();gameFocusPending=true;
                if(session->playing())focusGame();
            }else commandStatus=session->status().empty()?"Cannot enter Play.":session->status();
        }catch(const std::exception& error){commandStatus=error.what();}
        refresh();if(request)request();
    }
    bool ownsInput()const{if((browser&&browser->ownsInput())||(preferences&&preferences->ownsInput()))return true;auto* focus=QApplication::focusWidget();return focus&&((gameDock&&(gameDock==focus||gameDock->isAncestorOf(focus)))||(inspector&&(inspector==focus||inspector->isAncestorOf(focus))));}
    void focusGame(){if(!session||!session->playing()||session->paused())return;gameDock->show();gameDock->raise();gameDock->window()->activateWindow();game->setFocus(Qt::OtherFocusReason);session->gameInput().setViewport(game->region(),resolveGameCamera(session->presentationScene()).has_value(),false);session->gameInput().acquire();}
    static QWidget* nativePane(QWidget* widget){for(auto* current=widget;current;current=current->parentWidget())if(current->objectName().startsWith("viewportPane_"))return current;return nullptr;}
    static quader::ViewportPaneId nativePaneId(QWidget* widget){const auto* pane=nativePane(widget);const auto name=pane?pane->objectName():QString{};if(name.endsWith("top"))return quader::ViewportPaneId::Top;if(name.endsWith("front"))return quader::ViewportPaneId::Front;if(name.endsWith("side"))return quader::ViewportPaneId::Side;return quader::ViewportPaneId::Perspective;}
    QPointF localPoint(QWidget* widget,QPointF point,quader::ViewportPaneId pane)const{if(!widget)return point;const auto suffix=QString::fromLatin1(pane==quader::ViewportPaneId::Top?"top":pane==quader::ViewportPaneId::Front?"front":pane==quader::ViewportPaneId::Side?"side":"perspective");if(auto* surface=main.findChild<QWidget*>("viewportSurfaceContainer_"+suffix))return QPointF(surface->mapFromGlobal(widget->mapToGlobal(point.toPoint())));return point;}
    static Camera viewCamera(const quader::SceneSession& native,quader::ViewportPaneId pane){const auto& source=native.viewport_workspace().camera(pane);const auto pose=source.pose();const auto& state=source.state();Camera camera;camera.position={pose.eye.x,pose.eye.y,pose.eye.z};camera.target={pose.target.x,pose.target.y,pose.target.z};camera.up={pose.up.x,pose.up.y,pose.up.z};camera.fov=state.fov_degrees;camera.fovAxis=CameraFovAxis::Vertical;camera.nearPlane=state.near_plane;camera.farPlane=state.far_plane;camera.projection=state.projection==quader::ViewportProjection::Orthographic?CameraProjection::Orthographic:CameraProjection::Perspective;camera.orthographicHeight=state.orthographic_size;return camera;}
    bool nativeDrop(QObject* object,QEvent* event){
        auto* widget=qobject_cast<QWidget*>(object);if(!nativePane(widget)||session->playing()||!authoring||!authoring->session())return false;
        auto* drop=static_cast<QDropEvent*>(event);if(!drop->mimeData()->hasFormat(QtAssetMime))return false;
        if(event->type()!=QEvent::Drop){drop->acceptProposedAction();return true;}
        if(!settleTools())return true;
        try{std::vector<std::string> ids;for(const auto& item:nlohmann::json::parse(drop->mimeData()->data(QtAssetMime).toStdString()))if(!item.value("folder",false))ids.push_back(item.at("id").get<std::string>());
            auto& native=*authoring->session();const auto pane=nativePaneId(widget);(void)editor.viewports().focus_pane(pane);const auto size=native.viewport_workspace().camera(pane).viewport_size();const auto point=localPoint(widget,drop->position(),pane);session->dropAssets(ids,{float(point.x()),float(point.y())},viewCamera(native,pane),{size.x,size.y});drop->acceptProposedAction();if(request)request();
        }catch(const std::exception& error){main.statusBar()->showMessage(QString::fromUtf8(error.what()),10000);}return true;
    }
    void releaseMaterialPreview(){previewDocument=nullptr;preview_target.reset();preview_material.reset();if(materialPreview){materialPreview->set_record_callback({});materialPreview->set_surface_changed_callback({});materialPreview->shutdown();}delete materialPreviewContainer.data();materialPreviewContainer=nullptr;materialPreview=nullptr;materialPreviewWindow=nullptr;previewPages=nullptr;previewImage=nullptr;previewTwoD=nullptr;previewThreeD=nullptr;previewMeshes=nullptr;previewBlendLayer=nullptr;previewBlendChannel=nullptr;previewAsset.clear();previewRevision=0;previewMode=MaterialPreviewMode::ThreeD;if(session)session->clearMaterialPreview();}
    void bindMaterialPreview(MaterialDocument* document,QMainWindow* window){
        // Losing focus retains the previously composed texture, but relinquishes
        // all access to the draft before its owner is allowed to close it.
        previewDocument=nullptr;
        if(!document||!window)return;
        if(!materialPreview||materialPreviewWindow!=window||previewAsset!=document->assetId()) {
            releaseMaterialPreview();materialPreviewWindow=window;previewAsset=document->assetId();
            auto* container=new QWidget(window);materialPreviewContainer=container;
            auto* layout=new QVBoxLayout(container);layout->setContentsMargins(0,0,0,0);layout->setSpacing(0);
            auto* controls=new QToolBar(container);layout->addWidget(controls);
            auto* surface=new quader::QtMaterialPreviewSurface;materialPreview=surface;surface->QObject::setParent(container);
            if(document->definition().blendDefinition) {
                auto* layers=new QComboBox(controls);previewBlendLayer=layers;layers->setObjectName(QStringLiteral("CrimsonBlendPreviewLayer"));layers->addItems({"Base","Red","Green"});
                const auto& definition=*document->definition().blendDefinition;const auto& fourth=definition.layers[3];if((!definition.preservedExpressions.empty()&&fourth.referenceNode)||fourth.enabled||!fourth.material.empty())layers->addItem("Blue");controls->addWidget(layers);
                auto* channels=new QComboBox(controls);previewBlendChannel=channels;channels->setObjectName(QStringLiteral("CrimsonBlendPreviewChannel"));channels->addItems({"Full material","Base Color","Roughness","Metallic","Normal","Occlusion","Height","Emission"});controls->addWidget(channels);
                const auto select=[this,layers,channels,surface]{if(!documents)return;BlendMaterialPreviewSelection selection;selection.layer=std::size_t(layers->currentIndex());if(channels->currentIndex()>0)selection.channel=static_cast<BlendMaterialChannel>(channels->currentIndex()-1);if(documents->setBlendMaterialPreview(previewAsset,selection)){surface->mark_dirty();updateMaterialPreview();if(request)request();}};
                QObject::connect(layers,&QComboBox::activated,surface,[select](int){select();});QObject::connect(channels,&QComboBox::activated,surface,[select](int){select();});
            }
            auto* modes=new QActionGroup(controls);modes->setExclusive(true);
            const auto modeAction=[this,controls,modes,surface](const char* name,MaterialPreviewMode mode) {
                auto* action=controls->addAction(name);action->setObjectName(QStringLiteral("CrimsonMaterialPreview")+QString::fromLatin1(name));action->setCheckable(true);action->setVisible(false);modes->addAction(action);
                QObject::connect(action,&QAction::triggered,surface,[this,surface,mode]{if(!documents||!documents->setMaterialPreviewMode(previewAsset,mode))return;QEvent cancel(QEvent::UngrabMouse);QApplication::sendEvent(surface,&cancel);surface->mark_dirty();updateMaterialPreview();if(request)request();});return action;
            };
            previewTwoD=modeAction("2D",MaterialPreviewMode::TwoD);previewThreeD=modeAction("3D",MaterialPreviewMode::ThreeD);
            auto* meshes=new QComboBox(controls);previewMeshes=meshes;meshes->addItems({"Sphere","Box","Rounded Box","Cylinder","Plane"});controls->addWidget(meshes);
            QObject::connect(meshes,&QComboBox::currentIndexChanged,surface,[surface](int value){surface->set_preview_mesh(static_cast<quader::MaterialPreviewMeshKind>(value));});
            auto* environments=new QComboBox(controls);for(const auto& value:quader::scene_environment_catalog())environments->addItem(QString::fromUtf8(value.fallback_label.data(),int(value.fallback_label.size())),QString::fromUtf8(value.id.data(),int(value.id.size())));controls->addWidget(environments);
            QObject::connect(environments,&QComboBox::currentIndexChanged,surface,[surface,environments](int){surface->set_preview_environment(environments->currentData().toString().toStdString());});
            auto* background=controls->addAction("Background");background->setCheckable(true);QObject::connect(background,&QAction::toggled,surface,[surface](bool value){surface->set_show_environment_background(value);});
            auto* reset=controls->addAction("Reset");QObject::connect(reset,&QAction::triggered,surface,[surface]{surface->reset_preview();});
            auto* pages=new QStackedWidget(container);previewPages=pages;layout->addWidget(pages,1);
            auto* composed=surface->create_composed_widget(pages,host);pages->addWidget(composed);
            auto* image=new MaterialImageView(pages);image->setObjectName(QStringLiteral("CrimsonMaterialNodeImage"));previewImage=image;pages->addWidget(image);
            auto* inputGuard=new MaterialFlatInputGuard(composed);inputGuard->flat=[this]{return documents&&documents->materialPreviewTarget(previewAsset)&&documents->materialPreviewMode(previewAsset)==MaterialPreviewMode::TwoD;};composed->installEventFilter(inputGuard);
            surface->set_surface_changed_callback(request);
            surface->set_record_callback([this](const quader::MaterialPreviewState& state,std::uint32_t width,std::uint32_t height){
                if(!session||!previewDocument||!previewDocument->isOpen())return false;
                SceneSettings settings;const auto pan=state.pan_offset();settings.camera.position={pan.x,pan.y,state.camera_distance()};settings.camera.target={pan.x,pan.y,0};
                settings.camera.nearPlane=.05f;settings.camera.farPlane=100;settings.camera.fov=45;settings.camera.fovAxis=CameraFovAxis::Vertical;
                settings.environment.rotation=state.environment_rotation_radians()*180.f/std::numbers::pi_v<float>;
                settings.environment.background.show=state.show_environment_background();settings.environment.background.overrideColor=!settings.environment.background.show;settings.environment.background.color={.08f,.08f,.08f};
                settings.environment.toneMapping=ToneMapping::ACES;settings.antiAliasing=preferences?preferences->values().antiAliasing:AntiAliasing::Smaa;
                const std::array meshes{MaterialPreviewMesh::Sphere,MaterialPreviewMesh::Cube,MaterialPreviewMesh::RoundedCube,MaterialPreviewMesh::Cylinder,MaterialPreviewMesh::Plane};
                const auto target=documents->materialPreviewTarget(previewAsset);
                const auto material=documents->materialPreviewMaterial(previewAsset);
                if(target&&previewDocument->definition().blendDefinition&&!material)return false;
                const bool flat=target&&documents->materialPreviewMode(previewAsset)==MaterialPreviewMode::TwoD;
                if(flat){settings.camera=Camera{};settings.camera.position={0,0,2.6f};settings.camera.target={};settings.camera.nearPlane=.1f;settings.camera.farPlane=30;settings.environment.background.show=false;}
                const auto q=state.object_rotation();return session->recordMaterialPreview(material?*material:previewDocument->definition(),settings,flat?MaterialPreviewMesh::Plane:meshes.at(static_cast<unsigned>(state.mesh_kind())),flat?Mat4{}:math::rotation(Quat{q.x,q.y,q.z,q.w}),width,height,!flat&&state.show_environment_background(),true,target);
            });
            if(!surface->initialize(host)||!documents->setPreview(AssetKind::Material,document->assetId(),container)){releaseMaterialPreview();return;}
        }
        previewDocument=document;previewRevision=UINT64_MAX;materialPreview->mark_dirty();
    }
    void updateMaterialPreview(){
        if(!previewDocument||!materialPreview||!previewPages||!documents)return;
        const auto target=documents->materialPreviewTarget(previewAsset),previousTarget=preview_target;
        const auto material=documents->materialPreviewMaterial(previewAsset);
        const auto mode=documents->materialPreviewMode(previewAsset);const bool flat=target&&mode==MaterialPreviewMode::TwoD;
        if(previewTwoD){previewTwoD->setVisible(bool(target));previewTwoD->setChecked(mode==MaterialPreviewMode::TwoD);}
        if(previewThreeD){previewThreeD->setVisible(bool(target));previewThreeD->setChecked(mode==MaterialPreviewMode::ThreeD);}
        if(previewMeshes)previewMeshes->setVisible(!flat);
        if(const auto blend=documents->blendMaterialPreview(previewAsset)){if(previewBlendLayer){const QSignalBlocker block(previewBlendLayer);previewBlendLayer->setCurrentIndex(int(blend->layer));}if(previewBlendChannel){const QSignalBlocker block(previewBlendChannel);previewBlendChannel->setCurrentIndex(blend->channel?int(*blend->channel)+1:0);}}
        if(previewRevision!=previewDocument->previewRevision()||preview_target!=target||previewMode!=mode||preview_material!=material){previewRevision=previewDocument->previewRevision();preview_target=target;previewMode=mode;preview_material=material;materialPreview->mark_dirty();}
        if(previousTarget!=target&&previewImage)previewImage->clearImage();
        if(flat&&documents->materialNodeImageSupported(previewAsset,*target)) {
            previewPages->setCurrentIndex(1);previewImage->setImage(documents->materialNodeImage(previewAsset,*target,256));return;
        }
        previewPages->setCurrentIndex(0);(void)materialPreview->render_frame();
    }
    void processBrowserActions(){if(!session||!browser)return;const auto actions=browser->takeActions();
        for(const auto& id:{actions.openMaterial,actions.openParticle,actions.openAnimation,actions.openRagdoll,actions.openDecal,actions.openNavigationAgentProfile,actions.openGameplayEffect})if(!id.empty())openAsset(id);
        if(session->playing()){pendingRagdoll={};return;}
        const auto openCreated=[this](AssetKind kind,const std::string& id,const std::string& folder){if(id.empty())return;if(!folder.empty()){const std::array<std::string,1> ids{id};session->assets().browserCatalog().move(ids,folder,true);}browser->reveal(id);if(documents)documents->open(kind,id);};
        try {
            if(actions.createMaterial||actions.createBlendMaterial){settleTools();inspector->finish();const auto asset=actions.createBlendMaterial?session->assets().createBlendMaterial():session->assets().createMaterial();if(asset)openCreated(AssetKind::Material,asset->id,actions.materialFolder);}
            if(actions.createParticle){settleTools();inspector->finish();const auto asset=createParticleAsset(session->projectRoot());session->assets().noteAssetWrite(asset.id);session->assets().refreshParticles();session->assets().publishParticle(std::make_shared<const ParticleEffectAsset>(asset));openCreated(AssetKind::Particle,asset.id,actions.particleFolder);}
            if(actions.createDecal){settleTools();inspector->finish();if(const auto asset=session->assets().createDecal())openCreated(AssetKind::Decal,asset->id,actions.decalFolder);}
            if(actions.createNavigationAgentProfile){settleTools();inspector->finish();if(const auto asset=session->assets().createNavigationAgentProfile())openCreated(AssetKind::NavigationAgentProfile,asset->id,actions.navigationAgentProfileFolder);}
            if(actions.createGameplayEffect){settleTools();inspector->finish();if(const auto asset=session->assets().createGameplayEffect())openCreated(AssetKind::GameplayEffect,asset->id,actions.gameplayEffectFolder);}
            if(!actions.createRagdollModel.empty()){if(!settleTools())return;pendingRagdoll={actions.createRagdollModel,actions.ragdollFolder};}
            if(!pendingRagdoll.first.empty()){
                auto& assets=session->assets();const auto model=assets.findModel(pendingRagdoll.first);
                if(!model&&assets.modelLoading(pendingRagdoll.first)){main.statusBar()->showMessage("Preparing Ragdoll source…");if(request)request();}
                else {
                    const auto pending=std::exchange(pendingRagdoll,{});
                    if(!model)throw std::runtime_error(assets.modelDiagnostic(pending.first).empty()?"Ragdoll source model is unavailable.":assets.modelDiagnostic(pending.first));
                    if(!settleTools())return;
                    if(const auto asset=assets.createRagdoll(pending.first,{},pending.second)){main.statusBar()->clearMessage();openCreated(AssetKind::Ragdoll,asset->id,{});}
                }
            }
        }catch(const std::exception& error){main.statusBar()->showMessage(QString::fromUtf8(error.what()),10000);}
    }

    void bind(LevelSession* value){
        if(session==value)return;inspector->detach();releaseMaterialPreview();documentPreviews.reset();documents.reset();authoring.reset();if(browser)browser->bind(nullptr);if(session)session->gameInput().setMouseCapture({});session=value;commandStatus.clear();pendingRagdoll={};gameFocusPending=wasPlaying=false;
        if(browser)browser->bind(session);if(!session)return;
        authoring=std::make_unique<NativeSceneAuthoring>(*session,project);authoring->set_retention_observer(observeNativeAssets);documents=std::make_unique<AssetDocuments>(main,*session,request);documentPreviews=std::make_unique<LevelDocumentPreviews>(*documents,*session,host,request);
        documents->setActiveMaterialChanged([this](MaterialDocument* document,QMainWindow* window){bindMaterialPreview(document,window);});
        session->gameInput().setMouseCapture([this](InputSurfaceId id,bool enabled){return id==game->inputId()&&game->capture(enabled);});refresh();
    }
    void openAsset(const std::string& id){if(!session||!documents)return;const auto& catalog=session->assets().catalog();const auto found=std::find_if(catalog.begin(),catalog.end(),[&](const auto& entry){return entry.id==id;});if(found!=catalog.end()&&found->kind!=AssetKind::Scene)documents->open(found->kind,id);}
    void refreshInspector(){
        if(!session||!authoring||!authoring->session()){inspector->hide();return;}
        auto& native=authoring->session()->scene();const auto id=native.primary_selection();const auto target=session->nativeProjection().entity_id(native.uuid(id));
        inspector->setVisible(target!=NoEntity);inspector->setEnabled(!session->playing());
        if(target&&host.crimson_renderer()){inspector->setComponentsOnly(true,native.light(id)!=nullptr);inspector->setCollisionLayerNames(defaultCollisionLayerNames());inspector->setNavigationPreview(&session->navigationProfile(),[this](const std::string& asset){return session->navigationStatus(asset);});inspector->synchronize(session->document(),session->assets(),*host.crimson_renderer(),target);}
        const auto actions=inspector->takeActions();for(const auto& asset:{actions.openMaterial,actions.openParticle,actions.openAnimation,actions.openRagdoll,actions.openDecal,actions.openNavigationAgentProfile,actions.openGameplayEffect})if(!asset.empty())openAsset(asset);
        if(!actions.createRagdollModel.empty()&&!session->playing()){pendingRagdoll={actions.createRagdollModel,{}};if(request)request();}
        if(actions.playParticleEntity)session->replayParticle(actions.playParticleEntity);
    }
    void refresh(){toolbar->setEnabled(session!=nullptr);if(!session)return;playAction->setEnabled(!session->playing()&&!session->playPending());stopAction->setEnabled(session->playing()||session->playPending());pauseAction->setEnabled(session->playing());pauseAction->setText(session->paused()?"Resume":"Pause");stepAction->setEnabled(session->playing()&&session->paused());auto message=commandStatus.empty()?session->status():commandStatus;if(commandStatus.empty()&&session->playing()&&!resolveGameCamera(session->presentationScene()))message="No active Game camera. Create a Player or assign a Camera.";status->setText(QString::fromStdString(message));status->setToolTip(status->text());for(const auto& action:createActions)if(action)action->setEnabled(!session->playing());refreshInspector();if(documents)documents->update();updateMaterialPreview();}
};
LevelPanels::LevelPanels(QMainWindow& window,quader::RenderHost& host,quader::QuaderEditorApi& editor,quader::ProjectSession& project,std::function<void()> request):impl_(std::make_unique<Impl>(window,host,editor,project,std::move(request))){impl_->game->composition->set_prepare_callback([this]{return prepareFrame();});}
LevelPanels::~LevelPanels()=default;
void LevelPanels::bind(LevelSession* value){impl_->bind(value);}
void LevelPanels::setNativeAssetObserver(std::function<void(quader::SceneDocument&)> observer){impl_->observeNativeAssets=std::move(observer);if(impl_->authoring)impl_->authoring->set_retention_observer(impl_->observeNativeAssets);}
void LevelPanels::bindScene(quader::SceneSession* scene,std::string uuid){auto& state=*impl_;if(!state.authoring)return;state.authoring->bind(scene,std::move(uuid));state.authoring->synchronize();if(state.presentedNative!=scene){state.presentedNative=scene;for(auto* widget:state.main.findChildren<QWidget*>())if(Impl::nativePane(widget))widget->setAcceptDrops(true);}}
void LevelPanels::update(double seconds,bool active){auto& state=*impl_;if(!state.session)return;++state.updateEpoch;if(state.authoring)state.authoring->synchronize();const auto changes=state.session->takeFileChanges();if(state.documents&&!changes.empty())state.documents->filesChanged(changes);if(state.session->playing()&&!state.wasPlaying)state.gameFocusPending=true;if(active&&state.gameFocusPending&&state.session->playing()&&!state.session->paused()){state.focusGame();state.gameFocusPending=false;}state.wasPlaying=state.session->playing();state.game->drain();auto& input=state.session->gameInput();input.setViewport(state.game->region(),state.session->playing()&&!state.session->paused()&&resolveGameCamera(state.session->presentationScene()).has_value(),!active||!state.game->hasFocus()||QApplication::activeModalWidget());input.syncMouseCapture();state.browser->update(state.host.completed_frame(),false,state.preferences->values().browserPreviewSize);state.processBrowserActions();state.refresh();if(state.documentPreviews){state.documentPreviews->setAntiAliasing(state.preferences->values().antiAliasing);state.documentPreviews->update(seconds,active);}if(state.game->isVisible())state.game->composition->update();}
void LevelPanels::syncNativeSelection(){auto& state=*impl_;if(!state.viewportSelectionPending||!state.session||state.session->viewportRequestPending()||!state.authoring||!state.authoring->session())return;auto& native=state.authoring->session()->scene();std::vector<quader::EntityId> selected;for(const auto id:state.session->document().selection().ids){const auto entity=native.entity_for_uuid(state.session->nativeProjection().entity_uuid(id));if(entity!=quader::kInvalidEntityId)selected.push_back(entity);}(void)state.editor.selection().select_objects(selected);state.viewportSelectionPending=false;}
bool LevelPanels::prepareFrame(){
    auto& state=*impl_;if(!state.session||state.preparing)return false;const QScopedValueRollback preparing(state.preparing,true);
    try{
        const bool scene_lighting=state.editor.mode().workspace_mode()==quader::QuaderWorkspaceMode::Default;state.session->setSceneLighting(scene_lighting);
        LevelViewRequest request;const auto game=state.game->pixels();request.gameWidth=std::uint32_t(std::max(0,game.width()));request.gameHeight=std::uint32_t(std::max(0,game.height()));request.gameVisible=state.game->isVisible();request.sceneWidth=request.sceneHeight=1;request.sceneVisible=true;
        if(state.authoring&&state.authoring->session()){auto& native=*state.authoring->session();const auto pane=native.viewport_workspace().focused_pane();const auto size=native.viewport_workspace().camera(pane).viewport_size();request.sceneWidth=std::uint32_t(std::max(1.f,size.x));request.sceneHeight=std::uint32_t(std::max(1.f,size.y));request.sceneCamera=Impl::viewCamera(native,pane);}
        state.preferences->apply(request);const auto& document=state.session->editingDocument();const Impl::PreparedKey key{state.updateEpoch,document.documentGeneration(),document.scene().revision(),document.scene().poseRevision(),request.sceneWidth,request.sceneHeight,request.gameWidth,request.gameHeight,request.gameVisible,state.session->playing(),scene_lighting,request.sceneCamera.viewMatrix(),request.sceneCamera.projectionMatrix(float(request.sceneWidth)/float(request.sceneHeight),ClipDepth::ZeroToOne)};
        if(state.preparedKey==key)return true;auto frame=state.session->prepareRender(request);if(!frame)return false;state.host.publish_engine_frame(std::move(frame));state.host.publish_native_scene_bindings(state.session->nativeRenderBindings());state.preparedKey=key;return true;
    }catch(const std::exception& error){state.status->setText(QString::fromUtf8(error.what()));return false;}
}
bool LevelPanels::continuous()const{return impl_->session&&(impl_->session->playing()||(impl_->authoring&&impl_->authoring->session())||(impl_->previewDocument&&impl_->materialPreviewContainer&&impl_->materialPreviewContainer->isVisible())||(impl_->documentPreviews&&impl_->documentPreviews->continuous()));}
bool LevelPanels::ownsInput()const{return impl_->ownsInput();}
bool LevelPanels::navigating()const{return false;}
QWidget* LevelPanels::componentExtension()const{return impl_->inspector;}
std::shared_ptr<quader::QtAssetBrowserExtension> LevelPanels::assetBrowserExtension(){return impl_->browser->extension();}
void LevelPanels::setBeforePlay(std::function<void()> callback){impl_->beforePlay=std::move(callback);}
bool LevelPanels::confirmClose(){if(!impl_->inspector->finish())return false;if(impl_->authoring&&!impl_->authoring->finish())return false;if(impl_->session)impl_->session->stop();return !impl_->documents||impl_->documents->confirmClose();}
} // namespace crimson::integration
