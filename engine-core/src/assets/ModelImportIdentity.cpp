#include "ModelImportIdentity.h"
#include "MaterialAsset.h"
#include "ContentStorage.h"
#include "resources/AssetId.h"
#include "resources/Paths.h"
#include <Windows.h>
#include <bcrypt.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <climits>
#include <fstream>
#include <stdexcept>

#pragma comment(lib,"bcrypt.lib")

namespace crimson {
namespace {
namespace fs=std::filesystem;
using Json=nlohmann::json;
constexpr std::uintmax_t MaximumResourceBytes=512ull*1024*1024;
std::string utf8(const fs::path& path) {const auto value=path.generic_u8string();return {reinterpret_cast<const char*>(value.data()),value.size()};}
std::string lower(std::string value) {std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});return value;}
bool inside(const fs::path& file,const fs::path& root) {
    auto current=file.begin();for(auto part=root.begin();part!=root.end();++part,++current)
        if(current==file.end() || lower(utf8(*part))!=lower(utf8(*current)))return false;
    return true;
}
fs::path dependency(const fs::path& directory,const std::string& name) {
    const auto relative=pathFromUtf8(name),root=fs::weakly_canonical(directory);
    if(relative.empty() || relative.is_absolute() || relative.has_root_name())throw std::runtime_error("Model import identity contains an invalid managed path.");
    const auto result=fs::weakly_canonical(root/relative);
    if(result==root || !inside(result,root))throw std::runtime_error("Model import identity dependency escapes its managed package.");
    return result;
}
Json manifest(const fs::path& directory) {
    const auto file=directory/"manifest.json";
    if(fs::file_size(file)>16*1024*1024)throw std::runtime_error("Model import manifest exceeds the metadata size limit.");
    std::ifstream input(file);auto result=Json::parse(input);
    if(result.at("version")!=1)throw std::runtime_error("Unsupported model import manifest version.");
    return result;
}
ModelImportFileStamp stamp(const fs::path& file) {
    if(!fs::is_regular_file(file))throw std::runtime_error("Model import identity dependency is missing: "+utf8(file.filename()));
    return {file,fs::file_size(file),fs::last_write_time(file)};
}
class Sha256 {
public:
    Sha256() {
        if(BCryptOpenAlgorithmProvider(&algorithm_,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("Cannot initialize model import SHA-256.");
        DWORD bytes{},size{};
        if(BCryptGetProperty(algorithm_,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&size),sizeof(size),&bytes,0)<0) {
            BCryptCloseAlgorithmProvider(algorithm_,0);throw std::runtime_error("Cannot query model import SHA-256 storage.");
        }
        object_.resize(size);
        if(BCryptCreateHash(algorithm_,&hash_,object_.data(),size,nullptr,0,0)<0) {
            BCryptCloseAlgorithmProvider(algorithm_,0);throw std::runtime_error("Cannot create model import SHA-256.");
        }
    }
    ~Sha256(){if(hash_)BCryptDestroyHash(hash_);if(algorithm_)BCryptCloseAlgorithmProvider(algorithm_,0);}
    void append(const void* data,std::size_t bytes) {
        if(bytes>ULONG_MAX || BCryptHashData(hash_,const_cast<PUCHAR>(static_cast<const unsigned char*>(data)),static_cast<ULONG>(bytes),0)<0)
            throw std::runtime_error("Cannot hash model import bytes.");
    }
    void number(std::uint64_t value) {
        std::array<unsigned char,8> encoded{};for(unsigned i=0;i<8;++i)encoded[i]=static_cast<unsigned char>(value>>(8*i));append(encoded.data(),encoded.size());
    }
    std::string finish() {
        std::array<unsigned char,32> bytes{};
        if(BCryptFinishHash(hash_,bytes.data(),static_cast<ULONG>(bytes.size()),0)<0)throw std::runtime_error("Cannot finish model import SHA-256.");
        constexpr char digits[]="0123456789abcdef";std::string result;result.reserve(64);
        for(const auto byte:bytes){result+=digits[byte>>4];result+=digits[byte&15];}return result;
    }
private:
    BCRYPT_ALG_HANDLE algorithm_{};BCRYPT_HASH_HANDLE hash_{};std::vector<unsigned char> object_;
};
}
bool modelImportFilesUnchanged(const std::vector<ModelImportFileStamp>& files) {
    if(files.empty())return false;
    try {for(const auto& file:files)if(!fs::is_regular_file(file.file) || fs::file_size(file.file)!=file.bytes || fs::last_write_time(file.file)!=file.modified)return false;}
    catch(...) {return false;}return true;
}
std::string modelImportDataDigest(std::span<const std::uint8_t> bytes) {Sha256 hash;hash.append(bytes.data(),bytes.size());return hash.finish();}
ModelImportIdentity identifyModelImport(const fs::path& directory) {
    const auto originalManifest=stamp(directory/"manifest.json");
    const auto metadata=manifest(directory);std::vector<std::string> names{metadata.at("original").get<std::string>(),metadata.at("document").get<std::string>()};
    for(const auto& item:metadata.at("dependencies"))names.push_back(item.at("path").get<std::string>());
    std::sort(names.begin(),names.end());names.erase(std::unique(names.begin(),names.end()),names.end());
    ModelImportIdentity result;Sha256 hash;const std::string domain="Crimson complete model import v1";hash.append(domain.data(),domain.size());
    // External buffers may contain legal trailing bytes beyond byteLength. The
    // normalized managed buffer retains the declared range, while this checksum
    // keeps the exact source input version distinguishable after source removal.
    if(metadata.contains("sourceBufferExtras")) {
        const auto extra=metadata.at("sourceBufferExtras").dump();hash.number(extra.size());hash.append(extra.data(),extra.size());
    }
    for(const auto& name:names) {
        auto recorded=stamp(dependency(directory,name));if(!recorded.bytes || recorded.bytes>MaximumResourceBytes)throw std::runtime_error("Model import identity dependency has an invalid size.");
        hash.number(name.size());hash.append(name.data(),name.size());hash.number(recorded.bytes);
        std::ifstream input(recorded.file,std::ios::binary);std::array<char,64*1024> bytes{};std::uintmax_t remaining=recorded.bytes;
        while(remaining) {
            const auto count=static_cast<std::streamsize>(std::min<std::uintmax_t>(remaining,bytes.size()));
            if(!input.read(bytes.data(),count))throw std::runtime_error("Cannot read model import identity dependency.");
            hash.append(bytes.data(),static_cast<std::size_t>(count));remaining-=static_cast<std::uintmax_t>(count);
        }
        result.files.push_back(std::move(recorded));
    }
    result.files.push_back(originalManifest);
    if(!modelImportFilesUnchanged(result.files))throw std::runtime_error("Managed model files changed while their import identity was prepared.");
    result.digest=hash.finish();return result;
}
std::unordered_set<std::string> reservedModelImportIds(const fs::path& root) {
    const auto file=resolveAssetPath(root,".crimson/references.json");if(!fs::exists(file))return {};
    std::ifstream input(file);const auto state=Json::parse(input);
    if(state.at("version")!=1 || !state.at("deleted").is_array())throw std::runtime_error("Cannot read reserved asset identities for model import.");
    std::unordered_set<std::string> result;for(const auto& item:state.at("deleted"))result.insert(canonicalAssetId(item.get<std::string>()));return result;
}
bool modelImportIdentityReserved(const fs::path& root,const fs::path& directory,const std::function<bool(const std::string&)>& reservedId) {
    if(!reservedId)return false;
    const auto metadata=manifest(directory);const auto id=canonicalAssetId(assetIdAtPath(root,directory/"manifest.json"));if(reservedId(id))return true;
    for(const auto& item:metadata.at("dependencies"))
        if(reservedId(derivedAssetId(id,"image:"+item.at("path").get<std::string>())))return true;
    // Material sidecars have deterministic IDs. Count core glTF materials plus
    // its possible implicit default without decoding geometry or images.
    std::ifstream input(dependency(directory,metadata.at("document").get<std::string>()));const auto document=Json::parse(input);
    const auto count=document.value("materials",Json::array()).size();
    for(std::size_t i=0;i<=count;++i)if(reservedId(importedMaterialId(id,i)))return true;
    return false;
}
bool modelImportManifestMatches(const fs::path& directory,const std::string& digest) {
    try {const auto metadata=manifest(directory);const auto& identity=metadata.at("importIdentity");return identity.at("version")==1 && identity.at("sha256")==digest;}
    catch(...) {return false;}
}
std::optional<MatchingModelImport> findMatchingModelImport(const fs::path& root,const std::string& digest,const std::unordered_set<std::string>& reservedIds) {
    const auto models=fs::weakly_canonical(root);
    std::vector<fs::path> packages;
    if(contentStorageReady(root)) {
        for(const auto& entry:storedAssetPaths(root))if(entry.file.filename()=="manifest.json"&&fs::is_regular_file(entry.file))packages.push_back(entry.file.parent_path());
    } else if(fs::is_directory(models))for(const auto& item:fs::directory_iterator(models))if(item.is_directory() && !utf8(item.path().filename()).starts_with(".staging-") && fs::is_regular_file(item.path()/"manifest.json"))packages.push_back(item.path());
    std::sort(packages.begin(),packages.end());
    for(const auto& candidate:packages)try {
        const auto directory=fs::weakly_canonical(candidate);if(!contentStorageReady(root)&&directory.parent_path()!=models)continue;
        if(modelImportIdentityReserved(root,directory,[&](const auto& id){return reservedIds.contains(id);}))continue;
        const auto metadata=manifest(directory);
        if(metadata.contains("importIdentity") && !modelImportManifestMatches(directory,digest))continue;
        auto identity=identifyModelImport(directory);if(identity.digest!=digest)continue;
        return MatchingModelImport{directory,metadata.at("name").get<std::string>(),std::move(identity.files)};
    }catch(...) { /* A damaged/unrelated package is never reused or overwritten. */ }
    return {};
}
}
