#include "QtAssetBrowser.h"
#include "assets/AssetLibrary.h"
#include "assets/BrowserCatalog.h"
#include <crimson/renderer/forward/Environment.h>
#include <QAbstractListModel>
#include <QApplication>
#include <QComboBox>
#include <QCache>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QListView>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QPushButton>
#include <QScrollBar>
#include <QStyle>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <limits>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace crimson {
namespace {
QCache<QString,QImage> thumbnails{64*1024}; // KiB; CPU copies are separate from GPU residency.
std::mutex thumbnailMutex;
std::atomic_uint64_t thumbnailEpoch{1};
class AssetModel final : public QAbstractListModel {
public:
    std::vector<BrowserItem> items;
    explicit AssetModel(QObject* parent):QAbstractListModel(parent){}
    int rowCount(const QModelIndex& parent={})const override{return parent.isValid()?0:int(items.size());}
    QVariant data(const QModelIndex& index,int role)const override {
        if(!index.isValid()||index.row()<0||std::size_t(index.row())>=items.size())return {};
        const auto& item=items[std::size_t(index.row())];
        if(role==Qt::DisplayRole)return QString::fromStdString(item.name);
        if(role==Qt::ToolTipRole)return QString::fromStdString(item.name);
        if(role==Qt::UserRole)return QString::fromStdString(item.id);
        if(role==Qt::DecorationRole){const auto image=qtThumbnail(item.id);if(!image.isNull())return QIcon(QPixmap::fromImage(image));return QApplication::style()->standardIcon(item.folder?QStyle::SP_DirIcon:item.kind==AssetKind::Scene?QStyle::SP_ComputerIcon:QStyle::SP_FileIcon);}
        return {};
    }
    Qt::ItemFlags flags(const QModelIndex& index)const override{return index.isValid()?Qt::ItemIsEnabled|Qt::ItemIsSelectable|Qt::ItemIsDragEnabled|Qt::ItemIsDropEnabled:Qt::ItemIsDropEnabled;}
    QStringList mimeTypes()const override{return {QString::fromLatin1(QtAssetMime)};}
    QMimeData* mimeData(const QModelIndexList& indices)const override {
        auto mime=new QMimeData;auto rows=nlohmann::json::array();for(const auto& index:indices)if(index.isValid()&&index.row()>=0&&std::size_t(index.row())<items.size()){const auto& item=items[std::size_t(index.row())];rows.push_back({{"id",item.id},{"kind",unsigned(item.kind)},{"folder",item.folder}});}mime->setData(QtAssetMime,QByteArray::fromStdString(rows.dump()));return mime;
    }
    Qt::DropActions supportedDropActions()const override{return Qt::CopyAction|Qt::MoveAction;}
    void replace(std::vector<BrowserItem> next){beginResetModel();items=std::move(next);endResetModel();}
    void refreshIcons(){if(!items.empty())Q_EMIT dataChanged(index(0),index(int(items.size()-1)),{Qt::DecorationRole});}
};
class AssetView final : public QListView {
public:
    std::function<void(const QMimeData*,const QModelIndex&)> dropped;
    explicit AssetView(QWidget* parent):QListView(parent){setAcceptDrops(true);viewport()->setAcceptDrops(true);setDragEnabled(true);setDragDropMode(DragDrop);setDefaultDropAction(Qt::CopyAction);}
protected:
    void dragEnterEvent(QDragEnterEvent* event)override{if(event->mimeData()->hasUrls()||event->mimeData()->hasFormat(QtAssetMime))event->acceptProposedAction();}
    void dragMoveEvent(QDragMoveEvent* event)override{if(event->mimeData()->hasUrls()||event->mimeData()->hasFormat(QtAssetMime))event->acceptProposedAction();}
    void dropEvent(QDropEvent* event)override{if(dropped){dropped(event->mimeData(),indexAt(event->position().toPoint()));event->acceptProposedAction();}}
};
}
void qtPublishThumbnail(const std::string& id,const DecodedImage& image) {
    if(image.format!=ImagePixelFormat::RGBA8||!image.width||!image.height||image.width>std::uint32_t(std::numeric_limits<int>::max()/4)||image.height>std::uint32_t(std::numeric_limits<int>::max())||image.rgba.size()/4/std::size_t(image.width)<image.height)return;
    QImage copy(image.rgba.data(),int(image.width),int(image.height),int(image.width)*4,QImage::Format_RGBA8888);
    std::lock_guard lock(thumbnailMutex);const auto cost=std::max(1,int((copy.sizeInBytes()+1023)/1024));thumbnails.insert(QString::fromStdString(id),new QImage(copy.copy()),cost);thumbnailEpoch.fetch_add(1);
}
QImage qtThumbnail(const std::string& id){std::lock_guard lock(thumbnailMutex);const auto found=thumbnails.object(QString::fromStdString(id));return found?*found:QImage{};}
void qtClearThumbnails(){std::lock_guard lock(thumbnailMutex);thumbnails.clear();thumbnailEpoch.fetch_add(1);}
struct QtAssetBrowser::Impl {
    QObject connections;
    QtAssetBrowser& owner;AssetLibrary* assets{};AssetModel* model{};AssetView* view{};QLineEdit* search{};QComboBox* filter{};QComboBox* location{};QPushButton* importButton{};QLabel* status{};
    std::string folder;std::string displayedQuery;BrowserAssetFilter displayedFilter{};std::uint64_t revision{},icons{};BrowserActions actions;bool syncing{};
    explicit Impl(QtAssetBrowser& widget):owner(widget) {
        auto layout=new QVBoxLayout(&owner);layout->setContentsMargins(4,4,4,4);auto bar=new QHBoxLayout;layout->addLayout(bar);
        auto up=new QToolButton(&owner);up->setIcon(owner.style()->standardIcon(QStyle::SP_ArrowUp));up->setToolTip("Parent folder");bar->addWidget(up);
        location=new QComboBox(&owner);location->setMinimumContentsLength(12);bar->addWidget(location,1);
        search=new QLineEdit(&owner);search->setObjectName("CrimsonAssetSearch");search->setPlaceholderText("Search assets");bar->addWidget(search,2);
        filter=new QComboBox(&owner);for(const auto& name:{"All","Materials","Models","Textures","Scenes","Particles","Navigation Profiles","Gameplay Effects","Animations","Decals","Ragdolls"})filter->addItem(name);bar->addWidget(filter);
        auto import=new QPushButton("Import",&owner);importButton=import;bar->addWidget(import);auto create=new QToolButton(&owner);create->setText("Create");bar->addWidget(create);
        model=new AssetModel(&owner);view=new AssetView(&owner);view->setObjectName("CrimsonAssetList");view->setModel(model);view->setSelectionMode(QAbstractItemView::ExtendedSelection);view->setViewMode(QListView::IconMode);view->setResizeMode(QListView::Adjust);view->setUniformItemSizes(true);view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);view->setWordWrap(true);view->setContextMenuPolicy(Qt::CustomContextMenu);layout->addWidget(view);status=new QLabel(&owner);status->setWordWrap(true);layout->addWidget(status);
        QObject::connect(up,&QToolButton::clicked,&connections,[this]{if(assets){folder=assets->browserCatalog().parent(folder);revision=0;}});
        QObject::connect(location,&QComboBox::activated,&connections,[this](int index){folder=location->itemData(index).toString().toStdString();revision=0;});
        QObject::connect(search,&QLineEdit::textChanged,&connections,[this]{revision=0;});QObject::connect(filter,&QComboBox::activated,&connections,[this]{revision=0;});
        QObject::connect(import,&QPushButton::clicked,&connections,[this]{if(!assets||!assets->browserCatalog().writableFolder(folder))return;const auto names=QFileDialog::getOpenFileNames(&owner,"Import assets");std::vector<std::filesystem::path> paths;for(const auto& name:names)paths.emplace_back(name.toStdWString());if(!paths.empty())assets->queueImportFiles(paths,folder);});
        QObject::connect(create,&QToolButton::clicked,&connections,[this,create]{QMenu menu(create);createMenu(menu);menu.exec(create->mapToGlobal(QPoint(0,create->height())));});
        QObject::connect(view,&QListView::doubleClicked,&connections,[this](const QModelIndex& index){open(index);});
        QObject::connect(view,&QWidget::customContextMenuRequested,&connections,[this](QPoint point){context(point);});
        QObject::connect(view->selectionModel(),&QItemSelectionModel::selectionChanged,&connections,[this]{if(syncing||!assets)return;std::vector<std::string> ids;for(const auto& index:view->selectionModel()->selectedRows())ids.push_back(model->items[std::size_t(index.row())].id);assets->browserSelection().setAll(ids);});
        view->dropped=[this](const QMimeData* mime,const QModelIndex& index){if(!assets)return;auto target=folder;if(index.isValid()&&model->items[std::size_t(index.row())].folder)target=model->items[std::size_t(index.row())].id;if(!assets->browserCatalog().writableFolder(target))return;
            try{if(mime->hasUrls()){std::vector<std::filesystem::path> files;for(const auto& url:mime->urls())if(url.isLocalFile())files.emplace_back(url.toLocalFile().toStdWString());assets->queueImportFiles(files,target);}else if(mime->hasFormat(QtAssetMime)){const auto payload=nlohmann::json::parse(mime->data(QtAssetMime).toStdString());std::vector<std::string> ids;for(const auto& row:payload)ids.push_back(row.at("id").get<std::string>());assets->browserCatalog().move(ids,target);assets->noteAssetWrite("assets/browser.json");revision=0;}}catch(const std::exception& error){view->setToolTip(QString::fromUtf8(error.what()));}};
    }
    void createMenu(QMenu& menu) {
        if(!assets)return;const auto create=[this,&menu](const char* text,auto fn){auto action=menu.addAction(text);action->setEnabled(assets->browserCatalog().writableFolder(folder));QObject::connect(action,&QAction::triggered,&owner,fn);};
        create("Folder",[this]{bool ok=false;const auto name=QInputDialog::getText(&owner,"New Folder","Name",QLineEdit::Normal,"New Folder",&ok);if(ok&&!name.isEmpty()){try{assets->browserCatalog().createFolder(folder,name.toStdString());revision=0;}catch(const std::exception& e){view->setToolTip(e.what());}}});menu.addSeparator();
        create("Scene",[this]{actions.newScene=true;});create("Material",[this]{actions.createMaterial=true;actions.materialFolder=folder;});create("Blend Material",[this]{actions.createBlendMaterial=true;actions.materialFolder=folder;});create("Particle Effect",[this]{actions.createParticle=true;actions.particleFolder=folder;});create("Decal",[this]{actions.createDecal=true;actions.decalFolder=folder;});create("Navigation Agent Profile",[this]{actions.createNavigationAgentProfile=true;actions.navigationAgentProfileFolder=folder;});create("Gameplay Effect",[this]{actions.createGameplayEffect=true;actions.gameplayEffectFolder=folder;});
    }
    void open(const QModelIndex& index) {
        if(!index.isValid())return;const auto item=model->items[std::size_t(index.row())];if(item.folder){folder=item.id;revision=0;return;}
        switch(item.kind){case AssetKind::Scene:actions.openScene=item.id;break;case AssetKind::Material:actions.openMaterial=item.id;break;case AssetKind::Particle:actions.openParticle=item.id;break;case AssetKind::Animation:actions.openAnimation=item.id;break;case AssetKind::Ragdoll:actions.openRagdoll=item.id;break;case AssetKind::Decal:actions.openDecal=item.id;break;case AssetKind::NavigationAgentProfile:actions.openNavigationAgentProfile=item.id;break;case AssetKind::GameplayEffect:actions.openGameplayEffect=item.id;break;default:break;}
    }
    void context(QPoint point) {
        if(!assets)return;QMenu menu(view);const auto index=view->indexAt(point);if(index.isValid()) {
            if(!view->selectionModel()->isSelected(index))view->selectionModel()->select(index,QItemSelectionModel::ClearAndSelect|QItemSelectionModel::Rows);
            const auto item=model->items[std::size_t(index.row())];auto action=menu.addAction("Open");QObject::connect(action,&QAction::triggered,&menu,[this,id=item.id]{for(int row=0;row<model->rowCount();++row)if(model->items[std::size_t(row)].id==id){open(model->index(row));break;}});
            if(!item.folder&&(item.kind==AssetKind::NavigationAgentProfile||item.kind==AssetKind::Decal||item.kind==AssetKind::Ragdoll||item.kind==AssetKind::Animation||item.kind==AssetKind::GameplayEffect)) {
                auto duplicate=menu.addAction("Duplicate");QObject::connect(duplicate,&QAction::triggered,&menu,[this,item]{actions.duplicateAsset=item.id;actions.duplicateFolder=folder;});
            }
            auto references=menu.addAction("Find References");references->setEnabled(!item.folder);QObject::connect(references,&QAction::triggered,&connections,[this,item]{actions.findReferences=item.id;});
            auto rename=menu.addAction("Rename");rename->setEnabled(assets->browserCatalog().mutableItem(item.id));QObject::connect(rename,&QAction::triggered,&connections,[this,item]{bool ok=false;const auto name=QInputDialog::getText(&owner,"Rename","Name",QLineEdit::Normal,QString::fromStdString(item.name),&ok);if(ok)try{assets->browserCatalog().rename(item.id,name.toStdString());revision=0;}catch(const std::exception& e){view->setToolTip(e.what());}});
            auto erase=menu.addAction("Delete");erase->setEnabled(assets->browserCatalog().deletableItem(item.id));QObject::connect(erase,&QAction::triggered,&connections,[this]{for(const auto& index:view->selectionModel()->selectedRows())actions.deleteAssetIds.push_back(model->items[std::size_t(index.row())].id);});
            if(item.kind==AssetKind::Model){auto rig=menu.addAction("Create Ragdoll");QObject::connect(rig,&QAction::triggered,&connections,[this,item]{actions.createRagdollModel=item.id;actions.ragdollFolder=folder;});}menu.addSeparator();
        }auto submenu=menu.addMenu("Create");createMenu(*submenu);menu.exec(view->viewport()->mapToGlobal(point));
    }
};
QtAssetBrowser::QtAssetBrowser(QWidget* parent):QWidget(parent),impl_(std::make_unique<Impl>(*this)){}
QtAssetBrowser::~QtAssetBrowser(){std::lock_guard lock(thumbnailMutex);thumbnails.clear();thumbnailEpoch.fetch_add(1);}
void QtAssetBrowser::detach(){auto& p=*impl_;p.assets=nullptr;p.actions={};p.revision=0;p.folder.clear();p.syncing=true;p.model->replace({});p.location->clear();p.syncing=false;}
bool QtAssetBrowser::ownsInput()const{const auto focus=QApplication::focusWidget();const auto popup=QApplication::activePopupWidget();return (focus&&(focus==this||isAncestorOf(focus)))||(popup&&(popup==this||isAncestorOf(popup)));}
void QtAssetBrowser::synchronize(AssetLibrary& assets,int previewSize) {
    auto& p=*impl_;p.assets=&assets;auto& catalog=assets.browserCatalog();
    p.importButton->setEnabled(catalog.writableFolder(p.folder));const auto status=QString::fromStdString(assets.status().empty()?catalog.status():assets.status());if(p.status->text()!=status)p.status->setText(status);p.status->setVisible(!status.isEmpty());
    if(!p.folder.empty()&&!catalog.isFolder(p.folder))p.folder=BrowserCatalog::ContentFolder;
    p.view->setIconSize(QSize(previewSize,previewSize));p.view->setGridSize(QSize(std::max(76,previewSize+22),std::max(60,previewSize+42)));
    const auto text=p.search->text().toStdString();const auto filter=static_cast<BrowserAssetFilter>(p.filter->currentIndex());
    if(p.revision!=catalog.revision()||p.displayedQuery!=text||p.displayedFilter!=filter) {
        p.syncing=true;p.revision=catalog.revision();p.displayedQuery=text;p.displayedFilter=filter;p.model->replace(catalog.items(p.folder,text,filter));
        p.location->clear();p.location->addItem("Project",QString{});for(const auto& crumb:catalog.breadcrumbs(p.folder))p.location->addItem(QString::fromStdString(crumb.name),QString::fromStdString(crumb.id));p.location->setCurrentIndex(p.location->count()-1);
        for(int row=0;row<p.model->rowCount();++row)if(assets.browserSelection().contains(p.model->items[std::size_t(row)].id))p.view->selectionModel()->select(p.model->index(row),QItemSelectionModel::Select|QItemSelectionModel::Rows);p.syncing=false;
    }
    if(p.icons!=thumbnailEpoch.load()){p.icons=thumbnailEpoch.load();p.model->refreshIcons();}
}
BrowserActions QtAssetBrowser::takeActions(){return std::exchange(impl_->actions,{});}
std::vector<std::string> QtAssetBrowser::visibleIds()const {
    std::vector<std::string> result;const auto& p=*impl_;if(!isVisible())return result;const QRect viewport=p.view->viewport()->rect();const auto grid=p.view->gridSize();const int columns=std::max(1,viewport.width()/std::max(1,grid.width()));const int first=std::max(0,(p.view->verticalScrollBar()->value()/std::max(1,grid.height())-1)*columns);const int end=std::min(p.model->rowCount(),first+(viewport.height()/std::max(1,grid.height())+4)*columns);for(int row=first;row<end;++row){const auto index=p.model->index(row);if(!p.model->items[std::size_t(row)].folder&&p.view->visualRect(index).intersects(viewport))result.push_back(p.model->items[std::size_t(row)].id);}return result;
}
bool QtAssetBrowser::reveal(const std::string& id,AssetLibrary& assets){auto& p=*impl_;if(!assets.browserCatalog().contains(id))return false;p.folder=assets.browserCatalog().parent(id);p.search->clear();p.filter->setCurrentIndex(0);p.revision=0;assets.select(id);synchronize(assets,p.view->iconSize().width());for(int row=0;row<p.model->rowCount();++row)if(p.model->items[std::size_t(row)].id==id){p.view->setCurrentIndex(p.model->index(row));p.view->scrollTo(p.model->index(row));break;}return true;}
}
