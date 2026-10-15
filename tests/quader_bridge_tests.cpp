#include "quader/QuaderBridge.h"
#include "assets/AtomicFile.h"
#include "resources/ImageDecoder.h"
#include "resources/Paths.h"
#include "resources/StreamingModel.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace {
using namespace crimson;
using Json=nlohmann::json;
namespace fs=std::filesystem;
void require(bool value,const std::string& text){if(!value)throw std::runtime_error(text);}
void near(float actual,float expected){require(std::isfinite(actual)&&std::abs(actual-expected)<.0001f,"Unexpected converted Quader value.");}
template<class Operation> void rejects(Operation operation){try{operation();}catch(const std::exception&){return;}throw std::runtime_error("Expected a rejected Quader operation.");}
struct Temporary {
    fs::path root=fs::temp_directory_path()/("crimson-quader-tests-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temporary(){fs::create_directories(root);}
    ~Temporary(){try{const auto target=fs::weakly_canonical(root),parent=fs::weakly_canonical(fs::temp_directory_path());
        if(target.parent_path()!=parent||!target.filename().string().starts_with("crimson-quader-tests-"))return;
        std::error_code ignored;fs::remove_all(target,ignored);}catch(...){}}
};
std::string utf8(const fs::path& path){const auto value=path.generic_u8string();return {reinterpret_cast<const char*>(value.data()),value.size()};}
std::string fileBytes(const fs::path& path){std::ifstream input(path,std::ios::binary);require(static_cast<bool>(input),"Cannot read test asset.");return {std::istreambuf_iterator<char>(input),{}};}
fs::path fixture(){
#ifdef CRIMSON_QUADER_FIXTURE_DIR
    return fs::path(CRIMSON_QUADER_FIXTURE_DIR)/"godot-bridge-fixture-v2";
#else
    return fs::path(__FILE__).parent_path()/"fixtures/quader/godot-bridge-fixture-v2";
#endif
}
std::size_t models(const fs::path& root){std::size_t count{};if(fs::is_directory(root/"assets/models"))for(const auto& entry:fs::directory_iterator(root/"assets/models")){
    require(!entry.path().filename().string().starts_with(".staging-"),"Quader import leaked a staging directory.");if(fs::is_regular_file(entry.path()/"manifest.json"))++count;}return count;}
QuaderSourceScene synthetic(){
    QuaderSourceScene scene;scene.project.id="66666666-6666-4666-8666-666666666666";scene.project.name="Synthetic";scene.id="11111111-1111-4111-8111-111111111111";scene.relativePath="Scenes/test.qdr";scene.fingerprint={123,456};
    QuaderSourceMesh mesh;mesh.name="Triangle";mesh.displayEntity=1;mesh.geometryEntity=2;mesh.definitionEntity=3;mesh.transform=math::translation({2,3,4});
    mesh.vertices={{{0,0,0},{0,0,1},{0,0}},{{1,0,0},{0,0,1},{1,0}},{{0,1,0},{0,0,1},{0,1}}};mesh.indices={0,1,2};mesh.surfaces={{UINT32_MAX,0,3}};
    mesh.collision=QuaderCollision::StaticTriangles;mesh.collisionPoints={{0,0,0},{1,0,0},{0,1,0}};scene.meshes.push_back(mesh);
    QuaderSourceLight directional;directional.name="Sun";directional.entity=7;directional.energy=1;directional.castShadows=true;scene.lights.push_back(directional);
    auto point=directional;point.entity=8;point.name="Point";point.type=1;point.range=4;point.energy=2;point.transform=math::translation({1,2,3});scene.lights.push_back(point);
    auto spot=point;spot.entity=9;spot.type=2;scene.lights.push_back(spot);return scene;
}
void geometryAndLights(){
    Temporary temporary;auto scene=synthetic();QuaderPrepareOptions options;options.projectRoot=temporary.root;options.scale=2;
    auto result=prepareQuaderImport(scene,options);require(result.meshes.size()==1&&result.lights.size()==2&&result.skippedSpots==1,"Quader supported-light import scope changed.");
    require(result.meshes[0].sourceKey=="mesh:1:2:3","Quader occurrence identity lost definition/member fields.");near(result.meshes[0].local[12],4);near(result.meshes[0].local[13],6);near(result.meshes[0].local[14],8);
    require(!result.meshes[0].collisionModelId.empty()&&result.preparedModels.size()==2,"Static triangle collision was not staged as a standalone model.");
    near(result.lights[1].local[12],2);near(result.lights[1].local[13],4);near(result.lights[1].radius,8);near(result.lights[1].intensity,2);
    const auto direction=math::transformDirection(result.lights[0].local,{0,0,-1});near(direction[0],0);near(direction[1],-1);near(direction[2],0);
    require(models(temporary.root)==0,"Preparation published a live model package.");commitAssetTransaction(temporary.root,result.files);
    ModelResolvers resolvers;resolvers.material=[](const std::string& id)->std::optional<ModelMaterial>{if(id==DefaultMaterialId)return defaultMaterialAsset().surface;return std::nullopt;};
    auto loaded=loadModelPackage(temporary.root/pathFromUtf8(result.meshes[0].modelId),temporary.root,resolvers);near(loaded->bounds.maximum[0],2);near(loaded->bounds.maximum[1],2);
    auto collision=loadModelPackage(temporary.root/pathFromUtf8(result.meshes[0].collisionModelId),temporary.root,resolvers);require(collision->primitives[0].indices.size()==3,"Saved triangle collision lost source triangles.");
    auto repeated=prepareQuaderImport(scene,options);require(repeated.meshes[0].modelId==result.meshes[0].modelId,"Unchanged source created a different managed model identity.");require(repeated.files.empty(),"Unchanged source rewrote published immutable packages.");
    options.scale=3;auto changed=prepareQuaderImport(scene,options);require(changed.meshes[0].modelId!=result.meshes[0].modelId,"Scale replacement cannot preserve prior Undo geometry.");
    require(fs::is_regular_file(temporary.root/pathFromUtf8(result.meshes[0].modelId)),"Preparing replacement removed prior Undo geometry.");
    options.scale=0;rejects([&]{prepareQuaderImport(scene,options);});options.scale=std::numeric_limits<float>::infinity();rejects([&]{prepareQuaderImport(scene,options);});
    options.scale=1;for(std::uint64_t i=10;i<14;++i){auto light=scene.lights[0];light.entity=i;scene.lights.push_back(light);}rejects([&]{prepareQuaderImport(scene,options);});
    options.syncLights=false;auto noLights=prepareQuaderImport(scene,options);
    require(noLights.lights.empty()&&noLights.skippedSpots==0&&noLights.meshes.size()==1&&!noLights.meshes[0].collisionModelId.empty(),"Disabled light sync imported lights or blocked geometry/collision on the source light limit.");
    (void)models(temporary.root);
}
void deletedGeometryReimport(){
    Temporary temporary;const auto scene=synthetic();QuaderPrepareOptions options;options.projectRoot=temporary.root;options.scale=2;
    const auto original=prepareQuaderImport(scene,options);commitAssetTransaction(temporary.root,original.files);
    const auto packageIds=[](const QuaderImportResult& result){return std::array{result.meshes.at(0).modelId,result.meshes.at(0).collisionModelId};};
    const auto erasePackages=[&](const QuaderImportResult& result){
        const auto ids=packageIds(result);AssetReferenceManager references(temporary.root);
        const std::array<AssetEntry,2> assets{{{ids[0],"Quader geometry",AssetKind::Model},{ids[1],"Quader collision",AssetKind::Model}}};
        references.replace(assets,{});std::vector<AssetFileChange> changes;
        for(const auto& id:ids){options.reservedIds.insert(id);const auto directory=pathFromUtf8(id).parent_path();
            for(const auto* member:{"manifest.json","model.gltf","source.gltf","buffers/0.bin"})changes.push_back({utf8(directory/member),std::nullopt});}
        changes.push_back({"assets/references.json",references.serialize()});commitAssetTransaction(temporary.root,changes);
    };
    erasePackages(original);const auto referenceBytes=fileBytes(temporary.root/"assets/references.json");
    const auto replacement=prepareQuaderImport(scene,options);const auto originalIds=packageIds(original),replacementIds=packageIds(replacement);
    require(replacement.preparedModels.size()==2&&replacement.meshes[0].sourceKey==original.meshes[0].sourceKey,
        "Reimporting deleted Quader packages lost visual/collision preparation or source occurrence identity.");
    for(std::size_t index=0;index<originalIds.size();++index){
        require(replacementIds[index]!=originalIds[index]&&!options.reservedIds.contains(replacementIds[index]),
            "Quader reimport resurrected an explicitly deleted visual or collision asset identity.");
        const auto oldDirectory=pathFromUtf8(originalIds[index]).parent_path(),newDirectory=pathFromUtf8(replacementIds[index]).parent_path();
        for(const auto* member:{"manifest.json","model.gltf","source.gltf","buffers/0.bin"}){
            const auto oldId=utf8(oldDirectory/member),newId=utf8(newDirectory/member);
            const auto oldFile=std::find_if(original.files.begin(),original.files.end(),[&](const auto& file){return file.id==oldId;});
            const auto newFile=std::find_if(replacement.files.begin(),replacement.files.end(),[&](const auto& file){return file.id==newId;});
            require(oldFile!=original.files.end()&&newFile!=replacement.files.end()&&oldFile->contents==newFile->contents,
                "Fresh Quader package identity changed source geometry or collision bytes.");
        }
    }
    commitAssetTransaction(temporary.root,replacement.files);
    require(fileBytes(temporary.root/"assets/references.json")==referenceBytes,"Quader reimport changed authored asset deletion policy.");
    AssetReferenceManager references(temporary.root);
    for(const auto& id:originalIds)require(references.deleted(id)&&references.resolve(id).empty()&&!fs::exists(temporary.root/pathFromUtf8(id)),
        "Reimporting Quader geometry restored an old deleted reference or package.");
    auto repeated=prepareQuaderImport(scene,options);
    require(packageIds(repeated)==replacementIds&&repeated.files.empty(),"Repeated Quader reimport did not reuse its replacement package generation.");
    erasePackages(replacement);const auto next=prepareQuaderImport(scene,options);const auto nextIds=packageIds(next);
    for(std::size_t index=0;index<nextIds.size();++index)require(nextIds[index]!=originalIds[index]&&nextIds[index]!=replacementIds[index]&&!options.reservedIds.contains(nextIds[index]),
        "Deleting and reimporting Quader geometry twice reused a deleted generation.");
    commitAssetTransaction(temporary.root,next.files);repeated=prepareQuaderImport(scene,options);
    require(packageIds(repeated)==nextIds&&repeated.files.empty(),"The second replacement Quader generation was not idempotent.");
}
void reservedGeometryMembersAndCorruption(){
    for(const auto* member:{"model.gltf","source.gltf","buffers/0.bin"}){
        Temporary temporary;const auto scene=synthetic();QuaderPrepareOptions options;options.projectRoot=temporary.root;
        const auto original=prepareQuaderImport(scene,options);commitAssetTransaction(temporary.root,original.files);
        const auto id=original.meshes[0].modelId,directory=utf8(pathFromUtf8(id).parent_path());
        options.reservedIds.insert(directory+"/"+member);
        const auto replacement=prepareQuaderImport(scene,options);
        require(replacement.meshes[0].modelId!=id&&replacement.meshes[0].collisionModelId==original.meshes[0].collisionModelId,
            "A deleted Quader package member did not reserve its containing visual package identity.");
        for(const auto& file:original.files)require(file.contents&&fileBytes(temporary.root/pathFromUtf8(file.id))==*file.contents,
            "Selecting a replacement Quader generation altered its previous immutable package.");
        commitAssetTransaction(temporary.root,replacement.files);const auto repeated=prepareQuaderImport(scene,options);
        require(repeated.meshes[0].modelId==replacement.meshes[0].modelId&&repeated.files.empty(),
            "Reserved-member Quader replacement created redundant package generations.");
    }
    Temporary temporary;const auto scene=synthetic();QuaderPrepareOptions options;options.projectRoot=temporary.root;
    const auto original=prepareQuaderImport(scene,options);commitAssetTransaction(temporary.root,original.files);
    const auto corruptId=utf8(pathFromUtf8(original.meshes[0].modelId).parent_path()/"model.gltf");
    const std::string corruptBytes="Modified immutable Quader geometry";writeFileAtomically(temporary.root/pathFromUtf8(corruptId),corruptBytes);
    rejects([&]{prepareQuaderImport(scene,options);});
    for(const auto& file:original.files)require(file.contents&&fileBytes(temporary.root/pathFromUtf8(file.id))==(file.id==corruptId?corruptBytes:*file.contents),
        "Rejected Quader import replaced a conflicting immutable package member.");
    require(models(temporary.root)==2,"Rejected corrupt-package import published geometry or leaked its staging directory.");
}
void clusteredPointLightCapacity(){
    Temporary temporary;auto scene=synthetic();const auto point=scene.lights[1];scene.lights.clear();
    for(std::uint64_t i=0;i<64;++i){auto light=point;light.entity=100+i;light.name="Torch "+std::to_string(i);
        light.transform=math::translation({static_cast<float>(i),2,-4});scene.lights.push_back(light);}
    QuaderPrepareOptions options;options.projectRoot=temporary.root;
    const auto result=prepareQuaderImport(scene,options);
    require(result.lights.size()==64&&result.meshes.size()==1,"Quader import retained the legacy sixteen-point-light ceiling.");
    for(std::size_t i=0;i<result.lights.size();++i){require(result.lights[i].point,"Clustered Quader point light changed type.");
        near(result.lights[i].local[12],static_cast<float>(i));}
    scene.meshes.clear();scene.lights.clear();scene.lights.reserve(MaximumClusteredPointLights+1);
    for(std::uint64_t i=0;i<MaximumClusteredPointLights;++i){auto light=point;light.entity=100+i;scene.lights.push_back(light);}
    require(prepareQuaderImport(scene,options).lights.size()==MaximumClusteredPointLights,"Quader could not import the full clustered-light capacity.");
    auto overflow=point;overflow.entity=100+MaximumClusteredPointLights;scene.lights.push_back(overflow);
    rejects([&]{prepareQuaderImport(scene,options);});
    options.syncLights=false;require(prepareQuaderImport(scene,options).lights.empty(),"Disabled light sync rejected a source beyond the engine light capacity.");
}
Json record(const std::string& project,const std::string& document,const std::string& path,const QuaderFingerprint& fingerprint,const std::string& token){
    std::ostringstream hash;hash<<std::hex<<std::setfill('0')<<std::setw(16)<<fingerprint.hash;
    return {{"version",1},{"project_id",project},{"document_id",document},{"relative_path",path},{"save_token",token},{"byte_count",std::to_string(fingerprint.bytes)},{"content_hash",hash.str()}};
}
void completionRecords(){
    const auto source=synthetic();const std::string token="aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";auto json=record(source.project.id,source.id,source.relativePath,source.fingerprint,token);
    const auto parsed=parseQuaderCompletedRecord(json.dump(),source.project.id,source.id);require(parsed.fingerprint==source.fingerprint,"Completed-save fingerprint did not retain exact 64-bit fields.");
    auto wrong=json;wrong["relative_path"]="../escape.qdr";rejects([&]{parseQuaderCompletedRecord(wrong.dump(),source.project.id,source.id);});wrong=json;wrong["content_hash"]="000000000000ABCD";rejects([&]{parseQuaderCompletedRecord(wrong.dump(),source.project.id,source.id);});
    wrong=json;wrong["byte_count"]="18446744073709551616";rejects([&]{parseQuaderCompletedRecord(wrong.dump(),source.project.id,source.id);});wrong=json;wrong["version"]=2;rejects([&]{parseQuaderCompletedRecord(wrong.dump(),source.project.id,source.id);});
    Temporary temporary;QuaderPrepareOptions options;options.projectRoot=temporary.root;options.completed=QuaderCompletedSave{};options.completed->scene=parsed;
    auto valid=prepareQuaderImport(source,options);require(!valid.sourceKey.empty(),"Sealed import did not preserve consumption identity.");options.completed->scene->fingerprint.hash^=1;rejects([&]{prepareQuaderImport(source,options);});
    auto save=QuaderCompletedSave{};save.scene=parsed;const auto first=quaderSaveKey(save,1,false);auto material=parsed;material.token="bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb";save.materials["material"]=material;
    require(quaderSaveKey(save,1,false)==first,"Preserved-material synchronization reacted to an unrelated material save.");require(quaderSaveKey(save,1,true)!=quaderSaveKey(QuaderCompletedSave{parsed,{},{}},1,true),"Explicit material refresh omitted material completion tokens.");
    require(quaderSaveKey(save,1,false,true)==first&&quaderSaveKey(save,1,false,false)!=first,"Light sync configuration did not retain the default consumption key or distinguish disabled imports.");
}
void nativeReaderMaterials(){
    Temporary temporary;const auto project=fixture()/"godot-bridge-fixture-v2.qproj";const auto catalog=inspectQuaderProject(project);require(catalog.scenes.size()==2,"Bundled native reader did not inspect the native project fixture.");
    auto source=readQuaderScene(project,"11111111-1111-4111-8111-111111111111");require(source->meshes.size()==3,"Native mesh occurrence count changed.");
    require(std::count_if(source->meshes.begin(),source->meshes.end(),[](const auto& mesh){return !mesh.visible;})==1,"Hidden source placement was dropped.");
    require(std::any_of(source->meshes.begin(),source->meshes.end(),[](const auto& mesh){return math::handedness(mesh.transform)<0;}),"Native mirrored instance transform was lost.");
    QuaderPrepareOptions options;options.projectRoot=temporary.root;auto result=prepareQuaderImport(*source,options);require(result.meshes.size()==3&&!result.preparedModels.empty(),"Native fixture did not prepare standalone models.");
    commitAssetTransaction(temporary.root,result.files);std::string editedId;for(const auto& identity:source->materials){const auto id=quaderMaterialAssetId(source->project.id,source->id,identity);auto material=loadMaterialAsset(temporary.root,id);
        if(!identity.embedded){editedId=id;material.name="Edited in Crimson";saveMaterialAsset(temporary.root,material);}options.existingMaterials.emplace(id,std::move(material));}
    require(!editedId.empty(),"Native fixture lost the shared project material.");
    auto preservedSource=*source;preservedSource.readMaterial=[](std::uint32_t)->QuaderSourceMaterial{throw std::runtime_error("Preserved import read missing source material assets.");};
    auto preserved=prepareQuaderImport(preservedSource,options);require(std::none_of(preserved.files.begin(),preserved.files.end(),[](const auto& file){return file.id.ends_with(".crimson-material");}),"Default reimport overwrote existing native materials.");
    require(loadMaterialAsset(temporary.root,editedId).name=="Edited in Crimson","Default reimport discarded a Crimson material edit.");
    auto local=loadMaterialAsset(temporary.root,editedId);local.name="Current Crimson graph";materialOutputNode(local.graph).output.roughness=.123f;canonicalizeMaterialAsset(local);saveMaterialAsset(temporary.root,local);
    const auto localBytes=fileBytes(temporary.root/pathFromUtf8(editedId));
    auto latest=prepareQuaderImport(preservedSource,options);require(fileBytes(temporary.root/pathFromUtf8(editedId))==localBytes&&
        std::none_of(latest.files.begin(),latest.files.end(),[&](const auto& file){return file.id==editedId;}),"Reuse mode overwrote a material edited after the captured material map.");
    bool latestGraph{};for(const auto& model:latest.preparedModels)for(std::size_t slot=0;slot<model->materialIds.size();++slot)if(model->materialIds[slot]==editedId){
        require(model->materials.at(slot).name==local.name,"Worker-prepared model retained a stale captured Crimson material.");latestGraph=true;}
    require(latestGraph,"Native fixture no longer references its shared material.");
    std::string imageId,originalImage;for(const auto& id:materialTextureReferences(local))if(id.starts_with("assets/textures/")){imageId=id;break;}
    for(const auto& file:result.files)if(file.id==imageId)originalImage=*file.contents;
    require(!imageId.empty(),"Native fixture did not normalize a source texture.");
    writeFileAtomically(temporary.root/pathFromUtf8(imageId),"Crimson texture edit");
    require(fs::remove(temporary.root/pathFromUtf8(editedId)),"Cannot remove disposable missing-material fixture.");
    auto missingMaterial=prepareQuaderImport(*source,options);
    require(std::any_of(missingMaterial.files.begin(),missingMaterial.files.end(),[&](const auto& file){return file.id==editedId;})&&
        std::none_of(missingMaterial.files.begin(),missingMaterial.files.end(),[&](const auto& file){return file.id==imageId;}),"Reuse mode did not recreate a missing material or overwrote its existing shared image.");
    commitAssetTransaction(temporary.root,missingMaterial.files);require(fileBytes(temporary.root/pathFromUtf8(imageId))=="Crimson texture edit","Missing material publication replaced an existing stable-ID texture in reuse mode.");
    // Sync-on must repair even a malformed destination; no old parse is needed.
    writeFileAtomically(temporary.root/pathFromUtf8(editedId),"invalid Crimson material");
    options.refreshMaterials=true;auto refreshed=prepareQuaderImport(*source,options);require(std::any_of(refreshed.files.begin(),refreshed.files.end(),[&](const auto& file){return file.id==editedId;}),"Explicit refresh did not stage the shared material replacement.");
    commitAssetTransaction(temporary.root,refreshed.files);require(loadMaterialAsset(temporary.root,editedId).name=="Project surface","Explicit refresh retained an old authored material name.");
    require(fileBytes(temporary.root/pathFromUtf8(imageId))==originalImage,"Enabled material sync did not restore source pixels at the same texture identity.");
    for(const auto& file:result.files)if(file.id.ends_with(".png")){const auto image=decodeManagedImage(temporary.root/pathFromUtf8(file.id));require(image.width>0&&!image.rgba.empty(),"Normalized source image is unreadable.");}
    const auto count=models(temporary.root);auto missing=readQuaderScene(project,"22222222-2222-4222-8222-222222222222");rejects([&]{prepareQuaderImport(*missing,options);});require(models(temporary.root)==count,"Failed source texture import changed managed geometry.");
}
void deletedMaterialsAndTexturesReimport(){
    Temporary temporary;const auto project=fixture()/"godot-bridge-fixture-v2.qproj";
    const auto source=readQuaderScene(project,"11111111-1111-4111-8111-111111111111");
    QuaderPrepareOptions options;options.projectRoot=temporary.root;
    const auto original=prepareQuaderImport(*source,options);commitAssetTransaction(temporary.root,original.files);
    AssetReferenceManager references(temporary.root);std::vector<AssetEntry> deleted;std::vector<AssetFileChange> changes;
    std::vector<std::string> originalMaterialIds,originalImageIds;
    for(const auto& file:original.files){
        if(file.id.ends_with(".crimson-material")){deleted.push_back({file.id,"Deleted Quader material",AssetKind::Material});originalMaterialIds.push_back(file.id);}
        else if(file.id.ends_with(".png")){deleted.push_back({file.id,"Deleted Quader image",AssetKind::Texture});originalImageIds.push_back(file.id);}
        else continue;
        options.reservedIds.insert(file.id);changes.push_back({file.id,std::nullopt});
    }
    require(!originalMaterialIds.empty()&&!originalImageIds.empty(),"Native Quader fixture must contain shared materials and normalized textures.");
    references.replace(deleted,{});changes.push_back({"assets/references.json",references.serialize()});commitAssetTransaction(temporary.root,changes);
    for(const auto& id:originalMaterialIds)options.materialOverrides.emplace(id,references.resolveModelMaterial(id));
    for(const auto& id:originalImageIds)options.imageOverrides.emplace(id,std::array{references.resolveTexture(id,false),references.resolveTexture(id,true)});
    const auto referenceBytes=fileBytes(temporary.root/"assets/references.json");
    const auto replacement=prepareQuaderImport(*source,options);std::vector<std::string> freshMaterialIds,freshImageIds;
    for(const auto& file:replacement.files){
        if(file.id.ends_with(".crimson-material"))freshMaterialIds.push_back(file.id);
        else if(file.id.ends_with(".png")){
            freshImageIds.push_back(file.id);
            require(std::any_of(original.files.begin(),original.files.end(),[&](const auto& before){return before.id.ends_with(".png")&&before.contents==file.contents;}),
                "Reimporting deleted Quader textures changed normalized source pixels.");
        }else continue;
        require(!options.reservedIds.contains(file.id),"Quader reimport reused a deleted material or texture identity.");
    }
    require(freshMaterialIds.size()==originalMaterialIds.size()&&freshImageIds.size()==originalImageIds.size(),
        "Reimporting deleted Quader assets kept fallback gray materials or omitted source textures.");
    commitAssetTransaction(temporary.root,replacement.files);
    for(const auto& id:freshMaterialIds){
        const auto material=loadMaterialAsset(temporary.root,id);require(material.id==id&&material.originModel.empty(),
            "Reimported Quader material is not an editable native material.");
        for(const auto& image:materialTextureReferences(material))require(!options.reservedIds.contains(image),
            "Fresh Quader material retained a deleted image reference.");
    }
    std::unordered_map<std::string,std::string> editedMaterials;
    for(std::size_t index=0;index<freshMaterialIds.size();++index){auto material=loadMaterialAsset(temporary.root,freshMaterialIds[index]);
        material.name="Crimson replacement edit "+std::to_string(index);materialOutputNode(material.graph).output.roughness=.123f;
        canonicalizeMaterialAsset(material);saveMaterialAsset(temporary.root,material);
        editedMaterials.emplace(material.id,fileBytes(temporary.root/pathFromUtf8(material.id)));
    }
    const auto refreshedImage=freshImageIds.front();const auto sourcePixels=fileBytes(temporary.root/pathFromUtf8(refreshedImage));
    writeFileAtomically(temporary.root/pathFromUtf8(refreshedImage),"Crimson replacement texture edit");
    auto preservedSource=*source;preservedSource.readMaterial=[](std::uint32_t)->QuaderSourceMaterial{throw std::runtime_error("Reuse mode unnecessarily requested deleted Quader source material data.");};
    const auto preserved=prepareQuaderImport(preservedSource,options);
    require(preserved.files.empty(),"Disabled material sync did not reuse the fresh Quader material/image/package identities.");
    for(std::size_t index=0;index<replacement.meshes.size();++index)require(preserved.meshes[index].modelId==replacement.meshes[index].modelId&&
        preserved.meshes[index].collisionModelId==replacement.meshes[index].collisionModelId,"Material reuse changed fresh Quader package identities.");
    for(const auto& [id,contents]:editedMaterials)require(fileBytes(temporary.root/pathFromUtf8(id))==contents,
        "Disabled material sync discarded edits to a replacement Quader material.");
    require(fileBytes(temporary.root/pathFromUtf8(refreshedImage))=="Crimson replacement texture edit",
        "Disabled material sync discarded an edit to a replacement Quader texture.");
    options.refreshMaterials=true;const auto refreshed=prepareQuaderImport(*source,options);
    for(const auto& id:freshMaterialIds)require(std::any_of(refreshed.files.begin(),refreshed.files.end(),[&](const auto& file){return file.id==id;}),
        "Enabled material sync did not refresh the replacement material under its existing identity.");
    commitAssetTransaction(temporary.root,refreshed.files);
    require(fileBytes(temporary.root/pathFromUtf8(refreshedImage))==sourcePixels,"Enabled material sync did not restore replacement texture source pixels.");
    for(const auto& [id,contents]:editedMaterials)require(fileBytes(temporary.root/pathFromUtf8(id))!=contents,
        "Enabled material sync retained an edited replacement material.");
    require(fileBytes(temporary.root/"assets/references.json")==referenceBytes,"Importing replacement materials/textures modified old deletion policy.");
    AssetReferenceManager retained(temporary.root);
    for(const auto& asset:deleted)require(retained.deleted(asset.id)&&!fs::exists(temporary.root/pathFromUtf8(asset.id)),
        "Quader reimport revived a deleted material or texture instead of creating a fresh identity.");
}
void modernProjectManifest(){
    Temporary temporary;const auto sourceRoot=temporary.root/"modern-project-v5",destination=temporary.root/"engine-v5";
    fs::copy(fixture(),sourceRoot,fs::copy_options::recursive);
    const auto oldManifest=sourceRoot/"godot-bridge-fixture-v2.qproj",project=sourceRoot/"modern-project-v5.qproj";
    Json manifest;{std::ifstream input(oldManifest,std::ios::binary);input>>manifest;}
    fs::rename(oldManifest,project);
    // Keep the native binary scene/material fixture intact, changing only the
    // disposable project's modern catalog and its selected scene filename.
    fs::rename(sourceRoot/"Scenes/mixed_materials.qdr",sourceRoot/"Scenes/scene_1.qdr");
    manifest["version"]=5;manifest["minimumProductRelease"]=1;manifest["lastSavedAppVersion"]="0.1.67";
    manifest["project"]["name"]="modern-project-v5";manifest["scenes"][0]["path"]="Scenes/scene_1.qdr";
    manifest["workspace"]["openScenes"]=Json::array({"Scenes/scene_1.qdr"});manifest["workspace"]["activeScene"]="Scenes/scene_1.qdr";
    manifest["deletedMaterialIds"]=Json::array();
    const std::string textureId="44444444-4444-4444-8444-444444444444";
    manifest["textures"]=Json::array({{{"id",textureId},{"path","Textures/albedo.PNG"},
        {"aliases",Json::array({"77777777-7777-4777-8777-777777777777"})}}});
    manifest["deletedTextureIds"]=Json::array({"88888888-8888-4888-8888-888888888888"});
    const auto modernBytes=manifest.dump(2);writeFileAtomically(project,modernBytes);
    const auto catalog=inspectQuaderProject(project);const std::string sceneId="11111111-1111-4111-8111-111111111111";
    require(catalog.id=="66666666-6666-4666-8666-666666666666"&&catalog.name=="modern-project-v5"&&catalog.scenes.size()==2&&catalog.defaultScene==sceneId,
        "Native reader rejected or lost a supported v5 project catalog.");
    const auto selected=std::find_if(catalog.scenes.begin(),catalog.scenes.end(),[&](const auto& entry){return entry.id==sceneId;});
    require(selected!=catalog.scenes.end()&&selected->path=="Scenes/scene_1.qdr","Modern project did not expose its selected scene_1 catalog entry.");
    const auto source=readQuaderScene(project,sceneId);
    require(source->id==sceneId&&source->relativePath=="Scenes/scene_1.qdr"&&source->meshes.size()==3&&!source->materials.empty(),
        "Modern project catalog succeeded but native scene decoding failed.");
    const auto identity=std::find_if(source->materials.begin(),source->materials.end(),[](const auto& value){
        return !value.embedded&&value.id=="33333333-3333-4333-8333-333333333333";});
    require(identity!=source->materials.end(),"Modern project scene lost its shared project material identity.");
    const auto material=source->readMaterial(static_cast<std::uint32_t>(identity-source->materials.begin()));
    require(material.name=="Project surface"&&material.textures[0].present&&material.textures[0].id==textureId&&
        material.textures[0].path=="Textures/albedo.PNG"&&material.textures[0].srgb&&
        material.textures[0].bytes.size()==fs::file_size(sourceRoot/"Textures/albedo.PNG"),
        "Modern texture catalog metadata broke lazy native material/image decoding.");
    near(material.color[0],.2f);near(material.roughness,.45f);near(material.metallic,.7f);
    fs::create_directories(destination);
    QuaderPrepareOptions options;options.projectRoot=destination;const auto prepared=prepareQuaderImport(*source,options);
    require(prepared.meshes.size()==3&&!prepared.preparedModels.empty(),"Modern scene did not prepare Crimson geometry.");
    const auto sharedMaterial=quaderMaterialAssetId(source->project.id,source->id,*identity);
    require(std::any_of(prepared.preparedModels.begin(),prepared.preparedModels.end(),[&](const auto& model){
        return std::find(model->materialIds.begin(),model->materialIds.end(),sharedMaterial)!=model->materialIds.end();}),
        "Modern scene models lost their shared editable material assignment.");
    commitAssetTransaction(destination,prepared.files);bool decodedTexture=false;
    for(const auto& file:prepared.files)if(file.id.ends_with(".png")){
        const auto image=decodeManagedImage(destination/pathFromUtf8(file.id));decodedTexture|=image.width>0&&image.height>0&&!image.rgba.empty();}
    require(decodedTexture&&!loadMaterialAsset(destination,sharedMaterial).graph.empty(),"Modern source material/texture did not publish readable Crimson assets.");
    {std::ifstream input(project,std::ios::binary);const std::string unchanged{std::istreambuf_iterator<char>(input),{}};
        require(unchanged==modernBytes,"Read/import rewrote the source project manifest.");}
    auto unsupported=manifest;unsupported["version"]=6;writeFileAtomically(project,unsupported.dump(2));
    rejects([&]{inspectQuaderProject(project);});rejects([&]{readQuaderScene(project,sceneId);});
}
// Internal read/import diagnostic for an arbitrary project. Only the existing
// disposable test root is writable; authored project and scene bytes are checked
// after both successful imports and exceptions.
void verifyImport(const fs::path& project,const std::string& sceneId){
    const auto manifestBytes=fileBytes(project);
    const auto catalog=inspectQuaderProject(project);
    const auto selected=std::find_if(catalog.scenes.begin(),catalog.scenes.end(),[&](const auto& scene){return scene.id==sceneId;});
    require(selected!=catalog.scenes.end(),"Requested diagnostic scene is absent from the project catalog.");
    const auto scenePath=project.parent_path()/pathFromUtf8(selected->path);
    const auto sceneBytes=fileBytes(scenePath);
    Temporary temporary;
    std::exception_ptr failure;
    try{
        const auto source=readQuaderScene(project,sceneId);
        require(source&&source->id==sceneId&&source->relativePath==selected->path,"Native reader returned a different source scene.");
        require(!source->meshes.empty(),"Selected source scene contains no exported mesh occurrences.");
        std::cout<<"Source project: "<<utf8(project)<<"\nSource scene: "<<utf8(scenePath)<<"\nScene ID: "<<sceneId<<'\n';
        std::uint64_t sourceVertices{},sourceTriangles{},sourceSurfaces{};
        for(const auto& mesh:source->meshes){
            require(!mesh.vertices.empty()&&!mesh.indices.empty()&&mesh.indices.size()%3==0,
                "Source mesh has no complete triangles: "+mesh.name);
            for(const auto index:mesh.indices)require(index<mesh.vertices.size(),"Source mesh index exceeds its vertex array: "+mesh.name);
            for(const auto& vertex:mesh.vertices)require(math::finite(vertex.position)&&math::finite(vertex.normal)&&math::finite(vertex.uv),
                "Source mesh contains a nonfinite vertex: "+mesh.name);
            for(const auto& surface:mesh.surfaces)require(surface.count%3==0&&std::uint64_t(surface.offset)+surface.count<=mesh.indices.size(),
                "Source mesh surface exceeds its triangle array: "+mesh.name);
            sourceVertices+=mesh.vertices.size();sourceTriangles+=mesh.indices.size()/3;sourceSurfaces+=mesh.surfaces.size();
            std::cout<<"Mesh "<<std::quoted(mesh.name)<<": vertices="<<mesh.vertices.size()<<", triangles="<<mesh.indices.size()/3
                <<", surfaces="<<mesh.surfaces.size()<<", visible="<<(mesh.visible?"yes":"no")<<'\n';
        }
        QuaderPrepareOptions options;options.projectRoot=temporary.root;
        const auto imported=prepareQuaderImport(*source,options);
        require(imported.meshes.size()==source->meshes.size()&&!imported.preparedModels.empty(),
            "Preparing the selected scene lost mesh occurrences or model packages.");
        commitAssetTransaction(temporary.root,imported.files);
        ModelResolvers resolvers;
        resolvers.material=[&](const std::string& id)->std::optional<ModelMaterial>{
            if(id==DefaultMaterialId)return defaultMaterialAsset().surface;
            return loadMaterialAsset(temporary.root,id).surface;
        };
        resolvers.image=[&](const std::string& id){
            require(id.starts_with("builtin:")||fs::is_regular_file(temporary.root/pathFromUtf8(id)),
                "Published model references a missing managed texture: "+id);
            return id;
        };
        std::size_t modelCount{},materialCount{},textureCount{};
        for(const auto& id:imported.assetIds){
            if(id.starts_with("assets/models/")&&id.ends_with("/manifest.json")){
                const auto model=loadModelPackage(temporary.root/pathFromUtf8(id),temporary.root,resolvers);
                require(model&&!model->primitives.empty(),"Published model package contains no primitives: "+id);
                std::uint64_t vertices{},triangles{};
                for(const auto& primitive:model->primitives){
                    require(!primitive.vertices.empty()&&!primitive.indices.empty()&&primitive.indices.size()%3==0,
                        "Published model primitive contains no complete triangles: "+id);
                    for(const auto index:primitive.indices)require(index<primitive.vertices.size(),"Published model index exceeds its vertex array: "+id);
                    for(const auto& vertex:primitive.vertices)require(math::finite(vertex.position)&&math::finite(vertex.normal),
                        "Published model contains a nonfinite vertex: "+id);
                    vertices+=primitive.vertices.size();triangles+=primitive.indices.size()/3;
                }
                ++modelCount;
                std::cout<<"Published model "<<std::quoted(model->name)<<": vertices="<<vertices<<", triangles="<<triangles
                    <<", surfaces="<<model->primitives.size()<<", id="<<id<<'\n';
            }else if(id.ends_with(".crimson-material")){
                const auto material=loadMaterialAsset(temporary.root,id);validateMaterialAsset(material);++materialCount;
            }else if(id.starts_with("assets/textures/")){
                const auto image=decodeManagedImage(temporary.root/pathFromUtf8(id));
                require(image.width&&image.height&&!image.rgba.empty(),"Published texture failed decoding: "+id);++textureCount;
            }
        }
        std::set<std::string> preparedIds;for(const auto& model:imported.preparedModels){
            require(bool(model),"Preparation returned an empty model package.");preparedIds.insert(model->id);
        }
        require(modelCount==preparedIds.size()&&modelCount==models(temporary.root),
            "Diagnostic failed to reload every published visual/collision model package.");
        std::cout<<"Source totals: meshes="<<source->meshes.size()<<", vertices="<<sourceVertices<<", triangles="<<sourceTriangles
            <<", surfaces="<<sourceSurfaces<<"\nImported totals: models="<<modelCount<<", materials="<<materialCount
            <<", textures="<<textureCount<<", lights="<<imported.lights.size()<<", skipped lights="<<imported.skippedLights
            <<", skipped spots="<<imported.skippedSpots<<"\n";
    }catch(...){failure=std::current_exception();}
    require(fileBytes(project)==manifestBytes,"Read/import diagnostic modified the authored project manifest.");
    require(fileBytes(scenePath)==sceneBytes,"Read/import diagnostic modified the authored scene.");
    if(failure)std::rethrow_exception(failure);
    std::cout<<"Source project/scene bytes unchanged; every imported package reloaded from the disposable test root.\n";
}
void coordinatorTransactions(){
    Temporary temporary;const auto sourceRoot=temporary.root/"godot-bridge-fixture-v2",destination=temporary.root/"engine";fs::copy(fixture(),sourceRoot,fs::copy_options::recursive);fs::create_directories(destination);
    QuaderLink draft;draft.projectPath=utf8(sourceRoot/"godot-bridge-fixture-v2.qproj");draft.sceneId="11111111-1111-4111-8111-111111111111";draft.autoSync=false;
    std::vector<QuaderTarget> targets{{7,{},false,{}}};QuaderBridge bridge(destination);std::size_t published{},assets{};bool dirty{};
    QuaderBridgeHooks hooks;hooks.dirtyMaterial=[&](const auto&){return dirty;};hooks.resolveMaterial=[](const auto& id){return id;};hooks.reservedAsset=[](const auto&){return false;};
    hooks.applyImport=[&](EntityId entity,const QuaderLink& link,const QuaderImportResult& result){require(entity==7,"Import changed its publication target.");targets[0].link=link;targets[0].link.saveKey=result.sourceKey;targets[0].consumedSaveKey=result.sourceKey;targets[0].hasImported=true;++published;return true;};
    hooks.publishAssets=[&](const QuaderImportResult& result){require(!result.preparedModels.empty(),"Publication was missing worker-prepared model data.");++assets;};
    const auto pump=[&](auto predicate,int milliseconds=12000){const auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(milliseconds);while(!predicate()&&std::chrono::steady_clock::now()<end){bridge.update(targets,1,false,hooks);std::this_thread::sleep_for(std::chrono::milliseconds{1});}
        require(predicate(),"Asynchronous Quader import did not complete: "+bridge.status(7));};
    bridge.requestImport(7,draft,1);pump([&]{return !bridge.busy(7);});require(published==1&&assets==1,"Initial draft import was rejected before the source link was committed.");
    const auto count=models(destination);dirty=true;auto refresh=targets[0].link;refresh.refreshMaterials=true;refresh.importScale=2;bridge.requestImport(7,refresh,1);
    pump([&]{return bridge.status(7).find("waits")!=std::string::npos;});require(published==1&&models(destination)==count,"Dirty material refresh partially published geometry/assets.");
    dirty=false;pump([&]{return !bridge.busy(7);});require(published==2,"Material refresh did not resume after the dirty document settled.");near(targets[0].link.importScale,2);
    auto earlier=targets[0].link;earlier.importScale=3;bridge.requestImport(7,earlier,1);bridge.update(targets,1,false,hooks);auto later=earlier;later.importScale=4;bridge.requestImport(7,later,1);
    pump([&]{return !bridge.busy(7);});require(published==3,"Superseded import published an extra scene transaction.");near(targets[0].link.importScale,4);
    // A failed scene transaction restores every external material/image/model file.
    const auto rollbackRoot=temporary.root/"rollback";fs::create_directories(rollbackRoot);QuaderBridge rollback(rollbackRoot);auto failing=hooks;failing.applyImport=[](EntityId,const QuaderLink&,const QuaderImportResult&){return false;};
    rollback.requestImport(7,draft,1);const auto end=std::chrono::steady_clock::now()+std::chrono::seconds{12};while(rollback.busy(7)&&std::chrono::steady_clock::now()<end){rollback.update(targets,1,false,failing);std::this_thread::sleep_for(std::chrono::milliseconds{1});}
    require(!rollback.busy(7)&&models(rollbackRoot)==0,"Rejected scene transaction left imported model files published.");
    require(!fs::is_directory(rollbackRoot/"assets/materials")||fs::is_empty(rollbackRoot/"assets/materials"),"Rejected scene transaction overwrote external materials.");
    // A completed source save refreshes an imported link; Undo consumption survives.
    auto native=readQuaderScene(pathFromUtf8(draft.projectPath),draft.sceneId);const std::string token="aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
    writeFileAtomically(sourceRoot/".quader/bridge/scenes"/pathFromUtf8(native->id+".json"),record(native->project.id,native->id,native->relativePath,native->fingerprint,token).dump());
    for(std::uint32_t i=0;i<native->materials.size();++i)if(!native->materials[i].embedded){const auto material=native->readMaterial(i);writeFileAtomically(sourceRoot/".quader/bridge/materials"/pathFromUtf8(material.identity.id+".json"),record(native->project.id,material.identity.id,material.relativePath,material.fingerprint,token).dump());}
    targets[0].link.autoSync=true;const auto before=published;pump([&]{return published>before;},8000);require(published==before+1,"Completed source save did not produce one scene publication.");
    for(int i=0;i<30;++i)bridge.update(targets,1,false,hooks);require(published==before+1&&!bridge.busy(7),"Consumed Quader save immediately replayed after publication.");
}
void materialSyncTransactions(){
    Temporary temporary;const auto sourceRoot=temporary.root/"godot-bridge-fixture-v2",destination=temporary.root/"engine";
    fs::copy(fixture(),sourceRoot,fs::copy_options::recursive);fs::create_directories(destination);
    QuaderLink draft;draft.projectPath=utf8(sourceRoot/"godot-bridge-fixture-v2.qproj");draft.sceneId="11111111-1111-4111-8111-111111111111";draft.autoSync=false;
    const auto source=readQuaderScene(pathFromUtf8(draft.projectPath),draft.sceneId);
    std::string sharedId,embeddedId,sourceName;
    for(std::uint32_t index=0;index<source->materials.size();++index){const auto& identity=source->materials[index];const auto id=quaderMaterialAssetId(source->project.id,source->id,identity);
        if(identity.embedded)embeddedId=id;else{sharedId=id;sourceName=source->readMaterial(index).name;}}
    require(!sharedId.empty()&&!embeddedId.empty(),"Material synchronization fixture needs shared and embedded materials.");
    std::vector<QuaderTarget> targets{{7,{},false,{}}};QuaderBridge bridge(destination);std::size_t published{},notifications{};std::uint64_t policyVersion{1};std::string expectedName;
    QuaderBridgeHooks hooks;hooks.resolveMaterial=[](const auto& id){return id;};hooks.reservedAsset=[](const auto&){return false;};hooks.assetPolicyRevision=[&]{return policyVersion;};
    hooks.applyImport=[&](EntityId,const QuaderLink& link,const QuaderImportResult& result){
        bool checked{};if(!expectedName.empty())for(const auto& model:result.preparedModels)for(std::size_t slot=0;slot<model->materialIds.size();++slot)if(model->materialIds[slot]==sharedId){
            require(model->materials.at(slot).name==expectedName,"Published geometry retained a stale shared material graph.");checked=true;}
        require(expectedName.empty()||checked,"Published geometry lost its shared material assignment.");
        targets[0].link=link;targets[0].link.saveKey=result.sourceKey;targets[0].consumedSaveKey=result.sourceKey;targets[0].hasImported=true;++published;return true;
    };
    hooks.publishAssets=[&](const QuaderImportResult&){++notifications;};
    const auto run=[&](QuaderBridge& owner,auto predicate,int milliseconds=12000){const auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(milliseconds);
        while(!predicate()&&std::chrono::steady_clock::now()<end){owner.update(targets,1,false,hooks);std::this_thread::sleep_for(std::chrono::milliseconds{1});}
        require(predicate(),"Material synchronization did not complete: "+owner.status(7));};
    const auto pump=[&](auto predicate,int milliseconds=12000){run(bridge,predicate,milliseconds);};
    bridge.requestImport(7,draft,1);pump([&]{return !bridge.busy(7);});require(published==1,"Initial material synchronization import failed.");
    auto local=loadMaterialAsset(destination,sharedId);local.name="Local material edit";saveMaterialAsset(destination,local);expectedName=local.name;
    hooks.dirtyMaterial=[&](const auto& id){return id==sharedId;};
    bridge.requestImport(7,targets[0].link,1);pump([&]{return !bridge.busy(7);});
    require(published==2&&loadMaterialAsset(destination,sharedId).name==expectedName,"Disabled material sync blocked or overwrote an edited Crimson material.");

    // Mutate a preserved graph and restore a previously absent material after
    // worker preparation, from the publication guard. Neither may be overwritten
    // using the stale candidate; the complete import must be prepared again.
    auto restored=loadMaterialAsset(destination,embeddedId);restored.name="Restored locally";
    require(fs::remove(destination/pathFromUtf8(embeddedId)),"Cannot remove disposable embedded material.");
    unsigned checks{};bool changed{};hooks.dirtyMaterial=[&](const auto& id){if(id==embeddedId&&++checks==2){
        local.name="Latest local material";saveMaterialAsset(destination,local);saveMaterialAsset(destination,restored);changed=true;}return false;};
    const auto beforeChange=published;bridge.requestImport(7,targets[0].link,1);pump([&]{return changed;});
    require(published==beforeChange&&bridge.status(7).find("changed")!=std::string::npos,"A stale material candidate published before rechecking destination files.");
    expectedName=local.name;pump([&]{return !bridge.busy(7);});
    require(published==beforeChange+1&&loadMaterialAsset(destination,embeddedId).name==restored.name,"Repreparing did not preserve a newly restored material in reuse mode.");

    // A committed toggle cancels an old manual request, including one waiting
    // on a dirty material with no remaining worker future.
    targets[0].link.refreshMaterials=true;hooks.dirtyMaterial=[&](const auto& id){return id==sharedId;};
    bridge.requestImport(7,targets[0].link,1);pump([&]{return bridge.status(7).find("waits")!=std::string::npos;});
    const auto beforeCancel=published;targets[0].link.refreshMaterials=false;bridge.update(targets,1,false,hooks);
    require(!bridge.busy(7)&&published==beforeCancel&&loadMaterialAsset(destination,sharedId).name==expectedName,"An obsolete material-sync request overwrote the new toggle or local material.");
    hooks.dirtyMaterial={};bridge.requestImport(7,targets[0].link,1);pump([&]{return !bridge.busy(7);});
    require(published==beforeCancel+1,"Canceling a blocked import left the coordinator unable to run another import.");

    unsigned policyChecks{};bool policyChanged{};hooks.assetPolicyRevision=[&]{if(++policyChecks==2){++policyVersion;policyChanged=true;}return policyVersion;};
    const auto beforePolicy=published;bridge.requestImport(7,targets[0].link,1);pump([&]{return policyChanged;});
    require(published==beforePolicy&&bridge.status(7).find("changed")!=std::string::npos,"A changed replacement/deletion policy published a stale candidate.");
    pump([&]{return !bridge.busy(7);});require(published==beforePolicy+1,"Import did not resume with the current asset policy.");hooks.assetPolicyRevision=[&]{return policyVersion;};

    std::string imageId;for(const auto& id:materialTextureReferences(loadMaterialAsset(destination,sharedId)))if(id.starts_with("assets/textures/")){imageId=id;break;}
    require(!imageId.empty(),"Shared fixture material lost its source texture.");const auto sourcePixels=fileBytes(destination/pathFromUtf8(imageId));
    writeFileAtomically(destination/pathFromUtf8(imageId),"Local texture pixels");const auto priorMaterial=fileBytes(destination/pathFromUtf8(sharedId));const auto priorModels=models(destination);
    targets[0].link.refreshMaterials=true;auto replacement=targets[0].link;replacement.importScale=2;QuaderBridge rejected(destination);auto accepting=hooks.applyImport;
    hooks.applyImport=[](EntityId,const QuaderLink&,const QuaderImportResult&){return false;};const auto priorNotifications=notifications;
    rejected.requestImport(7,replacement,1);run(rejected,[&]{return !rejected.busy(7);});
    require(fileBytes(destination/pathFromUtf8(sharedId))==priorMaterial&&fileBytes(destination/pathFromUtf8(imageId))=="Local texture pixels"&&models(destination)==priorModels&&notifications==priorNotifications,
        "Rejected scene import did not roll back existing shared material/image files and new geometry.");hooks.applyImport=std::move(accepting);
    // The coordinator must not parse the old destination before replacing it.
    writeFileAtomically(destination/pathFromUtf8(sharedId),"invalid old material");expectedName=sourceName;
    bridge.requestImport(7,targets[0].link,1);pump([&]{return !bridge.busy(7);});
    require(loadMaterialAsset(destination,sharedId).name==sourceName&&fileBytes(destination/pathFromUtf8(imageId))==sourcePixels,"Enabled material sync did not overwrite the graph and same-ID texture from Quader.");

    const std::string token="bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb";
    writeFileAtomically(sourceRoot/".quader/bridge/scenes"/pathFromUtf8(source->id+".json"),record(source->project.id,source->id,source->relativePath,source->fingerprint,token).dump());
    for(std::uint32_t index=0;index<source->materials.size();++index)if(!source->materials[index].embedded){const auto material=source->readMaterial(index);
        writeFileAtomically(sourceRoot/".quader/bridge/materials"/pathFromUtf8(material.identity.id+".json"),record(source->project.id,material.identity.id,material.relativePath,material.fingerprint,token).dump());}
    local=loadMaterialAsset(destination,sharedId);local.name="Automatic reuse edit";saveMaterialAsset(destination,local);expectedName=local.name;
    targets[0].link.autoSync=true;targets[0].link.refreshMaterials=false;const auto beforeAuto=published;
    pump([&]{return published>beforeAuto;},8000);require(published==beforeAuto+1&&loadMaterialAsset(destination,sharedId).name==expectedName,"Automatic reimport ignored disabled material sync.");
    targets[0].link.refreshMaterials=true;expectedName=sourceName;const auto beforeAutoRefresh=published;
    pump([&]{return published>beforeAutoRefresh;},8000);require(published==beforeAutoRefresh+1&&loadMaterialAsset(destination,sharedId).name==sourceName,"Automatic reimport did not apply enabled material sync.");
}
}
int main(int argc,char** argv){try{
    if(argc==4&&std::string(argv[1])=="--verify-import"){verifyImport(pathFromUtf8(argv[2]),argv[3]);return 0;}
    require(argc==1,"Usage: quader_bridge_tests [--verify-import <project.qproj> <scene-id>]");
    geometryAndLights();deletedGeometryReimport();reservedGeometryMembersAndCorruption();clusteredPointLightCapacity();completionRecords();nativeReaderMaterials();deletedMaterialsAndTexturesReimport();modernProjectManifest();coordinatorTransactions();materialSyncTransactions();std::cout<<"Quader reader, import, collision, deletion/reimport, material, completed-save and publication checks passed.\n";return 0;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
