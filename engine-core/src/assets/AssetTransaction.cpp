#include "AssetTransaction.h"
#include "AtomicFile.h"
#include "ContentStorage.h"
#include "resources/AssetId.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <set>
#include <stdexcept>

namespace crimson {
namespace {
using Json=nlohmann::json;
std::filesystem::path path(const std::string& id){return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(id.data()),id.size()));}
std::string text(const std::filesystem::path& file){std::ifstream in(file,std::ios::binary);if(!in)throw std::runtime_error("Cannot read asset transaction file.");return {std::istreambuf_iterator<char>(in),{}};}
bool equalFiles(const std::filesystem::path& a,const std::filesystem::path& b){
    if(!std::filesystem::is_regular_file(a)||!std::filesystem::is_regular_file(b)||std::filesystem::file_size(a)!=std::filesystem::file_size(b))return false;
    std::ifstream left(a,std::ios::binary),right(b,std::ios::binary);std::array<char,4096> l{},r{};
    if(!left||!right)return false;while(left){left.read(l.data(),l.size());right.read(r.data(),r.size());const auto count=left.gcount();
        if(count!=right.gcount()||!std::equal(l.begin(),l.begin()+count,r.begin()))return false;}
    return left.eof()&&right.eof();
}
void restore(const std::filesystem::path& root,const std::filesystem::path& directory,const Json& journal){
    for(const auto& item:journal.at("files")){
        const auto target=checkedManagedAssetPath(root,item.at("id").get<std::string>());
        if(item.at("existed").get<bool>()){
            const auto backupName=path(item.at("backup").get<std::string>());
            if(backupName.has_root_path()||backupName.filename()!=backupName)throw std::runtime_error("Deletion recovery backup path is invalid.");
            const auto backup=directory/"before"/backupName;
            const auto within=std::filesystem::weakly_canonical(backup).lexically_relative(std::filesystem::weakly_canonical(directory/"before"));
            if(within.empty()||within.is_absolute()||*within.begin()=="..")throw std::runtime_error("Deletion recovery backup escapes the transaction.");
            if(!std::filesystem::is_regular_file(backup))throw std::runtime_error("Deletion recovery backup is missing.");
            std::filesystem::create_directories(target.parent_path());
            if(!equalFiles(backup,target))copyFileAtomically(backup,target);
        }else{std::error_code error;std::filesystem::remove(target,error);if(error)throw std::runtime_error("Cannot restore deletion transaction: "+error.message());}
    }
}
}
std::filesystem::path checkedManagedAssetPath(const std::filesystem::path& root,const std::string& id){
    const auto relative=path(id);if(relative.empty()||relative.has_root_path()||(!isAssetUuid(id)&&*relative.begin()!="assets"&&*relative.begin()!="Content"&&*relative.begin()!=".crimson"))throw std::runtime_error("Asset file must stay under Content or private game metadata: '"+id+"'.");
    for(const auto& part:relative)if(part=="..")throw std::runtime_error("Asset path cannot contain parent traversal.");
    const auto canonicalRoot=std::filesystem::weakly_canonical(root),result=isAssetUuid(id)||*relative.begin()=="assets"?resolveAssetPath(root,id):std::filesystem::weakly_canonical(root/relative),within=result.lexically_relative(canonicalRoot);
    if(within.empty()||within.is_absolute()||*within.begin()==".."||*within.begin()=="Engine"||(*within.begin()!="assets"&&*within.begin()!="Content"&&*within.begin()!=".crimson"))throw std::runtime_error("Asset path escapes writable game content.");return result;
}
void recoverAssetTransactions(const std::filesystem::path& root){
    const auto base=checkedManagedAssetPath(root,"assets/.resource-transactions");if(!std::filesystem::exists(base))return;
    for(const auto& item:std::filesystem::directory_iterator(base))if(item.is_directory()){
        const auto contained=std::filesystem::weakly_canonical(item.path()).lexically_relative(base);
        if(contained.empty()||contained.is_absolute()||*contained.begin()=="..")throw std::runtime_error("Transaction directory escapes its owner.");
        const auto file=item.path()/"journal.json";
        if(std::filesystem::exists(file)){const auto journal=Json::parse(text(file));if(journal.at("version")!=1)throw std::runtime_error("Unsupported deletion transaction version.");
            if(!journal.value("committed",false))restore(root,item.path(),journal);}
        std::filesystem::remove_all(item.path());
    }
}
void commitAssetTransaction(const std::filesystem::path& root,std::span<const AssetFileChange> changes){
    if(changes.empty())return;recoverAssetTransactions(root);
    static std::atomic<uint64_t> sequence{};const auto token=std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(sequence.fetch_add(1));
    const auto directory=checkedManagedAssetPath(root,"assets/.resource-transactions/"+token);std::filesystem::create_directories(directory/"before");
    Json journal{{"version",1},{"committed",false},{"files",Json::array()}};std::set<std::filesystem::path> targets;
    bool publishedJournal=false;
    try{
        for(std::size_t i=0;i<changes.size();++i){const auto target=checkedManagedAssetPath(root,changes[i].id);
            if(!targets.insert(target).second)throw std::runtime_error("Deletion transaction contains duplicate target files.");
            if(std::filesystem::exists(target)&&!std::filesystem::is_regular_file(target))throw std::runtime_error("Deletion transaction cannot replace a directory.");
            const bool existed=std::filesystem::exists(target);const auto backup=std::to_string(i)+".bin";
            if(existed)std::filesystem::copy_file(target,directory/"before"/backup);
            journal["files"].push_back({{"id",changes[i].id},{"existed",existed},{"backup",backup}});
        }
        writeFileAtomically(directory/"journal.json",journal.dump(2));publishedJournal=true;
        for(const auto& change:changes){const auto target=checkedManagedAssetPath(root,change.id);
            if(change.contents)writeFileAtomically(target,*change.contents);else if(std::filesystem::exists(target)&&!std::filesystem::remove(target))throw std::runtime_error("Could not delete managed asset.");}
        journal["committed"]=true;writeFileAtomically(directory/"journal.json",journal.dump(2));
    }catch(...){if(publishedJournal)restore(root,directory,journal);std::filesystem::remove_all(directory);throw;}
    std::error_code ignored;std::filesystem::remove_all(directory,ignored);
}
}
