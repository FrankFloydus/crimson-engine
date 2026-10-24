#include "BrowserCatalog.h"
#include "AtomicFile.h"
#include "QuaderSceneBundle.h"
#include "resources/AssetId.h"
#include "resources/Paths.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <unordered_set>

namespace crimson {
namespace {
using Json=nlohmann::json;
namespace fs=std::filesystem;
std::string utf8(const fs::path& path) {
    const auto text=path.generic_u8string();return {reinterpret_cast<const char*>(text.data()),text.size()};
}
std::string folded(std::string value) {
    std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});return value;
}
std::string cleanName(std::string name) {
    const auto first=name.find_first_not_of(" \t\r\n"),last=name.find_last_not_of(" \t\r\n");
    if(first==std::string::npos)throw std::runtime_error("Enter a name.");
    return contentFilename(name.substr(first,last-first+1));
}
std::string savedDisplayName(std::string name) {
    if(name.size()>240||name.find_first_of("/\\")!=std::string::npos||std::any_of(name.begin(),name.end(),[](unsigned char c){return c<32||c==127;}))
        throw std::runtime_error("Invalid saved browser display name.");
    return name;
}
std::string folderId() {
    return newAssetId();
}
bool below(const fs::path& path,const fs::path& parent) {
    const auto relative=path.lexically_relative(parent);
    if(relative.empty()||relative.is_absolute()||relative.has_root_path())return false;
    for(const auto& part:relative)if(part=="..")return false;
    return true;
}
bool contentRelative(const std::string& value) {
    const auto path=pathFromUtf8(value).lexically_normal();
    if(path.is_absolute()||path.has_root_path())return false;
    const auto first=path.begin();
    if(first==path.end()||(*first!="Content"&&*first!="Engine"))return false;
    for(const auto& part:path)if(part=="..")return false;
    return utf8(path)==value;
}
bool matchesFilter(AssetKind kind,BrowserAssetFilter filter) {
    switch(filter) {
    case BrowserAssetFilter::All:return true;
    case BrowserAssetFilter::Materials:return kind==AssetKind::Material;
    case BrowserAssetFilter::Models:return kind==AssetKind::Model;
    case BrowserAssetFilter::Textures:return kind==AssetKind::Texture||kind==AssetKind::Environment;
    case BrowserAssetFilter::Scenes:return kind==AssetKind::Scene;
    case BrowserAssetFilter::Particles:return kind==AssetKind::Particle;
    case BrowserAssetFilter::NavigationProfiles:return kind==AssetKind::NavigationAgentProfile;
    case BrowserAssetFilter::GameplayEffects:return kind==AssetKind::GameplayEffect;
    case BrowserAssetFilter::Animations:return kind==AssetKind::Animation;
    case BrowserAssetFilter::Decals:return kind==AssetKind::Decal;
    case BrowserAssetFilter::Ragdolls:return kind==AssetKind::Ragdoll;
    }
    return false;
}
}

BrowserCatalog::BrowserCatalog(fs::path projectRoot):root_(fs::absolute(std::move(projectRoot)).lexically_normal()),file_(root_/".crimson/browser.json") {
    const auto roots=[&] {
        folders_[ContentFolder]={ContentFolder,{},"Content","Content"};
        folders_[EngineFolder]={EngineFolder,{},"Engine","Engine"};
    };
    try {
        initializeContentStorage(root_);
        if(!fs::exists(file_)){roots();rebuildViews();return;}
        std::ifstream input(file_);if(!input)throw std::runtime_error("Cannot read browser metadata.");
        const auto json=Json::parse(input);
        if(json.at("version")!=2)throw std::runtime_error("Unsupported browser metadata version.");
        if(!json.at("folders").is_array()||!json.at("assets").is_object())throw std::runtime_error("Invalid browser folder or asset metadata.");
        std::unordered_set<std::string> directoryPaths;
        for(const auto& item:json.at("folders")) {
            BrowserFolder folder{item.at("id").get<std::string>(),item.at("parent").get<std::string>(),cleanName(item.at("name").get<std::string>()),item.at("path").get<std::string>()};
            if((!isAssetUuid(folder.id)&&(!folder.id.starts_with("folder:")||folder.id.size()==7))||!contentRelative(folder.path)||!directoryPaths.insert(folded(folder.path)).second||!folders_.emplace(folder.id,folder).second)
                throw std::runtime_error("Invalid or duplicate browser folder identity or path.");
        }
        if(!folders_.contains(ContentFolder)||!folders_.contains(EngineFolder)||folders_.at(ContentFolder).path!="Content"||folders_.at(EngineFolder).path!="Engine")
            throw std::runtime_error("The Content and Engine roots are missing.");
        for(const auto& [id,folder]:folders_) {
            validateFolder(folder.parent);validateFolderName(folder.parent,folder.name,id);
            if(folder.parent.empty()) {
                if(id!=ContentFolder&&id!=EngineFolder)throw std::runtime_error("A browser directory is outside Content or Engine.");
            } else if(pathFromUtf8(folder.path).parent_path()!=pathFromUtf8(folders_.at(folder.parent).path))
                throw std::runtime_error("A browser directory does not match its physical parent.");
            std::unordered_set<std::string> visited{id};
            for(auto parent=folder.parent;!parent.empty();parent=folders_.at(parent).parent)
                if(!visited.insert(parent).second)throw std::runtime_error("Browser folder hierarchy contains a cycle.");
        }
        for(auto it=json.at("assets").begin();it!=json.at("assets").end();++it) {
            Presentation value{it.value().value("folder",std::string(ContentFolder)),it.value().value("name",std::string{})};
            validateFolder(value.folder);if(!value.name.empty())value.name=savedDisplayName(value.name);
            presentation_.emplace(it.key(),std::move(value));
        }
    } catch(const std::exception& error) {
        folders_.clear();presentation_.clear();roots();writable_=false;
        status_="Browser organization could not be loaded: "+std::string(error.what())+" The existing browser.json was preserved.";
    }
    rebuildViews();
}
void BrowserCatalog::synchronize(std::span<const AssetEntry> assets) {
    assets_.clear();assets_.reserve(assets.size());for(const auto& asset:assets)assets_.emplace(asset.id,asset);
    const auto previousFolders=folders_;const auto previousPresentation=presentation_;
    std::unordered_map<std::string,std::string> byPath;
    for(const auto& [id,folder]:folders_)byPath[folded(folder.path)]=id;
    std::unordered_set<std::string> present{ContentFolder,EngineFolder};
    const auto scan=[&](const auto& self,const std::string& parent)->void {
        std::error_code error;
        for(fs::directory_iterator it(folderPath(parent),fs::directory_options::skip_permission_denied,error),end;!error&&it!=end;it.increment(error)) {
            if(!it->is_directory(error)||it->is_symlink(error)||error){error.clear();continue;}
            if(fs::is_regular_file(it->path()/"manifest.json",error)){error.clear();continue;}
            error.clear();const auto relative=utf8(it->path().lexically_relative(root_));
            const auto found=byPath.find(folded(relative));const auto id=found==byPath.end()?folderId():found->second;
            folders_[id]={id,parent,utf8(it->path().filename()),relative};present.insert(id);self(self,id);
        }
    };
    scan(scan,ContentFolder);scan(scan,EngineFolder);
    std::erase_if(folders_,[&](const auto& entry){return !present.contains(entry.first);});
    byPath.clear();for(const auto& [id,folder]:folders_)byPath[folded(folder.path)]=id;
    for(const auto& [id,asset]:assets_) {
        fs::path physical;
        try{physical=resolveAssetPath(root_,id);}catch(const std::exception&){}
        auto& metadata=metadata_[id];metadata.filename=folded(utf8(physical.filename()));
        metadata.writable=!isEngineAssetId(id)&&!physical.empty()&&below(physical,root_/"Content");
        metadata.deletable=metadata.writable;
        auto directory=physical.parent_path();
        if(asset.kind==AssetKind::Model&&physical.filename()=="manifest.json") {
            if(const auto bundle=quaderBundleSource(root_,id)) {
                directory=bundle->parent_path();metadata.filename=folded(utf8(bundle->filename()));metadata.writable=false;
                metadata.deletable=!isEngineAssetId(id);
            }else directory=directory.parent_path();
        }
        else if(packageImage(physical)) {
            for(auto parent=directory;below(parent,root_/"Content")||below(parent,root_/"Engine");parent=parent.parent_path())
                if(fs::is_regular_file(parent/"manifest.json")){directory=parent.parent_path();break;}
        }
        const auto found=byPath.find(folded(utf8(directory.lexically_relative(root_))));
        presentation_[id].folder=found==byPath.end()?(isEngineAssetId(id)?EngineFolder:ContentFolder):found->second;
    }
    for(auto& [id,value]:presentation_){(void)id;if(!folders_.contains(value.folder))value.folder=ContentFolder;}
    bool changed=previousFolders.size()!=folders_.size()||previousPresentation.size()!=presentation_.size();
    for(const auto& [id,value]:folders_) {
        const auto found=previousFolders.find(id);if(found==previousFolders.end()||found->second.parent!=value.parent||found->second.name!=value.name||found->second.path!=value.path)changed=true;
    }
    for(const auto& [id,value]:presentation_) {
        const auto found=previousPresentation.find(id);if(found==previousPresentation.end()||found->second.folder!=value.folder||found->second.name!=value.name)changed=true;
    }
    rebuildViews();if(writable_&&changed)save();
}
bool BrowserCatalog::isFolder(const std::string& id)const{return folders_.contains(id);}
bool BrowserCatalog::contains(const std::string& id)const{return isFolder(id)||assets_.contains(id);}
void BrowserCatalog::validateFolder(const std::string& id)const {
    if(!id.empty()&&!isFolder(id))throw std::runtime_error("The destination folder no longer exists.");
}
void BrowserCatalog::validateFolderName(const std::string& parent,const std::string& name,const std::string& except)const {
    for(const auto& [id,folder]:folders_)if(id!=except&&folder.parent==parent&&folded(folder.name)==folded(name))throw std::runtime_error("A folder with that name already exists here.");
}
std::string BrowserCatalog::parent(const std::string& id)const {
    if(const auto found=folders_.find(id);found!=folders_.end())return found->second.parent;
    if(const auto found=presentation_.find(id);found!=presentation_.end())return found->second.folder;
    return {};
}
std::string BrowserCatalog::name(const std::string& id)const {
    if(const auto found=folders_.find(id);found!=folders_.end())return found->second.name;
    if(const auto found=presentation_.find(id);found!=presentation_.end()&&!found->second.name.empty())return found->second.name;
    if(const auto found=assets_.find(id);found!=assets_.end())return found->second.name;
    return {};
}
fs::path BrowserCatalog::folderPath(const std::string& id)const {
    const auto found=folders_.find(id);if(found==folders_.end())throw std::runtime_error("The destination folder no longer exists.");return root_/pathFromUtf8(found->second.path);
}
bool BrowserCatalog::writableFolder(const std::string& id)const {
    const auto found=folders_.find(id);return writable_&&found!=folders_.end()&&(found->second.path=="Content"||found->second.path.starts_with("Content/"));
}
bool BrowserCatalog::mutableItem(const std::string& id)const {
    if(!writable_||!contains(id)||id==ContentFolder||id==EngineFolder)return false;
    if(isFolder(id))return writableFolder(id);
    const auto found=metadata_.find(id);return found!=metadata_.end()&&found->second.writable;
}
bool BrowserCatalog::deletableItem(const std::string& id)const {
    if(!writable_||!contains(id)||id==ContentFolder||id==EngineFolder)return false;
    if(isFolder(id))return writableFolder(id);
    const auto found=metadata_.find(id);return found!=metadata_.end()&&found->second.deletable;
}
fs::path BrowserCatalog::itemPath(const std::string& id)const {
    if(isFolder(id))return folderPath(id);
    auto file=resolveAssetPath(root_,id);if(assets_.at(id).kind==AssetKind::Model&&file.filename()=="manifest.json")file=file.parent_path();return file;
}
bool BrowserCatalog::packageImage(const fs::path& file)const {
    std::error_code error;
    for(auto parent=file.parent_path();below(parent,root_/"Content")||below(parent,root_/"Engine");parent=parent.parent_path())
        if(fs::is_regular_file(parent/"manifest.json",error))return true;
    return false;
}
void BrowserCatalog::rebuildViews() {
    std::erase_if(metadata_,[&](const auto& entry){return !contains(entry.first);});
    orderedItems_.clear();orderedItems_.reserve(folders_.size()+assets_.size());
    for(const auto& [id,folder]:folders_) {
        metadata_[id].name=folded(folder.name);orderedItems_.push_back({id,folder.name,AssetKind::Texture,true});
    }
    for(const auto& [id,asset]:assets_) {
        auto display=name(id);auto& metadata=metadata_[id];metadata.name=folded(display);metadata.catalogName=folded(asset.name);
        const auto slash=id.find_last_of("/\\");metadata.identity=folded(id.substr(slash==std::string::npos?0:slash+1));
        orderedItems_.push_back({id,std::move(display),asset.kind,false});
    }
    std::sort(orderedItems_.begin(),orderedItems_.end(),[&](const auto& a,const auto& b){
        if(a.folder!=b.folder)return a.folder;const auto& an=metadata_.at(a.id).name;const auto& bn=metadata_.at(b.id).name;return an==bn?a.id<b.id:an<bn;
    });
    directItems_.clear();directItems_.reserve(folders_.size()+1);
    for(const auto& item:orderedItems_)directItems_[parent(item.id)].push_back(item);
    queryValid_=false;queryItems_.clear();++revision_;
}
std::vector<BrowserItem> BrowserCatalog::items(const std::string& folder)const{return itemsView(folder);}
std::vector<BrowserItem> BrowserCatalog::items(const std::string& folder,std::string_view search,BrowserAssetFilter filter)const{return itemsView(folder,search,filter);}
const std::vector<BrowserItem>& BrowserCatalog::itemsView(const std::string& folder,std::string_view search,BrowserAssetFilter filter)const {
    const auto first=search.find_first_not_of(" \t\r\n"),last=search.find_last_not_of(" \t\r\n");
    const auto query=first==std::string_view::npos?std::string{}:folded(std::string(search.substr(first,last-first+1)));
    if(query.empty()&&filter==BrowserAssetFilter::All) {
        static const std::vector<BrowserItem> empty;
        const auto found=directItems_.find(folder);return found==directItems_.end()?empty:found->second;
    }
    if(queryValid_&&queryFolder_==folder&&queryText_==query&&queryFilter_==filter)return queryItems_;
    queryItems_.clear();queryFolder_=folder;queryText_=query;queryFilter_=filter;queryValid_=true;
    for(const auto& item:orderedItems_) {
        auto location=parent(item.id);
        if(item.folder){if(location==folder)queryItems_.push_back(item);continue;}
        if(!matchesFilter(item.kind,filter))continue;
        while(location!=folder&&!location.empty()){const auto found=folders_.find(location);if(found==folders_.end())break;location=found->second.parent;}
        if(location!=folder)continue;
        const auto& metadata=metadata_.at(item.id);
        if(!query.empty()&&metadata.name.find(query)==std::string::npos&&metadata.catalogName.find(query)==std::string::npos&&metadata.filename.find(query)==std::string::npos&&metadata.identity.find(query)==std::string::npos)continue;
        queryItems_.push_back(item);
    }
    return queryItems_;
}
std::vector<BrowserFolder> BrowserCatalog::breadcrumbs(const std::string& folder)const {
    std::vector<BrowserFolder> result;
    for(auto id=folder;!id.empty();){const auto found=folders_.find(id);if(found==folders_.end())break;result.push_back(found->second);id=found->second.parent;}
    std::reverse(result.begin(),result.end());return result;
}
std::string BrowserCatalog::serialize()const{return serializeWithout({});}
void BrowserCatalog::save()const{writeFileAtomically(file_,serialize());}
void BrowserCatalog::relocate(std::vector<ContentPathMove> moves) {
    std::stable_sort(moves.begin(),moves.end(),[](const auto& a,const auto& b){return a.copy&&!b.copy;});relocateContentPaths(root_,moves,serialize());rebuildViews();
}
std::string BrowserCatalog::createFolder(const std::string& parent,std::string name) {
    if(!writableFolder(parent))throw std::runtime_error("Create folders inside Content. Engine content is protected.");
    name=cleanName(std::move(name));validateFolderName(parent,name);const auto path=checkedContentPath(root_,folderPath(parent)/pathFromUtf8(name));
    if(fs::exists(path))throw std::runtime_error("A file or folder with that name already exists here.");
    const auto id=folderId();folders_[id]={id,parent,name,utf8(path.lexically_relative(root_))};
    bool created=false;
    try{created=fs::create_directory(path);if(!created)throw std::runtime_error("A file or folder with that name already exists here.");save();rebuildViews();}
    catch(...){folders_.erase(id);if(created){std::error_code ignored;fs::remove(path,ignored);}throw;}return id;
}
void BrowserCatalog::rename(const std::string& id,std::string name) {
    if(!mutableItem(id))throw std::runtime_error("The selected item is protected or no longer exists.");name=cleanName(std::move(name));
    if(name==this->name(id))return;
    const auto oldFolders=folders_;const auto oldPresentation=presentation_;const auto oldMetadata=metadata_;const auto from=itemPath(id);auto filename=pathFromUtf8(name);
    if(!isFolder(id)&&assets_.at(id).kind!=AssetKind::Model&&filename.extension()!=from.extension())filename+=from.extension();
    const auto to=(folderPath(parent(id))/filename).lexically_normal();(void)checkedContentPath(root_,to);
    if(from==to) {
        if(!isFolder(id)&&this->name(id)!=name) {
            presentation_[id].name=std::move(name);try{save();rebuildViews();}catch(...){presentation_=oldPresentation;throw;}
        }
        return;
    }
    if(isFolder(id))validateFolderName(parent(id),name,id);
    try {
        if(isFolder(id)) {
            folders_.at(id).name=name;
            for(auto& [key,folder]:folders_){(void)key;const auto physical=root_/pathFromUtf8(folder.path);if(below(physical,from))folder.path=utf8((physical==from?to:to/physical.lexically_relative(from)).lexically_relative(root_));}
        }else {presentation_[id].name=name;metadata_[id].filename=folded(utf8(assets_.at(id).kind==AssetKind::Model?fs::path("manifest.json"):to.filename()));}
        relocate({ContentPathMove{from,to,!isFolder(id)&&assets_.at(id).kind!=AssetKind::Model&&packageImage(from)}});
    }catch(...){folders_=oldFolders;presentation_=oldPresentation;metadata_=oldMetadata;throw;}
}
void BrowserCatalog::move(std::span<const std::string> ids,const std::string& destination,bool uniqueNames) {
    if(!writableFolder(destination))throw std::runtime_error("Move assets into Content. Engine content is protected.");
    const std::unordered_set<std::string> selected(ids.begin(),ids.end());std::vector<std::string> top;
    for(const auto& id:selected) {
        if(!mutableItem(id))throw std::runtime_error("A dragged item is protected or no longer exists.");
        if(isFolder(id))for(auto ancestor=destination;!ancestor.empty();ancestor=parent(ancestor))if(ancestor==id)throw std::runtime_error("A folder cannot be moved into itself or its descendants.");
        bool parentSelected=false;for(auto ancestor=parent(id);!ancestor.empty();ancestor=parent(ancestor))if(selected.contains(ancestor)){parentSelected=true;break;}
        const bool extractImage=!isFolder(id)&&assets_.at(id).kind!=AssetKind::Model&&packageImage(itemPath(id));
        if(!parentSelected&&(parent(id)!=destination||extractImage))top.push_back(id);
    }
    const auto oldFolders=folders_;const auto oldPresentation=presentation_;const auto oldMetadata=metadata_;std::vector<ContentPathMove> moves;
    std::unordered_set<std::string> plannedPaths;
    for(const auto& stored:storedAssetPaths(root_))plannedPaths.insert(folded(utf8(stored.file)));
    for(const auto& id:top) {
        const auto from=itemPath(id);
        if((!uniqueNames||isFolder(id))&&(isFolder(id)||assets_.at(id).kind==AssetKind::Model||!packageImage(from)))plannedPaths.insert(folded(utf8(folderPath(destination)/from.filename())));
    }
    try {
        for(const auto& id:top) {
            const auto from=itemPath(id);auto to=checkedContentPath(root_,folderPath(destination)/from.filename());
            const bool copyImage=!isFolder(id)&&assets_.at(id).kind!=AssetKind::Model&&packageImage(from);
            if(copyImage||(uniqueNames&&!isFolder(id))) {
                const auto original=to;unsigned suffix=2;
                while(fs::exists(to)||plannedPaths.contains(folded(utf8(to))))
                    to=checkedContentPath(root_,original.parent_path()/pathFromUtf8(utf8(original.stem())+" "+std::to_string(suffix++)+utf8(original.extension())));
                plannedPaths.insert(folded(utf8(to)));
            }
            if(isFolder(id)) {
                validateFolderName(destination,name(id),id);folders_.at(id).parent=destination;
                for(auto& [key,folder]:folders_){(void)key;const auto physical=root_/pathFromUtf8(folder.path);if(below(physical,from))folder.path=utf8((physical==from?to:to/physical.lexically_relative(from)).lexically_relative(root_));}
            }else {presentation_[id].folder=destination;metadata_[id].filename=folded(utf8(assets_.at(id).kind==AssetKind::Model?fs::path("manifest.json"):to.filename()));}
            moves.push_back({from,to,copyImage});
        }
        if(!moves.empty())relocate(std::move(moves));
    }catch(...){folders_=oldFolders;presentation_=oldPresentation;metadata_=oldMetadata;throw;}
}
std::vector<std::string> BrowserCatalog::descendants(const std::string& folder)const {
    validateFolder(folder);std::vector<std::string> result,pending{folder};
    while(!pending.empty()){const auto current=pending.back();pending.pop_back();for(const auto& item:items(current)){result.push_back(item.id);if(item.folder)pending.push_back(item.id);}}return result;
}
std::string BrowserCatalog::serializeWithout(std::span<const std::string> ids)const {
    if(!writable_)throw std::runtime_error(status_);const std::unordered_set<std::string> erased(ids.begin(),ids.end());
    for(const auto& id:erased) {
        if(contains(id)&&!deletableItem(id))throw std::runtime_error("Engine content and the Content root are protected.");
        if(isFolder(id))for(const auto& descendant:descendants(id))if(!erased.contains(descendant))throw std::runtime_error("Folder deletion must include its complete contents.");
    }
    Json folders=Json::array(),assets=Json::object();std::vector<std::string> keys;
    for(const auto& [id,folder]:folders_){(void)folder;if(!erased.contains(id))keys.push_back(id);}std::sort(keys.begin(),keys.end());
    for(const auto& id:keys){const auto& f=folders_.at(id);folders.push_back({{"id",id},{"parent",f.parent},{"name",f.name},{"path",f.path}});}
    for(const auto& [id,p]:presentation_)if(!erased.contains(id)&&!erased.contains(p.folder))assets[id]={{"folder",p.folder},{"name",p.name}};
    return Json{{"version",2},{"folders",folders},{"assets",assets}}.dump(2)+"\n";
}
void BrowserCatalog::erase(std::span<const std::string> ids) {
    const auto contents=serializeWithout(ids);auto prepared=prepareErase(ids);std::vector<fs::path> directories;
    for(const auto& id:ids)if(isFolder(id))directories.push_back(folderPath(id));
    std::sort(directories.begin(),directories.end(),[](const auto& a,const auto& b){return std::distance(a.begin(),a.end())>std::distance(b.begin(),b.end());});
    for(const auto& directory:directories)for(const auto& entry:fs::directory_iterator(directory))
        if(std::find(directories.begin(),directories.end(),entry.path())==directories.end())throw std::runtime_error("Delete the directory's files before removing its browser card.");
    writeFileAtomically(file_,contents);for(const auto& directory:directories)fs::remove(directory);*this=std::move(prepared);
}
BrowserCatalog BrowserCatalog::prepareErase(std::span<const std::string> ids)const {
    (void)serializeWithout(ids);auto prepared=*this;const std::unordered_set<std::string> erased(ids.begin(),ids.end());
    std::erase_if(prepared.presentation_,[&](const auto& item){return erased.contains(item.first)||erased.contains(item.second.folder);});
    for(const auto& id:ids){prepared.folders_.erase(id);prepared.assets_.erase(id);}prepared.rebuildViews();return prepared;
}
bool BrowserSelection::contains(const std::string& id) const { return std::find(ids_.begin(),ids_.end(),id)!=ids_.end(); }
void BrowserSelection::clear() { ids_.clear(); primary_.clear(); anchor_.clear(); }
void BrowserSelection::set(const std::string& id) { clear(); if (!id.empty()) { ids_.push_back(id); primary_=anchor_=id; } }
void BrowserSelection::click(const std::string& id,std::span<const std::string> visible,bool control,bool shift) {
    const auto target=std::find(visible.begin(),visible.end(),id);
    if (target==visible.end()) return;
    const auto anchor=std::find(visible.begin(),visible.end(),anchor_);
    if (shift && anchor!=visible.end()) {
        if (!control) ids_.clear();
        const auto first=std::min(target,anchor),last=std::max(target,anchor);
        for (auto it=first;it!=last+1;++it) if (!contains(*it)) ids_.push_back(*it);
        primary_=id;
    } else if (control) {
        if (contains(id)) { std::erase(ids_,id); primary_=ids_.empty() ? std::string{} : ids_.back(); }
        else { ids_.push_back(id); primary_=id; }
        anchor_=id;
    } else set(id);
}
void BrowserSelection::beginDrag(const std::string& id) {
    if (!contains(id)) set(id);
    else primary_=id;
}
void BrowserSelection::retain(std::span<const std::string> visible) {
    std::erase_if(ids_,[&](const auto& id){return std::find(visible.begin(),visible.end(),id)==visible.end();});
    if (!contains(primary_)) primary_=ids_.empty() ? std::string{} : ids_.back();
    if (std::find(visible.begin(),visible.end(),anchor_)==visible.end()) anchor_=primary_;
}
} // namespace crimson
