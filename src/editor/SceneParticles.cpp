#include "SceneParticles.h"
#include <algorithm>
#include <unordered_set>

namespace crimson {
void SceneParticles::reconcile(std::uint64_t generation,std::span<const ExtractedParticle> authored,
                              const DefinitionQuery& query,double seconds,EntityId restart,std::optional<double> interpolationOverride,
                              std::span<const ExtractedParticle> presentationTransforms) {
    if(generation_!=generation) { instances_.clear();generation_=generation; }
    status_.clear();presentation_.clear();groups_.clear();
    struct Admission { const ExtractedParticle* source;ParticleDefinitionVersion version;bool updated{}; };
    std::vector<Admission> admitted;admitted.reserve(std::min(authored.size(),MaximumInstances));
    std::unordered_set<EntityId> retained;
    std::size_t capacity=0;
    for(const auto& source:authored) {
        if(!math::finite(source.world) || !math::isAffine(source.world)) {
            status_+="Particle entity "+std::to_string(source.id)+": invalid composed affine transform; playback is suspended.\n";
            continue;
        }
        const auto definition=query(source.assetId);
        if(!definition.definition) {
            status_+="Particle entity "+std::to_string(source.id)+": "+(definition.diagnostic.empty()?"effect is unresolved.":definition.diagnostic)+"\n";
            continue;
        }
        const auto required=definition.definition->emitter.capacity;
        if(retained.size()>=MaximumInstances || required>MaximumReservedParticles-capacity) {
            status_+="Particle entity "+std::to_string(source.id)+": scene instance/capacity budget exceeded.\n";continue;
        }
        capacity+=required;retained.insert(source.id);
        admitted.push_back({&source,definition});
    }
    std::unordered_map<EntityId,const Mat4*> presentationWorlds;
    presentationWorlds.reserve(admitted.size());
    for(const auto& source:presentationTransforms)
        if(retained.contains(source.id) && math::finite(source.world) && math::isAffine(source.world))
            presentationWorlds.try_emplace(source.id,&source.world);
    // Release excluded instances before new reservations. Shrinking versions
    // prepare first, so steady reservations never exceed the admitted budget.
    std::erase_if(instances_,[&](const auto& entry){return !retained.contains(entry.first);});
    for(auto& entry:admitted) {
        auto found=instances_.find(entry.source->id);
        if(found==instances_.end() || found->second.simulation.reservedCapacity()<=entry.version.definition->emitter.capacity)continue;
        auto& instance=found->second;
        const auto& previous=instance.simulation.definition();const auto& next=*entry.version.definition;
        if(previous.id!=next.id || previous.emitter.id!=next.emitter.id)
            instance.simulation.restart(next,next.defaultSeed,entry.source->world);
        else { instance.simulation.setTransform(entry.source->world);instance.simulation.updateDefinition(next); }
        instance.assetId=entry.source->assetId;instance.revision=entry.version.revision;entry.updated=true;
    }
    for(const auto& entry:admitted) {
        const auto& source=*entry.source;const auto& definition=entry.version;
        auto [iterator,created]=instances_.try_emplace(source.id);
        auto& instance=iterator->second;
        const auto& previous=instance.simulation.definition();const auto& next=*definition.definition;
        if(created || previous.id!=next.id || previous.emitter.id!=next.emitter.id || restart==source.id) {
            instance.simulation.restart(*definition.definition,definition.definition->defaultSeed,source.world);
        } else {
            instance.simulation.setTransform(source.world);
            if(!entry.updated && instance.revision!=definition.revision)instance.simulation.updateDefinition(next);
        }
        instance.assetId=source.assetId;instance.revision=definition.revision;
        instance.presentationWorld=source.world;
        if(const auto presented=presentationWorlds.find(source.id);presented!=presentationWorlds.end())instance.presentationWorld=*presented->second;
    }
    for(const auto& entry:admitted) {
        const auto& source=*entry.source;const auto& definition=entry.version;const auto& next=*definition.definition;
        auto& instance=instances_.at(source.id);
        instance.simulation.advance(seconds);
        const auto first=presentation_.size();instance.simulation.appendPresentation(presentation_,interpolationOverride,&instance.presentationWorld);
        groups_.push_back({source.id,next.emitter.material.id,first,presentation_.size()-first});
        const auto& stats=instance.simulation.stats();
        if(stats.droppedSpawns || stats.droppedCatchUpTicks)
            status_+="Particle entity "+std::to_string(source.id)+": dropped "+std::to_string(stats.droppedSpawns)+" births, "+std::to_string(stats.droppedCatchUpTicks)+" catch-up ticks.\n";
        if(stats.clearedCapacityParticles)
            status_+="Particle entity "+std::to_string(source.id)+": capacity edits cleared "+std::to_string(stats.clearedCapacityParticles)+" particles.\n";
        if(stats.droppedScheduleSegments)
            status_+="Particle entity "+std::to_string(source.id)+": skipped "+std::to_string(stats.droppedScheduleSegments)+" emission-cycle segments because the scheduling budget was exceeded.\n";
        if(!definition.diagnostic.empty())status_+="Particle entity "+std::to_string(source.id)+": "+definition.diagnostic+"\n";
    }
}
const CpuParticleSystem* SceneParticles::instance(EntityId id) const {
    const auto found=instances_.find(id);return found==instances_.end()?nullptr:&found->second.simulation;
}
std::optional<Aabb> SceneParticles::bounds(EntityId id) const {
    const auto found=instances_.find(id);
    if(found==instances_.end() || found->second.simulation.particles().empty())return std::nullopt;
    const auto* simulation=&found->second.simulation;
    Aabb bounds;
    // A square can rotate in any plane about its center. Use its half diagonal
    // and the prepared lifetime maximum for conservative selection bounds.
    const auto radius=simulation->maxPresentationSize()*.7071067812f;
    for(const auto& p:simulation->particles()) {
        const auto world=simulation->particleWorldPosition(p,&found->second.presentationWorld);
        if(!math::finite(world))continue;
        math::include(bounds,world-Vec3{radius,radius,radius});math::include(bounds,world+Vec3{radius,radius,radius});
    }
    return bounds.valid ? std::optional<Aabb>{bounds} : std::nullopt;
}
}
