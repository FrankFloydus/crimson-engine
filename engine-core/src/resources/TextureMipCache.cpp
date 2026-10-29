#include "TextureMipCache.h"
#include "CacheIO.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace crimson::streaming {
namespace {
constexpr std::array<char,8> Magic{'C','R','M','I','P','0','0','1'};
constexpr std::uint64_t HeaderBytes=8+4+8+4+4, EntryBytes=4+4+8+8+8;
double linear(double value) { return value<=.04045 ? value/12.92 : std::pow((value+.055)/1.055,2.4); }
double gamma(double value) { return value<=.0031308 ? value*12.92 : 1.055*std::pow(value,1/2.4)-.055; }
std::uint8_t byte(double value) { return static_cast<std::uint8_t>(std::clamp(std::lround(value*255),0l,255l)); }
void validate(std::uint32_t width,std::uint32_t height,std::span<const std::uint8_t> pixels) {
    if (!width || !height || width>65535 || height>65535 || static_cast<std::uint64_t>(width)*height*4!=pixels.size())
        throw std::runtime_error("Texture mip input dimensions or pixels are invalid.");
}
std::uint64_t checksum(std::span<const std::uint8_t> bytes) { auto result=detail::HashSeed; detail::hash(result,bytes.data(),bytes.size()); return result; }
std::uint16_t half(float value) {
    if(!std::isfinite(value)||std::abs(value)>65504.f)throw std::runtime_error("Generated float texture contains a value outside the finite RGBA16F range.");
    const auto bits=std::bit_cast<std::uint32_t>(value);const auto sign=static_cast<std::uint16_t>((bits>>16)&0x8000u);
    auto exponent=static_cast<int>((bits>>23)&255u)-112;auto mantissa=bits&0x7fffffu;
    if(exponent<=0) {
        if(exponent < -10)return sign;
        mantissa|=0x800000u;const auto shift=static_cast<unsigned>(14-exponent);
        return static_cast<std::uint16_t>(sign|((mantissa+((1u<<(shift-1))-1u)+((mantissa>>shift)&1u))>>shift));
    }
    mantissa+=0xfffu+((mantissa>>13)&1u);
    if(mantissa&0x800000u){mantissa=0;++exponent;}
    return static_cast<std::uint16_t>(sign|(static_cast<unsigned>(exponent)<<10)|(mantissa>>13));
}
TextureMip packFloatMip(const FloatImage& image,std::stop_token token) {
    TextureMip result{image.width,image.height,std::vector<std::uint8_t>(image.rgba.size()*2),ImagePixelFormat::RGBA16F};
    for(std::size_t i=0;i<image.rgba.size();++i){if((i&65535u)==0&&token.stop_requested())throw std::runtime_error("Generated float mip request canceled.");const auto value=half(image.rgba[i]);result.rgba[i*2]=static_cast<std::uint8_t>(value);result.rgba[i*2+1]=static_cast<std::uint8_t>(value>>8);}
    return result;
}
}
std::uint64_t textureContentKey(std::span<const std::uint8_t> bytes,TextureRole role) {
    auto key=detail::HashSeed; detail::hash(key,TextureCacheVersion); detail::hash(key,role); detail::hash(key,bytes.data(),bytes.size()); return key;
}
std::vector<TextureMip> generateTextureMips(std::uint32_t width,std::uint32_t height,std::span<const std::uint8_t> rgba,TextureRole role) {
    validate(width,height,rgba);
    std::vector<TextureMip> result; result.push_back({width,height,{rgba.begin(),rgba.end()}});
    while (result.back().width>1 || result.back().height>1) {
        const auto& source=result.back();
        const auto w=std::max(1u,source.width/2),h=std::max(1u,source.height/2);
        TextureMip target{w,h,std::vector<std::uint8_t>(static_cast<std::size_t>(w)*h*4)};
        for (std::uint32_t y=0;y<h;++y) for (std::uint32_t x=0;x<w;++x) {
            const double x0=static_cast<double>(x)*source.width/w,x1=static_cast<double>(x+1)*source.width/w;
            const double y0=static_cast<double>(y)*source.height/h,y1=static_cast<double>(y+1)*source.height/h;
            std::array<double,4> sum{}; double weight=0;
            for (auto sy=static_cast<std::uint32_t>(std::floor(y0));sy<std::min(source.height,static_cast<std::uint32_t>(std::ceil(y1)));++sy)
                for (auto sx=static_cast<std::uint32_t>(std::floor(x0));sx<std::min(source.width,static_cast<std::uint32_t>(std::ceil(x1)));++sx) {
                    const double area=(std::min(x1,static_cast<double>(sx+1))-std::max(x0,static_cast<double>(sx)))*
                        (std::min(y1,static_cast<double>(sy+1))-std::max(y0,static_cast<double>(sy)));
                    const auto offset=(static_cast<std::size_t>(sy)*source.width+sx)*4; weight+=area;
                    for (std::size_t c=0;c<4;++c) {
                        double value=source.rgba[offset+c]/255.0;
                        if (c<3 && role==TextureRole::Color) value=linear(value);
                        else if (c<3 && role==TextureRole::Normal) { value=value*2-1; if (c<2 && std::abs(value)<=.00392257) value=0; }
                        sum[c]+=value*area;
                    }
                }
            for (auto& value:sum) value/=weight;
            if (role==TextureRole::Normal) {
                const double length=std::sqrt(sum[0]*sum[0]+sum[1]*sum[1]+sum[2]*sum[2]);
                if (length<1e-8) { sum[0]=sum[1]=0; sum[2]=1; }
                else for (std::size_t c=0;c<3;++c) sum[c]/=length;
                for (std::size_t c=0;c<3;++c) sum[c]=sum[c]*.5+.5;
            } else if (role==TextureRole::Color) for (std::size_t c=0;c<3;++c) sum[c]=gamma(sum[c]);
            const auto offset=(static_cast<std::size_t>(y)*w+x)*4;
            for (std::size_t c=0;c<4;++c) target.rgba[offset+c]=byte(sum[c]);
        }
        result.push_back(std::move(target));
    }
    return result;
}
TextureMipCache generatedTextureMipCache(std::uint64_t key,TextureMip image) {
    if(image.format!=ImagePixelFormat::RGBA8)throw std::runtime_error("Generated RGBA8 texture received an incompatible pixel format.");
    TextureMipCache result;result.key=key;result.role=TextureRole::Data;
    result.memory=generateTextureMips(image.width,image.height,image.rgba,TextureRole::Data);
    for(const auto& mip:result.memory)result.levels.push_back({mip.width,mip.height,0,mip.rgba.size(),checksum(mip.rgba)});
    return result;
}
TextureMipCache generatedFloatTextureMipCache(std::uint64_t key,FloatImage image,std::stop_token token) {
    if(!image.width||!image.height||image.width>4096||image.height>4096||std::uint64_t(image.width)*image.height*4!=image.rgba.size())
        throw std::runtime_error("Generated float texture requires dimensions 1-4096 and complete RGBA pixels.");
    TextureMipCache result;result.key=key;result.role=TextureRole::Data;result.format=ImagePixelFormat::RGBA16F;
    while(true) {
        if(token.stop_requested())throw std::runtime_error("Generated float mip request canceled.");
        result.memory.push_back(packFloatMip(image,token));
        if(image.width==1&&image.height==1)break;
        FloatImage next{std::max(1u,image.width/2),std::max(1u,image.height/2),{}};
        next.rgba.resize(std::size_t(next.width)*next.height*4);
        for(std::uint32_t y=0;y<next.height;++y) {
            if(token.stop_requested())throw std::runtime_error("Generated float mip request canceled.");
            const double y0=double(y)*image.height/next.height,y1=double(y+1)*image.height/next.height;
            for(std::uint32_t x=0;x<next.width;++x) {
                const double x0=double(x)*image.width/next.width,x1=double(x+1)*image.width/next.width;
                std::array<double,4> sum{};double weight{};
                for(auto sy=static_cast<std::uint32_t>(std::floor(y0));sy<std::min(image.height,static_cast<std::uint32_t>(std::ceil(y1)));++sy)
                    for(auto sx=static_cast<std::uint32_t>(std::floor(x0));sx<std::min(image.width,static_cast<std::uint32_t>(std::ceil(x1)));++sx) {
                        const auto area=(std::min(x1,double(sx+1))-std::max(x0,double(sx)))*(std::min(y1,double(sy+1))-std::max(y0,double(sy)));
                        const auto first=(std::size_t(sy)*image.width+sx)*4;weight+=area;
                        for(std::size_t c=0;c<4;++c)sum[c]+=double(image.rgba[first+c])*area;
                    }
                const auto first=(std::size_t(y)*next.width+x)*4;
                for(std::size_t c=0;c<4;++c)next.rgba[first+c]=static_cast<float>(sum[c]/weight);
            }
        }
        image=std::move(next);
    }
    for(const auto& mip:result.memory)result.levels.push_back({mip.width,mip.height,0,mip.rgba.size(),checksum(mip.rgba)});
    return result;
}
TextureMipCache generatedTextureThumbnail(const TextureMipCache& linear) {
    if(linear.format!=ImagePixelFormat::RGBA8)throw std::runtime_error("Generated display thumbnails require RGBA8 source pixels.");
    if(linear.memory.size()!=linear.levels.size()||linear.memory.empty())throw std::runtime_error("Generated thumbnail requires resident linear mip pixels.");
    TextureMipCache result;result.key=linear.key;result.role=TextureRole::Data;result.memory=linear.memory;
    for(auto& mip:result.memory) {
        for(std::size_t offset=0;offset<mip.rgba.size();offset+=4)for(unsigned channel=0;channel<3;++channel)mip.rgba[offset+channel]=byte(gamma(mip.rgba[offset+channel]/255.0));
        result.levels.push_back({mip.width,mip.height,0,mip.rgba.size(),checksum(mip.rgba)});
    }
    return result;
}
std::size_t textureMipCacheBytes(const TextureMipCache& cache) {
    std::size_t bytes=sizeof(cache)+cache.levels.capacity()*sizeof(MipInfo)+cache.memory.capacity()*sizeof(TextureMip);
    for(const auto& mip:cache.memory)bytes+=mip.rgba.capacity();
    return bytes;
}
TextureMipCache writeTextureMipCache(const std::filesystem::path& file,std::uint64_t key,TextureRole role,std::span<const TextureMip> levels) {
    if (levels.empty() || levels.size()>32) throw std::runtime_error("Texture mip cache has no valid levels.");
    TextureMipCache descriptor{file,key,role,{}}; std::uint64_t offset=HeaderBytes+EntryBytes*levels.size();
    for (std::size_t i=0;i<levels.size();++i) {
        const auto& mip=levels[i];if(mip.format!=ImagePixelFormat::RGBA8)throw std::runtime_error("Persistent texture mip cache only accepts RGBA8 source pixels.");validate(mip.width,mip.height,mip.rgba);
        if (i && (mip.width!=std::max(1u,levels[i-1].width/2) || mip.height!=std::max(1u,levels[i-1].height/2)))
            throw std::runtime_error("Texture mip chain dimensions are inconsistent.");
        descriptor.levels.push_back({mip.width,mip.height,offset,mip.rgba.size(),checksum(mip.rgba)}); offset+=mip.rgba.size();
    }
    if (levels.back().width!=1 || levels.back().height!=1) throw std::runtime_error("Texture mip cache must contain a complete tail.");
    std::string bytes; bytes.reserve(static_cast<std::size_t>(offset));
    detail::append(bytes,Magic); detail::append(bytes,TextureCacheVersion); detail::append(bytes,key); detail::append(bytes,role);
    detail::append(bytes,static_cast<std::uint32_t>(levels.size()));
    for (const auto& info:descriptor.levels) {
        detail::append(bytes,info.width); detail::append(bytes,info.height); detail::append(bytes,info.offset); detail::append(bytes,info.bytes); detail::append(bytes,info.checksum);
    }
    for (const auto& mip:levels) detail::appendBytes(bytes,mip.rgba.data(),mip.rgba.size());
    detail::writeCacheFileAtomically(file,bytes); return descriptor;
}
TextureMipCache readTextureMipCache(const std::filesystem::path& file,std::uint64_t key,TextureRole role) {
    std::ifstream input(file,std::ios::binary);
    if (detail::read<std::array<char,8>>(input)!=Magic || detail::read<std::uint32_t>(input)!=TextureCacheVersion ||
        detail::read<std::uint64_t>(input)!=key || detail::read<TextureRole>(input)!=role)
        throw std::runtime_error("Texture mip cache is stale or damaged.");
    const auto count=detail::read<std::uint32_t>(input);
    if (!count || count>32) throw std::runtime_error("Texture mip cache level count is invalid.");
    const auto size=std::filesystem::file_size(file); std::uint64_t expected=HeaderBytes+EntryBytes*count;
    TextureMipCache result{file,key,role,{}};
    for (std::uint32_t i=0;i<count;++i) {
        MipInfo info;
        info.width=detail::read<std::uint32_t>(input); info.height=detail::read<std::uint32_t>(input);
        info.offset=detail::read<std::uint64_t>(input); info.bytes=detail::read<std::uint64_t>(input); info.checksum=detail::read<std::uint64_t>(input);
        if (!info.width || !info.height || info.width>65535 || info.height>65535 || info.offset!=expected ||
            info.bytes!=static_cast<std::uint64_t>(info.width)*info.height*4 || info.offset>size || info.bytes>size-info.offset ||
            (i && (info.width!=std::max(1u,result.levels.back().width/2) || info.height!=std::max(1u,result.levels.back().height/2))))
            throw std::runtime_error("Texture mip cache ranges are invalid.");
        expected+=info.bytes; result.levels.push_back(info);
    }
    if (expected!=size || result.levels.back().width!=1 || result.levels.back().height!=1) throw std::runtime_error("Texture mip cache is incomplete.");
    return result;
}
TextureMip readTextureMip(const TextureMipCache& cache,std::uint32_t level) {
    if (level>=cache.levels.size()) throw std::out_of_range("Texture mip level is unavailable.");
    if(!cache.memory.empty()) {
        if(cache.memory.size()!=cache.levels.size())throw std::runtime_error("Generated mip payload is incomplete.");
        return cache.memory[level];
    }
    const auto& info=cache.levels[level]; std::ifstream input(cache.file,std::ios::binary);
    input.seekg(static_cast<std::streamoff>(info.offset));
    TextureMip result{info.width,info.height,std::vector<std::uint8_t>(static_cast<std::size_t>(info.bytes))};
    detail::readBytes(input,result.rgba.data(),result.rgba.size());
    if (checksum(result.rgba)!=info.checksum) throw std::runtime_error("Texture mip checksum mismatch.");
    return result;
}
std::uint32_t coarseTextureMip(std::span<const MipInfo> levels,std::uint32_t maximum) {
    if (levels.empty()) return 0;
    for (std::uint32_t i=0;i<levels.size();++i) if (std::max(levels[i].width,levels[i].height)<=maximum) return i;
    return static_cast<std::uint32_t>(levels.size()-1);
}
}
