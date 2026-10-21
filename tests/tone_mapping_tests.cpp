#include "crimson-renderer/ToneMapping.h"
#include "crimson-renderer/Debanding.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
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
    // Linear leaves HDR available for the final display clamp; default Reinhard
    // keeps the existing response until its optional white is authored.
    close(toneMap({.18f,1,4},ToneMapping::Linear),{.18f,1,4});
    close(toneMap({.18f,1,4},ToneMapping::Reinhard),{.18f/1.18f,.5f,.8f});
}
void authoredCurveParameters() {
    const ToneMappingParameters defaults;
    check(validToneMappingParameters(defaults),"Default tone curve parameters are invalid.");
    constexpr std::array modes{ToneMapping::Reinhard,ToneMapping::Linear,ToneMapping::ACES,ToneMapping::AgX};
    for(const auto mode:modes)for(const auto color:std::array<Vec3,4>{{{0,0,0},{.01f,.18f,1},{4,.5f,.1f},{16,1,0}}})
        check(toneMap(color,mode)==toneMap(color,mode,defaults),"Default parameter overload changed the existing curve.");

    ToneMappingParameters parameters;parameters.reinhardWhite=4;
    close(toneMap({.18f,1,4},ToneMapping::Reinhard,parameters),{.154258475f,.53125f,1});
    parameters.reinhardWhite=.00001f;
    const auto guarded=toneMap({.001f,.01f,1},ToneMapping::Reinhard,parameters);
    parameters.reinhardWhite=.01f;
    check(guarded==toneMap({.001f,.01f,1},ToneMapping::Reinhard,parameters),
        "A small positive Reinhard white did not retain its finite computational guard.");

    // Independent double-precision values from Godot's environment storage
    // coefficient equations and SDR tonemap shader, beyond default settings.
    parameters={0,4,2,2};
    close(toneMap({4,.5f,.1f},ToneMapping::ACES,parameters),{1.125626735f,.724807498f,.343871177f});
    close(toneMap({4,.5f,.1f},ToneMapping::AgX,parameters),{1.278548731f,.660893105f,.400569304f});
    parameters={0,64,64,4};
    close(toneMap({16,1,0},ToneMapping::ACES,parameters),{1.052394470f,.885222156f,.609064403f});
    close(toneMap({.01f,.02f,.04f},ToneMapping::AgX,parameters),{-.000034301f,.000039326f,.000369282f},1e-7f);
    parameters={4,4,8,1.5f};
    close(toneMap({4,.5f,.1f},ToneMapping::AgX,parameters),{1.035985957f,.567326145f,.356928455f});
    const auto packed=toneMappingUniforms(parameters);
    check(std::abs(packed.curve[0]-.0625f)<1e-7f&&std::abs(packed.curve[1]-.957039832f)<1e-7f,
        "Packed white coefficients differ from the independent SDR reference.");
    check(packed.curve[2]==1.5f&&std::abs(packed.curve[3]-.347896536f)<1e-7f&&
        std::abs(packed.shoulder[0]-1.23f)<1e-6f&&std::abs(packed.shoulder[1]-91.7286f)<3e-5f&&
        packed.shoulder[2]==0&&packed.shoulder[3]==0,"Packed AgX coefficients differ from Godot's curve.");
    close(toneMap({.18f,1,4},ToneMapping::Linear,parameters),{.18f,1,4});
}
void parameterValidationAndContinuity() {
    using Field=float ToneMappingParameters::*;
    constexpr std::array<Field,4> fields{&ToneMappingParameters::reinhardWhite,&ToneMappingParameters::acesWhite,
        &ToneMappingParameters::agxWhite,&ToneMappingParameters::agxContrast};
    constexpr std::array<float,4> lower{0,1,2,1},upper{64,64,64,4};
    const auto defaultUniforms=toneMappingUniforms({});
    for(std::size_t field=0;field<fields.size();++field) {
        for(const float endpoint:{lower[field],upper[field]}) {
            ToneMappingParameters parameters;parameters.*fields[field]=endpoint;
            check(validToneMappingParameters(parameters),"A valid authored tone curve boundary was rejected.");
        }
        for(const float invalid:{lower[field]-.01f,upper[field]+.01f,std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()}) {
            ToneMappingParameters parameters;parameters.*fields[field]=invalid;
            check(!validToneMappingParameters(parameters),"Invalid or nonfinite tone curve parameters were accepted.");
            const auto uniforms=toneMappingUniforms(parameters);
            check(uniforms.curve==defaultUniforms.curve&&uniforms.shoulder==defaultUniforms.shoulder,
                "Invalid transient tone settings produced unsafe shader coefficients.");
        }
    }
    for(const float white:{2.f,8.f,16.29f,64.f})for(const float contrast:{1.f,1.25f,2.f,4.f}) {
        ToneMappingParameters parameters;parameters.agxWhite=white;parameters.agxContrast=contrast;
        close(toneMap({white,white,white},ToneMapping::AgX,parameters),{1,1,1});
        close(toneMap({.18f,.18f,.18f},ToneMapping::AgX,parameters),{.18f,.18f,.18f});
        const auto low=toneMap({.179999f,.179999f,.179999f},ToneMapping::AgX,parameters);
        const auto high=toneMap({.180001f,.180001f,.180001f},ToneMapping::AgX,parameters);
        check(high[0]>=low[0]&&high[0]-low[0]<1e-5f,"Authored AgX contrast broke the toe/shoulder join.");
        float previous=-1;
        for(int step=0;step<=256;++step) {
            const float input=std::pow(10.f,-6.f+float(step)/32.f);
            const auto mapped=toneMap({input,input,input},ToneMapping::AgX,parameters);
            for(const float value:mapped)check(std::isfinite(value),"Authored AgX controls produced nonfinite light.");
            check(mapped[0]>=previous-2e-6f,"Authored AgX neutral response is not monotonic.");
            previous=mapped[0];
        }
    }
    for(const float white:{1.f,2.f,8.f,64.f}) {
        ToneMappingParameters parameters;parameters.acesWhite=white;parameters.reinhardWhite=white;
        close(toneMap({white,white,white},ToneMapping::ACES,parameters),{1,1,.99999f},3e-5f);
        close(toneMap({white,white,white},ToneMapping::Reinhard,parameters),{1,1,1});
    }
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
void extremeHdrReferences() {
    // A real HDR sun and the supported FP32 source envelope, before low/high
    // authored exposure. The CPU retains the original equations in double;
    // GPU presentation readbacks use this independent reference as well.
    constexpr std::array<Vec3,5> sources{{{129536,129024,122368},{1e10f,1e9f,1e8f},
        {1e20f,1e19f,1e18f},{1e30f,1e29f,1e28f},{1e30f,0,0}}};
    constexpr std::array modes{ToneMapping::Reinhard,ToneMapping::Linear,ToneMapping::ACES,ToneMapping::AgX};
    for(const float acesWhite:{1.f,64.f})for(const float agxWhite:{2.f,64.f})for(const float contrast:{1.f,4.f}) {
        ToneMappingParameters parameters;parameters.acesWhite=acesWhite;
        parameters.agxWhite=agxWhite;parameters.agxContrast=contrast;
        for(const auto source:sources)for(const float exposure:{1e-12f,1.f,256.f}) {
            Vec3 exposed=source;for(auto& value:exposed)value*=exposure;
            for(const auto mode:modes) {
                const auto mapped=toneMap(exposed,mode,parameters);
                for(const float value:mapped)
                    check(std::isfinite(value),"Finite HDR radiance overflowed a presentation curve.");
                if(mode==ToneMapping::Linear)
                    check(mapped==exposed,"Extreme HDR protection clipped the Linear presentation input.");
            }
        }
        // For any nonzero primary at this magnitude, the positive inset rows
        // all reach the scalar shoulder asymptote. Neither curve may collapse
        // such an HDR highlight to black after an intermediate inf/inf.
        const float acesLimit=static_cast<float>((1./.983729)/toneMappingUniforms(parameters).curve[1]);
        for(const auto input:std::array<Vec3,2>{{{1e30f,1e30f,1e30f},{1e30f,0,0}}}) {
            close(toneMap(input,ToneMapping::ACES,parameters),
                {acesLimit,acesLimit,.99999f*acesLimit},3e-6f);
            close(toneMap(input,ToneMapping::AgX,parameters),{1,1,1},2e-6f);
        }
    }
    // Extended Reinhard legitimately exceeds display white. Preserve that
    // original response instead of clipping HDR to hide its quadratic overflow.
    for(const float white:{.01f,1.f,64.f}) {
        ToneMappingParameters parameters;parameters.reinhardWhite=white;
        const double inverseWhiteSquared=toneMappingUniforms(parameters).curve[0];
        const Vec3 source{1e30f,1e29f,1e28f};
        const auto mapped=toneMap(source,ToneMapping::Reinhard,parameters);
        for(std::size_t channel=0;channel<source.size();++channel) {
            const double value=source[channel];
            const double expected=value*(1.+value*inverseWhiteSquared)/(1.+value);
            check(std::isfinite(mapped[channel])&&
                std::abs(double(mapped[channel])-expected)<=expected*1e-6,
                "Extended Reinhard changed or overflowed its finite HDR reference response.");
        }
    }
    close(toneMap({1e30f,1e29f,1e28f},ToneMapping::Reinhard),{1,1,1},1e-7f);
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
    try {references();authoredCurveParameters();parameterValidationAndContinuity();continuityAndEncoding();extremeHdrReferences();displayDebanding();std::cout<<"Tone mapping and debanding checks passed.\n";return 0;}
    catch (const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
