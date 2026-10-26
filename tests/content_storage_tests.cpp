#include "assets/ContentStorage.h"
#include "assets/AtomicFile.h"
#include "assets/AssetReferenceManager.h"
#include "resources/AssetId.h"
#include <nlohmann/json.hpp>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
namespace fs=std::filesystem;
using namespace crimson;
using Json=nlohmann::json;
void require(bool condition,const char* why){if(!condition)throw std::runtime_error(why);}
template<class F> void rejects(F&& f,const char* why){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}require(rejected,why);}
std::string read(const fs::path& p){std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
struct Fixture {
    fs::path root=fs::temp_directory_path()/("crimson-content-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Fixture(){fs::create_directories(root);}
    ~Fixture(){std::error_code error;const auto p=fs::weakly_canonical(root,error),base=fs::weakly_canonical(fs::temp_directory_path(),error);if(!error&&p.parent_path()==base&&p.filename().string().starts_with("crimson-content-"))fs::remove_all(p,error);}
    void write(const fs::path& p,const std::string& value){writeFileAtomically(root/p,value);}
};
void migrationAndMoves() {
    Fixture f;
    f.write("assets/textures/stone.png","image bytes");
    f.write("assets/materials/old.crimson-material",R"({"id":"assets/materials/old.crimson-material","name":"Stone Material","texture":"assets/textures/stone.png"})");
    f.write("assets/scenes/world.crimson-scene",R"({"name":"World","material":"assets/materials/old.crimson-material"})");
    f.write("assets/materials/long.crimson-material",Json{{"id","assets/materials/long.crimson-material"},{"name",std::string(200,'A')}}.dump());
    f.write("assets/browser.json",R"({"version":1,"folders":[{"id":"folder:old","parent":"","name":"Props"}],"assets":{"assets/textures/stone.png":{"folder":"folder:old","name":"Stone"},"assets/materials/old.crimson-material":{"folder":"folder:old","name":""}}})");
    f.write("assets/editor-session.json","session bytes");f.write("assets/branding/icon.ico","icon bytes");
    f.write("assets/references.json",R"({"version":1,"deleted":["assets/materials/gone.crimson-material","assets/models/deleted/manifest.json"],"replacements":{"assets/materials/gone.crimson-material":"builtin:material/default","assets/models/deleted/manifest.json":""}})");
    initializeContentStorage(f.root);
    const auto image=resolveAssetPath(f.root,"assets/textures/stone.png"),material=resolveAssetPath(f.root,"assets/materials/old.crimson-material");
    const auto materialBytes=read(material),sceneBytes=read(resolveAssetPath(f.root,"assets/scenes/world.crimson-scene"));
    require(image==fs::weakly_canonical(f.root/"Content/Props/Stone.png"),"Migration did not create the former browser folder physically.");
    require(resolveAssetPath(f.root,"assets/materials/long.crimson-material").extension()==".crimson-material","Migration dropped a native extension while shortening a long display name.");
    require(read(image)=="image bytes"&&Json::parse(materialBytes).at("name")=="Stone Material"&&Json::parse(materialBytes).at("texture")==canonicalAssetId("assets/textures/stone.png")&&Json::parse(sceneBytes).at("material")==canonicalAssetId("assets/materials/old.crimson-material"),"Migration changed authored values or failed UUID reference conversion.");
    require(!fs::exists(f.root/"assets/browser.json")&&read(f.root/".crimson/editor-session.json")=="session bytes"&&read(f.root/"assets/branding/icon.ico")=="icon bytes","Migration did not separate private metadata and engine branding.");
    require(fs::is_regular_file(resolveAssetPath(f.root,"builtin:texture/gray"))&&fs::is_directory(f.root/"Engine/materials"),"Engine defaults were not materialized separately.");
    AssetReferenceManager policy(f.root);
    require(policy.status().empty()&&policy.deleted(canonicalAssetId("assets/materials/gone.crimson-material"))&&policy.resolveModelMaterial(canonicalAssetId("assets/materials/gone.crimson-material"))==canonicalAssetId("builtin:material/default")&&policy.resolve(canonicalAssetId("assets/models/deleted/manifest.json")).empty(),"UUID conversion lost deletion policy for assets whose files were already absent.");
    const auto revision=contentStorageRevision(f.root);const auto browserBytes=read(f.root/".crimson/browser.json");initializeContentStorage(f.root);
    require(revision==contentStorageRevision(f.root)&&browserBytes==read(f.root/".crimson/browser.json"),"Migration is not idempotent.");
    const ContentPathMove move{f.root/"Content/Props",f.root/"Content/Environment"};
    relocateContentPaths(f.root,std::span(&move,1));
    require(!fs::exists(move.from)&&read(f.root/"Content/Environment/Stone.png")=="image bytes"&&assetIdAtPath(f.root,f.root/"Content/Environment/Stone.png")==canonicalAssetId("assets/textures/stone.png"),"Physical folder move did not preserve stable UUID identity.");
    require(read(resolveAssetPath(f.root,"assets/materials/old.crimson-material"))==materialBytes,"Folder move altered material references.");
    const auto before=contentStorageRevision(f.root);f.write("Content/Collision/keep.txt","keep");
    const ContentPathMove collision{move.to,f.root/"Content/Collision"};
    rejects([&]{relocateContentPaths(f.root,std::span(&collision,1));},"A content move overwrote an existing directory.");
    require(contentStorageRevision(f.root)==before&&read(f.root/"Content/Collision/keep.txt")=="keep"&&fs::exists(move.to),"Failed move changed files or the index.");
    const auto reserved=f.root/"Content/Reserved.png";registerAssetPath(f.root,newAssetId(),reserved);
    const ContentPathMove reservedMove{resolveAssetPath(f.root,"assets/textures/stone.png"),reserved};
    rejects([&]{relocateContentPaths(f.root,std::span(&reservedMove,1));},"A move reused a missing asset's reserved physical location.");
    const ContentPathMove engine{f.root/"Engine/textures/DefaultGray.tga",f.root/"Content/Gray.tga"};
    rejects([&]{relocateContentPaths(f.root,std::span(&engine,1));},"Protected engine defaults could be moved.");
    rejects([&]{resolveAssetPath(f.root,"assets/../../outside");},"Traversal identity was accepted.");
    for(const auto* name:{"CON","foo.","a:b","a?b","lpt2.png"})rejects([&]{contentFilename(name);},"Invalid Windows name was accepted.");
}
void modelPackageAndRecovery() {
    Fixture f;f.write("assets/models/model-abc/manifest.json",R"({"name":"Tomb","original":"source.glb","materialAssets":["assets/materials/model-abc-m0.crimson-material"]})");
    f.write("assets/models/model-abc/source.glb","retained original");f.write("assets/models/model-abc/model.gltf","gltf bytes");
    f.write("assets/models/model-abc/images/0.png","package image");
    f.write("assets/scenes/collider.crimson-scene",R"({"name":"Keep Display Text","model":"assets/models/model-abc/manifest.json","meshCollider":{"source":"assets/models/model-abc/manifest.json"}})");
    initializeContentStorage(f.root);
    const auto manifest=resolveAssetPath(f.root,"assets/models/model-abc/manifest.json"),image=resolveAssetPath(f.root,"assets/models/model-abc/images/0.png");
    require(read(manifest.parent_path()/"images/0.png")=="package image"&&read(image)=="package image"&&image.parent_path()==f.root/"Content","Model migration did not preserve original inputs and independently visible textures.");
    require(Json::parse(read(manifest)).at("materialAssets").at(0)==canonicalAssetId("assets/materials/model-abc-m0.crimson-material")&&read(manifest.parent_path()/"source.glb")=="retained original","Package material references were not converted independently of retained source bytes.");
    const auto scene=Json::parse(read(resolveAssetPath(f.root,"assets/scenes/collider.crimson-scene")));
    require(scene.at("model")==canonicalAssetId("assets/models/model-abc/manifest.json")&&scene.at("meshCollider").at("source")==scene.at("model"),"Collider source was omitted from UUID migration.");
    const auto indexBefore=read(f.root/".crimson/asset-locations.json"),browserBefore=read(f.root/".crimson/browser.json");
    const auto moved=f.root/"Content/Interrupted.png";fs::rename(image,moved);
    f.write(".crimson/content-move.json",Json{{"version",1},{"committed",false},{"indexBefore",indexBefore},{"browserBefore",browserBefore},{"moves",Json::array({{{"from",image.lexically_relative(f.root).generic_string()},{"to",moved.lexically_relative(f.root).generic_string()},{"copy",false}}})}}.dump());
    initializeContentStorage(f.root);
    require(fs::exists(image)&&!fs::exists(moved)&&read(f.root/".crimson/asset-locations.json")==indexBefore&&!fs::exists(f.root/".crimson/content-move.json"),"Interrupted physical move did not recover coherently.");
    const auto sceneId=canonicalAssetId("assets/scenes/collider.crimson-scene");const auto scenePath=resolveAssetPath(f.root,sceneId);
    f.write(".crimson/.resource-transactions/interrupted/before/0.bin",R"({"name":"Recovered scene","meshCollider":{"source":"assets/models/model-abc/manifest.json"}})");
    f.write(".crimson/.resource-transactions/interrupted/journal.json",Json{{"version",1},{"committed",false},{"files",Json::array({{{"id",sceneId},{"existed",true},{"backup","0.bin"}}})}}.dump());
    writeFileAtomically(scenePath,"{}");initializeContentStorage(f.root);
    const auto restored=Json::parse(read(scenePath));
    require(restored.at("name")=="Recovered scene"&&restored.at("meshCollider").at("source")==canonicalAssetId("assets/models/model-abc/manifest.json")&&!fs::exists(f.root/".crimson/.resource-transactions/interrupted"),"Recovered deletion backups bypassed UUID conversion.");
}
void collisionNamesAreDisplayText() {
    Fixture f;std::array<std::string,32> names{};names[0]="assets/textures/not-an-asset.png";names[1]=newAssetId();names[2]="builtin:texture/normal";
    const auto original=Json{{"version",1},{"names",names}}.dump(2);f.write(".crimson/collision-layers.json",original);
    f.write("assets/editor-session.json",R"({"version":1,"activeScene":"assets/scenes/missing.crimson-scene"})");
    initializeContentStorage(f.root);
    require(read(f.root/".crimson/collision-layers.json")==original,"UUID migration interpreted collision layer display names as asset references.");
    require(Json::parse(read(f.root/".crimson/editor-session.json")).at("activeScene")==canonicalAssetId("assets/scenes/missing.crimson-scene"),"Collision name exclusion disabled migration of unrelated private settings.");
    const auto verifyIndex=[&] {
        const auto index=Json::parse(read(f.root/".crimson/asset-locations.json"));
        require(!index.at("assets").contains(canonicalAssetId(names[0]))&&!index.at("assets").contains(names[1]),"Collision display names introduced missing asset records.");
    };
    verifyIndex();auto index=Json::parse(read(f.root/".crimson/asset-locations.json"));index["uuidReferences"]=false;
    f.write(".crimson/asset-locations.json",index.dump());refreshContentStorage(f.root);initializeContentStorage(f.root);
    require(read(f.root/".crimson/collision-layers.json")==original,"Repeated UUID conversion rewrote collision layer labels.");verifyIndex();
}
void uuidIdentity() {
    const auto first=newAssetId(),second=newAssetId();require(isAssetUuid(first)&&isAssetUuid(second)&&first!=second,"New assets did not receive distinct valid UUIDs.");
    require(canonicalAssetId("builtin:material/default")=="e698f0ce-a518-596e-8710-ff12e009eb5d"&&isEngineAssetId(canonicalAssetId("builtin:texture/normal")),"Protected UUID constants disagree with the UUID library.");
    const auto model=canonicalAssetId("assets/models/model-abc/manifest.json");
    require(canonicalAssetId("assets/materials/model-abc-m12.crimson-material")==derivedAssetId(model,"material:12"),"Imported sidecar UUID changed when converting its model identity.");
    require(canonicalAssetId("assets/models/model-abc/images/0.png")==derivedAssetId(model,"image:images/0.png"),"Imported image UUID changed during conversion.");
    require(!isAssetUuid("00000000-0000-0000-0000-000000000000")&&!isAssetUuid("assets/textures/a.png"),"A nil or path-shaped reference was accepted as a UUID.");
}
void watcherSnapshots() {
    Fixture f;initializeContentStorage(f.root);
    const auto id=newAssetId();f.write("Content/Original.png","pixels");registerAssetPath(f.root,id,f.root/"Content/Original.png");
    const auto original=resolveAssetPath(f.root,id);const auto revision=contentStorageRevision(f.root);
    for(int i=0;i<100;++i)require(contentStorageReady(f.root)&&contentStorageRevision(f.root)==revision&&resolveAssetPath(f.root,id)==original&&assetIdAtPath(f.root,original)==id,"Warm storage lookups changed identity or revision.");
    require(!refreshContentStorage(f.root)&&contentStorageRevision(f.root)==revision,"An unchanged watcher notification invalidated the catalog.");
    const auto file=f.root/".crimson/asset-locations.json";const auto before=read(file);auto changed=Json::parse(before);
    changed["assets"][id]="Content/Relocated.png";f.write(".crimson/asset-locations.json",changed.dump(2));
    require(resolveAssetPath(f.root,id)==original,"Storage lookups unexpectedly perform implicit filesystem refreshes.");
    require(refreshContentStorage(f.root)&&contentStorageRevision(f.root)>revision&&resolveAssetPath(f.root,id)==f.root/"Content/Relocated.png"&&assetIdAtPath(f.root,f.root/"Content/Relocated.png")==id,"Explicit refresh missed an external location edit without a persisted revision increment.");
    const auto movedRevision=contentStorageRevision(f.root);
    require(!refreshContentStorage(f.root)&&contentStorageRevision(f.root)==movedRevision,"Repeated external notifications changed an equal snapshot revision.");
    changed["assets"][id]="Content/../../outside.png";f.write(".crimson/asset-locations.json",changed.dump(2));
    rejects([&]{refreshContentStorage(f.root);},"External traversal location passed snapshot validation.");
    require(contentStorageRevision(f.root)>movedRevision,"Blocked external locations did not advance invalidation state.");
    rejects([&]{resolveAssetPath(f.root,id);},"A rejected external index retained a usable stale path.");
    rejects([&]{storedAssetPaths(f.root);},"A rejected external index retained an enumerable stale catalog.");
    f.write(".crimson/asset-locations.json",before);require(refreshContentStorage(f.root)&&resolveAssetPath(f.root,id)==original,"A repaired external index did not recover the snapshot.");
}
void registrationGroups() {
    Fixture f;initializeContentStorage(f.root);
    const auto index=f.root/".crimson/asset-locations.json";
    const std::string first="assets/textures/group-first.png";const auto second=newAssetId();
    const std::array paths{StoredAssetPath{first,f.root/"Content/First.png"},StoredAssetPath{second,f.root/"Content/Second.png"}};
    const auto before=contentStorageRevision(f.root);registerAssetPaths(f.root,paths);
    require(contentStorageRevision(f.root)==before+1,"A registration group published more than one index revision.");
    require(resolveAssetPath(f.root,first)==paths[0].file&&resolveAssetPath(f.root,canonicalAssetId(first))==paths[0].file&&
        resolveAssetPath(f.root,second)==paths[1].file&&assetIdAtPath(f.root,paths[0].file)==canonicalAssetId(first)&&legacyAssetId(f.root,canonicalAssetId(first))==first,
        "A registration group lost canonical identities, reverse lookup or legacy provenance.");
    const auto bytes=read(index);const auto revision=contentStorageRevision(f.root);const auto count=storedAssetPaths(f.root).size();
    fs::last_write_time(index,fs::last_write_time(index)-std::chrono::seconds{5});const auto timestamp=fs::last_write_time(index);
    registerAssetPaths(f.root,paths);registerAssetPaths(f.root,{});registerAssetPath(f.root,first,paths[0].file);
    require(read(index)==bytes&&contentStorageRevision(f.root)==revision&&fs::last_write_time(index)==timestamp,
        "Unchanged/empty registrations rewrote the index or advanced its revision.");
    const auto pending=newAssetId(),other=newAssetId();
    const auto unchanged=[&] {
        require(read(index)==bytes&&contentStorageRevision(f.root)==revision&&storedAssetPaths(f.root).size()==count&&resolveAssetPath(f.root,second)==paths[1].file,
            "A rejected registration group partially changed the index or cached snapshot.");
        rejects([&]{resolveAssetPath(f.root,pending);},"A rejected registration group retained a new cached asset location.");
    };
    const std::array duplicateFile{StoredAssetPath{pending,f.root/"Content/Third.png"},StoredAssetPath{other,f.root/"Content/Third.png"}};
    rejects([&]{registerAssetPaths(f.root,duplicateFile);},"Two identities in one registration group claimed the same file.");unchanged();
    const std::array existingFile{StoredAssetPath{pending,f.root/"Content/Third.png"},StoredAssetPath{second,paths[0].file}};
    rejects([&]{registerAssetPaths(f.root,existingFile);},"A registration group claimed another asset's existing location.");unchanged();
    const std::array duplicateIdentity{StoredAssetPath{pending,f.root/"Content/Third.png"},StoredAssetPath{pending,f.root/"Content/Fourth.png"}};
    rejects([&]{registerAssetPaths(f.root,duplicateIdentity);},"One identity claimed conflicting files in a registration group.");unchanged();
    const std::array protectedFile{StoredAssetPath{pending,f.root/"Content/Third.png"},StoredAssetPath{other,f.root/"Engine/Unprotected.png"}};
    rejects([&]{registerAssetPaths(f.root,protectedFile);},"A registration group assigned an unprotected identity to Engine content.");unchanged();
    const std::array outside{StoredAssetPath{pending,f.root/"Content/Third.png"},StoredAssetPath{other,f.root/"../Outside.png"}};
    rejects([&]{registerAssetPaths(f.root,outside);},"A registration group accepted a location outside its project.");unchanged();
    const std::array emptyIdentity{StoredAssetPath{pending,f.root/"Content/Third.png"},StoredAssetPath{{},f.root/"Content/Fourth.png"}};
    rejects([&]{registerAssetPaths(f.root,emptyIdentity);},"A registration group accepted an empty identity.");unchanged();
}
void resolutionSnapshots() {
    Fixture f;AssetReferenceManager policy(f.root);
    const std::array first{AssetEntry{"assets/textures/first.png","First",AssetKind::Texture}};
    const std::array second{AssetEntry{"assets/textures/second.png","Second",AssetKind::Texture}};
    policy.replace(first,{{first[0].id,second[0].id}});
    policy.setUsages({{first[0].id,"assets/scenes/world.crimson-scene","World","Texture"}});
    const auto before=policy.resolutionSnapshot();
    require(before->usages().empty()&&before->revision()==policy.revision()&&before->valid()==policy.valid()&&
        before->status()==policy.status()&&before->serialize()==policy.serialize()&&before->resolveTexture(first[0].id)==second[0].id,
        "A resolution snapshot copied usage metadata or lost replacement policy.");
    const auto revision=policy.revision();policy.setUsages({});
    require(policy.revision()==revision&&before->resolveTexture(first[0].id)==policy.resolveTexture(first[0].id),
        "Changing the usage index changed asset resolution or its policy revision.");
    policy.replace(second,{});const auto after=policy.resolutionSnapshot();
    require(after->revision()>before->revision()&&after->resolveTexture(first[0].id)==canonicalAssetId("builtin:texture/gray")&&
        after->resolveTexture(first[0].id,true)==canonicalAssetId("builtin:texture/normal")&&before->resolveTexture(first[0].id)==second[0].id&&
        !before->deleted(second[0].id),"A policy edit mutated an older immutable resolution snapshot.");
    f.write("assets/references.json","{broken JSON");AssetReferenceManager invalid(f.root);const auto blocked=invalid.resolutionSnapshot();
    require(!blocked->valid()&&blocked->status()==invalid.status()&&blocked->revision()==invalid.revision(),
        "A resolution snapshot lost invalid-policy state or its diagnostic.");
}
}
int main(){try{uuidIdentity();migrationAndMoves();modelPackageAndRecovery();collisionNamesAreDisplayText();watcherSnapshots();registrationGroups();resolutionSnapshots();std::cout<<"Content storage checks passed.\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
