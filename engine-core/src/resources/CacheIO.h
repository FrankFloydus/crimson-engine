#pragma once
#include "assets/AtomicFile.h"
#include <bit>
#include <cstring>
#include <cstdint>
#include <fstream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <array>
#include <algorithm>
#include <type_traits>

namespace crimson::streaming::detail {
inline constexpr std::uint64_t HashSeed = 14695981039346656037ull;
inline void hash(std::uint64_t& value, const void* data, std::size_t size) {
    for (const auto byte : std::span(static_cast<const unsigned char*>(data),size)) { value ^= byte; value *= 1099511628211ull; }
}
template<class T> void hash(std::uint64_t& value,const T& data) { static_assert(std::is_trivially_copyable_v<T>); hash(value,&data,sizeof data); }
template<class T> void append(std::string& bytes,const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    static_assert(std::endian::native == std::endian::little);
    bytes.append(reinterpret_cast<const char*>(&value),sizeof value);
}
inline void appendBytes(std::string& bytes,const void* value,std::size_t size) { bytes.append(static_cast<const char*>(value),size); }
template<class T> T read(std::istream& input) {
    T value{};
    if (!input.read(reinterpret_cast<char*>(&value),sizeof value)) throw std::runtime_error("Derived resource cache is incomplete.");
    return value;
}
inline void readBytes(std::istream& input,void* target,std::size_t size) {
    if (size>static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()) ||
        !input.read(static_cast<char*>(target),static_cast<std::streamsize>(size))) throw std::runtime_error("Derived resource payload is incomplete.");
}
inline std::string hex(std::uint64_t value) {
    constexpr char digits[]="0123456789abcdef"; std::string text(16,'0');
    for (int i=15;i>=0;--i) { text[static_cast<std::size_t>(i)]=digits[value&15]; value>>=4; } return text;
}
inline bool cacheFileMatches(const std::filesystem::path& path,std::string_view contents) {
    std::ifstream input(path,std::ios::binary|std::ios::ate);
    if(!input || input.tellg()<0 || static_cast<std::uint64_t>(input.tellg())!=contents.size())return false;
    input.seekg(0);std::array<char,64*1024> block{};
    for(std::size_t offset=0;offset<contents.size();) {
        const auto count=std::min(block.size(),contents.size()-offset);
        if(!input.read(block.data(),static_cast<std::streamsize>(count)) || std::memcmp(block.data(),contents.data()+offset,count)!=0)return false;
        offset+=count;
    }
    return true;
}
inline void writeCacheFileAtomically(const std::filesystem::path& path,std::string_view contents) {
    // Different managed model identities can share content-addressed geometry.
    // Preserve a matching immutable file so active Windows readers never need
    // to grant delete sharing for an otherwise unnecessary replacement.
    if(cacheFileMatches(path,contents))return;
    try {writeFileAtomically(path,contents);}
    catch(const std::exception& error) {
        // A competing builder may have published the same complete version.
        // Accept only exact bytes; damaged cache files must still be repaired.
        if(cacheFileMatches(path,contents))return;
        const auto bytes=path.generic_u8string();
        throw std::runtime_error("Could not publish derived cache '"+std::string(bytes.begin(),bytes.end())+"': "+error.what());
    }
}
}
