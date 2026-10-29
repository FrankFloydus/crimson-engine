#include "assets/QuaderSceneBundle.h"
#include "assets/AtomicFile.h"
#include "resources/AssetId.h"
#include <nlohmann/json.hpp>
#include <algorithm>
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
    auto original=bundle();original.models.push_back(model(original,"first"));original.models.push_back(model(original,"second"));original.models[1].current=false;const auto encoded=encodeQuaderSceneBundle(original);const auto decoded=decodeQuaderSceneBundle(encoded);
    require(decoded.id==original.id&&decoded.models.size()==2&&decoded.models[1].buffer==original.models[1].buffer&&decoded.models[0].manifest==original.models[0].manifest&&encodeQuaderSceneBundle(decoded)==encoded,"Binary scene bundle did not round trip exact geometry or material metadata.");
    require(encoded.find(original.models[0].buffer)!=std::string::npos,"Binary geometry was not stored as raw payload.");
    require(decoded.models[0].current&&!decoded.models[1].current,"Current and retired generation state did not round trip.");
    std::uint64_t length{};for(unsigned i=0;i<8;++i)length|=std::uint64_t(static_cast<unsigned char>(encoded[12+i]))<<(8*i);
    auto oldMetadata=Json::parse(encoded.substr(20,static_cast<std::size_t>(length)));for(auto& item:oldMetadata["models"])item.erase("current");
    const auto oldText=oldMetadata.dump();auto legacyBytes=encoded.substr(0,12);for(unsigned i=0;i<8;++i)legacyBytes.push_back(static_cast<char>((oldText.size()>>(8*i))&255));legacyBytes+=oldText;legacyBytes+=encoded.substr(20+static_cast<std::size_t>(length));
    const auto legacyDecoded=decodeQuaderSceneBundle(legacyBytes);require(legacyDecoded.models[0].current&&legacyDecoded.models[1].current,"Legacy bundles unexpectedly retired usable model assets.");
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
void sceneContentNames(){
    Fixture fixture;auto owner=bundle();
    for(const auto* name:{"Diablo Room","diabloRoom"," ../Diablo:/Room?? "}){
        owner.name=name;require(quaderSceneContentDirectory(fixture.root,owner)==fixture.root/"Content/diablo_room",
            "Scene content names did not normalize spaces, camel case or invalid path characters to one safe snake-case directory.");
    }
    owner.name="CON";require(quaderSceneContentDirectory(fixture.root,owner)==fixture.root/"Content/_con","A reserved Windows scene name produced an unusable directory.");
    owner.name="<>:/\\|?*";require(quaderSceneContentDirectory(fixture.root,owner)==fixture.root/"Content/quader_scene","An empty sanitized scene name did not use the stable fallback.");
    owner.name="Diablo Room";auto other=bundle("same-name-other-source");other.name=owner.name;
    require(defaultQuaderSceneBundlePath(fixture.root,owner)!=defaultQuaderSceneBundlePath(fixture.root,other)&&
        defaultQuaderSceneBundlePath(fixture.root,owner).parent_path()==fixture.root/"Content/diablo_room/models"&&
        defaultQuaderSceneBundlePath(fixture.root,other).parent_path()==fixture.root/"Content/diablo_room/models",
        "Two source scenes with the same display name collided or escaped the scene's model directory.");
}
void sceneContentLayoutMigration(){
    Fixture fixture;auto first=bundle(),second=bundle("scene-two");first.name="Diablo Room";second.name="Other Hall";
    const std::string materialLegacy="assets/materials/quader-project-one-shared.crimson-material",secondLegacy="assets/materials/quader-project-one-second.crimson-material";
    const std::string textureLegacy="assets/textures/quader-shared.png",manualMaterialLegacy="assets/materials/quader-project-one-manual.crimson-material",manualTextureLegacy="assets/textures/quader-manual.png";
    const auto materialId=canonicalAssetId(materialLegacy),secondId=canonicalAssetId(secondLegacy),textureId=canonicalAssetId(textureLegacy),manualMaterialId=canonicalAssetId(manualMaterialLegacy),manualTextureId=canonicalAssetId(manualTextureLegacy);
    const auto authoredId=canonicalAssetId("assets/materials/authored.crimson-material"),unrelatedId=canonicalAssetId("assets/textures/quader-unrelated.png");
    const auto materialFile=fixture.root/"Content/materials/quader-project-one-shared.crimson-material",secondFile=fixture.root/"Content/materials/quader-project-one-second.crimson-material",textureFile=fixture.root/"Content/textures/quader-shared.png";
    const auto manualMaterial=fixture.root/"Content/Custom/HandPlaced.crimson-material",manualTexture=fixture.root/"Content/Art/HandPlaced.png",authoredFile=fixture.root/"Content/materials/Authored.crimson-material",unrelatedTexture=fixture.root/"Content/textures/quader-unrelated.png";
    const auto materialBytes=Json{{"version",6},{"graph",{{"nodes",Json::array({{{"texture",{{"image",textureId}}}},{{"texture",{{"image",manualTextureId}}}}})}}}}.dump(2);
    const auto secondBytes=Json{{"version",2},{"surface",{{"baseColorTexture",{{"image",textureId}}}}}}.dump(2);
    const auto authoredBytes=Json{{"version",6},{"texture",{{"image",unrelatedId}}}}.dump();
    writeFileAtomically(materialFile,materialBytes);writeFileAtomically(secondFile,secondBytes);writeFileAtomically(textureFile,"shared generated pixels");
    writeFileAtomically(manualMaterial,"manually placed material");writeFileAtomically(manualTexture,"manually placed pixels");writeFileAtomically(authoredFile,authoredBytes);writeFileAtomically(unrelatedTexture,"unrelated pixels");
    const std::array<StoredAssetPath,7> native{{{materialLegacy,materialFile},{secondLegacy,secondFile},{textureLegacy,textureFile},{manualMaterialLegacy,manualMaterial},{manualTextureLegacy,manualTexture},{"assets/materials/authored.crimson-material",authoredFile},{"assets/textures/quader-unrelated.png",unrelatedTexture}}};
    registerAssetPaths(fixture.root,native);
    first.models={model(first,"layout-first")};second.models={model(second,"layout-second")};
    auto manifest=Json::parse(first.models[0].manifest);manifest["materialAssets"]=Json::array({materialId,authoredId,manualMaterialId});first.models[0].manifest=manifest.dump(2)+"\n";
    manifest=Json::parse(second.models[0].manifest);manifest["materialAssets"]=Json::array({secondId,materialId});second.models[0].manifest=manifest.dump(2)+"\n";
    const auto firstFile=fixture.root/"Content/models/OldDungeon.crimson-quader",secondBundle=fixture.root/"Content/models/OldOther.crimson-quader";
    const auto firstBytes=encodeQuaderSceneBundle(first),otherBytes=encodeQuaderSceneBundle(second);
    writeFileAtomically(firstFile,firstBytes);writeFileAtomically(secondBundle,otherBytes);registerAssetPath(fixture.root,first.id,firstFile);registerAssetPath(fixture.root,second.id,secondBundle);
    const auto scene=fixture.root/"Content/World.crimson-scene";const auto sceneBytes=Json{{"name","Unchanged world"},{"model",first.models[0].id},{"material",materialId},{"texture",textureId}}.dump(2);
    writeFileAtomically(scene,sceneBytes);writeFileAtomically(fixture.root/"Content/models/keep.txt","unrelated model-folder file");
    const auto aliases=Json::parse(read(fixture.root/".crimson/asset-locations.json")).at("aliases");const auto browser=read(fixture.root/".crimson/browser.json");
    initializeContentStorage(fixture.root);
    const auto firstDestination=defaultQuaderSceneBundlePath(fixture.root,first),secondDestination=defaultQuaderSceneBundlePath(fixture.root,second);
    const auto& sharedOwner=first.id<second.id?first:second;const auto sharedDirectory=quaderSceneContentDirectory(fixture.root,sharedOwner);
    require(!fs::exists(firstFile)&&!fs::exists(secondBundle)&&read(firstDestination)==firstBytes&&read(secondDestination)==otherBytes&&
        resolveAssetPath(fixture.root,first.id)==firstDestination&&resolveAssetPath(fixture.root,second.id)==secondDestination,
        "Flat scene bundles did not move with unchanged UUIDs and bytes to their scene model folders.");
    require(resolveAssetPath(fixture.root,materialId)==sharedDirectory/"materials"/materialFile.filename()&&
        resolveAssetPath(fixture.root,secondId)==quaderSceneContentDirectory(fixture.root,second)/"materials"/secondFile.filename()&&
        resolveAssetPath(fixture.root,textureId)==sharedDirectory/"textures"/textureFile.filename()&&
        read(resolveAssetPath(fixture.root,materialId))==materialBytes&&read(resolveAssetPath(fixture.root,secondId))==secondBytes&&read(resolveAssetPath(fixture.root,textureId))=="shared generated pixels"&&
        !fs::exists(materialFile)&&!fs::exists(secondFile)&&!fs::exists(textureFile),
        "Generated native materials/textures were duplicated, rewritten, or assigned nondeterministic scene ownership.");
    require(resolveAssetPath(fixture.root,manualMaterialId)==manualMaterial&&read(manualMaterial)=="manually placed material"&&
        resolveAssetPath(fixture.root,manualTextureId)==manualTexture&&read(manualTexture)=="manually placed pixels"&&
        resolveAssetPath(fixture.root,authoredId)==authoredFile&&read(authoredFile)==authoredBytes&&resolveAssetPath(fixture.root,unrelatedId)==unrelatedTexture&&
        read(unrelatedTexture)=="unrelated pixels"&&read(fixture.root/"Content/models/keep.txt")=="unrelated model-folder file"&&read(scene)==sceneBytes,
        "Scene layout migration moved authored replacements, manual paths or unrelated native resources, or rewrote a saved scene.");
    const auto index=fixture.root/".crimson/asset-locations.json";const auto indexBytes=read(index);const auto revision=contentStorageRevision(fixture.root);
    auto expectedAliases=aliases;
    for(const auto* owner:{&first,&second})for(const auto& generation:owner->models){const auto base=fs::path(generation.legacyManifest).parent_path();
        for(std::size_t member=0;member<Members.size();++member)expectedAliases[(base/Members[member]).generic_string()]=generation.memberIds[member];}
    require(Json::parse(indexBytes).at("aliases")==expectedAliases&&read(fixture.root/".crimson/browser.json")==browser&&
        legacyAssetId(fixture.root,materialId)==materialLegacy&&legacyAssetId(fixture.root,textureId)==textureLegacy,
        "Scene-folder adoption changed legacy reference aliases or browser metadata.");
    const auto indexTime=fs::last_write_time(index),bundleTime=fs::last_write_time(firstDestination),materialTime=fs::last_write_time(resolveAssetPath(fixture.root,materialId));
    initializeContentStorage(fixture.root);
    require(read(index)==indexBytes&&fs::last_write_time(index)==indexTime&&fs::last_write_time(firstDestination)==bundleTime&&
        fs::last_write_time(resolveAssetPath(fixture.root,materialId))==materialTime&&contentStorageRevision(fixture.root)==revision,
        "Warm initialization rewrote organized bundles, native materials or the location index.");
    fixture.clearCache();initializeContentStorage(fixture.root);
    require(read(resolveAssetPath(fixture.root,first.models[0].id))==first.models[0].manifest&&
        quaderBundleSource(fixture.root,first.models[0].id)==std::optional{firstDestination}&&read(firstDestination)==firstBytes&&read(scene)==sceneBytes,
        "Moving a scene bundle broke cache reconstruction, UUID resolution or saved scene preservation.");
    // A manually moved bundle stays at its authored location on later starts.
    const auto custom=fixture.root/"Content/Custom/PlacedScene.crimson-quader";const ContentPathMove move{firstDestination,custom,false};relocateContentPaths(fixture.root,std::span(&move,1));
    initializeContentStorage(fixture.root);require(resolveAssetPath(fixture.root,first.id)==custom&&read(custom)==firstBytes&&!fs::exists(firstDestination),"Initialization reversed an authored bundle move.");
}
void sceneLayoutRecoveryAndEmptyBundles(){
    Fixture fixture;auto owner=bundle();owner.name="Abandoned Room";const auto source=fixture.root/"Content/models/Empty.crimson-quader";
    writeFileAtomically(source,encodeQuaderSceneBundle(owner));registerAssetPath(fixture.root,owner.id,source);
    const auto index=read(fixture.root/".crimson/asset-locations.json"),browser=read(fixture.root/".crimson/browser.json");
    const auto temporary=fixture.root/".crimson/.staging-quader-layout-interrupted/Empty.crimson-quader";
    fs::create_directories(temporary.parent_path());fs::rename(source,temporary);
    writeFileAtomically(fixture.root/".crimson/content-move.json",Json{{"version",1},{"committed",false},{"indexBefore",index},{"browserBefore",browser},{"moves",Json::array({{{"from",source.lexically_relative(fixture.root).generic_string()},{"to",temporary.lexically_relative(fixture.root).generic_string()},{"copy",false}}})}}.dump());
    initializeContentStorage(fixture.root);
    const auto paths=storedAssetPaths(fixture.root);
    require(!fs::exists(source)&&!fs::exists(fixture.root/"Content/models")&&!fs::exists(fixture.root/".crimson/content-move.json")&&
        !findQuaderSceneBundle(fixture.root,owner.projectId,owner.sceneId)&&
        std::none_of(paths.begin(),paths.end(),[&](const auto& value){return value.id==owner.id;}),
        "Recovered empty historical bundles retained source files, registrations or an empty Models folder.");
    owner.models.push_back(model(owner,"returning"));const auto destination=defaultQuaderSceneBundlePath(fixture.root,owner);
    writeFileAtomically(destination,encodeQuaderSceneBundle(owner));initializeContentStorage(fixture.root);
    require(resolveAssetPath(fixture.root,owner.id)==destination&&read(resolveAssetPath(fixture.root,owner.models[0].id))==owner.models[0].manifest,
        "Retiring an empty historical bundle reserved its identity or prevented subsequent source import.");
}
void unusedGenerationPruning(){
    Fixture fixture;auto owner=bundle();owner.models={model(owner,"current"),model(owner,"saved"),model(owner,"history"),model(owner,"unused")};
    for(std::size_t i=1;i<owner.models.size();++i)owner.models[i].current=false;
    const auto source=defaultQuaderSceneBundlePath(fixture.root,owner);writeFileAtomically(source,encodeQuaderSceneBundle(owner));initializeQuaderSceneBundles(fixture.root);
    const auto unused=owner.models[3],saved=owner.models[1],history=owner.models[2];
    const auto unusedDirectory=quaderBundleCacheDirectory(fixture.root,owner.id,unused.id);writeFileAtomically(unusedDirectory/"keep.txt","unrelated file");
    const auto policy=fixture.root/".crimson/references.json";writeFileAtomically(policy,R"({"version":1,"deleted":[],"replacements":{}})");const auto beforePolicy=read(policy);
    const auto beforeSize=fs::file_size(source);const std::array references{saved.id,history.memberIds[3]};
    const auto removed=pruneUnusedQuaderBundleModels(fixture.root,references);const auto pruned=decodeQuaderSceneBundle(read(source));
    require(removed==std::vector<std::string>{unused.id}&&pruned.models.size()==3&&fs::file_size(source)<beforeSize,"Cleanup did not reclaim only an unused retired generation.");
    require(read(unusedDirectory/"keep.txt")=="unrelated file"&&!fs::exists(unusedDirectory/"manifest.json")&&!fs::exists(unusedDirectory/"buffers/0.bin"),"Cleanup lost an unrelated file or retained retired mesh bytes.");
    const auto locations=storedAssetPaths(fixture.root);for(const auto& id:unused.memberIds)require(std::none_of(locations.begin(),locations.end(),[&](const auto& path){return path.id==id;}),"Cleanup retained a dead member location.");
    require(legacyAssetId(fixture.root,unused.id)==unused.legacyManifest&&read(policy)==beforePolicy,"Pruning lost source identity provenance or created deletion tombstones.");
    const auto before=read(source),index=read(fixture.root/".crimson/asset-locations.json");const auto revision=contentStorageRevision(fixture.root);
    require(pruneUnusedQuaderBundleModels(fixture.root,references).empty()&&read(source)==before&&read(fixture.root/".crimson/asset-locations.json")==index&&contentStorageRevision(fixture.root)==revision,"An unchanged retention set rewrote bundle/index data.");
    initializeQuaderSceneBundles(fixture.root);require(!fs::exists(unusedDirectory/"manifest.json"),"Cache reconstruction resurrected reclaimed generations.");
    const auto remaining=pruneUnusedQuaderBundleModels(fixture.root,{});const auto final=decodeQuaderSceneBundle(read(source));
    require(remaining.size()==2&&final.models.size()==1&&final.models.front().current,"Released saved/history generations were not reclaimed, or current source geometry was removed.");
    auto restored=final;auto reused=unused;reused.current=true;restored.models.push_back(reused);writeFileAtomically(source,encodeQuaderSceneBundle(restored));initializeQuaderSceneBundles(fixture.root);
    require(read(resolveAssetPath(fixture.root,unused.id))==unused.manifest&&pruneUnusedQuaderBundleModels(fixture.root,{}).empty(),"Reimport could not reuse the identity of an unused reclaimed source mesh.");
    for(auto& generation:restored.models)generation.current=false;
    writeFileAtomically(source,encodeQuaderSceneBundle(restored));initializeQuaderSceneBundles(fixture.root);
    require(pruneUnusedQuaderBundleModels(fixture.root,{}).size()==2&&!fs::exists(source)&&!fs::exists(source.parent_path())&&
        read(unusedDirectory/"keep.txt")=="unrelated file"&&read(policy)==beforePolicy,
        "Reclaiming every unreferenced generation retained the empty bundle/model folder or removed unrelated cached data.");
    const auto emptyLocations=storedAssetPaths(fixture.root);
    require(std::none_of(emptyLocations.begin(),emptyLocations.end(),[&](const auto& location){return location.id==owner.id;}),
        "Reclaiming an empty bundle retained its managed location registration.");
    initializeQuaderSceneBundles(fixture.root);require(!fs::exists(source)&&!findQuaderSceneBundle(fixture.root,owner.projectId,owner.sceneId),
        "Startup reconstructed a fully reclaimed bundle or its empty scene Models folder.");
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
int main(){try{codecValidation();sceneContentNames();sceneContentLayoutMigration();sceneLayoutRecoveryAndEmptyBundles();unusedGenerationPruning();migrationAndReconstruction();recoveryAndTombstones();std::cout<<"Quader scene bundle checks passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
