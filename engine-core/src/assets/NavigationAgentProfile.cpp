#include "NavigationAgentProfile.h"
#include "AtomicFile.h"
#include "ContentStorage.h"
#include "AssetTransaction.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace crimson {
namespace {
using Json=nlohmann::json;
std::filesystem::path fromUtf8(const std::string& value){return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(value.data()),value.size()));}
std::string utf8(const std::filesystem::path& value){const auto text=value.generic_u8string();return {reinterpret_cast<const char*>(text.data()),text.size()};}
}
void validateNavigationAgentProfile(const NavigationAgentProfile& profile) {
    if(!isAssetUuid(profile.id)||profile.name.empty()||profile.name.size()>1024||profile.name.find_first_not_of(" \t\r\n")==std::string::npos)
        throw std::runtime_error("Navigation Agent Profile requires a UUID and a nonempty name of at most 1024 bytes.");
    if(!validAgentTraversal(profile.traversal))throw std::runtime_error("Navigation Agent Profile requires finite positive capsule dimensions, a slope below 90 degrees and a nonnegative step height.");
}
std::string serializeNavigationAgentProfile(const NavigationAgentProfile& profile) {
    validateNavigationAgentProfile(profile);const auto& value=profile.traversal;
    return Json{{"type","crimson-navigation-agent"},{"version",1},{"id",profile.id},{"name",profile.name},
        {"height",value.height},{"radius",value.radius},{"maxSlope",value.maxSlope},{"maxStep",value.maxStep}}.dump(2)+'\n';
}
NavigationAgentProfile deserializeNavigationAgentProfile(std::string_view text,const std::string& expectedId) {
    if(text.size()>65536)throw std::runtime_error("Navigation Agent Profile exceeds 64 KiB.");
    const auto data=Json::parse(text);
    if(data.at("type")!="crimson-navigation-agent"||data.at("version")!=1)throw std::runtime_error("Unsupported Navigation Agent Profile type or version.");
    NavigationAgentProfile profile{data.at("id").get<std::string>(),data.at("name").get<std::string>(),
        {data.at("height").get<float>(),data.at("radius").get<float>(),data.at("maxSlope").get<float>(),data.at("maxStep").get<float>()}};
    validateNavigationAgentProfile(profile);
    if(!expectedId.empty()&&profile.id!=expectedId)throw std::runtime_error("Navigation Agent Profile has a different asset identity.");
    return profile;
}
std::filesystem::path navigationAgentProfilePath(const std::filesystem::path& root,const std::string& id) {
    if(!isAssetUuid(id))throw std::runtime_error("Navigation Agent Profile reference must be a UUID.");
    auto file=resolveAssetPath(root,id);
    if(file.extension()!=".crimson-agent-profile")throw std::runtime_error("Navigation Agent Profile reference has an incompatible asset kind.");
    return file;
}
NavigationAgentProfile loadNavigationAgentProfile(const std::filesystem::path& root,const std::string& id) {
    if(id==DefaultNavigationAgentProfileId)return *defaultNavigationAgentProfile();
    const auto file=navigationAgentProfilePath(root,id);
    if(std::filesystem::file_size(file)>65536)throw std::runtime_error("Navigation Agent Profile exceeds 64 KiB.");
    std::ifstream input(file,std::ios::binary);if(!input)throw std::runtime_error("Cannot read Navigation Agent Profile.");
    const std::string contents{std::istreambuf_iterator<char>(input),{}};
    return deserializeNavigationAgentProfile(contents,id);
}
void saveNavigationAgentProfile(const std::filesystem::path& root,const NavigationAgentProfile& profile) {
    if(isEngineAssetId(profile.id))throw std::runtime_error("Default Navigation Agent Profile is read-only.");
    if(!contentStorageReady(root))initializeContentStorage(root);
    const auto contents=serializeNavigationAgentProfile(profile);
    const auto entries=storedAssetPaths(root);const auto existing=std::find_if(entries.begin(),entries.end(),[&](const auto& entry){return entry.id==profile.id;});
    const auto file=existing==entries.end()?checkedContentPath(root,root/"Content"/"Navigation"/fromUtf8(profile.id+".crimson-agent-profile")):navigationAgentProfilePath(root,profile.id);
    (void)checkedContentPath(root,file);
    if(existing==entries.end()&&std::filesystem::exists(file))throw std::runtime_error("An unregistered Navigation Agent Profile destination already exists.");
    const std::array<StoredAssetPath,1> registration{{{profile.id,file}}};
    const std::array<AssetFileChange,2> changes{{{utf8(file.lexically_relative(root)),contents},
        {".crimson/asset-locations.json",prepareAssetPathRegistration(root,registration)}}};
    commitAssetTransaction(root,changes);refreshContentStorage(root);
}
NavigationAgentProfile createNavigationAgentProfile(const std::filesystem::path& root,const std::string& name) {
    return copyNavigationAgentProfile(root,*defaultNavigationAgentProfile(),name);
}
NavigationAgentProfile copyNavigationAgentProfile(const std::filesystem::path& root,const NavigationAgentProfile& original,const std::string& name) {
    if(!contentStorageReady(root))initializeContentStorage(root);
    auto profile=original;profile.id=newAssetId();profile.name=name.empty()?original.name:name;
    validateNavigationAgentProfile(profile);
    saveNavigationAgentProfile(root,profile);return profile;
}
void ensureEngineNavigationAgentProfile(const std::filesystem::path& root) {
    if(!contentStorageReady(root))return;
    const auto profile=defaultNavigationAgentProfile();const auto file=navigationAgentProfilePath(root,profile->id);
    if(!std::filesystem::exists(file))writeFileAtomically(file,serializeNavigationAgentProfile(*profile));
}
}
