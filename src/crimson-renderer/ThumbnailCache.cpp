#include "ThumbnailCache.h"
#include <algorithm>

namespace crimson {
struct ThumbnailCache::Entry {
    std::uint64_t signature{},used{};
    std::size_t bytes{};
    bgfx::TextureHandle texture{BGFX_INVALID_HANDLE};
    ~Entry(){if(bgfx::isValid(texture))bgfx::destroy(texture);}
};
ThumbnailCache::ThumbnailCache(std::size_t budget):budget_(budget){}
ThumbnailCache::~ThumbnailCache()=default;
bool ThumbnailCache::publish(const std::string& id,std::uint64_t signature,const DecodedImage& image) {
    if(id.empty()||!image.width||!image.height||image.width>512||image.height>512||image.rgba.size()!=std::size_t(image.width)*image.height*4)return false;
    if(const auto found=entries_.find(id);found!=entries_.end()&&found->second->signature==signature){found->second->used=++clock_;return true;}
    auto next=std::make_unique<Entry>();next->signature=signature;next->used=++clock_;next->bytes=image.rgba.size();
    next->texture=bgfx::createTexture2D(static_cast<std::uint16_t>(image.width),static_cast<std::uint16_t>(image.height),false,1,bgfx::TextureFormat::RGBA8,
        BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP,bgfx::copy(image.rgba.data(),static_cast<std::uint32_t>(image.rgba.size())));
    if(!bgfx::isValid(next->texture))return false;
    if(const auto old=entries_.find(id);old!=entries_.end())bytes_-=old->second->bytes;
    bytes_+=next->bytes;entries_.insert_or_assign(id,std::move(next));
    while(bytes_>budget_&&entries_.size()>1){const auto victim=std::min_element(entries_.begin(),entries_.end(),[](const auto& a,const auto& b){return a.second->used<b.second->used;});
        bytes_-=victim->second->bytes;entries_.erase(victim);}
    return true;
}
bgfx::TextureHandle ThumbnailCache::find(const std::string& id,std::uint64_t signature) const {
    const auto found=entries_.find(id);if(found==entries_.end()||(signature&&found->second->signature!=signature))return BGFX_INVALID_HANDLE;
    found->second->used=++clock_;return found->second->texture;
}
void ThumbnailCache::erase(const std::string& id){const auto found=entries_.find(id);if(found==entries_.end())return;bytes_-=found->second->bytes;entries_.erase(found);}
void ThumbnailCache::clear(){entries_.clear();bytes_=0;}
void ThumbnailCache::trim(std::size_t target){while(bytes_>target&&!entries_.empty()){const auto victim=std::min_element(entries_.begin(),entries_.end(),[](const auto& a,const auto& b){return a.second->used<b.second->used;});
    bytes_-=victim->second->bytes;entries_.erase(victim);}}
} // namespace crimson
