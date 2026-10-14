#include "ProceduralSky.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace crimson::procedural {
namespace {
constexpr float Pi=std::numbers::pi_v<float>;
float finite(float value,float fallback,float lo,float hi) {
    return std::isfinite(value) ? std::clamp(value,lo,hi) : fallback;
}
float linear(float value) {
    value=finite(value,0,0,1);
    return value<=.04045f ? value/12.92f : std::pow((value+.055f)/1.055f,2.4f);
}
Vec4 color(const Vec3& value,float w) {return {linear(value[0]),linear(value[1]),linear(value[2]),w};}
Vec3 xyz(const Vec4& value) {return {value[0],value[1],value[2]};}
Vec3 mix(const Vec3& a,const Vec3& b,float t) {
    return {a[0]+(b[0]-a[0])*t,a[1]+(b[1]-a[1])*t,a[2]+(b[2]-a[2])*t};
}
// Circular footprint overlap in the angular tangent plane. Unlike widening a
// smoothstep alone, the covered fraction preserves a subpixel disk's energy.
float diskCoverage(float separation,float radius,float footprint) {
    if (radius<=0) return 0;
    if (footprint<=1e-12f) return separation<=radius ? 1.f : 0.f;
    const float sampleRadius=std::sqrt(std::min(footprint,4*Pi)/Pi);
    if (separation>=radius+sampleRadius) return 0;
    if (separation<=std::abs(radius-sampleRadius))
        return std::min(1.f,(radius*radius)/(sampleRadius*sampleRadius));
    const double d=separation,r=radius,s=sampleRadius;
    const double x=std::clamp(((d-s)*(d+s)+r*r)/(2*d*r),-1.,1.);
    const double height=r*std::sqrt(std::max(0.,1-x*x));
    const double a=std::acos(x),b=std::atan2(height,d-r*x);
    const auto segment=[](double angle) {
        const double square=angle*angle;
        return angle<.1 ? angle*square*(2./3-square*(2./15-square*4./315)) : angle-std::sin(angle)*std::cos(angle);
    };
    return static_cast<float>(std::clamp((r*r*segment(a)+s*s*segment(b))/(Pi*s*s),0.,1.));
}
float radicalInverse(std::uint32_t bits) {
    bits=(bits<<16)|(bits>>16);
    bits=((bits&0x55555555u)<<1)|((bits&0xaaaaaaaau)>>1);
    bits=((bits&0x33333333u)<<2)|((bits&0xccccccccu)>>2);
    bits=((bits&0x0f0f0f0fu)<<4)|((bits&0xf0f0f0f0u)>>4);
    bits=((bits&0x00ff00ffu)<<8)|((bits&0xff00ff00u)>>8);
    return static_cast<float>(bits)*2.3283064365386963e-10f;
}
FilterKernel makeDiffuseKernel() {
    FilterKernel result;result.diffuse=true;result.count=DiffuseSampleCount;
    for (std::uint16_t i=0;i<result.count;++i) {
        const float radius=std::sqrt((float(i)+.5f)/result.count),phi=2*Pi*radicalInverse(i);
        const float z=std::sqrt(std::max(0.f,1-radius*radius));
        result.samples[i]={radius*std::cos(phi),radius*std::sin(phi),z,Pi/(result.count*z)};
    }
    return result;
}
std::array<FilterKernel,SpecularMipCount> makeSpecularKernels() {
    std::array<FilterKernel,SpecularMipCount> result{};
    for (std::uint8_t mip=0;mip<SpecularMipCount;++mip) {
        auto& kernel=result[mip];
        const double roughness=RoughnessFloor+(1-RoughnessFloor)*double(mip)/(SpecularMipCount-1);
        const double a2=roughness*roughness*roughness*roughness;
        for (std::uint16_t i=0;i<SpecularSampleCount;++i) {
            const double xi=radicalInverse(i);
            const double cosine=std::sqrt((1-xi)/(1+(a2-1)*xi));
            const double sine=std::sqrt(std::max(0.,1-cosine*cosine));
            const double phi=2*std::numbers::pi*double(i)/SpecularSampleCount;
            const float z=static_cast<float>(2*cosine*cosine-1);
            if (z<=0) continue;
            const double denominator=(1-cosine*cosine)+a2*cosine*cosine;
            const double distribution=a2/(std::numbers::pi*denominator*denominator);
            kernel.samples[kernel.count++]={2*cosine*std::cos(phi)*sine,2*cosine*std::sin(phi)*sine,z,4/(SpecularSampleCount*distribution)};
        }
    }
    return result;
}
}

Parameters parameters(const ProceduralSkySettings& settings,const ProceduralSun& sun) {
    Parameters result;
    result.values[0]=color(settings.skyTop,.6f/finite(settings.skyCurve,.15f,.001f,10));
    result.values[1]=color(settings.skyHorizon,0);
    result.values[2]=color(settings.groundBottom,.6f/finite(settings.groundCurve,.02f,.001f,10));
    result.values[3]=color(settings.groundHorizon,0);
    const float radius=finite(settings.sunDiameter,.5f,0,360)*Pi/360;
    const float halo=std::max(radius,finite(settings.sunHaloExtent,30,0,180)*Pi/180);
    const auto direction=math::normalized(sun.direction,{0,1,0});
    const float intensity=sun.enabled ? finite(sun.intensity,0,0,10000) : 0;
    result.values[4]={direction[0],direction[1],direction[2],std::cos(radius)};
    result.values[5]=color(sun.color,std::cos(halo));
    for (std::size_t i=0;i<3;++i) result.values[5][i]*=intensity;
    result.values[6]={1.6f/std::pow(finite(settings.sunHaloCurve,.15f,.001f,10),1.4f),radius,halo,intensity>0 ? 1.f : 0.f};
    if (intensity<=0) {
        // Sun pose/color/shape cannot invalidate illumination while disabled.
        result.values[4]={0,1,0,1};result.values[5]={};result.values[6]={};
    }
    return result;
}

std::uint64_t cacheKey(const Parameters& parameters) {
    std::uint64_t hash=14695981039346656037ull;
    const auto append=[&](std::uint32_t value) {
        for (int byte=0;byte<4;++byte) {hash^=(value>>(byte*8))&255u;hash*=1099511628211ull;}
    };
    append(EvaluationVersion);append(DiffuseSize);append(SpecularSize);append(DiffuseSampleCount);append(SpecularSampleCount);
    for (const auto& value:parameters.values) for (float component:value)
        append(std::bit_cast<std::uint32_t>(component==0 ? 0.f : component));
    return hash;
}
std::uint64_t cacheKey(const ProceduralSkySettings& settings,const ProceduralSun& sun) {return cacheKey(parameters(settings,sun));}

Vec3 radiance(const Parameters& p,const Vec3& direction,float footprint) {
    const auto d=math::normalized(direction,{0,1,0});
    const float y=std::clamp(d[1],-1.f,1.f);
    if (y<0) return mix(xyz(p.values[2]),xyz(p.values[3]),std::pow(1+y,p.values[2][3]));
    auto sky=mix(xyz(p.values[0]),xyz(p.values[1]),std::pow(1-y,p.values[0][3]));
    if (p.values[6][3]<=0) return sky;
    const auto sun=xyz(p.values[4]);
    const float cosine=std::clamp(math::dotFloat(sun,d),-1.f,1.f);
    const float separation=static_cast<float>(std::atan2(math::length(math::cross(sun,d)),cosine));
    const float disk=diskCoverage(separation,p.values[6][1],std::max(0.f,footprint));
    const float haloDenominator=p.values[4][3]-p.values[5][3];
    const float halo=haloDenominator>1e-7f ? std::pow(std::clamp((cosine-p.values[5][3])/haloDenominator,0.f,1.f),p.values[6][0]) : 0;
    return mix(sky,xyz(p.values[5]),halo+(1-halo)*disk);
}

const FilterKernel& diffuseKernel() {static const auto value=makeDiffuseKernel();return value;}
const FilterKernel& specularKernel(std::uint8_t mip) {
    static const auto value=makeSpecularKernels();
    if (mip>=SpecularMipCount) throw std::out_of_range("Invalid procedural sky roughness level.");
    return value[mip];
}
Vec3 filteredRadiance(const Parameters& parameters,const Vec3& normal,const FilterKernel& kernel,float outputSolidAngle) {
    const auto n=math::normalized(normal,{0,1,0});
    const auto t=math::normalized(math::cross(std::abs(n[2])<.999f ? Vec3{0,0,1} : Vec3{1,0,0},n));
    const auto b=math::cross(n,t);
    Vec3 sum{};float total=0;
    for (std::uint16_t i=0;i<kernel.count;++i) {
        const auto& sample=kernel.samples[i];
        const Vec3 direction{t[0]*sample[0]+b[0]*sample[1]+n[0]*sample[2],t[1]*sample[0]+b[1]*sample[1]+n[1]*sample[2],t[2]*sample[0]+b[2]*sample[1]+n[2]*sample[2]};
        const float weight=kernel.diffuse ? 1.f : sample[2];
        const auto value=radiance(parameters,direction,std::max(2*sample[3],outputSolidAngle));
        for (std::size_t c=0;c<3;++c) sum[c]+=value[c]*weight;
        total+=weight;
    }
    if (total>0) for (float& c:sum) c/=total;
    return sum;
}
} // namespace crimson::procedural
