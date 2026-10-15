#include "ShaderSourceBaseline.h"
#include "Paths.h"
#include <Windows.h>
#include <bcrypt.h>
#include <array>
#include <fstream>
#include <string>

namespace crimson {
namespace {
struct Algorithm {
    BCRYPT_ALG_HANDLE handle{};
    ~Algorithm(){if(handle)BCryptCloseAlgorithmProvider(handle,0);}
};
struct Digest {
    BCRYPT_HASH_HANDLE handle{};
    ~Digest(){if(handle)BCryptDestroyHash(handle);}
};
std::string sha256(BCRYPT_ALG_HANDLE algorithm,const std::filesystem::path& file) {
    std::ifstream input(file,std::ios::binary);if(!input)return {};
    Digest digest;
    if(BCryptCreateHash(algorithm,&digest.handle,nullptr,0,nullptr,0,0)<0)return {};
    std::array<char,64*1024> buffer;
    while(input) {
        input.read(buffer.data(),buffer.size());
        if(input.gcount() && BCryptHashData(digest.handle,reinterpret_cast<PUCHAR>(buffer.data()),static_cast<ULONG>(input.gcount()),0)<0)return {};
    }
    if(!input.eof())return {};
    std::array<unsigned char,32> bytes{};
    if(BCryptFinishHash(digest.handle,bytes.data(),static_cast<ULONG>(bytes.size()),0)<0)return {};
    constexpr char hex[]="0123456789abcdef";
    std::string result;result.reserve(64);
    for(const auto byte:bytes){result.push_back(hex[byte>>4]);result.push_back(hex[byte&15]);}
    return result;
}
}
bool shaderSourcesMatchEmbedded(const ShaderCompileConfig& config,std::span<const ShaderSourceBaselineEntry> entries) noexcept {
    if(entries.empty())return false;
    try {
        Algorithm algorithm;
        if(BCryptOpenAlgorithmProvider(&algorithm.handle,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return false;
        for(const auto& entry:entries) {
            if(entry.root==ShaderSourceRoot::Contract){if(config.buildContract!=entry.sha256)return false;continue;}
            const auto file=entry.root==ShaderSourceRoot::Compiler?config.compiler:
                (entry.root==ShaderSourceRoot::Sources?config.sourceDirectory:config.includeDirectory)/pathFromUtf8(entry.relativePath);
            if(entry.sha256.size()!=64 || sha256(algorithm.handle,file)!=entry.sha256)return false;
        }
        return true;
    } catch(...) {return false;}
}
}
