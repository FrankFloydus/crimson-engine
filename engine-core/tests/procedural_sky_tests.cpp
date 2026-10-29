#include "crimson-renderer/ProceduralSky.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace {
using namespace crimson;
constexpr float Pi=std::numbers::pi_v<float>;
void check(bool value,const char* message) {if (!value) throw std::runtime_error(message);}
void close(const Vec3& value,const Vec3& expected,float tolerance=2e-5f) {
    for (std::size_t i=0;i<3;++i) check(std::abs(value[i]-expected[i])<=tolerance,"Procedural sky radiance differs from its reference.");
}
float linear(float value) {return value<=.04045f ? value/12.92f : std::pow((value+.055f)/1.055f,2.4f);}
ProceduralSkySettings blackSky() {
    ProceduralSkySettings value;value.skyTop=value.skyHorizon=value.groundBottom=value.groundHorizon={0,0,0};return value;
}
void gradientsAndCurves() {
    ProceduralSkySettings sky;ProceduralSun sun;
    const auto p=procedural::parameters(sky,sun);
    close(procedural::radiance(p,{0,1,0}),{linear(sky.skyTop[0]),linear(sky.skyTop[1]),linear(sky.skyTop[2])});
    close(procedural::radiance(p,{1,0,0}),{linear(sky.skyHorizon[0]),linear(sky.skyHorizon[1]),linear(sky.skyHorizon[2])});
    close(procedural::radiance(p,{0,-1,0}),{linear(sky.groundBottom[0]),linear(sky.groundBottom[1]),linear(sky.groundBottom[2])});
    // Godot's cosine-gradient exponent is .6/curve, not 1/curve.
    Vec3 expected;
    for (std::size_t i=0;i<3;++i) expected[i]=linear(sky.skyTop[i])+(linear(sky.skyHorizon[i])-linear(sky.skyTop[i]))*.0625f;
    close(procedural::radiance(p,{std::sqrt(.75f),.5f,0}),expected);
    sky.skyTop=sky.skyHorizon=sky.groundBottom=sky.groundHorizon={.5f,.5f,.5f};
    const auto constant=procedural::parameters(sky,sun);
    constexpr std::array normals{Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1},Vec3{0,-1,0},Vec3{1,2,3}};
    const Vec3 gray{.21404114f,.21404114f,.21404114f};
    for (const auto& n:normals) {
        close(procedural::filteredRadiance(constant,n,procedural::diffuseKernel()),gray);
        for (std::uint8_t mip=0;mip<procedural::SpecularMipCount;++mip)
            close(procedural::filteredRadiance(constant,n,procedural::specularKernel(mip)),gray);
    }
}
void solarDirectionAndFiltering() {
    auto sky=blackSky();sky.sunHaloExtent=sky.sunDiameter*.5f;
    ProceduralSun sun;sun.enabled=true;sun.intensity=2;sun.color={1,0,0};
    const auto p=procedural::parameters(sky,sun);
    close(procedural::radiance(p,{0,1,0}),{2,0,0});
    const float inside=.2f*Pi/180,outside=.3f*Pi/180;
    close(procedural::radiance(p,{std::sin(inside),std::cos(inside),0}),{2,0,0});
    close(procedural::radiance(p,{std::sin(outside),std::cos(outside),0}),{0,0,0});
    close(procedural::radiance(p,{0,-1,0}),{0,0,0});
    // A disk smaller than a filter footprint contributes its covered area,
    // including when no sample center happens to land within the disk itself.
    const float sampleRadius=.02f,footprint=Pi*sampleRadius*sampleRadius;
    const float radius=sky.sunDiameter*Pi/360;
    close(procedural::radiance(p,{0,1,0},footprint),{2*radius*radius/(sampleRadius*sampleRadius),0,0},1e-5f);
    double integral=0;
    constexpr int steps=4000;
    const double end=sampleRadius+radius;
    for (int i=0;i<steps;++i) {
        const double angle=(i+.5)*end/steps;
        const Vec3 direction{std::sin(angle),std::cos(angle),0};
        integral+=procedural::radiance(p,direction,footprint)[0]*2*std::numbers::pi*std::sin(angle)*end/steps;
    }
    const double expected=2*2*std::numbers::pi*(1-std::cos(double(radius)));
    check(std::abs(integral/expected-1)<.002,"Subpixel disk filtering did not conserve solar radiance integral.");
    // Rotating the authored Sun moves both point evaluation and filtered IBL.
    sun.direction={1,1,0};const auto rotated=procedural::parameters(sky,sun);
    close(procedural::radiance(rotated,{1,1,0}),{2,0,0});
    close(procedural::radiance(rotated,{0,1,0}),{0,0,0});
    check(procedural::filteredRadiance(p,{0,1,0},procedural::specularKernel(0),4.f/(128*128))[0]>
        procedural::filteredRadiance(rotated,{0,1,0},procedural::specularKernel(0),4.f/(128*128))[0],
        "Sun rotation did not move the filtered specular highlight.");
    sky.sunDiameter=20;sky.sunHaloExtent=10;
    const auto wide=procedural::parameters(sky,ProceduralSun{{0,1,0},{1,1,1},2,true});
    const float diffuse=procedural::filteredRadiance(wide,{0,1,0},procedural::diffuseKernel())[0];
    check(std::abs(diffuse-2*std::pow(std::sin(10.f*Pi/180),2))<.009f,"Diffuse cubemap is not normalized as irradiance/pi.");
}
void keysAndExtremes() {
    ProceduralSkySettings sky;ProceduralSun sun;
    const auto disabled=procedural::cacheKey(sky,sun);
    sun.direction={1,-2,3};sun.intensity=99;sun.color={1,0,0};
    sky.sunDiameter=5;sky.sunHaloExtent=45;
    check(procedural::cacheKey(sky,sun)==disabled,"Disabled Sun changes invalidated the environment.");
    sun.enabled=true;
    const auto active=procedural::cacheKey(sky,sun);
    check(active!=disabled,"Enabling the Sun did not invalidate the environment.");
    sun.intensity=3;
    check(procedural::cacheKey(sky,sun)!=active,"Solar energy changes did not invalidate the environment.");
    sun.direction={0,1,0};const auto normalized=procedural::cacheKey(sky,sun);sun.direction={0,4,0};
    check(procedural::cacheKey(sky,sun)==normalized,"Equivalent Sun directions have different environment keys.");
    sky.skyCurve=.001f;sky.groundCurve=10;sky.sunHaloCurve=.001f;
    sky.sunDiameter=0;sky.sunHaloExtent=0;sun.direction={0,0,0};
    const auto extremes=procedural::parameters(sky,sun);
    constexpr std::array directions{Vec3{},Vec3{0,1,0},Vec3{0,-1,0},Vec3{1,0,0},Vec3{1,1e-7f,0}};
    for (const auto& d:directions) for (float footprint:{0.f,1e-6f,.1f,4*Pi})
        for (float value:procedural::radiance(extremes,d,footprint))
            check(std::isfinite(value) && value>=0,"Degenerate sky or Sun settings produced invalid HDR radiance.");
    sun.direction={std::numeric_limits<float>::quiet_NaN(),0,0};sun.intensity=std::numeric_limits<float>::infinity();
    const auto sanitized=procedural::parameters(sky,sun);
    for (const auto& v:sanitized.values) for (float value:v) check(std::isfinite(value),"Renderer sky parameters were not sanitized.");
}
}
int main() {
    try {gradientsAndCurves();solarDirectionAndFiltering();keysAndExtremes();std::cout<<"Procedural sky checks passed.\n";return 0;}
    catch (const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
