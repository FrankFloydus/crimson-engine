#include "assets/AssetLibrary.h"
#include "assets/AsyncImports.h"
#include "assets/AtomicFile.h"
#include "assets/ModelImportIdentity.h"
#include "model_fixtures.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
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
struct Project {
    fs::path root=fs::temp_directory_path()/("crimson-import-identity-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Project(){fs::create_directories(root/"assets");}
    ~Project(){std::error_code error;const auto temporary=fs::weakly_canonical(fs::temp_directory_path(),error),target=fs::weakly_canonical(root,error);
        if(!error&&target.parent_path()==temporary&&target.filename().string().starts_with("crimson-import-identity-"))fs::remove_all(target,error);}
};
std::size_t packages(const fs::path& root,bool allowStaging=false) {
    std::size_t result{};for(const auto& entry:fs::directory_iterator(root/"assets/models")) {
        if(entry.path().filename().string().starts_with(".staging-")){require(allowStaging,"Model import leaked its staging package.");continue;}
        if(fs::is_regular_file(entry.path()/"manifest.json"))++result;
    }return result;
}
void exactRepeatAndDependencies() {
    Project p;const auto fixture=model_test::writeModelFixtures(p.root/"source");
    const auto first=importModelPackage(fixture.glb,p.root);const auto original=readBytes(p.root/pathFromUtf8(first->id));
    auto again=stageModelImport(fixture.glb,p.root);const auto repeated=publishModelImport(again);
    require(again->reused&&first->id==repeated->id&&first->images==repeated->images&&first->materialIds==repeated->materialIds,"Repeated GLB imports must reuse model, image and material identities.");
    require(!fs::exists(again->staging)&&packages(p.root)==1&&readBytes(p.root/pathFromUtf8(first->id))==original,"Reusing a package must not rewrite it or leave staging files.");
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
    const auto managed=p.root/pathFromUtf8(original->id);auto metadata=readJson(managed);metadata.erase("importIdentity");model_test::writeJson(managed,metadata);
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
    require(fs::exists(p.root/pathFromUtf8(original->id)),"Importing a new version must not remove retained old package data.");
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
std::size_t countKind(const AssetLibrary& library,AssetKind kind){return std::count_if(library.catalog().begin(),library.catalog().end(),[&](const auto& entry){return entry.kind==kind;});}
void actualAsyncCatalog() {
    Project p;const auto fixture=model_test::writeModelFixtures(p.root/"source");ResourceManager resources;AssetLibrary library(p.root,&resources);AsyncImports imports(p.root,resources);
    const auto other=p.root/"OTHER.GLB";fs::copy_file(fixture.glb,other);
    imports.enqueue({fixture.glb,other,p.root/"unsupported.xyz"},{});pumpImports(imports,resources,library);
    require(countKind(library,AssetKind::Model)==1&&countKind(library,AssetKind::Texture)==3,"Mixed-batch duplicate GLBs must publish one model and one set of embedded images.");
    require(library.status().find("reused 1")!=std::string::npos&&library.status().find("Unsupported")!=std::string::npos,"Batch results must distinguish reused imports and independent failures.");
    const auto id=library.selectedId();const auto before=library.catalog().size();
    const auto kept=library.browserCatalog().createFolder({},"Kept"),incoming=library.browserCatalog().createFolder({},"Incoming");
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
    resources.stop();
}
}
int main() {
    try {
        const std::array<std::uint8_t,3> abc{'a','b','c'};
        require(modelImportDataDigest(abc)=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","Model import identity must use standard SHA-256.");
        exactRepeatAndDependencies();legacyAndPublicationRaces();tombstonesFailureAndCancellation();actualAsyncCatalog();
        std::cout<<"Model import identity, dependency, tombstone, concurrent publication and public catalog checks passed.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
