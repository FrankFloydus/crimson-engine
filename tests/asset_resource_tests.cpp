#include "assets/AssetReferenceManager.h"
#include "assets/MaterialAsset.h"
#include "assets/ModelAsset.h"
#include "assets/AtomicFile.h"
#include "assets/AsyncImports.h"
#include "assets/DeletionPreparation.h"
#include "assets/ParticleAsset.h"
#include "model_fixtures.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
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
void triplanarWithoutMeshUvs() {
    Project p;const auto fixture=model_test::writeModelFixtures(p.root/"sources");
    std::ifstream input(fixture.gltf);auto source=Json::parse(input);input.close();
    for(auto& primitive:source["meshes"][0]["primitives"]) {
        primitive["attributes"].erase("TEXCOORD_0");primitive["attributes"].erase("TEXCOORD_1");primitive["attributes"].erase("TANGENT");
    }
    for(auto& material:source["materials"]) {
        material["pbrMetallicRoughness"].erase("baseColorTexture");material["pbrMetallicRoughness"].erase("metallicRoughnessTexture");
        material.erase("normalTexture");material.erase("occlusionTexture");material.erase("emissiveTexture");
    }
    model_test::writeJson(fixture.gltf,source);const auto imported=importModelPackage(fixture.gltf,p.root);
    auto projected=defaultMaterialAsset().surface;projected.triplanar=true;
    projected.baseColorTexture.image=AssetRef<DecodedImage>{imported->images.front()};projected.baseColorTexture.texCoord=1;
    projected.normalTexture=projected.baseColorTexture;
    ModelResolvers resolver;resolver.material=[&](const auto&)->std::optional<ModelMaterial>{return projected;};
    const auto mapped=loadModelPackage(imported->id,p.root,resolver);
    require(std::all_of(mapped->primitives.begin(),mapped->primitives.end(),[](const auto& primitive){return primitive.uvMask==0 && primitive.tangentUv==0xff;}),
        "Triplanar replacement generated fictitious UVs or a tangent UV basis.");
    require(mapped->materials.front().triplanar && mapped->materials.front().baseColorTexture.texCoord==1,
        "Triplanar replacement discarded its mapping mode or ordinary UV choice.");
    projected.triplanar=false;rejects([&]{loadModelPackage(imported->id,p.root,resolver);},"Ordinary UV mapping silently accepted missing mesh UVs.");
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
    fallback.surface.triplanar=true;fallback.surface.uvScale={2,3,4};saveMaterialAsset(p.root,fallback);
    const auto projected=validateAssetDeletion(p.root,entries,invalid);
    require(projected->models.size()==1 && projected->models.front()->materials[0].triplanar && projected->models.front()->materials[0].uvScale==Vec3{2,3,4} &&
        projected->models.front()->primitives[0].tangentUv==0xff && read(p.root/model->id)==before && !fs::exists(p.root/"assets/references.json"),
        "Triplanar deletion fallback required unavailable UVs/tangents or changed authored source files.");
    const auto originalMaterial=p.root/model->materialIds[0];writeFileAtomically(originalMaterial,read(originalMaterial)+" ");
    require(!deletionValidationCurrent(*validated),"Validated dependency state ignored a later material change.");
    std::atomic_bool entered{},release{};auto stale=manager.request<DeletionValidation>(AssetRef<DeletionValidation>{"import:delete/stale"},[&](std::stop_token stop){entered=true;while(!release&&!stop.stop_requested())std::this_thread::yield();if(stop.stop_requested())throw std::runtime_error("canceled");return loader(stop);});
    wait([&]{return entered.load();});manager.forget("import:delete/stale");release=true;wait([&]{return manager.idle();});
    require(!stale.snapshot()&&read(p.root/model->id)==before,"Canceled validation accepted a stale result or changed source files.");
}
void particleReferencesAndDeletionValidation(){
    Project p;const auto first=createParticleAsset(p.root,"First"),second=createParticleAsset(p.root,"Second");
    AssetReferenceManager references(p.root);
    const std::array<AssetEntry,1> selected{{{first.id,first.name,AssetKind::Particle}}};
    references.setUsages({{first.id,"assets/scenes/placed.crimson-scene","Scene","Sparks"},
                          {first.id,"active","Untitled","Particle history",true,true}});
    require(references.users(first.id).size()==2,"Particle reference scan omitted saved or historical users.");
    const auto authored=references.authoredUsers(first.id);
    require(authored.size()==1 && authored.front().ownerId=="assets/scenes/placed.crimson-scene",
            "Particle undo history was counted as a surviving authored dependency.");
    const std::array<std::string,1> removedScene{{"assets/scenes/placed.crimson-scene"}};
    require(references.authoredUsers(first.id,removedScene).empty(),
            "A saved scene removed with its particle effect still triggered a dependency warning.");
    references.replace(selected,{{first.id,second.id}});
    require(references.resolveParticle(first.id)==second.id,"Typed particle replacement did not resolve.");
    const auto before=references.serialize();
    rejects([&]{references.replace(selected,{{first.id,"assets/textures/foreign.png"}});},"Particle replacement accepted a texture.");
    rejects([&]{references.replace(selected,{{first.id,GrayTextureId}});},"Particle replacement accepted a built-in texture.");
    require(references.serialize()==before,"Rejected typed replacement altered the policy.");
    references.save();AssetReferenceManager reopened(p.root);
    require(reopened.resolveParticle(first.id)==second.id,"Particle replacement was not persistent.");
    require(reopened.resolveParticle("assets/particles/unresolved.crimson-particle")=="assets/particles/unresolved.crimson-particle",
            "An unresolved authored effect ID was discarded.");
    const std::array<AssetEntry,1> survivors{{{second.id,second.name,AssetKind::Particle}}};
    auto progress=std::make_shared<DeletionProgress>();progress->total=1;
    const auto validation=validateAssetDeletion(p.root,{},reopened,{},progress,survivors);
    require(validation->models.empty() && progress->completed.load()==1 && deletionValidationCurrent(*validation),
            "Particle deletion validation did not complete or track its file snapshot.");
    require(std::find(validation->dependencies.begin(),validation->dependencies.end(),particleAssetPath(p.root,second.id))!=validation->dependencies.end(),
            "Particle validation did not pin its surviving definition dependency.");
    const auto original=read(p.root/second.id);writeFileAtomically(p.root/second.id,original+" ");
    require(!deletionValidationCurrent(*validation),"A particle asset change did not invalidate pending deletion.");
    writeFileAtomically(p.root/second.id,"{invalid");
    rejects([&]{validateAssetDeletion(p.root,{},reopened,{}, {},survivors);},"Malformed surviving particle definition passed deletion validation.");
    require(read(p.root/first.id)==serializeParticleAsset(first),"Particle validation modified a managed source.");
    reopened.replace(survivors,{});
    require(reopened.resolveParticle(second.id).empty() && reopened.resolveParticle(first.id).empty(),
            "Deleting a replacement target did not disable old particle aliases.");
    reopened.save();AssetReferenceManager cleared(p.root);
    require(cleared.resolveParticle(first.id).empty() && cleared.resolveParticle(second.id).empty(),
            "Cleared particle replacement chains did not survive reopening.");
    std::stop_source cancel;cancel.request_stop();
    rejects([&]{validateAssetDeletion(p.root,{},reopened,cancel.get_token(),{},survivors);},"Canceled particle deletion validation proceeded.");
}
void particleReplacementStateValidation(){
    Project p;
    const std::string first="assets/particles/first.crimson-particle",second="assets/particles/second.crimson-particle";
    const auto state=Json{{"version",1},{"deleted",Json::array({first})},{"replacements",{{first,"assets/textures/foreign.png"}}}}.dump(2);
    writeFileAtomically(p.root/"assets/references.json",state);AssetReferenceManager damaged(p.root);
    require(!damaged.status().empty(),"Persisted particle replacement accepted a texture target.");
    rejects([&]{damaged.replace(std::array<AssetEntry,1>{{{second,"Second",AssetKind::Particle}}},{});},
            "Destructive operations remained enabled after incompatible persisted particle replacement.");
    require(read(p.root/"assets/references.json")==state,"Invalid persisted particle policy was overwritten.");
    writeFileAtomically(p.root/"assets/references.json",Json{{"version",1},{"deleted",Json::array()},{"replacements",Json::object()}}.dump(2));
    AssetReferenceManager references(p.root);
    references.replace(std::array<AssetEntry,1>{{{first,"First",AssetKind::Particle}}},{{first,second}});
    const auto previous=references.serialize();const auto revision=references.revision();
    // Revalidating every source also protects old particle aliases when an
    // incompatible later policy update targets the end of their chain.
    rejects([&]{references.replace(std::array<AssetEntry,1>{{{second,"Wrong kind",AssetKind::Texture}}},{});},
            "A later fallback changed an existing particle alias into a built-in texture.");
    require(references.serialize()==previous && references.revision()==revision && references.resolveParticle(first)==second,
            "Rejected particle replacement-chain update mutated the previous policy.");
}
void spriteMaterialStorageAndMigration(){
    Project p;const auto original=createMaterialAsset(p.root,"Smoke"),other=createMaterialAsset(p.root,"Smoke");
    require(original.id!=other.id&&original.name=="Smoke"&&other.name=="Smoke 2"&&original.originModel.empty(),"User materials did not receive unique native identities and names.");
    require(original.shading==MaterialShading::StandardPbr&&original.blend==MaterialBlend::Opaque&&modelMaterialCompatible(original)&&!particleMaterialCompatible(original),"New material is not Lit/Opaque.");
    require(original.surface.baseColor==Vec4{.21404114f,.21404114f,.21404114f,1}&&original.surface.roughness==1&&original.surface.metallic==0&&original.surface.baseColorTexture.image.empty()&&original.surface.sharedSampler.has_value(),
            "New material did not use sRGB midgray, roughness 1, metallic 0 and a shared sampler.");
    require(defaultMaterialAsset().surface.baseColor==Vec4{.5f,.5f,.5f,1} && defaultParticleMaterialAsset().surface.baseColor==Vec4{1,1,1,1},"New material defaults altered protected fallbacks.");
    auto alpha=original;alpha.shading=MaterialShading::Unlit;alpha.blend=MaterialBlend::Alpha;alpha.surface.sharedSampler.reset();alpha.surface.baseColor={.2f,.4f,.6f,.35f};alpha.surface.baseColorTexture.image=AssetRef<DecodedImage>{"assets/textures/unresolved.png"};
    alpha.surface.baseColorTexture.wrapU=ModelWrap::Mirror;alpha.surface.baseColorTexture.minFilter=ModelFilter::NearestMipmapLinear;saveMaterialAsset(p.root,alpha);
    const auto reopened=loadMaterialAsset(p.root,alpha.id);require(reopened.blend==MaterialBlend::Alpha&&reopened.surface.baseColor==alpha.surface.baseColor&&reopened.surface.baseColorTexture.image==alpha.surface.baseColorTexture.image&&reopened.surface.baseColorTexture.wrapU==ModelWrap::Mirror&&reopened.surface.baseColorTexture.minFilter==ModelFilter::NearestMipmapLinear,"Native sprite factors, unresolved map or sampler did not round-trip.");
    const auto bytes=read(p.root/alpha.id);auto imported=alpha;imported.originModel="assets/models/origin/manifest.json";
    const auto copy=copyMaterialAsset(p.root,imported);require(copy.id!=alpha.id&&copy.originModel.empty()&&copy.surface.baseColor==alpha.surface.baseColor&&read(p.root/alpha.id)==bytes,"Making an editable material copy changed its read-only source.");
    auto legacy=Json::parse(serializeMaterialAsset(alpha));legacy["version"]=1;legacy.erase("shading");legacy.erase("blend");legacy["alphaMask"]=true;
    writeFileAtomically(p.root/alpha.id,legacy.dump(2));const auto migrated=loadMaterialAsset(p.root,alpha.id);
    require(migrated.shading==MaterialShading::StandardPbr&&migrated.blend==MaterialBlend::Mask&&migrated.surface.alphaMask&&modelMaterialCompatible(migrated),"Legacy alphaMask did not migrate to PBR Mask.");
    require(Json::parse(serializeMaterialAsset(migrated)).at("version")==3,"Explicit material saving did not use native version 3.");
    auto unsupported=alpha;unsupported.surface.baseColorTexture.texCoord=1;require(!particleMaterialCompatible(unsupported),"Sprite material accepted a UV1 color map.");
    unsupported=alpha;unsupported.surface.normalTexture.image=AssetRef<DecodedImage>{"assets/textures/normal.png"};require(!particleMaterialCompatible(unsupported),"Sprite material accepted a tangent-space slot.");
    unsupported=alpha;unsupported.surface.baseColorTexture.wrapV=static_cast<ModelWrap>(99);rejects([&]{validateMaterialAsset(unsupported);},"Native material accepted an invalid sampler.");
    rejects([&]{saveMaterialAsset(p.root,defaultParticleMaterialAsset());},"Protected particle default was writable.");
    unsupported=alpha;unsupported.surface.baseColor[0]=-1;const auto previous=read(p.root/alpha.id);rejects([&]{saveMaterialAsset(p.root,unsupported);},"Invalid material was written.");require(read(p.root/alpha.id)==previous,"Failed material save overwrote the previous source.");
}
void particleMaterialStorageAndDependencies(){
    Project p;auto effect=createParticleAsset(p.root,"Sprite");auto material=createMaterialAsset(p.root,"Sprite material");
    material.shading=MaterialShading::Unlit;material.blend=MaterialBlend::Additive;
    effect.emitter.material=AssetRef<MaterialAsset>{material.id};effect.emitter.opacity=.3f;saveParticleAsset(p.root,effect);
    require(loadParticleAsset(p.root,effect.id)==effect,"Particle v2 material and opacity did not round-trip.");
    auto legacy=Json::parse(serializeParticleAsset(effect));legacy["version"]=1;legacy["emitters"][0]["renderer"]="procedural-additive";legacy["emitters"][0].erase("material");legacy["emitters"][0].erase("opacity");
    const auto migrated=deserializeParticleAsset(legacy.dump(),effect.id);require(migrated.emitter.material.id==DefaultParticleMaterialId&&migrated.emitter.opacity==1,"Particle v1 migration changed the procedural appearance.");
    auto invalid=effect;invalid.emitter.opacity=1.1f;rejects([&]{validateParticleAsset(invalid);},"Particle accepted opacity above one.");
    invalid=effect;invalid.emitter.material=AssetRef<MaterialAsset>{"assets/materials/../../foreign.crimson-material"};rejects([&]{validateParticleAsset(invalid);},"Particle accepted an escaping material reference.");
    const auto fixtures=model_test::writeModelFixtures(p.root/"source");fs::create_directories(p.root/"assets/textures");fs::copy_file(fixtures.image,p.root/"assets/textures/sprite.png");
    material.surface.baseColorTexture.image=AssetRef<DecodedImage>{"assets/textures/sprite.png"};saveMaterialAsset(p.root,material);
    AssetReferenceManager policy(p.root);const std::array<AssetEntry,1> effects{{{effect.id,effect.name,AssetKind::Particle}}};
    const auto validation=validateAssetDeletion(p.root,{},policy,{}, {},effects);
    require(std::find(validation->dependencies.begin(),validation->dependencies.end(),materialAssetPath(p.root,material.id))!=validation->dependencies.end()&&std::find(validation->dependencies.begin(),validation->dependencies.end(),checkedManagedAssetPath(p.root,"assets/textures/sprite.png"))!=validation->dependencies.end(),"Deletion validation omitted sprite material or color image dependencies.");
    writeFileAtomically(p.root/material.id,read(p.root/material.id)+" ");require(!deletionValidationCurrent(*validation),"A changed particle material did not invalidate deletion preparation.");
    policy.replace(std::array<AssetEntry,1>{{{material.id,material.name,AssetKind::Material}}},{});
    require(policy.resolveMaterial(material.id)==DefaultMaterialId&&policy.resolveParticleMaterial(material.id)==DefaultParticleMaterialId&&policy.resolveParticleMaterial(DefaultMaterialId)==DefaultMaterialId,"Material defaults lost context or an explicit PBR assignment became a particle material.");
    (void)validateAssetDeletion(p.root,{},policy,{}, {},effects);
    policy.replace(std::array<AssetEntry,1>{{{"assets/textures/sprite.png","Sprite",AssetKind::Texture}}},{});
    require(policy.resolveParticleTexture("assets/textures/sprite.png").empty()&&policy.resolveTexture("assets/textures/sprite.png")==GrayTextureId,"Deleted sprite images did not use the procedural fallback without changing model defaults.");
    policy.save();AssetReferenceManager reopened(p.root);require(reopened.resolveParticleMaterial(material.id)==DefaultParticleMaterialId,"Role-correct material deletion did not survive reopening.");
    const auto previous=reopened.serialize();rejects([&]{reopened.replace(std::array<AssetEntry,1>{{{material.id,material.name,AssetKind::Material}}},{{material.id,effect.id}});},"Material replacement accepted a particle-effect target.");
    require(reopened.serialize()==previous,"Rejected material-kind replacement changed the reference policy.");
    const auto damaged=Json{{"version",1},{"deleted",Json::array({material.id})},{"replacements",{{material.id,GrayTextureId}}}}.dump(2);
    writeFileAtomically(p.root/"assets/references.json",damaged);AssetReferenceManager invalidPolicy(p.root);
    require(!invalidPolicy.status().empty()&&read(p.root/"assets/references.json")==damaged,"Incompatible persisted material chain was accepted or overwritten.");
}
void materialMappingAndSharedSamplerStorage() {
    Project p;auto material=createMaterialAsset(p.root,"Mapped");auto& surface=material.surface;
    require(!surface.triplanarWorld,"New materials must preserve local triplanar as the default.");
    surface.uvScale={2,-3,0};surface.uvOffset={.25f,-.75f,4};surface.uvRotation={10,-30,90};surface.triplanar=true;surface.triplanarWorld=true;surface.uvScaleLinked=false;
    surface.baseColorTexture.texCoord=1;surface.normalTexture.texCoord=0;
    surface.sharedSampler=ModelSampler{ModelWrap::Mirror,ModelWrap::Clamp,ModelFilter::NearestMipmapLinear,ModelFilter::Nearest};
    surface.baseColorTexture.wrapU=ModelWrap::Clamp;surface.normalTexture.wrapU=ModelWrap::Repeat;
    saveMaterialAsset(p.root,material);const auto serialized=read(p.root/material.id);const auto loaded=loadMaterialAsset(p.root,material.id);
    require(serializeMaterialAsset(loaded)==serializeMaterialAsset(material) && Json::parse(serialized).at("version")==3,
        "Native v3 mapping, linkage, shared sampler or per-slot UV selection did not round-trip.");
    for(const auto* slot:{&loaded.surface.baseColorTexture,&loaded.surface.metallicRoughnessTexture,&loaded.surface.normalTexture,&loaded.surface.occlusionTexture,&loaded.surface.emissiveTexture})
        require(materialSampler(loaded.surface,*slot)==*surface.sharedSampler,"A texture channel ignored the shared material sampler.");
    const auto copied=copyMaterialAsset(p.root,material,"Mapped copy");
    require(copied.surface.uvScale==surface.uvScale && copied.surface.uvOffset==surface.uvOffset && copied.surface.uvRotation==surface.uvRotation &&
        copied.surface.triplanar && copied.surface.triplanarWorld && !copied.surface.uvScaleLinked && copied.surface.sharedSampler==surface.sharedSampler && read(p.root/material.id)==serialized,
        "Copy lost mapping/sampler state or modified its source.");
    auto earlierV3=Json::parse(serialized);earlierV3.erase("triplanarWorld");
    writeFileAtomically(p.root/material.id,earlierV3.dump());const auto earlierBytes=read(p.root/material.id);
    const auto earlier=loadMaterialAsset(p.root,material.id);auto expectedEarlier=material;expectedEarlier.surface.triplanarWorld=false;
    require(serializeMaterialAsset(earlier)==serializeMaterialAsset(expectedEarlier) && read(p.root/material.id)==earlierBytes,
        "Opening an earlier v3 material changed local triplanar mapping or rewrote its file.");
    auto invalidSpace=Json::parse(serialized);invalidSpace["triplanarWorld"]="world";
    writeFileAtomically(p.root/material.id,invalidSpace.dump());
    rejects([&]{loadMaterialAsset(p.root,material.id);},"A malformed World Triplanar field was accepted.");
    for(const int version:{1,2}) {
        auto legacy=Json::parse(serialized);legacy["version"]=version;
        for(const auto* field:{"uvScale","uvOffset","uvRotation","triplanar","triplanarWorld","uvScaleLinked","sharedSampler"})legacy.erase(field);
        if(version==1){legacy.erase("shading");legacy.erase("blend");}
        writeFileAtomically(p.root/material.id,legacy.dump());const auto before=read(p.root/material.id);
        const auto old=loadMaterialAsset(p.root,material.id);
        require(old.surface.uvScale==Vec3{1,1,1} && old.surface.uvOffset==Vec3{} && old.surface.uvRotation==Vec3{} &&
            !old.surface.triplanar && !old.surface.triplanarWorld && old.surface.uvScaleLinked && !old.surface.sharedSampler &&
            old.surface.baseColorTexture.texCoord==1 && materialSampler(old.surface,old.surface.baseColorTexture).wrapU==ModelWrap::Clamp &&
            materialSampler(old.surface,old.surface.normalTexture).wrapU==ModelWrap::Repeat && read(p.root/material.id)==before,
            "Opening legacy material changed its mapping, UV selection, mixed sampler behavior or file.");
        auto savedCopy=copyMaterialAsset(p.root,old,"Legacy copy");
        require(!savedCopy.surface.sharedSampler && !loadMaterialAsset(p.root,savedCopy.id).surface.sharedSampler,
            "Save As silently normalized legacy per-texture samplers.");
    }
    auto invalid=material;invalid.surface.uvOffset[2]=std::numeric_limits<float>::quiet_NaN();
    rejects([&]{validateMaterialAsset(invalid);},"Nonfinite UV mapping was accepted.");
    invalid=material;invalid.surface.sharedSampler->magFilter=ModelFilter::LinearMipmapLinear;
    rejects([&]{validateMaterialAsset(invalid);},"Invalid shared magnification filter was accepted.");
}
}
int main(){try{policies();authoredWarningClassification();transactionAndRecovery();managedMaterialsAndImages();stagedImages();materialTangentBasis();triplanarWithoutMeshUvs();backgroundDeletionValidation();particleReferencesAndDeletionValidation();particleReplacementStateValidation();spriteMaterialStorageAndMigration();particleMaterialStorageAndDependencies();materialMappingAndSharedSamplerStorage();std::cout<<"Asset reference, material and managed transaction checks passed.\n";return 0;}
    catch(const std::exception& e){std::cerr<<"Asset resource test failed: "<<e.what()<<'\n';return 1;}}
