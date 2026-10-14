#pragma once

#include "ParticleTypes.h"
#include <array>

namespace crimson {

// Authoring keys are validated before preparation. These bounded CPU values
// own no clocks or references into a document and are immutable during a frame.
// The first/last authored ages may be inside [0,1]; evaluation holds their
// values before/after that authored interval instead of extrapolating.
class PreparedParticleCurve {
public:
    float evaluate(double normalizedAge) const;
    float maximum() const { return maximum_; }
    bool empty() const { return count_==0; }
private:
    struct Segment { double age{},endAge{},slope{};float value{}; };
    std::array<Segment,MaximumParticleAppearanceKeys-1> segments_{};
    std::size_t count_{};
    float finalValue_{},maximum_{};
    friend PreparedParticleCurve prepareParticleCurve(const ParticleScalarCurve&);
};
class PreparedParticleGradient {
public:
    Vec4 evaluate(double normalizedAge) const;
    bool empty() const { return count_==0; }
private:
    struct Segment { double age{},endAge{},inverseDuration{};Vec4 value{},difference{}; };
    std::array<Segment,MaximumParticleAppearanceKeys-1> segments_{};
    std::size_t count_{};
    Vec4 finalValue_{};
    friend PreparedParticleGradient prepareParticleGradient(const ParticleColorGradient&);
};
PreparedParticleCurve prepareParticleCurve(const ParticleScalarCurve&);
PreparedParticleGradient prepareParticleGradient(const ParticleColorGradient&);

struct PreparedParticleScalar {
    ParticleAppearanceMode mode{ParticleAppearanceMode::Constant};
    float constant{},maximumValue{};
    double rangeMinimum{},rangeSpan{};
    PreparedParticleCurve curve;
    float evaluate(double normalizedAge,uint64_t mixedSeed,uint64_t spawnOrdinal,uint32_t lane) const;
};
struct ParticleAppearanceSample {
    float size{},rotation{}; // Rotation in radians; size in world meters.
    Vec4 color{}; // Straight linear HDR RGB and linear opacity.
};
struct PreparedParticleAppearance {
    PreparedParticleScalar size,intensity,rotation;
    ParticleColorMode colorMode{ParticleColorMode::Constant};
    Vec4 constantColor{}; // Straight linear HDR RGB, before intensity application.
    PreparedParticleGradient gradient;
    ParticleAppearanceSample sample(double normalizedAge,uint64_t mixedSeed,uint64_t spawnOrdinal) const;
    float maximumSize() const { return size.maximumValue; }
};
PreparedParticleAppearance prepareParticleAppearance(const ParticleEmitterDefinition&);

// Same counter lanes as birth sampling: no mutable random generator and no
// dependence on compaction, evaluation order or another appearance property.
uint64_t particleAppearanceSeed(uint64_t effectSeed,uint64_t emitterId);
float particleAppearanceRange(const ParticleAppearanceRange&,uint64_t effectSeed,uint64_t emitterId,
                              uint64_t spawnOrdinal,uint32_t lane);
// For legacy asset migration and display-sRGB picker adapters only. Canonical
// authored RGB, emitted radiance and gradient interpolation are already linear.
// Opacity has no color transfer function.
float particleSrgbToLinear(float value);

} // namespace crimson
