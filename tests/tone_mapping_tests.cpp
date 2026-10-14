#include "crimson-renderer/ToneMapping.h"
#include "crimson-renderer/Debanding.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace crimson;
void check(bool value,const char* message) {if (!value) throw std::runtime_error(message);}
void close(const Vec3& value,const Vec3& expected,float tolerance=2e-5f) {
    for (std::size_t i=0;i<3;++i) check(std::abs(value[i]-expected[i])<=tolerance,"Tone curve differs from independent Godot SDR reference values.");
}
void references() {
    // Double-precision evaluations of the local Godot Environment defaults:
    // ACES white=1 and exposure bias=1.8; AgX white=16.29, contrast=1.25.
    close(toneMap({.18f,.18f,.18f},ToneMapping::ACES),{.299983706f,.299983706f,.299980706f});
    close(toneMap({4,.5f,.1f},ToneMapping::ACES),{1.379220524f,.888100244f,.421342325f});
    close(toneMap({16,1,0},ToneMapping::ACES),{1.364712323f,1.147928481f,.789815721f});
    close(toneMap({.18f,.18f,.18f},ToneMapping::AgX),{.18f,.18f,.18f});
    close(toneMap({1,1,1},ToneMapping::AgX),{.596110849f,.596110849f,.596110849f});
    close(toneMap({4,.5f,.1f},ToneMapping::AgX),{.997273435f,.526761085f,.326478116f});
    close(toneMap({16,1,0},ToneMapping::AgX),{1.090135250f,.789032823f,.638281789f});
    close(toneMap({16.29f,16.29f,16.29f},ToneMapping::AgX),{1,1,1});
    close(toneMap({.01f,.01f,.01f},ToneMapping::AgX),{.005885784f,.005885784f,.005885784f});
    // Linear leaves HDR available for the final display clamp; legacy Reinhard
    // keeps its existing response, even though Godot's Reinhard has a white knob.
    close(toneMap({.18f,1,4},ToneMapping::Linear),{.18f,1,4});
    close(toneMap({.18f,1,4},ToneMapping::Reinhard),{.18f/1.18f,.5f,.8f});
}
void continuityAndEncoding() {
    constexpr std::array modes{ToneMapping::Reinhard,ToneMapping::Linear,ToneMapping::ACES,ToneMapping::AgX};
    for (auto mode:modes) {
        check(validToneMapping(mode) && *toneMappingName(mode),"Valid serialized tone mapping mode was rejected.");
        close(toneMap({0,0,0},mode),{0,0,0},1e-7f);
        close(toneMap({-1,-2,-3},mode),{0,0,0},1e-7f);
        float previous=-1;
        for (int step=0;step<=640;++step) {
            const float input=std::pow(10.f,-6.f+float(step)/80.f);
            const auto mapped=toneMap({input,input,input},mode);
            for (float channel:mapped) check(std::isfinite(channel),"Tone curve produced a nonfinite HDR result.");
            check(mapped[0]>=previous-2e-6f,"Neutral tone response is not monotonic.");
            check(std::abs(mapped[0]-mapped[1])<4e-5f && std::abs(mapped[0]-mapped[2])<4e-5f,
                "Tone curve introduced a neutral-axis color cast.");
            previous=mapped[0];
        }
    }
    check(!validToneMapping(static_cast<ToneMapping>(-1)) && !validToneMapping(static_cast<ToneMapping>(4)),"Invalid serialized tone mapping mode was accepted.");
    const auto low=toneMap({.179999f,.179999f,.179999f},ToneMapping::AgX);
    const auto high=toneMap({.180001f,.180001f,.180001f},ToneMapping::AgX);
    check(high[0]>=low[0] && high[0]-low[0]<3e-6f,"AgX toe/shoulder crossover is discontinuous.");
    // Middle gray remains display-linear, not prematurely sRGB encoded (~.461).
    const float gray=toneMap({.18f,.18f,.18f},ToneMapping::AgX)[0];
    check(std::abs(gray-.18f)<1e-6f && std::abs(gray-.461f)>.2f,"AgX applied an extra display transfer function.");
}
void displayDebanding() {
    const Vec3 encoded{24.25f/255,45.75f/255,80.4f/255};
    Vec3 linear;
    for(std::size_t c=0;c<3;++c)linear[c]=std::pow((encoded[c]+.055f)/1.055f,2.4f);
    std::array<double,3> quantizedSum{};
    std::array<int,3> minimum{255,255,255},maximum{};
    constexpr int side=128;
    for(int y=0;y<side;++y)for(int x=0;x<side;++x) {
        const float px=float(x)+.5f,py=float(y)+.5f;
        check(debanding::apply(linear,encoded,px,py,false)==encoded,"Disabled debanding changed the existing encoding path.");
        const auto dithered=debanding::apply(linear,encoded,px,py,true);
        check(dithered==debanding::apply(linear,encoded,px,py,true),"Debanding is not stable at a fixed screen pixel.");
        for(std::size_t c=0;c<3;++c) {
            check(std::abs(dithered[c]-encoded[c])<=.5001f/255,"Debanding exceeded half an 8-bit code step.");
            const auto quantized=static_cast<int>(std::lround(dithered[c]*255));
            minimum[c]=std::min(minimum[c],quantized);maximum[c]=std::max(maximum[c],quantized);
            quantizedSum[c]+=quantized;
        }
        for(float endpoint:{0.f,1.f}) {
            const Vec3 solid{endpoint,endpoint,endpoint};
            const float transfer=endpoint<=.0031308f?endpoint*12.92f:1.055f*std::pow(endpoint,1.f/2.4f)-.055f;
            const Vec3 encodedSolid{transfer,transfer,transfer};
            check(debanding::apply(solid,encodedSolid,px,py,true)==solid,"Debanding altered exact display black or white after sRGB rounding.");
            check(debanding::apply(solid,encodedSolid,px,py,false)==encodedSolid,"Endpoint protection changed disabled encoding.");
        }
    }
    // A flat, fractional code value must distribute between its two adjacent
    // output codes while preserving its spatial mean, rather than form a band.
    for(std::size_t c=0;c<3;++c) {
        check(maximum[c]-minimum[c]==1,"Debanding did not distribute neighboring output codes.");
        check(std::abs(quantizedSum[c]/(side*side)-encoded[c]*255)<.025,
            "Debanding shifted the spatial average of an 8-bit gradient.");
    }
}
}
int main() {
    try {references();continuityAndEncoding();displayDebanding();std::cout<<"Tone mapping and debanding checks passed.\n";return 0;}
    catch (const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
