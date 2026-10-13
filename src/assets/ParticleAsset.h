#pragma once

#include "particles/ParticleTypes.h"
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace crimson {

struct ParticleEffectAsset {
    std::string id, name{"New Particle Effect"};
    uint64_t defaultSeed{1};
    ParticleEmitterDefinition emitter;
    bool operator==(const ParticleEffectAsset&) const = default;
};
struct ParticleAssetEntry { std::string id, name; };

ParticleEffectAsset defaultParticleEffect(const std::string& name = "New Particle Effect");
void validateParticleAsset(const ParticleEffectAsset& asset);
std::filesystem::path particleAssetPath(const std::filesystem::path& projectRoot, const std::string& id);
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
