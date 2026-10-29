#include "AssetDocuments.h"
#include "LevelSession.h"
#include "widgets/QtDocumentProperties.h"
#include "widgets/QtMaterialGraph.h"
#include "browser/QtAssetBrowser.h"
#include "assets/AssetLibrary.h"
#include "assets/ContentStorage.h"
#include "resources/ResourceManager.h"
#include "resources/TextureStreamer.h"
#include <crimson/renderer/forward/MaterialGraphImageBake.h>
#include "editor/AnimationDocument.h"
#include "editor/CollisionLayersDocument.h"
#include "editor/DecalDocument.h"
#include "editor/GameplayEffectDocument.h"
#include "editor/MaterialDocument.h"
#include "editor/NavigationAgentProfileDocument.h"
#include "editor/ParticleDocument.h"
#include "editor/RagdollDocument.h"
#include "ui/qt_unsaved_confirmation_dialog.hpp"
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QFile>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QStatusBar>
#include <QTreeWidget>
#include <algorithm>
#include <map>
#include <stdexcept>
#include <type_traits>
#include <tuple>
#include <variant>

namespace crimson::integration {
namespace {
using Json=nlohmann::json;
using Domain=std::variant<std::unique_ptr<MaterialDocument>,std::unique_ptr<ParticleEffectDocument>,
    std::unique_ptr<DecalDocument>,std::unique_ptr<RagdollDocument>,std::unique_ptr<AnimationDocument>,
    std::unique_ptr<NavigationAgentProfileDocument>,std::unique_ptr<GameplayEffectDocument>,
    std::unique_ptr<CollisionLayersDocument>>;
template<class D> constexpr bool IsLayers=std::is_same_v<D,CollisionLayersDocument>;
template<class D> bool documentOpen(const D& document) {if constexpr(IsLayers<D>)return true;else return document.isOpen();}
template<class D> bool editable(const D& document) {if constexpr(requires {document.editable();})return document.editable();else return true;}
template<class D> std::string identity(const D& document) {if constexpr(IsLayers<D>)return {};else return document.assetId();}
template<class D> std::string documentName(const D& document) {if constexpr(IsLayers<D>)return "Collision Layers";else return document.definition().name;}
template<class D> std::uint64_t previewRevision(const D& document) {if constexpr(IsLayers<D>)return document.revision();else return document.previewRevision();}
template<class D> bool load(D& document,const std::string& id) {if constexpr(IsLayers<D>)return document.reload(true);else return document.load(id);}
template<class D> bool saveDocument(D& document,const std::string& name,bool saveAs,bool overwrite) {
    if constexpr(IsLayers<D>){(void)name;(void)saveAs;return document.save(overwrite);}
    else if constexpr(std::is_same_v<D,RagdollDocument>)return document.save(name,saveAs,overwrite);
    else {(void)overwrite;return document.save(name,saveAs);}
}

Json snapshot(const MaterialDocument& document,MaterialNodeId selected) {
    const auto& asset=document.definition();auto value=Json::parse(serializeMaterialAsset(asset));
    if(asset.blendDefinition)return value;
    if(!selected)selected=materialOutputNode(asset.graph).id;
    for(auto node:value.at("graph").at("nodes"))if(node.at("id").get<MaterialNodeId>()==selected){node["name"]=findMaterialNode(asset.graph,selected)->name;return node;}
    return Json{{"name",asset.name}};
}
Json snapshot(const ParticleEffectDocument& document,MaterialNodeId){return Json::parse(serializeParticleAsset(document.definition()));}
Json snapshot(const DecalDocument& document,MaterialNodeId){return Json::parse(serializeDecalAsset(document.definition()));}
Json snapshot(const RagdollDocument& document,MaterialNodeId){return Json::parse(serializeRagdollAsset(document.definition()));}
Json snapshot(const AnimationDocument& document,MaterialNodeId){return Json::parse(serializeAnimationAsset(document.definition()));}
Json snapshot(const NavigationAgentProfileDocument& document,MaterialNodeId){return Json::parse(serializeNavigationAgentProfile(document.definition()));}
Json snapshot(const GameplayEffectDocument& document,MaterialNodeId){return Json::parse(serializeGameplayEffectAsset(document.definition()));}
Json snapshot(const CollisionLayersDocument& document,MaterialNodeId){return Json{{"layers",document.names()}};}

void assign(MaterialDocument& document,const Json& value) {
    auto candidate=document.definition();
    if(candidate.blendDefinition){candidate.name=value.at("name").get<std::string>();candidate.blendDefinition=parse_blend_material_definition(value.at("blend").dump(),material_graph_image_policy());}
    else if(value.contains("id")){auto graph=Json::parse(serializeMaterialGraph(candidate.graph));for(auto& node:graph.at("nodes"))if(node.at("id")==value.at("id")){node=value;break;}candidate.graph=parseMaterialGraph(graph.dump());}
    else if(value.contains("name"))candidate.name=value.at("name").get<std::string>();
    canonicalizeMaterialAsset(candidate);if(!document.update([&](auto& target){target=std::move(candidate);}))throw std::runtime_error(document.status());
}
void assign(ParticleEffectDocument& document,const Json& value){const auto candidate=deserializeParticleAsset(value.dump(),document.assetId());if(!document.update([&](auto& target){target=candidate;}))throw std::runtime_error(document.status());}
void assign(DecalDocument& document,const Json& value){const auto candidate=deserializeDecalAsset(value.dump(),document.assetId());if(!document.update([&](auto& target){target=candidate;}))throw std::runtime_error(document.status());}
void assign(RagdollDocument& document,const Json& value){const auto candidate=deserializeRagdollAsset(value.dump(),document.assetId());if(!document.update([&](auto& target){target=candidate;}))throw std::runtime_error(document.status());}
void assign(AnimationDocument& document,const Json& value){const auto candidate=deserializeAnimationAsset(value.dump(),document.assetId());if(!document.update([&](auto& target){target=candidate;}))throw std::runtime_error(document.status());}
void assign(NavigationAgentProfileDocument& document,const Json& value){const auto candidate=deserializeNavigationAgentProfile(value.dump(),document.assetId());if(!document.update([&](auto& target){target=candidate;}))throw std::runtime_error(document.status());}
void assign(GameplayEffectDocument& document,const Json& value){const auto candidate=deserializeGameplayEffectAsset(value.dump(),document.assetId());if(!document.update([&](auto& target){target=candidate;}))throw std::runtime_error(document.status());}
void assign(CollisionLayersDocument& document,const Json& value){const auto candidate=value.at("layers").get<CollisionLayerNames>();if(!document.update([&](auto& target){target=candidate;}))throw std::runtime_error(document.status());}

void publish(AssetLibrary& assets,const MaterialDocument& document){assets.publishMaterial(document.committed());}
void publish(AssetLibrary& assets,const ParticleEffectDocument& document){assets.publishParticle(document.committed());}
void publish(AssetLibrary& assets,const DecalDocument& document){assets.publishDecal(document.committed());}
void publish(AssetLibrary& assets,const RagdollDocument& document){assets.publishRagdoll(document.committed());}
void publish(AssetLibrary& assets,const AnimationDocument& document){assets.publishAnimation(document.committed());}
void publish(AssetLibrary& assets,const NavigationAgentProfileDocument& document){assets.publishNavigationAgentProfile(document.committed());}
void publish(AssetLibrary& assets,const GameplayEffectDocument& document){assets.publishGameplayEffect(document.committed());}
void publish(AssetLibrary&,const CollisionLayersDocument&){}
void retain(AssetLibrary& assets,const MaterialDocument& document){assets.protectMaterialDocument(document.assetId());assets.setMaterialDocumentReferences(materialTextureReferences(document.definition()),document.historicalTextureReferences(),materialMaterialReferences(document.definition()),document.historicalMaterialReferences());}
void retain(AssetLibrary& assets,const ParticleEffectDocument& document){assets.protectParticleDocument(document.assetId());assets.setParticleDocumentReferences({document.definition().emitter.material.id},document.historicalMaterialReferences());}
void retain(AssetLibrary& assets,const DecalDocument& document){assets.protectDecalDocument(document.assetId());assets.setDecalDocumentReferences(decalMaterialReferences(document.definition()),document.historicalMaterialReferences());}
void retain(AssetLibrary& assets,const RagdollDocument& document){assets.protectRagdollDocument(document.assetId());assets.setRagdollDocumentReferences(document.modelReferences(),document.modelReferences(true));}
void retain(AssetLibrary& assets,const AnimationDocument& document){assets.protectAnimationDocument(document.assetId());assets.setAnimationDocumentReferences(document.modelReferences(),document.modelReferences(true));}
void retain(AssetLibrary& assets,const NavigationAgentProfileDocument& document){assets.protectNavigationAgentProfileDocument(document.assetId());}
void retain(AssetLibrary& assets,const GameplayEffectDocument& document){assets.protectGameplayEffectDocument(document.assetId());}
void retain(AssetLibrary&,const CollisionLayersDocument&){}
template<class Document> void release(AssetLibrary& assets,const Document&){
    if constexpr(std::is_same_v<Document,MaterialDocument>){assets.protectMaterialDocument({});assets.setMaterialDocumentReferences({},{});}
    else if constexpr(std::is_same_v<Document,ParticleEffectDocument>){assets.protectParticleDocument({});assets.setParticleDocumentReferences({},{});}
    else if constexpr(std::is_same_v<Document,DecalDocument>){assets.protectDecalDocument({});assets.setDecalDocumentReferences({},{});}
    else if constexpr(std::is_same_v<Document,RagdollDocument>){assets.protectRagdollDocument({});assets.setRagdollDocumentReferences({},{});}
    else if constexpr(std::is_same_v<Document,AnimationDocument>){assets.protectAnimationDocument({});assets.setAnimationDocumentReferences({},{});}
    else if constexpr(std::is_same_v<Document,NavigationAgentProfileDocument>)assets.protectNavigationAgentProfileDocument({});
    else if constexpr(std::is_same_v<Document,GameplayEffectDocument>)assets.protectGameplayEffectDocument({});
}
template<class Document> bool reconcile(AssetLibrary&,Document&){return true;}
bool reconcile(AssetLibrary& assets,MaterialDocument& document){const auto& policy=assets.references();return document.reconcileTextureReferences([&](const std::string& id,MaterialTextureRole role){return policy.deleted(id)?policy.resolveTexture(id,role==MaterialTextureRole::Normal):id;},[&](const std::string& id){return policy.deleted(id)?policy.resolveMaterial(id):id;});}
bool reconcile(AssetLibrary& assets,DecalDocument& document){return document.reconcileMaterialReferences([&](const std::string& id){return assets.references().resolveDecalMaterial(id);});}

class DocumentWindow final : public QMainWindow {
public:
    Domain domain;
    AssetLibrary& assets;
    AssetKind kind;
    QtDocumentProperties* properties{};
    QtMaterialGraph* graph{};
    QSplitter* splitter{};
    QPointer<QWidget> preview;
    QAction *saveAction{},*saveAsAction{},*undoAction{},*redoAction{};
    std::function<void()> requestFrame;
    std::function<void(DocumentWindow*)> beforeClose;
    quader::QtUnsavedConfirmationDialog unsaved;
    std::filesystem::path layoutFile;
    std::string message;
    std::uint64_t publishedRevision{UINT64_MAX};
    std::uint64_t referencePreview{UINT64_MAX},referencePolicy{UINT64_MAX};std::string referenceId;
    bool layerDocument{},closing{};
    struct NodeImage {std::shared_ptr<const DecodedImage> pixels;QImage image;};
    std::shared_ptr<const MaterialGraph> image_graph;
    std::uint64_t image_revision{UINT64_MAX};
    std::string image_document;
    std::map<std::tuple<MaterialNodeId,MaterialPin,unsigned>,NodeImage> node_images;
    MaterialPreviewMode preview_mode{MaterialPreviewMode::ThreeD};
    std::optional<MaterialGraphPreviewTarget> classified_target;
    std::uint64_t classified_revision{UINT64_MAX};
    bool static_image{};
    BlendMaterialPreviewSelection blend_preview,cached_blend_preview;
    struct BlendSource {std::string id;std::shared_ptr<const MaterialAsset> material;std::uint64_t revision{};};
    std::vector<BlendSource> blend_sources;
    std::shared_ptr<const MaterialAsset> blend_preview_material;
    std::optional<MaterialGraphPreviewTarget> image_target;

    template<class Document> DocumentWindow(QMainWindow& parent,AssetLibrary& library,AssetKind type,
        std::unique_ptr<Document> document,std::filesystem::path root,std::function<void()> requestCallback)
        :QMainWindow(&parent,Qt::Window),domain(std::move(document)),assets(library),kind(type),requestFrame(std::move(requestCallback)),
         unsaved(this),layoutFile(std::move(root)/".crimson/engine-document-layout.ini"),layerDocument(IsLayers<Document>) {
        setAttribute(Qt::WA_DeleteOnClose,false);resize(1100,740);
        auto* file=menuBar()->addMenu("File");saveAction=file->addAction("Save");saveAction->setShortcut(QKeySequence::Save);
        saveAsAction=file->addAction("Save As…");saveAsAction->setShortcut(QKeySequence::SaveAs);saveAsAction->setEnabled(!layerDocument);
        file->addSeparator();auto* close=file->addAction("Close");close->setShortcut(QKeySequence::Close);
        auto* edit=menuBar()->addMenu("Edit");undoAction=edit->addAction("Undo");undoAction->setShortcut(QKeySequence::Undo);
        redoAction=edit->addAction("Redo");redoAction->setShortcuts({QKeySequence::Redo,QKeySequence("Ctrl+Shift+Z")});
        splitter=new QSplitter(this);properties=new QtDocumentProperties(splitter);splitter->addWidget(properties);setCentralWidget(splitter);
        if constexpr(std::is_same_v<Document,MaterialDocument>){auto& material=*std::get<std::unique_ptr<MaterialDocument>>(domain);image_document=material.assetId()+":native-graph";if(!material.definition().blendDefinition){graph=new QtMaterialGraph(material,splitter);splitter->addWidget(graph);splitter->setSizes({340,760});
            graph->selectionChanged=[this]{finish();properties->resetProjection();request();};
            graph->previewChanged=[this](auto){request();};
            graph->releaseImageCache=[this]{releaseImageCache();};
            graph->nodeImage=[this](MaterialGraphPreviewTarget target){return nodeImage(target,128);};
            graph->validTexture=[this](const std::string& id){const auto* texture=assets.find(id);return texture&&!texture->hdr;};
            graph->chooseTexture=[this](MaterialNodeId node){QStringList labels{"None"};std::vector<std::string> ids{{}};for(const auto& texture:assets.assets())if(!texture.hdr){labels.push_back(QString::fromStdString(texture.name));ids.push_back(texture.id);}bool accepted=false;const auto selected=QInputDialog::getItem(this,"Texture","Texture",labels,0,false,&accepted);if(!accepted)return;const auto index=labels.indexOf(selected);if(index<0)return;finish();auto& material=*std::get<std::unique_ptr<MaterialDocument>>(domain);material.edit("Assign texture",[&](auto& asset){if(auto* target=findMaterialNode(asset.graph,node))target->texture.image.id=ids[std::size_t(index)];});refresh();request();};
        }}
        properties->bind([this]{return visit([this](const auto& doc){return snapshot(doc,graph?graph->selectedNode():0);});},
            [this](const Json& value,const std::string& label,bool final){try{visit([&](auto& doc){
                using D=std::remove_cvref_t<decltype(doc)>;
                if constexpr(std::is_same_v<D,DecalDocument>){const auto candidate=deserializeDecalAsset(value.dump(),doc.assetId());if(candidate.material.id!=doc.definition().material.id&&!candidate.material.id.empty()){const auto material=assets.findMaterial(candidate.material.id);if(!material||materialOutputNode(material->graph).output.shading!=MaterialShading::Decal)throw std::runtime_error("Choose a material with Shader Model: Decal.");}}
                if(!doc.transactionActive()&&!doc.begin(label))throw std::runtime_error(doc.status());assign(doc,value);if(final&&!doc.commit())throw std::runtime_error(doc.status());});message.clear();}catch(const std::exception& error){message=error.what();visit([](auto& doc){doc.cancel();});}refresh(false);request();},
            [this]{finish();},[this]{cancel();});
        properties->setChoices([this](const std::string& path){QStringList options;const bool material=path.find("material")!=std::string::npos,image=path.find("image")!=std::string::npos,model=path.find("sourceModel")!=std::string::npos;
            if(material||image||model)for(const auto& entry:assets.catalog())if((material&&entry.kind==AssetKind::Material)||(image&&entry.kind==AssetKind::Texture)||(model&&entry.kind==AssetKind::Model)){
                if(kind==AssetKind::Decal&&material){const auto candidate=assets.findMaterial(entry.id);if(!candidate||materialOutputNode(candidate->graph).output.shading!=MaterialShading::Decal)continue;}
                options.push_back(QString::fromStdString(assets.displayName(entry.id))+"\n"+QString::fromStdString(entry.id));
            }return options;});
        connect(saveAction,&QAction::triggered,this,[this]{save(false);});connect(saveAsAction,&QAction::triggered,this,[this]{save(true);});
        connect(close,&QAction::triggered,this,[this]{this->close();});connect(undoAction,&QAction::triggered,this,[this]{undo();});connect(redoAction,&QAction::triggered,this,[this]{redo();});
        auto* escape=new QShortcut(QKeySequence(Qt::Key_Escape),this);connect(escape,&QShortcut::activated,this,[this]{cancel();});
        QSettings layout(QString::fromStdWString(layoutFile.wstring()),QSettings::IniFormat);const auto geometry=layout.value(layoutKey()+"/geometry").toByteArray();if(!geometry.isEmpty())restoreGeometry(geometry);
        refresh();
    }
    ~DocumentWindow() override {properties->detach();if(graph){graph->selectionChanged={};graph->previewChanged={};delete graph;graph=nullptr;}releaseImageCache();visit([&](auto& doc){doc.cancel();release(assets,doc);});}
    template<class Function> decltype(auto) visit(Function&& function){return std::visit([&](auto& document)->decltype(auto){return function(*document);},domain);}
    template<class Function> decltype(auto) visit(Function&& function)const{return std::visit([&](const auto& document)->decltype(auto){return function(*document);},domain);}
    QString layoutKey() const{return layerDocument?QStringLiteral("CollisionLayers"):QString::number(static_cast<int>(kind));}
    std::string id()const{return visit([](const auto& doc){return identity(doc);});}
    void request(){if(requestFrame)requestFrame();}
    void releaseImageCache(){node_images.clear();image_graph.reset();image_revision=UINT64_MAX;classified_revision=UINT64_MAX;classified_target.reset();blend_sources.clear();blend_preview_material.reset();image_target.reset();if(!image_document.empty())assets.releaseMaterialNodeImagePreviews(image_document);}
    void updateImageGraph() {
        const auto& material=*std::get<std::unique_ptr<MaterialDocument>>(domain);
        if(material.definition().blendDefinition&&blend_preview.channel) {
            bool changed=image_revision!=material.previewRevision()||cached_blend_preview!=blend_preview;
            if(image_revision!=material.previewRevision()) {
                blend_sources.clear();for(const auto& id:materialMaterialReferences(material.definition()))blend_sources.push_back({id,{},{}});
            }
            for(auto& source:blend_sources){const auto current=assets.findMaterial(source.id);const auto revision=assets.materialRevision(source.id);changed|=current!=source.material||revision!=source.revision;source.material=current;source.revision=revision;}
            if(!changed)return;
            std::vector<std::shared_ptr<const MaterialAsset>> sources;for(const auto& source:blend_sources)if(source.material)sources.push_back(source.material);
            image_revision=material.previewRevision();cached_blend_preview=blend_preview;
            try {
                auto preview=std::make_shared<MaterialAsset>(material.definition());
                auto derived=std::make_shared<const MaterialGraph>(blend_material_channel_preview_graph(material.definition(),sources,blend_preview.layer,*blend_preview.channel,material_graph_image_policy()));
                preview->graph=*derived;preview->blendDefinition.reset();preview->surface.graph=derived;
                image_graph=std::move(derived);blend_preview_material=std::move(preview);
                image_target=blend_material_channel_preview_target(*material.definition().blendDefinition,blend_preview.layer,*blend_preview.channel);
                classified_revision=UINT64_MAX;
                std::erase_if(node_images,[this](const auto& entry){return !findMaterialNode(*image_graph,std::get<0>(entry.first));});
                message.clear();
            }catch(const std::exception& error){message=error.what();}
            return;
        }
        if(image_revision==material.previewRevision())return;
        image_graph=std::make_shared<const MaterialGraph>(material.definition().graph);
        image_revision=material.previewRevision();
        blend_preview_material.reset();image_target.reset();classified_revision=UINT64_MAX;
        std::erase_if(node_images,[this](const auto& entry){return !findMaterialNode(*image_graph,std::get<0>(entry.first));});
    }
    bool supportsNodeImage(MaterialGraphPreviewTarget target) {
        updateImageGraph();
        if(!image_graph||(blend_preview.channel&&image_target!=target))return false;
        if(classified_revision!=image_revision||classified_target!=target) {
            classified_revision=image_revision;classified_target=target;static_image=false;
            try {MaterialImagePreviewOptions options;options.width=options.height=256;
                validateMaterialGraphImagePreview(*image_graph,target,options);static_image=true;
            }catch(const std::exception&){ }
        }
        return static_image;
    }
    QImage nodeImage(MaterialGraphPreviewTarget target,unsigned edge) {
        updateImageGraph();edge=std::clamp(edge,32u,256u);
        if(!image_graph||(blend_preview.channel&&image_target!=target))return {};
        try {
            (void)assets.materialNodeImagePreview(image_document,image_graph,target,edge);
            const auto snapshot=assets.materialNodeImageSnapshot(image_document,target,edge);
            if(snapshot&&!snapshot->memory.empty()) {
                const auto pixels=std::shared_ptr<const DecodedImage>{snapshot.shared(),&snapshot->memory.front()};
                auto& cached=node_images[{target.node,target.pin,edge}];
                if(cached.pixels!=pixels) {
                    const auto format=pixels->format==ImagePixelFormat::RGBA16F?QImage::Format_RGBA16FPx4:QImage::Format_RGBA8888;
                    const QImage image(pixels->rgba.data(),int(pixels->width),int(pixels->height),int(pixels->width*imagePixelBytes(pixels->format)),format);
                    cached={pixels,image.convertToFormat(QImage::Format_RGBA8888)};
                }
                return cached.image;
            }
        }catch(const std::exception&){ }
        const auto* node=findMaterialNode(*image_graph,target.node);
        return node&&(node->kind==MaterialNodeKind::Texture2D||node->kind==MaterialNodeKind::ImageInput)?qtThumbnail(node->texture.image.id):QImage{};
    }
    bool finish(){if(graph)graph->finishInteraction();const bool result=visit([](auto& doc){return !doc.transactionActive()||doc.commit();});refresh(false);return result;}
    void cancel(){if(graph)graph->cancelInteraction();visit([](auto& doc){doc.cancel();});properties->resetProjection();refresh();request();}
    bool undo(){finish();const bool changed=visit([](auto& doc){return doc.undo();});properties->resetProjection();refresh();request();return changed;}
    bool redo(){finish();const bool changed=visit([](auto& doc){return doc.redo();});properties->resetProjection();refresh();request();return changed;}
    void refresh(bool project=true) {
        visit([&](auto& doc){if(!documentOpen(doc))return;const auto name=documentName(doc);setWindowTitle(QString::fromStdString(name+(doc.dirty()?" *":"")));
            if(referencePreview!=previewRevision(doc)||referencePolicy!=assets.references().revision()||referenceId!=identity(doc)){
                if(!reconcile(assets,doc)){message=doc.status();return;}retain(assets,doc);referencePreview=previewRevision(doc);referencePolicy=assets.references().revision();referenceId=identity(doc);}
            saveAction->setEnabled(editable(doc));undoAction->setEnabled(doc.canUndo());redoAction->setEnabled(doc.canRedo());undoAction->setText(QString::fromStdString(doc.undoLabel().empty()?"Undo":"Undo "+doc.undoLabel()));redoAction->setText(QString::fromStdString(doc.redoLabel().empty()?"Redo":"Redo "+doc.redoLabel()));
            statusBar()->showMessage(QString::fromStdString(message.empty()?doc.status():message));if(project)properties->refresh(previewRevision(doc),editable(doc));
            if(publishedRevision!=doc.revision()){publish(assets,doc);publishedRevision=doc.revision();}});
        if(graph){graph->refresh();graph->refreshImages();}
    }
    bool save(bool saveAs) {
        if(!finish())return false;bool overwrite=false;
        if(!saveAs&&visit([](const auto& doc){return doc.externalFileChanged();})) {
            QMessageBox conflict(QMessageBox::Warning,"File changed on disk","The file changed on disk.",QMessageBox::NoButton,this);
            auto* replace=conflict.addButton("Overwrite",QMessageBox::AcceptRole);auto* reload=conflict.addButton("Reload and discard edits",QMessageBox::DestructiveRole);
            auto* copy=layerDocument?nullptr:conflict.addButton("Save As…",QMessageBox::ActionRole);conflict.addButton("Keep editing",QMessageBox::RejectRole);conflict.exec();
            if(conflict.clickedButton()==reload){const auto current=id();const bool loaded=visit([&](auto& doc){return load(doc,current);});properties->resetProjection();refresh();request();return loaded;}
            if(conflict.clickedButton()==copy)return save(true);if(conflict.clickedButton()!=replace)return false;overwrite=true;
        }
        std::string name;if(saveAs){bool accepted=false;const auto chosen=QInputDialog::getText(this,"Save As","Name",QLineEdit::Normal,QString::fromStdString(visit([](const auto& doc){return documentName(doc);})),&accepted);if(!accepted||chosen.trimmed().isEmpty())return false;name=chosen.trimmed().toStdString();}
        const bool saved=visit([&](auto& doc){return saveDocument(doc,name,saveAs,overwrite);});if(saved){if(!layerDocument)assets.noteAssetWrite(id());message.clear();}
        refresh();request();return saved;
    }
    bool resolveClose() {
        if(!finish())return false;
        if(visit([](const auto& doc){return doc.dirty();})) {
            const auto name=QString::fromStdString(visit([](const auto& doc){return documentName(doc);}));
            const auto decision=unsaved.present({QStringLiteral("engineAssetUnsaved"),QStringLiteral("Unsaved changes"),"Save changes to "+name+"?","Save","Discard","Cancel"});
            if(decision==quader::QtUnsavedConfirmationDecision::Cancel)return false;
            if(decision==quader::QtUnsavedConfirmationDecision::Save&&!save(false))return false;
        }
        return true;
    }
    void closeResolved() {
        if(closing)return;
        if(beforeClose)beforeClose(this);
        if(isVisible()&&!testAttribute(Qt::WA_DontShowOnScreen)&&QApplication::platformName()!="minimal") {
            std::error_code error;std::filesystem::create_directories(layoutFile.parent_path(),error);if(!error){QSettings layout(QString::fromStdWString(layoutFile.wstring()),QSettings::IniFormat);layout.setValue(layoutKey()+"/geometry",saveGeometry());layout.sync();}
        }
        cancel();if(graph)graph->releaseImages();releaseImageCache();visit([&](auto& doc){if(doc.dirty())doc.discard();publish(assets,doc);release(assets,doc);if constexpr(!IsLayers<std::remove_cvref_t<decltype(doc)>>)doc.close();});hide();closing=true;
    }
protected:
    void closeEvent(QCloseEvent* event)override{event->ignore();if(resolveClose())closeResolved();}
};
}

struct AssetDocuments::Impl {
    QMainWindow& parent;LevelSession& session;std::function<void()> requestFrame;
    std::map<int,std::unique_ptr<DocumentWindow>> windows;
    std::function<void(MaterialDocument*,QMainWindow*)> materialChanged;
    AssetDocuments::DocumentOpened previewOpened;
    AssetDocuments::DocumentClosing previewClosing;
    MaterialDocument* publishedMaterial{};QPointer<QMainWindow> publishedWindow;
    Impl(QMainWindow& owner,LevelSession& level,std::function<void()> request):parent(owner),session(level),requestFrame(std::move(request)){}
    ~Impl(){if(previewClosing)for(const auto& [key,window]:windows){(void)key;previewClosing(window->kind,window->id());}if(materialChanged)materialChanged(nullptr,nullptr);publishedMaterial=nullptr;publishedWindow.clear();}
    DocumentWindow* active()const{for(const auto& [key,window]:windows){(void)key;if(window->isVisible()&&(window->isActiveWindow()||window->isAncestorOf(QApplication::focusWidget())))return window.get();}return nullptr;}
    DocumentWindow* activeMaterialWindow()const{auto* window=active();return window&&!window->closing&&window->kind==AssetKind::Material?window:nullptr;}
    void notifyMaterial(bool force=false){auto* window=activeMaterialWindow();auto* document=window?std::get<std::unique_ptr<MaterialDocument>>(window->domain).get():nullptr;
        if(force||document!=publishedMaterial||window!=publishedWindow){publishedMaterial=document;publishedWindow=window;if(materialChanged)materialChanged(document,window);}}
    void beforeWindowClose(DocumentWindow* window){if(previewClosing)previewClosing(window->kind,window->id());if(publishedWindow==window){publishedMaterial=nullptr;publishedWindow.clear();if(materialChanged)materialChanged(nullptr,nullptr);}}
    template<class Document> bool open(int key,AssetKind kind,const std::string& id) {
        if(const auto found=windows.find(key);found!=windows.end()){
            if(!found->second->closing&&found->second->id()==id){found->second->show();found->second->raise();found->second->activateWindow();if(previewOpened)previewOpened(kind,id,*found->second);return true;}
            if(!found->second->resolveClose())return false;found->second->closeResolved();windows.erase(found);
        }
        auto document=std::make_unique<Document>(session.projectRoot());
        if(!load(*document,id)){parent.statusBar()->showMessage(QString::fromStdString(document->status()),10000);return false;}
        auto window=std::make_unique<DocumentWindow>(parent,session.assets(),kind,std::move(document),session.projectRoot(),requestFrame);
        window->beforeClose=[this](DocumentWindow* closing){beforeWindowClose(closing);};window->setObjectName(QStringLiteral("EngineDocument")+QString::number(key));auto* shown=window.get();windows.emplace(key,std::move(window));if(previewOpened)previewOpened(kind,id,*shown);shown->show();shown->raise();shown->activateWindow();notifyMaterial();if(requestFrame)requestFrame();return true;
    }
};
AssetDocuments::AssetDocuments(QMainWindow& parent,LevelSession& session,std::function<void()> requestFrame):impl_(std::make_unique<Impl>(parent,session,std::move(requestFrame))){}
AssetDocuments::~AssetDocuments()=default;
bool AssetDocuments::open(AssetKind kind,const std::string& id) {
    const auto key=static_cast<int>(kind);
    switch(kind){case AssetKind::Material:return impl_->open<MaterialDocument>(key,kind,id);case AssetKind::Particle:return impl_->open<ParticleEffectDocument>(key,kind,id);
    case AssetKind::Decal:return impl_->open<DecalDocument>(key,kind,id);case AssetKind::Ragdoll:return impl_->open<RagdollDocument>(key,kind,id);case AssetKind::Animation:return impl_->open<AnimationDocument>(key,kind,id);
    case AssetKind::NavigationAgentProfile:return impl_->open<NavigationAgentProfileDocument>(key,kind,id);case AssetKind::GameplayEffect:return impl_->open<GameplayEffectDocument>(key,kind,id);default:return false;}
}
bool AssetDocuments::openCollisionLayers(){return impl_->open<CollisionLayersDocument>(-1,AssetKind::Scene,{});}
void AssetDocuments::update(){for(auto& [kind,window]:impl_->windows){(void)kind;if(!window->closing)window->refresh();}impl_->notifyMaterial();}
void AssetDocuments::filesChanged(std::span<const std::filesystem::path> paths){
    for(auto& [kind,window]:impl_->windows){(void)kind;if(window->closing)continue;
        window->visit([&](auto& document){try{
            using D=std::remove_cvref_t<decltype(document)>;if(!documentOpen(document))return;
            const auto file=[&]{if constexpr(IsLayers<D>)return document.filePath();else return resolveAssetPath(impl_->session.projectRoot(),document.assetId());}();
            if(std::none_of(paths.begin(),paths.end(),[&](const auto& changed){return resourcePathKey(changed)==resourcePathKey(file);})||!document.externalFileChanged())return;
            if(document.dirty()||document.transactionActive()){window->message="The file changed on disk.";return;}
            if(!load(document,identity(document))){window->message=document.status();return;}
            window->message.clear();window->properties->resetProjection();
        }catch(const std::exception& error){window->message=error.what();}});
        window->refresh();
    }
    impl_->notifyMaterial();
}
bool AssetDocuments::confirmClose(){for(auto& [kind,window]:impl_->windows){(void)kind;if(!window->closing&&!window->resolveClose())return false;}for(auto& [kind,window]:impl_->windows){(void)kind;if(!window->closing)window->closeResolved();}return true;}
bool AssetDocuments::finishActive(){if(auto* active=impl_->active())return active->finish();return true;}
bool AssetDocuments::undoActive(){if(auto* active=impl_->active())return active->undo();return false;}
bool AssetDocuments::redoActive(){if(auto* active=impl_->active())return active->redo();return false;}
QMainWindow* AssetDocuments::window(AssetKind kind)const{const auto found=impl_->windows.find(static_cast<int>(kind));return found==impl_->windows.end()?nullptr:found->second.get();}
MaterialDocument* AssetDocuments::materialDocument(const std::string& id)const{const auto found=impl_->windows.find(static_cast<int>(AssetKind::Material));if(found==impl_->windows.end()||found->second->id()!=id)return nullptr;return std::get<std::unique_ptr<MaterialDocument>>(found->second->domain).get();}
std::optional<MaterialGraphPreviewTarget> AssetDocuments::materialPreviewTarget(const std::string& id)const{
    const auto found=impl_->windows.find(static_cast<int>(AssetKind::Material));if(found==impl_->windows.end()||found->second->closing||found->second->id()!=id)return {};
    const auto& window=*found->second;const auto& material=*std::get<std::unique_ptr<MaterialDocument>>(window.domain);
    if(material.definition().blendDefinition&&window.blend_preview.channel)return blend_material_channel_preview_target(*material.definition().blendDefinition,window.blend_preview.layer,*window.blend_preview.channel);
    return window.graph?window.graph->previewTarget():std::nullopt;
}
std::optional<BlendMaterialPreviewSelection> AssetDocuments::blendMaterialPreview(const std::string& id)const{
    const auto found=impl_->windows.find(static_cast<int>(AssetKind::Material));if(found==impl_->windows.end()||found->second->closing||found->second->id()!=id)return {};
    return std::get<std::unique_ptr<MaterialDocument>>(found->second->domain)->definition().blendDefinition?std::optional{found->second->blend_preview}:std::nullopt;
}
bool AssetDocuments::setBlendMaterialPreview(const std::string& id,BlendMaterialPreviewSelection selection){
    const auto found=impl_->windows.find(static_cast<int>(AssetKind::Material));if(found==impl_->windows.end()||found->second->closing||found->second->id()!=id||selection.layer>=4||
        (selection.channel&&static_cast<unsigned>(*selection.channel)>static_cast<unsigned>(BlendMaterialChannel::Emission)))return false;
    auto& window=*found->second;if(!std::get<std::unique_ptr<MaterialDocument>>(window.domain)->definition().blendDefinition||!window.finish())return false;
    if(window.blend_preview!=selection){window.blend_preview=selection;if(selection.channel)window.preview_mode=MaterialPreviewMode::TwoD;window.request();}return true;
}
std::shared_ptr<const MaterialAsset> AssetDocuments::materialPreviewMaterial(const std::string& id){
    const auto found=impl_->windows.find(static_cast<int>(AssetKind::Material));if(found==impl_->windows.end()||found->second->closing||found->second->id()!=id||!found->second->blend_preview.channel)return {};
    auto& window=*found->second;window.updateImageGraph();return window.image_target==materialPreviewTarget(id)?window.blend_preview_material:nullptr;
}
MaterialPreviewMode AssetDocuments::materialPreviewMode(const std::string& id)const {
    const auto found=impl_->windows.find(static_cast<int>(AssetKind::Material));
    return found==impl_->windows.end()||found->second->closing||found->second->id()!=id?MaterialPreviewMode::ThreeD:found->second->preview_mode;
}
bool AssetDocuments::setMaterialPreviewMode(const std::string& id,MaterialPreviewMode mode) {
    const auto found=impl_->windows.find(static_cast<int>(AssetKind::Material));
    if(found==impl_->windows.end()||found->second->closing||found->second->id()!=id)return false;
    if(mode!=MaterialPreviewMode::TwoD&&mode!=MaterialPreviewMode::ThreeD)return false;
    if(found->second->preview_mode!=mode){found->second->preview_mode=mode;found->second->request();}return true;
}
bool AssetDocuments::materialNodeImageSupported(const std::string& id,MaterialGraphPreviewTarget target) {
    const auto found=impl_->windows.find(static_cast<int>(AssetKind::Material));
    return found!=impl_->windows.end()&&!found->second->closing&&found->second->id()==id&&found->second->supportsNodeImage(target);
}
QImage AssetDocuments::materialNodeImage(const std::string& id,MaterialGraphPreviewTarget target,unsigned edge) {
    const auto found=impl_->windows.find(static_cast<int>(AssetKind::Material));
    return found==impl_->windows.end()||found->second->closing||found->second->id()!=id?QImage{}:found->second->nodeImage(target,edge);
}
DecalDocument* AssetDocuments::decalDocument(const std::string& id)const{const auto found=impl_->windows.find(static_cast<int>(AssetKind::Decal));if(found==impl_->windows.end()||found->second->id()!=id)return nullptr;return std::get<std::unique_ptr<DecalDocument>>(found->second->domain).get();}
RagdollDocument* AssetDocuments::ragdollDocument(const std::string& id)const{const auto found=impl_->windows.find(static_cast<int>(AssetKind::Ragdoll));if(found==impl_->windows.end()||found->second->id()!=id)return nullptr;return std::get<std::unique_ptr<RagdollDocument>>(found->second->domain).get();}
AnimationDocument* AssetDocuments::animationDocument(const std::string& id)const{const auto found=impl_->windows.find(static_cast<int>(AssetKind::Animation));if(found==impl_->windows.end()||found->second->id()!=id)return nullptr;return std::get<std::unique_ptr<AnimationDocument>>(found->second->domain).get();}
ParticleEffectDocument* AssetDocuments::particleDocument(const std::string& id)const{const auto found=impl_->windows.find(static_cast<int>(AssetKind::Particle));if(found==impl_->windows.end()||found->second->id()!=id)return nullptr;return std::get<std::unique_ptr<ParticleEffectDocument>>(found->second->domain).get();}
QtDocumentProperties* AssetDocuments::properties(AssetKind kind)const{const auto found=impl_->windows.find(static_cast<int>(kind));return found==impl_->windows.end()?nullptr:found->second->properties;}
void AssetDocuments::setDocumentPreviewCallbacks(DocumentOpened opened,DocumentClosing closing){if(impl_->previewClosing)for(const auto& [key,window]:impl_->windows){(void)key;impl_->previewClosing(window->kind,window->id());}impl_->previewOpened=std::move(opened);impl_->previewClosing=std::move(closing);if(impl_->previewOpened)for(const auto& [key,window]:impl_->windows){(void)key;if(!window->closing)impl_->previewOpened(window->kind,window->id(),*window);}}
MaterialDocument* AssetDocuments::activeMaterialDocument()const{auto* window=impl_->activeMaterialWindow();return window?std::get<std::unique_ptr<MaterialDocument>>(window->domain).get():nullptr;}
QMainWindow* AssetDocuments::activeMaterialWindow()const{return impl_->activeMaterialWindow();}
void AssetDocuments::setActiveMaterialChanged(std::function<void(MaterialDocument*,QMainWindow*)> callback){if(impl_->materialChanged)impl_->materialChanged(nullptr,nullptr);impl_->materialChanged=std::move(callback);impl_->notifyMaterial(true);}
bool AssetDocuments::setPreview(AssetKind kind,const std::string& id,QWidget* preview){const auto found=impl_->windows.find(static_cast<int>(kind));if(found==impl_->windows.end()||found->second->id()!=id||!preview)return false;auto& window=*found->second;if(window.preview&&window.preview!=preview)window.preview->deleteLater();window.preview=preview;window.splitter->addWidget(preview);preview->show();return true;}
}
