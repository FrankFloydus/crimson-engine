#pragma once
#include "particles/CpuParticleSystem.h"
#include "scene/SceneExtraction.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace crimson {
struct ParticleDefinitionVersion {
    std::shared_ptr<const ParticleEffectAsset> definition;
    std::uint64_t revision{};
    std::string diagnostic;
};
struct SceneParticleGroup {
    EntityId entity{};
    std::string materialId;
    std::size_t first{}, count{};
};
// Live scene state uses persistent IDs and document replacement generation.
// Ordinary registry rebuilds, selection and transform edits preserve playback.
// Effective visibility suppresses packets/bounds, never simulation admission.
class SceneParticles {
public:
    using DefinitionQuery=std::function<ParticleDefinitionVersion(const std::string&)>;
    static constexpr std::size_t MaximumInstances=256,MaximumReservedParticles=262144;
    // A transactional restart can additionally hold one replacement emitter
    // reservation (at most MaximumParticleCapacity) before retiring its old buffer.
    void reconcile(std::uint64_t documentGeneration,std::span<const ExtractedParticle>,const DefinitionQuery&,
                   double seconds,EntityId restart=NoEntity,std::optional<double> interpolationOverride = {},
                   std::span<const ExtractedParticle> presentationTransforms = {});
    std::span<const ParticlePresentation> presentation() const { return presentation_; }
    std::span<const SceneParticleGroup> groups() const { return groups_; }
    std::optional<Aabb> bounds(EntityId) const;
    const CpuParticleSystem* instance(EntityId) const;
    const std::string& status() const { return status_; }
private:
    struct Instance {
        std::string assetId;std::uint64_t revision{};CpuParticleSystem simulation;
        Mat4 presentationWorld{}; // Copied view of the interpolated hierarchy, independent of the tick transform.
        bool visible{true};
    };
    std::unordered_map<EntityId,Instance> instances_;
    std::uint64_t generation_{};
    std::vector<ParticlePresentation> presentation_;
    std::vector<SceneParticleGroup> groups_;
    std::string status_;
};
}
