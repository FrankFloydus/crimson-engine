#include "Environment.h"
#include "assets/ContentStorage.h"
#include "resources/AssetId.h"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string_view>
namespace crimson {
namespace {
std::filesystem::path fromUtf8(const std::string& text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}
std::vector<uint8_t> readSource(const std::filesystem::path& root, const std::string& id) {
    const auto relative = fromUtf8(id).lexically_normal();
    if (!isAssetUuid(id) && (relative.is_absolute() || relative.empty() || *relative.begin() != "assets"))
        throw std::runtime_error("Environment must reference a managed project asset.");
    for (const auto& part : relative) if (part == "..") throw std::runtime_error("Environment path leaves the project.");
    std::ifstream input(resolveAssetPath(root,id), std::ios::binary|std::ios::ate);
    const auto length = input.tellg();
    if (!input || length <= 0 || length > 256*1024*1024) throw std::runtime_error("Environment source is missing, empty or exceeds 256 MiB.");
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input) throw std::runtime_error("Environment source could not be read completely.");
    return bytes;
}
}
std::shared_ptr<const PreparedEnvironment> prepareEnvironmentCpu(const std::filesystem::path& projectRoot,const std::string& sourceId) {
    constexpr std::string_view BuiltinSource="Crimson studio environment v1;512x256;linear HDR";
    const auto source=sourceId.empty() ? std::vector<uint8_t>(BuiltinSource.begin(),BuiltinSource.end()) : readSource(projectRoot,sourceId);
    auto prepared=std::make_shared<PreparedEnvironment>();
    prepared->key=ibl::cacheKey(source,prepared->settings);
    std::ostringstream name; name << std::hex << std::setfill('0') << std::setw(16) << prepared->key << ".ibl";
    const auto cache=resolveAssetPath(projectRoot,"assets/cache/ibl")/name.str();
    const bool cached=ibl::readCache(cache,prepared->key,prepared->settings,prepared->data);
    bool saved=true;
    if (!cached) {
        prepared->data=ibl::bake(sourceId.empty() ? studio() : ibl::decode(source),prepared->settings);
        saved=ibl::writeCache(cache,prepared->key,prepared->settings,prepared->data);
    }
    prepared->status=(sourceId.empty() ? "Studio environment" : "Environment")+std::string(cached ? " loaded from cache." : " prepared and cached.");
    if (!saved) prepared->status="Environment prepared; cache could not be saved.";
    return prepared;
}
}
