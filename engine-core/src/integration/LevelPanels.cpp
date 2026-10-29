#include "LevelPanels.h"
#include "LevelSession.h"
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
#include "tools/ScenePanels.h"
#include "ui/qt_vulkan_composition_widget.hpp"
#include "ui/qt_relative_pointer_input.hpp"
#include "ui/qt_dock_title_presentation.hpp"
#include "ui/qt_panel_section_header.hpp"
#include "ui/qt_material_preview_surface.hpp"
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
class LevelTree final:public QTreeWidget {
public:
    using QTreeWidget::QTreeWidget;
    std::function<void(EntityId)> reparent;
protected:
    void dropEvent(QDropEvent* event)override{if(reparent){auto* target=itemAt(event->position().toPoint());reparent(target?target->data(0,Qt::UserRole).toULongLong():NoEntity);event->acceptProposedAction();}}
};
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
    QMainWindow& main;quader::RenderHost& host;std::function<void()> request;
    LevelSession* session{};QMenu* menu{};QToolBar* toolbar{};
    std::function<void(const std::filesystem::path&)> openProject;
    QDockWidget *sceneDock{},*gameDock{},*treeDock{},*inspectorDock{},*assetsDock{};
    LevelViewport *scene{},*game{};LevelTree* tree{};QtInspector* inspector{};QWidget* assets{};
    std::unique_ptr<LevelAssetBrowser> browser;
    std::unique_ptr<LevelPreferences> preferences;
    QLineEdit* search{};QComboBox* filter{};SceneOutlinerProjection outliner;
    std::unique_ptr<AssetDocuments> documents;
    std::unique_ptr<LevelDocumentPreviews> documentPreviews;
    std::unique_ptr<LevelVertexPaint> vertexPaint;
    LevelViewportTools* viewportTools{};
    CameraPilot pilot;
    std::unique_ptr<CameraBookmarks> bookmarks;
    QMenu *cameraMenu{},*bookmarkMenu{};
    QPointer<QMenu> contextMenu;
    QPoint contextPosition;
    bool autoPlayContext{};
    QPointer<quader::QtMaterialPreviewSurface> materialPreview;
    QPointer<QWidget> materialPreviewContainer;
    QPointer<QMainWindow> materialPreviewWindow;
    QPointer<QStackedWidget> previewPages;
    QPointer<MaterialImageView> previewImage;
    QPointer<QAction> previewTwoD,previewThreeD;
    QPointer<QComboBox> previewMeshes;
    QPointer<QComboBox> previewBlendLayer,previewBlendChannel;
    std::shared_ptr<const MaterialAsset> preview_material;
    MaterialPreviewMode previewMode{MaterialPreviewMode::ThreeD};
    MaterialDocument* previewDocument{};
    std::string previewAsset;
    std::uint64_t previewRevision{};
    std::optional<MaterialGraphPreviewTarget> preview_target;
    QAction *playAction{},*pauseAction{},*stepAction{},*stopAction{};QLabel* status{};
    Navigation navigation;Camera camera;
    std::uint64_t shownRevision{UINT64_MAX},shownCatalog{UINT64_MAX};EntityId shownSelection{UINT64_MAX};
    std::uint64_t shownGeneration{};
    std::filesystem::path modelingManifest;std::string modelingProject,modelingScene;
    bool updating{},preparing{},wasPlaying{},navigationVisible{};
    struct FrameKey {
        std::uint64_t epoch{},document{},generation{},revision{},pose{};
        Mat4 view,projection;std::vector<EntityId> selection;EntityId primary{};
        std::uint32_t sceneWidth{},sceneHeight{},gameWidth{},gameHeight{};
        bool sceneVisible{},gameVisible{},playing{},paused{},paintEnabled{},paintHeld{},subdivisions{},grid{};
        AntiAliasing antiAliasing{};ViewportAppearance appearance;std::uint16_t shadowResolution{};
        bool operator==(const FrameKey&)const=default;
    };
    std::uint64_t updateEpoch{};
    std::optional<FrameKey> preparedKey;
    EngineCommandFilter commands;
    Impl(QMainWindow& window,quader::RenderHost& render,std::function<void()> wake):main(window),host(render),request(std::move(wake)){
        menu=main.menuBar()->addMenu("Engine");toolbar=new QToolBar("Engine Play",&main);toolbar->setObjectName("enginePlayToolbar");main.addToolBar(toolbar);
        playAction=toolbar->addAction("Play");pauseAction=toolbar->addAction("Pause");stepAction=toolbar->addAction("Step");stopAction=toolbar->addAction("Stop");
        QObject::connect(playAction,&QAction::triggered,toolbar,[this]{settleTools();inspector->finish();if(session&&session->play())focusGame();refresh();});
        QObject::connect(pauseAction,&QAction::triggered,toolbar,[this]{if(session&&session->setPaused(!session->paused())&&!session->paused())focusGame();refresh();});
        QObject::connect(stepAction,&QAction::triggered,toolbar,[this]{if(session)session->step();refresh();});
        QObject::connect(stopAction,&QAction::triggered,toolbar,[this]{detachTools();inspector->detach();if(session)session->stop();invalidate();});
        const auto action=[this](const char* title,const std::function<void()>& command){auto* value=menu->addAction(title);QObject::connect(value,&QAction::triggered,menu,command);return value;};
        action("Open Engine Project…",[this]{if(!openProject)return;const auto folder=QFileDialog::getExistingDirectory(&main,"Open Engine Project",session?QString::fromStdWString(session->projectRoot().wstring()):QString{});if(!folder.isEmpty())openProject(std::filesystem::path(folder.toStdWString()));});
        action("New Level",[this]{if(session&&confirm())session->newLevel(true);invalidate();});
        action("Open Level…",[this]{if(!session||!confirm())return;QStringList names;std::vector<std::string> ids;for(const auto& value:session->assets().catalog())if(value.kind==AssetKind::Scene){names.push_back(QString::fromStdString(value.name));ids.push_back(value.id);}bool accepted{};const auto selected=QInputDialog::getItem(&main,"Open Level","Level",names,0,false,&accepted);if(accepted)session->openLevel(ids.at(static_cast<std::size_t>(names.indexOf(selected))),true);invalidate();});
        action("Save Level",[this]{save(false);});action("Save Level As…",[this]{save(true);});
        action("Import Files…",[this]{if(!session)return;const auto files=QFileDialog::getOpenFileNames(&main,"Import Engine Assets");std::vector<std::filesystem::path> paths;for(const auto& file:files)paths.emplace_back(file.toStdWString());session->assets().queueImportFiles(paths);invalidate();});
        action("Link Current Modeling Scene",[this]{if(session)session->linkModelingScene(modelingManifest,modelingProject,modelingScene);invalidate();});
        menu->addSeparator();auto* add=menu->addMenu("Add");
        const auto create=[this,add](const char* title,std::function<EntityId(SceneDocument&)> command){auto* value=add->addAction(title);QObject::connect(value,&QAction::triggered,add,[this,command]{if(session&&!session->playing()){const auto id=command(session->document());session->document().select({id});invalidate();}});};
        create("Entity",[](auto& doc){return doc.createEmpty();});create("Player",[](auto& doc){return doc.createPlayer();});create("Point Light",[](auto& doc){return doc.createPointLight();});create("Spot Light",[](auto& doc){return doc.createSpotLight();});create("Directional Light",[](auto& doc){return doc.createDirectionalLight();});create("Environment",[](auto& doc){return doc.createEnvironment();});create("Camera",[](auto& doc){return doc.createCamera();});create("Collider",[](auto& doc){return doc.createCollider();});create("Navigation Volume",[](auto& doc){return doc.createNavigationVolume();});create("Box",[](auto& doc){return doc.createBox();});create("Sphere",[](auto& doc){return doc.createSphere();});
        menu->addSeparator();action("Undo Level",[this]{if(session){inspector->finish();if(!documents||!documents->undoActive())session->undo();}invalidate();});action("Redo Level",[this]{if(session){inspector->finish();if(!documents||!documents->redoActive())session->redo();}invalidate();});
        action("Collision Layers…",[this]{if(documents)documents->openCollisionLayers();});
        cameraMenu=menu->addMenu("Camera");QObject::connect(cameraMenu,&QMenu::aboutToShow,cameraMenu,[this]{populateCameras();});
        bookmarkMenu=menu->addMenu("Camera Bookmarks");QObject::connect(bookmarkMenu,&QMenu::aboutToShow,bookmarkMenu,[this]{populateBookmarks();});
        auto* outlinerContent=new QWidget;auto* outlinerLayout=new QVBoxLayout(outlinerContent);outlinerLayout->setContentsMargins(4,4,4,4);
        auto* searchRow=new QHBoxLayout;search=new QLineEdit;search->setPlaceholderText("Search entities");filter=new QComboBox;
        for(const auto value:SceneOutlinerFilters)filter->addItem(sceneOutlinerFilterName(value),int(value));
        searchRow->addWidget(search,1);searchRow->addWidget(filter);outlinerLayout->addLayout(searchRow);
        tree=new LevelTree;tree->setHeaderHidden(true);tree->setSelectionMode(QAbstractItemView::ExtendedSelection);tree->setDragDropMode(QAbstractItemView::InternalMove);outlinerLayout->addWidget(tree);
        inspector=new QtInspector;browser=std::make_unique<LevelAssetBrowser>(main,request);assets=browser->widget();preferences=std::make_unique<LevelPreferences>(main,request);
        scene=new LevelViewport(host,false);game=new LevelViewport(host,true);
        viewportTools=new LevelViewportTools(*scene,request);
        viewportTools->beforeInteraction=[this]{inspector->finish();if(session)session->cancelViewportRequest();};
        vertexPaint=std::make_unique<LevelVertexPaint>(main,request);
        viewportTools->additionalEvent=[this](QEvent& event){return vertexPaint&&vertexPaint->event(event,*scene);};
        viewportTools->additionalPaint=[this](QPainter& painter){if(vertexPaint)vertexPaint->paint(painter);};
        auto* paintAction=action("Vertex Paint",[this]{if(!session||session->playing())return;settleTools();session->cancelViewportRequest();vertexPaint->setEnabled(!vertexPaint->enabled());if(request)request();});paintAction->setCheckable(true);
        QObject::connect(menu,&QMenu::aboutToShow,menu,[this,paintAction]{paintAction->setEnabled(session&&!session->playing());paintAction->setChecked(vertexPaint->enabled());});
        sceneDock=dock("EngineScene","Engine Scene",scene,Qt::RightDockWidgetArea);gameDock=dock("EngineGame","Game",game,Qt::RightDockWidgetArea);
        treeDock=dock("EngineOutliner","Level Outliner",outlinerContent,Qt::LeftDockWidgetArea);inspectorDock=dock("EngineInspector","Level Inspector",inspector,Qt::RightDockWidgetArea);assetsDock=dock("EngineAssets","Engine Assets",assets,Qt::BottomDockWidgetArea);
        main.tabifyDockWidget(sceneDock,gameDock);sceneDock->raise();gameDock->hide();
        status=new QLabel(toolbar);status->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);status->setMinimumWidth(0);status->setMaximumWidth(520);toolbar->addWidget(status);
        QObject::connect(tree,&QTreeWidget::itemSelectionChanged,tree,[this]{if(updating||!session)return;settleTools();inspector->finish();std::vector<EntityId> ids;for(auto* item:tree->selectedItems())ids.push_back(item->data(0,Qt::UserRole).toULongLong());session->editingDocument().select(std::move(ids));refreshInspector();});
        QObject::connect(search,&QLineEdit::textChanged,tree,[this]{shownRevision=UINT64_MAX;refresh();});QObject::connect(filter,&QComboBox::currentIndexChanged,tree,[this]{shownRevision=UINT64_MAX;refresh();});
        tree->reparent=[this](EntityId parent){if(session&&!session->playing()){inspector->finish();session->document().reparentSelection(parent);invalidate();}};
        tree->setContextMenuPolicy(Qt::CustomContextMenu);QObject::connect(tree,&QWidget::customContextMenuRequested,tree,[this](QPoint point){if(!session||session->playing())return;QMenu context;auto* duplicate=context.addAction("Duplicate");auto* erase=context.addAction("Delete");context.addSeparator();auto* group=context.addAction("Group");auto* ungroup=context.addAction("Ungroup");auto* chosen=context.exec(tree->viewport()->mapToGlobal(point));inspector->finish();if(chosen==duplicate)session->document().duplicateSelection();else if(chosen==erase)session->document().eraseSelection();else if(chosen==group)session->document().groupSelection();else if(chosen==ungroup)session->document().ungroupSelection();invalidate();});
        browser->setPrepareCommand([this]{if(!session||session->playing()||QApplication::activeModalWidget())return false;settleTools();inspector->finish();if(documents&&!documents->finishActive())return false;return !session->document().transactionActive();});
        browser->setNavigateScene([this](const std::string& id,EntityId entity){if(!session||!confirm()||!session->openLevel(id,true))return;if(session->document().scene().contains(entity))session->document().select({entity});sceneDock->show();sceneDock->raise();scene->setFocus(Qt::OtherFocusReason);invalidate();});
        vertexPaint->setThumbnailProvider(qtThumbnail);
        action("Preferences…",[this]{preferences->open();});auto* navigationAction=action("Show Navigation",[this]{if(session){session->setNavigationVisible(!navigationVisible);navigationVisible=!navigationVisible;if(request)request();}});navigationAction->setCheckable(true);QObject::connect(menu,&QMenu::aboutToShow,menu,[this,navigationAction]{navigationAction->setChecked(navigationVisible);});
        scene->dispatch=[this](const InputEvent& event){
            navigation.handleEvent(event);if(!session)return;
            if(event.type==InputEventType::SurfaceFocusLost||(event.type==InputEventType::KeyDown&&event.physical==PhysicalKeyCode::Escape))session->cancelViewportRequest();
            if(event.type==InputEventType::MouseButtonDown&&event.button==MouseButton::Left&&!navigation.active()&&!vertexPaint->enabled()){
                inspector->finish();
                const auto pixels=scene->pixels();const Vec2 size{float(pixels.width()),float(pixels.height())};
                const Vec2 pixel{event.position[0]*size[0]/std::max(1,scene->width()),event.position[1]*size[1]/std::max(1,scene->height())};
                const auto flags=QApplication::keyboardModifiers();session->selectViewport(pixel,camera,size,flags.testFlag(Qt::ControlModifier),flags.testFlag(Qt::ShiftModifier));
            }
        };scene->changed=request;game->changed=request;
        navigation.setMouseCapture([this](InputSurfaceId id,bool enabled){return id==scene->inputId()&&scene->capture(enabled);});
        game->dispatch=[this](const InputEvent& event){if(session){if(event.type==InputEventType::MouseButtonDown)session->gameInput().setViewport(game->region(),session->playing()&&!session->paused(),!game->hasFocus()||QApplication::activeModalWidget());session->gameInput().handleEvent(event);}};
        scene->dropped=[this](const std::vector<std::string>& ids,QPointF position){if(!session||session->playing())return;settleTools();inspector->finish();const auto pixels=scene->pixels();const Vec2 size{float(pixels.width()),float(pixels.height())};
            const Vec2 pixel{float(position.x())*size[0]/std::max(1,scene->width()),float(position.y())*size[1]/std::max(1,scene->height())};session->dropAssets(ids,pixel,camera,size);if(request)request();};
        game->composition->set_render_callback([this](const auto& target){return session&&host.render_engine_viewport(true,target);});scene->composition->set_render_callback([this](const auto& target){return session&&host.render_engine_viewport(false,target);});
        commands.callback=[this](QObject*,QEvent* event){
            if(!ownsInput()||!session||QApplication::activeModalWidget()||QApplication::activePopupWidget())return false;
            if(preferences&&preferences->ownsInput())return false;
            if(event->type()!=QEvent::ShortcutOverride&&event->type()!=QEvent::KeyPress)return false;
            const auto& key=*static_cast<QKeyEvent*>(event);auto* focus=QApplication::focusWidget();
            const bool text=qobject_cast<QLineEdit*>(focus)||qobject_cast<QAbstractSpinBox*>(focus);
            const bool saveKey=key.matches(QKeySequence::Save),saveAs=key.matches(QKeySequence::SaveAs),undo=key.matches(QKeySequence::Undo),redo=key.matches(QKeySequence::Redo);
            if(text&&!saveKey&&!saveAs)return false;
            const bool start=!text&&key.key()==Qt::Key_P&&key.modifiers()==Qt::NoModifier;
            const bool stop=!text&&key.key()==Qt::Key_Escape&&key.modifiers()==Qt::ShiftModifier;
            const bool pause=!text&&key.key()==Qt::Key_F6,step=!text&&key.key()==Qt::Key_F7;
            const bool erase=!text&&key.key()==Qt::Key_Delete&&key.modifiers()==Qt::NoModifier;
            const bool focusSelection=!text&&key.key()==Qt::Key_F&&key.modifiers()==Qt::NoModifier&&scene->hasFocus();
            const bool playHere=!text&&key.key()==Qt::Key_F5&&key.modifiers()==Qt::ShiftModifier&&scene->hasFocus();
            if(!saveKey&&!saveAs&&!undo&&!redo&&!start&&!stop&&!pause&&!step&&!erase&&!focusSelection&&!playHere)return false;
            if(event->type()==QEvent::ShortcutOverride){event->accept();return true;}
            if(key.isAutoRepeat())return true;
            settleTools();
            if(saveKey||saveAs)save(saveAs);
            else if(undo){inspector->finish();if(!documents||!documents->undoActive())session->editingDocument().undo();invalidate();}
            else if(redo){inspector->finish();if(!documents||!documents->redoActive())session->editingDocument().redo();invalidate();}
            else if(stop)stopAction->trigger();
            else if(focusSelection)this->focusSelection();
            else if(playHere)requestContext(scene->mapFromGlobal(QCursor::pos()),true);
            else if(!navigation.active()){if(start)playAction->trigger();else if(pause)pauseAction->trigger();else if(step)stepAction->trigger();else if(erase){inspector->finish();if(browser->ownsInput())browser->requestDeletion(session->assets().browserSelection().ids());else session->editingDocument().eraseSelection();invalidate();}}
            return true;
        };
        QApplication::instance()->installEventFilter(&commands);
        camera.position={6,4,8};camera.target={0,1,0};refresh();
    }
    ~Impl(){QApplication::instance()->removeEventFilter(&commands);bind(nullptr);navigation.cancel();if(contextMenu)delete contextMenu;vertexPaint.reset();browser.reset();preferences.reset();game->composition->set_prepare_callback({});scene->composition->set_prepare_callback({});delete menu;delete toolbar;delete sceneDock;delete gameDock;delete treeDock;delete inspectorDock;delete assetsDock;}
    void settleTools(){if(viewportTools)viewportTools->finish();if(vertexPaint)vertexPaint->finishForCommand();if(session)pilot.finishGesture(session->editingDocument(),camera);}
    void detachTools(){settleTools();if(viewportTools)viewportTools->bind(nullptr,nullptr,nullptr);if(vertexPaint)vertexPaint->bind(nullptr,nullptr,nullptr);if(session)pilot.useEditorCamera(session->editingDocument(),camera);pilot=CameraPilot{};if(contextMenu)contextMenu->close();}
    void focusSelection(){if(!session)return;settleTools();if(pilot.selectedCamera()&&!pilot.piloting())pilot.useEditorCamera(session->editingDocument(),camera);session->focusSelection(camera,float(std::max(1,scene->width()))/float(std::max(1,scene->height())),pilot.selectedCamera()!=NoEntity);if(pilot.piloting())pilot.apply(session->editingDocument(),camera,false);if(request)request();}
    void populateCameras(){cameraMenu->clear();if(!session)return;auto& document=session->editingDocument();
        auto* editor=cameraMenu->addAction("Editor Camera");editor->setCheckable(true);editor->setChecked(!pilot.selectedCamera());QObject::connect(editor,&QAction::triggered,cameraMenu,[this]{settleTools();pilot.useEditorCamera(session->editingDocument(),camera);if(request)request();});
        auto* pilotAction=cameraMenu->addAction("Pilot Camera");pilotAction->setCheckable(true);pilotAction->setChecked(pilot.piloting());pilotAction->setEnabled(pilot.selectedCamera()&&!session->playing());QObject::connect(pilotAction,&QAction::triggered,cameraMenu,[this](bool enabled){settleTools();if(!pilot.setPiloting(session->editingDocument(),camera,enabled))status->setText(QString::fromStdString(pilot.status()));if(request)request();});
        auto* capture=cameraMenu->addAction("Create Camera from View");capture->setEnabled(!session->playing());QObject::connect(capture,&QAction::triggered,cameraMenu,[this]{settleTools();inspector->finish();auto& document=session->document();const auto id=document.createCamera(camera,float(std::max(1,scene->width()))/float(std::max(1,scene->height())));if(id)document.select({id});invalidate();});
        cameraMenu->addSeparator();for(const auto& record:document.scene().records())if(record.camera){const auto id=record.id,generation=document.documentGeneration();auto* select=cameraMenu->addAction(QString::fromStdString(record.name));select->setCheckable(true);select->setChecked(pilot.selectedCamera()==id);QObject::connect(select,&QAction::triggered,cameraMenu,[this,id,generation]{if(!session||session->editingDocument().documentGeneration()!=generation)return;settleTools();inspector->finish();pilot.selectCamera(session->editingDocument(),id,camera);if(request)request();});}
    }
    void bookmarkFailure(){if(bookmarks&&!bookmarks->status().empty())main.statusBar()->showMessage(QString::fromStdString(bookmarks->status()),10000);}
    void populateBookmarks(){bookmarkMenu->clear();if(!session||!bookmarks)return;const auto key=session->document().assetId();
        auto* add=bookmarkMenu->addAction("Add Bookmark…");add->setEnabled(!key.empty());add->setToolTip("Save the level before adding a camera bookmark.");QObject::connect(add,&QAction::triggered,bookmarkMenu,[this,key]{if(!session||session->document().assetId()!=key)return;settleTools();pilot.useEditorCamera(session->editingDocument(),camera);bool accepted{};const auto name=QInputDialog::getText(&main,"Camera Bookmark","Name",QLineEdit::Normal,{},&accepted);if(accepted&&!bookmarks->add(key,name.toStdString(),camera))bookmarkFailure();});
        auto* reload=bookmarkMenu->addAction("Reload Bookmarks");QObject::connect(reload,&QAction::triggered,bookmarkMenu,[this]{if(bookmarks&&!bookmarks->reload())bookmarkFailure();});
        bookmarkMenu->addSeparator();for(const auto& value:bookmarks->views(key)){const auto id=value.id;auto* entry=bookmarkMenu->addMenu(QString::fromStdString(value.name));auto* recall=entry->addAction("Recall");QObject::connect(recall,&QAction::triggered,entry,[this,key,id]{if(!session||session->document().assetId()!=key)return;if(const auto view=bookmarks->recall(key,id)){settleTools();navigation.cancel();pilot.useEditorCamera(session->editingDocument(),camera);camera=*view;if(request)request();}});
            auto* update=entry->addAction("Update from View");QObject::connect(update,&QAction::triggered,entry,[this,key,id]{if(session&&session->document().assetId()==key){settleTools();pilot.useEditorCamera(session->editingDocument(),camera);if(!bookmarks->update(key,id,camera))bookmarkFailure();}});
            auto* rename=entry->addAction("Rename…");const auto oldName=value.name;QObject::connect(rename,&QAction::triggered,entry,[this,key,id,oldName]{if(!session||session->document().assetId()!=key)return;bool accepted{};const auto name=QInputDialog::getText(&main,"Rename Bookmark","Name",QLineEdit::Normal,QString::fromStdString(oldName),&accepted);if(accepted&&!bookmarks->rename(key,id,name.toStdString()))bookmarkFailure();});
            auto* remove=entry->addAction("Delete");QObject::connect(remove,&QAction::triggered,entry,[this,key,id]{if(session&&session->document().assetId()==key&&!bookmarks->remove(key,id))bookmarkFailure();});
        }
    }
    void requestContext(QPointF local,bool playImmediately=false){if(!session||session->playing()||vertexPaint->enabled())return;settleTools();inspector->finish();const auto pixels=scene->pixels();const Vec2 size{float(pixels.width()),float(pixels.height())};const Vec2 pixel{float(local.x())*size[0]/std::max(1,scene->width()),float(local.y())*size[1]/std::max(1,scene->height())};contextPosition=scene->mapToGlobal(local.toPoint());autoPlayContext=playImmediately;session->requestViewportContext(pixel,camera,size);if(request)request();}
    void processContext(){if(!session)return;const auto result=session->takeViewportContext();if(!result)return;const auto current=[this,result]{return session&&!session->playing()&&session->document().documentGeneration()==result->document&&session->document().scene().generation()==result->generation&&session->document().scene().revision()==result->revision;};
        if(autoPlayContext){autoPlayContext=false;if(result->position&&current()){detachTools();inspector->finish();if(session->playFromHere(*result->position,result->camera))focusGame();}return;}
        if(contextMenu)delete contextMenu;contextMenu=new QMenu(scene);auto* popup=contextMenu.data();auto* start=popup->addAction("Play From Here");start->setEnabled(result->position.has_value()&&session->document().scene().properties().playerCharacter!=NoEntity);QObject::connect(start,&QAction::triggered,popup,[this,result,current]{if(current()&&result->position){detachTools();inspector->finish();if(session->playFromHere(*result->position,result->camera))focusGame();}});
        if(result->entity){popup->addSeparator();for(const auto* name:{"Duplicate","Delete","Focus"}){auto* action=popup->addAction(name);QObject::connect(action,&QAction::triggered,popup,[this,result,current,name]{if(!current()||session->document().transactionActive())return;settleTools();inspector->finish();session->document().select({result->entity});if(std::string_view(name)=="Duplicate")session->document().duplicateSelection();else if(std::string_view(name)=="Delete")session->document().eraseSelection();else focusSelection();invalidate();});}}
        popup->popup(contextPosition);
    }
    bool ownsInput()const{if((browser&&browser->ownsInput())||(preferences&&preferences->ownsInput()))return true;if(vertexPaint&&vertexPaint->ownsInput())return true;auto* focus=QApplication::focusWidget();if(!focus)return false;for(auto* widget:std::array<QWidget*,6>{sceneDock,gameDock,treeDock,inspectorDock,assetsDock,toolbar})if(widget&&(widget==focus||widget->isAncestorOf(focus)))return true;return false;}
    void focusGame(){if(!session||!session->playing()||session->paused())return;gameDock->show();gameDock->raise();gameDock->window()->activateWindow();game->setFocus(Qt::OtherFocusReason);session->gameInput().setViewport(game->region(),resolveGameCamera(session->presentationScene()).has_value(),false);session->gameInput().acquire();}
    QDockWidget* dock(const char* key,const char* title,QWidget* content,Qt::DockWidgetArea area){auto* result=new QDockWidget(title,&main);result->setObjectName(key);result->setWidget(content);quader::install_palette_aware_dock_title_presentation(*result);main.addDockWidget(area,result);menu->addAction(result->toggleViewAction());return result;}
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
    void bind(LevelSession* value){if(session==value)return;detachTools();inspector->detach();releaseMaterialPreview();documentPreviews.reset();documents.reset();if(browser)browser->bind(nullptr);bookmarks.reset();if(session)session->gameInput().setMouseCapture({});session=value;navigation.cancel();if(browser)browser->bind(session);inspector->setQuaderBridge(session?&session->modelingBridge():nullptr);inspector->setCollisionLayerNames(defaultCollisionLayerNames());if(session){bookmarks=std::make_unique<CameraBookmarks>(session->projectRoot());documents=std::make_unique<AssetDocuments>(main,*session,request);documentPreviews=std::make_unique<LevelDocumentPreviews>(*documents,*session,host,request);documents->setActiveMaterialChanged([this](MaterialDocument* document,QMainWindow* window){bindMaterialPreview(document,window);});session->gameInput().setMouseCapture([this](InputSurfaceId id,bool enabled){return id==game->inputId()&&game->capture(enabled);});vertexPaint->setQueries([this](const SceneDocument& document,EntityId id){return session->vertexPaintTargets(document,id);},[this](const SceneDocument& document){return session->vertexPaintSurfaces(document);});if(session->editorCamera())camera=*session->editorCamera();}invalidate();}
    void invalidate(){shownRevision=UINT64_MAX;shownCatalog=UINT64_MAX;shownSelection=UINT64_MAX;preparedKey.reset();refresh();if(request)request();}
    bool save(bool saveAs){if(!session)return true;settleTools();inspector->finish();session->document().setPreviewCamera(pilot.freeCamera().value_or(camera));std::string name;if(saveAs||session->document().assetId().empty()){bool accepted{};const auto value=QInputDialog::getText(&main,"Save Level","Name",QLineEdit::Normal,QString::fromStdString(session->document().displayName()),&accepted);if(!accepted||value.trimmed().isEmpty())return false;name=value.toStdString();}const auto result=session->save(name,saveAs);refresh();if(!result)QMessageBox::warning(&main,"Save Level",QString::fromStdString(session->status()));return result;}
    bool confirm(){settleTools();if(session&&session->playing()){detachTools();inspector->detach();session->stop();}if(documents&&!documents->confirmClose())return false;if(!session||!session->document().dirty())return true;const auto& document=session->document();if(document.assetId().empty()&&document.scene().records().empty()&&document.history().size()==0)return true;const auto result=QMessageBox::question(&main,"Unsaved Level","Save changes to the level?",QMessageBox::Save|QMessageBox::Discard|QMessageBox::Cancel,QMessageBox::Save);if(result==QMessageBox::Discard)return true;return result==QMessageBox::Save&&save(false);}
    void openAsset(const std::string& id){if(!session)return;const auto found=std::find_if(session->assets().catalog().begin(),session->assets().catalog().end(),[&](const auto& asset){return asset.id==id;});if(found==session->assets().catalog().end())return;if(found->kind==AssetKind::Scene){if(confirm())session->openLevel(id,true);}else if(documents)documents->open(found->kind,id);}
    void processBrowserActions(){if(!session||!browser)return;const auto actions=browser->takeActions();
        if(actions.newScene&&confirm()){session->newLevel(true);invalidate();}
        if(!actions.openScene.empty())openAsset(actions.openScene);
        if(!actions.deleteScene.empty())browser->requestDeletion({actions.deleteScene});
        for(const auto& id:{actions.openMaterial,actions.openParticle,actions.openAnimation,actions.openRagdoll,actions.openDecal,actions.openNavigationAgentProfile,actions.openGameplayEffect})if(!id.empty())openAsset(id);
        if(session->playing())return;
        const auto openCreated=[this](AssetKind kind,const std::string& id,const std::string& folder){if(id.empty())return;if(!folder.empty()){const std::array<std::string,1> ids{id};session->assets().browserCatalog().move(ids,folder,true);}browser->reveal(id);if(documents)documents->open(kind,id);};
        try {
            if(actions.createMaterial||actions.createBlendMaterial){settleTools();inspector->finish();const auto asset=actions.createBlendMaterial?session->assets().createBlendMaterial():session->assets().createMaterial();if(asset)openCreated(AssetKind::Material,asset->id,actions.materialFolder);}
            if(actions.createParticle){settleTools();inspector->finish();const auto asset=createParticleAsset(session->projectRoot());session->assets().noteAssetWrite(asset.id);session->assets().refreshParticles();session->assets().publishParticle(std::make_shared<const ParticleEffectAsset>(asset));openCreated(AssetKind::Particle,asset.id,actions.particleFolder);}
            if(actions.createDecal){settleTools();inspector->finish();if(const auto asset=session->assets().createDecal())openCreated(AssetKind::Decal,asset->id,actions.decalFolder);}
            if(actions.createNavigationAgentProfile){settleTools();inspector->finish();if(const auto asset=session->assets().createNavigationAgentProfile())openCreated(AssetKind::NavigationAgentProfile,asset->id,actions.navigationAgentProfileFolder);}
            if(actions.createGameplayEffect){settleTools();inspector->finish();if(const auto asset=session->assets().createGameplayEffect())openCreated(AssetKind::GameplayEffect,asset->id,actions.gameplayEffectFolder);}
            if(!actions.createRagdollModel.empty()){settleTools();inspector->finish();if(const auto asset=session->assets().createRagdoll(actions.createRagdollModel,{},actions.ragdollFolder))openCreated(AssetKind::Ragdoll,asset->id,{});}
        }catch(const std::exception& error){main.statusBar()->showMessage(QString::fromUtf8(error.what()),10000);}
    }
    void refresh(){const bool enabled=session!=nullptr;toolbar->setEnabled(enabled);tree->setEnabled(enabled);inspector->setEnabled(enabled);assets->setEnabled(enabled);if(!session)return;
        playAction->setEnabled(!session->playing()&&!session->playPending());stopAction->setEnabled(session->playing()||session->playPending());pauseAction->setEnabled(session->playing());pauseAction->setText(session->paused()?"Resume":"Pause");stepAction->setEnabled(session->playing()&&session->paused());status->setText(QString::fromStdString(session->status()));
        auto& document=session->editingDocument();
        if(shownGeneration!=document.documentGeneration()){if(!session->playing()&&session->editorCamera()){pilot.useEditorCamera(document,camera);camera=*session->editorCamera();}shownGeneration=document.documentGeneration();shownRevision=UINT64_MAX;shownSelection=UINT64_MAX;}
        if(shownRevision!=session->revision()||shownSelection!=document.selection().primary){updating=true;const QSignalBlocker block(tree);tree->clear();outliner.synchronize(document.scene(),search->text().toStdString(),static_cast<SceneOutlinerFilter>(filter->currentData().toInt()));std::unordered_map<EntityId,QTreeWidgetItem*> rows;for(const auto& row:outliner.rows()){const auto& record=outliner.record(row.entity);auto* item=new QTreeWidgetItem(QStringList{QString::fromStdString(record.name)});item->setData(0,Qt::UserRole,QVariant::fromValue<qulonglong>(record.id));rows.emplace(record.id,item);}for(const auto& row:outliner.rows()){auto* item=rows.at(row.entity);const auto parent=rows.find(row.parent);if(parent!=rows.end())parent->second->addChild(item);else tree->addTopLevelItem(item);item->setSelected(std::find(document.selection().ids.begin(),document.selection().ids.end(),row.entity)!=document.selection().ids.end());if(outliner.filtered())item->setExpanded(true);}shownRevision=session->revision();shownSelection=document.selection().primary;updating=false;}
        refreshInspector();if(documents)documents->update();updateMaterialPreview();
    }
    void refreshInspector(){if(!session)return;if(auto* renderer=host.crimson_renderer()){inspector->setQuaderBridge(&session->modelingBridge());inspector->setCollisionLayerNames(defaultCollisionLayerNames());inspector->setNavigationPreview(&session->navigationProfile(),[this](const std::string& id){return session->navigationStatus(id);});inspector->synchronize(session->editingDocument(),session->assets(),*renderer);setMeshPaintSubdivisionQuery(session->editingDocument(),[this](const EntityRecord& record,std::uint32_t level,SceneMeshPaint& paint,std::string& diagnostic){return session->meshPaintSubdivision(record,level,paint,diagnostic);});}
        const auto actions=inspector->takeActions();
        for(const auto& id:{actions.openMaterial,actions.openParticle,actions.openAnimation,actions.openRagdoll,actions.openDecal,actions.openNavigationAgentProfile,actions.openGameplayEffect})if(!id.empty())openAsset(id);
        if(!actions.createRagdollModel.empty())if(const auto asset=session->assets().createRagdoll(actions.createRagdollModel);asset&&documents)documents->open(AssetKind::Ragdoll,asset->id);
        if(actions.playParticleEntity)session->replayParticle(actions.playParticleEntity);
        if(session->playing()){if(actions.activateRagdollEntity)session->runtime().activateRagdoll(actions.activateRagdollEntity);if(actions.resetRagdollEntity)session->runtime().resetRagdoll(actions.resetRagdollEntity);}
        if(const auto id=inspector->takeDecalPlacement())session->armDecalPlacement(id);
    }
};
LevelPanels::LevelPanels(QMainWindow& window,quader::RenderHost& host,std::function<void()> request):impl_(std::make_unique<Impl>(window,host,std::move(request))){
    impl_->scene->composition->set_prepare_callback([this]{return prepareFrame();});impl_->game->composition->set_prepare_callback([this]{return prepareFrame();});
}
LevelPanels::~LevelPanels()=default;
void LevelPanels::bind(LevelSession* value){impl_->bind(value);}
void LevelPanels::setModelingSource(std::filesystem::path manifest,std::string project,std::string scene){impl_->modelingManifest=std::move(manifest);impl_->modelingProject=std::move(project);impl_->modelingScene=std::move(scene);}
void LevelPanels::update(double seconds,bool active){
    auto& state=*impl_;if(!state.session)return;
    const auto changed=state.session->takeFileChanges();if(state.documents&&!changed.empty())state.documents->filesChanged(changed);
    ++state.updateEpoch;
    if(active&&state.session->playing()&&!state.wasPlaying)state.focusGame();
    state.wasPlaying=state.session->playing();
    auto& document=state.session->editingDocument();
    state.viewportTools->bind(&document,&state.session->assets(),&state.camera);
    state.vertexPaint->bind(&document,&state.session->assets(),state.host.crimson_renderer());
    state.pilot.sync(document,state.camera);
    state.scene->drain();state.game->drain();
    auto region=state.scene->region();region.inputBlocked=state.viewportTools->active()||state.vertexPaint->interacting()||(state.pilot.selectedCamera()&&!state.pilot.piloting());
    if(!active)state.navigation.cancel();
    state.navigation.update(state.camera,region,float(seconds),false,state.pilot.selectedCamera()!=NoEntity);
    if(state.pilot.piloting())state.pilot.apply(document,state.camera,state.navigation.active());
    if(const auto click=state.navigation.consumeContextClick())state.requestContext(QPointF{(*click)[0],(*click)[1]});
    state.viewportTools->synchronize(state.navigation.active()||state.session->playing()||state.pilot.gestureActive()||state.vertexPaint->enabled());
    const auto size=state.scene->pixels();
    state.vertexPaint->synchronize({state.camera.projectionMatrix(float(std::max(1,size.width()))/float(std::max(1,size.height())),ClipDepth::NegativeOneToOne)*state.camera.viewMatrix(),std::uint32_t(std::max(0,size.width())),std::uint32_t(std::max(0,size.height())),float(state.scene->width()),float(state.scene->height()),state.scene->underMouse(),state.scene->hasFocus(),!active||state.navigation.active()||state.session->playing()||state.pilot.gestureActive()},state.host.completed_frame());
    state.session->setVertexPaintState(state.vertexPaint->enabled(),state.vertexPaint->surfaceHeld(),state.vertexPaint->showSubdivisions());
    if(!state.session->playing())state.session->rememberEditorCamera(state.pilot.freeCamera().value_or(state.camera));
    state.processContext();
    auto& input=state.session->gameInput();
    input.setViewport(state.game->region(),state.session->playing()&&!state.session->paused()&&resolveGameCamera(state.session->presentationScene()).has_value(),
        !active||!state.game->hasFocus()||QApplication::activeModalWidget());
    input.syncMouseCapture();state.browser->update(state.host.completed_frame(),state.vertexPaint->surfaceHeld(),state.preferences->values().browserPreviewSize,state.vertexPaint->previewMaterials());state.processBrowserActions();state.refresh();
    if(state.documentPreviews){state.documentPreviews->setAntiAliasing(state.preferences->values().antiAliasing);state.documentPreviews->update(seconds,active);}
    if(state.scene->isVisible())state.scene->composition->update();
    if(state.game->isVisible())state.game->composition->update();
}
bool LevelPanels::prepareFrame(){
    auto& state=*impl_;if(!state.session||state.preparing)return false;const QScopedValueRollback preparing(state.preparing,true);
    try {
        LevelViewRequest request;request.sceneCamera=state.camera;const auto scene=state.scene->pixels(),game=state.game->pixels();request.sceneWidth=std::uint32_t(std::max(0,scene.width()));request.sceneHeight=std::uint32_t(std::max(0,scene.height()));request.gameWidth=std::uint32_t(std::max(0,game.width()));request.gameHeight=std::uint32_t(std::max(0,game.height()));request.sceneVisible=state.scene->isVisible();request.gameVisible=state.game->isVisible();
        state.preferences->apply(request);
        const auto& document=state.session->editingDocument();
        Impl::FrameKey key{state.updateEpoch,document.documentGeneration(),document.scene().generation(),document.scene().revision(),document.scene().poseRevision(),state.camera.viewMatrix(),state.camera.projectionMatrix(float(std::max(1u,request.sceneWidth))/float(std::max(1u,request.sceneHeight)),ClipDepth::ZeroToOne),document.selection().ids,document.selection().primary,request.sceneWidth,request.sceneHeight,request.gameWidth,request.gameHeight,request.sceneVisible,request.gameVisible,state.session->playing(),state.session->paused(),state.vertexPaint->enabled(),state.vertexPaint->surfaceHeld(),state.vertexPaint->showSubdivisions(),request.gridVisible,request.antiAliasing,request.appearance,request.shadowResolution};
        if(state.preparedKey&&*state.preparedKey==key)return true;
        auto frame=state.session->prepareRender(request);if(!frame)return false;state.host.publish_engine_frame(std::move(frame));state.preparedKey=std::move(key);return true;
    }catch(const std::exception& error){state.status->setText(QString::fromUtf8(error.what()));return false;}
}
bool LevelPanels::continuous()const{return impl_->session&&(impl_->session->playing()||impl_->navigation.active()||impl_->scene->isVisible()||impl_->game->isVisible()||(impl_->previewDocument&&impl_->materialPreviewContainer&&impl_->materialPreviewContainer->isVisible())||(impl_->documentPreviews&&impl_->documentPreviews->continuous()));}
bool LevelPanels::ownsInput()const{return impl_->ownsInput();}
bool LevelPanels::navigating()const{return impl_->navigation.active();}
void LevelPanels::setProjectOpenHandler(std::function<void(const std::filesystem::path&)> callback){impl_->openProject=std::move(callback);}
bool LevelPanels::confirmClose(){if(!impl_->confirm())return false;if(impl_->session){impl_->session->rememberEditorCamera(impl_->pilot.freeCamera().value_or(impl_->camera));(void)impl_->session->saveEditorState();}return true;}
} // namespace crimson::integration
