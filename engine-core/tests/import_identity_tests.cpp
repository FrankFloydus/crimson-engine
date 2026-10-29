#include "assets/AssetLibrary.h"
#include <bgfx/platform.h>
#include "assets/AsyncImports.h"
#include "assets/AtomicFile.h"
#include "assets/ModelImportIdentity.h"
#include "editor/CollisionResources.h"
#include "resources/StreamingModel.h"
#include "scene/BuiltinModels.h"
#include "model_fixtures.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <thread>

namespace {
using namespace crimson;
namespace fs=std::filesystem;
using Json=nlohmann::json;
using Bytes=model_test::Bytes;
void require(bool value,const std::string& text){if(!value)throw std::runtime_error(text);}
template<class F>void rejects(F action){bool failed=false;try{action();}catch(const std::exception&){failed=true;}require(failed,"Invalid model import unexpectedly succeeded.");}
Bytes readBytes(const fs::path& file){std::ifstream input(file,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};}
Json readJson(const fs::path& file){std::ifstream input(file);return Json::parse(input);}
std::string pathText(const fs::path& path){const auto text=path.generic_u8string();return {reinterpret_cast<const char*>(text.data()),text.size()};}
struct Project {
    fs::path root=fs::temp_directory_path()/("crimson-import-identity-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Project(){fs::create_directories(root/"assets");}
    ~Project(){std::error_code error;const auto temporary=fs::weakly_canonical(fs::temp_directory_path(),error),target=fs::weakly_canonical(root,error);
        if(!error&&target.parent_path()==temporary&&target.filename().string().starts_with("crimson-import-identity-"))fs::remove_all(target,error);}
};
std::size_t packages(const fs::path& root,bool allowStaging=false) {
    if(contentStorageReady(root)) {
        std::size_t result{};for(const auto& entry:storedAssetPaths(root))if(entry.file.filename()=="manifest.json"&&fs::is_regular_file(entry.file))++result;
        if(!allowStaging&&fs::is_directory(resolveAssetPath(root,"assets/.staging-models")))
            require(fs::is_empty(resolveAssetPath(root,"assets/.staging-models")),"Model import leaked its private staging package.");
        return result;
    }
    std::size_t result{};for(const auto& entry:fs::directory_iterator(root/"assets/models")) {
        if(entry.path().filename().string().starts_with(".staging-")){require(allowStaging,"Model import leaked its staging package.");continue;}
        if(fs::is_regular_file(entry.path()/"manifest.json"))++result;
    }return result;
}
void exactRepeatAndDependencies() {
    Project p;const auto fixture=model_test::writeModelFixtures(p.root/"source");
    const auto first=importModelPackage(fixture.glb,p.root);const auto original=readBytes(resolveAssetPath(p.root,first->id));
    auto again=stageModelImport(fixture.glb,p.root);const auto repeated=publishModelImport(again);
    require(again->reused&&first->id==repeated->id&&first->images==repeated->images&&first->materialIds==repeated->materialIds,"Repeated GLB imports must reuse model, image and material identities.");
    require(!fs::exists(again->staging)&&packages(p.root)==1&&readBytes(resolveAssetPath(p.root,first->id))==original,"Reusing a package must not rewrite it or leave staging files.");
    const auto copy=p.root/"different-name.GLB";fs::copy_file(fixture.glb,copy);
    require(importModelPackage(copy,p.root)->id==first->id,"Identical model bytes from another filename should reuse their content identity.");
    const auto external=importModelPackage(fixture.gltf,p.root);
    auto bytes=readBytes(fixture.gltf.parent_path()/"geometry.bin");const float changed=-.55f;std::memcpy(bytes.data(),&changed,sizeof(changed));
    model_test::writeBytes(fixture.gltf.parent_path()/"geometry.bin",bytes);
    const auto geometryChanged=importModelPackage(fixture.gltf,p.root);require(geometryChanged->id!=external->id,"An external buffer change must produce a distinct complete-input version.");
    Bytes pixels(8*8*4,255);for(std::size_t i=0;i<pixels.size();i+=4)pixels[i]=15;
    model_test::writeBytes(fixture.image,model_test::png(8,8,pixels));
    const auto imageChanged=importModelPackage(fixture.gltf,p.root);require(imageChanged->id!=geometryChanged->id,"An external image change with identical glTF JSON must not reuse an old model version.");
    bytes.push_back(17);model_test::writeBytes(fixture.gltf.parent_path()/"geometry.bin",bytes);
    const auto trailing=importModelPackage(fixture.gltf,p.root);bytes.back()=19;model_test::writeBytes(fixture.gltf.parent_path()/"geometry.bin",bytes);
    require(importModelPackage(fixture.gltf,p.root)->id!=trailing->id,"Legal trailing external buffer bytes remain part of the exact source version.");
    const auto source=p.root/"source",archived=p.root/"removed-source";fs::rename(source,archived);
    require(loadModelPackage(pathFromUtf8(imageChanged->id),p.root)->images==imageChanged->images,"Reopening must resolve managed files after source removal.");
    require(packages(p.root)==6,"Exact repeats must not add managed packages.");
}
void legacyAndPublicationRaces() {
    Project p;const auto fixture=model_test::writeModelFixtures(p.root/"source");auto original=importModelPackage(fixture.glb,p.root);
    const auto managed=resolveAssetPath(p.root,original->id);auto metadata=readJson(managed);metadata.erase("importIdentity");model_test::writeJson(managed,metadata);
    auto legacy=stageModelImport(fixture.glb,p.root);require(publishModelImport(legacy)->id==original->id&&legacy->reused,"Legacy packages must be recognized from managed bytes without rewriting them.");
    require(!readJson(managed).contains("importIdentity"),"Duplicate detection must not rewrite legacy package manifests.");
    auto stale=stageModelImport(fixture.glb,p.root);const auto buffer=managed.parent_path()/"buffers/0.bin";auto bytes=readBytes(buffer);bytes[0]^=1;
    model_test::writeBytes(buffer,bytes);fs::last_write_time(buffer,fs::file_time_type::clock::now()-std::chrono::hours{24});
    const auto replacement=publishModelImport(stale);require(!stale->reused&&replacement->id!=original->id,"A changed known candidate must not pass publication through a stale manifest claim.");
    auto left=stageModelImport(fixture.gltf,p.root),right=stageModelImport(fixture.gltf,p.root);
    const auto a=publishModelImport(left),b=publishModelImport(right);
    require(a->id==b->id&&!left->reused&&right->reused&&a->images==b->images,"Concurrent workers must converge on one main-thread publication.");
    auto first=stageModelImport(fixture.glb,p.root),second=stageModelImport(fixture.glb,p.root);
    const auto blockedImage=first->asset->images.front();
    const auto reserved=[&](const std::string& id){return id==blockedImage;};
    const auto freshA=publishModelImport(first,reserved),freshB=publishModelImport(second,reserved);
    require(freshA->id==freshB->id&&freshA->id!=replacement->id&&second->reused,"Concurrent tombstone fallback collisions must reuse the same fresh package.");
    require(packages(p.root)==4,"Publication retries must not create redundant package versions.");
}
void tombstonesFailureAndCancellation() {
    Project p;const auto fixture=model_test::writeModelFixtures(p.root/"source");const auto original=importModelPackage(fixture.glb,p.root);
    AssetReferenceManager refs(p.root);const std::array<AssetEntry,1> deleted{{{original->id,"Deleted model",AssetKind::Model}}};refs.replace(deleted,{});refs.save();
    const auto next=importModelPackage(fixture.glb,p.root);require(next->id!=original->id&&!refs.deleted(next->id),"A tombstoned model identity must never be resurrected.");
    require(fs::exists(resolveAssetPath(p.root,original->id)),"Importing a new version must not remove retained old package data.");
    fs::remove(fixture.gltf.parent_path()/"geometry.bin");rejects([&]{importModelPackage(fixture.gltf,p.root);});
    require(packages(p.root)==2,"A missing external dependency must roll back its complete staging package.");
    {
        ResourceManager resources;AssetLibrary library(p.root,&resources);AsyncImports imports(p.root,resources);
        imports.enqueue({fixture.glb},{});resources.stop();
    }
    require(packages(p.root)==2,"Canceling an asynchronous duplicate import must not publish or leak packages.");
}
void pumpImports(AsyncImports& imports,ResourceManager& resources,AssetLibrary& library) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{30};
    while(imports.pending()&&std::chrono::steady_clock::now()<deadline){resources.pump();imports.pump(library);std::this_thread::sleep_for(std::chrono::milliseconds{1});}
    require(!imports.pending(),"Async model import batch did not complete.");
}
std::size_t countKind(const AssetLibrary& library,AssetKind kind){return std::count_if(library.catalog().begin(),library.catalog().end(),[&](const auto& entry){return entry.kind==kind&&!isEngineAssetId(entry.id);});}
std::vector<std::string> modelImportCards(const ModelAsset& model) {
    std::vector<std::string> ids{model.id};ids.insert(ids.end(),model.images.begin(),model.images.end());
    ids.insert(ids.end(),model.materialIds.begin(),model.materialIds.end());return ids;
}
void requireFolder(AssetLibrary& library,std::span<const std::string> ids,const std::string& folder) {
    auto& catalog=library.browserCatalog();
    const std::string destination=folder.empty()?BrowserCatalog::RootFolder:folder;
    for(const auto& id:ids) {
        require(isAssetUuid(id)&&catalog.contains(id)&&catalog.parent(id)==destination,"Imported card has the wrong folder or non-UUID identity: "+id);
        const auto physical=resolveAssetPath(catalog.folderPath(BrowserCatalog::RootFolder).parent_path(),id);
        const auto parent=physical.filename()=="manifest.json"?physical.parent_path().parent_path():physical.parent_path();
        require(parent==catalog.folderPath(destination),"Imported file is outside its displayed physical folder: "+id);
    }
}
void requireSourceSlots(AssetLibrary& library,const ModelAsset& model,const Json& source) {
    require(model.images.size()==source.at("images").size()&&model.materialIds.size()==model.materials.size()&&model.materials.size()>=source.at("materials").size(),
        "The imported model lost source images or authored material slots.");
    for(std::size_t i=0;i<source.at("materials").size();++i) {
        const auto material=library.findMaterial(model.materialIds[i]);
        require(material&&material->name==source.at("materials").at(i).at("name").get<std::string>()&&material->originModel==model.id,
            "An authored source material was not promoted with its model identity.");
    }
}
void synchronousImportDestinations() {
    Project p;const auto fixture=model_test::writeModelFixtures(p.root/"source");ResourceManager resources;AssetLibrary library(p.root,&resources);
    require(fs::is_regular_file(fixture.image),"The standalone image source fixture is missing before import.");
    for(const auto* id:{DefaultMaterialId,DefaultParticleMaterialId}) {
        const auto file=resolveAssetPath(p.root,id);require(fs::is_regular_file(file),"Protected Engine material was not materialized.");
        const auto contents=readBytes(file);ensureEngineMaterialAssets(p.root);
        require(readBytes(file)==contents&&loadMaterialAsset(p.root,id).id==id,"Engine material packaging changed an existing protected file.");
    }
    const auto destination=library.browserCatalog().createFolder(BrowserCatalog::RootFolder,"Destination");
    const auto kept=library.browserCatalog().createFolder(BrowserCatalog::RootFolder,"Existing"),other=library.browserCatalog().createFolder(BrowserCatalog::RootFolder,"Other");
    const auto imported=library.importFiles({fixture.glb,fixture.image,p.root/"unsupported.xyz"},destination);
    require(imported==2,"A failed file prevented successful synchronous batch imports: "+library.status());
    require(library.status().find("unsupported.xyz")!=std::string::npos,"Mixed synchronous import lost its per-file diagnostic.");
    const auto imageId=library.selectedId();const auto model=library.importModel(fixture.glb,other);require(static_cast<bool>(model),library.status());
    const auto cards=modelImportCards(*model);requireFolder(library,cards,destination);requireFolder(library,std::array{imageId},destination);
    requireSourceSlots(library,*model,readJson(fixture.gltf));
    const auto imageContents=readBytes(resolveAssetPath(p.root,imageId));
    auto wrongMaterial=defaultMaterialAsset();wrongMaterial.id=imageId;rejects([&]{saveMaterialAsset(p.root,wrongMaterial);});
    auto wrongParticle=defaultParticleEffect();wrongParticle.id=imageId;rejects([&]{saveParticleAsset(p.root,wrongParticle);});
    require(readBytes(resolveAssetPath(p.root,imageId))==imageContents,"A native asset save overwrote an image through its UUID.");
    require(model->images.size()==readJson(fixture.gltf).at("images").size()&&model->materialIds.size()==model->materials.size()&&
        model->materials.size()>=readJson(fixture.gltf).at("materials").size()&&countKind(library,AssetKind::Model)==1&&
        countKind(library,AssetKind::Texture)==model->images.size()+1&&countKind(library,AssetKind::Material)==model->materialIds.size(),
        "The model import did not expose its complete image and native-material dependency group.");
    const auto writes=library.takeOwnWrites();
    require(std::find(writes.begin(),writes.end(),resolveAssetPath(p.root,"assets/browser.json"))!=writes.end(),"Import routing did not acknowledge its browser organization write.");
    // Give every reused dependency its own authored presentation and preserve
    // native material edits across both public synchronous import entry points.
    auto authored=loadMaterialAsset(p.root,model->materialIds.front());authored.name="Authored material";saveMaterialAsset(p.root,authored);
    const auto originalPackage=readBytes(resolveAssetPath(p.root,model->id).parent_path()/"images/0.png");
    model_test::writeBytes(resolveAssetPath(p.root,model->images.front()),model_test::png(8,8,Bytes(8*8*4,120)));
    require(readBytes(resolveAssetPath(p.root,model->id).parent_path()/"images/0.png")==originalPackage,
        "Editing an extracted image changed immutable model import inputs.");
    std::map<std::string,Bytes> contents;
    std::map<std::string,std::pair<std::string,std::string>> presentation;
    for(std::size_t i=0;i<cards.size();++i) {
        const auto& id=cards[i];const auto& folder=i%2?kept:destination;
        const auto alias="Retained card "+std::to_string(i);library.browserCatalog().move(std::array{id},folder);library.browserCatalog().rename(id,alias);
        presentation.emplace(id,std::pair{folder,alias});contents.emplace(id,readBytes(resolveAssetPath(p.root,id)));
    }
    const auto catalogSize=library.catalog().size();
    require(library.importFiles({fixture.glb},other)==1,"Synchronous reimport failed.");
    for(const auto& [id,entry]:presentation)require(library.browserCatalog().parent(id)==entry.first&&library.displayName(id)==entry.second&&readBytes(resolveAssetPath(p.root,id))==contents.at(id),
        "Reusing a source changed an existing card's location, alias or authored file.");
    require(library.catalog().size()==catalogSize&&library.browserCatalog().items(other).empty(),"Reimport created or relocated existing dependencies.");
    library.browserCatalog().rename(imageId,"Retained image");
    require(library.importTexture(resolveAssetPath(p.root,imageId),other)!=nullptr,"Reusing a managed image failed.");
    requireFolder(library,std::array{imageId},destination);require(library.displayName(imageId)=="Retained image","Reusing a managed image changed its alias.");
    require(library.importFiles({fixture.image},"deleted-folder")==1&&library.status().find("imported into Content")!=std::string::npos,
        "A missing synchronous destination should retain a successful root import with a diagnostic.");
    requireFolder(library,std::array{library.selectedId()},{});
    require(library.importFiles({fixture.image},{})==1,"Explicit root imports must succeed.");requireFolder(library,std::array{library.selectedId()},{});
    resources.stop();
}
void asynchronousImportDestinations() {
    Project p;const auto fixture=model_test::writeModelFixtures(p.root/"source");ResourceManager resources;AssetLibrary library(p.root,&resources);AsyncImports imports(p.root,resources);
    const auto first=library.browserCatalog().createFolder(BrowserCatalog::RootFolder,"First"),second=library.browserCatalog().createFolder(BrowserCatalog::RootFolder,"Second");
    std::string selectedFolder=first;imports.enqueue({fixture.glb},selectedFolder);selectedFolder=second;
    imports.enqueue({fixture.gltf,fixture.image,p.root/"unsupported.xyz"},selectedFolder);selectedFolder.clear();
    pumpImports(imports,resources,library);
    const auto left=library.importModel(fixture.glb),right=library.importModel(fixture.gltf);
    require(left&&right&&left->id!=right->id,"Independent queued sources did not publish their model packages.");
    requireSourceSlots(library,*left,readJson(fixture.gltf));requireSourceSlots(library,*right,readJson(fixture.gltf));
    requireFolder(library,modelImportCards(*left),first);requireFolder(library,modelImportCards(*right),second);
    for(const auto& image:library.assets())if(!isEngineAssetId(image.id)&&
        std::find(left->images.begin(),left->images.end(),image.id)==left->images.end()&&std::find(right->images.begin(),right->images.end(),image.id)==right->images.end())
            requireFolder(library,std::array{image.id},second);
    require(countKind(library,AssetKind::Model)==2&&countKind(library,AssetKind::Texture)==left->images.size()+right->images.size()+1&&
        countKind(library,AssetKind::Material)==left->materialIds.size()+right->materialIds.size(),
        "Mixed asynchronous import did not retain both complete dependency groups and its standalone image.");
    std::map<std::string,std::pair<std::string,std::string>> presentation;
    for(const auto& entry:library.catalog()) {
        const auto alias=isEngineAssetId(entry.id)?library.displayName(entry.id):"Retained "+entry.id;
        if(!isEngineAssetId(entry.id))library.browserCatalog().rename(entry.id,alias);
        presentation.emplace(entry.id,std::pair{library.browserCatalog().parent(entry.id),alias});
    }
    imports.enqueue({fixture.glb,fixture.gltf},{});pumpImports(imports,resources,library);
    require(library.catalog().size()==presentation.size(),"Asynchronous reimport appended duplicate dependency cards.");
    for(const auto& [id,entry]:presentation)require(library.browserCatalog().parent(id)==entry.first&&library.displayName(id)==entry.second,
        "Asynchronous reimport moved or renamed a reused model, image or native material.");
    const auto deletedFixture=model_test::writeModelFixtures(p.root/"deleted-source");auto source=readJson(deletedFixture.gltf);
    source["asset"]["generator"]="Deleted destination fixture";model_test::writeJson(deletedFixture.gltf,source);
    const auto deleted=library.browserCatalog().createFolder(BrowserCatalog::RootFolder,"Deleted while pending");imports.enqueue({deletedFixture.gltf},deleted);
    library.browserCatalog().erase(std::array{deleted});pumpImports(imports,resources,library);
    require(library.status().find("Imported 1")!=std::string::npos&&library.status().find("imported into Content")!=std::string::npos,
        "Deleting a captured destination should retain a successful root import with a diagnostic.");
    const auto fallback=library.importModel(deletedFixture.gltf);require(static_cast<bool>(fallback),library.status());requireFolder(library,modelImportCards(*fallback),{});
    // Browser moves must survive reopening without changing managed identities.
    BrowserCatalog reopened(p.root);reopened.synchronize(library.catalog());
    for(const auto& [id,entry]:presentation)require(reopened.parent(id)==entry.first&&reopened.name(id)==entry.second,"Import presentation did not persist in browser.json.");
    resources.stop();
}
void queuedExternalImports() {
    Project p;const auto fixture=model_test::writeModelFixtures(p.root/"source");
    const auto unicode=p.root/"source"/pathFromUtf8("Muro \xc3\xa8.png");fs::copy_file(fixture.image,unicode);
    const auto sourceModel=readBytes(fixture.glb),sourceImage=readBytes(unicode);
    ResourceManager resources;AssetLibrary library(p.root,&resources);
    auto destination=library.browserCatalog().createFolder(BrowserCatalog::RootFolder,"Dropped files");
    const auto expectedFolder=destination,other=library.browserCatalog().createFolder(BrowserCatalog::RootFolder,"Later navigation");
    auto paths=std::vector<fs::path>{fixture.glb,unicode,p.root/"unsupported.xyz",fixture.gltf.parent_path()};
    const auto before=library.catalog().size();library.queueImportFiles(paths,destination);
    paths.clear();destination=other;
    require(library.catalog().size()==before,"External file delivery imported synchronously instead of queuing its immutable batch.");
    const auto untilComplete=[&](const char* summary) {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{30};
        do {resources.pump();library.processImports();
            if(library.status().find(summary)!=std::string::npos)return;
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }while(std::chrono::steady_clock::now()<deadline);
        throw std::runtime_error("The public external import queue did not publish: "+library.status());
    };
    untilComplete("of 4 asset(s).");
    require(library.status().find("Imported 2")!=std::string::npos && library.status().find("unsupported.xyz")!=std::string::npos &&
        library.status().find("source:")!=std::string::npos,"Mixed external imports lost a usable sibling, unsupported-file error or nonrecursive directory diagnostic.");
    const auto model=std::find_if(library.catalog().begin(),library.catalog().end(),[](const auto& item){return item.kind==AssetKind::Model;});
    require(model!=library.catalog().end(),"The public queue omitted its model.");
    const auto modelId=model->id;std::shared_ptr<const ModelAsset> prepared;
    const auto readyDeadline=std::chrono::steady_clock::now()+std::chrono::seconds{30};
    do {
        resources.pump();library.updateResources(0);prepared=library.findModel(modelId);
        if(prepared)break;
        require(library.modelDiagnostic(modelId).empty(),"Queued model preparation failed: "+library.modelDiagnostic(modelId));
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }while(std::chrono::steady_clock::now()<readyDeadline);
    require(bool(prepared),"Queued model metadata did not publish after its asynchronous seed: "+library.modelDiagnostic(modelId));
    requireFolder(library,modelImportCards(*prepared),expectedFolder);
    bool foundImage=false;
    for(const auto& item:library.catalog())if(item.name=="Muro \xc3\xa8.png") {requireFolder(library,std::array{item.id},expectedFolder);foundImage=true;}
    require(foundImage && readBytes(fixture.glb)==sourceModel && readBytes(unicode)==sourceImage,"External Unicode import lost its image or changed a Desktop/source file.");
    library.queueImportFiles(std::array{fixture.glb},other);untilComplete("of 1 asset(s).");
    require(countKind(library,AssetKind::Model)==1 && library.status().find("reused 1")!=std::string::npos,
        "Exact-content external reimport bypassed existing import identity reuse.");
    requireFolder(library,modelImportCards(*prepared),expectedFolder);resources.stop();
}
void actualAsyncCatalog() {
    Project p;const auto fixture=model_test::writeModelFixtures(p.root/"source");ResourceManager resources;AssetLibrary library(p.root,&resources);AsyncImports imports(p.root,resources);
    const auto other=p.root/"OTHER.GLB";fs::copy_file(fixture.glb,other);
    imports.enqueue({fixture.glb,other,p.root/"unsupported.xyz"},{});pumpImports(imports,resources,library);
    require(countKind(library,AssetKind::Model)==1&&countKind(library,AssetKind::Texture)==3,"Mixed-batch duplicate GLBs must publish one model and one set of embedded images.");
    require(library.status().find("reused 1")!=std::string::npos&&library.status().find("Unsupported")!=std::string::npos,"Batch results must distinguish reused imports and independent failures: "+library.status());
    const auto id=library.selectedId();const auto before=library.catalog().size();
    const auto kept=library.browserCatalog().createFolder(BrowserCatalog::RootFolder,"Kept"),incoming=library.browserCatalog().createFolder(BrowserCatalog::RootFolder,"Incoming");
    library.browserCatalog().move(std::array{id},kept);library.browserCatalog().rename(id,"Keep this alias");
    const auto materialCount=countKind(library,AssetKind::Material);const auto imageIds=library.assets();
    imports.enqueue({fixture.glb,other},incoming);pumpImports(imports,resources,library);
    require(library.catalog().size()==before&&countKind(library,AssetKind::Material)==materialCount&&library.assets().size()==imageIds.size(),"Repeat imports must not append duplicate catalog, texture or material records.");
    require(library.displayName(id)=="Keep this alias","Reusing a model import must preserve its browser alias.");
    const auto retained=library.browserCatalog().items(kept);require(std::any_of(retained.begin(),retained.end(),[&](const auto& item){return item.id==id;}),"Repeat import must preserve the asset's existing browser folder.");
    require(library.browserCatalog().items(incoming).empty(),"Reused assets must not move to the current import folder.");
    const auto synchronous=library.importModel(fixture.glb);require(static_cast<bool>(synchronous),library.status());
    require(synchronous->id==id&&library.catalog().size()==before,"The synchronous AssetLibrary import must use the same idempotent identity.");
    require(packages(p.root)==1,"Public asynchronous and synchronous import paths must converge on one package.");
    const auto retainedPackage=resolveAssetPath(p.root,id).parent_path(),buffer=retainedPackage/"buffers/0.bin";
    auto changed=readBytes(buffer);changed.front()^=1;model_test::writeBytes(buffer,changed);
    const auto replacement=library.importModel(fixture.glb,incoming);require(static_cast<bool>(replacement),library.status());
    require(replacement->id!=id&&resolveAssetPath(p.root,id).parent_path()==retainedPackage&&readBytes(buffer)==changed,
        "A moved, externally changed package lost its surviving UUID or was overwritten by reimport.");
    requireFolder(library,modelImportCards(*replacement),incoming);
    resources.stop();
}
void startupMaterialCatalog() {
    Project p;const auto fixture=model_test::writeModelFixtures(p.root/"source");
    const auto imported=importModelPackage(fixture.glb,p.root);
    auto shared=defaultMaterialAsset();shared.id="assets/materials/shared.crimson-material";shared.name="Shared native material";
    saveMaterialAsset(p.root,shared);
    const auto manifestFile=resolveAssetPath(p.root,imported->id);const auto originalManifest=readBytes(manifestFile);
    auto manifest=readJson(manifestFile);manifest["materialAssets"]=Json::array();
    for(std::size_t i=0;i<imported->materials.size();++i)manifest["materialAssets"].push_back(shared.id);
    writeFileAtomically(manifestFile,manifest.dump());
    {
        const auto sharedFile=resolveAssetPath(p.root,shared.id);
        const auto sharedBefore=readBytes(sharedFile),manifestBefore=readBytes(manifestFile);
        ResourceManager resources;AssetLibrary library(p.root,&resources);
        // Ordinary models legitimately scan their animation catalog. This
        // no-animation fixture must publish only its empty catalog packet;
        // shared material assignments still require no material/geometry load.
        require(resources.stats().records==1,"Shared material assignments queued work beyond the one animation-catalog scan.");
        std::vector<ResourcePublication> publications;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{10};
        do {
            resources.pump();auto current=resources.takePublications();
            publications.insert(publications.end(),current.begin(),current.end());
            if(resources.idle())break;
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }while(std::chrono::steady_clock::now()<deadline);
        const auto stats=resources.stats();
        require(resources.idle() && stats.records==1 && stats.loads==1 && stats.failures==0 &&
            stats.cpuBytes==sizeof(ModelAsset),"Shared material startup decoded geometry/materials or failed its empty animation scan.");
        require(publications.size()==1 && publications.front().kind==ResourceKind::Model &&
            publications.front().id==imported->id && publications.front().variant=="animation-catalog",
            "Shared material startup published an unexpected material-catalog or normal-model job.");
        require(readBytes(sharedFile)==sharedBefore && readBytes(manifestFile)==manifestBefore && library.takeOwnWrites().empty(),
            "The empty startup animation scan changed authored material/package files or reported sidecar writes.");
        require(library.findMaterial(shared.id)&&countKind(library,AssetKind::Model)==1&&countKind(library,AssetKind::Material)==1,
            "Skipping imported-sidecar migration lost the explicit native material or model catalog.");
        auto changed=shared;changed.name="Updated shared material";saveMaterialAsset(p.root,changed);library.onFileChanged(resolveAssetPath(p.root,shared.id));
        require(library.findMaterial(shared.id)->name==changed.name,"Explicit shared materials stopped responding to source changes.");
        require(library.takeOwnWrites().empty(),"An unchanged startup reported authored material writes.");resources.stop();
    }
    model_test::writeBytes(resolveAssetPath(p.root,imported->id),originalManifest);
    const auto sharedBytes=readBytes(resolveAssetPath(p.root,shared.id));
    const auto pump=[&](AssetLibrary& library,ResourceManager& resources) {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{10};
        while(std::chrono::steady_clock::now()<deadline) {
            resources.pump();library.updateResources(0);
            if(resources.stats().queued==0&&resources.stats().running==0) {
                resources.pump();library.updateResources(0);return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        throw std::runtime_error("Startup material migration did not complete.");
    };
    {
        ResourceManager resources;AssetLibrary library(p.root,&resources);pump(library,resources);
        for(const auto& id:imported->materialIds)require(library.findMaterial(id)&&fs::is_regular_file(resolveAssetPath(p.root,id)),
            "A missing imported sidecar was not migrated and published during startup.");
        const auto writes=library.takeOwnWrites();
        require(writes.size()==imported->materialIds.size(),"Material migration did not acknowledge exactly the new native sidecars.");
        require(readBytes(resolveAssetPath(p.root,shared.id))==sharedBytes,"Imported material migration rewrote an unrelated shared material.");resources.stop();
    }
    {
        ResourceManager resources;AssetLibrary library(p.root,&resources);
        const auto before=library.findMaterial(shared.id);
        auto external=*before;external.name="Deferred source notification";saveMaterialAsset(p.root,external);
        pump(library,resources);
        require(library.findMaterial(shared.id)==before,"An unchanged model catalog completion rescanned unrelated material sources.");
        require(library.takeOwnWrites().empty(),"Warm startup regenerated existing native sidecars.");
        library.onFileChanged(resolveAssetPath(p.root,shared.id));
        require(library.findMaterial(shared.id)->name==external.name,"A deferred source notification did not refresh its material.");resources.stop();
    }
}
void externalDirectoryNotifications() {
    Project p;ResourceManager resources;AssetLibrary library(p.root,&resources);
    const auto hasFolder=[&](const char* name) {
        const auto items=library.browserCatalog().items(BrowserCatalog::RootFolder);
        return std::any_of(items.begin(),items.end(),[&](const auto& item){return item.folder&&item.name==name;});
    };
    require(!hasFolder("External"),"External directory fixture was already present.");
    const auto original=p.root/"Content/External",renamed=p.root/"Content/Renamed";
    fs::create_directory(original);library.onFileChanged(original);
    require(hasFolder("External"),"An externally created empty Content directory was not discovered after its notification.");
    fs::rename(original,renamed);library.onFileChanged(original);library.onFileChanged(renamed);
    require(!hasFolder("External")&&hasFolder("Renamed"),"External empty directory rename did not refresh cached browser folders.");
    fs::remove(renamed);library.onFileChanged(renamed);
    require(!hasFolder("Renamed"),"An externally removed empty Content directory remained in the cached browser.");
    resources.stop();
}
Json profileMaterialCatalog(const fs::path& requestedRoot) {
    const auto root=fs::weakly_canonical(requestedRoot),temporary=fs::weakly_canonical(fs::temp_directory_path());
    require(root.parent_path()==temporary&&root.filename().string().starts_with("crimson-catalog-profile-"),
        "Catalog profiling requires a disposable project directly under the system temporary directory named crimson-catalog-profile-*. Copy the project there first.");
    require(fs::is_directory(root/"assets"),"Catalog profile project has no assets directory.");
    const auto inside=[&](const fs::path& file) {
        const auto relative=fs::weakly_canonical(file).lexically_relative(root);
        return !relative.empty()&&!relative.is_absolute()&&*relative.begin()!="..";
    };
    const auto snapshot=[&] {
        std::map<std::string,std::string> result;
        for(const auto& item:fs::recursive_directory_iterator(root)) {
            require(inside(item.path()),"Catalog profile project contains a redirected path outside its disposable root.");
            if(item.is_regular_file())result.emplace(pathText(item.path().lexically_relative(root)),modelImportDataDigest(readBytes(item.path())));
        }
        return result;
    };
    const auto before=snapshot();Json report;Json acknowledged=Json::array();
    using Clock=std::chrono::steady_clock;
    const auto milliseconds=[](auto duration){return std::chrono::duration<double,std::milli>(duration).count();};
    {
        ResourceManager resources;
        const auto started=Clock::now();AssetLibrary library(root,&resources);const auto constructed=Clock::now();
        const auto initial=resources.stats();const auto initialCatalogRevision=library.catalogRevision();
        double publicationMs{};const auto deadline=constructed+std::chrono::seconds{60};
        for(;;) {
            const auto tick=Clock::now();resources.pump();library.updateResources(0);publicationMs+=milliseconds(Clock::now()-tick);
            const auto stats=resources.stats();
            if(stats.queued==0&&stats.running==0) {
                // A worker may have enqueued its final publication immediately
                // before decrementing running. Drain it before recording results.
                const auto finalTick=Clock::now();resources.pump();library.updateResources(0);publicationMs+=milliseconds(Clock::now()-finalTick);
                const auto final=resources.stats();if(final.queued==0&&final.running==0)break;
            }
            require(Clock::now()<deadline,"Catalog profile did not finish its background metadata work.");
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        const auto finished=Clock::now();const auto final=resources.stats();
        for(const auto& file:library.takeOwnWrites()) {
            require(inside(file),"Catalog profiling wrote a sidecar outside its disposable project.");
            acknowledged.push_back(pathText(file.lexically_relative(root)));
        }
        report={{"projectRoot",pathText(root)},{"scope","CPU asset catalog construction and background metadata publication; no window, renderer, shaders, geometry streaming or total editor boot timing"},
            {"fileCacheCondition","Input preservation hashes read all files before timing; filesystem cache is warm"},
            {"constructionMs",milliseconds(constructed-started)},{"drainMs",milliseconds(finished-constructed)},
            {"totalMs",milliseconds(finished-started)},{"publicationMainThreadMs",publicationMs},
            {"initialRecords",initial.records},{"initialQueued",initial.queued},{"initialRunning",initial.running},
            {"records",final.records},{"loads",final.loads},{"cacheHits",final.cacheHits},{"failures",final.failures},
            {"staleResults",final.staleResults},{"cpuBytes",final.cpuBytes},
            {"catalogEntries",library.catalog().size()},{"models",countKind(library,AssetKind::Model)},
            {"materials",countKind(library,AssetKind::Material)},
            {"initialCatalogRevision",initialCatalogRevision},{"finalCatalogRevision",library.catalogRevision()},
            {"acknowledgedWrites",std::move(acknowledged)},{"status",library.status()}};
        resources.stop();
    }
    const auto after=snapshot();Json created=Json::array(),changed=Json::array(),removed=Json::array();
    for(const auto& [file,hash]:after) {
        const auto previous=before.find(file);
        if(previous==before.end())created.push_back(file);else if(previous->second!=hash)changed.push_back(file);
    }
    for(const auto& [file,hash]:before) { (void)hash;if(!after.contains(file))removed.push_back(file); }
    report["createdFiles"]=std::move(created);report["changedFiles"]=std::move(changed);report["removedFiles"]=std::move(removed);
    return report;
}
fs::path collisionGrid(const fs::path& folder) {
    // A continuous surface simplifies substantially for rendering. Its two
    // authored placements have an independently known area, extent and winding.
    struct Vertex {float position[3],normal[3],uv[2];};
    constexpr unsigned side=16;Bytes buffer;
    for(unsigned z=0;z<=side;++z)for(unsigned x=0;x<=side;++x) {
        const Vertex vertex{{x*.25f,0,z*.25f},{0,1,0},{float(x)/side,float(z)/side}};
        model_test::append(buffer,vertex);
    }
    const auto indexOffset=buffer.size();
    for(unsigned z=0;z<side;++z)for(unsigned x=0;x<side;++x) {
        const auto a=static_cast<std::uint16_t>(z*(side+1)+x),b=static_cast<std::uint16_t>(a+1),c=static_cast<std::uint16_t>(a+side+1),d=static_cast<std::uint16_t>(c+1);
        for(const auto index:std::array<std::uint16_t,6>{a,c,b,b,c,d})model_test::append(buffer,index);
    }
    Json data={
        {"asset",{{"version","2.0"}}},
        {"bufferViews",Json::array({{{"buffer",0},{"byteOffset",0},{"byteLength",indexOffset},{"byteStride",sizeof(Vertex)}},
            {{"buffer",0},{"byteOffset",indexOffset},{"byteLength",buffer.size()-indexOffset}}})},
        {"accessors",Json::array({
            {{"bufferView",0},{"byteOffset",0},{"componentType",5126},{"count",(side+1)*(side+1)},{"type","VEC3"},{"min",{0,0,0}},{"max",{4,0,4}}},
            {{"bufferView",0},{"byteOffset",12},{"componentType",5126},{"count",(side+1)*(side+1)},{"type","VEC3"}},
            {{"bufferView",0},{"byteOffset",24},{"componentType",5126},{"count",(side+1)*(side+1)},{"type","VEC2"}},
            {{"bufferView",1},{"byteOffset",0},{"componentType",5123},{"count",side*side*6},{"type","SCALAR"}}
        })},
        {"materials",Json::array({{{"name","Collision grid"},{"pbrMetallicRoughness",{{"metallicFactor",0},{"roughnessFactor",1}}}}})},
        {"meshes",Json::array({{{"primitives",Json::array({{{"attributes",{{"POSITION",0},{"NORMAL",1},{"TEXCOORD_0",2}}},{"indices",3},{"material",0}}})}}})},
        {"nodes",Json::array({{{"name","Grid root"},{"translation",{-1,2,3}},{"mesh",0},{"children",{1}}},
            {{"name","Reflected child"},{"translation",{8,0,0}},{"scale",{-1,2,1}},{"mesh",0}}})},
        {"scenes",Json::array({{{"nodes",{0}}}})},{"scene",0}
    };
    const auto file=folder/"collision-grid.glb";model_test::writeGlb(file,std::move(data),std::move(buffer));return file;
}
void requireOriginalCollision(const CollisionMeshResult& result) {
    require(result.current&&result.mesh&&result.diagnostic.empty(),"Collision publication is not current: "+result.diagnostic);
    const auto& mesh=*result.mesh;
    require(mesh.positions.size()==578&&mesh.indices.size()==3072,"Mesh collision used simplified render geometry or lost an authored occurrence.");
    require(mesh.bounds.valid&&mesh.bounds.minimum==Vec3{-1,2,3}&&mesh.bounds.maximum==Vec3{7,2,7},"Collision failed to accumulate authored parent/child affine placements exactly once.");
    double area=0;
    for(std::size_t i=0;i<mesh.indices.size();i+=3) {
        const auto& a=mesh.positions.at(mesh.indices[i]);const auto& b=mesh.positions.at(mesh.indices[i+1]);const auto& c=mesh.positions.at(mesh.indices[i+2]);
        const auto normal=math::cross(b-a,c-a);
        require(normal[1]>0,"A reflected source placement reversed collision triangle winding.");area+=math::length(normal)*.5;
    }
    require(std::abs(area-32)<1e-6,"Original collision triangles lost surface area during flattening.");
}
void collisionResourceIntegration() {
    Project p;const auto source=collisionGrid(p.root/"source");const auto imported=importModelPackage(source,p.root);
    fs::remove(source); // The imported package is self-contained.
    bgfx::renderFrame();bgfx::Init init;init.type=bgfx::RendererType::Noop;init.resolution.width=init.resolution.height=1;
    require(bgfx::init(init),"Could not initialize headless collision resource checks.");
    struct Shutdown {~Shutdown(){bgfx::shutdown();}} shutdown;
    fs::path originalFile;Bytes originalBytes;
    const auto run=[&](bool damageCache) {
        ResourceManager resources;AssetLibrary library(p.root,&resources);CollisionResources collisions(library,resources);
        const auto until=[&](auto action,auto ready,const std::string& failure) {
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{20};
            while(std::chrono::steady_clock::now()<deadline) {
                resources.pump();library.updateResources(0);
                if(ready(action()))return;
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }
            throw std::runtime_error(failure+" Model diagnostic: "+library.modelDiagnostic(imported->id)+"; asset status: "+library.status());
        };
        std::shared_ptr<const ModelAsset> metadata;
        until([&]{metadata=library.findModel(imported->id);return bool(metadata);},[](bool ready){return ready;},"Managed collision metadata did not load.");
        require(metadata->stream&&metadata->draws.size()==2&&metadata->primitives.size()==1&&metadata->primitives[0].vertices.empty()&&metadata->primitives[0].indices.empty(),
            "Collision fixture did not exercise the production metadata-only AssetLibrary path.");
        const auto dependencies=library.modelDependencies(imported->id);
        const auto manifest=resolveAssetPath(p.root,imported->id);
        require(std::find(dependencies.begin(),dependencies.end(),manifest)!=dependencies.end()&&std::find(dependencies.begin(),dependencies.end(),manifest.parent_path()/"model.gltf")!=dependencies.end()&&
            std::any_of(dependencies.begin(),dependencies.end(),[](const auto& file){return file.extension()==".bin";}),"Preview dependency snapshots omit the model manifest, normalized document, or source buffers.");
        const auto stableRevision=library.modelRevision(imported->id);
        registerAssetPath(p.root,newAssetId(),p.root/"Content"/(damageCache?"Unrelated-second.png":"Unrelated-first.png"));library.updateResources(0);
        require(library.modelRevision(imported->id)==stableRevision&&library.findModel(imported->id)==metadata,
            "An unrelated asset registration discarded the resident model or queued a repeated load.");
        const auto& levels=metadata->stream->primitives.front().levels;
        require(levels.size()>1&&levels.back().indexCount<levels.front().indexCount,"Collision fixture has no genuinely coarser render LOD.");
        const auto coarse=streaming::loadModelLod(levels.back(),0,static_cast<std::uint32_t>(levels.size()-1));
        require(coarse->geometry.indices.size()*2<3072,"Render simplification did not distinguish the original collision surface.");
        originalFile=levels.front().file;
        if(damageCache) {
            auto damaged=originalBytes;require(!damaged.empty(),"Missing original LOD bytes for corruption fixture.");damaged.back()^=0x40;
            model_test::writeBytes(originalFile,damaged);
        } else originalBytes=readBytes(originalFile);
        CollisionMeshResult accepted;
        CollisionMeshResult builtin;
        until([&]{builtin=collisions.query(BuiltinBoxModelId);return builtin.current;},[](bool ready){return ready;},"Builtin Box Trimesh collision did not prepare.");
        require(builtin.mesh&&builtin.mesh->indices.size()==36,"Builtin Box collision omitted its original triangle geometry.");
        const auto sphere=library.findModel(BuiltinSphereModelId);
        require(sphere && library.peekModel(BuiltinSphereModelId)==sphere && library.findModel(BuiltinSphereModelId)==sphere &&
            library.displayName(BuiltinSphereModelId)=="Sphere","AssetLibrary did not expose shared built-in Sphere geometry and its name.");
        require(library.modelRevision(BuiltinSphereModelId)>0 && library.modelFingerprint(BuiltinSphereModelId)!=0 &&
            library.modelFingerprint(BuiltinSphereModelId)!=library.modelFingerprint(BuiltinBoxModelId) &&
            library.modelDependencies(BuiltinSphereModelId).empty() && !library.modelLoading(BuiltinSphereModelId) && library.modelDiagnostic(BuiltinSphereModelId).empty(),
            "Sphere resource identity depends on imported files or collides with Box invalidation.");
        CollisionMeshResult sphereCollision;
        until([&]{sphereCollision=collisions.query(BuiltinSphereModelId);return sphereCollision.current;},[](bool ready){return ready;},
            "Builtin Sphere Trimesh collision did not prepare.");
        require(sphereCollision.mesh && sphereCollision.mesh->indices==sphere->primitives.front().indices &&
            sphereCollision.mesh->positions.size()==sphere->primitives.front().vertices.size(),
            "Builtin Sphere collision omitted its original geometry.");
        PrimitiveMeshSettings coarseSettings;coarseSettings.radius=2;coarseSettings.sides=5;coarseSettings.rings=3;
        auto fineSettings=coarseSettings;fineSettings.sides=16;fineSettings.rings=8;
        const auto coarseSphere=library.findModel(BuiltinSphereModelId,coarseSettings),fineSphere=library.findModel(BuiltinSphereModelId,fineSettings);
        require(coarseSphere && fineSphere && coarseSphere!=fineSphere && coarseSphere->id!=fineSphere->id &&
            coarseSphere->id!=BuiltinSphereModelId && library.findModel(BuiltinSphereModelId,coarseSettings)==coarseSphere &&
            library.peekModel(BuiltinSphereModelId,fineSettings)==fineSphere,
            "Concurrent Sphere variants collided or failed to reuse their immutable AssetLibrary entries.");
        require(coarseSphere->primitives.front().indices.size()<fineSphere->primitives.front().indices.size() &&
            coarseSphere->bounds.maximum==Vec3{2,2,2} && sphere->bounds.maximum==Vec3{.5f,.5f,.5f} &&
            library.findModel(BuiltinSphereModelId)==sphere && library.resolvedModelMaterials(*coarseSphere).size()==1,
            "Preparing a Sphere variant changed another variant/default or lost its material binding.");
        auto inactiveSettings=fineSettings;inactiveSettings.height=9;
        require(library.findModel(BuiltinSphereModelId,inactiveSettings)==fineSphere,"Inactive Sphere dimensions prevented variant cache reuse.");
        const auto cylinder=library.findModel(BuiltinCylinderModelId,coarseSettings);
        require(cylinder && cylinder->name=="Cylinder" && cylinder->id!=coarseSphere->id &&
            library.peekModel(BuiltinCylinderModelId,coarseSettings)==cylinder &&
            library.modelFingerprint(BuiltinCylinderModelId)!=library.modelFingerprint(BuiltinSphereModelId),
            "Cylinder variant reused a Sphere entry or omitted its source fingerprint.");
        PrimitiveMeshSettings transientSettings;transientSettings.radius=3.125f;transientSettings.sides=11;transientSettings.rings=13;
        std::weak_ptr<const ModelAsset> retiredVariant;std::string transientGeometryId;
        {
            const auto temporary=library.findModel(BuiltinSphereModelId,transientSettings);
            require(bool(temporary),"Transient primitive cache fixture did not prepare.");
            transientGeometryId=temporary->id;retiredVariant=temporary;
            require(library.primitiveModelAlive(transientGeometryId),"A strongly held primitive variant was reported dead.");
        }
        require(retiredVariant.expired()&&!library.primitiveModelAlive(transientGeometryId),"The primitive cache retained an unused variant or reported an expired geometry owner alive.");
        {
            const auto recreated=library.findModel(BuiltinSphereModelId,transientSettings);
            require(recreated && recreated->id==transientGeometryId && library.primitiveModelAlive(transientGeometryId) &&
                (retiredVariant.owner_before(recreated)||recreated.owner_before(retiredVariant)),
                "Recreating a primitive variant failed to restore its stable identity with a fresh geometry owner.");
        }
        until([&]{accepted=collisions.query(imported->id);return accepted.current;},[](bool ready){return ready;},"Original collision resource failed to load/recover: "+library.modelDiagnostic(imported->id));
        requireOriginalCollision(accepted);
        if(damageCache) {
            require(readBytes(originalFile)==originalBytes,"Collision did not rebuild its damaged disposable LOD cache from managed source data.");
        } else {
            const auto managed=resolveAssetPath(p.root,imported->id),document=managed.parent_path()/"model.gltf";
            const auto saved=readBytes(document);require(!saved.empty(),"Managed collision fixture document is missing.");
            fs::remove(document);library.onFileChanged(document);
            CollisionMeshResult missing;
            until([&]{missing=collisions.query(imported->id);return !missing.current&&!missing.diagnostic.empty()&&!library.modelLoading(imported->id);},[](bool ready){return ready;},"Deleted managed collision source remained current or supplied no diagnostic.");
            require(missing.mesh==accepted.mesh,"A failed reload discarded the last complete collision mesh required by live physics.");
            model_test::writeBytes(document,saved);library.onFileChanged(document);
            until([&]{accepted=collisions.query(imported->id);return accepted.current;},[](bool ready){return ready;},"Restoring managed collision data did not recover publication.");
            requireOriginalCollision(accepted);
        }
        resources.stop();
    };
    run(false);run(true);
}
}
int main(int argc,char** argv) {
    try {
        if(argc>=2&&std::string(argv[1])=="--profile-catalog") {
            require(argc==3||argc==4,"Use --profile-catalog DISPOSABLE_ROOT [REPORT_JSON].");
            const auto report=profileMaterialCatalog(pathFromUtf8(argv[2])).dump(2)+"\n";
            if(argc==4) {const auto output=fs::absolute(pathFromUtf8(argv[3]));fs::create_directories(output.parent_path());writeFileAtomically(output,report);}
            std::cout<<report;return 0;
        }
        const std::array<std::uint8_t,3> abc{'a','b','c'};
        require(modelImportDataDigest(abc)=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","Model import identity must use standard SHA-256.");
        exactRepeatAndDependencies();legacyAndPublicationRaces();tombstonesFailureAndCancellation();actualAsyncCatalog();synchronousImportDestinations();asynchronousImportDestinations();queuedExternalImports();startupMaterialCatalog();externalDirectoryNotifications();collisionResourceIntegration();
        std::cout<<"Model import identity, dependency, tombstone, concurrent publication and public catalog checks passed.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
