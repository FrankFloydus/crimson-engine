#include "LightAuthoring.h"
#include "Scene.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace crimson {
namespace {
constexpr std::array<std::string_view,24> behaviorIds{
    "candle","oil_lamp","torch","fireplace","campfire","roaring_fire",
    "unstable_incandescent","failing_fluorescent","fluorescent_startup","faulty_neon","loose_connection","power_brownout",
    "soft_breathing","slow_pulse","hard_blink","strobe","double_flash","heartbeat",
    "lightning","electrical_arc","welding","tv_monitor","reactor_instability","random_flashes"};
bool bounded(float value,float low,float high) { return std::isfinite(value)&&value>=low&&value<=high; }
bool interval(float low,float high,float minimum,float maximum) { return bounded(low,minimum,maximum)&&bounded(high,low,maximum); }
bool tintValid(const Vec3& color) { return std::all_of(color.begin(),color.end(),[](float x){return bounded(x,0,1);}); }
bool channelValid(const LightFlickerChannel& c) { return interval(c.minimum,c.maximum,0,10)&&bounded(c.response,.05f,10); }
float saturate(double x) { return static_cast<float>(std::clamp(x,0.0,1.0)); }
double smooth(double x) { x=std::clamp(x,0.0,1.0);return x*x*(3-2*x); }
double smoother(double x) { x=std::clamp(x,0.0,1.0);return x*x*x*(x*(x*6-15)+10); }
double mix(double a,double b,double t) { return a+(b-a)*t; }
double linear(double x) { return x<=.04045?x/12.92:std::pow((x+.055)/1.055,2.4); }
float srgb(double x) { x=std::max(0.0,x);return static_cast<float>(x<=.0031308?12.92*x:1.055*std::pow(x,1/2.4)-.055); }
std::uint64_t hash(std::uint64_t value) {
    value+=0x9e3779b97f4a7c15ull;value=(value^(value>>30))*0xbf58476d1ce4e5b9ull;
    value=(value^(value>>27))*0x94d049bb133111ebull;return value^(value>>31);
}
double random(const LightFlickerSettings& s,std::int64_t index,std::uint64_t lane) {
    const auto bits=hash(static_cast<std::uint64_t>(index)^hash(static_cast<std::uint64_t>(s.seed)+(lane*0x9e3779b97f4a7c15ull)));
    return static_cast<double>(bits>>11)*0x1.0p-53;
}
double range(const LightFlickerSettings& s,std::int64_t index,std::uint64_t lane,double low,double high) {
    return mix(low,high,.5+(random(s,index,lane)-.5)*s.timingRandomness);
}
double noise(const LightFlickerSettings& s,double time,std::uint64_t lane,double softness) {
    const auto cell=static_cast<std::int64_t>(std::floor(time));const auto fraction=time-std::floor(time);
    return mix(random(s,cell,lane),random(s,cell+1,lane),mix(fraction,smoother(fraction),softness));
}
double peak(const LightFlickerSettings& s,std::int64_t index,std::uint64_t lane) {
    return 1-s.brightnessRandomness*.75*random(s,index,lane);
}
double pulse(double t,double hold,double rise,double fall,bool soft=false) {
    if(t<0)return 0;
    double value{};
    if(rise>0&&t<rise)value=t/rise;
    else if(t<rise+hold)value=1;
    else if(fall>0&&t<rise+hold+fall)value=1-(t-rise-hold)/fall;
    return soft?smooth(value):value;
}
double localTime(const LightFlickerSettings& s,double time) {
    const double elapsed=time-s.startDelay;
    if(s.playback==LightFlickerPlayback::Once) {
        if(elapsed>(s.duration+std::abs(s.timeOffset))/s.speed)return s.duration;
        return std::clamp(elapsed*s.speed+s.timeOffset,0.0,static_cast<double>(s.duration));
    }
    // Reduce before multiplication, including arbitrarily large finite clocks.
    double local=std::fmod(elapsed,s.duration/s.speed)*s.speed+s.timeOffset;
    local=std::fmod(local,s.duration);return local<0?local+s.duration:local;
}
double continuous(const LightFlickerSettings& s,double time) {
    const auto sample=[&](double rate,std::uint64_t lane,double softness,double warp=0) {
        if(rate==0)return noise(s,0,lane,softness);
        const double first=noise(s,time*rate+warp,lane,softness);
        if(s.playback==LightFlickerPlayback::Once)return first;
        return mix(first,noise(s,(time-s.duration)*rate+warp,lane,softness),smoother(time/s.duration));
    };
    const double warp=s.timingRandomness*.45*(sample(.37,101,1)-.5);
    const auto slow=sample(s.slowRate,102,s.smoothness,warp);
    const auto flutter=sample(s.flutterRate,103,s.smoothness,warp);
    const auto variation=mix(1.0,.45+.55*sample(s.slowRate==0&&s.flutterRate==0?0:.73,104,1),s.brightnessRandomness);
    return .5+s.brightnessBias*.5+variation*(s.slowAmount*(slow-.5)+s.flutterAmount*(flutter-.5));
}
double electrical(const LightFlickerSettings& s,double time) {
    // A bounded immutable episode table makes seeking independent of previous
    // frames while retaining actual authored interval bounds (no unused tails).
    struct Cycle { double stable,failure,hold,gap,level,length;std::uint32_t count; };
    std::array<Cycle,32> cycles{};std::array<double,33> prefix{};
    for(std::size_t i=0;i<cycles.size();++i) {
        auto& c=cycles[i];const auto index=static_cast<std::int64_t>(i);
        c.stable=range(s,index,200,s.stableMinimum,s.stableMaximum);c.failure=range(s,index,201,s.failureMinimum,s.failureMaximum);
        c.hold=range(s,index,210,s.flashMinimum,s.flashMaximum);c.gap=range(s,index,211,s.gapMinimum,s.gapMaximum);c.level=peak(s,index,202);
        c.count=s.flashesMinimum+static_cast<std::uint32_t>(std::floor(range(s,index,203,0,static_cast<double>(s.flashesMaximum-s.flashesMinimum)+.999999)));
        c.length=std::max(.001,c.stable+c.failure+s.fallTime+c.count*(c.hold+c.gap+s.fallTime)+s.recoveryTime);prefix[i+1]=prefix[i]+c.length;
    }
    const bool startup=s.behavior==LightFlickerBehavior::FluorescentStartup;
    const bool singleStartup=startup&&s.playback==LightFlickerPlayback::Once;
    const double episode=singleStartup?time:std::fmod(time,prefix.back());
    const auto cell=singleStartup?std::size_t{}:static_cast<std::size_t>(std::upper_bound(prefix.begin(),prefix.end(),episode)-prefix.begin()-1);
    const auto& cycle=cycles[cell];const auto index=static_cast<std::int64_t>(cell);
    double position=episode-prefix[cell];const double stable=cycle.stable,failure=cycle.failure,stableLevel=cycle.level;
    if(singleStartup&&position>=cycle.length)return stableLevel;
    if(!startup){if(position<stable)return stableLevel;position-=stable;}
    if(position<s.fallTime+failure) {
        const double blend=s.fallTime>0?saturate(position/s.fallTime):1;
        return mix(startup?s.failureLevel:stableLevel,s.failureLevel,
            s.behavior==LightFlickerBehavior::PowerBrownout?smooth(blend):blend);
    }
    position-=s.fallTime+failure;
    for(std::uint32_t flash=0;flash<cycle.count;++flash) {
        const auto lane=210+flash*4;
        const double hold=cycle.hold,gap=cycle.gap;
        const double length=hold+s.fallTime;
        if(position<length) {
            double envelope=pulse(position,hold,0,s.fallTime,s.behavior==LightFlickerBehavior::UnstableIncandescent||s.behavior==LightFlickerBehavior::PowerBrownout);
            if(s.behavior==LightFlickerBehavior::FaultyNeon)envelope*=.8+.2*std::abs(std::sin(position*180));
            if(s.behavior==LightFlickerBehavior::LooseConnection)
                envelope*=mix(1.0,mix(.45,1.0,random(s,index,lane+2)),s.brightnessRandomness);
            return mix(s.failureLevel,peak(s,index,lane+3),envelope);
        }
        position-=length;
        if(position<gap)return s.failureLevel;
        position-=gap;
    }
    if(s.recoveryTime>0&&position<s.recoveryTime)return mix(s.failureLevel,stableLevel,smooth(position/s.recoveryTime));
    return stableLevel;
}
double rhythmic(const LightFlickerSettings& s,double time) {
    const auto index=static_cast<std::int64_t>(std::floor(time/s.period));
    const double phase=time-index*s.period;
    const double width=s.period*s.pulseWidth;
    const double shift=s.timingRandomness*random(s,index,300)*std::max(0.0,static_cast<double>(s.period)-width)*.5;
    const double p=phase-shift;
    auto wave=[&](double at,double scale) {
        if(width<=0||at<0||at>=width)return 0.0;
        const double ramp=s.riseTime+s.fallTime;
        const double scaleRamps=ramp>width?width/ramp:1;
        const double rise=s.riseTime*scaleRamps,fall=s.fallTime*scaleRamps;
        const double envelope=pulse(at,std::max(0.0,width-rise-fall),rise,fall,false);
        switch(s.behavior) {
        case LightFlickerBehavior::SoftBreathing:return envelope*std::pow(std::sin(std::numbers::pi*at/width),2)*scale;
        case LightFlickerBehavior::SlowPulse:return envelope*std::sin(std::numbers::pi*at/width)*scale;
        case LightFlickerBehavior::Heartbeat:return envelope*std::pow(std::sin(std::numbers::pi*at/width),3)*scale;
        default:return envelope*scale;
        }
    };
    double result=wave(p,1);
    if(s.behavior==LightFlickerBehavior::DoubleFlash||s.behavior==LightFlickerBehavior::Heartbeat)
        result=std::max(result,wave(p-s.secondarySpacing*s.period,s.secondaryStrength));
    return result*peak(s,index,301);
}
double irregular(const LightFlickerSettings& s,double time) {
    const double baseline=s.behavior==LightFlickerBehavior::TvMonitor?.2:s.behavior==LightFlickerBehavior::ReactorInstability?.3:0;
    if(s.eventFrequency<=0)return baseline;
    struct Cycle { double hold,gap,peak,packet,length;std::uint32_t count; };
    std::array<Cycle,32> cycles{};std::array<double,33> prefix{};
    for(std::size_t i=0;i<cycles.size();++i) {
        auto& c=cycles[i];const auto index=static_cast<std::int64_t>(i);
        c.count=s.flashesMinimum+static_cast<std::uint32_t>(std::floor(range(s,index,401,0,static_cast<double>(s.flashesMaximum-s.flashesMinimum)+.999999)));
        c.hold=range(s,index,410,s.flashMinimum,s.flashMaximum);c.gap=range(s,index,411,s.gapMinimum,s.gapMaximum);c.peak=peak(s,index,412);
        if(s.behavior==LightFlickerBehavior::TvMonitor)c.peak*=mix(.35,1.0,random(s,index,413));
        c.packet=c.count*(s.riseTime+c.hold+s.fallTime)+(c.count-1)*c.gap;
        const double desired=std::clamp(1.0/s.eventFrequency-c.packet,static_cast<double>(s.restMinimum),static_cast<double>(s.restMaximum));
        const double rest=std::clamp(desired+(random(s,index,400)-.5)*s.timingRandomness*(s.restMaximum-s.restMinimum),static_cast<double>(s.restMinimum),static_cast<double>(s.restMaximum));
        c.length=std::max(.001,c.packet+rest);prefix[i+1]=prefix[i]+c.length;
    }
    const double episode=std::fmod(time,prefix.back());
    const auto cell=static_cast<std::size_t>(std::upper_bound(prefix.begin(),prefix.end(),episode)-prefix.begin()-1);
    const auto& cycle=cycles[cell];const double position=episode-prefix[cell];
    const double decay=std::max(.001,static_cast<double>(s.fallTime));
    const auto impulse=[&](double age,double hold) {
        if(age<0)return 0.0;
        if(s.riseTime>0&&age<s.riseTime)return age/s.riseTime;
        age-=s.riseTime;
        if(age<hold)return 1.0;
        age-=hold;
        if(s.fallTime>0&&age<s.fallTime)return mix(1.0,s.afterglow,age/s.fallTime);
        age-=s.fallTime;
        return s.afterglow*std::exp(-std::max(0.0,age)/decay);
    };
    double output=0;
    const double stride=s.riseTime+cycle.hold+s.fallTime+cycle.gap;
    if(cycle.packet>0&&stride>0) {
        const auto flash=std::min(cycle.count-1,static_cast<std::uint32_t>(std::floor(position/stride)));
        double envelope=impulse(position-flash*stride,cycle.hold);
        if(flash>0)envelope=std::max(envelope,impulse(position-(flash-1)*stride,cycle.hold));
        if(s.behavior==LightFlickerBehavior::TvMonitor||s.behavior==LightFlickerBehavior::ReactorInstability)envelope=smooth(envelope);
        output=cycle.peak*envelope;
    }
    // Carry decays from previous packets through gaps/rest without restarting
    // their clocks. One complete periodic table bounds all relevant tails.
    if(s.afterglow>0)for(std::size_t previous=0;previous<cycles.size();++previous) {
        if(previous==cell||cycles[previous].packet<=0)continue;
        double age=episode-prefix[previous]-cycles[previous].packet;if(previous>cell)age+=prefix.back();
        if(age>=0) {
            double tail=s.afterglow*std::exp(-age/decay);
            if(s.behavior==LightFlickerBehavior::TvMonitor||s.behavior==LightFlickerBehavior::ReactorInstability)tail=smooth(tail);
            output=std::max(output,cycles[previous].peak*tail);
        }
    }
    return baseline+(1-baseline)*output;
}
float channelSample(float u,bool invert,float response) { return std::pow(invert?1-u:u,response); }
float product(float base,double multiplier) {
    return static_cast<float>(std::clamp(static_cast<double>(base)*multiplier,0.0,static_cast<double>(std::numeric_limits<float>::max())));
}
LightFixturePreset temperature(std::string_view id,std::string_view category,std::string_view name,float kelvin,float intensity,float radius=0,bool directional=false) {
    return {id,category,name,directional,LightColorMode::Temperature,{1,1,1},kelvin,intensity,radius};
}
LightFixturePreset colored(std::string_view id,std::string_view category,std::string_view name,Vec3 color,float intensity,float radius) {
    return {id,category,name,false,LightColorMode::Color,color,6500,intensity,radius};
}
const std::array<LightFixturePreset,65> fixtures{{
    temperature("candle","Flame","Candle",1800,1,2),temperature("oil_lamp","Flame","Oil lamp",2000,4,4),
    temperature("lantern","Flame","Lantern",2200,8,5),temperature("wall_torch","Flame","Wall torch",1900,20,7),
    temperature("brazier","Flame","Brazier",1800,35,10),temperature("campfire","Flame","Campfire",2000,50,12),
    temperature("fireplace","Flame","Fireplace",1900,40,10),temperature("furnace","Flame","Furnace",1500,100,16),
    colored("blue_gas_flame","Flame","Blue gas flame",{.18f,.42f,1},8,4),
    temperature("dim_filament","Incandescent","Dim filament",2200,5,5),temperature("warm_bulb","Incandescent","Warm bulb",2700,12,8),
    temperature("bright_bulb","Incandescent","Bright bulb",2800,25,12),temperature("workshop_bulb","Incandescent","Workshop bulb",3000,40,16),
    temperature("halogen_desk","Halogen","Desk lamp",3000,15,7),temperature("halogen_work","Halogen","Work lamp",3200,60,18),
    temperature("halogen_headlamp","Halogen","Headlamp",3200,80,25),temperature("halogen_studio","Halogen","Studio lamp",3200,150,30),
    temperature("led_amber","LED","Amber decorative",2200,6,5),temperature("led_warm","LED","Warm bulb",2700,15,9),
    temperature("led_soft_white","LED","Soft white bulb",3000,20,10),temperature("led_neutral","LED","Neutral white bulb",4000,25,12),
    temperature("led_cool","LED","Cool white bulb",5000,30,14),temperature("led_daylight","LED","Daylight bulb",6500,35,16),
    temperature("led_warm_strip","LED","Warm strip segment",3000,8,5),temperature("led_neutral_strip","LED","Neutral strip segment",4000,10,6),
    temperature("led_panel","LED","Ceiling panel",4000,60,18),temperature("led_work","LED","Work light",5000,100,25),
    temperature("led_flood","LED","Floodlight",5700,250,40),temperature("led_street","LED","Street lamp",4000,150,30),
    temperature("fluorescent_warm","Fluorescent","Warm tube",3000,30,12),temperature("fluorescent_neutral","Fluorescent","Neutral tube",4000,40,15),
    temperature("fluorescent_cool","Fluorescent","Cool tube",6500,45,16),colored("fluorescent_aged","Fluorescent","Aged green tube",{.72f,1,.62f},25,12),
    colored("sodium_low","Discharge","Low-pressure sodium",{1,.58f,.06f},100,25),colored("sodium_high","Discharge","High-pressure sodium",{1,.70f,.35f},150,30),
    colored("mercury_vapor","Discharge","Mercury vapor",{.62f,.86f,1},120,28),temperature("metal_halide","Discharge","Metal halide",5000,200,35),
    temperature("xenon_arc","Discharge","Xenon arc",6000,250,40),
    colored("neon_red","Neon / sign","Red",{1,.08f,.025f},15,7),colored("neon_orange","Neon / sign","Orange",{1,.30f,.025f},15,7),
    colored("neon_amber","Neon / sign","Amber",{1,.62f,.04f},15,7),colored("neon_green","Neon / sign","Green",{.08f,1,.20f},15,7),
    colored("neon_cyan","Neon / sign","Cyan",{.025f,.85f,1},15,7),colored("neon_blue","Neon / sign","Blue",{.08f,.22f,1},15,7),
    colored("neon_violet","Neon / sign","Violet",{.48f,.10f,1},15,7),colored("neon_magenta","Neon / sign","Magenta",{1,.08f,.55f},15,7),
    colored("signal_red","Signal","Red warning lamp",{1,.025f,.01f},12,8),colored("signal_amber","Signal","Amber warning lamp",{1,.48f,.02f},12,8),
    colored("signal_blue","Signal","Blue warning lamp",{.025f,.18f,1},12,8),colored("signal_exit","Signal","Green exit lamp",{.08f,1,.20f},5,5),
    temperature("screen_warm","Display","Warm screen",4500,3,3),temperature("screen_neutral","Display","Neutral screen",6500,4,4),
    temperature("screen_cold","Display","Cold screen",9000,4,4),colored("crt_green","Display","CRT green",{.12f,1,.24f},3,3),
    colored("crt_amber","Display","CRT amber",{1,.52f,.08f},3,3),
    temperature("sun_dawn","Natural","Dawn",2500,.6f,0,true),temperature("sun_sunrise","Natural","Sunrise",3000,1,0,true),
    temperature("sun_morning","Natural","Morning sun",4500,1.5f,0,true),temperature("sun_noon","Natural","Noon sun",5500,2,0,true),
    temperature("sun_cool_daylight","Natural","Cool daylight",6500,1.5f,0,true),temperature("sun_overcast","Natural","Overcast",7500,.8f,0,true),
    temperature("sun_blue_fill","Natural","Blue sky fill",10000,.35f,0,true),temperature("sun_golden_hour","Natural","Golden hour",3500,1.2f,0,true),
    temperature("sun_sunset","Natural","Sunset",2200,.8f,0,true),temperature("sun_moonlight","Natural","Moonlight",9000,.08f,0,true)
}};

std::array<LightFlickerPreset,24> makeFlickers() {
    constexpr std::array<std::string_view,24> names{"Candle","Oil lamp","Torch","Fireplace","Campfire","Roaring fire",
        "Unstable incandescent","Failing fluorescent","Fluorescent startup","Faulty neon","Loose connection","Power brownout",
        "Soft breathing","Slow pulse","Hard blink","Strobe","Double flash","Heartbeat",
        "Lightning","Electrical arc","Welding","TV / monitor","Reactor instability","Random flashes"};
    std::array<LightFlickerPreset,24> result{};
    for(std::size_t i=0;i<result.size();++i) {
        auto& p=result[i];p.id=behaviorIds[i];p.name=names[i];p.category=i<6?"Fire":i<12?"Electrical":i<18?"Rhythm":"Irregular";
        p.settings.behavior=static_cast<LightFlickerBehavior>(i);p.settings.duration=30;
    }
    const auto fire=[&](std::size_t i,float slow,float slowRate,float flutter,float flutterRate,float softness,float low,float high,float shift) {
        auto& s=result[i].settings;s.slowAmount=slow;s.slowRate=slowRate;s.flutterAmount=flutter;s.flutterRate=flutterRate;s.smoothness=softness;
        s.intensity.minimum=low;s.intensity.maximum=high;s.color.minimumTemperatureShift=-shift;s.color.maximumTemperatureShift=shift;
    };
    fire(0,.35f,.7f,.5f,10,.9f,.8f,1.2f,120);fire(1,.2f,.45f,.25f,6,.95f,.9f,1.1f,80);
    fire(2,.6f,1.4f,.8f,13,.7f,.6f,1.4f,250);fire(3,.7f,.65f,.45f,7,.85f,.65f,1.3f,200);
    fire(4,.8f,.9f,.7f,11,.75f,.55f,1.5f,300);fire(5,.9f,1.8f,1,18,.45f,.45f,1.7f,400);
    for(std::size_t i=6;i<12;++i) {
        auto& s=result[i].settings;s.intensity.minimum=0;s.intensity.maximum=1;s.stableMinimum=1;s.stableMaximum=5;
        s.failureMinimum=.08f;s.failureMaximum=.5f;s.flashesMinimum=2;s.flashesMaximum=6;s.flashMinimum=.025f;s.flashMaximum=.12f;
        s.gapMinimum=.03f;s.gapMaximum=.18f;s.fallTime=.02f;s.recoveryTime=.08f;s.timingRandomness=.8f;s.brightnessRandomness=.35f;
    }
    auto& incandescent=result[6].settings;incandescent.failureLevel=.25f;incandescent.stableMinimum=3;incandescent.stableMaximum=9;incandescent.fallTime=.16f;incandescent.recoveryTime=.4f;
    auto& fluorescent=result[7].settings;fluorescent.stableMinimum=.8f;fluorescent.stableMaximum=3;fluorescent.flashesMaximum=8;fluorescent.recoveryTime=.025f;
    auto& startup=result[8].settings;startup.playback=LightFlickerPlayback::Once;startup.duration=8;startup.stableMinimum=2;startup.stableMaximum=4;startup.failureMinimum=.3f;startup.failureMaximum=1;startup.flashesMinimum=4;startup.flashesMaximum=9;
    auto& neon=result[9].settings;neon.failureLevel=.04f;neon.stableMinimum=.4f;neon.stableMaximum=2;neon.flashMaximum=.2f;neon.recoveryTime=.015f;
    auto& loose=result[10].settings;loose.stableMinimum=1;loose.stableMaximum=7;loose.failureMinimum=.2f;loose.failureMaximum=1.3f;loose.flashesMinimum=1;loose.flashesMaximum=4;loose.fallTime=0;loose.recoveryTime=0;
    auto& brownout=result[11].settings;brownout.failureLevel=.2f;brownout.stableMinimum=4;brownout.stableMaximum=12;brownout.failureMinimum=1;brownout.failureMaximum=3;brownout.flashesMinimum=1;brownout.flashesMaximum=2;brownout.flashMinimum=.2f;brownout.flashMaximum=.6f;brownout.fallTime=.7f;brownout.recoveryTime=1.2f;
    for(std::size_t i=12;i<18;++i) {
        auto& s=result[i].settings;s.timingRandomness=0;s.brightnessRandomness=0;s.intensity.minimum=0;s.intensity.maximum=1;
        s.period=2;s.pulseWidth=.5f;s.riseTime=.05f;s.fallTime=.05f;
    }
    auto& breathing=result[12].settings;breathing.period=4;breathing.pulseWidth=1;breathing.riseTime=.2f;breathing.fallTime=.2f;breathing.intensity.minimum=.3f;
    auto& slow=result[13].settings;slow.period=2.5f;slow.pulseWidth=.7f;slow.riseTime=.1f;slow.fallTime=.3f;
    auto& blink=result[14].settings;blink.period=1;blink.pulseWidth=.5f;blink.riseTime=blink.fallTime=0;
    auto& strobe=result[15].settings;strobe.period=.1f;strobe.pulseWidth=.12f;strobe.riseTime=strobe.fallTime=0;
    auto& doubleFlash=result[16].settings;doubleFlash.period=1.4f;doubleFlash.pulseWidth=.08f;doubleFlash.secondarySpacing=.18f;doubleFlash.secondaryStrength=1;doubleFlash.riseTime=0;doubleFlash.fallTime=.015f;
    auto& heartbeat=result[17].settings;heartbeat.period=.85f;heartbeat.pulseWidth=.18f;heartbeat.secondarySpacing=.28f;heartbeat.secondaryStrength=.65f;heartbeat.riseTime=.01f;heartbeat.fallTime=.02f;
    for(std::size_t i=12;i<18;++i)result[i].settings.duration=result[i].settings.period*std::ceil(30.f/result[i].settings.period);
    for(std::size_t i=18;i<24;++i) {
        auto& s=result[i].settings;s.intensity.minimum=0;s.intensity.maximum=1.5f;s.eventFrequency=1;s.restMinimum=.2f;s.restMaximum=1;
        s.flashesMinimum=1;s.flashesMaximum=4;s.flashMinimum=.02f;s.flashMaximum=.12f;s.gapMinimum=.02f;s.gapMaximum=.08f;
        s.riseTime=.005f;s.fallTime=.04f;s.timingRandomness=1;s.brightnessRandomness=.5f;s.afterglow=.1f;
    }
    auto& lightning=result[18].settings;lightning.eventFrequency=.3f;lightning.restMinimum=1;lightning.restMaximum=4;lightning.flashesMaximum=3;lightning.intensity.maximum=4;lightning.fallTime=.12f;lightning.afterglow=.35f;
    auto& arc=result[19].settings;arc.eventFrequency=3;arc.restMinimum=.1f;arc.restMaximum=.4f;arc.flashesMinimum=2;arc.flashesMaximum=7;arc.flashMaximum=.035f;arc.fallTime=.01f;arc.intensity.maximum=2.5f;
    auto& welding=result[20].settings;welding.eventFrequency=5;welding.restMinimum=0;welding.restMaximum=.2f;welding.flashesMinimum=5;welding.flashesMaximum=12;welding.flashMaximum=.06f;welding.gapMaximum=.025f;welding.afterglow=.2f;
    auto& tv=result[21].settings;tv.eventFrequency=8;tv.restMinimum=0;tv.restMaximum=.05f;tv.flashesMinimum=1;tv.flashesMaximum=3;tv.flashMinimum=.08f;tv.flashMaximum=.35f;tv.gapMinimum=0;tv.gapMaximum=.015f;tv.riseTime=.04f;tv.fallTime=.08f;tv.afterglow=.6f;tv.intensity.maximum=1.2f;
    auto& reactor=result[22].settings;reactor.eventFrequency=.7f;reactor.restMinimum=.5f;reactor.restMaximum=2;reactor.flashesMinimum=2;reactor.flashesMaximum=5;reactor.flashMinimum=.1f;reactor.flashMaximum=.45f;reactor.gapMinimum=.05f;reactor.gapMaximum=.3f;reactor.riseTime=.12f;reactor.fallTime=.25f;reactor.afterglow=.5f;reactor.intensity.maximum=2;
    auto& randomFlash=result[23].settings;randomFlash.eventFrequency=1.5f;randomFlash.restMinimum=.1f;randomFlash.restMaximum=2;randomFlash.flashesMinimum=1;randomFlash.flashesMaximum=2;randomFlash.flashMinimum=.04f;randomFlash.flashMaximum=.25f;randomFlash.riseTime=0;randomFlash.fallTime=.02f;randomFlash.afterglow=0;
    return result;
}
const auto flickers=makeFlickers();
} // namespace

Vec3 temperatureColorSrgb(float kelvin) {
    // CIE 1960 UCS Planckian-locus fit used by Godot Light3D (MIT), originally
    // from Google's Filament (Apache 2.0). Conversion is normalized linear RGB,
    // then sRGB encoded to match Crimson's existing authored-light convention.
    // https://github.com/godotengine/godot/blob/master/scene/3d/light_3d.cpp
    const double t=std::clamp(std::isfinite(kelvin)?kelvin:6500.f,MinimumLightTemperature,MaximumLightTemperature),t2=t*t;
    const double u=(.860117757+1.54118254e-4*t+1.28641212e-7*t2)/(1+8.42420235e-4*t+7.08145163e-7*t2);
    const double v=(.317398726+4.22806245e-5*t+4.20481691e-8*t2)/(1-2.89741816e-5*t+1.61456053e-7*t2);
    const double d=1/(2*u-8*v+4),x=3*u*d,y=2*v*d;
    const double X=x/y,Z=(1-x-y)/y;
    std::array<double,3> rgb{3.2404542*X-1.5371385-.4985314*Z,-.9692660*X+1.8760108+.0415560*Z,.0556434*X-.2040259+1.0572252*Z};
    const double maximum=std::max({rgb[0],rgb[1],rgb[2],.000001});
    return {srgb(std::clamp(rgb[0]/maximum,0.0,1.0)),srgb(std::clamp(rgb[1]/maximum,0.0,1.0)),srgb(std::clamp(rgb[2]/maximum,0.0,1.0))};
}
Vec3 resolveLightColorSrgb(LightColorMode mode,const Vec3& color,float temperatureKelvin) {
    return mode==LightColorMode::Temperature?temperatureColorSrgb(temperatureKelvin):color;
}
bool validLightColorMode(LightColorMode mode) { return mode==LightColorMode::Color||mode==LightColorMode::Temperature; }
bool validLightTemperature(float temperature) { return bounded(temperature,MinimumLightTemperature,MaximumLightTemperature); }
bool validLightFlicker(const LightFlickerSettings& s) {
    return s.behavior<LightFlickerBehavior::Count&&(s.playback==LightFlickerPlayback::Loop||s.playback==LightFlickerPlayback::Once)&&
        bounded(s.speed,.01f,100)&&bounded(s.duration,.01f,3600)&&bounded(s.startDelay,0,3600)&&bounded(s.timeOffset,-1000000,1000000)&&
        bounded(s.timingRandomness,0,1)&&bounded(s.brightnessRandomness,0,1)&&bounded(s.slowAmount,0,1)&&bounded(s.slowRate,0,1000)&&
        bounded(s.flutterAmount,0,1)&&bounded(s.flutterRate,0,1000)&&bounded(s.smoothness,0,1)&&bounded(s.brightnessBias,-1,1)&&
        interval(s.stableMinimum,s.stableMaximum,0,3600)&&interval(s.failureMinimum,s.failureMaximum,0,3600)&&bounded(s.failureLevel,0,1)&&
        s.flashesMinimum>=1&&s.flashesMaximum>=s.flashesMinimum&&s.flashesMaximum<=64&&
        interval(s.flashMinimum,s.flashMaximum,0,3600)&&interval(s.gapMinimum,s.gapMaximum,0,3600)&&interval(s.restMinimum,s.restMaximum,0,3600)&&
        bounded(s.eventFrequency,0,1000)&&bounded(s.riseTime,0,3600)&&bounded(s.fallTime,0,3600)&&bounded(s.recoveryTime,0,3600)&&bounded(s.afterglow,0,1)&&
        bounded(s.period,.001f,3600)&&bounded(s.pulseWidth,0,1)&&bounded(s.secondaryStrength,0,1)&&bounded(s.secondarySpacing,0,1)&&
        channelValid(s.intensity)&&channelValid(s.radius)&&tintValid(s.color.minimumTint)&&tintValid(s.color.maximumTint)&&
        interval(s.color.minimumTemperatureShift,s.color.maximumTemperatureShift,-14000,14000)&&bounded(s.color.response,.05f,10)&&bounded(s.color.amount,0,1);
}
std::span<const LightFixturePreset> lightFixturePresets() { return fixtures; }
std::span<const LightFlickerPreset> lightFlickerPresets() { return flickers; }
bool applyLightFixturePreset(ScenePointLight& light,std::string_view id) {
    const auto found=std::find_if(fixtures.begin(),fixtures.end(),[&](const auto& preset){return preset.id==id&&!preset.directional;});
    if(found==fixtures.end())return false;
    light.colorMode=found->colorMode;
    if(found->colorMode==LightColorMode::Color)light.color=found->color;else light.temperatureKelvin=found->temperatureKelvin;
    light.intensity=found->intensity;light.radius=found->radius;return true;
}
bool applyLightFixturePreset(SceneDirectionalLight& light,std::string_view id) {
    const auto found=std::find_if(fixtures.begin(),fixtures.end(),[&](const auto& preset){return preset.id==id&&preset.directional;});
    if(found==fixtures.end())return false;
    light.colorMode=found->colorMode;
    if(found->colorMode==LightColorMode::Color)light.color=found->color;else light.temperatureKelvin=found->temperatureKelvin;
    light.intensity=found->intensity;return true;
}
bool applyLightFlickerPreset(LightFlickerSettings& settings,std::string_view id) {
    const auto found=std::find_if(flickers.begin(),flickers.end(),[&](const auto& preset){return preset.id==id;});
    if(found==flickers.end())return false;
    auto replacement=found->settings;replacement.enabled=settings.enabled;replacement.seed=settings.seed;replacement.timeOffset=settings.timeOffset;settings=replacement;return true;
}
std::string_view lightFlickerBehaviorId(LightFlickerBehavior behavior) {
    const auto i=static_cast<std::size_t>(behavior);return i<behaviorIds.size()?behaviorIds[i]:std::string_view{};
}
std::optional<LightFlickerBehavior> lightFlickerBehaviorFromId(std::string_view id) {
    const auto found=std::find(behaviorIds.begin(),behaviorIds.end(),id);if(found==behaviorIds.end())return std::nullopt;
    return static_cast<LightFlickerBehavior>(found-behaviorIds.begin());
}
float sampleLightFlicker(const LightFlickerSettings& settings,double sceneSeconds) {
    if(!settings.enabled||!std::isfinite(sceneSeconds)||sceneSeconds<settings.startDelay)return .5f;
    const double time=localTime(settings,sceneSeconds);const auto behavior=static_cast<std::uint8_t>(settings.behavior);
    return saturate(behavior<6?continuous(settings,time):behavior<12?electrical(settings,time):behavior<18?rhythmic(settings,time):irregular(settings,time));
}
LightPresentation evaluateLightPresentation(LightColorMode mode,const Vec3& color,float kelvin,float intensity,float radius,
    const LightFlickerSettings& s,double seconds,bool animate) {
    LightPresentation result{resolveLightColorSrgb(mode,color,kelvin),intensity,radius};
    if(!animate||!s.enabled||!std::isfinite(seconds)||seconds<s.startDelay)return result;
    const float u=sampleLightFlicker(s,seconds);
    if(s.intensity.enabled)result.intensity=product(intensity,mix(s.intensity.minimum,s.intensity.maximum,channelSample(u,s.intensity.invert,s.intensity.response)));
    if(s.radius.enabled&&radius>0)result.radius=static_cast<float>(std::clamp(static_cast<double>(radius)*mix(s.radius.minimum,s.radius.maximum,channelSample(u,s.radius.invert,s.radius.response)),.01,100000.0));
    if(s.color.enabled&&s.color.amount>0) {
        const float t=channelSample(u,s.color.invert,s.color.response);
        if(mode==LightColorMode::Temperature)result.color=temperatureColorSrgb(static_cast<float>(kelvin+mix(s.color.minimumTemperatureShift,s.color.maximumTemperatureShift,t)*s.color.amount));
        else for(std::size_t channel=0;channel<3;++channel) {
            const double tint=mix(linear(s.color.minimumTint[channel]),linear(s.color.maximumTint[channel]),t);
            result.color[channel]=srgb(linear(color[channel])*mix(1.0,tint,s.color.amount));
        }
    }
    return result;
}
} // namespace crimson
