#pragma once

#include "resources/TextureMipCache.h"
#include <bgfx/bgfx.h>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace crimson {
// Main-thread GPU owner for completed browser images. Visibility only touches
// LRU order; time away from the browser is not a reason to discard a thumbnail.
class ThumbnailCache final {
public:
    explicit ThumbnailCache(std::size_t budget=64ull*1024*1024);
    ~ThumbnailCache();
    ThumbnailCache(const ThumbnailCache&)=delete;
    ThumbnailCache& operator=(const ThumbnailCache&)=delete;
    bool publish(const std::string&,std::uint64_t signature,const DecodedImage&);
    bgfx::TextureHandle find(const std::string&,std::uint64_t signature=0) const;
    void erase(const std::string&);
    void clear();
    std::size_t bytes() const { return bytes_; }
    void trim(std::size_t target);
private:
    struct Entry;
    std::unordered_map<std::string,std::unique_ptr<Entry>> entries_;
    std::size_t budget_{},bytes_{};
    mutable std::uint64_t clock_{};
};
} // namespace crimson
