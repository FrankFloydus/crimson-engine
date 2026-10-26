#pragma once
#include "math/Math.h"
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace crimson {
enum class LightColorMode : std::uint8_t { Color, Temperature };
enum class LightFlickerBehavior : std::uint8_t {
    Candle, OilLamp, Torch, Fireplace, Campfire, RoaringFire,
    UnstableIncandescent, FailingFluorescent, FluorescentStartup, FaultyNeon, LooseConnection, PowerBrownout,
    SoftBreathing, SlowPulse, HardBlink, Strobe, DoubleFlash, Heartbeat,
    Lightning, ElectricalArc, Welding, TvMonitor, ReactorInstability, RandomFlashes, Count
};
enum class LightFlickerPlayback : std::uint8_t { Loop, Once };
struct LightFlickerChannel {
    bool enabled{true};
    float minimum{.8f},maximum{1.2f};
    bool invert{};
    float response{1.f};
    bool operator==(const LightFlickerChannel&) const = default;
};
struct LightFlickerColor {
    bool enabled{};
    Vec3 minimumTint{1,1,1},maximumTint{1,1,1}; // Authored sRGB, multiplied in linear light.
    float minimumTemperatureShift{-150},maximumTemperatureShift{150}; // Kelvin relative to base.
    bool invert{};
    float response{1},amount{1};
    bool operator==(const LightFlickerColor&) const = default;
};
struct LightFlickerSettings {
    bool enabled{};
    LightFlickerBehavior behavior{LightFlickerBehavior::Candle};
    LightFlickerPlayback playback{LightFlickerPlayback::Loop};
    float speed{1},duration{8},startDelay{},timeOffset{};
    std::uint32_t seed{1};
    float timingRandomness{.3f},brightnessRandomness{.2f};
    float slowAmount{.3f},slowRate{1},flutterAmount{.7f},flutterRate{8},smoothness{.7f},brightnessBias{};
    float stableMinimum{2},stableMaximum{8},failureMinimum{.1f},failureMaximum{.8f},failureLevel{};
    std::uint32_t flashesMinimum{2},flashesMaximum{5};
    float flashMinimum{.02f},flashMaximum{.08f},gapMinimum{.03f},gapMaximum{.15f};
    float restMinimum{1},restMaximum{4},eventFrequency{1};
    float riseTime{.02f},fallTime{.05f},recoveryTime{.12f},afterglow{};
    float period{1},pulseWidth{.5f},secondaryStrength{.6f},secondarySpacing{.2f};
    LightFlickerChannel intensity{};
    LightFlickerColor color{};
    LightFlickerChannel radius{false,.9f,1.1f,false,1};
    bool operator==(const LightFlickerSettings&) const = default;
};
struct ScenePointLight;
struct SceneSpotLight;
struct SceneDirectionalLight;
struct LightFixturePreset {
    std::string_view id,category,name;
    bool directional{};
    LightColorMode colorMode{LightColorMode::Temperature};
    Vec3 color{1,1,1};
    float temperatureKelvin{6500},intensity{35},radius{20};
};
struct LightFlickerPreset {
    std::string_view id,category,name;
    LightFlickerSettings settings;
};
inline constexpr float MinimumLightTemperature=1000,MaximumLightTemperature=15000;
Vec3 temperatureColorSrgb(float kelvin);
Vec3 resolveLightColorSrgb(LightColorMode,const Vec3& manualColor,float temperatureKelvin);
bool validLightColorMode(LightColorMode);
bool validLightTemperature(float);
bool validLightFlicker(const LightFlickerSettings&);
std::span<const LightFixturePreset> lightFixturePresets();
std::span<const LightFlickerPreset> lightFlickerPresets();
bool applyLightFixturePreset(ScenePointLight&,std::string_view id);
bool applyLightFixturePreset(SceneSpotLight&,std::string_view id);
bool applyLightFixturePreset(SceneDirectionalLight&,std::string_view id);
// Applies behavior tuning, retaining the independent enabled switch, seed and offset.
bool applyLightFlickerPreset(LightFlickerSettings&,std::string_view id);
std::string_view lightFlickerBehaviorId(LightFlickerBehavior);
std::optional<LightFlickerBehavior> lightFlickerBehaviorFromId(std::string_view);
float sampleLightFlicker(const LightFlickerSettings&,double sceneSeconds);
struct LightPresentation { Vec3 color;float intensity{},radius{}; };
LightPresentation evaluateLightPresentation(LightColorMode,const Vec3& color,float temperatureKelvin,
    float intensity,float radius,const LightFlickerSettings&,double sceneSeconds,bool animate=true);
} // namespace crimson
