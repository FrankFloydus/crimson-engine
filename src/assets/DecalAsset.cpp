#include "DecalAsset.h"
#include "AtomicFile.h"
#include "ContentStorage.h"
#include "resources/AssetId.h"
#include "resources/AssetRefJson.h"
#include "math/Json.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <unordered_set>

namespace crimson {
namespace {
using Json=nlohmann::json;
constexpr std::size_t MaximumBytes=1024*1024;
void unit(float value,const char* label) {
    if(!math::finite(value)||value<0||value>1)throw std::runtime_error(std::string(label)+" must be finite and between zero and one.");
}
void imageReference(const std::string& id) {
    if(id.empty()||isAssetUuid(id))return;
    const std::filesystem::path path(std::u8string(reinterpret_cast<const char8_t*>(id.data()),id.size()));
    if(!id.starts_with("assets/")||path.has_root_path()||id.find(':')!=std::string::npos)throw std::runtime_error("Decal maps require managed image references.");
    for(const auto& part:path)if(part==".."||part==".")throw std::runtime_error("Decal image reference escapes its managed path.");
    auto extension=path.extension().string();std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});
    if(extension!=".png"&&extension!=".jpg"&&extension!=".jpeg"&&extension!=".bmp"&&extension!=".tga")throw std::runtime_error("Decal maps require ordinary managed images, not HDR environments.");
}
const char* channelName(DecalTextureChannel channel) {
    switch(channel){case DecalTextureChannel::R:return "r";case DecalTextureChannel::G:return "g";case DecalTextureChannel::B:return "b";case DecalTextureChannel::A:return "a";}
    throw std::runtime_error("Unsupported decal image channel.");
}
DecalTextureChannel readChannel(const Json& value) {
    const auto text=value.get<std::string>();
    if(text=="r")return DecalTextureChannel::R;if(text=="g")return DecalTextureChannel::G;if(text=="b")return DecalTextureChannel::B;if(text=="a")return DecalTextureChannel::A;
    throw std::runtime_error("Unsupported decal image channel.");
}
std::string unusedName(const std::filesystem::path& root,const std::string& requested) {
    const auto base=requested.empty()?std::string("New Decal"):requested;std::unordered_set<std::string> names;
    for(const auto& entry:scanDecalAssets(root))names.insert(entry.name);
    if(!names.contains(base))return base;
    for(std::uint64_t number=2;;++number){const auto name=base+" "+std::to_string(number);if(name.size()>1024)throw std::runtime_error("Decal name is too long for a unique suffix.");if(!names.contains(name))return name;}
}
}
DecalAsset defaultDecalAsset(const std::string& name) {DecalAsset result;result.name=name;return result;}
void validateDecalAsset(const DecalAsset& asset) {
    if(!asset.id.empty()&&(!isAssetUuid(asset.id)||isEngineAssetId(asset.id)))throw std::runtime_error("Decal assets require a writable managed UUID.");
    if(asset.name.empty()||asset.name.size()>1024||asset.name.find_first_not_of(" \t\r\n")==std::string::npos||asset.name.find('\0')!=std::string::npos)throw std::runtime_error("Decal name must contain 1 to 1024 UTF-8 bytes.");
    for(const auto value:asset.color)unit(value,"Decal color");
    unit(asset.colorStrength,"Color strength");unit(asset.normalStrength,"Normal strength");unit(asset.roughnessStrength,"Roughness strength");unit(asset.roughness,"Roughness");
    imageReference(asset.colorTexture.id);imageReference(asset.coverageTexture.id);imageReference(asset.normalTexture.id);imageReference(asset.roughnessTexture.id);
    (void)channelName(asset.coverageChannel);(void)channelName(asset.roughnessChannel);
}
void canonicalizeDecalAsset(DecalAsset& asset) {
    validateDecalAsset(asset);asset.id=canonicalAssetId(asset.id);
    for(auto* map:{&asset.colorTexture,&asset.coverageTexture,&asset.normalTexture,&asset.roughnessTexture})map->id=canonicalAssetId(map->id);
}
std::vector<std::string> decalTextureReferences(const DecalAsset& asset) {
    std::vector<std::string> result;
    for(const auto* map:{&asset.colorTexture,&asset.coverageTexture,&asset.normalTexture,&asset.roughnessTexture})if(!map->empty())result.push_back(map->id);
    std::sort(result.begin(),result.end());result.erase(std::unique(result.begin(),result.end()),result.end());return result;
}
bool rewriteDecalTextureReferences(DecalAsset& asset,const std::function<std::string(const std::string&,bool)>& rewrite) {
    auto next=asset;const bool colorMask=!asset.colorTexture.empty()&&asset.coverageTexture.empty();
    const auto replace=[&](auto& map,bool normal){if(!map.empty())map.id=rewrite(map.id,normal);};
    replace(next.colorTexture,false);replace(next.coverageTexture,false);replace(next.normalTexture,true);replace(next.roughnessTexture,false);
    if(!asset.colorTexture.empty()&&next.colorTexture.empty())next.colorEnabled=false;
    if(!asset.normalTexture.empty()&&next.normalTexture.empty())next.normalEnabled=false;
    if(!asset.roughnessTexture.empty()&&next.roughnessTexture.empty())next.roughnessEnabled=false;
    if((!asset.coverageTexture.empty()&&next.coverageTexture.empty())||(colorMask&&next.colorTexture.empty()))next.colorEnabled=next.normalEnabled=next.roughnessEnabled=false;
    canonicalizeDecalAsset(next);if(next==asset)return false;asset=std::move(next);return true;
}
std::filesystem::path decalAssetPath(const std::filesystem::path& root,const std::string& id) {
    if(!isAssetUuid(id)||isEngineAssetId(id))throw std::runtime_error("Decal file requires a writable managed UUID.");
    for(const auto& entry:storedAssetPaths(root))if(entry.id==canonicalAssetId(id)) {
        if(entry.file.extension()!=".crimson-decal")throw std::runtime_error("Asset identity does not reference a decal file.");return entry.file;
    }
    try{const auto file=resolveAssetPath(root,id);if(file.extension()==".crimson-decal")return file;}catch(const std::exception&){/* New identity has no location until Save. */}
    return resolveAssetPath(root,"assets/decals")/(canonicalAssetId(id)+".crimson-decal");
}
std::string serializeDecalAsset(const DecalAsset& value) {
    auto asset=value;canonicalizeDecalAsset(asset);if(asset.id.empty())throw std::runtime_error("Saving a decal requires a managed identity.");
    return Json{{"version",1},{"type","crimson-decal"},{"id",asset.id},{"name",asset.name},
        {"color",{{"enabled",asset.colorEnabled},{"texture",asset.colorTexture},{"factor",asset.color},{"strength",asset.colorStrength}}},
        {"coverage",{{"texture",asset.coverageTexture},{"channel",channelName(asset.coverageChannel)}}},
        {"normal",{{"enabled",asset.normalEnabled},{"texture",asset.normalTexture},{"strength",asset.normalStrength}}},
        {"roughness",{{"enabled",asset.roughnessEnabled},{"texture",asset.roughnessTexture},{"channel",channelName(asset.roughnessChannel)},{"value",asset.roughness},{"strength",asset.roughnessStrength}}}}.dump(2)+"\n";
}
DecalAsset deserializeDecalAsset(std::string_view text,const std::string& expectedId) {
    if(text.size()>MaximumBytes)throw std::runtime_error("Decal asset exceeds 1 MiB.");const auto j=Json::parse(text);
    if(j.at("version")!=1||j.at("type")!="crimson-decal")throw std::runtime_error("Unsupported decal file type or version.");
    DecalAsset asset;asset.id=j.at("id").get<std::string>();asset.name=j.at("name").get<std::string>();
    const auto& color=j.at("color");asset.colorEnabled=color.at("enabled").get<bool>();asset.colorTexture=color.at("texture").get<AssetRef<DecodedImage>>();asset.color=color.at("factor").get<Vec3>();asset.colorStrength=color.at("strength").get<float>();
    const auto& coverage=j.at("coverage");asset.coverageTexture=coverage.at("texture").get<AssetRef<DecodedImage>>();asset.coverageChannel=readChannel(coverage.at("channel"));
    const auto& normal=j.at("normal");asset.normalEnabled=normal.at("enabled").get<bool>();asset.normalTexture=normal.at("texture").get<AssetRef<DecodedImage>>();asset.normalStrength=normal.at("strength").get<float>();
    const auto& rough=j.at("roughness");asset.roughnessEnabled=rough.at("enabled").get<bool>();asset.roughnessTexture=rough.at("texture").get<AssetRef<DecodedImage>>();asset.roughnessChannel=readChannel(rough.at("channel"));asset.roughness=rough.at("value").get<float>();asset.roughnessStrength=rough.at("strength").get<float>();
    canonicalizeDecalAsset(asset);if(asset.id.empty()||(!expectedId.empty()&&asset.id!=canonicalAssetId(expectedId)))throw std::runtime_error("Decal identity does not match its managed file.");return asset;
}
DecalAsset loadDecalAsset(const std::filesystem::path& root,const std::string& id) {
    const auto file=decalAssetPath(root,id);if(std::filesystem::file_size(file)>MaximumBytes)throw std::runtime_error("Decal asset exceeds 1 MiB.");
    std::ifstream input(file,std::ios::binary);if(!input)throw std::runtime_error("Cannot read decal asset.");const std::string text{std::istreambuf_iterator<char>(input),{}};
    if(input.bad())throw std::runtime_error("Could not read the complete decal asset.");return deserializeDecalAsset(text,id);
}
void saveDecalAsset(const std::filesystem::path& root,const DecalAsset& asset) {
    const auto text=serializeDecalAsset(asset);if(!contentStorageReady(root))initializeContentStorage(root);
    const auto file=decalAssetPath(root,asset.id);(void)checkedContentPath(root,file);writeFileAtomically(file,text);registerAssetPath(root,asset.id,file);
}
DecalAsset createDecalAsset(const std::filesystem::path& root,const std::string& name) {
    if(!contentStorageReady(root))initializeContentStorage(root);auto asset=defaultDecalAsset(unusedName(root,name));asset.id=newAssetId();saveDecalAsset(root,asset);return asset;
}
DecalAsset saveDecalAssetAs(const std::filesystem::path& root,const DecalAsset& source,const std::string& name) {
    validateDecalAsset(source);if(!contentStorageReady(root))initializeContentStorage(root);auto asset=source;asset.id=newAssetId();asset.name=unusedName(root,name.empty()?source.name:name);saveDecalAsset(root,asset);return asset;
}
std::vector<DecalAssetEntry> scanDecalAssets(const std::filesystem::path& root,std::vector<std::string>* diagnostics) {
    std::vector<DecalAssetEntry> result;if(!contentStorageReady(root))return result;
    for(const auto& entry:storedAssetPaths(root))if(entry.file.extension()==".crimson-decal"&&std::filesystem::is_regular_file(entry.file)) {
        try{auto asset=loadDecalAsset(root,entry.id);result.push_back({std::move(asset.id),std::move(asset.name)});}catch(const std::exception& error){if(diagnostics)diagnostics->push_back("Decal "+entry.id+": "+error.what());}
    }
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.name!=b.name?a.name<b.name:a.id<b.id;});return result;
}
}
