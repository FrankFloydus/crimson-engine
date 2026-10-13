#include "Environment.h"

#include <bimg/decode.h>
#include <bx/allocator.h>
#include <bx/error.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace crimson {
namespace {
constexpr float Pi = 3.14159265358979323846f;
constexpr double PiDouble = 3.14159265358979323846;
constexpr float RoughnessFloor = 0.045f;
constexpr uint64_t FnvOffset = 14695981039346656037ull, FnvPrime = 1099511628211ull;
constexpr uint64_t Sampler = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP;

float dot(const Vec3& a, const Vec3& b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
Vec3 normalize(Vec3 value) {
    const float squared = dot(value, value);
    if (!std::isfinite(squared) || squared < 1e-12f) return {0,1,0};
    const float reciprocal = 1/std::sqrt(squared);
    for (float& v : value) v *= reciprocal;
    return value;
}
Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]};
}
float finiteRadiance(float value) { return std::isfinite(value) ? std::clamp(value, 0.0f, 60000.0f) : 0.0f; }
float roughnessValue(float value) { return std::isfinite(value) ? std::clamp(value, RoughnessFloor, 1.0f) : 1.0f; }
float radicalInverse(uint32_t bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xaaaaaaaau) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xccccccccu) >> 2u);
    bits = ((bits & 0x0f0f0f0fu) << 4u) | ((bits & 0xf0f0f0f0u) >> 4u);
    bits = ((bits & 0x00ff00ffu) << 8u) | ((bits & 0xff00ff00u) >> 8u);
    return static_cast<float>(bits) * 2.3283064365386963e-10f;
}
Vec3 ggxHalf(uint32_t i, uint32_t samples, float roughness) {
    const float alpha = roughness*roughness, alphaSquared = alpha*alpha;
    const float xi = radicalInverse(i);
    const float cosine = std::sqrt((1-xi) / (1+(alphaSquared-1)*xi));
    const float sine = std::sqrt(std::max(0.0f, 1-cosine*cosine));
    const float phi = 2*Pi*static_cast<float>(i)/static_cast<float>(samples);
    return {std::cos(phi)*sine, std::sin(phi)*sine, cosine};
}
struct Basis {
    Vec3 n, t, b;
    explicit Basis(const Vec3& normal) : n(normalize(normal)) {
        t = normalize(cross(std::abs(n[2]) < .999f ? Vec3{0,0,1} : Vec3{1,0,0}, n));
        b = cross(n, t);
    }
    Vec3 world(const Vec3& local) const {
        return {t[0]*local[0]+b[0]*local[1]+n[0]*local[2],
                t[1]*local[0]+b[1]*local[1]+n[1]*local[2],
                t[2]*local[0]+b[2]*local[1]+n[2]*local[2]};
    }
};
void hashBytes(uint64_t& hash, const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i=0; i<size; ++i) { hash ^= bytes[i]; hash *= FnvPrime; }
}
void hashValue(uint64_t& hash, uint32_t value) { hashBytes(hash, &value, sizeof(value)); }
void validateSettings(const ibl::Settings& s) {
    for (const auto size : {s.diffuseSize, s.specularSize, s.lutSize})
        if (!std::has_single_bit(size) || size > 512) throw std::invalid_argument("Invalid IBL texture size.");
    for (const auto samples : {s.diffuseSamples, s.specularSamples, s.lutSamples})
        if (!samples || samples > 65536) throw std::invalid_argument("Invalid IBL sample count.");
}
ibl::Bake emptyBake(const ibl::Settings& s) {
    validateSettings(s);
    ibl::Bake result;
    result.diffuse = {s.diffuseSize, std::vector<float>(static_cast<size_t>(s.diffuseSize)*s.diffuseSize*6*4)};
    for (uint32_t size=s.specularSize;; size/=2) {
        result.specular.push_back({size, std::vector<float>(static_cast<size_t>(size)*size*6*4)});
        if (size == 1) break;
    }
    result.brdf.resize(static_cast<size_t>(s.lutSize)*s.lutSize*2);
    return result;
}
template<class Function> void fillCube(ibl::CubeLevel& level, Function&& sample) {
    for (uint8_t face=0; face<6; ++face) for (uint32_t y=0; y<level.size; ++y) for (uint32_t x=0; x<level.size; ++x) {
        const Vec3 direction = ibl::cubeDirection(face, (static_cast<float>(x)+.5f)/level.size, (static_cast<float>(y)+.5f)/level.size);
        const Vec3 color = sample(direction,ibl::cubeTexelSolidAngle(level.size,x,y));
        const size_t offset = ((static_cast<size_t>(face)*level.size+y)*level.size+x)*4;
        for (size_t c=0; c<3; ++c) level.rgba[offset+c] = finiteRadiance(color[c]);
        level.rgba[offset+3] = 1;
    }
}
struct FilterSample {
    Vec3 direction;
    float solidAngle;
};
std::vector<FilterSample> diffuseSamples(uint32_t count) {
    std::vector<FilterSample> result(count);
    for (uint32_t i=0; i<count; ++i) {
        const float radius = std::sqrt((static_cast<float>(i)+.5f)/static_cast<float>(count));
        const float phi = 2*Pi*radicalInverse(i);
        const float noL=std::sqrt(std::max(0.0f,1-radius*radius));
        result[i] = {{radius*std::cos(phi),radius*std::sin(phi),noL},Pi/(static_cast<float>(count)*noL)};
    }
    return result;
}
std::vector<FilterSample> specularSamples(uint32_t count, float roughness) {
    std::vector<FilterSample> result;
    result.reserve(count);
    for (uint32_t i=0; i<count; ++i) {
        const Vec3 half = ggxHalf(i, count, roughnessValue(roughness));
        const Vec3 light{2*half[2]*half[0], 2*half[2]*half[1], 2*half[2]*half[2]-1};
        if (light[2]>0) result.push_back({light,ibl::ggxSampleSolidAngle(roughness,half[2],count)});
    }
    return result;
}
Vec3 convolve(const ibl::SourcePyramid& source, const Vec3& normal,
              const std::vector<FilterSample>& samples, bool weighted, float outputSolidAngle) {
    const Basis basis(normal);
    Vec3 sum{};
    float weightSum = 0;
    for (const auto& sample : samples) {
        const float weight = weighted ? sample.direction[2] : 1;
        // Filtered importance sampling (GPU Gems 3, ch.20 / Filament CubemapIBL):
        // low-PDF samples represent larger solid angles, so they must not fetch
        // an isolated full-resolution sun texel. K=2 gives half a mip of overlap:
        // concentrated-emitter checks retain smooth samples with less GGX-tail
        // bias than K=4. The destination footprint also antialiases the mirror mip.
        const float footprint=std::max(2*sample.solidAngle,outputSolidAngle);
        const auto color = source.sample(basis.world(sample.direction),footprint);
        for (size_t c=0; c<3; ++c) sum[c] += color[c]*weight;
        weightSum += weight;
    }
    for (float& c : sum) c = finiteRadiance(c/std::max(weightSum, 1e-6f));
    return sum;
}
Vec3 sampleImageUv(const ibl::Image& image,float u,float v) {
    const float x=u*static_cast<float>(image.width)-.5f, y=v*static_cast<float>(image.height)-.5f;
    const int ix=static_cast<int>(std::floor(x)), iy=static_cast<int>(std::floor(y));
    const float fx=x-std::floor(x), fy=y-std::floor(y);
    const auto texel = [&](int tx,int ty) -> const Vec3& {
        tx=(tx%static_cast<int>(image.width)+static_cast<int>(image.width))%static_cast<int>(image.width);
        ty=std::clamp(ty,0,static_cast<int>(image.height)-1);
        return image.pixels[static_cast<size_t>(ty)*image.width+static_cast<size_t>(tx)];
    };
    const auto &a=texel(ix,iy), &b=texel(ix+1,iy), &c=texel(ix,iy+1), &d=texel(ix+1,iy+1);
    Vec3 result{};
    for (size_t channel=0;channel<3;++channel)
        result[channel]=finiteRadiance((a[channel]*(1-fx)+b[channel]*fx)*(1-fy)+(c[channel]*(1-fx)+d[channel]*fx)*fy);
    return result;
}
ibl::Image reduceSource(const ibl::Image& source) {
    const uint32_t width=std::max(1u,source.width/2), height=std::max(1u,source.height/2);
    ibl::Image result{width,height,std::vector<Vec3>(static_cast<size_t>(width)*height)};
    // Each destination pixel covers exact source-cell overlaps, including odd
    // dimensions. Integral weights use dOmega=dPhi*(cos(theta0)-cos(theta1)).
    for (uint32_t y=0;y<height;++y) {
        const double y0=static_cast<double>(y)*source.height/height;
        const double y1=static_cast<double>(y+1)*source.height/height;
        const uint32_t firstY=static_cast<uint32_t>(std::floor(y0));
        const uint32_t endY=std::min(source.height,static_cast<uint32_t>(std::ceil(y1)));
        for (uint32_t x=0;x<width;++x) {
            const double x0=static_cast<double>(x)*source.width/width;
            const double x1=static_cast<double>(x+1)*source.width/width;
            const uint32_t firstX=static_cast<uint32_t>(std::floor(x0));
            const uint32_t endX=std::min(source.width,static_cast<uint32_t>(std::ceil(x1)));
            std::array<double,3> sum{};
            double totalWeight=0;
            for (uint32_t sy=firstY;sy<endY;++sy) {
                const double theta0=PiDouble*std::max(y0,static_cast<double>(sy))/source.height;
                const double theta1=PiDouble*std::min(y1,static_cast<double>(sy+1))/source.height;
                const double latitudeWeight=std::cos(theta0)-std::cos(theta1);
                for (uint32_t sx=firstX;sx<endX;++sx) {
                    const double weight=latitudeWeight*(std::min(x1,static_cast<double>(sx+1))-std::max(x0,static_cast<double>(sx)));
                    const auto& color=source.pixels[static_cast<size_t>(sy)*source.width+sx];
                    for (size_t c=0;c<3;++c) sum[c]+=finiteRadiance(color[c])*weight;
                    totalWeight+=weight;
                }
            }
            auto& color=result.pixels[static_cast<size_t>(y)*width+x];
            for (size_t c=0;c<3;++c) color[c]=static_cast<float>(sum[c]/totalWeight);
        }
    }
    return result;
}
template<class T> bool readValue(std::istream& input, T& value) {
    return static_cast<bool>(input.read(reinterpret_cast<char*>(&value), sizeof(value)));
}
template<class T> void writeValue(std::ostream& output, const T& value) {
    output.write(reinterpret_cast<const char*>(&value), sizeof(value));
}
uint64_t payloadHash(const ibl::Bake& result) {
    uint64_t hash=FnvOffset;
    hashBytes(hash, result.diffuse.rgba.data(), result.diffuse.rgba.size()*sizeof(float));
    for (const auto& level : result.specular) hashBytes(hash, level.rgba.data(), level.rgba.size()*sizeof(float));
    hashBytes(hash, result.brdf.data(), result.brdf.size()*sizeof(float));
    return hash;
}
bool validPixels(const std::vector<float>& pixels, float maximum) {
    return std::all_of(pixels.begin(), pixels.end(), [maximum](float value) { return std::isfinite(value) && value>=0 && value<=maximum; });
}
std::filesystem::path fromUtf8(const std::string& text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}
std::vector<uint8_t> readSource(const std::filesystem::path& root, const std::string& id) {
    const auto relative = fromUtf8(id).lexically_normal();
    if (relative.is_absolute() || relative.empty() || *relative.begin() != "assets")
        throw std::runtime_error("Environment must reference a managed project asset.");
    for (const auto& part : relative) if (part == "..") throw std::runtime_error("Environment path leaves the project.");
    std::ifstream input(root/relative, std::ios::binary|std::ios::ate);
    const auto length = input.tellg();
    if (!input || length <= 0 || length > 256*1024*1024) throw std::runtime_error("Environment source is missing, empty or exceeds 256 MiB.");
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input) throw std::runtime_error("Environment source could not be read completely.");
    return bytes;
}
ibl::Image decodeSource(std::span<const uint8_t> source) {
    if (source.empty() || source.size()>256*1024*1024) throw std::runtime_error("Environment source is empty or exceeds 256 MiB.");
    bx::DefaultAllocator allocator;
    bx::Error error;
    std::unique_ptr<bimg::ImageContainer, decltype(&bimg::imageFree)> image(
        bimg::imageParse(&allocator, source.data(), static_cast<uint32_t>(source.size()), bimg::TextureFormat::Count, &error), bimg::imageFree);
    const bool parsed = error.isOk();
    if (!parsed || !image || image->m_cubeMap || image->m_numLayers != 1 || image->m_depth > 1)
        throw std::runtime_error("Could not decode the equirectangular environment.");
    if (!image->m_width || !image->m_height || static_cast<uint64_t>(image->m_width)*image->m_height > 32*1024*1024)
        throw std::runtime_error("Environment dimensions exceed the 32 megapixel preprocessing limit.");
    const auto format = image->m_format;
    const bool hdr = format == bimg::TextureFormat::RGBA32F || format == bimg::TextureFormat::RGBA16F || format == bimg::TextureFormat::RGB9E5F;
    const bool grayscale = format == bimg::TextureFormat::R8 || format == bimg::TextureFormat::RG8 ||
                           format == bimg::TextureFormat::R16 || format == bimg::TextureFormat::RG16;
    if (format != bimg::TextureFormat::RGBA32F) {
        auto* converted = bimg::imageConvert(&allocator, bimg::TextureFormat::RGBA32F, *image, false);
        if (!converted) throw std::runtime_error("Environment float conversion failed.");
        image.reset(converted);
    }
    bimg::ImageMip mip{};
    if (!bimg::imageGetRawData(*image, 0, 0, image->m_data, image->m_size, mip) ||
        mip.m_format != bimg::TextureFormat::RGBA32F || static_cast<uint64_t>(mip.m_width)*mip.m_height*16 > mip.m_size)
        throw std::runtime_error("Environment float pixels are incomplete.");
    ibl::Image result{mip.m_width, mip.m_height, std::vector<Vec3>(static_cast<size_t>(mip.m_width)*mip.m_height)};
    for (size_t i=0; i<result.pixels.size(); ++i) {
        std::array<float,4> rgba{};
        std::memcpy(rgba.data(), mip.m_data+i*16, 16);
        for (size_t c=0; c<3; ++c) {
            float value = finiteRadiance(rgba[grayscale ? 0 : c]);
            if (!hdr) value = value <= .04045f ? value/12.92f : std::pow((value+.055f)/1.055f,2.4f);
            result.pixels[i][c] = finiteRadiance(value);
        }
    }
    return result;
}
ibl::Image studio() {
    ibl::Image image{512,256,std::vector<Vec3>(512*256)};
    const auto key = normalize(Vec3{-1,1,1}), fill = normalize(Vec3{1,.35f,-.65f}), rim = normalize(Vec3{-.2f,.4f,-1});
    for (uint32_t y=0; y<image.height; ++y) for (uint32_t x=0; x<image.width; ++x) {
        const float theta = Pi*(static_cast<float>(y)+.5f)/image.height;
        const float phi = 2*Pi*((static_cast<float>(x)+.5f)/image.width-.5f);
        const Vec3 direction{std::sin(theta)*std::cos(phi),std::cos(theta),std::sin(theta)*std::sin(phi)};
        const float sky = .08f+.22f*std::max(direction[1],0.0f);
        const float keyLight = 10*std::pow(std::max(dot(direction,key),0.0f),96.0f);
        const float fillLight = 3*std::pow(std::max(dot(direction,fill),0.0f),24.0f);
        const float rimLight = 6*std::pow(std::max(dot(direction,rim),0.0f),160.0f);
        image.pixels[static_cast<size_t>(y)*image.width+x] = {
            sky+keyLight+fillLight*.35f+rimLight*.7f,
            sky+keyLight*.88f+fillLight*.6f+rimLight*.8f,
            sky+keyLight*.7f+fillLight+rimLight};
    }
    return image;
}
void destroyTexture(bgfx::TextureHandle handle) { if (bgfx::isValid(handle)) bgfx::destroy(handle); }
bgfx::TextureHandle uploadCube(const std::vector<ibl::CubeLevel>& levels) {
    const auto handle = bgfx::createTextureCube(static_cast<uint16_t>(levels.front().size), levels.size()>1, 1,
                                               bgfx::TextureFormat::RGBA32F, Sampler);
    if (!bgfx::isValid(handle)) throw std::runtime_error("IBL cubemap allocation failed.");
    for (size_t mip=0; mip<levels.size(); ++mip) {
        const auto& level = levels[mip];
        const uint32_t faceFloats = level.size*level.size*4;
        for (uint8_t face=0; face<6; ++face) bgfx::updateTextureCube(handle,0,face,static_cast<uint8_t>(mip),0,0,
            static_cast<uint16_t>(level.size),static_cast<uint16_t>(level.size),
            bgfx::copy(level.rgba.data()+static_cast<size_t>(face)*faceFloats,faceFloats*sizeof(float)));
    }
    return handle;
}
} // namespace

namespace ibl {
Image decode(std::span<const uint8_t> source) { return decodeSource(source); }
Vec3 Image::sample(const Vec3& direction) const {
    if (!width || !height || pixels.size()!=static_cast<size_t>(width)*height) return {};
    const auto n = normalize(direction);
    return sampleImageUv(*this,std::atan2(n[2],n[0])/(2*Pi)+.5f,std::acos(std::clamp(n[1],-1.0f,1.0f))/Pi);
}
float equirectTexelSolidAngle(uint32_t width,uint32_t height,uint32_t row) {
    if (!width || !height || row>=height) throw std::invalid_argument("Invalid equirectangular texel.");
    const double theta0=PiDouble*row/height, theta1=PiDouble*(static_cast<double>(row)+1)/height;
    return static_cast<float>((2*PiDouble/width)*(std::cos(theta0)-std::cos(theta1)));
}
float cubeTexelSolidAngle(uint32_t size,uint32_t x,uint32_t y) {
    if (!size || x>=size || y>=size) throw std::invalid_argument("Invalid cubemap texel.");
    const double u0=2.0*x/size-1, v0=2.0*y/size-1;
    const double u1=2.0*(static_cast<double>(x)+1)/size-1, v1=2.0*(static_cast<double>(y)+1)/size-1;
    const auto area=[](double u,double v) { return std::atan2(u*v,std::sqrt(u*u+v*v+1)); };
    return static_cast<float>(area(u1,v1)-area(u0,v1)-area(u1,v0)+area(u0,v0));
}
float ggxSampleSolidAngle(float roughness,float noH,uint32_t samples) {
    if (!samples) throw std::invalid_argument("GGX footprint requires samples.");
    const double r=roughnessValue(roughness), alphaSquared=r*r*r*r;
    const double cosine=std::isfinite(noH) ? std::clamp(static_cast<double>(noH),0.0,1.0) : 0;
    const double denominator=(1-cosine*cosine)+alphaSquared*cosine*cosine;
    const double distribution=alphaSquared/(PiDouble*denominator*denominator);
    // Sampling H by GGX transforms to p(L)=D*NoH/(4*VoH)=D/4 for V=N.
    return static_cast<float>(4/(static_cast<double>(samples)*distribution));
}
SourcePyramid::SourcePyramid(const Image& source) {
    if (!source.width || !source.height || source.pixels.size()!=static_cast<size_t>(source.width)*source.height)
        throw std::invalid_argument("IBL source image is incomplete.");
    levels.push_back(source);
    while (levels.back().width>1 || levels.back().height>1) levels.push_back(reduceSource(levels.back()));
    rowSolidAngles_.reserve(levels.size());
    for (const auto& level : levels) {
        auto& rows=rowSolidAngles_.emplace_back(level.height);
        for (uint32_t y=0;y<level.height;++y) rows[y]=equirectTexelSolidAngle(level.width,level.height,y);
    }
}
Vec3 SourcePyramid::sample(const Vec3& direction,float solidAngle) const {
    const auto n=normalize(direction);
    const float u=std::atan2(n[2],n[0])/(2*Pi)+.5f, v=std::acos(std::clamp(n[1],-1.0f,1.0f))/Pi;
    const auto area=[&](size_t level) {
        const auto& rows=rowSolidAngles_[level];
        const float y=v*static_cast<float>(rows.size())-.5f;
        const int row=static_cast<int>(std::floor(y)), last=static_cast<int>(rows.size())-1;
        const float fraction=y-std::floor(y);
        return rows[static_cast<size_t>(std::clamp(row,0,last))]*(1-fraction)+
               rows[static_cast<size_t>(std::clamp(row+1,0,last))]*fraction;
    };
    // Compare actual spherical texel areas at this latitude. The common
    // .5*log2(omegaSample/omegaTexel) assumes exact 2x dimensions and fails at
    // polar rows or the last non-square/odd-sized levels.
    size_t lower=0;
    float lowerArea=area(0);
    if (!(solidAngle>lowerArea)) return sampleImageUv(levels.front(),u,v);
    while (lower+1<levels.size()) {
        const float upperArea=area(lower+1);
        if (solidAngle<=upperArea) {
            const float blend=std::clamp(std::log(solidAngle/lowerArea)/std::log(upperArea/lowerArea),0.0f,1.0f);
            const auto a=sampleImageUv(levels[lower],u,v), b=sampleImageUv(levels[lower+1],u,v);
            return {a[0]+(b[0]-a[0])*blend,a[1]+(b[1]-a[1])*blend,a[2]+(b[2]-a[2])*blend};
        }
        ++lower; lowerArea=upperArea;
    }
    return sampleImageUv(levels.back(),u,v);
}
Vec3 cubeDirection(uint8_t face, float u, float v) {
    const float x=2*u-1, y=2*v-1;
    // D3D texture cube addressing: row zero points up on each face.
    switch (face) {
    case 0: return normalize({1,-y,-x});
    case 1: return normalize({-1,-y,x});
    case 2: return normalize({x,1,y});
    case 3: return normalize({x,-1,-y});
    case 4: return normalize({x,-y,1});
    case 5: return normalize({-x,-y,-1});
    default: throw std::invalid_argument("Invalid cubemap face.");
    }
}
std::array<float,2> integrateBrdf(float noV, float roughness, uint32_t samples) {
    if (!samples) throw std::invalid_argument("BRDF integration requires samples.");
    noV = std::isfinite(noV) ? std::clamp(noV,1e-5f,1.0f) : 1e-5f;
    roughness = roughnessValue(roughness);
    const float alpha=roughness*roughness, alphaSquared=alpha*alpha;
    const Vec3 view{std::sqrt(std::max(0.0f,1-noV*noV)),0,noV};
    double scale=0, bias=0;
    for (uint32_t i=0; i<samples; ++i) {
        const auto half = ggxHalf(i,samples,roughness);
        const float voH=std::clamp(dot(view,half),0.0f,1.0f), noH=std::max(half[2],1e-8f);
        const float noL=std::clamp(2*voH*half[2]-noV,0.0f,1.0f);
        if (noL<=0) continue;
        // Identical visibility and denominator clamp to fs_pbr.sc. GGX sampling
        // cancels D from BRDF * NoL / pdf, where pdf=D*NoH/(4*VoH).
        const float smithL=noV*std::sqrt(noL*noL*(1-alphaSquared)+alphaSquared);
        const float smithV=noL*std::sqrt(noV*noV*(1-alphaSquared)+alphaSquared);
        const float visibility=.5f/std::max(smithL+smithV,1e-6f);
        const float weight=4*visibility*noL*voH/noH;
        const float fc=std::pow(1-voH,5.0f);
        scale += (1-fc)*weight;
        bias += fc*weight;
    }
    return {static_cast<float>(scale/samples),static_cast<float>(bias/samples)};
}
Vec3 integrateDiffuse(const Image& image, const Vec3& normal, uint32_t samples) {
    if (!samples) throw std::invalid_argument("Diffuse integration requires samples.");
    return convolve(SourcePyramid(image),normal,diffuseSamples(samples),false,0);
}
Vec3 prefilterSpecular(const Image& image, const Vec3& normal, float roughness, uint32_t samples) {
    if (!samples) throw std::invalid_argument("Specular integration requires samples.");
    return prefilterSpecular(SourcePyramid(image),normal,roughness,samples);
}
Vec3 prefilterSpecular(const SourcePyramid& source, const Vec3& normal, float roughness,uint32_t samples,float outputSolidAngle) {
    if (!samples) throw std::invalid_argument("Specular integration requires samples.");
    return convolve(source,normal,specularSamples(samples,roughness),true,outputSolidAngle);
}
Bake bake(const Image& image, const Settings& settings) {
    if (!image.width || !image.height || image.pixels.size()!=static_cast<size_t>(image.width)*image.height)
        throw std::invalid_argument("IBL source image is incomplete.");
    auto result=emptyBake(settings);
    const SourcePyramid source(image);
    const auto diffuseDirections=diffuseSamples(settings.diffuseSamples);
    fillCube(result.diffuse,[&](const Vec3& n,float footprint) { return convolve(source,n,diffuseDirections,false,footprint); });
    for (size_t mip=0; mip<result.specular.size(); ++mip) {
        const float roughness=RoughnessFloor+(1-RoughnessFloor)*static_cast<float>(mip)/static_cast<float>(std::max(size_t{1},result.specular.size()-1));
        const auto directions=specularSamples(settings.specularSamples,roughness);
        fillCube(result.specular[mip],[&](const Vec3& n,float footprint) { return convolve(source,n,directions,true,footprint); });
    }
    for (uint32_t y=0; y<settings.lutSize; ++y) for (uint32_t x=0; x<settings.lutSize; ++x) {
        const auto value=integrateBrdf((static_cast<float>(x)+.5f)/settings.lutSize,
                                     (static_cast<float>(y)+.5f)/settings.lutSize,settings.lutSamples);
        const size_t offset=(static_cast<size_t>(y)*settings.lutSize+x)*2;
        result.brdf[offset]=value[0]; result.brdf[offset+1]=value[1];
    }
    return result;
}
uint64_t cacheKey(std::span<const uint8_t> source, const Settings& settings, uint32_t preprocessingVersion, uint32_t brdfVersion) {
    uint64_t hash=FnvOffset;
    hashValue(hash,preprocessingVersion); hashValue(hash,brdfVersion);
    hashValue(hash,settings.diffuseSize); hashValue(hash,settings.specularSize); hashValue(hash,settings.lutSize);
    hashValue(hash,settings.diffuseSamples); hashValue(hash,settings.specularSamples); hashValue(hash,settings.lutSamples);
    hashBytes(hash,source.data(),source.size());
    return hash;
}
bool readCache(const std::filesystem::path& path, uint64_t key, const Settings& settings, Bake& result) {
    try {
        std::ifstream input(path,std::ios::binary);
        std::array<char,8> magic{};
        uint64_t storedKey{}, checksum{};
        uint32_t preprocessingVersion{}, brdfVersion{};
        if (!readValue(input,magic) || magic!=std::array<char,8>{'C','R','I','B','L','0','0','1'} ||
            !readValue(input,preprocessingVersion) || preprocessingVersion!=PreprocessVersion ||
            !readValue(input,brdfVersion) || brdfVersion!=BrdfVersion ||
            !readValue(input,storedKey) || storedKey!=key || !readValue(input,checksum)) return false;
        auto candidate=emptyBake(settings);
        const auto readPixels=[&](std::vector<float>& pixels) {
            return static_cast<bool>(input.read(reinterpret_cast<char*>(pixels.data()),static_cast<std::streamsize>(pixels.size()*sizeof(float))));
        };
        if (!readPixels(candidate.diffuse.rgba) || !validPixels(candidate.diffuse.rgba,60000)) return false;
        for (auto& mip : candidate.specular) if (!readPixels(mip.rgba) || !validPixels(mip.rgba,60000)) return false;
        if (!readPixels(candidate.brdf) || !validPixels(candidate.brdf,8) || input.peek()!=std::char_traits<char>::eof() || payloadHash(candidate)!=checksum) return false;
        result=std::move(candidate);
        return true;
    } catch (...) { return false; } // Missing/corrupt/stale cache is regenerated from source.
}
bool writeCache(const std::filesystem::path& path, uint64_t key, const Settings& settings, const Bake& result) {
    try {
        validateSettings(settings);
        std::filesystem::create_directories(path.parent_path());
        auto temporary=path;
        temporary += ".tmp";
        std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
        writeValue(output,std::array<char,8>{'C','R','I','B','L','0','0','1'});
        writeValue(output,PreprocessVersion); writeValue(output,BrdfVersion);
        writeValue(output,key); writeValue(output,payloadHash(result));
        const auto writePixels=[&](const std::vector<float>& pixels) {
            output.write(reinterpret_cast<const char*>(pixels.data()),static_cast<std::streamsize>(pixels.size()*sizeof(float)));
        };
        writePixels(result.diffuse.rgba);
        for (const auto& mip : result.specular) writePixels(mip.rgba);
        writePixels(result.brdf);
        output.close();
        if (!output) return false;
        // Only a derived cache file is replaced; authored assets are never touched.
        std::error_code error;
        std::filesystem::remove(path,error);
        error.clear(); std::filesystem::rename(temporary,path,error);
        return !error;
    } catch (...) { return false; }
}
} // namespace ibl

EnvironmentLighting::~EnvironmentLighting() { destroyTexture(diffuse_); destroyTexture(specular_); destroyTexture(brdf_); }
bool EnvironmentLighting::prepare(const std::filesystem::path& projectRoot, const std::string& sourceId) {
    bgfx::TextureHandle diffuse{BGFX_INVALID_HANDLE}, specular{BGFX_INVALID_HANDLE}, brdf{BGFX_INVALID_HANDLE};
    try {
        constexpr std::string_view BuiltinSource="Crimson studio environment v1;512x256;linear HDR";
        const auto source=sourceId.empty() ? std::vector<uint8_t>(BuiltinSource.begin(),BuiltinSource.end()) : readSource(projectRoot,sourceId);
        const ibl::Settings settings{};
        const auto key=ibl::cacheKey(source,settings);
        if (key==key_ && bgfx::isValid(diffuse_) && bgfx::isValid(specular_) && bgfx::isValid(brdf_)) {
            status_=sourceId.empty() ? "Studio environment ready." : "Environment ready.";
            return true;
        }
        std::ostringstream name; name << std::hex << std::setfill('0') << std::setw(16) << key << ".ibl";
        const auto cache=projectRoot/"assets"/"cache"/"ibl"/name.str();
        ibl::Bake data;
        const bool cached=ibl::readCache(cache,key,settings,data);
        bool saved=true;
        if (!cached) {
            data=ibl::bake(sourceId.empty() ? studio() : ibl::decode(source),settings);
            saved=ibl::writeCache(cache,key,settings,data);
        }
        if (!bgfx::isTextureValid(0,true,1,bgfx::TextureFormat::RGBA32F,Sampler) ||
            !bgfx::isTextureValid(0,false,1,bgfx::TextureFormat::RG32F,Sampler))
            throw std::runtime_error("GPU does not support the linear HDR IBL texture formats.");
        diffuse=uploadCube({data.diffuse});
        specular=uploadCube(data.specular);
        brdf=bgfx::createTexture2D(static_cast<uint16_t>(settings.lutSize),static_cast<uint16_t>(settings.lutSize),false,1,
            bgfx::TextureFormat::RG32F,Sampler,bgfx::copy(data.brdf.data(),static_cast<uint32_t>(data.brdf.size()*sizeof(float))));
        if (!bgfx::isValid(brdf)) throw std::runtime_error("IBL BRDF lookup allocation failed.");
        bgfx::setName(diffuse,"Environment irradiance / pi"); bgfx::setName(specular,"Environment GGX prefilter"); bgfx::setName(brdf,"Correlated Smith BRDF LUT");
        destroyTexture(diffuse_); destroyTexture(specular_); destroyTexture(brdf_);
        diffuse_=diffuse; specular_=specular; brdf_=brdf;
        key_=key; maxSpecularLod_=static_cast<float>(data.specular.size()-1);
        status_=(sourceId.empty() ? "Studio environment" : "Environment")+std::string(cached ? " loaded from cache." : " prepared and cached.");
        if (!saved) status_="Environment prepared; cache could not be saved.";
        return true;
    } catch (const std::exception& error) {
        destroyTexture(diffuse); destroyTexture(specular); destroyTexture(brdf);
        status_=std::string("Environment: ")+error.what();
        return false;
    }
}
} // namespace crimson
