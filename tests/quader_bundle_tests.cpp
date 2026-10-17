#include "assets/QuaderSceneBundle.h"
#include "assets/AtomicFile.h"
#include "resources/AssetId.h"
#include <nlohmann/json.hpp>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace crimson;
namespace fs=std::filesystem;
using Json=nlohmann::json;
constexpr std::array<const char*,4> Members{"manifest.json","model.gltf","source.gltf","buffers/0.bin"};
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class F>void rejects(F&& action,const char* message){bool failed{};try{action();}catch(const std::exception&){failed=true;}require(failed,message);}
std::string read(const fs::path& file){std::ifstream input(file,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};}
struct Fixture {
    fs::path root=fs::temp_directory_path()/("crimson-quader-bundle-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Fixture(){fs::create_directories(root);initializeContentStorage(root);}
    ~Fixture(){std::error_code error;const auto target=fs::weakly_canonical(root,error),base=fs::weakly_canonical(fs::temp_directory_path(),error);if(!error&&target.parent_path()==base&&target.filename().string().starts_with("crimson-quader-bundle-"))fs::remove_all(target,error);}
    void clearCache(){const auto target=fs::weakly_canonical(root/".crimson/cache"),owner=fs::weakly_canonical(root);require(target.parent_path()==owner/".crimson","Unsafe test cache cleanup.");fs::remove_all(target);}
};
QuaderSceneBundle bundle(std::string scene="scene-one") {
    QuaderSceneBundle value;value.projectId="project-one";value.sceneId=std::move(scene);value.id=quaderSceneBundleId(value.projectId,value.sceneId);value.name="Dungeon";return value;
}
QuaderBundleModel model(const QuaderSceneBundle& owner,const std::string& token) {
    QuaderBundleModel value;value.legacyManifest="assets/models/quader-"+token+"/manifest.json";
    const auto directory=fs::path(value.legacyManifest).parent_path();for(std::size_t i=0;i<Members.size();++i)value.memberIds[i]=canonicalAssetId((directory/Members[i]).generic_string());value.id=value.memberIds[0];
    value.buffer=std::string{"\0\1\2\xff\0",5};value.buffer+=token;
    value.gltf=Json{{"asset",{{"version","2.0"}}},{"buffers",Json::array({{{"uri","buffers/0.bin"},{"byteLength",value.buffer.size()}}})}}.dump();
    value.manifest=Json{{"version",1},{"name",token},{"format","Quader"},{"document","model.gltf"},{"original","source.gltf"},{"quader",{{"project",owner.projectId},{"scene",owner.sceneId},{"collision",false}}},{"materialAssets",Json::array({canonicalAssetId("assets/materials/authored.crimson-material"),canonicalAssetId("builtin:material/default")})}}.dump(2)+"\n";
    return value;
}
fs::path legacy(Fixture& fixture,const QuaderBundleModel& value) {
    const auto directory=fixture.root/"Content/models"/fs::path(value.legacyManifest).parent_path().filename();
    const std::array<const std::string*,4> values{&value.manifest,&value.gltf,&value.gltf,&value.buffer};std::vector<StoredAssetPath> locations;
    for(std::size_t i=0;i<Members.size();++i){writeFileAtomically(directory/Members[i],*values[i]);locations.push_back({(fs::path(value.legacyManifest).parent_path()/Members[i]).generic_string(),directory/Members[i]});}
    registerAssetPaths(fixture.root,locations);return directory;
}
void codecValidation() {
    auto original=bundle();original.models.push_back(model(original,"first"));original.models.push_back(model(original,"second"));const auto encoded=encodeQuaderSceneBundle(original);const auto decoded=decodeQuaderSceneBundle(encoded);
    require(decoded.id==original.id&&decoded.models.size()==2&&decoded.models[1].buffer==original.models[1].buffer&&decoded.models[0].manifest==original.models[0].manifest&&encodeQuaderSceneBundle(decoded)==encoded,"Binary scene bundle did not round trip exact geometry or material metadata.");
    require(encoded.find(original.models[0].buffer)!=std::string::npos,"Binary geometry was not stored as raw payload.");
    auto changed=encoded;changed.back()^=1;rejects([&]{decodeQuaderSceneBundle(changed);},"Corrupt binary member passed its checksum.");
    rejects([&]{decodeQuaderSceneBundle(std::string_view(encoded).substr(0,encoded.size()-1));},"Truncated binary bundle was accepted.");
    rejects([&]{decodeQuaderSceneBundle(encoded+"x");},"Unowned trailing bundle bytes were accepted.");
    changed=encoded;changed[12]=static_cast<char>(255);changed[13]=static_cast<char>(255);rejects([&]{decodeQuaderSceneBundle(changed);},"Invalid metadata length was accepted.");
    auto invalid=original;invalid.models.push_back(invalid.models.front());rejects([&]{encodeQuaderSceneBundle(invalid);},"Duplicate immutable member identities were accepted.");
    invalid=original;invalid.models[0].legacyManifest="assets/models/../outside/manifest.json";rejects([&]{encodeQuaderSceneBundle(invalid);},"Legacy member traversal was accepted.");
    invalid=original;invalid.models[0].buffer+="x";rejects([&]{encodeQuaderSceneBundle(invalid);},"Geometry byte length mismatch was accepted.");
    invalid=original;invalid.sceneId="another scene";rejects([&]{encodeQuaderSceneBundle(invalid);},"A source scene identity mismatch was accepted.");
    Fixture fixture;rejects([&]{quaderBundleCacheDirectory(fixture.root,"../outside",original.models[0].id);},"Cache identity traversal was accepted.");
}
void migrationAndReconstruction() {
    Fixture fixture;auto first=bundle(),second=bundle("scene-two");first.models={model(first,"a"),model(first,"old-generation")};second.models={model(second,"b")};
    const auto a=legacy(fixture,first.models[0]),old=legacy(fixture,first.models[1]),b=legacy(fixture,second.models[0]);
    writeFileAtomically(a/"keep.txt","unrelated authored file");
    const auto scene=fixture.root/"Content/World.crimson-scene",material=fixture.root/"Content/Authored.crimson-material";
    const auto sceneBytes=Json{{"name","World"},{"entities",Json::array({{{"model",first.models[0].id},{"modelMaterials",Json::array({canonicalAssetId("assets/materials/override.crimson-material")})},{"parent","0000000000000010"}},{{"collider",{{"source",first.models[1].id}}},{"parent","0000000000000011"}}})}}.dump(2);
    writeFileAtomically(scene,sceneBytes);writeFileAtomically(material,"authored material bytes");
    initializeContentStorage(fixture.root);
    const auto found=findQuaderSceneBundle(fixture.root,first.projectId,first.sceneId),other=findQuaderSceneBundle(fixture.root,second.projectId,second.sceneId);
    require(found&&other&&found->first!=other->first&&found->second.models.size()==2&&other->second.models.size()==1,"Migration did not create one bundle per source scene with all surviving generations.");
    require(read(a/"keep.txt")=="unrelated authored file"&&!fs::exists(a/"manifest.json")&&!fs::exists(old)&&!fs::exists(b),"Migration lost unrelated files or retained old geometry package folders.");
    require(read(scene)==sceneBytes&&read(material)=="authored material bytes","Migration changed scene hierarchy, assignments, collider references or authored material bytes.");
    const auto locations=quaderBundleCacheLocations(fixture.root,found->second);
    for(const auto& location:locations)require(resolveAssetPath(fixture.root,location.id)==location.file&&fs::is_regular_file(location.file),"An original asset UUID was not retargeted to its expanded cache member.");
    const auto& original=first.models[0];require(legacyAssetId(fixture.root,original.id)==original.legacyManifest&&read(resolveAssetPath(fixture.root,original.memberIds[3]))==original.buffer,"Legacy identity or geometry bytes changed during migration.");
    require(quaderBundleSource(fixture.root,original.id)==std::optional{found->first},"Submodel lookup did not find its tracked scene bundle.");
    const auto index=fixture.root/".crimson/asset-locations.json",cached=resolveAssetPath(fixture.root,original.id);const auto indexBytes=read(index),bundleBytes=read(found->first);const auto indexTime=fs::last_write_time(index),bundleTime=fs::last_write_time(found->first),cacheTime=fs::last_write_time(cached);const auto revision=contentStorageRevision(fixture.root);
    initializeContentStorage(fixture.root);
    require(read(index)==indexBytes&&fs::last_write_time(index)==indexTime&&fs::last_write_time(found->first)==bundleTime&&fs::last_write_time(cached)==cacheTime&&contentStorageRevision(fixture.root)==revision,"Warm startup rewrote bundle/cache/index data.");
    writeFileAtomically(cached,"corrupt derived metadata");initializeContentStorage(fixture.root);require(read(cached)==original.manifest,"Startup did not repair a corrupt disposable cache member.");
    fixture.clearCache();initializeContentStorage(fixture.root);require(read(cached)==original.manifest&&read(resolveAssetPath(fixture.root,original.memberIds[3]))==original.buffer&&read(found->first)==bundleBytes,"A missing cache was not reconstructed from its durable bundle.");
    fixture.clearCache();fs::remove(index);initializeContentStorage(fixture.root);
    require(resolveAssetPath(fixture.root,original.id)==cached&&legacyAssetId(fixture.root,original.id)==original.legacyManifest&&read(cached)==original.manifest&&read(scene)==sceneBytes,"A clean project without its private location index could not recover its embedded subasset identities.");
    auto corrupt=bundleBytes;corrupt.back()^=1;writeFileAtomically(found->first,corrupt);const auto before=read(cached);rejects([&]{initializeContentStorage(fixture.root);},"A corrupt tracked source bundle silently passed startup.");require(read(found->first)==corrupt&&read(cached)==before,"Source corruption was overwritten or partially published.");
}
void recoveryAndTombstones() {
    Fixture fixture;auto owner=bundle();const auto value=model(owner,"recover");const auto source=legacy(fixture,value);const auto index=read(fixture.root/".crimson/asset-locations.json"),browser=read(fixture.root/".crimson/browser.json");
    const auto temporary=fixture.root/".crimson/.staging-quader-interrupted/model.gltf";fs::create_directories(temporary.parent_path());fs::rename(source/"model.gltf",temporary);
    writeFileAtomically(fixture.root/".crimson/content-move.json",Json{{"version",1},{"committed",false},{"indexBefore",index},{"browserBefore",browser},{"moves",Json::array({{{"from",(source/"model.gltf").lexically_relative(fixture.root).generic_string()},{"to",temporary.lexically_relative(fixture.root).generic_string()},{"copy",false}}})}}.dump());
    initializeContentStorage(fixture.root);const auto found=findQuaderSceneBundle(fixture.root,owner.projectId,owner.sceneId);
    require(found&&read(resolveAssetPath(fixture.root,value.memberIds[1]))==value.gltf&&!fs::exists(fixture.root/".crimson/content-move.json"),"Interrupted migration recovery did not restore inputs before bundle publication.");
    Fixture deleted;const auto gone=model(owner,"deleted");const auto directory=legacy(deleted,gone);const auto policy=Json{{"version",1},{"deleted",Json::array({gone.id})},{"replacements",{{gone.id,""}}}}.dump();writeFileAtomically(deleted.root/".crimson/references.json",policy);
    initializeContentStorage(deleted.root);require(fs::is_regular_file(directory/"manifest.json")&&!findQuaderSceneBundle(deleted.root,owner.projectId,owner.sceneId)&&read(deleted.root/".crimson/references.json")==policy,"Migration revived a tombstoned model or changed deletion policy.");
    Fixture distinct;auto divergent=model(owner,"distinct-original");const auto retained=legacy(distinct,divergent);writeFileAtomically(retained/"source.gltf","different retained original");rejects([&]{initializeContentStorage(distinct.root);},"Distinct retained source bytes were silently discarded.");require(read(retained/"source.gltf")=="different retained original"&&read(retained/"manifest.json")==divergent.manifest,"Rejected migration altered authored package inputs.");
    Fixture invalidPolicy;const auto blocked=model(owner,"blocked-policy");const auto blockedDirectory=legacy(invalidPolicy,blocked);auto other=bundle("another-scene");other.models.push_back(model(other,"tracked"));const auto tracked=defaultQuaderSceneBundlePath(invalidPolicy.root,other);const auto trackedBytes=encodeQuaderSceneBundle(other);writeFileAtomically(tracked,trackedBytes);
    const auto locationFile=invalidPolicy.root/".crimson/asset-locations.json";const auto beforeLocations=read(locationFile);writeFileAtomically(invalidPolicy.root/".crimson/references.json","{invalid policy");
    initializeContentStorage(invalidPolicy.root);
    require(read(blockedDirectory/"manifest.json")==blocked.manifest&&read(tracked)==trackedBytes&&read(locationFile)==beforeLocations&&!fs::exists(invalidPolicy.root/".crimson/cache/quader-imports"),"Malformed reference policy did not preserve graceful startup and unchanged bundle/package storage.");
}
}
int main(){try{codecValidation();migrationAndReconstruction();recoveryAndTombstones();std::cout<<"Quader scene bundle checks passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
