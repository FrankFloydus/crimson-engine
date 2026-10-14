#pragma once

#include "particles/ParticleTypes.h"
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace crimson {

inline constexpr float MaximumParticlePreprocessSeconds=60.f;

struct ParticleEffectAsset {
    std::string id, name{"New Particle Effect"};
    uint64_t defaultSeed{1};
    float preprocessSeconds{}; // Saved simulated time before playback is shown.
    ParticleEmitterDefinition emitter;
    bool operator==(const ParticleEffectAsset&) const = default;
};
struct ParticleAssetEntry { std::string id, name; };

ParticleEffectAsset defaultParticleEffect(const std::string& name = "New Particle Effect");
void validateParticleAsset(const ParticleEffectAsset& asset);
// Explicitly adopts the Explosiveness control. Legacy burst counts are folded
// into rate over duration; continuous emitters extend that duration through the
// last legacy burst. Clears those records only after the complete candidate is
// valid. Returns whether values changed; invalid input/rate overflow throws and
// leaves the emitter untouched. Calling with zero also adopts legacy schedules.
bool setParticleExplosiveness(ParticleEmitterDefinition& emitter, float value);
std::filesystem::path particleAssetPath(const std::filesystem::path& projectRoot, const std::string& id);
// Writers publish native v8 with Explosiveness and optional legacyBursts. Readers
// retain v1-v7 emission schedules with Explosiveness zero, v1-v6 preprocessing
// disabled, and decode v1-v5 authored sRGB once. Loading never rewrites files.
std::string serializeParticleAsset(const ParticleEffectAsset& asset);
ParticleEffectAsset deserializeParticleAsset(std::string_view text, const std::string& expectedId = {});
ParticleEffectAsset loadParticleAsset(const std::filesystem::path& projectRoot, const std::string& id);
void saveParticleAsset(const std::filesystem::path& projectRoot, const ParticleEffectAsset& asset);
ParticleEffectAsset createParticleAsset(const std::filesystem::path& projectRoot,
                                      const std::string& name = "New Particle Effect");
// Save As publishes a new native identity; the source file is never modified.
ParticleEffectAsset saveParticleAssetAs(const std::filesystem::path& projectRoot,
                                       const ParticleEffectAsset& asset, const std::string& name = {});
std::vector<ParticleAssetEntry> scanParticleAssets(const std::filesystem::path& projectRoot,
                                                std::vector<std::string>* diagnostics = nullptr);

} // namespace crimson
