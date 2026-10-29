#include "ContentStorage.h"
#include "AtomicFile.h"
#include "resources/Paths.h"
#include "resources/AssetId.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace crimson {
namespace {
namespace fs = std::filesystem;
using Json = nlohmann::json;
// Filesystem operations and their journals are serialized, including worker
// import publication. There is no process-global asset registry.
std::recursive_mutex storageMutex;
std::string utf8(const fs::path& p) { const auto s=p.generic_u8string();return {reinterpret_cast<const char*>(s.data()),s.size()}; }
std::string lower(std::string s) {for(auto& c:s)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return s;}
fs::path normalized(const fs::path& p) {return fs::weakly_canonical(fs::absolute(p));}
bool inside(const fs::path& p,const fs::path& base) {
    const auto r=p.lexically_relative(base);return !r.empty()&&!r.has_root_path()&&*r.begin()!="..";
}
fs::path checkedRelative(const fs::path& root,const fs::path& value) {
    if(value.empty()||value.has_root_path())throw std::runtime_error("Project file location must be project-relative.");
    for(const auto& part:value)if(part==".."||part==".")throw std::runtime_error("Project file location cannot traverse directories.");
    const auto base=normalized(root),result=normalized(base/value);
    if(!inside(result,base))throw std::runtime_error("Project file location escapes the project directory.");
    return result;
}
fs::path indexFile(const fs::path& root){return checkedRelative(root,".crimson/asset-locations.json");}
fs::path browserFile(const fs::path& root){return checkedRelative(root,".crimson/browser.json");}
std::string read(const fs::path& p){std::ifstream in(p,std::ios::binary);if(!in)throw std::runtime_error("Cannot read content metadata: "+utf8(p));return {std::istreambuf_iterator<char>(in),{}};}
struct StorageSnapshot {
    fs::path root;
    Json value;
    bool ready{};
    std::uint64_t revision{};
    std::string failure;
    std::unordered_map<std::string,fs::path> paths;
    std::unordered_map<std::string,std::string> reverse,legacy;
    std::unordered_map<std::string,fs::path> fallbacks;
    std::vector<StoredAssetPath> entries;
};
std::map<fs::path,StorageSnapshot> snapshots;
fs::path snapshotKey(const fs::path& root){return (root.is_absolute()?root:fs::absolute(root)).lexically_normal();}
StorageSnapshot makeSnapshot(const fs::path& root,Json value,bool ready,bool validate) {
    StorageSnapshot result;result.root=validate?normalized(root):root;result.ready=ready;result.value=std::move(value);
    if(result.value.at("version")!=2||!result.value.at("assets").is_object())throw std::runtime_error("Unsupported content location index.");
    result.revision=result.value.value("revision",std::uint64_t{});
    result.entries.reserve(result.value.at("assets").size());
    for(auto it=result.value.at("assets").begin();it!=result.value.at("assets").end();++it) {
        auto relative=pathFromUtf8(it.value().get<std::string>());
        const auto file=validate?checkedRelative(result.root,relative):(result.root/relative).lexically_normal();
        if(validate)it.value()=utf8(file.lexically_relative(result.root));
        result.paths.emplace(it.key(),file);result.reverse.emplace(lower(utf8(file)),it.key());result.entries.push_back({it.key(),file});
    }
    if(result.value.contains("aliases"))for(auto it=result.value["aliases"].begin();it!=result.value["aliases"].end();++it)
        result.legacy.try_emplace(it.value().get<std::string>(),it.key());
    return result;
}
bool reloadSnapshot(const fs::path& root) {
    const auto key=snapshotKey(root);auto found=snapshots.find(key);
    try {
        const auto base=normalized(root),file=indexFile(base);const bool ready=fs::is_regular_file(file);
        auto next=makeSnapshot(base,ready?Json::parse(read(file)):Json{{"version",2},{"revision",0},{"assets",Json::object()}},ready,true);
        const bool changed=found!=snapshots.end()&&(found->second.failure.size()||found->second.ready!=next.ready||found->second.value!=next.value);
        if(found!=snapshots.end())next.revision=changed?std::max(next.revision,found->second.revision+1):found->second.revision;
        snapshots[key]=std::move(next);return changed;
    }catch(const std::exception& error) {
        StorageSnapshot blocked;blocked.root=key;blocked.failure=error.what();blocked.ready=found!=snapshots.end()&&found->second.ready;
        blocked.revision=found==snapshots.end()?1:found->second.revision+1;snapshots[key]=std::move(blocked);throw;
    }
}
StorageSnapshot& snapshot(const fs::path& root) {
    const auto key=snapshotKey(root);auto found=snapshots.find(key);
    if(found==snapshots.end()){reloadSnapshot(root);found=snapshots.find(key);}
    return found->second;
}
const Json& index(const fs::path& root) {
    const auto& current=snapshot(root);if(!current.failure.empty())throw std::runtime_error(current.failure);return current.value;
}
void writeIndex(const fs::path& root,Json value) {
    auto& previous=snapshot(root);const auto base=previous.root;
    auto next=makeSnapshot(base,std::move(value),true,false);
    next.revision=std::max(next.revision,previous.revision+(previous.value!=next.value||!previous.ready||!previous.failure.empty()));
    writeFileAtomically(indexFile(root),next.value.dump(2)+"\n");snapshots[snapshotKey(root)]=std::move(next);
}
void removeOwned(const fs::path& root,const fs::path& p) {
    const auto relative=normalized(p).lexically_relative(normalized(root));
    const auto checked=checkedRelative(root,relative);if(checked==normalized(root))throw std::runtime_error("Cannot remove the project root.");
    fs::remove_all(checked);
}
void restoreFile(const fs::path& file,const Json& value) {if(value.is_null()){std::error_code error;fs::remove(file,error);if(error)throw std::runtime_error(error.message());}else writeFileAtomically(file,value.get<std::string>());}
void recoverMoves(const fs::path& root) {
    const auto journalFile=root/".crimson/content-move.json";
    if(!fs::exists(journalFile))return;
    const auto journal=Json::parse(read(journalFile));if(journal.at("version")!=1)throw std::runtime_error("Unsupported content move journal.");
    if(!journal.value("committed",false)) {
        const auto& moves=journal.at("moves");
        for(auto it=moves.rbegin();it!=moves.rend();++it) {
            const auto from=checkedRelative(root,pathFromUtf8(it->at("from").get<std::string>()));
            const auto to=checkedRelative(root,pathFromUtf8(it->at("to").get<std::string>()));
            if(it->value("copy",false)){if(fs::exists(to))removeOwned(root,to);}
            else if(!fs::exists(from)&&fs::exists(to)){fs::create_directories(from.parent_path());fs::rename(to,from);}
        }
        restoreFile(indexFile(root),journal.at("indexBefore"));restoreFile(browserFile(root),journal.at("browserBefore"));reloadSnapshot(root);
    }
    fs::remove(journalFile);
}
void publishMoves(const fs::path& root,std::span<const ContentPathMove> moves,Json next,const std::optional<std::string>& browser) {
    recoverMoves(root);
    Json plan=Json::array();std::set<std::string> destinations;
    for(const auto& move:moves) {
        const auto from=checkedRelative(root,normalized(move.from).lexically_relative(normalized(root)));
        const auto requestedTo=fs::absolute(move.to).lexically_normal();
        (void)checkedRelative(root,requestedTo.lexically_relative(normalized(root)));
        const auto to=requestedTo;
        if(from==to)continue;
        auto source=from;
        // A later copy may read a package already renamed by an earlier move.
        for(auto it=plan.rbegin();it!=plan.rend()&&!fs::exists(source);++it) {
            const auto earlierTo=checkedRelative(root,pathFromUtf8(it->at("to").get<std::string>()));
            if(source==earlierTo||inside(source,earlierTo)) {
                const auto earlierFrom=checkedRelative(root,pathFromUtf8(it->at("from").get<std::string>()));
                source=source==earlierTo?earlierFrom:earlierFrom/source.lexically_relative(earlierTo);
            }
        }
        if(!fs::exists(source))throw std::runtime_error("Project file to move no longer exists: "+utf8(from));
        bool vacated=false;
        for(const auto& earlier:plan)if(!earlier.value("copy",false)) {
            const auto earlierFrom=checkedRelative(root,pathFromUtf8(earlier.at("from").get<std::string>()));
            if(lower(utf8(earlierFrom))==lower(utf8(to)))vacated=true;
        }
        if((fs::exists(to)&&!vacated)||!destinations.insert(lower(utf8(to))).second)throw std::runtime_error("A file or folder already exists at the destination: "+utf8(to));
        if(inside(to,from))throw std::runtime_error("A folder cannot be moved into itself.");
        plan.push_back({{"from",utf8(from.lexically_relative(normalized(root)))},{"to",utf8(to.lexically_relative(normalized(root)))},{"copy",move.copy}});
    }
    Json journal{{"version",1},{"committed",false},{"moves",plan},
        {"indexBefore",fs::exists(indexFile(root))?Json(read(indexFile(root))):Json(nullptr)},
        {"browserBefore",fs::exists(browserFile(root))?Json(read(browserFile(root))):Json(nullptr)}};
    const auto journalFile=root/".crimson/content-move.json";
    writeFileAtomically(journalFile,journal.dump(2));
    try {
        for(const auto& move:plan) {
            const auto from=checkedRelative(root,pathFromUtf8(move.at("from").get<std::string>())),to=checkedRelative(root,pathFromUtf8(move.at("to").get<std::string>()));
            fs::create_directories(to.parent_path());
            if(move.at("copy").get<bool>()) {
                if(fs::is_directory(from))fs::copy(from,to,fs::copy_options::recursive);
                else fs::copy_file(from,to);
            }else fs::rename(from,to);
        }
        next["revision"]=next.value("revision",std::uint64_t{})+1;
        writeIndex(root,std::move(next));
        if(browser)writeFileAtomically(browserFile(root),*browser);
        journal["committed"]=true;writeFileAtomically(journalFile,journal.dump(2));
    }catch(...){recoverMoves(root);throw;}
    fs::remove(journalFile);
}
void defaultTextures(const fs::path& root,Json& locations) {
    for(const bool normal:{false,true}) {
        const auto relative=fs::path(".crimson/defaults/textures")/(normal?"NeutralNormal.tga":"DefaultGray.tga");
        const auto file=checkedRelative(root,relative);
        if(!fs::exists(file)) {
            // Uncompressed 1x1 BGRA TGA, readable by the ordinary image importer.
            std::string pixels(22,'\0');pixels[2]=2;pixels[12]=pixels[14]=1;pixels[16]=32;pixels[17]=40;
            pixels[18]=normal?static_cast<char>(255):static_cast<char>(128);pixels[19]=pixels[20]=static_cast<char>(128);pixels[21]=static_cast<char>(255);
            writeFileAtomically(file,pixels);
        }
        const auto legacy=normal?"builtin:texture/normal":"builtin:texture/gray";
        locations["assets"][canonicalAssetId(legacy)]=utf8(relative);locations["aliases"][legacy]=canonicalAssetId(legacy);
    }
    locations["assets"][canonicalAssetId("builtin:material/default")]=".crimson/defaults/materials/Default.crimson-material";
    locations["assets"][canonicalAssetId("builtin:material/particle")]=".crimson/defaults/materials/DefaultParticle.crimson-material";
    locations["assets"][canonicalAssetId("builtin:navigation-agent/default")]=".crimson/defaults/navigation/Default.crimson-agent-profile";
    locations["assets"][canonicalAssetId("builtin:gameplay-effect/player-defaults")]=".crimson/defaults/gameplay/PlayerDefaults.crimson-effect";
    locations["assets"][canonicalAssetId("builtin:gameplay-effect/enemy-defaults")]=".crimson/defaults/gameplay/EnemyDefaults.crimson-effect";
    locations["aliases"]["builtin:material/default"]=canonicalAssetId("builtin:material/default");
    locations["aliases"]["builtin:material/particle"]=canonicalAssetId("builtin:material/particle");
    fs::create_directories(checkedRelative(root,".crimson/defaults/materials"));
}
bool recoverFileTransactions(const fs::path& root) {
    const auto base=checkedRelative(root,".crimson/.resource-transactions");
    if(!fs::exists(base))return false;bool restored=false;
    for(const auto& entry:fs::directory_iterator(base))if(entry.is_directory()) {
        const auto directory=checkedRelative(root,entry.path().lexically_relative(root)),journalFile=directory/"journal.json";
        if(fs::is_regular_file(journalFile)) {
            const auto journal=Json::parse(read(journalFile));if(journal.at("version")!=1)throw std::runtime_error("Unsupported asset recovery journal.");
            if(!journal.value("committed",false))for(const auto& file:journal.at("files")) {
                const auto id=file.at("id").get<std::string>();const auto p=pathFromUtf8(id);
                const auto target=isAssetUuid(id)||id.starts_with("assets/")?resolveAssetPath(root,id):checkedRelative(root,p);
                if(isEngineAssetId(id))throw std::runtime_error("Asset recovery cannot overwrite private builtin defaults.");
                if(file.at("existed").get<bool>()) {
                    const auto backup=pathFromUtf8(file.at("backup").get<std::string>());
                    if(backup.has_root_path()||backup.filename()!=backup)throw std::runtime_error("Invalid asset recovery backup name.");
                    const auto source=checkedRelative(root,(directory/"before"/backup).lexically_relative(root));
                    copyFileAtomically(source,target);
                }else {std::error_code error;fs::remove(target,error);if(error)throw std::runtime_error(error.message());}
                restored=true;
            }
        }
        removeOwned(root,directory);
    }
    return restored;
}
} // namespace

std::string contentFilename(std::string name) {
    if(name.empty()||name=="."||name==".."||name.size()>240||name.back()=='.'||name.back()==' '||
       name.find_first_of("<>:\"/\\|?*")!=std::string::npos||std::any_of(name.begin(),name.end(),[](unsigned char c){return c<32||c==127;}))
        throw std::runtime_error("Enter a valid Windows file or folder name.");
    auto base=lower(name.substr(0,name.find('.')));
    if(base=="con"||base=="prn"||base=="aux"||base=="nul"||(base.size()==4&&(base.starts_with("com")||base.starts_with("lpt"))&&base[3]>='1'&&base[3]<='9'))
        throw std::runtime_error("That name is reserved by Windows.");
    return name;
}
bool contentStorageReady(const fs::path& root){std::lock_guard lock(storageMutex);return snapshot(root).ready;}
bool refreshContentStorage(const fs::path& root){std::lock_guard lock(storageMutex);return reloadSnapshot(root);}
void initializeContentStorage(const fs::path& rootValue) {
    std::lock_guard lock(storageMutex);const auto root=normalized(rootValue);
    if(!fs::is_directory(root))throw std::runtime_error("Choose an existing project folder.");
    fs::create_directories(checkedRelative(root,".crimson"));recoverMoves(root);reloadSnapshot(root);
    (void)recoverFileTransactions(root);
    auto locations=index(root);const auto before=locations;defaultTextures(root,locations);
    if(locations!=before||!contentStorageReady(root))writeIndex(root,std::move(locations));
}
fs::path resolveAssetPath(const fs::path& root,std::string_view id) {
    std::lock_guard lock(storageMutex);
    if(id.empty())throw std::runtime_error("Asset identity is empty.");
    (void)index(root);auto& current=snapshot(root);const auto identity=std::string(id);
    if(const auto it=current.paths.find(identity);it!=current.paths.end())return it->second;
    if(const auto it=current.fallbacks.find(identity);it!=current.fallbacks.end())return it->second;
    const auto key=canonicalAssetId(id);
    if(const auto it=current.paths.find(key);it!=current.paths.end())return it->second;
    const auto remember=[&](const fs::path& value){auto result=checkedRelative(current.root,value);current.fallbacks.emplace(identity,result);return result;};
    if(isAssetUuid(id))throw std::runtime_error("Unknown asset UUID.");
    const auto relative=pathFromUtf8(id);
    if(relative.has_root_path()||relative.empty()||id.find(':')!=std::string_view::npos)throw std::runtime_error("Unknown asset identity.");
    return remember(relative);
}

std::string assetIdAtPath(const fs::path& root,const fs::path& file) {
    std::lock_guard lock(storageMutex);(void)index(root);const auto& current=snapshot(root);
    const auto resolved=(file.is_absolute()?file:fs::absolute(file)).lexically_normal();
    if(const auto found=current.reverse.find(lower(utf8(resolved)));found!=current.reverse.end())return found->second;
    const auto relative=checkedRelative(current.root,resolved.lexically_relative(current.root)).lexically_relative(current.root);
    if(relative.empty()||relative.has_root_path()||*relative.begin()=="..")throw std::runtime_error("File is outside the game.");
    return canonicalAssetId(utf8(relative));
}
std::string legacyAssetId(const fs::path& root,std::string_view id) {
    if(!isAssetUuid(id))return std::string(id);
    std::lock_guard lock(storageMutex);(void)index(root);const auto& current=snapshot(root);
    if(const auto found=current.paths.find(std::string(id));found!=current.paths.end())return utf8(found->second.lexically_relative(current.root));
    return std::string(id);
}

std::vector<StoredAssetPath> storedAssetPaths(const fs::path& root) {
    std::lock_guard lock(storageMutex);(void)index(root);return snapshot(root).entries;
}
void registerAssetPaths(const fs::path& root,std::span<const StoredAssetPath> paths) {
    std::lock_guard lock(storageMutex);if(paths.empty())return;
    auto j=index(root);const auto base=normalized(root);bool changed=false;
    std::unordered_map<std::string,std::string> assignments;assignments.reserve(paths.size());
    for(const auto& entry:paths) {
        if(entry.id.empty())throw std::runtime_error("Cannot register an empty asset identity.");
        const auto relative=normalized(entry.file).lexically_relative(base);(void)checkedRelative(root,relative);
        if(lower(utf8(*relative.begin()))==".crimson"&&!isEngineAssetId(entry.id))throw std::runtime_error("Authored assets cannot own private project metadata.");
        const auto key=canonicalAssetId(entry.id),value=utf8(relative);
        const auto [assignment,inserted]=assignments.emplace(key,value);
        if(!inserted&&assignment->second!=value)throw std::runtime_error("One asset identity cannot register multiple files in the same group.");
        if(j["assets"].contains(key)&&j["assets"][key]==value)continue;
        j["assets"][key]=value;if(key!=entry.id)j["aliases"][entry.id]=key;changed=true;
    }
    if(!changed)return;
    std::unordered_map<std::string,std::string> owners;owners.reserve(j["assets"].size());
    for(auto it=j["assets"].begin();it!=j["assets"].end();++it)
        if(!owners.emplace(lower(it.value().get<std::string>()),it.key()).second)throw std::runtime_error("Two asset identities cannot own the same file.");
    j["revision"]=j.value("revision",std::uint64_t{})+1;writeIndex(root,std::move(j));
}
void registerAssetPath(const fs::path& root,const std::string& id,const fs::path& file) {
    const StoredAssetPath entry{id,file};registerAssetPaths(root,std::span(&entry,1));
}
std::string prepareAssetPathRegistration(const fs::path& root,std::span<const StoredAssetPath> paths,std::span<const std::string> removedIds) {
    std::lock_guard lock(storageMutex);auto j=index(root);const auto base=normalized(root);bool changed=false;
    for(const auto& id:removedIds) {
        if(!isAssetUuid(id)||isEngineAssetId(id))throw std::runtime_error("Asset location removal requires a managed UUID.");
        changed=j["assets"].erase(id)!=0||changed;
    }
    std::unordered_map<std::string,std::string> assignments;
    for(const auto& entry:paths) {
        if(!isAssetUuid(entry.id))throw std::runtime_error("Asset registration requires a UUID.");
        const auto relative=normalized(entry.file).lexically_relative(base);(void)checkedRelative(root,relative);
        if(lower(utf8(*relative.begin()))==".crimson"&&!isEngineAssetId(entry.id))throw std::runtime_error("Authored assets cannot own private project metadata.");
        const auto value=utf8(relative);const auto [where,added]=assignments.emplace(entry.id,value);
        if(!added&&where->second!=value)throw std::runtime_error("One asset identity cannot register multiple files.");
        if(j["assets"].contains(entry.id)&&j["assets"][entry.id]==value)continue;
        j["assets"][entry.id]=value;changed=true;
    }
    std::unordered_map<std::string,std::string> owners;
    for(auto it=j["assets"].begin();it!=j["assets"].end();++it)
        if(!owners.emplace(lower(it.value().get<std::string>()),it.key()).second)throw std::runtime_error("Two asset identities cannot own the same file.");
    if(changed)j["revision"]=j.value("revision",std::uint64_t{})+1;
    return j.dump(2)+'\n';
}
std::uint64_t contentStorageRevision(const fs::path& root){std::lock_guard lock(storageMutex);return snapshot(root).revision;}
std::string assetPathExtension(const fs::path& file){return lower(utf8(file.extension()));}
fs::path checkedContentPath(const fs::path& root,const fs::path& file) {
    const auto relative=file.is_absolute()?normalized(file).lexically_relative(normalized(root)):file;
    const auto result=checkedRelative(root,relative);const auto first=lower(utf8(*relative.begin()));if(first==".crimson"||first==".git")throw std::runtime_error("Private project metadata is protected.");
    for(const auto& part:relative)(void)contentFilename(utf8(part));return result;
}
void relocateContentPaths(const fs::path& root,std::span<const ContentPathMove> moves,const std::optional<std::string>& browser) {
    std::lock_guard lock(storageMutex);auto j=index(root);const auto base=normalized(root);
    std::vector<ContentPathMove> effective;
    for(const auto& move:moves) {
        const auto from=checkedContentPath(root,move.from);(void)checkedContentPath(root,move.to);
        const auto to=fs::absolute(move.to).lexically_normal();
        if(!move.copy&&from!=to&&lower(utf8(from))==lower(utf8(to))) {
            const auto temporary=from.parent_path()/pathFromUtf8(".rename-"+newAssetId());effective.push_back({from,temporary,false});effective.push_back({temporary,to,false});
        }else effective.push_back({from,to,move.copy});
    }
    for(const auto& move:effective) {
        const auto from=fs::absolute(move.from).lexically_normal(),to=fs::absolute(move.to).lexically_normal();
        for(auto it=j["assets"].begin();it!=j["assets"].end();++it) {
            const auto current=base/pathFromUtf8(it.value().get<std::string>());
            if(current==from||inside(current,from))it.value()=utf8((current==from?to:to/current.lexically_relative(from)).lexically_relative(normalized(root)));
        }
    }
    std::set<std::string> destinations;
    for(auto it=j["assets"].begin();it!=j["assets"].end();++it)
        if(!destinations.insert(lower(it.value().get<std::string>())).second)throw std::runtime_error("The destination is reserved by another asset identity.");
    publishMoves(root,effective,std::move(j),browser);
}

} // namespace crimson
