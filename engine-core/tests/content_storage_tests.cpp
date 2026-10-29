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
void projectFolderAndMoves() {
    Fixture f;f.write("Art/Stone.png","image bytes");f.write("Notes/readme.txt","keep user bytes");f.write("Game.qproj","keep project metadata");
    initializeContentStorage(f.root);
    require(!fs::exists(f.root/"Content")&&!fs::exists(f.root/"Engine")&&!fs::exists(f.root/"assets"),"Storage created a forced visible asset root.");
    require(read(f.root/"Notes/readme.txt")=="keep user bytes"&&read(f.root/"Game.qproj")=="keep project metadata","Storage changed unrelated project files.");
    require(fs::is_regular_file(resolveAssetPath(f.root,canonicalAssetId("builtin:texture/gray")))&&resolveAssetPath(f.root,canonicalAssetId("builtin:texture/gray")).lexically_relative(f.root).begin()->string()==".crimson","Builtin defaults were not private.");
    const auto id=newAssetId();registerAssetPath(f.root,id,f.root/"Art/Stone.png");
    require(resolveAssetPath(f.root,id)==f.root/"Art/Stone.png"&&resolveAssetPath(f.root,"Art/Stone.png")==f.root/"Art/Stone.png","Root-relative project files did not resolve.");
    const auto index=f.root/".crimson/asset-locations.json";const auto bytes=read(index);const auto revision=contentStorageRevision(f.root);
    initializeContentStorage(f.root);require(read(index)==bytes&&contentStorageRevision(f.root)==revision,"Warm initialization rewrote the current index.");
    const ContentPathMove move{f.root/"Art",f.root/"Environment"};relocateContentPaths(f.root,std::span(&move,1));
    require(!fs::exists(move.from)&&read(f.root/"Environment/Stone.png")=="image bytes"&&resolveAssetPath(f.root,id)==f.root/"Environment/Stone.png","Folder relocation lost bytes or stable identity.");
    f.write("Collision/keep.txt","keep");const auto before=read(index);const ContentPathMove collision{move.to,f.root/"Collision"};
    rejects([&]{relocateContentPaths(f.root,std::span(&collision,1));},"A move overwrote an existing folder.");
    require(read(index)==before&&read(f.root/"Collision/keep.txt")=="keep"&&fs::exists(move.to),"A rejected move changed project data.");
    const ContentPathMove builtin{resolveAssetPath(f.root,canonicalAssetId("builtin:texture/gray")),f.root/"Gray.tga"};
    rejects([&]{relocateContentPaths(f.root,std::span(&builtin,1));},"A private builtin was moved by an authoring command.");
    rejects([&]{checkedContentPath(f.root,f.root/"../outside");},"A project write escaped its folder.");
    rejects([&]{resolveAssetPath(f.root,"../outside");},"A relative lookup escaped its folder.");
}
void interruptedMoveRecovery() {
    Fixture f;initializeContentStorage(f.root);f.write("Original.bin","recover these bytes");const auto id=newAssetId();registerAssetPath(f.root,id,f.root/"Original.bin");
    const auto before=read(f.root/".crimson/asset-locations.json");fs::rename(f.root/"Original.bin",f.root/"Interrupted.bin");
    f.write(".crimson/content-move.json",Json{{"version",1},{"committed",false},{"moves",Json::array({Json{{"from","Original.bin"},{"to","Interrupted.bin"},{"copy",false}}})},{"indexBefore",before},{"browserBefore",nullptr}}.dump());
    initializeContentStorage(f.root);
    require(read(f.root/"Original.bin")=="recover these bytes"&&!fs::exists(f.root/"Interrupted.bin")&&!fs::exists(f.root/".crimson/content-move.json")&&resolveAssetPath(f.root,id)==f.root/"Original.bin","Current-format move recovery did not restore the exact accepted files and index.");
}
void uuidIdentity() {
    const auto first=newAssetId(),second=newAssetId();require(isAssetUuid(first)&&isAssetUuid(second)&&first!=second,"New assets did not receive distinct UUIDs.");
    require(isEngineAssetId(canonicalAssetId("builtin:texture/normal"))&&!isAssetUuid("00000000-0000-0000-0000-000000000000"),"Builtin or nil UUID policy changed.");
}
void watcherSnapshots() {
    Fixture f;initializeContentStorage(f.root);
    const auto id=newAssetId();f.write("Art/Original.png","pixels");registerAssetPath(f.root,id,f.root/"Art/Original.png");
    const auto original=resolveAssetPath(f.root,id);const auto revision=contentStorageRevision(f.root);
    for(int i=0;i<100;++i)require(contentStorageReady(f.root)&&contentStorageRevision(f.root)==revision&&resolveAssetPath(f.root,id)==original&&assetIdAtPath(f.root,original)==id,"Warm storage lookups changed identity or revision.");
    require(!refreshContentStorage(f.root)&&contentStorageRevision(f.root)==revision,"An unchanged watcher notification invalidated the catalog.");
    const auto file=f.root/".crimson/asset-locations.json";const auto before=read(file);auto changed=Json::parse(before);
    changed["assets"][id]="Art/Relocated.png";f.write(".crimson/asset-locations.json",changed.dump(2));
    require(resolveAssetPath(f.root,id)==original,"Storage lookups unexpectedly perform implicit filesystem refreshes.");
    require(refreshContentStorage(f.root)&&contentStorageRevision(f.root)>revision&&resolveAssetPath(f.root,id)==f.root/"Art/Relocated.png"&&assetIdAtPath(f.root,f.root/"Art/Relocated.png")==id,"Explicit refresh missed an external location edit without a persisted revision increment.");
    const auto movedRevision=contentStorageRevision(f.root);
    require(!refreshContentStorage(f.root)&&contentStorageRevision(f.root)==movedRevision,"Repeated external notifications changed an equal snapshot revision.");
    changed["assets"][id]="Art/../../outside.png";f.write(".crimson/asset-locations.json",changed.dump(2));
    rejects([&]{refreshContentStorage(f.root);},"External traversal location passed snapshot validation.");
    require(contentStorageRevision(f.root)>movedRevision,"Blocked external locations did not advance invalidation state.");
    rejects([&]{resolveAssetPath(f.root,id);},"A rejected external index retained a usable stale path.");
    rejects([&]{storedAssetPaths(f.root);},"A rejected external index retained an enumerable stale catalog.");
    f.write(".crimson/asset-locations.json",before);require(refreshContentStorage(f.root)&&resolveAssetPath(f.root,id)==original,"A repaired external index did not recover the snapshot.");
}
void registrationGroups() {
    Fixture f;initializeContentStorage(f.root);
    const auto index=f.root/".crimson/asset-locations.json";
    const std::string first=newAssetId();const auto second=newAssetId();
    const std::array paths{StoredAssetPath{first,f.root/"Art/First.png"},StoredAssetPath{second,f.root/"Art/Second.png"}};
    const auto before=contentStorageRevision(f.root);registerAssetPaths(f.root,paths);
    require(contentStorageRevision(f.root)==before+1,"A registration group published more than one index revision.");
    require(resolveAssetPath(f.root,first)==paths[0].file&&resolveAssetPath(f.root,canonicalAssetId(first))==paths[0].file&&
        resolveAssetPath(f.root,second)==paths[1].file&&assetIdAtPath(f.root,paths[0].file)==canonicalAssetId(first)&&legacyAssetId(f.root,first)=="Art/First.png",
        "A registration group lost canonical identities, reverse lookup or source path provenance.");
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
    const std::array duplicateFile{StoredAssetPath{pending,f.root/"Art/Third.png"},StoredAssetPath{other,f.root/"Art/Third.png"}};
    rejects([&]{registerAssetPaths(f.root,duplicateFile);},"Two identities in one registration group claimed the same file.");unchanged();
    const std::array existingFile{StoredAssetPath{pending,f.root/"Art/Third.png"},StoredAssetPath{second,paths[0].file}};
    rejects([&]{registerAssetPaths(f.root,existingFile);},"A registration group claimed another asset's existing location.");unchanged();
    const std::array duplicateIdentity{StoredAssetPath{pending,f.root/"Art/Third.png"},StoredAssetPath{pending,f.root/"Art/Fourth.png"}};
    rejects([&]{registerAssetPaths(f.root,duplicateIdentity);},"One identity claimed conflicting files in a registration group.");unchanged();
    const std::array protectedFile{StoredAssetPath{pending,f.root/"Art/Third.png"},StoredAssetPath{other,f.root/".crimson/Unprotected.png"}};
    rejects([&]{registerAssetPaths(f.root,protectedFile);},"A registration group assigned an unprotected identity to Engine content.");unchanged();
    const std::array outside{StoredAssetPath{pending,f.root/"Art/Third.png"},StoredAssetPath{other,f.root/"../Outside.png"}};
    rejects([&]{registerAssetPaths(f.root,outside);},"A registration group accepted a location outside its project.");unchanged();
    const std::array emptyIdentity{StoredAssetPath{pending,f.root/"Art/Third.png"},StoredAssetPath{{},f.root/"Art/Fourth.png"}};
    rejects([&]{registerAssetPaths(f.root,emptyIdentity);},"A registration group accepted an empty identity.");unchanged();
}
void resolutionSnapshots() {
    Fixture f;initializeContentStorage(f.root);
    const std::array first{AssetEntry{newAssetId(),"First",AssetKind::Texture}};
    const std::array second{AssetEntry{newAssetId(),"Second",AssetKind::Texture}};
    f.write("First.png","first");f.write("Second.png","second");registerAssetPath(f.root,first[0].id,f.root/"First.png");registerAssetPath(f.root,second[0].id,f.root/"Second.png");AssetReferenceManager policy(f.root);
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
    f.write(".crimson/references.json","{broken JSON");AssetReferenceManager invalid(f.root);const auto blocked=invalid.resolutionSnapshot();
    require(!blocked->valid()&&blocked->status()==invalid.status()&&blocked->revision()==invalid.revision(),
        "A resolution snapshot lost invalid-policy state or its diagnostic.");
}
}
int main(){try{uuidIdentity();projectFolderAndMoves();interruptedMoveRecovery();watcherSnapshots();registrationGroups();resolutionSnapshots();std::cout<<"Content storage checks passed.\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
