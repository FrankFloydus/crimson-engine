#include "LevelAssetBrowser.h"
#include "QtAssetBrowser.h"
#include "integration/LevelSession.h"
#include "assets/AssetLibrary.h"
#include "resources/ResourceManager.h"
#include <QAbstractTableModel>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMainWindow>
#include <QPointer>
#include <QPushButton>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableView>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <atomic>
#include <limits>
#include <stop_token>
#include <stdexcept>
#include <utility>
#include <unordered_set>

namespace crimson::integration {
namespace {
class ReferenceModel final : public QAbstractTableModel {
public:
    explicit ReferenceModel(bool dependencies,QObject* parent):QAbstractTableModel(parent),dependencies_(dependencies){}
    int rowCount(const QModelIndex& parent={})const override{return parent.isValid()?0:int(std::min(rows().size(),std::size_t(std::numeric_limits<int>::max())));}
    int columnCount(const QModelIndex& parent={})const override{return parent.isValid()?0:4;}
    QVariant headerData(int section,Qt::Orientation orientation,int role)const override {
        if(orientation!=Qt::Horizontal||role!=Qt::DisplayRole||section<0||section>=4)return {};
        constexpr std::array names{"Name","Location","State","Entity"};return QString::fromUtf8(names[std::size_t(section)]);
    }
    QVariant data(const QModelIndex& index,int role)const override {
        if(!index.isValid()||index.row()<0||std::size_t(index.row())>=rows().size()||role!=Qt::DisplayRole)return {};
        const auto& row=rows()[std::size_t(index.row())];
        switch(index.column()) {
        case 0:if(!dependencies_)return QString::fromStdString(row.ownerName);
            if(const auto found=names_.find(row.targetId);found!=names_.end())return QString::fromStdString(found->second);
            return QString::fromStdString(row.targetId);
        case 1:return QString::fromStdString(row.slot);
        case 2:return row.editorState?QStringLiteral("Editor State"):row.historical?QStringLiteral("Undo / Redo"):
            row.active?(row.entityId?QStringLiteral("Current Scene"):QStringLiteral("Open Editor")):QStringLiteral("Saved");
        case 3:return row.entityId?QString::number(row.entityId,16):QString{};
        default:return {};
        }
    }
    void setReport(std::shared_ptr<const AssetReferenceReport> report) {
        beginResetModel();report_=std::move(report);names_.clear();
        if(report_)for(const auto& asset:report_->catalog)names_.emplace(asset.id,asset.name);
        endResetModel();
    }
    std::optional<AssetUsage> row(int index) const {
        if(index<0||std::size_t(index)>=rows().size())return {};
        return rows()[std::size_t(index)];
    }
private:
    const std::vector<AssetUsage>& rows()const {
        static const std::vector<AssetUsage> empty;
        return !report_?empty:dependencies_?report_->dependencies:report_->uses;
    }
    bool dependencies_{};
    std::shared_ptr<const AssetReferenceReport> report_;
    std::unordered_map<std::string,std::string> names_;
};

void mergeActions(BrowserActions& destination,BrowserActions source) {
    destination.newScene|=source.newScene;destination.createParticle|=source.createParticle;
    destination.createMaterial|=source.createMaterial;destination.createBlendMaterial|=source.createBlendMaterial;
    destination.createNavigationAgentProfile|=source.createNavigationAgentProfile;destination.createGameplayEffect|=source.createGameplayEffect;
    destination.createDecal|=source.createDecal;
    const auto move=[](std::string& to,std::string& from){if(!from.empty())to=std::move(from);};
    move(destination.openScene,source.openScene);move(destination.openMaterial,source.openMaterial);
    move(destination.openParticle,source.openParticle);move(destination.openNavigationAgentProfile,source.openNavigationAgentProfile);
    move(destination.openGameplayEffect,source.openGameplayEffect);move(destination.openAnimation,source.openAnimation);
    move(destination.openRagdoll,source.openRagdoll);move(destination.createRagdollModel,source.createRagdollModel);
    move(destination.openDecal,source.openDecal);
    if(source.createMaterial||source.createBlendMaterial)destination.materialFolder=std::move(source.materialFolder);
    if(source.createParticle)destination.particleFolder=std::move(source.particleFolder);
    if(source.createNavigationAgentProfile)destination.navigationAgentProfileFolder=std::move(source.navigationAgentProfileFolder);
    if(source.createGameplayEffect)destination.gameplayEffectFolder=std::move(source.gameplayEffectFolder);
    if(source.createDecal)destination.decalFolder=std::move(source.decalFolder);
    if(!source.ragdollFolder.empty())destination.ragdollFolder=std::move(source.ragdollFolder);
}
}

struct LevelAssetBrowser::Impl {
    QObject connections;
    LevelAssetBrowser& owner;
    QMainWindow& window;
    std::function<void()> requestFrame;
    std::function<bool()> prepareCommand;
    std::function<void(const std::string&,EntityId)> navigateScene;
    DeletionConfirmation confirmDeletion;
    LevelSession* session{};
    QPointer<QtAssetBrowser> browser;
    QPointer<QDockWidget> references;
    QTableView *uses{},*dependencies{};
    ReferenceModel *usesModel{},*dependencyModel{};
    QLabel* referenceStatus{};
    ResourceHandle<AssetReferenceReport> referenceRequest;
    std::stop_source cancellation;
    std::shared_ptr<const AssetReferenceReport> report;
    std::string target;
    std::uint64_t referenceRevision{};
    BrowserActions actions;
    std::unordered_map<std::string,std::uint64_t> imageRevisions;
    std::unordered_set<std::string> imageAssets;
    std::uint64_t imageCatalogRevision{};
    bool modal{},deletionOwned{};
    std::vector<std::string> deferredDeletion;

    Impl(LevelAssetBrowser& adapter,QMainWindow& main,std::function<void()> frame)
        :owner(adapter),window(main),requestFrame(std::move(frame)),browser(new QtAssetBrowser(&main)) {
        browser->setObjectName(QStringLiteral("CrimsonEngineAssetBrowser"));
        references=new QDockWidget(QStringLiteral("Asset References"),&window);
        references->setObjectName(QStringLiteral("CrimsonEngineAssetReferences"));
        auto body=new QWidget(references);auto layout=new QVBoxLayout(body);
        auto toolbar=new QHBoxLayout;referenceStatus=new QLabel(body);toolbar->addWidget(referenceStatus,1);
        auto refresh=new QPushButton(QStringLiteral("Refresh"),body);toolbar->addWidget(refresh);layout->addLayout(toolbar);
        auto tabs=new QTabWidget(body);uses=new QTableView(tabs);dependencies=new QTableView(tabs);
        usesModel=new ReferenceModel(false,uses);dependencyModel=new ReferenceModel(true,dependencies);
        uses->setModel(usesModel);dependencies->setModel(dependencyModel);
        for(auto table:{uses,dependencies}) {
            table->setSelectionBehavior(QAbstractItemView::SelectRows);table->setEditTriggers(QAbstractItemView::NoEditTriggers);
            table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
            table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
            table->horizontalHeader()->setStretchLastSection(true);
        }
        tabs->addTab(uses,QStringLiteral("Used By"));tabs->addTab(dependencies,QStringLiteral("Dependencies"));
        layout->addWidget(tabs);references->setWidget(body);window.addDockWidget(Qt::BottomDockWidgetArea,references);references->hide();
        QObject::connect(refresh,&QPushButton::clicked,&connections,[this]{owner.requestReferences(target);});
        QObject::connect(references,&QDockWidget::visibilityChanged,&connections,[this](bool visible){if(!visible)owner.cancelReferences();});
        const auto navigate=[this](ReferenceModel* model,bool dependency,const QModelIndex& index){
            if(!session)return;const auto row=model->row(index.row());if(!row)return;
            if(!dependency&&row->entityId) {
                if(row->ownerId==session->document().assetId()) {
                    if(prepareCommand&&!prepareCommand())return;
                    if(session->document().scene().contains(row->entityId))session->document().select({row->entityId});
                } else if(!row->historical&&!row->editorState&&navigateScene)navigateScene(row->ownerId,row->entityId);
            } else owner.reveal(dependency?row->targetId:row->ownerId);
            if(requestFrame)requestFrame();
        };
        QObject::connect(uses,&QTableView::doubleClicked,&connections,[this,navigate](const QModelIndex& index){navigate(usesModel,false,index);});
        QObject::connect(dependencies,&QTableView::doubleClicked,&connections,[this,navigate](const QModelIndex& index){navigate(dependencyModel,true,index);});
    }
    ~Impl(){delete browser.data();delete references.data();}
    void notify(const std::string& message){window.statusBar()->showMessage(QString::fromStdString(message),8000);}
    bool confirm(const AssetDeletionPlan& plan,std::unordered_map<std::string,std::string>& replacements) {
        if(confirmDeletion)return confirmDeletion(plan,replacements);
        QDialog dialog(&window);dialog.setObjectName(QStringLiteral("CrimsonAssetDeletion"));dialog.setWindowTitle(QStringLiteral("Delete Assets"));
        auto layout=new QVBoxLayout(&dialog);
        layout->addWidget(new QLabel(QStringLiteral("Delete %1 assets and %2 folders?").arg(plan.assetIds.size()).arg(plan.folderIds.size()),&dialog));
        auto form=new QFormLayout;layout->addLayout(form);
        const auto& assets=session->assets();
        for(const auto& id:plan.assetIds) {
            if(std::none_of(plan.warningUsages.begin(),plan.warningUsages.end(),[&](const auto& usage){return usage.targetId==id;}))continue;
            auto picker=new QComboBox(&dialog);picker->addItem(QStringLiteral("Default / Clear"),QString{});
            for(const auto& candidate:assets.fallbackCandidates(id))
                if(std::find(plan.assetIds.begin(),plan.assetIds.end(),candidate.id)==plan.assetIds.end())
                    picker->addItem(QString::fromStdString(candidate.name),QString::fromStdString(candidate.id));
            form->addRow(QString::fromStdString(assets.displayName(id)),picker);
            QObject::connect(picker,&QComboBox::activated,&dialog,[&replacements,id,picker]{replacements[id]=picker->currentData().toString().toStdString();});
        }
        if(!plan.warningUsages.empty()) {
            auto table=new QTableView(&dialog);auto model=new ReferenceModel(false,table);
            auto warnings=std::make_shared<AssetReferenceReport>();warnings->uses=plan.warningUsages;model->setReport(std::move(warnings));
            table->setModel(model);table->setEditTriggers(QAbstractItemView::NoEditTriggers);table->horizontalHeader()->setStretchLastSection(true);layout->addWidget(table);
        }
        auto buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);
        buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Delete"));buttons->button(QDialogButtonBox::Cancel)->setDefault(true);
        layout->addWidget(buttons);QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
        QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        modal=true;const auto result=dialog.exec();modal=false;return result==QDialog::Accepted;
    }
    void updateReferences() {
        if(!session)return;
        if(referenceRequest&&session->assets().referenceReportRevision(&session->document())!=referenceRevision) {
            owner.cancelReferences();referenceStatus->setText(QStringLiteral("Stale — Refresh"));
        }
        if(referenceRequest&&referenceRequest.state()==ResourceState::Failed) {
            referenceStatus->setText(QString::fromStdString(referenceRequest.diagnostic()));referenceRequest={};
        }
        if(referenceRequest&&referenceRequest.state()==ResourceState::Ready) {
            report=referenceRequest.snapshot().shared();referenceRequest={};
            usesModel->setReport(report);dependencyModel->setReport(report);
        }
        if(report)referenceStatus->setText(QString::fromStdString(report->targetName)+(owner.referencesStale()?QStringLiteral(" — Stale"):QString{}));
    }
};

LevelAssetBrowser::LevelAssetBrowser(QMainWindow& main,std::function<void()> frame):impl_(std::make_unique<Impl>(*this,main,std::move(frame))){}
LevelAssetBrowser::~LevelAssetBrowser(){bind(nullptr);}
QWidget* LevelAssetBrowser::widget()const{return impl_->browser.data();}
void LevelAssetBrowser::bind(LevelSession* session) {
    auto& state=*impl_;if(state.session==session)return;
    cancelReferences();
    if(state.session){state.session->setAssetPreviewPublisher({});if(state.deletionOwned)state.session->assets().cancelDeletion();}
    if(state.browser)state.browser->detach();state.session=session;state.actions={};state.deferredDeletion.clear();state.deletionOwned=false;
    state.imageRevisions.clear();state.imageAssets.clear();state.imageCatalogRevision=0;
    state.report.reset();state.target.clear();state.referenceRevision=0;
    if(state.references){state.usesModel->setReport({});state.dependencyModel->setReport({});state.referenceStatus->clear();}
    qtClearThumbnails();
    if(session) {
        session->setAssetPreviewPublisher(qtPublishThumbnail,[](const std::string& id){return !qtThumbnail(id).isNull();});
        state.browser->synchronize(session->assets(),128);
    }
}
void LevelAssetBrowser::update(std::uint32_t frame,bool pauseThumbnails,int previewSize,
    std::span<const std::string> materialPreviews) {
    auto& state=*impl_;if(!state.session)return;
    auto& session=*state.session;auto& assets=session.assets();
    if(state.deletionOwned) {
        if(!assets.deletionPending()||assets.pumpDeletion(&session.document()))state.deletionOwned=false;
        if(!assets.deletionPending()&&!assets.status().empty())state.notify(assets.status());
    }
    if(!session.playing()&&!state.deferredDeletion.empty()) {
        auto ids=std::exchange(state.deferredDeletion,{});requestDeletion(ids);
    }
    state.browser->synchronize(assets,std::clamp(previewSize,16,512));
    auto actions=state.browser->takeActions();
    if(!actions.findReferences.empty())requestReferences(actions.findReferences);
    if(!actions.deleteAssetIds.empty())requestDeletion(actions.deleteAssetIds);
    if(!actions.duplicateAsset.empty())requestDuplicate(actions.duplicateAsset,actions.duplicateFolder);
    mergeActions(state.actions,std::move(actions));state.updateReferences();
    auto visible=state.browser->visibleIds();
    // Publish one demand set so off-browser paint swatches and browser cards
    // retain their existing shared preparation and thumbnail owners.
    for(const auto& id:materialPreviews)if(!id.empty()&&std::find(visible.begin(),visible.end(),id)==visible.end())visible.push_back(id);
    if(!pauseThumbnails) {
        const auto edge=std::uint32_t(std::clamp(previewSize,16,512));
        session.pumpAssetPreviews(frame,visible,edge);
        if(state.imageCatalogRevision!=assets.catalogRevision()) {
            state.imageCatalogRevision=assets.catalogRevision();state.imageAssets.clear();
            for(const auto& asset:assets.catalog())if(asset.kind==AssetKind::Texture||asset.kind==AssetKind::Environment)state.imageAssets.insert(asset.id);
            std::erase_if(state.imageRevisions,[&](const auto& item){return !state.imageAssets.contains(item.first);});
        }
        for(const auto& id:visible)if(state.imageAssets.contains(id))if(const auto image=assets.thumbnailPixels(id,edge))
            if(state.imageRevisions[id]!=image.revision()||qtThumbnail(id).isNull()) {
                qtPublishThumbnail(id,*image);state.imageRevisions[id]=image.revision();
            }
    }
}
BrowserActions LevelAssetBrowser::takeActions(){return std::exchange(impl_->actions,{});}
bool LevelAssetBrowser::reveal(const std::string& id){return impl_->session&&impl_->browser->reveal(id,impl_->session->assets());}
bool LevelAssetBrowser::ownsInput()const {
    if(impl_->browser&&impl_->browser->ownsInput())return true;
    const auto focus=QApplication::focusWidget();const auto popup=QApplication::activePopupWidget();const auto dock=impl_->references.data();
    return impl_->modal||(dock&&((focus&&(focus==dock||dock->isAncestorOf(focus)))||(popup&&(popup==dock||dock->isAncestorOf(popup)))));
}
bool LevelAssetBrowser::commandPending()const{return impl_->modal||impl_->deletionOwned||!impl_->deferredDeletion.empty();}
void LevelAssetBrowser::setPrepareCommand(std::function<bool()> callback){impl_->prepareCommand=std::move(callback);}
void LevelAssetBrowser::setNavigateScene(std::function<void(const std::string&,EntityId)> callback){impl_->navigateScene=std::move(callback);}
void LevelAssetBrowser::setDeletionConfirmation(DeletionConfirmation callback){impl_->confirmDeletion=std::move(callback);}
bool LevelAssetBrowser::requestReferences(const std::string& id) {
    auto& state=*impl_;if(!state.session||id.empty())return false;
    cancelReferences();state.report.reset();state.usesModel->setReport({});state.dependencyModel->setReport({});state.target=id;
    state.references->show();state.references->raise();state.referenceStatus->setText(QStringLiteral("Loading…"));
    state.cancellation=std::stop_source{};
    try {
        auto input=state.session->assets().referenceIndexInput(&state.session->document());state.referenceRevision=input.requestRevision;
        auto cancellation=state.cancellation;static std::atomic_uint64_t sequence{1};
        ResourceRequestOptions options;options.priority=ResourcePriority::Background;options.cancelWhenUnused=true;
        options.retainLoader=false;options.dependencyValidation=DependencyValidation::FileState;
        for(const auto& location:input.locations)options.dependencies.push_back(location.file);
        state.referenceRequest=state.session->resources().requestDerived<AssetReferenceReport>("qt-references-"+std::to_string(sequence.fetch_add(1)),
            [input=std::move(input),id,cancellation](std::stop_token stop)mutable {
                std::stop_callback stopping(stop,[&]{cancellation.request_stop();});
                auto result=std::make_shared<AssetReferenceReport>(makeAssetReferenceReport(input,id,cancellation.get_token()));
                return ResourceLoadResult<AssetReferenceReport>{result,sizeof(AssetReferenceReport)+(result->uses.size()+result->dependencies.size())*sizeof(AssetUsage),result->requestRevision};
            },std::move(options));
        if(state.requestFrame)state.requestFrame();return bool(state.referenceRequest);
    } catch(const std::exception& error){state.referenceStatus->setText(QString::fromUtf8(error.what()));return false;}
}
void LevelAssetBrowser::cancelReferences(){auto& state=*impl_;state.cancellation.request_stop();state.referenceRequest={};}
std::shared_ptr<const AssetReferenceReport> LevelAssetBrowser::referenceReport()const{return impl_->report;}
bool LevelAssetBrowser::referencesPending()const{return bool(impl_->referenceRequest);}
bool LevelAssetBrowser::referencesStale()const{return impl_->session&&impl_->referenceRevision&&impl_->session->assets().referenceReportRevision(&impl_->session->document())!=impl_->referenceRevision;}
bool LevelAssetBrowser::requestDeletion(const std::vector<std::string>& ids) {
    auto& state=*impl_;if(!state.session||ids.empty()||state.deletionOwned)return false;
    if(state.session->playing()){state.deferredDeletion=ids;return true;}
    if(state.prepareCommand&&!state.prepareCommand())return false;
    auto& assets=state.session->assets();auto& document=state.session->document();
    try {
        const auto plan=assets.planDeletion(ids,&document);
        if(!plan.error.empty()){state.notify(plan.error);return false;}
        std::unordered_map<std::string,std::string> replacements;
        if(!state.confirm(plan,replacements))return false;
        const auto result=assets.deleteAssets(plan,replacements,&document);state.deletionOwned=assets.deletionPending();
        if(!result&&!state.deletionOwned)state.notify(assets.status());
        if(state.requestFrame)state.requestFrame();return result||state.deletionOwned;
    } catch(const std::exception& error){state.notify(error.what());return false;}
}
bool LevelAssetBrowser::requestDuplicate(const std::string& id,const std::string& folder) {
    auto& state=*impl_;if(!state.session||id.empty()||commandPending()||state.session->playing())return false;
    if(state.prepareCommand&&!state.prepareCommand())return false;
    auto& assets=state.session->assets();
    const auto found=std::find_if(assets.catalog().begin(),assets.catalog().end(),[&](const auto& entry){return entry.id==id;});
    if(found==assets.catalog().end())return false;
    const auto kind=found->kind;
    try {
        const auto accept=[&](const auto& copy,std::string& open_action) {
            if(!copy)throw std::runtime_error("The asset could not be duplicated.");
            if(assets.browserCatalog().writableFolder(folder)) {
                const std::array<std::string,1> ids{copy->id};assets.browserCatalog().move(ids,folder);
            }
            assets.select(copy->id);state.browser->reveal(copy->id,assets);open_action=copy->id;
            if(state.requestFrame)state.requestFrame();return true;
        };
        switch(kind) {
        case AssetKind::NavigationAgentProfile:return accept(assets.copyNavigationAgentProfile(id),state.actions.openNavigationAgentProfile);
        case AssetKind::Decal:return accept(assets.copyDecal(id),state.actions.openDecal);
        case AssetKind::Ragdoll:return accept(assets.copyRagdoll(id),state.actions.openRagdoll);
        case AssetKind::Animation:return accept(assets.copyAnimation(id),state.actions.openAnimation);
        case AssetKind::GameplayEffect:return accept(assets.copyGameplayEffect(id),state.actions.openGameplayEffect);
        default:return false;
        }
    }catch(const std::exception& error){state.notify(error.what());return false;}
}
}
