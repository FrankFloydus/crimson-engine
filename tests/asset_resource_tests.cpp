#include "assets/AssetReferenceManager.h"
#include "assets/MaterialAsset.h"
#include "assets/ModelAsset.h"
#include "assets/AtomicFile.h"
#include "assets/AsyncImports.h"
#include "assets/DeletionPreparation.h"
#include "model_fixtures.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace crimson;
namespace fs=std::filesystem;
using Json=nlohmann::json;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Action>void rejects(Action action,const char* message){try{action();}catch(const std::exception&){return;}throw std::runtime_error(message);}
struct Project{
    fs::path root=fs::temp_directory_path()/("crimson-reference-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Project(){fs::create_directories(root/"assets");}
    ~Project(){std::error_code ignored;fs::remove_all(root,ignored);}
};
std::string read(const fs::path& file){std::ifstream input(file,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};}
void policies(){
    Project p;AssetReferenceManager refs(p.root);
    const std::array<AssetEntry,4> entries{{{"assets/textures/a.png","A",AssetKind::Texture},{"assets/textures/sky.hdr","Sky",AssetKind::Environment},
        {"assets/materials/a.crimson-material","Material",AssetKind::Material},{"assets/models/a/manifest.json","Model",AssetKind::Model}}};
    refs.setUsages({{entries[0].id,entries[2].id,"Material","Base color"},{entries[2].id,entries[3].id,"Model","Material"},
        {entries[3].id,"assets/scenes/a.crimson-scene","Scene","Mesh"},{entries[3].id,"active","Untitled","Undo",true,true}});
    require(refs.users(entries[0].id).size()==4,"Reverse graph did not include transitive authored and history users.");
    refs.replace(entries,{});
    require(refs.resolveTexture(entries[0].id)==GrayTextureId,"Deleted ordinary texture did not resolve to solid gray.");
    require(refs.resolveTexture(entries[0].id,true)==NeutralNormalTextureId,"Deleted normal texture did not resolve to neutral normal.");
    require(refs.resolveEnvironment(entries[1].id).empty(),"Deleted HDR did not select the built-in studio.");
    require(refs.resolve(entries[2].id)==DefaultMaterialId,"Deleted material did not select the default material.");
    require(refs.resolve(entries[3].id).empty(),"Deleted model unexpectedly resolved to geometry.");
    refs.save();AssetReferenceManager reopened(p.root);require(reopened.deleted(entries[0].id)&&reopened.resolveTexture(entries[0].id,true)==NeutralNormalTextureId,"Replacement policy did not survive reopening.");
    const std::array<AssetEntry,1> other{{{"assets/textures/b.png","B",AssetKind::Texture}}};
    AssetReferenceManager chain(p.root);chain.replace(other,{{other[0].id,entries[0].id}});require(chain.resolveTexture(other[0].id)==GrayTextureId,"Chained replacement did not resolve.");
    rejects([&]{chain.replace(other,{{other[0].id,other[0].id}});},"Self-fallback accepted.");
    require(chain.resolveTexture(other[0].id)==GrayTextureId,"Failed replacement altered the previous policy.");
    rejects([&]{checkedManagedAssetPath(p.root,"assets/../../outside.txt");},"Path traversal accepted.");
    writeFileAtomically(p.root/"assets/references.json","{broken JSON");AssetReferenceManager corrupt(p.root);
    require(!corrupt.status().empty(),"Damaged reference state produced no diagnostic.");rejects([&]{corrupt.replace(other,{});},"Destructive editing remained enabled with damaged reference state.");
    require(read(p.root/"assets/references.json")=="{broken JSON","Damaged reference state was overwritten.");
}
void authoredWarningClassification(){
    Project p;AssetReferenceManager refs(p.root);
    const std::string model="assets/models/a/manifest.json",image="assets/textures/a.png",material="assets/materials/a.crimson-material",scene="assets/scenes/a.crimson-scene";
    refs.setUsages({{model,"","Untitled","Dropped model",true,false},
        {model,"","Untitled","Undo / redo history",true,true},
        {model,"assets/preview.json","Preview","model",false,false,true},
        {model,"assets/editor-session.json","Session","model",false,false,true}});
    require(refs.users(model).size()==4&&refs.authoredUsers(model).empty(),"Live placement, history or editor bookmarks became authored dependency warnings.");
    refs.setUsages({{model,scene,"Saved scene","First model"},{model,scene,"Saved scene","Second model"},
        {model,scene,"Saved scene","First model",true,false},{model,scene,"Saved scene","Undo / redo history",true,true},
        {image,material,"Material","Base color"},{image,material,"Material","Normal"},{material,model,"Model","Material 1"},
        {model,"assets/scenes/other.crimson-scene","Other scene","Model"}});
    const auto modelOwners=refs.authoredUsers(model);
    require(modelOwners.size()==2,"An active saved scene or repeated model instance inflated persisted owner counts.");
    require(std::all_of(modelOwners.begin(),modelOwners.end(),[](const auto& usage){return !usage.active&&!usage.historical;}),"Editor state appeared in authored warning owners.");
    require(refs.authoredUsers(image).size()==1&&refs.authoredUsers(image).front().ownerId==material,"Transitive scene/model users were counted as direct texture owners.");
    require(refs.authoredUsers(image).front().slot.find("Base color")!=std::string::npos&&refs.authoredUsers(image).front().slot.find("Normal")!=std::string::npos,"Deduplicating an owner lost its affected material slots.");
    const std::array<std::string,2> deleting{scene,material};
    require(refs.authoredUsers(model,deleting).size()==1&&refs.authoredUsers(image,deleting).empty(),"An owner being deleted still triggered a dependency warning.");
    AssetDeletionPlan plan;require(!plan.needsReferenceWarning(),"An unused asset required a dependency warning.");
    plan.usages=refs.users(model);require(!plan.needsReferenceWarning(),"Full impact/history rows independently triggered a dependency warning.");
    plan.warningUsages=modelOwners;require(plan.needsReferenceWarning(),"Persisted owners did not trigger a dependency warning.");
    const std::string replacement="assets/materials/replacement.crimson-material";
    refs.replace(std::array<AssetEntry,1>{{{material,"Deleted original",AssetKind::Material}}},{{material,replacement}});
    refs.setUsages({{image,material,"Deleted original","Texture"},{image,replacement,"Surviving material","Base color"}});
    const auto redirected=refs.authoredUsers(image);
    require(redirected.size()==1&&redirected.front().ownerId==replacement&&redirected.front().ownerName=="Surviving material","A deleted material appeared as a ghost owner beside its native replacement.");
    require(refs.authoredUsers(image,std::array<std::string,1>{replacement}).empty(),"A removed fallback material remained a surviving texture owner.");
    refs.replace(std::array<AssetEntry,1>{{{replacement,"Surviving material",AssetKind::Material}}},{});
    require(refs.authoredUsers(image).empty(),"A built-in default material inherited ghost dependencies from deleted sidecars.");
}
void transactionAndRecovery(){
    Project p;writeFileAtomically(p.root/"assets/a.txt","original");writeFileAtomically(p.root/"assets/b.txt","preserved");
    const std::array<AssetFileChange,3> changes{{{"assets/a.txt",std::string("updated")},{"assets/b.txt",std::nullopt},{"assets/c.txt",std::string("new")}}};
    commitAssetTransaction(p.root,changes);require(read(p.root/"assets/a.txt")=="updated"&&!fs::exists(p.root/"assets/b.txt")&&read(p.root/"assets/c.txt")=="new","Multi-file transaction did not publish.");
    const std::array<AssetFileChange,2> invalid{{{"assets/a.txt",std::string("broken")},{"assets/../outside",std::string("bad")}}};
    rejects([&]{commitAssetTransaction(p.root,invalid);},"Invalid transaction succeeded.");require(read(p.root/"assets/a.txt")=="updated","Failed validation partially published files.");
#ifdef _WIN32
    writeFileAtomically(p.root/"assets/protected.txt","readonly");fs::permissions(p.root/"assets/protected.txt",fs::perms::owner_read,fs::perm_options::replace);
    const std::array<AssetFileChange,2> denied{{{"assets/a.txt",std::string("must roll back")},{"assets/protected.txt",std::string("denied")}}};
    bool failed=false;try{commitAssetTransaction(p.root,denied);}catch(const std::exception&){failed=true;}
    fs::permissions(p.root/"assets/protected.txt",fs::perms::owner_all,fs::perm_options::replace);
    require(failed&&read(p.root/"assets/a.txt")=="updated"&&read(p.root/"assets/protected.txt")=="readonly","Mid-publication failure did not roll back earlier writes.");
#endif
    const auto staging=p.root/"assets/.resource-transactions/interrupted";fs::create_directories(staging/"before");
    writeFileAtomically(staging/"before/0.bin","updated");writeFileAtomically(p.root/"assets/a.txt","partial");writeFileAtomically(p.root/"assets/new.tmp","partial");
    const Json journal{{"version",1},{"committed",false},{"files",Json::array({{{"id","assets/a.txt"},{"existed",true},{"backup","0.bin"}},{{"id","assets/new.tmp"},{"existed",false},{"backup","1.bin"}}})}};
    writeFileAtomically(staging/"journal.json",journal.dump());recoverAssetTransactions(p.root);
    require(read(p.root/"assets/a.txt")=="updated"&&!fs::exists(p.root/"assets/new.tmp")&&!fs::exists(staging),"Interrupted transaction did not restore its original state.");
}
void managedMaterialsAndImages(){
    Project p;const auto fixtures=model_test::writeModelFixtures(p.root/"sources");auto stage=stageModelImport(fixtures.glb,p.root);
    require(!fs::exists(stage->destination)&&fs::exists(stage->staging),"Worker import published its package.");
    const auto model=publishModelImport(stage);require(fs::exists(stage->destination)&&stage->published,"Main-thread publication did not publish staged model.");
    const auto metadata=loadModelMaterialMetadata(model->id,p.root);
    require(metadata->materials.size()==model->materials.size()&&metadata->primitives.empty()&&metadata->draws.empty()&&metadata->images==model->images,"Material catalog lookup decoded geometry or lost managed references.");
    require(model->materialIds.size()==model->materials.size()&&model->primitives[0].uvMask==3,"Imported material identity or authored UV metadata missing.");
    promoteModelMaterials(p.root,*model);const auto materialId=model->materialIds[0];const auto promoted=loadMaterialAsset(p.root,materialId);
    require(promoted.id==materialId&&promoted.surface.baseColor==model->materials[0].baseColor&&promoted.surface.baseColorTexture.minFilter==ModelFilter::LinearMipmapLinear,"Material promotion changed factors or sampler state.");
    auto changed=promoted;changed.surface.roughness=.91f;changed.surface.alphaCutoff=2;saveMaterialAsset(p.root,changed);promoteModelMaterials(p.root,*model);
    const auto preserved=loadMaterialAsset(p.root,materialId);
    require(preserved.surface.roughness==.91f&&preserved.surface.alphaCutoff==2,"Idempotent promotion overwrote authored factors or rejected a valid glTF cutoff.");
    const auto standard=defaultMaterialAsset();require(standard.surface.baseColor==Vec4{.5f,.5f,.5f,1}&&standard.surface.roughness==1&&standard.surface.metallic==0,"Default material factors differ from requested values.");
    AssetReferenceManager refs(p.root);const std::array<AssetEntry,1> image{{{model->images[0],"Image",AssetKind::Texture}}};refs.replace(image,{});refs.save();
    const std::array<AssetFileChange,1> remove{{{image[0].id,std::nullopt}}};commitAssetTransaction(p.root,remove);
    const ModelResolvers resolved{[refs](const auto& id){return refs.resolveTexture(id);},[&](const auto& id)->std::optional<ModelMaterial>{return loadMaterialAsset(p.root,id).surface;}};
    const auto reopened=loadModelPackage(model->id,p.root,resolved);require(reopened->draws.size()==model->draws.size(),"Explicitly deleted package image broke managed reopening.");
    rejects([&]{loadModelPackage(model->id,p.root);},"Unintentionally missing image did not fail without the replacement policy.");
    fs::path abandonedPath;{const auto abandoned=stageModelImport(fixtures.gltf,p.root);abandonedPath=abandoned->staging;auto owned=abandoned;
        owned.reset();require(fs::exists(abandonedPath),"A shared staging package was destroyed while still held.");}
    require(!fs::exists(abandonedPath),"An unpublished staging package leaked after cancellation.");
    const auto before=read(p.root/materialId);std::vector<AssetFileChange> modelFiles;
    const auto package=(p.root/model->id).parent_path();for(const auto& file:fs::recursive_directory_iterator(package))if(file.is_regular_file()&&file.path().parent_path().filename()!="images")
        modelFiles.push_back({file.path().lexically_relative(p.root).generic_string(),std::nullopt});
    commitAssetTransaction(p.root,modelFiles);require(read(p.root/materialId)==before&&fs::exists(p.root/model->images[1]),"Deleting model files removed independently usable material/image data.");
}
void stagedImages(){
    Project p;model_test::writeModelFixtures(p.root/"sources");const auto source=p.root/"sources/CASE_CHECKER.PNG";
    fs::copy_file(p.root/"sources/checker.png",source);
    std::shared_ptr<ImageImportStage> prepared;{std::jthread worker([&]{prepared=stageImageImport(source,p.root);});worker.join();}
    require(prepared&&prepared->image->width>0&&prepared->image->rgba.size()>0&&!fs::exists(p.root/"assets/textures/CASE_CHECKER.PNG"),"Worker image decoding published managed data or required bgfx.");
    const auto first=publishImageImport(prepared);auto next=stageImageImport(source,p.root);const auto second=publishImageImport(next);
    require(first!=second&&read(first)==read(source)&&read(second)==read(source),"Staged image collision overwrote a managed asset.");
    AssetReferenceManager refs(p.root);const auto id=first.lexically_relative(p.root).generic_string();refs.replace(std::array<AssetEntry,1>{{{id,"Deleted",AssetKind::Texture}}},{});fs::remove(first);
    const auto replacement=publishImageImport(stageImageImport(source,p.root),[&](const auto& candidate){return refs.deleted(candidate);});
    require(replacement!=first&&replacement!=second,"Reimport reused a tombstoned persistent identity.");
    rejects([&]{publishImageImport(prepared);},"Image stage was published twice.");
    fs::path abandoned;{const auto stage=stageImageImport(source,p.root);abandoned=stage->directory;}require(!fs::exists(abandoned),"Canceled image staging directory leaked.");
    std::stop_source canceled;canceled.request_stop();rejects([&]{stageImageImport(source,p.root,canceled.get_token());},"Stopped image import continued.");
    writeFileAtomically(p.root/"sources/broken.png","not a PNG");rejects([&]{stageImageImport(p.root/"sources/broken.png",p.root);},"Malformed image entered a publishable stage.");
    require(fs::directory_iterator(p.root/"assets/textures")!=fs::directory_iterator{},"Valid mixed-batch image was removed after a failed sibling.");
}
void materialTangentBasis(){
    Project p;const auto fixture=model_test::writeModelFixtures(p.root/"sources");
    std::ifstream input(fixture.gltf);auto document=Json::parse(input);input.close();
    std::ifstream binary(p.root/"sources/geometry.bin",std::ios::binary);model_test::Bytes bytes{std::istreambuf_iterator<char>(binary),{}};binary.close();const auto offset=bytes.size();
    for(int i=0;i<24;++i)model_test::append(bytes,(i/4==2||i/4==3)?std::array<float,4>{0,0,1,1}:std::array<float,4>{1,0,0,1});
    const auto view=document["bufferViews"].size();document["bufferViews"].push_back({{"buffer",0},{"byteOffset",offset},{"byteLength",bytes.size()-offset}});
    const auto accessor=document["accessors"].size();document["accessors"].push_back({{"bufferView",view},{"componentType",5126},{"count",24},{"type","VEC4"}});
    for(auto& primitive:document["meshes"][0]["primitives"])primitive["attributes"]["TANGENT"]=accessor;
    document["buffers"][0]["byteLength"]=bytes.size();model_test::writeBytes(p.root/"sources/geometry.bin",bytes);model_test::writeJson(fixture.gltf,document);
    const auto imported=importModelPackage(fixture.gltf,p.root);require(imported->primitives[1].tangentUv==1,"Authored UV1 normal tangent basis was recorded as UV0.");
    auto replacement=imported->materials[1];replacement.normalTexture.texCoord=0;
    const auto id=imported->materialIds[1];ModelResolvers resolver;resolver.material=[&](const auto& material)->std::optional<ModelMaterial>{return material==id?std::optional<ModelMaterial>{replacement}:std::nullopt;};
    const auto reopened=loadModelPackage(imported->id,p.root,resolver);require(reopened->primitives[1].tangentUv==0,"Replacement normal UV did not regenerate tangent data.");
    require(std::any_of(reopened->warnings.begin(),reopened->warnings.end(),[](const auto& warning){return warning.find("authored tangents were regenerated")!=std::string::npos;}),"Tangent-basis replacement lacked a diagnostic.");
    replacement.normalTexture.texCoord=2;rejects([&]{loadModelPackage(imported->id,p.root,resolver);},"Replacement material beyond UV1 was accepted.");
}
void backgroundDeletionValidation(){
    Project p;const auto fixture=model_test::writeModelFixtures(p.root/"sources");std::ifstream source(fixture.gltf);auto json=Json::parse(source);source.close();
    for(auto& primitive:json["meshes"][0]["primitives"])primitive["attributes"].erase("TEXCOORD_1");
    json["materials"][1]["pbrMetallicRoughness"]["baseColorTexture"]["texCoord"]=0;json["materials"][1]["normalTexture"]["texCoord"]=0;model_test::writeJson(fixture.gltf,json);
    const auto model=importModelPackage(fixture.gltf,p.root);promoteModelMaterials(p.root,*model);AssetReferenceManager refs(p.root);
    const std::array<AssetEntry,1> entries{{{model->id,model->name,AssetKind::Model}}};const auto before=read(p.root/model->id);
    ResourceManager manager;std::atomic_uint calls{};
    const auto loader=[&](std::stop_token stop){++calls;auto data=validateAssetDeletion(p.root,entries,refs,stop);return ResourceLoadResult<DeletionValidation>{data,data->bytes,data->fileStateFingerprint,data->dependencies};};
    auto a=manager.request<DeletionValidation>(AssetRef<DeletionValidation>{"validation"},loader);auto b=manager.request<DeletionValidation>(AssetRef<DeletionValidation>{"validation"},loader);
    const auto wait=[&](const auto& predicate){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds{10};do{manager.pump();if(predicate())return;std::this_thread::sleep_for(std::chrono::milliseconds{1});}while(std::chrono::steady_clock::now()<end);throw std::runtime_error("Deletion worker did not finish.");};
    wait([&]{return a.state()==ResourceState::Ready;});require(calls==1&&b.snapshot(),"Duplicate deletion validation loaded the project twice.");
    const auto validated=a.snapshot();require(validated->models.size()==1&&validated->models.front()->stream&&deletionValidationCurrent(*validated),"Deletion validation did not return a current streamed model descriptor.");
    for(const auto& primitive:validated->models.front()->primitives)require(primitive.vertices.capacity()==0&&primitive.indices.capacity()==0,"Compact validation retained full geometry capacity.");
    MaterialAsset fallback{"assets/materials/invalid-uv.crimson-material","Unavailable UV",model->id,model->materials[0]};fallback.surface.normalTexture.texCoord=1;saveMaterialAsset(p.root,fallback);
    auto invalid=refs;invalid.replace(std::array<AssetEntry,1>{{{model->materialIds[0],"Original",AssetKind::Material}}},{{model->materialIds[0],fallback.id}});
    auto bad=manager.request<DeletionValidation>(AssetRef<DeletionValidation>{"invalid-validation"},[&](std::stop_token stop){auto data=validateAssetDeletion(p.root,entries,invalid,stop);return ResourceLoadResult<DeletionValidation>{data,data->bytes,data->fileStateFingerprint,data->dependencies};});
    wait([&]{return bad.state()==ResourceState::Failed;});require(bad.diagnostic().find("UV")!=std::string::npos&&read(p.root/model->id)==before&&!fs::exists(p.root/"assets/references.json"),"Invalid fallback published files or lacked a UV diagnostic.");
    const auto originalMaterial=p.root/model->materialIds[0];writeFileAtomically(originalMaterial,read(originalMaterial)+" ");
    require(!deletionValidationCurrent(*validated),"Validated dependency state ignored a later material change.");
    std::atomic_bool entered{},release{};auto stale=manager.request<DeletionValidation>(AssetRef<DeletionValidation>{"import:delete/stale"},[&](std::stop_token stop){entered=true;while(!release&&!stop.stop_requested())std::this_thread::yield();if(stop.stop_requested())throw std::runtime_error("canceled");return loader(stop);});
    wait([&]{return entered.load();});manager.forget("import:delete/stale");release=true;wait([&]{return manager.idle();});
    require(!stale.snapshot()&&read(p.root/model->id)==before,"Canceled validation accepted a stale result or changed source files.");
}
}
int main(){try{policies();authoredWarningClassification();transactionAndRecovery();managedMaterialsAndImages();stagedImages();materialTangentBasis();backgroundDeletionValidation();std::cout<<"Asset reference, material and managed transaction checks passed.\n";return 0;}
    catch(const std::exception& e){std::cerr<<"Asset resource test failed: "<<e.what()<<'\n';return 1;}}
