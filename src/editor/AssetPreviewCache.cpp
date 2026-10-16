#include "AssetPreviewCache.h"
#include "assets/AtomicFile.h"
#include "resources/CacheIO.h"
#include "resources/ImageDecoder.h"
#include "resources/Paths.h"
#include "resources/ResourceManager.h"
#include <lodepng.h>
#include <nlohmann/json.hpp>
#include <array>
#include <fstream>
#include <stdexcept>

namespace crimson {
namespace {
using Json=nlohmann::json;
constexpr unsigned IndexVersion=1,ImageVersion=10;
std::string utf8(const std::filesystem::path& file) { const auto value=file.generic_u8string();return {value.begin(),value.end()}; }
std::uint64_t textHash(const std::string& text) { auto key=streaming::detail::HashSeed;streaming::detail::hash(key,text.data(),text.size());return key; }
void checkCanceled(std::stop_token token) { if(token.stop_requested())throw std::runtime_error("Preview cache request canceled."); }
std::uint64_t contentHash(const std::filesystem::path& file,std::stop_token token,std::uint64_t* resourceHash=nullptr) {
    std::ifstream input(file,std::ios::binary);if(!input)throw std::runtime_error("Preview dependency is unavailable.");
    auto key=streaming::detail::HashSeed;std::array<char,64*1024> block{};
    if(resourceHash){*resourceHash=streaming::detail::HashSeed;const auto name=resourcePathKey(file);streaming::detail::hash(*resourceHash,name.data(),name.size());}
    while(input){checkCanceled(token);input.read(block.data(),block.size());const auto bytes=static_cast<std::size_t>(input.gcount());
        streaming::detail::hash(key,block.data(),bytes);if(resourceHash)streaming::detail::hash(*resourceHash,block.data(),bytes);}
    if(!input.eof())throw std::runtime_error("Preview dependency could not be read.");return key;
}
std::string storedPath(const std::filesystem::path& root,const std::filesystem::path& file) {
    const auto relative=file.lexically_relative(root);
    if(!relative.empty() && *relative.begin()!=L"..")return utf8(relative);
    return utf8(file);
}
std::filesystem::path restoredPath(const std::filesystem::path& root,const Json& path) {
    auto file=pathFromUtf8(path.get<std::string>());return file.is_absolute()?file:(root/file).lexically_normal();
}
}
std::filesystem::path assetPreviewImagePath(const std::filesystem::path& folder,const std::string& id,std::uint64_t signature) {
    return folder/(streaming::detail::hex(textHash(id))+"-"+streaming::detail::hex(signature)+".png");
}
std::filesystem::path assetPreviewIndexPath(const std::filesystem::path& folder,const std::string& id,std::uint32_t edge) {
    return folder/(streaming::detail::hex(textHash(id))+"-"+std::to_string(edge)+".json");
}
void writeAssetPreviewCache(const std::filesystem::path& root,const std::filesystem::path& folder,const std::string& id,
                           const CachedAssetPreview& preview,bool publishIndex,std::stop_token token) {
    checkCanceled(token);
    if(!preview.image || preview.edge<16 || preview.edge>512 || preview.image->width!=preview.edge || preview.image->height!=preview.edge)
        throw std::runtime_error("Preview cache dimensions are invalid.");
    if(preview.files.size()>8192 || preview.materials.size()>8192 || preview.locations.size()>8192)
        throw std::runtime_error("Preview cache descriptor is too large.");
    if(publishIndex && preview.sourceFingerprints.size()!=preview.files.size())
        throw std::runtime_error("Preview cache publication requires exact render-time source versions.");
    Json sources=Json::array();
    for(size_t i=0;i<preview.files.size();++i) {
        const auto& file=preview.files[i];
        const auto size=std::filesystem::file_size(file);const auto modified=std::filesystem::last_write_time(file).time_since_epoch().count();
        std::uint64_t actual{};const auto key=contentHash(file,token,&actual);
        if(!preview.sourceFingerprints.empty() && (preview.sourceFingerprints.size()!=preview.files.size() || preview.sourceFingerprints[i]!=actual))
            throw std::runtime_error("Preview render source changed before cache publication.");
        if(size!=std::filesystem::file_size(file) || modified!=std::filesystem::last_write_time(file).time_since_epoch().count())
            throw std::runtime_error("Preview source changed while writing its cache.");
        sources.push_back({{"file",storedPath(root,file)},{"size",size},{"modified",modified},{"hash",key}});
    }
    std::vector<unsigned char> encoded;const auto error=lodepng::encode(encoded,preview.image->rgba,preview.edge,preview.edge);
    if(error)throw std::runtime_error(std::string("Cannot encode preview: ")+lodepng_error_text(error));
    checkCanceled(token);
    const auto imageFile=assetPreviewImagePath(folder,id,preview.signature);
    writeFileAtomically(imageFile,std::string(reinterpret_cast<const char*>(encoded.data()),encoded.size()));
    writeFileAtomically(std::filesystem::path(imageFile.wstring()+L".json"),Json{{"version",ImageVersion},{"id",id},{"signature",preview.signature},{"width",preview.edge},{"height",preview.edge}}.dump()+"\n");
    if(!publishIndex)return;
    Json materials=Json::array(),locations=Json::array();
    for(const auto& material:preview.materials)materials.push_back({{"id",material.id},{"hash",material.fingerprint}});
    for(const auto& location:preview.locations)locations.push_back({{"id",location.id},{"file",storedPath(root,location.file)}});
    checkCanceled(token);
    writeFileAtomically(assetPreviewIndexPath(folder,id,preview.edge),Json{{"version",IndexVersion},{"id",id},{"signature",preview.signature},
        {"edge",preview.edge},{"shader",preview.shaderSignature},{"policy",preview.policyFingerprint},
        {"sources",std::move(sources)},{"materials",std::move(materials)},{"locations",std::move(locations)}}.dump()+"\n");
}
CachedAssetPreview readAssetPreviewCache(const std::filesystem::path& root,const std::filesystem::path& folder,const std::string& id,
                                        std::uint32_t edge,std::uint64_t shaderSignature,std::uint64_t policyFingerprint,std::stop_token token) {
    checkCanceled(token);std::ifstream input(assetPreviewIndexPath(folder,id,edge));const auto record=Json::parse(input);
    if(record.at("version")!=IndexVersion || record.at("id")!=id || record.at("edge")!=edge ||
       record.at("shader")!=shaderSignature || record.at("policy")!=policyFingerprint)
        throw std::runtime_error("Preview cache descriptor is stale.");
    if(record.at("sources").size()>8192 || record.at("materials").size()>8192 || record.at("locations").size()>8192)
        throw std::runtime_error("Preview cache descriptor is too large.");
    CachedAssetPreview result;result.signature=record.at("signature");result.edge=edge;result.shaderSignature=shaderSignature;result.policyFingerprint=policyFingerprint;
    std::vector<std::pair<std::uintmax_t,std::filesystem::file_time_type>> states;
    for(const auto& source:record.at("sources")) {
        checkCanceled(token);const auto file=restoredPath(root,source.at("file"));
        const auto size=std::filesystem::file_size(file);const auto time=std::filesystem::last_write_time(file);const auto modified=time.time_since_epoch().count();
        if(size!=source.at("size").get<std::uintmax_t>() || (modified!=source.at("modified").get<std::int64_t>() && contentHash(file,token)!=source.at("hash").get<std::uint64_t>()))
            throw std::runtime_error("Preview source changed.");
        result.files.push_back(file);states.emplace_back(size,time);
    }
    for(const auto& material:record.at("materials"))result.materials.push_back({material.at("id"),material.at("hash")});
    for(const auto& location:record.at("locations"))result.locations.push_back({location.at("id"),restoredPath(root,location.at("file"))});
    checkCanceled(token);auto image=std::make_shared<DecodedImage>(decodeManagedImage(assetPreviewImagePath(folder,id,result.signature)));
    if(image->width!=edge || image->height!=edge)throw std::runtime_error("Preview cache image dimensions are invalid.");
    for(size_t i=0;i<result.files.size();++i)if(std::filesystem::file_size(result.files[i])!=states[i].first || std::filesystem::last_write_time(result.files[i])!=states[i].second)
        throw std::runtime_error("Preview source changed during cache loading.");
    result.image=std::move(image);return result;
}
} // namespace crimson
