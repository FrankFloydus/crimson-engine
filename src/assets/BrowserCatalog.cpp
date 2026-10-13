#include "BrowserCatalog.h"
#include "AtomicFile.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <unordered_set>

namespace crimson {
namespace {
using Json = nlohmann::json;
std::string folded(std::string value) {
    std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    return value;
}
std::string cleanName(std::string name) {
    const auto first=name.find_first_not_of(" \t\r\n"), last=name.find_last_not_of(" \t\r\n");
    if (first==std::string::npos) throw std::runtime_error("Enter a name.");
    name=name.substr(first,last-first+1);
    if (name=="." || name==".." || name.size()>240 || name.find_first_of("/\\")!=std::string::npos ||
        std::any_of(name.begin(),name.end(),[](unsigned char c){return c<32 || c==127;}))
        throw std::runtime_error("Names must contain 1 to 240 characters without slashes or control characters.");
    return name;
}
std::string folderId() {
    static std::atomic<std::uint64_t> sequence{0};
    return "folder:"+std::to_string(std::chrono::system_clock::now().time_since_epoch().count())+"-"+std::to_string(sequence.fetch_add(1));
}
}

BrowserCatalog::BrowserCatalog(std::filesystem::path projectRoot) : file_(std::move(projectRoot)/"assets"/"browser.json") {
    try {
        if (!std::filesystem::exists(file_)) return;
        std::ifstream input(file_);
        if (!input) throw std::runtime_error("Cannot read browser metadata.");
        const auto json=Json::parse(input);
        if (json.at("version")!=1) throw std::runtime_error("Unsupported browser metadata version.");
        if (!json.at("folders").is_array() || !json.at("assets").is_object())
            throw std::runtime_error("Invalid browser folder or asset metadata.");
        for (const auto& item:json.at("folders")) {
            BrowserFolder folder{item.at("id").get<std::string>(),item.at("parent").get<std::string>(),cleanName(item.at("name").get<std::string>())};
            if (!folder.id.starts_with("folder:") || folder.id.size()==7 || !folders_.emplace(folder.id,folder).second)
                throw std::runtime_error("Invalid or duplicate browser folder identity.");
        }
        for (const auto& [id,folder]:folders_) {
            validateFolder(folder.parent);
            validateFolderName(folder.parent,folder.name,id);
            std::unordered_set<std::string> visited{id};
            auto parent=folder.parent;
            while (!parent.empty()) {
                if (!visited.insert(parent).second) throw std::runtime_error("Browser folder hierarchy contains a cycle.");
                parent=folders_.at(parent).parent;
            }
        }
        for (auto it=json.at("assets").begin();it!=json.at("assets").end();++it) {
            Presentation value{it.value().value("folder",std::string{}),it.value().value("name",std::string{})};
            validateFolder(value.folder);
            if (!value.name.empty()) value.name=cleanName(value.name);
            presentation_.emplace(it.key(),std::move(value));
        }
    } catch (const std::exception& error) {
        folders_.clear(); presentation_.clear(); writable_=false;
        status_="Browser organization could not be loaded: "+std::string(error.what())+" The existing browser.json was preserved.";
    }
}
void BrowserCatalog::synchronize(std::span<const AssetEntry> assets) {
    assets_.clear(); assets_.reserve(assets.size());
    for (const auto& asset:assets) assets_.emplace(asset.id,asset);
}
bool BrowserCatalog::isFolder(const std::string& id) const { return folders_.contains(id); }
bool BrowserCatalog::contains(const std::string& id) const { return isFolder(id) || assets_.contains(id); }
void BrowserCatalog::validateFolder(const std::string& id) const {
    if (!id.empty() && !isFolder(id)) throw std::runtime_error("The destination folder no longer exists.");
}
void BrowserCatalog::validateFolderName(const std::string& parent, const std::string& name, const std::string& except) const {
    for (const auto& [id,folder]:folders_)
        if (id!=except && folder.parent==parent && folded(folder.name)==folded(name))
            throw std::runtime_error("A folder with that name already exists here.");
}
std::string BrowserCatalog::parent(const std::string& id) const {
    if (const auto found=folders_.find(id);found!=folders_.end()) return found->second.parent;
    if (const auto found=presentation_.find(id);found!=presentation_.end()) return found->second.folder;
    return {};
}
std::string BrowserCatalog::name(const std::string& id) const {
    if (const auto found=folders_.find(id);found!=folders_.end()) return found->second.name;
    if (const auto found=presentation_.find(id);found!=presentation_.end() && !found->second.name.empty()) return found->second.name;
    if (const auto found=assets_.find(id);found!=assets_.end()) return found->second.name;
    return {};
}
std::vector<BrowserItem> BrowserCatalog::items(const std::string& folder) const {
    std::vector<BrowserItem> result;
    for (const auto& [id,value]:folders_) if (value.parent==folder) result.push_back({id,value.name,AssetKind::Texture,true});
    for (const auto& [id,value]:assets_) if (parent(id)==folder) result.push_back({id,name(id),value.kind,false});
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){
        if (a.folder!=b.folder) return a.folder;
        const auto an=folded(a.name),bn=folded(b.name);
        return an==bn ? a.id<b.id : an<bn;
    });
    return result;
}
std::vector<BrowserFolder> BrowserCatalog::breadcrumbs(const std::string& folder) const {
    std::vector<BrowserFolder> result;
    for (auto id=folder;!id.empty();) {
        const auto found=folders_.find(id);
        if (found==folders_.end()) break;
        result.push_back(found->second); id=found->second.parent;
    }
    std::reverse(result.begin(),result.end());
    return result;
}
void BrowserCatalog::save() const {
    if (!writable_) throw std::runtime_error(status_);
    Json folders=Json::array(),assets=Json::object();
    std::vector<std::string> keys;
    for (const auto& [id,value]:folders_) { (void)value; keys.push_back(id); }
    std::sort(keys.begin(),keys.end());
    for (const auto& id:keys) { const auto& folder=folders_.at(id); folders.push_back({{"id",id},{"parent",folder.parent},{"name",folder.name}}); }
    for (const auto& [id,value]:presentation_) assets[id]={{"folder",value.folder},{"name",value.name}};
    writeFileAtomically(file_,Json{{"version",1},{"folders",std::move(folders)},{"assets",std::move(assets)}}.dump(2));
}
std::string BrowserCatalog::createFolder(const std::string& parent,std::string name) {
    validateFolder(parent); name=cleanName(std::move(name)); validateFolderName(parent,name);
    const auto id=folderId();
    folders_.emplace(id,BrowserFolder{id,parent,std::move(name)});
    try { save(); } catch (...) { folders_.erase(id); throw; }
    return id;
}
void BrowserCatalog::rename(const std::string& id,std::string name) {
    name=cleanName(std::move(name));
    if (!contains(id)) throw std::runtime_error("The selected asset no longer exists.");
    if (isFolder(id)) {
        auto& folder=folders_.at(id); validateFolderName(folder.parent,name,id);
        const auto previous=folder.name; folder.name=std::move(name);
        try { save(); } catch (...) { folder.name=previous; throw; }
    } else {
        const auto previous=presentation_;
        presentation_[id].name=std::move(name);
        try { save(); } catch (...) { presentation_=previous; throw; }
    }
}
void BrowserCatalog::move(std::span<const std::string> ids,const std::string& destination) {
    validateFolder(destination);
    std::unordered_set<std::string> selected(ids.begin(),ids.end());
    for (const auto& id:selected) {
        if (!contains(id)) throw std::runtime_error("A dragged asset no longer exists.");
        if (isFolder(id)) {
            for (auto ancestor=destination;!ancestor.empty();ancestor=parent(ancestor))
                if (ancestor==id) throw std::runtime_error("A folder cannot be moved into itself or its descendants.");
        }
    }
    const auto oldFolders=folders_; const auto oldPresentation=presentation_;
    try {
        for (const auto& id:selected) {
            bool parentSelected=false;
            for (auto ancestor=parent(id);!ancestor.empty();ancestor=parent(ancestor))
                if (selected.contains(ancestor)) { parentSelected=true; break; }
            if (parentSelected) continue;
            if (isFolder(id)) folders_.at(id).parent=destination;
            else presentation_[id].folder=destination;
        }
        for (const auto& [id,folder]:folders_) validateFolderName(folder.parent,folder.name,id);
        save();
    } catch (...) { folders_=oldFolders; presentation_=oldPresentation; throw; }
}

std::vector<std::string> BrowserCatalog::descendants(const std::string& folder) const {
    validateFolder(folder);std::vector<std::string> result,pending{folder};
    while(!pending.empty()) { const auto current=pending.back();pending.pop_back();
        for(const auto& item:items(current)) { result.push_back(item.id);if(item.folder)pending.push_back(item.id); }
    }
    return result;
}
std::string BrowserCatalog::serializeWithout(std::span<const std::string> ids) const {
    if(!writable_)throw std::runtime_error(status_);
    const std::unordered_set<std::string> erased(ids.begin(),ids.end());
    for(const auto& id:erased)if(isFolder(id))for(const auto& descendant:descendants(id))
        if(!erased.contains(descendant))throw std::runtime_error("Folder deletion must include its complete contents.");
    Json folders=Json::array(),assets=Json::object();std::vector<std::string> keys;
    for(const auto& [id,folder]:folders_){(void)folder;if(!erased.contains(id))keys.push_back(id);}std::sort(keys.begin(),keys.end());
    for(const auto& id:keys){const auto& f=folders_.at(id);folders.push_back({{"id",id},{"parent",f.parent},{"name",f.name}});}
    for(const auto& [id,p]:presentation_)if(!erased.contains(id)&&!erased.contains(p.folder))assets[id]={{"folder",p.folder},{"name",p.name}};
    return Json{{"version",1},{"folders",folders},{"assets",assets}}.dump(2)+"\n";
}
void BrowserCatalog::erase(std::span<const std::string> ids) {
    const auto contents=serializeWithout(ids);auto prepared=prepareErase(ids);writeFileAtomically(file_,contents);*this=std::move(prepared);
}
BrowserCatalog BrowserCatalog::prepareErase(std::span<const std::string> ids)const {
    (void)serializeWithout(ids);auto prepared=*this;
    const std::unordered_set<std::string> erased(ids.begin(),ids.end());
    std::erase_if(prepared.presentation_,[&](const auto& item){return erased.contains(item.first)||erased.contains(item.second.folder);});
    for(const auto& id:ids){prepared.folders_.erase(id);prepared.assets_.erase(id);}return prepared;
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
