#include "ContentStorage.h"
#include "AtomicFile.h"
#include "QuaderSceneBundle.h"
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
    if(value.empty()||value.has_root_path())throw std::runtime_error("Content location must be project-relative.");
    for(const auto& part:value)if(part==".."||part==".")throw std::runtime_error("Content location cannot traverse directories.");
    const auto base=normalized(root),result=normalized(base/value);
    if(!inside(result,base))throw std::runtime_error("Content location escapes the game directory.");
    // Never accept redirected Content/Engine/private roots outside this game.
    const auto first=utf8(*value.begin());
    if(first!="Content"&&first!="Engine"&&first!=".crimson"&&first!="assets")throw std::runtime_error("Unknown content storage root.");
    if(!inside(result,base/pathFromUtf8(first)))throw std::runtime_error("Content root is redirected outside its owner.");
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
    if(result.value.at("version")!=1||!result.value.at("assets").is_object())throw std::runtime_error("Unsupported content location index.");
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
        auto next=makeSnapshot(base,ready?Json::parse(read(file)):Json{{"version",1},{"revision",0},{"assets",Json::object()}},ready,true);
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
bool image(const fs::path& p){const auto e=lower(utf8(p.extension()));return e==".png"||e==".jpg"||e==".jpeg"||e==".bmp"||e==".tga"||e==".hdr";}
std::string safeName(std::string name) {
    for(auto& c:name)if(static_cast<unsigned char>(c)<32||std::string_view("<>:\"/\\|?*").find(c)!=std::string_view::npos)c='_';
    while(!name.empty()&&(name.back()=='.'||name.back()==' '))name.pop_back();
    if(name.empty()||name=="."||name=="..")name="Asset";
    if(name.size()>160){std::size_t end=160;while(end>0&&(static_cast<unsigned char>(name[end])&0xc0)==0x80)--end;name.resize(end);}
    while(!name.empty()&&(name.back()=='.'||name.back()==' '))name.pop_back();
    try{return contentFilename(name);}catch(...){return "_"+name;}
}
std::string safeFileName(std::string name) {
    for(auto& c:name)if(static_cast<unsigned char>(c)<32||std::string_view("<>:\"/\\|?*").find(c)!=std::string_view::npos)c='_';
    const auto extension=utf8(pathFromUtf8(name).extension());
    if(extension.empty()||extension.size()>32)return safeName(std::move(name));
    return safeName(name.substr(0,name.size()-extension.size()))+extension;
}
fs::path uniquePath(const fs::path& root,fs::path candidate,std::set<std::string>& reserved) {
    const auto fit=[&](fs::path p,const std::string& suffix=std::string{}) {
        auto stem=utf8(p.stem());const auto extension=utf8(p.extension());const auto parent=p.parent_path();
        p=parent/pathFromUtf8(stem+suffix+extension);
        // Leave space for sibling atomic-save/restore names on Windows. The
        // authored display name remains unchanged inside the native asset.
        while((root/p).native().size()>220&&stem.size()>8) {
            auto end=stem.size()-1;while(end>0&&(static_cast<unsigned char>(stem[end])&0xc0)==0x80)--end;stem.resize(end);
            while(!stem.empty()&&(stem.back()=='.'||stem.back()==' '))stem.pop_back();
            p=parent/pathFromUtf8(stem+suffix+extension);
        }
        if((root/p).native().size()>220)throw std::runtime_error("The Content directory hierarchy is too long for safe Windows saves.");
        return p;
    };
    candidate=fit(std::move(candidate));const auto original=candidate;unsigned suffix=2;
    while(fs::exists(root/candidate)||!reserved.insert(lower(utf8(candidate))).second)
        candidate=fit(original," "+std::to_string(suffix++));
    return candidate;
}
void removeOwned(const fs::path& root,const fs::path& p) {
    const auto relative=normalized(p).lexically_relative(normalized(root));
    const auto checked=checkedRelative(root,relative);if(checked==normalized(root))throw std::runtime_error("Cannot remove the game root.");
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
        if(!fs::exists(source))throw std::runtime_error("Content file to move no longer exists: "+utf8(from));
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
        const auto relative=fs::path("Engine/textures")/(normal?"NeutralNormal.tga":"DefaultGray.tga");
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
    locations["assets"][canonicalAssetId("builtin:material/default")]="Engine/materials/Default.crimson-material";
    locations["assets"][canonicalAssetId("builtin:material/particle")]="Engine/materials/DefaultParticle.crimson-material";
    locations["assets"][canonicalAssetId("builtin:navigation-agent/default")]="Engine/navigation/Default.crimson-agent-profile";
    locations["assets"][canonicalAssetId("builtin:gameplay-effect/player-defaults")]="Engine/gameplay/PlayerDefaults.crimson-effect";
    locations["assets"][canonicalAssetId("builtin:gameplay-effect/enemy-defaults")]="Engine/gameplay/EnemyDefaults.crimson-effect";
    locations["aliases"]["builtin:material/default"]=canonicalAssetId("builtin:material/default");
    locations["aliases"]["builtin:material/particle"]=canonicalAssetId("builtin:material/particle");
    fs::create_directories(checkedRelative(root,"Engine/materials"));
}
Json uuidIndex(Json locations) {
    Json converted=Json::object(),aliases=locations.value("aliases",Json::object());
    for(auto it=locations["assets"].begin();it!=locations["assets"].end();++it) {
        const auto uuid=canonicalAssetId(it.key());converted[uuid]=it.value();if(uuid!=it.key())aliases[it.key()]=uuid;
    }
    locations["assets"]=std::move(converted);locations["aliases"]=std::move(aliases);locations["uuidReferences"]=true;return locations;
}
bool oldReference(std::string_view value) {
    if(value.starts_with("builtin:"))return true;
    if(!value.starts_with("assets/"))return false;
    const auto p=pathFromUtf8(value);if(p.has_root_path())return false;
    for(const auto& part:p)if(part==".."||part==".")return false;
    const auto suffix=lower(utf8(p.extension()));
    return image(p)||suffix==".crimson-material"||suffix==".crimson-particle"||suffix==".crimson-agent-profile"||suffix==".crimson-effect"||suffix==".crimson-scene"||p.filename()=="manifest.json";
}
void convertReferences(Json& value) {
    if(value.is_string()){const auto text=value.get<std::string>();if(oldReference(text))value=canonicalAssetId(text);}
    else if(value.is_array())for(auto& item:value)convertReferences(item);
    else if(value.is_object()) {
        Json result=Json::object();
        for(auto it=value.begin();it!=value.end();++it) {
            auto item=it.value();
            // User-authored display text and retained input paths are not refs.
            if(it.key()!="name"&&it.key()!="path"&&it.key()!="original"&&it.key()!="document")convertReferences(item);
            result[oldReference(it.key())?canonicalAssetId(it.key()):it.key()]=std::move(item);
        }
        value=std::move(result);
    }
}
void retainMissingReferences(const Json& value,Json& locations) {
    const auto retain=[&](const std::string& legacy) {
        if(!oldReference(legacy)||legacy.starts_with("builtin:"))return;
        const auto uuid=canonicalAssetId(legacy);
        if(locations["assets"].contains(legacy)||locations["assets"].contains(uuid))return;
        // Tombstones and unexpectedly missing dependencies still need location
        // and type provenance. Recording the absent path never recreates it.
        locations["assets"][legacy]=utf8(fs::path("Content")/pathFromUtf8(legacy).lexically_relative("assets"));
    };
    if(value.is_string())retain(value.get<std::string>());
    else if(value.is_array())for(const auto& item:value)retainMissingReferences(item,locations);
    else if(value.is_object())for(auto it=value.begin();it!=value.end();++it) {
        retain(it.key());if(it.key()!="name"&&it.key()!="path"&&it.key()!="original"&&it.key()!="document")retainMissingReferences(it.value(),locations);
    }
}
void migrateUuidReferences(const fs::path& root) {
    // This journal covers authored JSON and the index together. Source GLBs,
    // glTFs, image pixels and buffer bytes stay byte-for-byte intact; only the
    // explicit material-reference array in package manifests is converted.
    const auto transaction=root/".crimson/uuid-migration.json";
    if(fs::exists(transaction)) {
        const auto journal=Json::parse(read(transaction));
        if(!journal.value("committed",false))for(const auto& entry:journal.at("files"))restoreFile(checkedRelative(root,pathFromUtf8(entry.at("path").get<std::string>())),entry.at("before"));
        fs::remove(transaction);reloadSnapshot(root);
    }
    auto locations=index(root);if(locations.value("uuidReferences",false))return;
    Json files=Json::array();std::vector<std::pair<fs::path,std::string>> replacements;
    for(const auto& tree:{root/"Content",root/".crimson"})if(fs::exists(tree))for(auto it=fs::recursive_directory_iterator(tree);it!=fs::recursive_directory_iterator();++it) {
        if(tree==root/".crimson"&&it->is_directory()&&(it->path().filename()=="cache"||utf8(it->path().filename()).starts_with('.'))){it.disable_recursion_pending();continue;}
        if(!it->is_regular_file())continue;const auto path=it->path();const auto ext=utf8(path.extension()),name=utf8(path.filename());
        const bool native=ext==".crimson-material"||ext==".crimson-particle"||ext==".crimson-agent-profile"||ext==".crimson-effect"||ext==".crimson-scene";
        const bool package=tree==root/"Content"&&name=="manifest.json";
        const bool settings=path.parent_path()==root/".crimson"&&ext==".json"&&name!="asset-locations.json"&&name!="legacy-browser.backup.json"&&name!="content-move.json"&&name!="uuid-migration.json"&&name!="collision-layers.json";
        if(!native&&!settings&&!package)continue;
        const auto before=read(path);Json json;
        try{json=Json::parse(before);}catch(...){continue;}const auto previous=json;
        if(package) {
            if(!json.contains("materialAssets"))continue;
            retainMissingReferences(json["materialAssets"],locations);convertReferences(json["materialAssets"]);
        }else {retainMissingReferences(json,locations);convertReferences(json);}
        if(json==previous)continue;
        files.push_back({{"path",utf8(path.lexically_relative(root))},{"before",before}});replacements.emplace_back(path,json.dump(2)+"\n");
    }
    files.push_back({{"path",".crimson/asset-locations.json"},{"before",read(indexFile(root))}});
    Json journal{{"version",1},{"committed",false},{"files",files}};writeFileAtomically(transaction,journal.dump(2));
    try {
        for(const auto& [path,contents]:replacements)writeFileAtomically(path,contents);
        auto next=uuidIndex(std::move(locations));next["revision"]=next.value("revision",std::uint64_t{})+1;writeIndex(root,std::move(next));
        journal["committed"]=true;writeFileAtomically(transaction,journal.dump(2));
    }catch(...){for(const auto& entry:files)restoreFile(checkedRelative(root,pathFromUtf8(entry.at("path").get<std::string>())),entry.at("before"));fs::remove(transaction);reloadSnapshot(root);throw;}
    fs::remove(transaction);
}
bool recoverFileTransactions(const fs::path& root) {
    const auto base=contentStorageReady(root)?checkedRelative(root,".crimson/.resource-transactions"):checkedRelative(root,"assets/.resource-transactions");
    if(!fs::exists(base))return false;bool restored=false;
    for(const auto& entry:fs::directory_iterator(base))if(entry.is_directory()) {
        const auto directory=checkedRelative(root,entry.path().lexically_relative(root)),journalFile=directory/"journal.json";
        if(fs::is_regular_file(journalFile)) {
            const auto journal=Json::parse(read(journalFile));if(journal.at("version")!=1)throw std::runtime_error("Unsupported asset recovery journal.");
            if(!journal.value("committed",false))for(const auto& file:journal.at("files")) {
                const auto id=file.at("id").get<std::string>();const auto p=pathFromUtf8(id);
                const auto target=isAssetUuid(id)||id.starts_with("assets/")?resolveAssetPath(root,id):checkedRelative(root,p);
                if(*target.lexically_relative(root).begin()=="Engine")throw std::runtime_error("Asset recovery cannot overwrite protected Engine content.");
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
void migrate(const fs::path& root) {
    Json browser{{"version",2},{"folders",Json::array()},{"assets",Json::object()}};
    Json legacy{{"folders",Json::array()},{"assets",Json::object()}};
    if(fs::exists(root/"assets/browser.json")) {
        legacy=Json::parse(read(root/"assets/browser.json"));
        if(legacy.at("version")!=1)throw std::runtime_error("Cannot migrate unsupported browser metadata.");
    }
    std::map<std::string,fs::path> folders{{"", "Content"},{"folder:content","Content"},{"folder:engine","Engine"}};
    std::set<std::string> reserved{"content","engine"};
    Json pending=legacy.at("folders");
    for(std::size_t pass=0;!pending.empty();++pass) {
        if(pass>legacy.at("folders").size())throw std::runtime_error("Cannot migrate cyclic browser folders.");
        for(auto it=pending.begin();it!=pending.end();) {
            const auto parent=it->value("parent",std::string{});if(!folders.contains(parent)){++it;continue;}
            const auto name=safeName(it->at("name").get<std::string>());
            const auto destination=uniquePath(root,folders.at(parent)/pathFromUtf8(name),reserved);
            const auto id=it->at("id").get<std::string>();folders[id]=destination;
            browser["folders"].push_back({{"id",id},{"parent",parent.empty()?"folder:content":parent},{"name",utf8(destination.filename())},{"path",utf8(destination)}});
            it=pending.erase(it);
        }
    }
    browser["folders"].push_back({{"id","folder:content"},{"parent",""},{"name","Content"},{"path","Content"}});
    browser["folders"].push_back({{"id","folder:engine"},{"parent",""},{"name","Engine"},{"path","Engine"}});
    for(auto it=legacy.at("assets").begin();it!=legacy.at("assets").end();++it) {
        auto entry=it.value();if(entry.value("folder",std::string{}).empty())entry["folder"]="folder:content";
        browser["assets"][it.key()]=std::move(entry);
    }
    const auto destinationFolder=[&](const std::string& id,const std::string& fallback=std::string{}) {
        auto folder=fallback;
        if(legacy.at("assets").contains(id))folder=legacy.at("assets").at(id).value("folder",std::string{});
        if(!folders.contains(folder))throw std::runtime_error("Asset references a missing browser folder during migration.");
        return folders.at(folder);
    };
    Json locations=index(root);std::vector<ContentPathMove> moves;
    const auto addFile=[&](const std::string& id,const fs::path& source,const fs::path& destination,bool copy=false) {
        locations["assets"][id]=utf8(destination);moves.push_back({source,root/destination,copy});
    };
    const auto models=root/"assets/models";
    if(fs::exists(models))for(const auto& entry:fs::directory_iterator(models))if(entry.is_directory()&&fs::is_regular_file(entry.path()/"manifest.json")) {
        const auto manifest=Json::parse(read(entry.path()/"manifest.json"));
        const auto id=utf8((entry.path()/"manifest.json").lexically_relative(root));
        const auto token=utf8(entry.path().filename());const auto label=safeName(manifest.value("name",token));
        const auto package=uniquePath(root,destinationFolder(id)/pathFromUtf8(label+"-"+token.substr(token.size()>8?token.size()-8:0)+".crimson-model"),reserved);
        moves.push_back({entry.path(),root/package,false});
        for(const auto& member:fs::recursive_directory_iterator(entry.path()))if(member.is_regular_file()) {
            const auto memberId=utf8(member.path().lexically_relative(root));const auto relative=member.path().lexically_relative(entry.path());
            locations["assets"][memberId]=utf8(package/relative);
            if(image(member.path())) {
                // Keep package-relative import inputs unchanged. The visible
                // image has an independent file and can move with its own card.
                const auto folder=destinationFolder(memberId,legacy.at("assets").contains(id)?legacy.at("assets").at(id).value("folder",std::string{}):std::string{});
                const auto target=uniquePath(root,folder/pathFromUtf8(safeFileName(label+"_"+utf8(member.path().filename()))),reserved);
                moves.push_back({root/package/relative,root/target,true});locations["assets"][memberId]=utf8(target);
            }
        }
    }
    for(const auto& type:{"textures","materials","scenes","particles"}) {
        const auto directory=root/"assets"/type;if(!fs::exists(directory))continue;
        for(const auto& entry:fs::recursive_directory_iterator(directory))if(entry.is_regular_file()) {
            const auto id=utf8(entry.path().lexically_relative(root));auto name=utf8(entry.path().filename());
            const auto ext=utf8(entry.path().extension());
            if(legacy.at("assets").contains(id)&&!legacy.at("assets").at(id).value("name",std::string{}).empty())name=legacy.at("assets").at(id).at("name").get<std::string>();
            else if(std::string_view(type)!="textures")try{const auto j=Json::parse(read(entry.path()));name=j.value("name",utf8(entry.path().stem()));}catch(...){/* Preserve damaged assets verbatim. */}
            if(!name.ends_with(ext))name+=ext;
            const auto target=uniquePath(root,destinationFolder(id)/pathFromUtf8(safeFileName(name)),reserved);addFile(id,entry.path(),target);
        }
    }
    // Settings, import receipts, journals and disposable caches are private game
    // metadata. Branding remains an engine build source under assets/branding.
    if(fs::exists(root/"assets"))for(const auto& entry:fs::directory_iterator(root/"assets")) {
        const auto name=utf8(entry.path().filename());
        if(name=="branding"||name=="browser.json"||name=="models"||name=="textures"||name=="materials"||name=="scenes"||name=="particles")continue;
        const auto target=root/".crimson"/entry.path().filename();
        if(!fs::exists(target))moves.push_back({entry.path(),target,false});
        else throw std::runtime_error("Private metadata destination already exists: "+utf8(target));
    }
    for(const auto& [id,p]:folders){(void)id;fs::create_directories(checkedRelative(root,p));}
    defaultTextures(root,locations);
    // Backup legacy organization as migration evidence, never as runtime data.
    if(fs::exists(root/"assets/browser.json"))writeFileAtomically(root/".crimson/legacy-browser.backup.json",read(root/"assets/browser.json"));
    publishMoves(root,moves,std::move(locations),browser.dump(2)+"\n");
    if(fs::exists(root/"assets/browser.json"))fs::remove(root/"assets/browser.json");
    // Empty old directories only; unexpected files are preserved for diagnosis.
    for(const auto& type:{"textures","materials","scenes","particles","models"}) {
        const auto directory=root/"assets"/type;
        if(fs::exists(directory)&&fs::is_empty(directory))fs::remove(directory);
    }
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
    fs::create_directories(checkedRelative(root,".crimson"));recoverMoves(root);reloadSnapshot(root);
    const bool restored=recoverFileTransactions(root);
    fs::create_directories(checkedRelative(root,"Content"));fs::create_directories(checkedRelative(root,"Engine"));
    if(!contentStorageReady(root))migrate(root);
    else {auto j=index(root);const auto before=j;defaultTextures(root,j);if(j!=before)writeIndex(root,std::move(j));}
    if(restored){auto j=index(root);j["uuidReferences"]=false;writeIndex(root,std::move(j));}
    migrateUuidReferences(root);
    initializeQuaderSceneBundles(root);
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
    if(isAssetUuid(id)&&!current.ready&&fs::exists(current.root/"assets")) {
        for(const auto& entry:fs::recursive_directory_iterator(root/"assets"))if(entry.is_regular_file()) {
            const auto legacy=utf8(entry.path().lexically_relative(root));if(canonicalAssetId(legacy)==key)return remember(pathFromUtf8(legacy));
        }
    }
    const auto relative=pathFromUtf8(id);
    if(relative.has_root_path()||relative.empty()||id.find(':')!=std::string_view::npos)throw std::runtime_error("Unknown asset identity.");
    for(const auto& part:relative)if(part==".."||part==".")throw std::runtime_error("Asset identity cannot traverse directories.");
    if(*relative.begin()!="assets")throw std::runtime_error("Unknown managed asset identity.");
    if(!current.ready)return remember(relative);
    auto suffix=relative.lexically_relative("assets");
    if(suffix.empty()||suffix==".")return remember("Content");
    const auto first=utf8(*suffix.begin());
    if(first=="cache"||first.starts_with('.')||suffix.extension()==".json"&&suffix.parent_path().empty())return remember(fs::path(".crimson")/suffix);
    return remember(fs::path("Content")/suffix);
}
std::string assetIdAtPath(const fs::path& root,const fs::path& file) {
    std::lock_guard lock(storageMutex);(void)index(root);const auto& current=snapshot(root);
    const auto resolved=(file.is_absolute()?file:fs::absolute(file)).lexically_normal();
    if(const auto found=current.reverse.find(lower(utf8(resolved)));found!=current.reverse.end())return found->second;
    const auto relative=checkedRelative(current.root,resolved.lexically_relative(current.root)).lexically_relative(current.root);
    if(relative.empty()||relative.has_root_path()||*relative.begin()=="..")throw std::runtime_error("File is outside the game.");
    if(*relative.begin()=="assets")return contentStorageReady(root)?canonicalAssetId(utf8(relative)):utf8(relative);
    // Discovery assigns immutable identities; consumers register this result
    // before publishing it. Native authored files retain the ID in their JSON.
    if(*relative.begin()=="Content") {
        const auto suffix=relative.lexically_relative("Content");
        return canonicalAssetId("assets/"+utf8(suffix));
    }
    if(*relative.begin()==".crimson")return "assets/"+utf8(relative.lexically_relative(".crimson"));
    return {};
}
std::string legacyAssetId(const fs::path& root,std::string_view id) {
    if(!isAssetUuid(id))return std::string(id);
    std::lock_guard lock(storageMutex);(void)index(root);const auto& current=snapshot(root);
    if(const auto found=current.legacy.find(std::string(id));found!=current.legacy.end())return found->second;
    if(!contentStorageReady(root)&&fs::exists(root/"assets"))for(const auto& entry:fs::recursive_directory_iterator(root/"assets"))if(entry.is_regular_file()) {
        const auto legacy=utf8(entry.path().lexically_relative(root));if(canonicalAssetId(legacy)==id)return legacy;
    }
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
        if(*relative.begin()=="Engine"&&!isEngineAssetId(entry.id))throw std::runtime_error("Only protected engine identities can own Engine files.");
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
        if(*relative.begin()=="Engine"&&!isEngineAssetId(entry.id))throw std::runtime_error("Only protected identities can own Engine files.");
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
fs::path checkedContentPath(const fs::path& root,const fs::path& file) {
    const auto relative=file.is_absolute()?normalized(file).lexically_relative(normalized(root)):file;
    const auto result=checkedRelative(root,relative);if(*relative.begin()!="Content")throw std::runtime_error("Engine content is protected.");
    for(auto it=++relative.begin();it!=relative.end();++it)(void)contentFilename(utf8(*it));return result;
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

void initializeQuaderSceneBundles(const fs::path& rootValue) {
    std::lock_guard lock(storageMutex);const auto root=normalized(rootValue);
    // The storage initializer calls this only after its recovery/index boundary.
    // Do not call initializeContentStorage here: browser/store constructors may
    // independently revisit that boundary during ordinary project startup.
    (void)index(root);
    constexpr std::array<const char*,4> members{"manifest.json","model.gltf","source.gltf","buffers/0.bin"};
    const auto boundedRead=[&](const fs::path& file){
        if(!fs::is_regular_file(file)||fs::file_size(file)>std::uint64_t{2}*1024*1024*1024)
            throw std::runtime_error("Quader bundle input is missing or exceeds its supported size: "+utf8(file));
        return read(file);
    };
    std::set<std::string> deleted;
    const auto references=root/".crimson/references.json";
    if(fs::is_regular_file(references)) {
        // Invalid deletion metadata is never grounds for silently resurrecting
        // an asset, or for destructively changing its former package files.
        // AssetReferenceManager reports its existing read-only policy diagnostic
        // later; preserve that graceful startup without changing storage first.
        try {
            const auto policy=Json::parse(boundedRead(references));
            if(policy.at("version")!=1||!policy.at("deleted").is_array()||!policy.at("replacements").is_object())return;
            for(const auto& value:policy.at("deleted")) {
                const auto id=canonicalAssetId(value.get<std::string>());if(!isAssetUuid(id)||isEngineAssetId(id))return;deleted.insert(id);
            }
            for(auto it=policy.at("replacements").begin();it!=policy.at("replacements").end();++it) {
                if(!deleted.contains(canonicalAssetId(it.key()))||!it.value().is_string())return;
            }
        }catch(const std::exception&){return;}
    }
    struct Group {fs::path file;QuaderSceneBundle bundle;bool changed{};};
    struct Legacy {std::string bundleId;QuaderBundleModel model;std::array<fs::path,4> paths;};
    std::map<std::string,Group> groups;std::vector<fs::path> legacyManifests;
    const auto content=checkedRelative(root,"Content");
    if(fs::is_directory(content))for(auto it=fs::recursive_directory_iterator(content);it!=fs::recursive_directory_iterator{};++it) {
        if(it->is_symlink()) {if(it->is_directory())it.disable_recursion_pending();continue;}
        if(it->is_directory()&&fs::is_regular_file(it->path()/"manifest.json")) {
            legacyManifests.push_back(checkedContentPath(root,it->path()/"manifest.json"));it.disable_recursion_pending();continue;
        }
        if(!it->is_regular_file()||it->path().extension()!=".crimson-quader")continue;
        const auto file=checkedContentPath(root,it->path());auto bundle=decodeQuaderSceneBundle(boundedRead(file));
        const auto id=bundle.id;if(!groups.emplace(id,Group{file,std::move(bundle),false}).second)throw std::runtime_error("Two Quader scene bundles claim the same source identity.");
    }
    std::vector<Legacy> legacy;std::vector<fs::path> oldDirectories;
    for(const auto& file:legacyManifests) {
        const auto text=boundedRead(file);Json manifest;
        try{manifest=Json::parse(text);}catch(const std::exception&){continue;}
        if(manifest.value("format",std::string{})!="Quader"||!manifest.contains("quader"))continue;
        const auto& source=manifest.at("quader");if(source.value("collision",false))continue;
        const auto project=source.at("project").get<std::string>(),scene=source.at("scene").get<std::string>();
        const auto owner=quaderSceneBundleId(project,scene),modelId=canonicalAssetId(assetIdAtPath(root,file));
        if(deleted.contains(owner)||deleted.contains(modelId))continue;
        Legacy item;item.bundleId=owner;item.model.id=modelId;item.model.legacyManifest=legacyAssetId(root,modelId);
        if(!item.model.legacyManifest.starts_with("assets/"))item.model.legacyManifest="assets/"+utf8(file.lexically_relative(content));
        bool reserved=false;
        for(std::size_t i=0;i<members.size();++i){
            item.paths[i]=checkedContentPath(root,file.parent_path()/members[i]);item.model.memberIds[i]=canonicalAssetId(assetIdAtPath(root,item.paths[i]));reserved|=deleted.contains(item.model.memberIds[i]);
        }
        if(reserved)continue;
        item.model.memberIds[0]=modelId;item.model.manifest=text;item.model.gltf=boundedRead(item.paths[1]);
        if(boundedRead(item.paths[2])!=item.model.gltf)throw std::runtime_error("Quader package has distinct original geometry; its files were preserved: "+utf8(file.parent_path()));
        item.model.buffer=boundedRead(item.paths[3]);
        auto found=groups.find(owner);
        if(found==groups.end()) {
            QuaderSceneBundle bundle{owner,project,scene,"Quader Scene",{}};
            const auto destination=defaultQuaderSceneBundlePath(root,bundle);
            found=groups.emplace(owner,Group{destination,std::move(bundle),true}).first;
        }
        auto& group=found->second;const auto prior=std::find_if(group.bundle.models.begin(),group.bundle.models.end(),[&](const auto& model){return model.id==modelId;});
        if(prior!=group.bundle.models.end()) {
            if(prior->manifest!=item.model.manifest||prior->gltf!=item.model.gltf||prior->buffer!=item.model.buffer||prior->memberIds!=item.model.memberIds)
                throw std::runtime_error("Quader model identity has conflicting bundle/package bytes; both sources were preserved.");
        }else {group.bundle.models.push_back(item.model);group.changed=true;}
        oldDirectories.push_back(file.parent_path()/"buffers");oldDirectories.push_back(file.parent_path());legacy.push_back(std::move(item));
    }
    // Validate the entire publication before touching any authored source. An
    // unrelated file inside an old package stays in Content; only these four
    // proven import inputs participate in migration.
    std::set<std::string> allIds;
    for(auto& [id,group]:groups) {
        (void)id;std::sort(group.bundle.models.begin(),group.bundle.models.end(),[](const auto& a,const auto& b){return a.id<b.id;});
        (void)encodeQuaderSceneBundle(group.bundle);
        if(!allIds.insert(group.bundle.id).second)throw std::runtime_error("Duplicate Quader bundle identity.");
        for(const auto& model:group.bundle.models)for(const auto& member:model.memberIds)if(!allIds.insert(member).second)throw std::runtime_error("Multiple Quader bundles claim one immutable model member.");
    }
    const auto registerLocations=[&](Json& next,const Group& group) {
        next["assets"][group.bundle.id]=utf8(group.file.lexically_relative(root));
        for(const auto& model:group.bundle.models) {
            if(deleted.contains(model.id))continue;
            const auto directory=quaderBundleCacheDirectory(root,group.bundle.id,model.id);
            const auto legacyParent=pathFromUtf8(model.legacyManifest).parent_path();
            for(std::size_t i=0;i<members.size();++i) {
                if(deleted.contains(model.memberIds[i]))continue;
                next["assets"][model.memberIds[i]]=utf8((directory/members[i]).lexically_relative(root));
                if(!model.legacyManifest.empty())next["aliases"][utf8(legacyParent/members[i])]=model.memberIds[i];
            }
        }
    };
    if(!legacy.empty()) {
        const auto stage=checkedRelative(root,fs::path(".crimson")/pathFromUtf8(".staging-quader-bundle-"+newAssetId()));
        fs::create_directories(stage);std::vector<ContentPathMove> moves;auto next=index(root);
        try {
            for(auto& [id,group]:groups) {
                registerLocations(next,group);if(!group.changed)continue;
                const auto prepared=stage/pathFromUtf8(id+".crimson-quader");writeFileAtomically(prepared,encodeQuaderSceneBundle(group.bundle));
                if(fs::exists(group.file)) {
                    if(decodeQuaderSceneBundle(boundedRead(group.file)).id!=id)throw std::runtime_error("A different authored asset already owns the Quader bundle destination.");
                    moves.push_back({group.file,stage/"previous"/pathFromUtf8(id+".crimson-quader"),false});
                }
                moves.push_back({prepared,group.file,false});
            }
            for(const auto& item:legacy) {
                const auto destination=quaderBundleCacheDirectory(root,item.bundleId,item.model.id);
                for(std::size_t i=0;i<members.size();++i) {
                    const auto target=destination/members[i];
                    if(fs::exists(target))moves.push_back({target,stage/"previous-cache"/pathFromUtf8(item.model.id)/members[i],false});
                    moves.push_back({item.paths[i],target,false});
                }
            }
            publishMoves(root,moves,std::move(next),{});
            // The journal is durably committed before deleting retired duplicate
            // cache/bundle bytes. Its Content sources have been captured above.
            removeOwned(root,stage);
        }catch(...){if(!fs::exists(root/".crimson/content-move.json")){try{removeOwned(root,stage);}catch(...){/* Retain staging evidence if cleanup fails. */}}throw;}
        std::sort(oldDirectories.begin(),oldDirectories.end(),[](const auto& a,const auto& b){return a.native().size()>b.native().size();});
        for(const auto& directory:oldDirectories){std::error_code ignored;if(fs::is_directory(directory)&&fs::is_empty(directory))fs::remove(directory,ignored);}
    }
    // Adopt the former flat generated layout once. Asset UUIDs and aliases
    // remain authoritative; moving bytes never rewrites scenes or materials.
    // Authored/manual paths outside those exact old folders stay where they are.
    {
        const auto before=index(root);auto relocated=before;std::vector<ContentPathMove> moves;
        std::set<std::string> reserved,claimed;std::vector<fs::path> formerDirectories;
        for(const auto& value:before.at("assets"))reserved.insert(lower(value.get<std::string>()));
        const auto flatModels=checkedRelative(root,"Content/models"),flatMaterials=checkedRelative(root,"Content/materials"),flatTextures=checkedRelative(root,"Content/textures");
        const auto samePath=[&](const fs::path& a,const fs::path& b){return lower(utf8(a))==lower(utf8(b));};
        const auto generated=[&](const std::string& id,const fs::path& folder,const std::string& legacyPrefix)->std::optional<fs::path>{
            if(deleted.contains(id)||isEngineAssetId(id))return {};
            const auto found=before.at("assets").find(id);if(found==before.at("assets").end())return {};
            const auto file=checkedContentPath(root,root/pathFromUtf8(found->get<std::string>()));
            if(!samePath(file.parent_path(),folder)||!lower(utf8(file.filename())).starts_with("quader-")||!fs::is_regular_file(file))return {};
            if(const auto aliases=before.find("aliases");aliases!=before.end())for(auto it=aliases->begin();it!=aliases->end();++it)
                if(it.value()==id&&it.key().starts_with(legacyPrefix))return file;
            const auto old="assets/"+utf8(file.lexically_relative(content));
            if(old.starts_with(legacyPrefix)&&canonicalAssetId(old)==id)return file;
            return {};
        };
        const auto relocate=[&](const std::string& id,const fs::path& from,const fs::path& requested){
            if(!claimed.insert(id).second)return;
            const auto relative=uniquePath(root,requested.lexically_relative(root),reserved),to=checkedRelative(root,relative);
            moves.push_back({from,to,false});relocated["assets"][id]=utf8(relative);formerDirectories.push_back(from.parent_path());
        };
        const auto imageReferences=[](auto&& self,const Json& value,std::set<std::string>& images)->void {
            if(value.is_object())for(auto it=value.begin();it!=value.end();++it){
                if(it.key()=="image"&&it.value().is_string())images.insert(canonicalAssetId(it.value().get<std::string>()));
                else self(self,it.value(),images);
            }else if(value.is_array())for(const auto& child:value)self(self,child,images);
        };
        // Ordered bundle/asset identities give shared native resources one
        // deterministic destination without duplicating their files or UUIDs.
        const auto retired=checkedRelative(root,fs::path(".crimson")/pathFromUtf8(".staging-quader-layout-"+newAssetId()));
        for(auto it=groups.begin();it!=groups.end();){
            auto& group=it->second;
            if(group.bundle.models.empty()){
                moves.push_back({group.file,retired/pathFromUtf8(group.bundle.id+".crimson-quader"),false});
                relocated["assets"].erase(group.bundle.id);formerDirectories.push_back(group.file.parent_path());it=groups.erase(it);continue;
            }
            const auto directory=quaderSceneContentDirectory(root,group.bundle);
            if(samePath(group.file.parent_path(),flatModels)){
                relocate(group.bundle.id,group.file,defaultQuaderSceneBundlePath(root,group.bundle));
                group.file=checkedRelative(root,pathFromUtf8(relocated.at("assets").at(group.bundle.id).get<std::string>()));
            }
            std::set<std::string> materials;
            for(const auto& model:group.bundle.models){const auto manifest=Json::parse(model.manifest);
                if(const auto mapped=manifest.find("materialAssets");mapped!=manifest.end())for(const auto& value:*mapped)materials.insert(canonicalAssetId(value.get<std::string>()));}
            for(const auto& id:materials){
                const auto file=generated(id,flatMaterials,"assets/materials/quader-"+group.bundle.projectId+"-");
                if(!file||file->extension()!=".crimson-material")continue;
                Json material;try{material=Json::parse(boundedRead(*file));}catch(const std::exception&){continue;}
                relocate(id,*file,directory/"materials"/file->filename());
                std::set<std::string> images;imageReferences(imageReferences,material,images);
                for(const auto& imageId:images)if(const auto texture=generated(imageId,flatTextures,"assets/textures/quader-"))
                    if(image(*texture))relocate(imageId,*texture,directory/"textures"/texture->filename());
            }
            ++it;
        }
        if(!moves.empty()){
            std::set<std::string> owners;for(const auto& value:relocated.at("assets"))
                if(!owners.insert(lower(value.get<std::string>())).second)throw std::runtime_error("Two identities claim the same organized Quader content file.");
            try{publishMoves(root,moves,std::move(relocated),{});if(fs::exists(retired))removeOwned(root,retired);}
            catch(...){if(!fs::exists(root/".crimson/content-move.json")&&fs::exists(retired)){try{removeOwned(root,retired);}catch(...){}}throw;}
            for(const auto& directory:formerDirectories){std::error_code ignored;if(fs::is_directory(directory,ignored)&&fs::is_empty(directory,ignored))fs::remove(directory,ignored);}
        }
    }
    // The bundle is the sole durable geometry source. Private cache removal,
    // partial writes and missing location metadata all recover from it without
    // the external Quader project or its reader library.
    auto next=index(root);
    for(const auto& [id,group]:groups) {
        if(deleted.contains(id))continue;registerLocations(next,group);
        for(const auto& model:group.bundle.models) {
            if(deleted.contains(model.id))continue;const auto directory=quaderBundleCacheDirectory(root,id,model.id);
            const std::array<const std::string*,4> values{&model.manifest,&model.gltf,&model.gltf,&model.buffer};
            for(std::size_t i=0;i<members.size();++i) {
                if(deleted.contains(model.memberIds[i]))continue;
                const auto file=checkedRelative(root,(directory/members[i]).lexically_relative(root));
                if(!fs::is_regular_file(file)||fs::file_size(file)!=values[i]->size()||boundedRead(file)!=*values[i])writeFileAtomically(file,*values[i]);
            }
        }
    }
    if(next!=index(root)){
        std::set<std::string> owners;for(auto it=next["assets"].begin();it!=next["assets"].end();++it)
            if(!owners.insert(lower(it.value().get<std::string>())).second)throw std::runtime_error("Two identities claim the same Quader bundle/cache file.");
        next["revision"]=next.value("revision",std::uint64_t{})+1;writeIndex(root,std::move(next));
    }
}
} // namespace crimson
