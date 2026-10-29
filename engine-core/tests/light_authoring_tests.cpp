#include "scene/LightAuthoring.h"
#include "scene/Scene.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace crimson;
void require(bool value,const std::string& message) { if(!value)throw std::runtime_error(message); }
bool near(float a,float b,float tolerance=.00001f) { return std::abs(a-b)<=tolerance; }
bool near(const Vec3& a,const Vec3& b,float tolerance=.00001f) {
    return near(a[0],b[0],tolerance)&&near(a[1],b[1],tolerance)&&near(a[2],b[2],tolerance);
}
double linear(double x) { return x<=.04045?x/12.92:std::pow((x+.055)/1.055,2.4); }

void temperatureAndValidation() {
    require(validLightColorMode(LightColorMode::Color)&&validLightColorMode(LightColorMode::Temperature)&&
        !validLightColorMode(static_cast<LightColorMode>(42)),"Light color mode validation failed.");
    for(float invalid:{999.f,15001.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})
        require(!validLightTemperature(invalid),"Invalid Kelvin accepted.");
    for(float kelvin:{1000.f,1500.f,1800.f,2800.f,4000.f,6500.f,10000.f,15000.f}) {
        require(validLightTemperature(kelvin),"Valid Kelvin rejected.");
        const auto color=temperatureColorSrgb(kelvin);
        for(float channel:color)require(std::isfinite(channel)&&channel>=0&&channel<=1,"Temperature produced invalid sRGB.");
        require(near(*std::max_element(color.begin(),color.end()),1),"Temperature color is not maximum-channel normalized.");
    }
    const auto warm=temperatureColorSrgb(1800),cool=temperatureColorSrgb(15000),neutral=temperatureColorSrgb(6500);
    require(warm[0]>warm[1]&&warm[1]>warm[2]&&cool[2]>cool[1]&&cool[1]>cool[0],"Warm/cool Kelvin ordering changed.");
    require(neutral[0]>.85f&&neutral[1]>.85f&&neutral[2]>.85f,"Daylight reference ceased being nearly white.");
    const Vec3 manual{.14f,.68f,.37f};
    require(resolveLightColorSrgb(LightColorMode::Color,manual,1800)==manual,"Color mode changed manual RGB.");
    require(resolveLightColorSrgb(LightColorMode::Temperature,manual,1800)==warm,"Temperature mode used manual RGB.");
    require(temperatureColorSrgb(0)==temperatureColorSrgb(1000)&&temperatureColorSrgb(999999)==cool,"Presentation temperature clamp failed.");

    LightFlickerSettings settings;
    require(validLightFlicker(settings),"Default flicker settings are invalid.");
    const std::array members{&LightFlickerSettings::speed,&LightFlickerSettings::duration,&LightFlickerSettings::startDelay,
        &LightFlickerSettings::timeOffset,&LightFlickerSettings::timingRandomness,&LightFlickerSettings::brightnessRandomness,
        &LightFlickerSettings::slowAmount,&LightFlickerSettings::slowRate,&LightFlickerSettings::flutterAmount,&LightFlickerSettings::flutterRate,
        &LightFlickerSettings::smoothness,&LightFlickerSettings::brightnessBias,&LightFlickerSettings::stableMinimum,&LightFlickerSettings::stableMaximum,
        &LightFlickerSettings::failureMinimum,&LightFlickerSettings::failureMaximum,&LightFlickerSettings::failureLevel,
        &LightFlickerSettings::flashMinimum,&LightFlickerSettings::flashMaximum,&LightFlickerSettings::gapMinimum,&LightFlickerSettings::gapMaximum,
        &LightFlickerSettings::restMinimum,&LightFlickerSettings::restMaximum,&LightFlickerSettings::eventFrequency,
        &LightFlickerSettings::riseTime,&LightFlickerSettings::fallTime,&LightFlickerSettings::recoveryTime,&LightFlickerSettings::afterglow,
        &LightFlickerSettings::period,&LightFlickerSettings::pulseWidth,&LightFlickerSettings::secondaryStrength,&LightFlickerSettings::secondarySpacing};
    for(auto member:members) {
        auto invalid=settings;invalid.*member=std::numeric_limits<float>::quiet_NaN();
        require(!validLightFlicker(invalid),"A nonfinite flicker field was accepted.");
    }
    for(const auto pair:{std::pair{&LightFlickerSettings::stableMinimum,&LightFlickerSettings::stableMaximum},
        std::pair{&LightFlickerSettings::failureMinimum,&LightFlickerSettings::failureMaximum},
        std::pair{&LightFlickerSettings::flashMinimum,&LightFlickerSettings::flashMaximum},
        std::pair{&LightFlickerSettings::gapMinimum,&LightFlickerSettings::gapMaximum},
        std::pair{&LightFlickerSettings::restMinimum,&LightFlickerSettings::restMaximum}}) {
        auto invalid=settings;invalid.*pair.first=2;invalid.*pair.second=1;require(!validLightFlicker(invalid),"Inverted timing range was accepted.");
    }
    auto invalid=settings;invalid.behavior=LightFlickerBehavior::Count;require(!validLightFlicker(invalid),"Invalid behavior accepted.");
    invalid=settings;invalid.playback=static_cast<LightFlickerPlayback>(2);require(!validLightFlicker(invalid),"Invalid playback accepted.");
    invalid=settings;invalid.flashesMinimum=0;require(!validLightFlicker(invalid),"Zero flash count accepted.");
    invalid=settings;invalid.flashesMaximum=65;require(!validLightFlicker(invalid),"Unbounded flash count accepted.");
    invalid=settings;invalid.intensity.minimum=2;invalid.intensity.maximum=1;require(!validLightFlicker(invalid),"Inverted multiplier range accepted.");
    invalid=settings;invalid.radius.response=0;require(!validLightFlicker(invalid),"Zero response accepted.");
    invalid=settings;invalid.color.minimumTint[0]=-1;require(!validLightFlicker(invalid),"Invalid tint accepted.");
    invalid=settings;invalid.color.maximumTemperatureShift=14001;require(!validLightFlicker(invalid),"Invalid Kelvin shift accepted.");
    invalid=settings;invalid.color.amount=1.1f;require(!validLightFlicker(invalid),"Invalid color amount accepted.");
}

void fixtureCatalog() {
    const auto presets=lightFixturePresets();require(presets.size()==65,"Common fixture library lost entries.");
    std::set<std::string_view> ids;std::size_t points{},directions{};
    for(const auto& preset:presets) {
        require(!preset.id.empty()&&!preset.name.empty()&&!preset.category.empty()&&ids.insert(preset.id).second,"Fixture identity/label is invalid.");
        require(validLightTemperature(preset.temperatureKelvin)&&std::isfinite(preset.intensity)&&preset.intensity>=0,"Fixture values are invalid.");
        ScenePointLight point;point.color={.2f,.3f,.4f};point.temperatureKelvin=3333;point.enabled=false;point.castShadows=true;point.attenuation=-2;point.indirectEnergy=4;
        point.flicker.enabled=true;point.flicker.seed=77;point.flicker.timeOffset=-3;
        SceneDirectionalLight directional;directional.color={.4f,.3f,.2f};directional.temperatureKelvin=4444;directional.enabled=false;directional.castShadows=false;
        directional.shadowDistance=321;directional.shadowAngularDiameter=1.4f;directional.indirectEnergy=3;directional.flicker=point.flicker;
        SceneSpotLight spot;static_cast<ScenePointLight&>(spot)=point;spot.innerAngle=12;spot.outerAngle=67;spot.coneFalloff=2.5f;
        if(preset.directional) {
            ++directions;const auto before=point;require(!applyLightFixturePreset(point,preset.id)&&point==before,"Directional fixture changed a point light.");
            const auto beforeSpot=spot;require(!applyLightFixturePreset(spot,preset.id)&&spot==beforeSpot,"Directional fixture changed a spot light.");
            auto expected=directional;expected.colorMode=preset.colorMode;expected.intensity=preset.intensity;
            if(preset.colorMode==LightColorMode::Color)expected.color=preset.color;else expected.temperatureKelvin=preset.temperatureKelvin;
            require(applyLightFixturePreset(directional,preset.id)&&directional==expected,"Directional fixture changed unrelated or inactive properties.");
        } else {
            ++points;require(preset.radius>=.01f&&preset.radius<=100000,"Point fixture radius invalid.");
            const auto before=directional;require(!applyLightFixturePreset(directional,preset.id)&&directional==before,"Point fixture changed a directional light.");
            auto expected=point;expected.colorMode=preset.colorMode;expected.intensity=preset.intensity;expected.radius=preset.radius;
            if(preset.colorMode==LightColorMode::Color)expected.color=preset.color;else expected.temperatureKelvin=preset.temperatureKelvin;
            require(applyLightFixturePreset(point,preset.id)&&point==expected,"Point fixture changed unrelated or inactive properties.");
            auto expectedSpot=spot;static_cast<ScenePointLight&>(expectedSpot)=expected;
            require(applyLightFixturePreset(spot,preset.id)&&spot==expectedSpot,"Spot fixture did not share point calibration or changed cone/shadow/flicker settings.");
        }
    }
    require(points==55&&directions==10,"Fixture type coverage changed.");
    ScenePointLight point;const auto before=point;require(!applyLightFixturePreset(point,"missing")&&point==before,"Unknown fixture mutated a light.");
    applyLightFixturePreset(point,"candle");require(point.temperatureKelvin==1800&&point.intensity==1&&point.radius==2,"Candle calibration changed.");
    applyLightFixturePreset(point,"led_daylight");require(point.temperatureKelvin==6500&&point.intensity==35&&point.radius==16,"LED calibration changed.");
}

void flickerCatalogAndDeterminism() {
    const auto presets=lightFlickerPresets();require(presets.size()==24,"Flicker catalog lost a behavior.");
    std::set<std::string_view> ids;std::array<bool,24> behaviors{};std::vector<std::vector<float>> signatures;
    for(const auto& preset:presets) {
        require(ids.insert(preset.id).second&&!preset.name.empty()&&!preset.category.empty()&&validLightFlicker(preset.settings),"Invalid flicker preset.");
        require(lightFlickerBehaviorFromId(preset.id)==preset.settings.behavior&&lightFlickerBehaviorId(preset.settings.behavior)==preset.id,"Behavior serialization identity changed.");
        behaviors[static_cast<std::size_t>(preset.settings.behavior)]=true;
        LightFlickerSettings applied;applied.enabled=true;applied.seed=1234;applied.timeOffset=-2.5f;
        auto expected=preset.settings;expected.enabled=true;expected.seed=1234;expected.timeOffset=-2.5f;
        require(applyLightFlickerPreset(applied,preset.id)&&applied==expected,"Flicker preset failed to retain enable/seed/offset.");
        auto s=preset.settings;s.enabled=true;
        std::vector<float> values;float low=1,high=0;
        for(int sample=0;sample<5000;++sample) {
            const double time=sample*.017371;
            const float value=sampleLightFlicker(s,time);values.push_back(value);low=std::min(low,value);high=std::max(high,value);
            require(std::isfinite(value)&&value>=0&&value<=1,"Behavior produced an invalid sample: "+std::string(preset.id));
        }
        require(high-low>.01f,"Behavior is unexpectedly static: "+std::string(preset.id));
        for(int sample=4999;sample>=0;sample-=13)require(sampleLightFlicker(s,sample*.017371)==values[static_cast<std::size_t>(sample)],"Seeking or sampling order altered a flicker sequence.");
        for(const auto& previous:signatures)require(previous!=values,"Two flicker presets have identical output.");
        signatures.push_back(std::move(values));
        const auto original=s;s.seed=9;bool changed=false;
        for(int sample=0;sample<100;++sample)changed|=sampleLightFlicker(s,sample*.137)!=sampleLightFlicker(original,sample*.137);
        if(s.timingRandomness>0||s.brightnessRandomness>0)require(changed,"Randomized behavior ignores its seed.");
    }
    require(std::all_of(behaviors.begin(),behaviors.end(),[](bool found){return found;}),"Not every behavior has a preset.");
    require(!lightFlickerBehaviorFromId("missing")&&lightFlickerBehaviorId(LightFlickerBehavior::Count).empty(),"Unknown behavior identity accepted.");
    LightFlickerSettings s;const auto before=s;require(!applyLightFlickerPreset(s,"missing")&&s==before,"Unknown flicker preset mutated settings.");
}

void presentationChannels() {
    LightFlickerSettings s;s.enabled=true;s.slowAmount=s.flutterAmount=0;s.brightnessBias=0;
    s.intensity={true,2,4,false,1};s.radius={true,.5f,1.5f,true,2};
    const Vec3 color{.8f,.5f,.2f};
    auto value=evaluateLightPresentation(LightColorMode::Color,color,2800,10,20,s,1);
    require(near(value.intensity,30)&&near(value.radius,15)&&value.color==color,"Shared scalar response/inversion changed.");
    s.color.enabled=true;s.color.minimumTint={0,0,0};s.color.maximumTint={1,1,1};s.color.amount=1;
    value=evaluateLightPresentation(LightColorMode::Color,color,2800,10,20,s,1);
    for(std::size_t channel=0;channel<3;++channel)require(std::abs(linear(value.color[channel])-linear(color[channel])*.5)<.00001,"Tint interpolation did not use linear light.");
    s.color.minimumTemperatureShift=s.color.maximumTemperatureShift=500;
    value=evaluateLightPresentation(LightColorMode::Temperature,color,2800,10,20,s,1);
    require(near(value.color,temperatureColorSrgb(3300)),"Temperature flicker did not shift the base Kelvin.");
    s.color.minimumTemperatureShift=s.color.maximumTemperatureShift=-14000;
    require(evaluateLightPresentation(LightColorMode::Temperature,color,2800,10,20,s,1).color==temperatureColorSrgb(1000),"Kelvin flicker escaped the supported range.");
    auto base=evaluateLightPresentation(LightColorMode::Temperature,color,2800,10,20,s,1,false);
    require(base.color==temperatureColorSrgb(2800)&&base.intensity==10&&base.radius==20,"Edit/static presentation retained flicker.");
    s.enabled=false;require(evaluateLightPresentation(LightColorMode::Color,color,2800,10,20,s,1).color==color,"Disabled flicker changed base RGB.");
    s.enabled=true;s.startDelay=2;base=evaluateLightPresentation(LightColorMode::Color,color,2800,10,20,s,1);
    require(base.color==color&&base.intensity==10&&base.radius==20,"Start delay failed to retain the exact authored base.");
    s.startDelay=0;s.radius.minimum=s.radius.maximum=0;
    require(evaluateLightPresentation(LightColorMode::Color,color,2800,10,20,s,1).radius==.01f&&
        evaluateLightPresentation(LightColorMode::Color,color,2800,10,0,s,1).radius==0,"Radius presentation clamp affected directional lights.");
}

void playbackAndTimingCorners() {
    LightFlickerSettings s;s.enabled=true;s.duration=3.13f;s.behavior=LightFlickerBehavior::Candle;s.speed=1.7f;s.timeOffset=-.3f;
    const double time=.67;
    require(near(sampleLightFlicker(s,time),sampleLightFlicker(s,time+s.duration/s.speed)),"Loop playback did not wrap its duration.");
    auto reference=s;reference.speed=1;reference.timeOffset=0;reference.startDelay=0;
    const double phase=std::fmod(time*s.speed+s.timeOffset+s.duration,s.duration);
    require(near(sampleLightFlicker(s,time),sampleLightFlicker(reference,phase)),"Speed and phase offset did not share the scene clock.");
    s.playback=LightFlickerPlayback::Once;
    const auto final=sampleLightFlicker(s,(s.duration-s.timeOffset)/s.speed);
    require(sampleLightFlicker(s,100)==final&&sampleLightFlicker(s,999999)==final,"Once playback failed to hold its final value.");
    for(const auto& preset:lightFlickerPresets()) {
        auto zero=preset.settings;zero.enabled=true;zero.duration=.01f;zero.speed=100;zero.timeOffset=-1000000;
        zero.stableMinimum=zero.stableMaximum=zero.failureMinimum=zero.failureMaximum=0;
        zero.flashMinimum=zero.flashMaximum=zero.gapMinimum=zero.gapMaximum=zero.restMinimum=zero.restMaximum=0;
        zero.riseTime=zero.fallTime=zero.recoveryTime=0;zero.period=.001f;zero.pulseWidth=0;zero.flashesMaximum=64;zero.eventFrequency=1000;
        require(validLightFlicker(zero),"Legal zero-duration edges were rejected.");
        for(double seconds:{0.0,.001,1.0,999999.0})require(std::isfinite(sampleLightFlicker(zero,seconds)),"Zero timing caused invalid evaluation.");
        zero.pulseWidth=1;zero.eventFrequency=0;
        for(double seconds:{0.0,.001,1.0})require(std::isfinite(sampleLightFlicker(zero,seconds)),"Full duty/zero event rate caused invalid evaluation.");
    }
    s={};s.enabled=true;s.behavior=LightFlickerBehavior::HardBlink;s.timingRandomness=s.brightnessRandomness=0;
    s.period=1;s.pulseWidth=.5f;s.riseTime=s.fallTime=0;
    require(sampleLightFlicker(s,.1)==1&&sampleLightFlicker(s,.6)==0,"Hard zero-time blink transitions were softened.");
    s.behavior=LightFlickerBehavior::DoubleFlash;s.period=1;s.pulseWidth=.05f;s.secondarySpacing=.3f;s.secondaryStrength=.7f;
    require(sampleLightFlicker(s,.02)==1&&near(sampleLightFlicker(s,.32),.7f)&&sampleLightFlicker(s,.2)==0,"Double flash lost its separate second pulse.");
    s.behavior=LightFlickerBehavior::Lightning;s.eventFrequency=0;
    for(double seconds:{0.0,1.0,100.0})require(sampleLightFlicker(s,seconds)==0,"Zero event frequency still emitted flashes.");
    s.behavior=LightFlickerBehavior::LooseConnection;s.stableMinimum=s.stableMaximum=1;s.failureMinimum=s.failureMaximum=.5f;s.failureLevel=0;
    s.flashesMinimum=s.flashesMaximum=1;s.flashMinimum=s.flashMaximum=.1f;s.gapMinimum=s.gapMaximum=.1f;s.recoveryTime=0;
    require(sampleLightFlicker(s,.5)==1,"Electrical stable interval changed.");
    require(sampleLightFlicker(s,1.1)==0,"Electrical failure interval changed.");
    require(sampleLightFlicker(s,1.55)==1,"Loose connection reduced a flash despite zero brightness randomness.");
    require(sampleLightFlicker(s,1.65)==0,"Electrical inter-flash gap changed.");
    require(sampleLightFlicker(s,1.75)==1,"Zero-time recovery failed to restore stable illumination.");
    auto otherSeed=s;otherSeed.seed=12345;
    for(double seconds:{.5,1.1,1.55,1.65,1.75,3.25})
        require(sampleLightFlicker(s,seconds)==sampleLightFlicker(otherSeed,seconds),"Zero randomness retained hidden seed-dependent connection brightness.");
    applyLightFlickerPreset(s,"fluorescent_startup");s.enabled=true;s.timeOffset=0;s.seed=1;
    require(sampleLightFlicker(s,8)>.6f&&sampleLightFlicker(s,80)==sampleLightFlicker(s,8),"One-shot fluorescent startup restarted or held a failed second ignition.");
    for(const auto& preset:lightFlickerPresets()) {
        auto clock=preset.settings;clock.enabled=true;clock.speed=100;
        require(std::isfinite(sampleLightFlicker(clock,std::numeric_limits<double>::max())),"A large finite clock overflowed the pure evaluator.");
    }
    applyLightFlickerPreset(s,"candle");s.enabled=true;s.timeOffset=0;s.seed=1;s.startDelay=0;
    require(std::abs(sampleLightFlicker(s,1e-7)-sampleLightFlicker(s,s.duration-1e-7))<.0001f,"Smooth fire loop has a discontinuity at its seam.");
    s.slowRate=s.flutterRate=0;
    require(sampleLightFlicker(s,0)==sampleLightFlicker(s,1)&&sampleLightFlicker(s,0)==sampleLightFlicker(s,20),"Zero-Hz fire noise still moved through timing warp.");
    applyLightFlickerPreset(s,"random_flashes");s.enabled=true;s.timeOffset=0;s.duration=30;s.timingRandomness=s.brightnessRandomness=0;
    s.flashesMinimum=s.flashesMaximum=1;s.flashMinimum=s.flashMaximum=.2f;s.gapMinimum=s.gapMaximum=.1f;
    s.riseTime=.1f;s.fallTime=.2f;s.restMinimum=s.restMaximum=1;s.afterglow=.5f;s.eventFrequency=2;
    const float beforeEnd=sampleLightFlicker(s,.5-1e-6),afterEnd=sampleLightFlicker(s,.5+1e-6);
    require(std::abs(beforeEnd-afterEnd)<.0001f,"Afterglow rebrightened at the end of a flash.");
    require(sampleLightFlicker(s,.7)<sampleLightFlicker(s,.6)&&sampleLightFlicker(s,.9)<sampleLightFlicker(s,.7),"Afterglow restarted during the rest interval.");
    s.behavior=LightFlickerBehavior::HardBlink;s.period=.001f;s.pulseWidth=.5f;s.riseTime=s.fallTime=1;
    require(sampleLightFlicker(s,.0005-1e-9)<.0001f,"Overlapping pulse ramps were truncated at the authored width.");
}
} // namespace
int main() {
    try { temperatureAndValidation();fixtureCatalog();flickerCatalogAndDeterminism();presentationChannels();playbackAndTimingCorners();
        std::cout<<"Light authoring checks passed.\n";return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
