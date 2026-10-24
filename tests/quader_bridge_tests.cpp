#include "quader/QuaderBridge.h"
#include "assets/AtomicFile.h"
#include "assets/ContentStorage.h"
#include "assets/ModelImportIdentity.h"
#include "assets/ModelMaterialSlots.h"
#include "assets/QuaderSceneBundle.h"
#include "resources/AssetId.h"
#include "resources/ImageDecoder.h"
#include "resources/Paths.h"
#include "resources/StreamingModel.h"
#include "scene/SceneDocument.h"
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
    Temporary(){fs::create_directories(root);initializeContentStorage(root);}
    ~Temporary(){try{const auto target=fs::weakly_canonical(root),parent=fs::weakly_canonical(fs::temp_directory_path());
        if(target.parent_path()!=parent||!target.filename().string().starts_with("crimson-quader-tests-"))return;
        std::error_code ignored;fs::remove_all(target,ignored);}catch(...){}}
};
std::string utf8(const fs::path& path){const auto value=path.generic_u8string();return {reinterpret_cast<const char*>(value.data()),value.size()};}
std::string fileBytes(const fs::path& path){std::ifstream input(path,std::ios::binary);require(static_cast<bool>(input),"Cannot read test asset.");return {std::istreambuf_iterator<char>(input),{}};}
std::string fileAssetId(const QuaderImportResult& result,const AssetFileChange& file){
    const auto relative=utf8(pathFromUtf8(file.id).lexically_normal());
    for(const auto& location:result.locations){const auto absolute=utf8(location.file.lexically_normal());
        if(absolute==relative||absolute.ends_with("/"+relative))return location.id;}
    return canonicalAssetId(file.id);
}
void commit(const fs::path& root,std::span<const AssetFileChange> files) {
    initializeContentStorage(root);
    commitAssetTransaction(root,files);
    std::vector<StoredAssetPath> paths;paths.reserve(files.size());
    for(const auto& file:files)if(file.contents&&file.id!="assets/references.json")paths.push_back({file.id,resolveAssetPath(root,file.id)});
    registerAssetPaths(root,paths);
}
void commit(const fs::path& root,const QuaderImportResult& result){
    commitAssetTransaction(root,result.files);auto paths=result.locations;
    for(const auto& file:result.files)if(file.contents&&file.id.starts_with("assets/")&&file.id!="assets/references.json")
        paths.push_back({file.id,resolveAssetPath(root,file.id)});
    registerAssetPaths(root,paths);
}
QuaderSceneBundle preparedBundle(const QuaderImportResult& result){
    for(const auto& file:result.files)if(file.id.ends_with(".crimson-quader")&&file.contents)return decodeQuaderSceneBundle(*file.contents);
    throw std::runtime_error("Prepared import did not contain a scene bundle.");
}
std::vector<AssetFileChange> looseLegacyFiles(const QuaderImportResult& result){
    const auto bundle=preparedBundle(result);std::vector<AssetFileChange> files;
    for(const auto& model:bundle.models){const auto base=pathFromUtf8(model.legacyManifest).parent_path();
        files.push_back({utf8(base/"manifest.json"),model.manifest});files.push_back({utf8(base/"model.gltf"),model.gltf});
        files.push_back({utf8(base/"source.gltf"),model.gltf});files.push_back({utf8(base/"buffers/0.bin"),model.buffer});}
    return files;
}
std::set<std::pair<std::string,fs::path>> locations(const fs::path& root) {
    std::set<std::pair<std::string,fs::path>> result;
    for(const auto& entry:storedAssetPaths(root))result.emplace(entry.id,entry.file);
    return result;
}
std::string logicalFile(const QuaderImportResult& result,const std::string& id) {
    for(const auto& file:result.files)if(file.id.ends_with(".crimson-quader")&&file.contents)
        for(const auto& model:decodeQuaderSceneBundle(*file.contents).models)if(model.id==canonicalAssetId(id))return model.legacyManifest;
    for(const auto& file:result.files)if(fileAssetId(result,file)==canonicalAssetId(id))return file.id;
    throw std::runtime_error("Prepared Quader package lost its file provenance.");
}
fs::path fixture(){
#ifdef CRIMSON_QUADER_FIXTURE_DIR
    return fs::path(CRIMSON_QUADER_FIXTURE_DIR)/"godot-bridge-fixture-v2";
#else
    return fs::path(__FILE__).parent_path()/"fixtures/quader/godot-bridge-fixture-v2";
#endif
}
std::size_t models(const fs::path& root){std::size_t count{};for(const auto& entry:storedAssetPaths(root))if(entry.file.filename()=="manifest.json"&&fs::is_regular_file(entry.file))++count;
    for(const auto& entry:fs::directory_iterator(root/".crimson"))require(!entry.path().filename().string().starts_with(".staging-quader-"),"Quader import leaked a staging directory.");return count;}
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
    scene.lights[0].color={.25f,.5f,1.f};scene.lights[1].color={.0031308f,0.f,.5f};
    auto result=prepareQuaderImport(scene,options);require(result.meshes.size()==1&&result.lights.size()==2&&result.skippedSpots==1,"Quader supported-light import scope changed.");
    near(result.lights[0].color[0],.53709873f);near(result.lights[0].color[1],.73535698f);near(result.lights[0].color[2],1.f);
    near(result.lights[1].color[0],.04044994f);near(result.lights[1].color[1],0.f);near(result.lights[1].color[2],.73535698f);
    const auto decode=[](float value){return value<=.04045f?value/12.92f:std::pow((value+.055f)/1.055f,2.4f);};
    for(std::size_t light=0;light<result.lights.size();++light)for(std::size_t channel=0;channel<3;++channel)
        near(decode(result.lights[light].color[channel]),scene.lights[light].color[channel]);
    require(scene.lights[0].color==Vec3{.25f,.5f,1.f}&&scene.lights[1].color==Vec3{.0031308f,0.f,.5f},"Import changed canonical source light colors.");
    require(result.meshes[0].sourceKey=="mesh:1:2:3","Quader occurrence identity lost definition/member fields.");near(result.meshes[0].local[12],4);near(result.meshes[0].local[13],6);near(result.meshes[0].local[14],8);
    require(result.meshes[0].collision&&result.preparedModels.size()==1,"Static triangle collision was not prepared as native collider data only.");
    near(result.lights[1].local[12],2);near(result.lights[1].local[13],4);near(result.lights[1].radius,8);near(result.lights[1].intensity,2);
    const auto direction=math::transformDirection(result.lights[0].local,{0,0,-1});near(direction[0],0);near(direction[1],-1);near(direction[2],0);
    require(models(temporary.root)==0,"Preparation published a live model package.");commit(temporary.root,result);
    ModelResolvers resolvers;resolvers.material=[](const std::string& id)->std::optional<ModelMaterial>{if(id==DefaultMaterialId)return defaultMaterialAsset().surface;return std::nullopt;};
    auto loaded=loadModelPackage(resolveAssetPath(temporary.root,result.meshes[0].modelId),temporary.root,resolvers);near(loaded->bounds.maximum[0],2);near(loaded->bounds.maximum[1],2);
    require(loaded->materials.size()==1&&loaded->materialIds==std::vector<std::string>{DefaultMaterialId}&&loaded->primitives[0].material==0,
        "One Quader source material created an unused native fallback slot.");
    const auto collision=result.meshes[0].collision;
    require(collision->positions==std::vector<Vec3>{{0,0,0},{2,0,0},{0,2,0}}&&collision->indices==std::vector<std::uint32_t>{0,1,2},
        "Native triangle collision changed source positions, scale or winding.");
    require(models(temporary.root)==1&&result.assetIds.size()==1,"Native collision created a separately browsable model asset.");
    auto repeated=prepareQuaderImport(scene,options);require(repeated.meshes[0].modelId==result.meshes[0].modelId,"Unchanged source created a different managed model identity.");require(repeated.files.empty(),"Unchanged source rewrote published immutable packages.");
    auto collisionOnly=synthetic();collisionOnly.meshes[0].vertices.clear();collisionOnly.meshes[0].indices.clear();collisionOnly.meshes[0].surfaces.clear();collisionOnly.lights.clear();
    const auto nativeOnly=prepareQuaderImport(collisionOnly,options);
    require(nativeOnly.meshes[0].modelId.empty()&&nativeOnly.meshes[0].collision&&
        preparedBundle(nativeOnly).models.size()==1&&!preparedBundle(nativeOnly).models[0].current&&
        nativeOnly.assetIds.empty()&&nativeOnly.preparedModels.empty(),"A collision-only source published a renderable or browsable model asset.");
    options.scale=3;auto changed=prepareQuaderImport(scene,options);require(changed.meshes[0].modelId!=result.meshes[0].modelId,"Scale replacement cannot preserve prior Undo geometry.");
    require(fs::is_regular_file(resolveAssetPath(temporary.root,result.meshes[0].modelId)),"Preparing replacement removed prior Undo geometry.");
    options.scale=0;rejects([&]{prepareQuaderImport(scene,options);});options.scale=std::numeric_limits<float>::infinity();rejects([&]{prepareQuaderImport(scene,options);});
    options.scale=1;for(std::uint64_t i=10;i<14;++i){auto light=scene.lights[0];light.entity=i;scene.lights.push_back(light);}rejects([&]{prepareQuaderImport(scene,options);});
    options.syncLights=false;auto noLights=prepareQuaderImport(scene,options);
    require(noLights.lights.empty()&&noLights.skippedSpots==0&&noLights.meshes.size()==1&&noLights.meshes[0].collision,"Disabled light sync imported lights or blocked geometry/collision on the source light limit.");
    (void)models(temporary.root);
}
void sourceHierarchyPlacement(){
    Temporary temporary;auto source=synthetic();QuaderPrepareOptions options;options.projectRoot=temporary.root;options.scale=2;
    Mat4 affine=math::translation({4,5,6});affine[0]=-2;affine[4]=.35f;affine[5]=3;
    const auto nested=math::multiply(affine,math::translation({7,8,9}));
    source.groups={{"group:root","","Room",affine,true,2},{"group:child","group:root","Walls",nested,true,1}};
    source.meshes[0].parentKey="group:child";source.meshes[0].order=3;
    source.meshes[0].transform=math::multiply(nested,math::translation({1,2,3}));
    source.lights[2].parentKey="group:root";source.lights[1].parentKey="light:9";
    const auto result=prepareQuaderImport(source,options);
    require(result.groups.size()==2&&result.groups[1].name=="Walls"&&result.groups[1].parentKey=="group:root"&&
        result.meshes[0].parentKey=="group:child"&&result.meshes[0].order==3,
        "Import lost source group names, nesting or sibling order.");
    auto world=math::multiply(result.groups[0].local,result.groups[1].local);world=math::multiply(world,result.meshes[0].local);
    auto expected=source.meshes[0].transform;for(std::size_t i=12;i<15;++i)expected[i]*=2;
    for(std::size_t i=0;i<16;++i)near(world[i],expected[i]);
    near(result.meshes[0].local[12],2);near(result.meshes[0].local[13],4);near(result.meshes[0].local[14],6);
    require(result.lights[1].parentKey=="group:root","A skipped spotlight orphaned its imported child.");
    const auto lightWorld=math::multiply(result.groups[0].local,result.lights[1].local);
    near(lightWorld[12],2);near(lightWorld[13],4);near(lightWorld[14],6);
    options.syncLights=false;source.groups[1].parentKey="light:8";source.lights[1].parentKey="group:root";
    const auto noLights=prepareQuaderImport(source,options);
    require(noLights.lights.empty()&&noLights.groups[1].parentKey=="group:root","Disabled light sync orphaned a source group.");
    source.groups[0].parentKey="group:child";rejects([&]{prepareQuaderImport(source,options);});
    source.groups[0].parentKey="missing";rejects([&]{prepareQuaderImport(source,options);});
    source.groups[0].parentKey="";source.groups[0].transform=math::scaling({0,1,1});rejects([&]{prepareQuaderImport(source,options);});
}
void uvInterpolationPublication(){
    Temporary temporary;auto source=synthetic();source.lights.clear();QuaderPrepareOptions options;options.projectRoot=temporary.root;options.scale=3;
    const auto ordinary=prepareQuaderImport(source,options);const auto ordinaryModel=ordinary.meshes.front().modelId;const auto ordinaryBundle=preparedBundle(ordinary);
    const auto ordinaryDocument=Json::parse(ordinaryBundle.models.front().gltf);
    require(!ordinaryDocument["meshes"][0]["primitives"][0]["attributes"].contains("_CRIMSON_UV_INTERPOLATION_COORD")&&
        !ordinaryDocument["meshes"][0]["primitives"][0]["attributes"].contains("_CRIMSON_CIRCULAR_COORD"),"Ordinary import gained UV interpolation attributes.");
    commit(temporary.root,ordinary);
    const std::array<Vec4,3> coordinates{{{1,0,1,0},{0,2,2,1.57079637f},{1,1,1.41421356f,.78539816f}}};const Vec4 coefficient{2,.3f,-.5f,1.25f};
    for(std::size_t vertex=0;vertex<3;++vertex){source.meshes[0].vertices[vertex].uvInterpolationCoordinates=coordinates[vertex];source.meshes[0].vertices[vertex].uvInterpolationCorrection=coefficient;source.meshes[0].vertices[vertex].uvInterpolationMode=1;}
    const auto circular=prepareQuaderImport(source,options);require(circular.meshes.front().modelId!=ordinaryModel,"Circular interpolation failed to create a distinct immutable geometry generation.");
    const auto verify=[&](const ModelAsset& model){const auto& vertices=model.primitives.at(0).vertices;require(vertices.size()==3,"Circular import changed source topology.");
        for(std::size_t vertex=0;vertex<3;++vertex){const auto& expected=source.meshes[0].vertices[vertex];require(vertices[vertex].uvInterpolationCoordinates==expected.uvInterpolationCoordinates&&
            vertices[vertex].uvInterpolationCorrection==expected.uvInterpolationCorrection&&vertices[vertex].uvInterpolationMode==float(expected.uvInterpolationMode),
            "Import changed the local interpolation payload with Import Scale.");}near(vertices[1].position[0],3);};
    verify(*circular.preparedModels.at(0));require(circular.meshes[0].collision->positions==ordinary.meshes[0].collision->positions,"Circular UV interpolation altered collision geometry.");
    commit(temporary.root,circular);ModelResolvers resolvers;resolvers.material=[](const std::string&)->std::optional<ModelMaterial>{return defaultMaterialAsset().surface;};
    verify(*loadModelPackage(circular.meshes[0].modelId,temporary.root,resolvers));
    const auto repeated=prepareQuaderImport(source,options);require(repeated.files.empty()&&repeated.meshes[0].modelId==circular.meshes[0].modelId,"Unchanged circular UV import rewrote or duplicated geometry.");
    auto malformed=source;malformed.meshes[0].vertices[0].uvInterpolationCoordinates={};malformed.meshes[0].vertices[0].uvInterpolationCorrection={};
    rejects([&]{prepareQuaderImport(malformed,options);});malformed=source;malformed.meshes[0].vertices[0].uvInterpolationCorrection[0]=std::numeric_limits<float>::quiet_NaN();
    rejects([&]{prepareQuaderImport(malformed,options);});
    malformed=source;malformed.meshes[0].vertices[0].uvInterpolationMode=2;rejects([&]{prepareQuaderImport(malformed,options);});
    malformed=source;malformed.meshes[0].vertices[0].uvInterpolationMode=3;rejects([&]{prepareQuaderImport(malformed,options);});
    const std::array<Vec4,3> quadCoordinates{{{0,0,0,1},{1,0,-.2f,1},{0,1,0,1}}};
    for(std::size_t vertex=0;vertex<3;++vertex){source.meshes[0].vertices[vertex].uvInterpolationCoordinates=quadCoordinates[vertex];source.meshes[0].vertices[vertex].uvInterpolationMode=2;}
    const auto quad=prepareQuaderImport(source,options);require(quad.meshes[0].modelId!=ordinaryModel&&quad.meshes[0].modelId!=circular.meshes[0].modelId,
        "Quad interpolation reused an incompatible immutable geometry generation.");
    verify(*quad.preparedModels.at(0));commit(temporary.root,quad);verify(*loadModelPackage(quad.meshes[0].modelId,temporary.root,resolvers));
    const auto quadDocument=Json::parse(preparedBundle(quad).models.back().gltf);const auto& attributes=quadDocument["meshes"][0]["primitives"][0]["attributes"];
    require(attributes.contains("_CRIMSON_UV_INTERPOLATION_MODE")&&!attributes.contains("_CRIMSON_CIRCULAR_COORD"),"Current writer retained the superseded circle-only representation.");
    const auto repeatedQuad=prepareQuaderImport(source,options);require(repeatedQuad.files.empty()&&repeatedQuad.meshes[0].modelId==quad.meshes[0].modelId,"Unchanged quad UV import rewrote geometry.");
    for(auto& vertex:source.meshes[0].vertices){vertex.uvInterpolationCoordinates={};vertex.uvInterpolationCorrection={};vertex.uvInterpolationMode=0;}
    const auto restored=prepareQuaderImport(source,options);require(restored.meshes[0].modelId==ordinaryModel,"Disabling circular interpolation changed ordinary geometry identity.");
    const auto restoredBundle=preparedBundle(restored);const auto original=std::find_if(restoredBundle.models.begin(),restoredBundle.models.end(),[&](const auto& model){return model.id==ordinaryModel;});
    require(original!=restoredBundle.models.end()&&original->gltf==ordinaryBundle.models[0].gltf&&original->buffer==ordinaryBundle.models[0].buffer,
        "Circular import modified the retained ordinary mesh generation.");
}

void currentSourceMeshSet(){
    Temporary temporary;auto source=synthetic();source.lights.clear();QuaderPrepareOptions options;options.projectRoot=temporary.root;
    for(std::uint64_t i=2;i<=3;++i){auto mesh=source.meshes.front();mesh.displayEntity=i;mesh.name="Wall "+std::to_string(i);mesh.vertices[1].position[0]=static_cast<float>(i);source.meshes.push_back(mesh);}
    const auto first=prepareQuaderImport(source,options);commit(temporary.root,first);const auto removed=first.meshes.back().modelId;
    source.meshes.pop_back();const auto second=prepareQuaderImport(source,options);const auto bundle=preparedBundle(second);
    require(bundle.models.size()==3&&std::count_if(bundle.models.begin(),bundle.models.end(),[](const auto& model){return model.current;})==2&&
        std::any_of(bundle.models.begin(),bundle.models.end(),[&](const auto& model){return model.id==removed&&!model.current;}),
        "Reimport did not retire exactly the removed wall while retaining its immutable data for reference checks.");
    commit(temporary.root,second);source.meshes.clear();const auto empty=prepareQuaderImport(source,options);const auto cleared=preparedBundle(empty);
    require(std::none_of(cleared.models.begin(),cleared.models.end(),[](const auto& model){return model.current;}),
        "Removing every source mesh failed to publish an empty current model set.");
    commit(temporary.root,empty);const auto repeated=prepareQuaderImport(source,options);require(repeated.files.empty(),"Repeated empty source rewrote the bundle.");
    source=synthetic();source.lights.clear();const auto restored=prepareQuaderImport(source,options);const auto reactivated=preparedBundle(restored);
    require(restored.meshes[0].modelId==first.meshes[0].modelId&&std::count_if(reactivated.models.begin(),reactivated.models.end(),[](const auto& model){return model.current;})==1,
        "Restoring source geometry duplicated its retained generation or revived other removed walls.");
}
void geometryReuseIgnoresNamesAndPlacement(){
    Temporary temporary;auto scene=synthetic();QuaderPrepareOptions options;options.projectRoot=temporary.root;
    auto second=scene.meshes.front();second.name="Another occurrence";second.displayEntity=11;
    second.transform=math::translation({20,30,40});scene.meshes.push_back(second);
    const auto first=prepareQuaderImport(scene,options);
    require(first.meshes.size()==2&&first.meshes[0].modelId==first.meshes[1].modelId&&first.preparedModels.size()==1&&first.assetIds.size()==1,
        "Identical named occurrences created multiple immutable geometry packages or publication candidates.");
    require(preparedBundle(first).models.size()==1&&
        std::count_if(first.files.begin(),first.files.end(),[](const auto& file){return file.id.ends_with(".crimson-quader");})==1&&
        std::none_of(first.files.begin(),first.files.end(),[](const auto& file){return !file.id.ends_with(".crimson-quader")&&
            (file.id.starts_with("assets/models/")||file.id.starts_with("Content/models/"));}),
        "One source scene produced loose authored mesh packages or more than one scene bundle.");
    commit(temporary.root,first);const auto initialLocations=locations(temporary.root);const auto revision=contentStorageRevision(temporary.root);
    scene.meshes[0].name="Renamed source";scene.meshes[0].transform=math::translation({-5,6,-7});
    scene.meshes[1].name="Renamed instance";scene.fingerprint={456,789};
    for(bool refresh:{false,true}){
        options.refreshMaterials=refresh;const auto repeated=prepareQuaderImport(scene,options);
        require(repeated.files.empty()&&repeated.preparedModels.size()==1&&repeated.meshes[0].modelId==first.meshes[0].modelId&&
            repeated.meshes[1].modelId==first.meshes[0].modelId,"Rename, placement or repeated synchronization duplicated unchanged geometry.");
        commit(temporary.root,repeated);
        require(locations(temporary.root)==initialLocations&&contentStorageRevision(temporary.root)==revision&&models(temporary.root)==1,
            "An unchanged repeated import changed the physical asset catalog or its revision.");
    }
    scene.meshes[1].vertices[1].uv={.25f,.75f};
    const auto edited=prepareQuaderImport(scene,options);
    require(edited.meshes[0].modelId==first.meshes[0].modelId&&edited.meshes[1].modelId!=first.meshes[0].modelId,
        "Content reuse merged a real UV edit into a previously shared immutable model.");
    commit(temporary.root,edited);require(models(temporary.root)==2,"A changed visual geometry version overwrote its shared predecessor.");
    const auto retained=findQuaderSceneBundle(temporary.root,scene.project.id,scene.id);
    require(retained&&retained->second.models.size()==2&&retained->second.models[0].id==first.meshes[0].modelId&&
        preparedBundle(edited).id==preparedBundle(first).id,
        "A real geometry edit replaced its prior generation or created another scene bundle.");

    // Seed the former name-dependent layout, then verify the new importer reuses
    // it in place instead of producing an upgrade-only asset generation.
    Temporary legacy;options.projectRoot=legacy.root;options.refreshMaterials=false;auto oldSource=synthetic();
    auto legacyFiles=looseLegacyFiles(prepareQuaderImport(oldSource,options));
    std::string gltf,buffer,manifest;
    for(const auto& file:legacyFiles){if(file.id.ends_with("/model.gltf"))gltf=*file.contents;
        else if(file.id.ends_with("/buffers/0.bin"))buffer=*file.contents;else if(file.id.ends_with("/manifest.json"))manifest=*file.contents;}
    auto metadata=Json::parse(manifest);metadata["materialAssets"].push_back(DefaultMaterialId);const auto materialIds=metadata.at("materialAssets").dump();
    const auto raw=oldSource.project.id+"|"+oldSource.id+"|"+gltf+"\n"+materialIds+"\n"+buffer;
    const auto content=modelImportDataDigest({reinterpret_cast<const std::uint8_t*>(raw.data()),raw.size()});
    const auto base="assets/models/quader-"+content;metadata["quader"]["content"]=content;
    for(auto& file:legacyFiles){const auto member=file.id.substr(file.id.find("/quader-")+1);const auto slash=member.find('/');
        file.id=base+member.substr(slash);if(file.id.ends_with("/manifest.json"))file.contents=metadata.dump(2)+"\n";}
    commit(legacy.root,legacyFiles);initializeContentStorage(legacy.root);const auto oldId=canonicalAssetId(base+"/manifest.json");const auto oldLocations=locations(legacy.root);
    oldSource.meshes[0].name="Renamed old package";const auto migrated=prepareQuaderImport(oldSource,options);
    require(migrated.files.size()==2&&std::all_of(migrated.files.begin(),migrated.files.end(),[](const auto& file){return file.id.ends_with(".crimson-quader");})&&
        migrated.meshes[0].modelId==oldId&&locations(legacy.root)==oldLocations,
        "Reusing compatible preexisting geometry rewrote its members instead of only adopting the known source scene name.");
    commit(legacy.root,migrated);const auto warm=prepareQuaderImport(oldSource,options);
    require(warm.files.empty()&&warm.meshes[0].modelId==oldId,"A compatible preexisting package or adopted scene directory was not stable after reimport.");
}
void authoredMeshOwnership(){
    Temporary temporary;auto source=synthetic();source.lights.clear();QuaderPrepareOptions options;options.projectRoot=temporary.root;
    auto ordinary=source.meshes.front();ordinary.definitionEntity=0;ordinary.displayEntity=ordinary.geometryEntity=10;ordinary.name="Stair A";
    auto duplicate=ordinary;duplicate.displayEntity=duplicate.geometryEntity=11;duplicate.name="Stair B";duplicate.transform=math::translation({20,30,40});
    auto instance=ordinary;instance.displayEntity=50;instance.geometryEntity=102;instance.definitionEntity=100;instance.name="Linked stair";
    auto occurrence=instance;occurrence.displayEntity=60;occurrence.transform=math::translation({-5,6,7});
    auto otherDefinition=instance;otherDefinition.displayEntity=70;otherDefinition.definitionEntity=200;
    auto otherMember=instance;otherMember.geometryEntity=103;
    source.meshes={ordinary,duplicate,instance,occurrence,otherDefinition,otherMember};
    const auto first=prepareQuaderImport(source,options);const auto& meshes=first.meshes;
    require(meshes.size()==6&&first.preparedModels.size()==5&&first.assetIds.size()==5&&preparedBundle(first).models.size()==5,
        "Ordinary Quader objects or distinct definition members were implicitly deduplicated.");
    require(meshes[0].modelId!=meshes[1].modelId&&meshes[2].modelId==meshes[3].modelId&&
        meshes[0].modelId!=meshes[2].modelId&&meshes[2].modelId!=meshes[4].modelId&&meshes[2].modelId!=meshes[5].modelId&&
        meshes[4].modelId!=meshes[5].modelId&&meshes[2].sourceKey!=meshes[3].sourceKey,
        "Only formal occurrences of the same Quader definition/member may share a mesh asset.");
    const std::array<std::string,6> owners{"object:10","object:11","instance:100:102","instance:100:102","instance:200:102","instance:100:103"};
    const auto bundle=preparedBundle(first);
    for(std::size_t i=0;i<meshes.size();++i){
        const auto model=std::find_if(bundle.models.begin(),bundle.models.end(),[&](const auto& value){return value.id==meshes[i].modelId;});
        require(model!=bundle.models.end()&&Json::parse(model->manifest).at("quader").at("meshOwner")==owners[i]&&
            meshes[i].name==source.meshes[i].name&&meshes[i].local==source.meshes[i].transform&&bool(meshes[i].collision),
            "Owned geometry lost source identity, occurrence name, placement or independent collision payload.");
    }
    commit(temporary.root,first);const auto initialLocations=locations(temporary.root);const auto initialRevision=contentStorageRevision(temporary.root);
    for(std::size_t i=0;i<source.meshes.size();++i){source.meshes[i].name="Renamed "+std::to_string(i);source.meshes[i].transform=math::translation({float(i),4,5});}
    const auto repeated=prepareQuaderImport(source,options);
    require(repeated.files.empty()&&repeated.preparedModels.size()==5&&locations(temporary.root)==initialLocations&&contentStorageRevision(temporary.root)==initialRevision,
        "Same-source rename or placement changed owned geometry generations.");
    for(std::size_t i=0;i<meshes.size();++i)require(repeated.meshes[i].modelId==meshes[i].modelId,"Owned geometry identity changed after a source rename.");
    source.meshes[0].vertices[1].uv={.25f,.75f};const auto changed=prepareQuaderImport(source,options);const auto generations=preparedBundle(changed);
    require(changed.meshes[0].modelId!=meshes[0].modelId&&changed.meshes[1].modelId==meshes[1].modelId&&
        changed.meshes[2].modelId==meshes[2].modelId&&generations.models.size()==6&&
        std::any_of(generations.models.begin(),generations.models.end(),[&](const auto& model){return model.id==meshes[0].modelId&&!model.current;}),
        "An ordinary object edit changed another owner or overwrote the prior immutable generation.");
    commit(temporary.root,changed);source.meshes[0].vertices=ordinary.vertices;
    const auto restored=prepareQuaderImport(source,options);
    require(restored.meshes[0].modelId==meshes[0].modelId&&restored.meshes[1].modelId==meshes[1].modelId,
        "Restoring ordinary geometry reused another object's asset rather than its own retained generation.");
}
void contentOnlyOwnershipMigration(){
    Temporary temporary;auto source=synthetic();source.lights.clear();QuaderPrepareOptions options;options.projectRoot=temporary.root;
    const auto seed=prepareQuaderImport(source,options);auto legacy=preparedBundle(seed);auto& old=legacy.models.front();
    auto manifest=Json::parse(old.manifest),document=Json::parse(old.gltf);manifest["quader"].erase("meshOwner");
    const auto omit=[](auto&& self,Json& value)->void {
        if(value.is_object()){value.erase("name");for(auto& child:value)self(self,child);}
        else if(value.is_array())for(auto& child:value)self(self,child);
    };
    omit(omit,document);auto materials=manifest.at("materialAssets");materials.push_back(DefaultMaterialId);
    const auto raw="Quader visual geometry v2|"+source.project.id+"|"+source.id+"|"+document.dump()+"\n"+materials.dump()+"\n"+old.buffer;
    const auto content=modelImportDataDigest({reinterpret_cast<const std::uint8_t*>(raw.data()),raw.size()});
    const auto base="assets/models/quader-"+content;old.legacyManifest=base+"/manifest.json";
    const std::array<const char*,4> members{"manifest.json","model.gltf","source.gltf","buffers/0.bin"};
    for(std::size_t i=0;i<members.size();++i)old.memberIds[i]=canonicalAssetId(base+"/"+members[i]);
    old.id=old.memberIds.front();manifest["quader"]["content"]=content;old.manifest=manifest.dump(2)+"\n";
    const auto bundleFile=defaultQuaderSceneBundlePath(temporary.root,legacy);
    writeFileAtomically(bundleFile,encodeQuaderSceneBundle(legacy));registerAssetPath(temporary.root,legacy.id,bundleFile);
    initializeQuaderSceneBundles(temporary.root);const auto oldLocations=quaderBundleCacheLocations(temporary.root,legacy);
    std::vector<std::string> oldBytes;for(const auto& location:oldLocations)oldBytes.push_back(fileBytes(location.file));
    const auto authored=createMaterialAsset(temporary.root,"Historical material override");
    SceneDocument historical(temporary.root);const auto oldEntity=historical.createModel(old.id,"Historical shared mesh");
    auto record=*historical.scene().find(oldEntity);record.modelMaterials={AssetRef<MaterialAsset>{authored.id}};require(historical.updateEntity(oldEntity,record),historical.status());
    require(historical.save("Historical scene"),historical.status());const auto sceneFile=resolveAssetPath(temporary.root,historical.assetId());const auto sceneBytes=fileBytes(sceneFile);
    historical.select({oldEntity},oldEntity);require(historical.eraseSelection(),historical.status());
    auto ordinary=source.meshes.front();ordinary.definitionEntity=0;ordinary.displayEntity=ordinary.geometryEntity=10;
    auto duplicate=ordinary;duplicate.displayEntity=duplicate.geometryEntity=11;
    auto occurrence=source.meshes.front();occurrence.displayEntity=50;auto repeatedOccurrence=occurrence;repeatedOccurrence.displayEntity=60;
    source.meshes={ordinary,duplicate,occurrence,repeatedOccurrence};const auto migrated=prepareQuaderImport(source,options);const auto owned=preparedBundle(migrated);
    require(migrated.preparedModels.size()==3&&owned.models.size()==4&&migrated.meshes[0].modelId!=migrated.meshes[1].modelId&&
        migrated.meshes[2].modelId==migrated.meshes[3].modelId&&std::all_of(migrated.meshes.begin(),migrated.meshes.end(),[&](const auto& mesh){return mesh.modelId!=old.id;})&&
        std::any_of(owned.models.begin(),owned.models.end(),[&](const auto& model){return model.id==old.id&&!model.current&&model.memberIds==old.memberIds&&model.manifest==old.manifest&&model.gltf==old.gltf&&model.buffer==old.buffer;}),
        "Migrating content-only assets did not split ordinary owners, retain formal sharing and preserve the old immutable package.");
    commit(temporary.root,migrated);
    for(std::size_t i=0;i<oldLocations.size();++i)require(resolveAssetPath(temporary.root,oldLocations[i].id)==oldLocations[i].file&&fileBytes(oldLocations[i].file)==oldBytes[i],
        "Source ownership migration rewrote or unregistered a historical geometry member.");
    require(fileBytes(sceneFile)==sceneBytes&&historical.undo()&&historical.scene().find(oldEntity)->model->id==old.id&&
        historical.scene().find(oldEntity)->modelMaterials==record.modelMaterials,
        "Source ownership migration changed a saved scene, authored override or undoable historical geometry reference.");
    ModelResolvers resolvers;resolvers.material=[](const std::string&)->std::optional<ModelMaterial>{return defaultMaterialAsset().surface;};
    const auto reopened=loadModelPackage(resolveAssetPath(temporary.root,old.id),temporary.root,resolvers);
    require(reopened->materialIds.size()==1&&reopened->primitives.size()==1,"A historical content-only mesh stopped loading after source ownership migration.");
    const auto repeated=prepareQuaderImport(source,options);require(repeated.files.empty(),"Owned migration repeated on a warm source import.");
    for(std::size_t i=0;i<source.meshes.size();++i)require(repeated.meshes[i].modelId==migrated.meshes[i].modelId,"Owned migration changed a stable source asset on reimport.");
}
void legacyUnusedFallbackPackages(){
    for(bool nameDependent:{false,true}){
        Temporary temporary;auto source=synthetic();QuaderPrepareOptions options;options.projectRoot=temporary.root;
        const auto prepared=prepareQuaderImport(source,options);auto files=looseLegacyFiles(prepared);
        std::string gltf,buffer,manifest;
        for(const auto& file:files){if(file.id.ends_with("/model.gltf"))gltf=*file.contents;
            else if(file.id.ends_with("/buffers/0.bin"))buffer=*file.contents;else if(file.id.ends_with("/manifest.json"))manifest=*file.contents;}
        auto metadata=Json::parse(manifest);const auto document=Json::parse(gltf);
        require(document.at("materials").size()==1&&metadata.at("materialAssets").size()==1,
            "A fresh Quader package still serialized an unused fallback slot.");
        metadata["materialAssets"].push_back(DefaultMaterialId);
        auto content=metadata.at("quader").at("content").get<std::string>();
        if(nameDependent){const auto raw=source.project.id+"|"+source.id+"|"+gltf+"\n"+metadata.at("materialAssets").dump()+"\n"+buffer;
            content=modelImportDataDigest({reinterpret_cast<const std::uint8_t*>(raw.data()),raw.size()});metadata["quader"]["content"]=content;}
        const auto base="assets/models/quader-"+content;
        for(auto& file:files){const auto member=file.id.substr(file.id.find("/quader-")+1);const auto slash=member.find('/');
            file.id=base+member.substr(slash);if(file.id.ends_with("/manifest.json"))file.contents=metadata.dump(2)+"\n";}
        commit(temporary.root,files);initializeContentStorage(temporary.root);const auto oldId=canonicalAssetId(base+"/manifest.json");
        const auto unchanged=prepareQuaderImport(source,options);
        require(unchanged.files.size()==2&&std::all_of(unchanged.files.begin(),unchanged.files.end(),[](const auto& file){return file.id.ends_with(".crimson-quader");})&&
            unchanged.meshes[0].modelId==oldId&&unchanged.preparedModels.size()==1&&
            unchanged.preparedModels[0]->materials.size()==1&&unchanged.preparedModels[0]->materialIds==std::vector<std::string>{DefaultMaterialId},
            "Legacy fallback compatibility changed an immutable package ID, slot count or used assignment.");
        commit(temporary.root,unchanged);const auto oldLocations=locations(temporary.root);const auto revision=contentStorageRevision(temporary.root);
        source.meshes[0].name="Renamed legacy fallback mesh";source.meshes[0].transform=math::translation({11,12,13});
        const auto renamed=prepareQuaderImport(source,options);
        require(renamed.files.empty()&&renamed.meshes[0].modelId==oldId&&locations(temporary.root)==oldLocations&&
            contentStorageRevision(temporary.root)==revision,"Legacy fallback normalization duplicated geometry after a source rename or placement edit.");
        for(const auto& file:files)require(file.contents&&fileBytes(checkedManagedAssetPath(temporary.root,file.id))==*file.contents,
            "Legacy fallback compatibility rewrote an immutable package member.");
        const auto corruptId=base+"/model.gltf";const std::string corrupt="Modified immutable legacy fallback package";
        writeFileAtomically(resolveAssetPath(temporary.root,corruptId),corrupt);
        // Once migrated, cache files are disposable. A damaged expansion is
        // repaired from the unchanged bundle without changing the model UUID.
        if(nameDependent)source.meshes[0].name="Triangle";
        const auto repair=prepareQuaderImport(source,options);
        require(repair.meshes[0].modelId==oldId,"Repairing a migrated cache changed its source model identity.");
        commit(temporary.root,repair);
        for(const auto& file:files)require(file.contents&&fileBytes(checkedManagedAssetPath(temporary.root,file.id))==*file.contents,
            "Migrated legacy package cache was not reconstructed from its bundle.");
    }
    Temporary temporary;auto source=synthetic();auto& mesh=source.meshes[0];mesh.indices={0,1,2,0,1,2};
    mesh.surfaces={{UINT32_MAX,0,3},{UINT32_MAX,3,3}};QuaderPrepareOptions options;options.projectRoot=temporary.root;
    const auto prepared=prepareQuaderImport(source,options);
    require(prepared.preparedModels.size()==1&&prepared.preparedModels[0]->materials.size()==2&&
        prepared.preparedModels[0]->materialIds==std::vector<std::string>{DefaultMaterialId,DefaultMaterialId}&&
        prepared.preparedModels[0]->primitives.size()==2&&prepared.preparedModels[0]->primitives[0].material==0&&prepared.preparedModels[0]->primitives[1].material==1,
        "Removing the unused fallback merged real source surface slots or changed their indices.");
    for(const auto& file:prepared.files)if(file.id.ends_with("/manifest.json"))
        require(Json::parse(*file.contents).at("materialAssets").size()==2,"A two-surface Quader package serialized a phantom third material.");
}
void deletedGeometryReimport(){
    Temporary temporary;const auto scene=synthetic();QuaderPrepareOptions options;options.projectRoot=temporary.root;options.scale=2;
    const auto original=prepareQuaderImport(scene,options);commit(temporary.root,original);
    const auto packageIds=[](const QuaderImportResult& result){return std::array{result.meshes.at(0).modelId};};
    const auto erasePackages=[&](const QuaderImportResult& result){
        const auto ids=packageIds(result);AssetReferenceManager references(temporary.root);
        const std::array<AssetEntry,1> assets{{{ids[0],"Quader geometry",AssetKind::Model}}};
        const auto prepared=prepareQuaderBundleModelRemoval(temporary.root,ids);
        require(prepared.modelIds==std::vector<std::string>{ids[0]}&&prepared.memberIds.size()==4,
            "Explicit Quader mesh deletion did not prepare its bundle record and exact generated members.");
        references.replace(assets,{});auto changes=prepared.files;
        changes.push_back({"assets/references.json",references.serialize()});
        auto removedLocations=prepared.memberIds;removedLocations.insert(removedLocations.end(),prepared.removedBundleIds.begin(),prepared.removedBundleIds.end());
        changes.push_back({".crimson/asset-locations.json",prepareAssetPathRegistration(temporary.root,{},removedLocations)});
        validateQuaderBundleModelRemoval(temporary.root,prepared);commitAssetTransaction(temporary.root,changes);refreshContentStorage(temporary.root);
        const auto remaining=findQuaderSceneBundle(temporary.root,scene.project.id,scene.id);
        require(!remaining&&prepared.removedBundleIds==std::vector<std::string>{preparedBundle(result).id},"Deleting the last Quader mesh retained its empty bundle or source-bundle registration.");
        const auto stored=locations(temporary.root);
        for(const auto& member:prepared.memberLocations)require(!fs::exists(member.file)&&!stored.contains({member.id,member.file}),
            "Explicit Quader mesh deletion retained a generated member file or UUID location.");
        initializeQuaderSceneBundles(temporary.root);
        for(const auto& member:prepared.memberLocations)require(!fs::exists(member.file),"Startup reconstruction revived explicitly deleted Quader geometry.");
        AssetReferenceManager reopened(temporary.root);require(reopened.valid()&&reopened.deleted(ids[0])&&reopened.resolve(ids[0]).empty(),
            "Removing a bundled mesh's location invalidated or cleared its authored deletion policy.");
        const auto reserved=reservedModelImportIds(temporary.root);options.reservedIds.clear();options.reservedIds.insert(reserved.begin(),reserved.end());
    };
    erasePackages(original);const auto referenceBytes=fileBytes(resolveAssetPath(temporary.root,"assets/references.json"));
    const auto replacement=prepareQuaderImport(scene,options);const auto originalIds=packageIds(original),replacementIds=packageIds(replacement);
    require(replacement.preparedModels.size()==1&&replacement.meshes[0].collision&&
        replacement.meshes[0].collision->fingerprint==original.meshes[0].collision->fingerprint&&replacement.meshes[0].sourceKey==original.meshes[0].sourceKey,
        "Reimporting deleted Quader packages lost visual/collision preparation or source occurrence identity.");
    for(std::size_t index=0;index<originalIds.size();++index){
        require(replacementIds[index]!=originalIds[index]&&!options.reservedIds.contains(replacementIds[index]),
            "Quader reimport resurrected an explicitly deleted visual or collision asset identity.");
        const auto before=preparedBundle(original),after=preparedBundle(replacement);
        const auto oldModel=std::find_if(before.models.begin(),before.models.end(),[&](const auto& model){return model.id==originalIds[index];});
        const auto newModel=std::find_if(after.models.begin(),after.models.end(),[&](const auto& model){return model.id==replacementIds[index];});
        require(oldModel!=before.models.end()&&newModel!=after.models.end()&&oldModel->manifest==newModel->manifest&&
            oldModel->gltf==newModel->gltf&&oldModel->buffer==newModel->buffer,
            "Fresh Quader model identity changed source geometry or collision bytes.");
    }
    commit(temporary.root,replacement);
    require(fileBytes(resolveAssetPath(temporary.root,"assets/references.json"))==referenceBytes,"Quader reimport changed authored asset deletion policy.");
    AssetReferenceManager references(temporary.root);
    for(const auto& id:originalIds)require(references.valid()&&references.deleted(id)&&references.resolve(id).empty()&&
        !fs::exists(quaderBundleCacheDirectory(temporary.root,preparedBundle(original).id,id)/"manifest.json"),
        "Reimporting Quader geometry restored an old deleted reference or package.");
    auto repeated=prepareQuaderImport(scene,options);
    require(packageIds(repeated)==replacementIds&&repeated.files.empty(),"Repeated Quader reimport did not reuse its replacement package generation.");
    erasePackages(replacement);const auto next=prepareQuaderImport(scene,options);const auto nextIds=packageIds(next);
    for(std::size_t index=0;index<nextIds.size();++index)require(nextIds[index]!=originalIds[index]&&nextIds[index]!=replacementIds[index]&&!options.reservedIds.contains(nextIds[index]),
        "Deleting and reimporting Quader geometry twice reused a deleted generation.");
    commit(temporary.root,next);repeated=prepareQuaderImport(scene,options);
    require(packageIds(repeated)==nextIds&&repeated.files.empty(),"The second replacement Quader generation was not idempotent.");
}
void reservedGeometryMembersAndCorruption(){
    for(const auto* member:{"model.gltf","source.gltf","buffers/0.bin"}){
        Temporary temporary;const auto scene=synthetic();QuaderPrepareOptions options;options.projectRoot=temporary.root;
        const auto original=prepareQuaderImport(scene,options);commit(temporary.root,original);
        const auto id=original.meshes[0].modelId,directory=utf8(pathFromUtf8(logicalFile(original,id)).parent_path());
        options.reservedIds.insert(directory+"/"+member);
        const auto replacement=prepareQuaderImport(scene,options);
        require(replacement.meshes[0].modelId!=id&&replacement.meshes[0].collision&&
            replacement.meshes[0].collision->fingerprint==original.meshes[0].collision->fingerprint,
            "A deleted Quader package member did not reserve its containing visual package identity.");
        for(const auto& file:original.files)require(file.contents&&fileBytes(checkedManagedAssetPath(temporary.root,file.id))==*file.contents,
            "Selecting a replacement Quader generation altered its previous immutable package.");
        commit(temporary.root,replacement);const auto repeated=prepareQuaderImport(scene,options);
        require(repeated.meshes[0].modelId==replacement.meshes[0].modelId&&repeated.files.empty(),
            "Reserved-member Quader replacement created redundant package generations.");
    }
    Temporary temporary;const auto scene=synthetic();QuaderPrepareOptions options;options.projectRoot=temporary.root;
    const auto original=prepareQuaderImport(scene,options);commit(temporary.root,original);
    const auto corruptId=utf8(pathFromUtf8(logicalFile(original,original.meshes[0].modelId)).parent_path()/"model.gltf");
    const std::string corruptBytes="Modified immutable Quader geometry";writeFileAtomically(resolveAssetPath(temporary.root,corruptId),corruptBytes);
    const auto repair=prepareQuaderImport(scene,options);
    require(repair.meshes[0].modelId==original.meshes[0].modelId&&repair.files.size()==1,
        "A damaged disposable model expansion was not repaired from its bundle.");
    commit(temporary.root,repair);
    for(const auto& file:original.files)require(file.contents&&fileBytes(checkedManagedAssetPath(temporary.root,file.id))==*file.contents,
        "Reconstructing Quader cache modified the durable bundle or its geometry.");
    const auto source=findQuaderSceneBundle(temporary.root,scene.project.id,scene.id);
    require(source.has_value(),"Imported geometry has no durable source bundle.");
    const auto sourceBytes=fileBytes(source->first);writeFileAtomically(source->first,"Corrupt authoritative scene bundle");
    rejects([&]{prepareQuaderImport(scene,options);});
    require(fileBytes(source->first)=="Corrupt authoritative scene bundle"&&models(temporary.root)==1,
        "Rejected source-bundle corruption overwrote data or leaked its staging directory.");
    writeFileAtomically(source->first,sourceBytes);
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
    require(first.starts_with("quader:v4:"),"Logical topology support did not invalidate earlier save consumption keys.");
    require(quaderSaveKey(save,1,false)==first,"Preserved-material synchronization reacted to an unrelated material save.");require(quaderSaveKey(save,1,true)!=quaderSaveKey(QuaderCompletedSave{parsed,{},{}},1,true),"Explicit material refresh omitted material completion tokens.");
    require(quaderSaveKey(save,1,false,true)==first&&quaderSaveKey(save,1,false,false)!=first,"Light sync configuration did not retain the default consumption key or distinguish disabled imports.");
}
void nativeReaderHierarchy(const fs::path& directory=fixture().parent_path()/"hierarchy"){
    const auto project=directory/"hierarchy.qproj",sceneFile=directory/"Scenes/hierarchy.qdr";
    const auto manifestBytes=fileBytes(project),sceneBytes=fileBytes(sceneFile);
    const auto source=readQuaderScene(project,"88888888-8888-4888-8888-888888888888");
    require(source->groups.size()==7&&source->meshes.size()==6&&source->lights.size()==1,
        "Native Quader hierarchy lost authored groups or multi-member instance geometry.");
    const auto group=[&](const std::string& key)->const QuaderSourceGroup&{
        const auto found=std::find_if(source->groups.begin(),source->groups.end(),[&](const auto& value){return value.sourceKey==key;});
        require(found!=source->groups.end(),"Native hierarchy omitted an expected source group.");return *found;
    };
    require(group("group:10:0").name=="Level Group"&&!group("group:10:0").visible&&group("group:10:0").order==0&&
        group("group:11:0").name=="Nested Group"&&group("group:11:0").parentKey=="group:10:0"&&group("group:11:0").visible&&
        group("group:40:0").name=="Empty Group"&&group("group:40:0").parentKey.empty(),
        "Native hierarchy flattened nested/empty groups, changed authored names or baked parent hiding into children.");
    require(group("group:50:0").name=="Instance One"&&group("group:50:0").parentKey=="group:10:0"&&
        group("group:101:50").name=="Assembly Inner"&&group("group:101:50").parentKey=="group:50:0"&&
        group("group:60:0").name=="Instance Two"&&group("group:101:60").parentKey=="group:60:0",
        "Native instance expansion lost occurrence names or nested member groups.");
    const std::array<std::string,6> names{"Wall B","Wall A","Arch","Column","Arch","Column"};
    const std::array<std::string,6> parents{"group:10:0","group:11:0","group:101:50","group:50:0","group:101:60","group:60:0"};
    const std::array<std::uint64_t,6> displays{12,13,50,50,60,60},geometries{12,13,102,103,102,103};
    const std::array<std::uint32_t,6> order{1,3,7,8,12,13};
    for(std::size_t index=0;index<names.size();++index) {
        const auto mesh=std::find_if(source->meshes.begin(),source->meshes.end(),[&](const auto& value){return value.displayEntity==displays[index]&&value.geometryEntity==geometries[index];});
        require(mesh!=source->meshes.end()&&mesh->name==names[index]&&mesh->parentKey==parents[index]&&mesh->order==order[index]&&mesh->visible&&
            mesh->definitionEntity==(displays[index]>=50?100u:0u),
            "Native mesh names, authored sibling order, parent grouping or local visibility changed.");
    }
    require(std::count_if(source->meshes.begin(),source->meshes.end(),[](const auto& mesh){return mesh.displayEntity==50;})==2&&
        std::count_if(source->meshes.begin(),source->meshes.end(),[](const auto& mesh){return mesh.displayEntity==60;})==2,
        "Native hierarchy fixture did not retain distinct members sharing a concrete instance occurrence.");
    require(source->lights[0].name=="Window Light"&&source->lights[0].parentKey=="group:11:0"&&source->lights[0].order==4,
        "Native grouped light lost its source name, hierarchy or order.");
    Temporary temporary;QuaderPrepareOptions options;options.projectRoot=temporary.root;options.scale=2;
    const auto prepared=prepareQuaderImport(*source,options);
    struct Node {std::string parent;Mat4 local;};std::unordered_map<std::string,Node> nodes;
    const auto append=[&](const auto& values){for(const auto& value:values)nodes.emplace(value.sourceKey,Node{value.parentKey,value.local});};
    append(prepared.groups);append(prepared.meshes);append(prepared.lights);
    const auto world=[&](const std::string& key){const auto& node=nodes.at(key);auto value=node.local;auto parent=node.parent;
        for(std::size_t depth=0;!parent.empty();++depth){require(depth<nodes.size(),"Prepared native hierarchy contains a cycle.");
            const auto& ancestor=nodes.at(parent);value=math::multiply(ancestor.local,value);parent=ancestor.parent;}
        return value;
    };
    for(const auto& value:source->groups){const auto actual=world(value.sourceKey);for(std::size_t axis=12;axis<15;++axis)near(actual[axis],value.transform[axis]*2);}
    for(std::size_t index=0;index<prepared.meshes.size();++index){const auto actual=world(prepared.meshes[index].sourceKey);
        for(std::size_t axis=0;axis<16;++axis)near(actual[axis],source->meshes[index].transform[axis]*(axis>=12&&axis<15?2.f:1.f));}
    commit(temporary.root,prepared);const auto repeated=prepareQuaderImport(*source,options);
    require(repeated.files.empty()&&repeated.groups.size()==prepared.groups.size(),"Repeated grouped native import rewrote unchanged geometry.");
    require(fileBytes(project)==manifestBytes&&fileBytes(sceneFile)==sceneBytes,"Native hierarchy import changed its authored Quader source.");
}
void nativeReaderMaterials(){
    Temporary temporary;const auto project=fixture()/"godot-bridge-fixture-v2.qproj";const auto catalog=inspectQuaderProject(project);require(catalog.scenes.size()==2,"Bundled native reader did not inspect the native project fixture.");
    auto source=readQuaderScene(project,"11111111-1111-4111-8111-111111111111");require(source->meshes.size()==3,"Native mesh occurrence count changed.");
    require(std::count_if(source->meshes.begin(),source->meshes.end(),[](const auto& mesh){return !mesh.visible;})==1,"Hidden source placement was dropped.");
    require(std::any_of(source->meshes.begin(),source->meshes.end(),[](const auto& mesh){return math::handedness(mesh.transform)<0;}),"Native mirrored instance transform was lost.");
    QuaderPrepareOptions options;options.projectRoot=temporary.root;auto result=prepareQuaderImport(*source,options);require(result.meshes.size()==3&&!result.preparedModels.empty(),"Native fixture did not prepare standalone models.");
    commit(temporary.root,result);
    require(result.meshes.size()==source->meshes.size(),"Native fixture changed occurrence order.");
    for(std::size_t mesh=0;mesh<source->meshes.size();++mesh){
        const auto& occurrence=result.meshes[mesh];if(occurrence.modelId.empty())continue;
        const auto prepared=std::find_if(result.preparedModels.begin(),result.preparedModels.end(),[&](const auto& model){return model->id==occurrence.modelId;});
        require(prepared!=result.preparedModels.end(),"Native occurrence lost its prepared model.");
        std::vector<std::string> expected;
        for(const auto& surface:source->meshes[mesh].surfaces)if(surface.count)
            expected.push_back(surface.material==UINT32_MAX?DefaultMaterialId:
                canonicalAssetId(quaderMaterialAssetId(source->project.id,source->id,source->materials.at(surface.material))));
        if(expected.empty()&&!source->meshes[mesh].indices.empty())expected.push_back(DefaultMaterialId);
        const auto slots=usedModelMaterialSlots(**prepared);
        require(slots.size()==expected.size()&&(*prepared)->materials.size()==slots.size()&&(*prepared)->materialIds.size()==slots.size(),
            "Native import created an unused fallback or lost a real source material slot.");
        for(std::size_t index=0;index<slots.size();++index)
            require((*prepared)->materialIds.at(slots[index])==expected[index],
                "Native used-slot presentation changed a source material assignment.");
        const auto manifest=resolveAssetPath(temporary.root,occurrence.modelId),directory=manifest.parent_path();
        require(Json::parse(fileBytes(manifest)).at("materialAssets").size()==expected.size(),
            "Native importer serialized more material mappings than source surface slots.");
        const std::array<fs::path,4> files{manifest,directory/"model.gltf",directory/"source.gltf",directory/"buffers/0.bin"};
        std::array<std::string,4> before;for(std::size_t index=0;index<files.size();++index)before[index]=fileBytes(files[index]);
        const auto reopened=loadModelPackage(manifest,temporary.root);
        require(reopened->id==occurrence.modelId&&reopened->materials.size()==expected.size()&&reopened->materialIds==(*prepared)->materialIds&&
            usedModelMaterialSlots(*reopened)==slots,"Native package reopen changed used slots or immutable identities.");
        const auto stream=streaming::buildModelCache(*reopened,temporary.root);
        const auto cached=streaming::loadModelMetadataCache(manifest,temporary.root);
        require(!stream.primitives.empty()&&cached&&cached->materials.size()==expected.size()&&cached->materialIds==reopened->materialIds&&
            usedModelMaterialSlots(*cached)==slots,"Warm native model metadata exposed an unused fallback slot.");
        for(std::size_t index=0;index<files.size();++index)
            require(fileBytes(files[index])==before[index],"Used-slot presentation or cache warming rewrote an immutable native package.");
    }
    std::string editedId;for(const auto& identity:source->materials){const auto id=canonicalAssetId(quaderMaterialAssetId(source->project.id,source->id,identity));auto material=loadMaterialAsset(temporary.root,id);
        if(!identity.embedded){editedId=id;material.name="Edited in Crimson";saveMaterialAsset(temporary.root,material);}options.existingMaterials.emplace(id,std::move(material));}
    require(!editedId.empty(),"Native fixture lost the shared project material.");
    auto preservedSource=*source;preservedSource.readMaterial=[](std::uint32_t)->QuaderSourceMaterial{throw std::runtime_error("Preserved import read missing source material assets.");};
    auto preserved=prepareQuaderImport(preservedSource,options);require(std::none_of(preserved.files.begin(),preserved.files.end(),[](const auto& file){return file.id.ends_with(".crimson-material");}),"Default reimport overwrote existing native materials.");
    require(loadMaterialAsset(temporary.root,editedId).name=="Edited in Crimson","Default reimport discarded a Crimson material edit.");
    auto local=loadMaterialAsset(temporary.root,editedId);local.name="Current Crimson graph";materialOutputNode(local.graph).output.roughness=.123f;canonicalizeMaterialAsset(local);saveMaterialAsset(temporary.root,local);
    const auto localBytes=fileBytes(resolveAssetPath(temporary.root,editedId));
    auto latest=prepareQuaderImport(preservedSource,options);require(fileBytes(resolveAssetPath(temporary.root,editedId))==localBytes&&
        std::none_of(latest.files.begin(),latest.files.end(),[&](const auto& file){return fileAssetId(latest,file)==editedId;}),"Reuse mode overwrote a material edited after the captured material map.");
    bool latestGraph{};for(const auto& model:latest.preparedModels)for(std::size_t slot=0;slot<model->materialIds.size();++slot)if(model->materialIds[slot]==editedId){
        require(model->materials.at(slot).name==local.name,"Worker-prepared model retained a stale captured Crimson material.");latestGraph=true;}
    require(latestGraph,"Native fixture no longer references its shared material.");
    std::string imageId,originalImage;for(const auto& id:materialTextureReferences(local))if(!isEngineAssetId(id)){imageId=id;break;}
    for(const auto& file:result.files)if(fileAssetId(result,file)==imageId)originalImage=*file.contents;
    require(!imageId.empty(),"Native fixture did not normalize a source texture.");
    writeFileAtomically(resolveAssetPath(temporary.root,imageId),"Crimson texture edit");
    require(fs::remove(resolveAssetPath(temporary.root,editedId)),"Cannot remove disposable missing-material fixture.");
    auto missingMaterial=prepareQuaderImport(*source,options);
    require(std::any_of(missingMaterial.files.begin(),missingMaterial.files.end(),[&](const auto& file){return fileAssetId(missingMaterial,file)==editedId;})&&
        std::none_of(missingMaterial.files.begin(),missingMaterial.files.end(),[&](const auto& file){return fileAssetId(missingMaterial,file)==imageId;}),"Reuse mode did not recreate a missing material or overwrote its existing shared image.");
    commit(temporary.root,missingMaterial);require(fileBytes(resolveAssetPath(temporary.root,imageId))=="Crimson texture edit","Missing material publication replaced an existing stable-ID texture in reuse mode.");
    // Sync-on must repair even a malformed destination; no old parse is needed.
    writeFileAtomically(resolveAssetPath(temporary.root,editedId),"invalid Crimson material");
    options.refreshMaterials=true;auto refreshed=prepareQuaderImport(*source,options);require(std::any_of(refreshed.files.begin(),refreshed.files.end(),[&](const auto& file){return fileAssetId(refreshed,file)==editedId;}),"Explicit refresh did not stage the shared material replacement.");
    commit(temporary.root,refreshed);require(loadMaterialAsset(temporary.root,editedId).name=="Project surface","Explicit refresh retained an old authored material name.");
    require(fileBytes(resolveAssetPath(temporary.root,imageId))==originalImage,"Enabled material sync did not restore source pixels at the same texture identity.");
    for(const auto& file:result.files)if(file.id.ends_with(".png")){const auto image=decodeManagedImage(checkedManagedAssetPath(temporary.root,file.id));require(image.width>0&&!image.rgba.empty(),"Normalized source image is unreadable.");}
    const auto count=models(temporary.root);auto missing=readQuaderScene(project,"22222222-2222-4222-8222-222222222222");rejects([&]{prepareQuaderImport(*missing,options);});require(models(temporary.root)==count,"Failed source texture import changed managed geometry.");
}
void sceneScopedContentLayout(){
    Temporary temporary;const auto project=fixture()/"godot-bridge-fixture-v2.qproj";
    auto source=*readQuaderScene(project,"11111111-1111-4111-8111-111111111111");source.relativePath="Scenes/Diablo Room.qdr";
    QuaderPrepareOptions options;options.projectRoot=temporary.root;const auto imported=prepareQuaderImport(source,options);
    const auto bundle=preparedBundle(imported);const auto directory=temporary.root/"Content/diablo_room";
    require(quaderSceneContentDirectory(temporary.root,bundle)==directory,"Quader scene directory did not use the lowercase snake_case scene filename.");
    std::size_t nativeMaterials{},nativeTextures{},bundles{};
    for(const auto& file:imported.files){const auto path=checkedManagedAssetPath(temporary.root,file.id);
        if(path.extension()==".crimson-quader"){
            require(path.parent_path()==directory/"models"&&fileAssetId(imported,file)==bundle.id,"Quader bundle did not publish under the scene's models folder with its stable identity.");++bundles;
        }else if(path.extension()==".crimson-material"||path.extension()==".png"){
            const auto kind=path.extension()==".crimson-material"?"materials":"textures";
            require(path.parent_path()==directory/kind&&fileAssetId(imported,file)==canonicalAssetId("assets/"+std::string(kind)+"/"+utf8(path.filename())),
                "Quader native assets lost their original UUIDs or were written outside their scene's material/texture folders.");
            if(path.extension()==".crimson-material")++nativeMaterials;else ++nativeTextures;
        }else require(file.id.starts_with(".crimson/cache/quader-imports/"),"Quader expanded geometry escaped the private import cache.");
    }
    require(bundles==1&&nativeMaterials>0&&nativeTextures>0,"Scene-layout fixture did not cover bundle, native material and texture publication.");
    commit(temporary.root,imported);
    require(!fs::exists(temporary.root/"Content/models")&&!fs::exists(temporary.root/"Content/materials")&&!fs::exists(temporary.root/"Content/textures"),
        "Fresh Quader import created flat global models/materials/textures folders.");
    for(const auto& identity:source.materials){const auto id=canonicalAssetId(quaderMaterialAssetId(source.project.id,source.id,identity));
        require(resolveAssetPath(temporary.root,id).parent_path()==directory/"materials"&&loadMaterialAsset(temporary.root,id).id==id,
            "Scene-scoped native material publication changed a canonical source identity or failed to register its new location.");}
    const auto index=fileBytes(temporary.root/".crimson/asset-locations.json");const auto revision=contentStorageRevision(temporary.root);
    std::vector<std::pair<fs::path,fs::file_time_type>> times;
    for(const auto& file:imported.files)times.emplace_back(checkedManagedAssetPath(temporary.root,file.id),fs::last_write_time(checkedManagedAssetPath(temporary.root,file.id)));
    const auto repeated=prepareQuaderImport(source,options);require(repeated.files.empty(),"Unchanged scene-scoped import rewrote authored assets or the durable bundle.");commit(temporary.root,repeated);
    require(fileBytes(temporary.root/".crimson/asset-locations.json")==index&&contentStorageRevision(temporary.root)==revision,
        "Repeated scene-scoped import rewrote the location index or advanced its revision.");
    for(const auto& [path,time]:times)require(fs::last_write_time(path)==time,"Repeated scene-scoped import rewrote an equal published file.");
}
void genericSceneNameAdoption(){
    Temporary temporary;const auto project=fixture()/"godot-bridge-fixture-v2.qproj";
    auto source=*readQuaderScene(project,"11111111-1111-4111-8111-111111111111");source.relativePath="Scenes/Quader Scene.qdr";
    QuaderPrepareOptions options;options.projectRoot=temporary.root;const auto original=prepareQuaderImport(source,options);commit(temporary.root,original);
    const auto oldBundle=preparedBundle(original);const auto oldFile=resolveAssetPath(temporary.root,oldBundle.id);
    const auto materialId=canonicalAssetId(quaderMaterialAssetId(source.project.id,source.id,source.materials[0]));
    auto material=loadMaterialAsset(temporary.root,materialId);material.name="Edited before source name adoption";
    materialOutputNode(material.graph).output.roughness=.123f;canonicalizeMaterialAsset(material);saveMaterialAsset(temporary.root,material);
    const auto oldMaterial=resolveAssetPath(temporary.root,materialId);const auto editedBytes=fileBytes(oldMaterial);
    std::vector<std::pair<std::string,std::string>> textures;
    for(const auto& id:materialTextureReferences(material))if(!isEngineAssetId(id))textures.emplace_back(id,fileBytes(resolveAssetPath(temporary.root,id)));
    source.relativePath="Scenes/Diablo Room.qdr";const auto adopted=prepareQuaderImport(source,options);const auto named=preparedBundle(adopted);
    require(named.name=="Diablo Room"&&named.id==oldBundle.id&&named.models.size()==oldBundle.models.size()&&
        adopted.meshes.size()==original.meshes.size()&&
        std::any_of(adopted.files.begin(),adopted.files.end(),[&](const auto& change){return !change.contents&&checkedManagedAssetPath(temporary.root,change.id)==oldFile;}),
        "A generic migrated scene did not adopt its real source name while preserving the source bundle and geometry identities.");
    for(std::size_t i=0;i<adopted.meshes.size();++i)require(adopted.meshes[i].modelId==original.meshes[i].modelId,"Adopting a real scene name regenerated unchanged source geometry.");
    commit(temporary.root,adopted);const auto directory=temporary.root/"Content/diablo_room";
    require(resolveAssetPath(temporary.root,named.id).parent_path()==directory/"models"&&!fs::exists(oldFile)&&!fs::exists(oldMaterial)&&
        resolveAssetPath(temporary.root,materialId).parent_path()==directory/"materials"&&fileBytes(resolveAssetPath(temporary.root,materialId))==editedBytes,
        "Adopting the real source name lost Crimson material edits or left the native material/bundle at the generic path.");
    for(const auto& [id,pixels]:textures)require(resolveAssetPath(temporary.root,id).parent_path()==directory/"textures"&&fileBytes(resolveAssetPath(temporary.root,id))==pixels,
        "Adopting a real scene name lost shared texture UUIDs or changed their authored pixels.");
    const auto index=fileBytes(temporary.root/".crimson/asset-locations.json");const auto revision=contentStorageRevision(temporary.root);
    const auto repeated=prepareQuaderImport(source,options);require(repeated.files.empty(),"A generic scene-name conversion repeated after its first import.");commit(temporary.root,repeated);
    require(fileBytes(temporary.root/".crimson/asset-locations.json")==index&&contentStorageRevision(temporary.root)==revision,
        "Warm import of an adopted scene changed storage registrations.");
    // Refresh writes and relocation must describe the same final file version.
    Temporary refresh;options.projectRoot=refresh.root;source.relativePath="Scenes/Quader Scene.qdr";
    const auto seed=prepareQuaderImport(source,options);commit(refresh.root,seed);source.relativePath="Scenes/Diablo Room.qdr";options.refreshMaterials=true;
    const auto refreshed=prepareQuaderImport(source,options);commit(refresh.root,refreshed);
    require(resolveAssetPath(refresh.root,materialId).parent_path()==refresh.root/"Content/diablo_room/materials"&&
        loadMaterialAsset(refresh.root,materialId).name==source.readMaterial(0).name,
        "Source-name adoption dropped or published a refresh to the former material path.");
    // An explicit authored location is never a generic automatic-folder claim.
    Temporary custom;options.projectRoot=custom.root;options.refreshMaterials=false;source.relativePath="Scenes/Quader Scene.qdr";
    const auto initial=prepareQuaderImport(source,options);commit(custom.root,initial);
    const auto customBundle=custom.root/"Content/Custom/Bundle.crimson-quader",customMaterial=custom.root/"Content/Custom/Material.crimson-material";
    const std::array<ContentPathMove,2> moves{{{resolveAssetPath(custom.root,oldBundle.id),customBundle,false},{resolveAssetPath(custom.root,materialId),customMaterial,false}}};
    relocateContentPaths(custom.root,moves);source.relativePath="Scenes/Diablo Room.qdr";const auto unchanged=prepareQuaderImport(source,options);
    require(unchanged.files.empty()&&resolveAssetPath(custom.root,oldBundle.id)==customBundle&&resolveAssetPath(custom.root,materialId)==customMaterial,
        "A real source name reversed manually authored bundle or native-material locations.");
}
void renamedSceneContentFolders(){
    Temporary temporary;auto source=*readQuaderScene(fixture()/"godot-bridge-fixture-v2.qproj","11111111-1111-4111-8111-111111111111");
    source.relativePath="Scenes/Diablo Room.qdr";QuaderPrepareOptions options;options.projectRoot=temporary.root;
    const auto original=prepareQuaderImport(source,options);commit(temporary.root,original);const auto owner=preparedBundle(original);
    const auto originalDirectory=temporary.root/"Content/diablo_room",directory=temporary.root/"Content/My Renamed Level";
    const ContentPathMove rootMove{originalDirectory,directory,false};relocateContentPaths(temporary.root,std::span(&rootMove,1));
    const std::array<ContentPathMove,3> roles{{{directory/"models",directory/"geometry",false},{directory/"materials",directory/"surfaces",false},{directory/"textures",directory/"images",false}}};
    relocateContentPaths(temporary.root,roles);
    const auto bundlePath=resolveAssetPath(temporary.root,owner.id);
    std::vector<std::pair<std::string,std::string>> existing;
    for(const auto& identity:source.materials){const auto id=canonicalAssetId(quaderMaterialAssetId(source.project.id,source.id,identity));existing.emplace_back(id,fileBytes(resolveAssetPath(temporary.root,id)));}
    const auto reader=source.readMaterial;auto added=reader(0);
    for(std::uint32_t index=0;index<source.materials.size();++index){auto candidate=reader(index);
        if(std::any_of(candidate.textures.begin(),candidate.textures.end(),[](const auto& texture){return texture.present;})){added=std::move(candidate);break;}}
    require(std::any_of(added.textures.begin(),added.textures.end(),[](const auto& texture){return texture.present;}),"Renamed-folder fixture has no textured source material.");
    added.identity.id="aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaab";added.identity.embedded=false;added.name="New room surface";
    for(auto& texture:added.textures)if(texture.present)texture.id+="-added-after-folder-rename";
    const auto addedIndex=static_cast<std::uint32_t>(source.materials.size());source.materials.push_back(added.identity);
    source.readMaterial=[reader,added,addedIndex](std::uint32_t index){return index==addedIndex?added:reader(index);};
    require(!source.meshes.empty()&&!source.meshes[0].surfaces.empty(),"Renamed-folder fixture has no material-bearing geometry.");source.meshes[0].surfaces[0].material=addedIndex;
    const auto imported=prepareQuaderImport(source,options);
    std::size_t addedMaterials{},addedTextures{};
    for(const auto& file:imported.files)if(file.contents){const auto path=checkedManagedAssetPath(temporary.root,file.id);
        if(path.extension()==".crimson-material"){require(path.parent_path()==directory/"surfaces","New source material ignored the renamed role folder.");++addedMaterials;}
        else if(path.extension()==".png"){require(path.parent_path()==directory/"images","New source texture ignored the renamed role folder.");++addedTextures;}
        else if(path.extension()==".crimson-quader")require(path==bundlePath,"Geometry changes recreated an old scene/model folder after a rename.");
    }
    require(addedMaterials==1&&addedTextures>0,"Renamed-folder fixture did not publish new source material/texture assets.");commit(temporary.root,imported);
    require(resolveAssetPath(temporary.root,owner.id)==bundlePath&&!fs::exists(originalDirectory)&&!fs::exists(directory/"models")&&
        !fs::exists(directory/"materials")&&!fs::exists(directory/"textures"),"Reimport recreated scene or role folders that the user renamed.");
    for(const auto& [id,data]:existing)require(resolveAssetPath(temporary.root,id).parent_path()==directory/"surfaces"&&fileBytes(resolveAssetPath(temporary.root,id))==data,
        "Reimport after folder renames changed existing source asset UUIDs, authored material bytes or assigned locations.");
    const auto repeated=prepareQuaderImport(source,options);require(repeated.files.empty(),"A repeated import into renamed scene/role folders rewrote equal data.");
}
void deletedMaterialsAndTexturesReimport(){
    Temporary temporary;const auto project=fixture()/"godot-bridge-fixture-v2.qproj";
    const auto source=readQuaderScene(project,"11111111-1111-4111-8111-111111111111");
    QuaderPrepareOptions options;options.projectRoot=temporary.root;
    const auto original=prepareQuaderImport(*source,options);commit(temporary.root,original);
    AssetReferenceManager references(temporary.root);std::vector<AssetEntry> deleted;std::vector<AssetFileChange> changes;
    std::vector<std::string> originalMaterialIds,originalImageIds;
    for(const auto& file:original.files){
        if(file.id.ends_with(".crimson-material")){deleted.push_back({fileAssetId(original,file),"Deleted Quader material",AssetKind::Material});originalMaterialIds.push_back(fileAssetId(original,file));}
        else if(file.id.ends_with(".png")){deleted.push_back({fileAssetId(original,file),"Deleted Quader image",AssetKind::Texture});originalImageIds.push_back(fileAssetId(original,file));}
        else continue;
        options.reservedIds.insert(fileAssetId(original,file));changes.push_back({file.id,std::nullopt});
    }
    require(!originalMaterialIds.empty()&&!originalImageIds.empty(),"Native Quader fixture must contain shared materials and normalized textures.");
    references.replace(deleted,{});changes.push_back({"assets/references.json",references.serialize()});commit(temporary.root,changes);
    for(const auto& identity:source->materials){const auto id=quaderMaterialAssetId(source->project.id,source->id,identity);options.materialOverrides.emplace(id,references.resolveModelMaterial(id));}
    for(const auto& id:originalImageIds)options.imageOverrides.emplace(id,std::array{references.resolveTexture(id,false),references.resolveTexture(id,true)});
    const auto referenceBytes=fileBytes(resolveAssetPath(temporary.root,"assets/references.json"));
    const auto replacement=prepareQuaderImport(*source,options);std::vector<std::string> freshMaterialIds,freshImageIds;
    for(const auto& file:replacement.files){
        if(file.id.ends_with(".crimson-material"))freshMaterialIds.push_back(fileAssetId(replacement,file));
        else if(file.id.ends_with(".png")){
            freshImageIds.push_back(fileAssetId(replacement,file));
            require(std::any_of(original.files.begin(),original.files.end(),[&](const auto& before){return before.id.ends_with(".png")&&before.contents==file.contents;}),
                "Reimporting deleted Quader textures changed normalized source pixels.");
        }else continue;
        require(!options.reservedIds.contains(fileAssetId(replacement,file)),"Quader reimport reused a deleted material or texture identity.");
    }
    require(freshMaterialIds.size()==originalMaterialIds.size()&&freshImageIds.size()==originalImageIds.size(),
        "Reimporting deleted Quader assets kept fallback gray materials or omitted source textures.");
    commit(temporary.root,replacement);
    for(const auto& id:freshMaterialIds){
        const auto material=loadMaterialAsset(temporary.root,id);require(material.id==canonicalAssetId(id)&&material.originModel.empty(),
            "Reimported Quader material is not an editable native material.");
        for(const auto& image:materialTextureReferences(material))require(!options.reservedIds.contains(image),
            "Fresh Quader material retained a deleted image reference.");
    }
    std::unordered_map<std::string,std::string> editedMaterials;
    for(std::size_t index=0;index<freshMaterialIds.size();++index){auto material=loadMaterialAsset(temporary.root,freshMaterialIds[index]);
        material.name="Crimson replacement edit "+std::to_string(index);materialOutputNode(material.graph).output.roughness=.123f;
        canonicalizeMaterialAsset(material);saveMaterialAsset(temporary.root,material);
        editedMaterials.emplace(material.id,fileBytes(resolveAssetPath(temporary.root,material.id)));
    }
    const auto refreshedImage=freshImageIds.front();const auto sourcePixels=fileBytes(resolveAssetPath(temporary.root,refreshedImage));
    writeFileAtomically(resolveAssetPath(temporary.root,refreshedImage),"Crimson replacement texture edit");
    auto preservedSource=*source;preservedSource.readMaterial=[](std::uint32_t)->QuaderSourceMaterial{throw std::runtime_error("Reuse mode unnecessarily requested deleted Quader source material data.");};
    const auto preserved=prepareQuaderImport(preservedSource,options);
    require(preserved.files.empty(),"Disabled material sync did not reuse the fresh Quader material/image/package identities.");
    for(std::size_t index=0;index<replacement.meshes.size();++index)require(preserved.meshes[index].modelId==replacement.meshes[index].modelId&&
        bool(preserved.meshes[index].collision)==bool(replacement.meshes[index].collision)&&
        (!preserved.meshes[index].collision||preserved.meshes[index].collision->fingerprint==replacement.meshes[index].collision->fingerprint),
        "Material reuse changed fresh Quader package identities or native collision geometry.");
    for(const auto& [id,contents]:editedMaterials)require(fileBytes(resolveAssetPath(temporary.root,id))==contents,
        "Disabled material sync discarded edits to a replacement Quader material.");
    require(fileBytes(resolveAssetPath(temporary.root,refreshedImage))=="Crimson replacement texture edit",
        "Disabled material sync discarded an edit to a replacement Quader texture.");
    options.refreshMaterials=true;const auto refreshed=prepareQuaderImport(*source,options);
    for(const auto& id:freshMaterialIds)require(std::any_of(refreshed.files.begin(),refreshed.files.end(),[&](const auto& file){return fileAssetId(refreshed,file)==id;}),
        "Enabled material sync did not refresh the replacement material under its existing identity.");
    commit(temporary.root,refreshed);
    require(fileBytes(resolveAssetPath(temporary.root,refreshedImage))==sourcePixels,"Enabled material sync did not restore replacement texture source pixels.");
    for(const auto& [id,contents]:editedMaterials)require(fileBytes(resolveAssetPath(temporary.root,id))!=contents,
        "Enabled material sync retained an edited replacement material.");
    require(fileBytes(resolveAssetPath(temporary.root,"assets/references.json"))==referenceBytes,"Importing replacement materials/textures modified old deletion policy.");
    AssetReferenceManager retained(temporary.root);
    for(const auto& asset:deleted)require(retained.deleted(asset.id)&&!fs::exists(resolveAssetPath(temporary.root,asset.id)),
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
    const auto sharedMaterial=canonicalAssetId(quaderMaterialAssetId(source->project.id,source->id,*identity));
    require(std::any_of(prepared.preparedModels.begin(),prepared.preparedModels.end(),[&](const auto& model){
        return std::find(model->materialIds.begin(),model->materialIds.end(),sharedMaterial)!=model->materialIds.end();}),
        "Modern scene models lost their shared editable material assignment.");
    commit(destination,prepared);bool decodedTexture=false;
    for(const auto& file:prepared.files)if(file.id.ends_with(".png")){
        const auto image=decodeManagedImage(checkedManagedAssetPath(destination,file.id));decodedTexture|=image.width>0&&image.height>0&&!image.rgba.empty();}
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
        std::uint64_t sourceVertices{},sourceTriangles{},sourceSurfaces{},sourceCircularVertices{},sourceQuadVertices{};
        for(const auto& mesh:source->meshes){
            require(!mesh.vertices.empty()&&!mesh.indices.empty()&&mesh.indices.size()%3==0,
                "Source mesh has no complete triangles: "+mesh.name);
            for(const auto index:mesh.indices)require(index<mesh.vertices.size(),"Source mesh index exceeds its vertex array: "+mesh.name);
            for(const auto& vertex:mesh.vertices)require(math::finite(vertex.position)&&math::finite(vertex.normal)&&math::finite(vertex.uv),
                "Source mesh contains a nonfinite vertex: "+mesh.name);
            for(const auto& surface:mesh.surfaces)require(surface.count%3==0&&std::uint64_t(surface.offset)+surface.count<=mesh.indices.size(),
                "Source mesh surface exceeds its triangle array: "+mesh.name);
            sourceVertices+=mesh.vertices.size();sourceTriangles+=mesh.indices.size()/3;sourceSurfaces+=mesh.surfaces.size();
            const auto circular=std::count_if(mesh.vertices.begin(),mesh.vertices.end(),[](const auto& vertex){return vertex.uvInterpolationMode==1;});
            const auto quad=std::count_if(mesh.vertices.begin(),mesh.vertices.end(),[](const auto& vertex){return vertex.uvInterpolationMode==2;});
            sourceCircularVertices+=circular;sourceQuadVertices+=quad;
            std::cout<<"Mesh "<<std::quoted(mesh.name)<<": vertices="<<mesh.vertices.size()<<", triangles="<<mesh.indices.size()/3
                <<", surfaces="<<mesh.surfaces.size()<<", circular vertices="<<circular<<", quad vertices="<<quad<<", visible="<<(mesh.visible?"yes":"no")<<'\n';
        }
        QuaderPrepareOptions options;options.projectRoot=temporary.root;
        const auto imported=prepareQuaderImport(*source,options);
        require(imported.meshes.size()==source->meshes.size()&&!imported.preparedModels.empty(),
            "Preparing the selected scene lost mesh occurrences or model packages.");
        commit(temporary.root,imported);
        ModelResolvers resolvers;
        resolvers.material=[&](const std::string& id)->std::optional<ModelMaterial>{
            if(id==DefaultMaterialId)return defaultMaterialAsset().surface;
            return loadMaterialAsset(temporary.root,id).surface;
        };
        resolvers.image=[&](const std::string& id){
            require(isEngineAssetId(id)||fs::is_regular_file(resolveAssetPath(temporary.root,id)),
                "Published model references a missing managed texture: "+id);
            return id;
        };
        std::size_t modelCount{},materialCount{},textureCount{},modelCircularVertices{},modelQuadVertices{};
        for(const auto& id:imported.assetIds){
            const auto file=resolveAssetPath(temporary.root,id);
            if(file.filename()=="manifest.json"){
                const auto model=loadModelPackage(resolveAssetPath(temporary.root,id),temporary.root,resolvers);
                require(model&&!model->primitives.empty(),"Published model package contains no primitives: "+id);
                std::uint64_t vertices{},triangles{};
                for(const auto& primitive:model->primitives){
                    require(!primitive.vertices.empty()&&!primitive.indices.empty()&&primitive.indices.size()%3==0,
                        "Published model primitive contains no complete triangles: "+id);
                    for(const auto index:primitive.indices)require(index<primitive.vertices.size(),"Published model index exceeds its vertex array: "+id);
                    for(const auto& vertex:primitive.vertices)require(math::finite(vertex.position)&&math::finite(vertex.normal),
                        "Published model contains a nonfinite vertex: "+id);
                    vertices+=primitive.vertices.size();triangles+=primitive.indices.size()/3;
                    modelCircularVertices+=std::count_if(primitive.vertices.begin(),primitive.vertices.end(),[](const auto& vertex){return vertex.uvInterpolationMode==1;});
                    modelQuadVertices+=std::count_if(primitive.vertices.begin(),primitive.vertices.end(),[](const auto& vertex){return vertex.uvInterpolationMode==2;});
                }
                ++modelCount;
                std::cout<<"Published model "<<std::quoted(model->name)<<": vertices="<<vertices<<", triangles="<<triangles
                    <<", surfaces="<<model->primitives.size()<<", id="<<id<<'\n';
            }else if(file.extension()==".crimson-material"){
                const auto material=loadMaterialAsset(temporary.root,id);validateMaterialAsset(material);++materialCount;
            }else if(file.extension()==".png"){
                const auto image=decodeManagedImage(resolveAssetPath(temporary.root,id));
                require(image.width&&image.height&&!image.rgba.empty(),"Published texture failed decoding: "+id);++textureCount;
            }
        }
        std::set<std::string> preparedIds;for(const auto& model:imported.preparedModels){
            require(bool(model),"Preparation returned an empty model package.");preparedIds.insert(model->id);
        }
        require(modelCount==preparedIds.size()&&modelCount==models(temporary.root),
            "Diagnostic failed to reload every published visual model package.");
        require((sourceCircularVertices>0)==(modelCircularVertices>0),"Published packages lost native circular UV interpolation.");
        require((sourceQuadVertices>0)==(modelQuadVertices>0),"Published packages lost native quad UV interpolation.");
        std::cout<<"Source totals: meshes="<<source->meshes.size()<<", vertices="<<sourceVertices<<", triangles="<<sourceTriangles
            <<", surfaces="<<sourceSurfaces<<"\nImported totals: models="<<modelCount<<", materials="<<materialCount
            <<", textures="<<textureCount<<", lights="<<imported.lights.size()<<", skipped lights="<<imported.skippedLights
            <<", skipped spots="<<imported.skippedSpots<<"\nCircular UV vertices: source="<<sourceCircularVertices<<", reloaded="<<modelCircularVertices
            <<"\nQuad UV vertices: source="<<sourceQuadVertices<<", reloaded="<<modelQuadVertices<<"\n";
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
    const auto initialRevision=contentStorageRevision(destination);
    bridge.requestImport(7,draft,1);pump([&]{return !bridge.busy(7);});require(published==1&&assets==1,"Initial draft import was rejected before the source link was committed: "+bridge.status(7));
    require(contentStorageRevision(destination)==initialRevision+1,"Initial Quader import published its locations once per file instead of once per group.");
    const auto count=models(destination);dirty=true;auto refresh=targets[0].link;refresh.refreshMaterials=true;refresh.importScale=2;bridge.requestImport(7,refresh,1);
    pump([&]{return bridge.status(7).find("waits")!=std::string::npos;});require(published==1&&models(destination)==count,"Dirty material refresh partially published geometry/assets.");
    dirty=false;pump([&]{return !bridge.busy(7);});require(published==2,"Material refresh did not resume after the dirty document settled.");near(targets[0].link.importScale,2);
    auto earlier=targets[0].link;earlier.importScale=3;bridge.requestImport(7,earlier,1);bridge.update(targets,1,false,hooks);auto later=earlier;later.importScale=4;bridge.requestImport(7,later,1);
    pump([&]{return !bridge.busy(7);});require(published==3,"Superseded import published an extra scene transaction.");near(targets[0].link.importScale,4);
    // A failed scene transaction restores every external material/image/model file.
    const auto rollbackRoot=temporary.root/"rollback";fs::create_directories(rollbackRoot);QuaderBridge rollback(rollbackRoot);auto failing=hooks;
    std::vector<std::string> rejectedIds;failing.applyImport=[&](EntityId,const QuaderLink&,const QuaderImportResult& result){
        for(const auto& location:result.locations)rejectedIds.push_back(location.id);
        for(const auto& file:result.files)if(file.contents&&file.id.starts_with("assets/"))rejectedIds.push_back(canonicalAssetId(file.id));return false;};
    std::vector<QuaderTarget> rollbackTargets{{7,{},false,{}}};
    const auto rollbackIndex=fileBytes(rollbackRoot/".crimson/asset-locations.json");const auto rollbackLocations=locations(rollbackRoot);
    rollback.requestImport(7,draft,1);const auto end=std::chrono::steady_clock::now()+std::chrono::seconds{12};while(rollback.busy(7)&&std::chrono::steady_clock::now()<end){rollback.update(rollbackTargets,1,false,failing);std::this_thread::sleep_for(std::chrono::milliseconds{1});}
    require(!rollback.busy(7)&&models(rollbackRoot)==0,"Rejected scene transaction left imported model files published.");
    for(const auto& entry:fs::recursive_directory_iterator(rollbackRoot/"Content"))
        require(!entry.is_regular_file(),"Rejected scene transaction retained a scene-scoped bundle, material or texture file.");
    require(fileBytes(rollbackRoot/".crimson/asset-locations.json")==rollbackIndex,"Rejected scene transaction retained newly registered asset locations.");
    require(!rejectedIds.empty()&&locations(rollbackRoot)==rollbackLocations,"Rejected scene transaction restored index bytes but retained newly registered cached locations.");
    for(const auto& id:rejectedIds)rejects([&]{resolveAssetPath(rollbackRoot,id);});
    auto retry=hooks;std::size_t retried{};retry.applyImport=[&](EntityId,const QuaderLink& link,const QuaderImportResult& result){
        rollbackTargets[0].link=link;rollbackTargets[0].hasImported=true;rollbackTargets[0].consumedSaveKey=result.sourceKey;++retried;return true;};
    const auto retryRevision=contentStorageRevision(rollbackRoot);rollback.requestImport(7,draft,1);
    const auto retryEnd=std::chrono::steady_clock::now()+std::chrono::seconds{12};while(rollback.busy(7)&&std::chrono::steady_clock::now()<retryEnd){rollback.update(rollbackTargets,1,false,retry);std::this_thread::sleep_for(std::chrono::milliseconds{1});}
    require(!rollback.busy(7)&&retried==1&&models(rollbackRoot)>0&&contentStorageRevision(rollbackRoot)==retryRevision+1,
        "Retry after rejected scene publication did not publish one complete index group.");
    for(const auto& id:rejectedIds)require(fs::is_regular_file(resolveAssetPath(rollbackRoot,id)),"Retried publication retained an unresolved or missing package member.");
    // A completed source save refreshes an imported link; Undo consumption survives.
    auto native=readQuaderScene(pathFromUtf8(draft.projectPath),draft.sceneId);const std::string token="aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
    writeFileAtomically(sourceRoot/".quader/bridge/scenes"/pathFromUtf8(native->id+".json"),record(native->project.id,native->id,native->relativePath,native->fingerprint,token).dump());
    for(std::uint32_t i=0;i<native->materials.size();++i)if(!native->materials[i].embedded){const auto material=native->readMaterial(i);writeFileAtomically(sourceRoot/".quader/bridge/materials"/pathFromUtf8(material.identity.id+".json"),record(native->project.id,material.identity.id,material.relativePath,material.fingerprint,token).dump());}
    targets[0].link.autoSync=true;const auto before=published;pump([&]{return published>before;},8000);require(published==before+1,"Completed source save did not produce one scene publication.");
    for(int i=0;i<30;++i)bridge.update(targets,1,false,hooks);require(published==before+1&&!bridge.busy(7),"Consumed Quader save immediately replayed after publication.");
}
void topologyUpgradeAndPaintInputGuards(){
    Temporary temporary;const auto sourceRoot=temporary.root/"godot-bridge-fixture-v2",destination=temporary.root/"engine";
    fs::copy(fixture(),sourceRoot,fs::copy_options::recursive);fs::create_directories(destination);
    QuaderLink link;link.projectPath=utf8(sourceRoot/"godot-bridge-fixture-v2.qproj");link.sceneId="11111111-1111-4111-8111-111111111111";link.autoSync=true;
    const auto source=readQuaderScene(pathFromUtf8(link.projectPath),link.sceneId);link.projectId=source->project.id;
    const std::string token="aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
    writeFileAtomically(sourceRoot/".quader/bridge/scenes"/pathFromUtf8(source->id+".json"),record(source->project.id,source->id,source->relativePath,source->fingerprint,token).dump());
    for(std::uint32_t i=0;i<source->materials.size();++i)if(!source->materials[i].embedded){const auto material=source->readMaterial(i);
        writeFileAtomically(sourceRoot/".quader/bridge/materials"/pathFromUtf8(material.identity.id+".json"),record(source->project.id,material.identity.id,material.relativePath,material.fingerprint,token).dump());}
    const auto completed=readQuaderCompletedSave(pathFromUtf8(link.projectPath),link.projectId,link.sceneId,true);
    const auto currentKey=quaderSaveKey(completed,link.importScale,link.refreshMaterials,link.syncLights);
    require(currentKey.starts_with("quader:v4:"),"Topology upgrade test requires the current save key.");
    link.saveKey="quader:v3:"+currentKey.substr(10);
    std::vector<QuaderTarget> targets{{7,link,true,link.saveKey}};QuaderBridge bridge(destination);
    std::size_t captures{},checks{},published{},notifications{};QuaderBridgeHooks hooks;
    hooks.resolveMaterial=[](const auto& id){return id;};hooks.reservedAsset=[](const auto&){return false;};
    hooks.capturePaintInputs=[&](EntityId entity)->std::optional<std::vector<QuaderPaintInput>>{require(entity==7,"Paint capture changed its import target.");++captures;return std::vector<QuaderPaintInput>{};};
    hooks.paintInputsCurrent=[&](EntityId entity,const QuaderImportResult& result){require(entity==7&&!result.files.empty(),"Paint guard was not checked before a new package publication.");++checks;return checks>1;};
    hooks.applyImport=[&](EntityId entity,const QuaderLink& applied,const QuaderImportResult& result){
        require(entity==7&&result.topologyOnly,"A consumed v3 save was not recognized as a topology-only upgrade.");
        require(captures==2&&checks==2,"Changed paint inputs were not recaptured and prepared exactly once.");
        targets[0].link=applied;targets[0].link.saveKey=result.sourceKey;targets[0].consumedSaveKey=result.sourceKey;++published;return true;};
    hooks.publishAssets=[&](const QuaderImportResult&){++notifications;};
    const auto initialRevision=contentStorageRevision(destination);const auto initialIndex=fileBytes(destination/".crimson/asset-locations.json");
    const auto initialLocations=locations(destination);
    const auto pump=[&](auto predicate){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds{12};
        while(!predicate()&&std::chrono::steady_clock::now()<end){bridge.update(targets,1,false,hooks);std::this_thread::sleep_for(std::chrono::milliseconds{1});}
        require(predicate(),"Topology upgrade did not complete: "+bridge.status(7));};
    pump([&]{return checks==1;});
    require(captures==1&&published==0&&notifications==0&&models(destination)==0&&contentStorageRevision(destination)==initialRevision&&
        fileBytes(destination/".crimson/asset-locations.json")==initialIndex&&locations(destination)==initialLocations,
        "Stale paint preparation partially published package files, locations or scene changes.");
    pump([&]{return published==1&&!bridge.busy(7);});
    require(targets[0].link.saveKey==currentKey&&notifications==1&&models(destination)>0,"Topology upgrade did not publish the current key and complete packages.");
    for(unsigned i=0;i<30;++i)bridge.update(targets,1,false,hooks);
    require(published==1&&notifications==1&&captures==2&&!bridge.busy(7),"Consumed topology upgrade immediately replayed.");
}
void materialSyncTransactions(){
    Temporary temporary;const auto sourceRoot=temporary.root/"godot-bridge-fixture-v2",destination=temporary.root/"engine";
    fs::copy(fixture(),sourceRoot,fs::copy_options::recursive);fs::create_directories(destination);
    QuaderLink draft;draft.projectPath=utf8(sourceRoot/"godot-bridge-fixture-v2.qproj");draft.sceneId="11111111-1111-4111-8111-111111111111";draft.autoSync=false;
    const auto source=readQuaderScene(pathFromUtf8(draft.projectPath),draft.sceneId);
    std::string sharedId,embeddedId,sourceName;
    for(std::uint32_t index=0;index<source->materials.size();++index){const auto& identity=source->materials[index];const auto id=canonicalAssetId(quaderMaterialAssetId(source->project.id,source->id,identity));
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
    require(fs::remove(resolveAssetPath(destination,embeddedId)),"Cannot remove disposable embedded material.");
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

    std::string imageId;for(const auto& id:materialTextureReferences(loadMaterialAsset(destination,sharedId)))if(!isEngineAssetId(id)){imageId=id;break;}
    require(!imageId.empty(),"Shared fixture material lost its source texture.");const auto sourcePixels=fileBytes(resolveAssetPath(destination,imageId));
    writeFileAtomically(resolveAssetPath(destination,imageId),"Local texture pixels");const auto priorMaterial=fileBytes(resolveAssetPath(destination,sharedId));const auto priorModels=models(destination);
    targets[0].link.refreshMaterials=true;auto replacement=targets[0].link;replacement.importScale=2;QuaderBridge rejected(destination);auto accepting=hooks.applyImport;
    const auto priorIndex=fileBytes(destination/".crimson/asset-locations.json");const auto priorLocations=locations(destination);
    hooks.applyImport=[](EntityId,const QuaderLink&,const QuaderImportResult&){return false;};const auto priorNotifications=notifications;
    rejected.requestImport(7,replacement,1);run(rejected,[&]{return !rejected.busy(7);});
    require(fileBytes(resolveAssetPath(destination,sharedId))==priorMaterial&&fileBytes(resolveAssetPath(destination,imageId))=="Local texture pixels"&&models(destination)==priorModels&&notifications==priorNotifications,
        "Rejected scene import did not roll back existing shared material/image files and new geometry.");hooks.applyImport=std::move(accepting);
    require(fileBytes(destination/".crimson/asset-locations.json")==priorIndex,"Rejected material synchronization retained replacement geometry locations.");
    require(locations(destination)==priorLocations,"Rejected material synchronization retained replacement geometry in its cached location snapshot.");
    // The coordinator must not parse the old destination before replacing it.
    writeFileAtomically(resolveAssetPath(destination,sharedId),"invalid old material");expectedName=sourceName;
    bridge.requestImport(7,targets[0].link,1);pump([&]{return !bridge.busy(7);});
    require(loadMaterialAsset(destination,sharedId).name==sourceName&&fileBytes(resolveAssetPath(destination,imageId))==sourcePixels,"Enabled material sync did not overwrite the graph and same-ID texture from Quader.");

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
    geometryAndLights();uvInterpolationPublication();sourceHierarchyPlacement();currentSourceMeshSet();geometryReuseIgnoresNamesAndPlacement();authoredMeshOwnership();contentOnlyOwnershipMigration();legacyUnusedFallbackPackages();deletedGeometryReimport();reservedGeometryMembersAndCorruption();clusteredPointLightCapacity();completionRecords();nativeReaderHierarchy();nativeReaderHierarchy(fixture().parent_path()/"schema20/hierarchy");nativeReaderMaterials();sceneScopedContentLayout();genericSceneNameAdoption();renamedSceneContentFolders();deletedMaterialsAndTexturesReimport();modernProjectManifest();coordinatorTransactions();topologyUpgradeAndPaintInputGuards();materialSyncTransactions();std::cout<<"Quader reader, import, hierarchy, source mesh ownership, current mesh set, collision, deletion/reimport, material, completed-save and publication checks passed.\n";return 0;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
