#pragma once

#include "Renderer.h"

namespace crimson {
// One conservative receiver box for lit sprites, independent of camera-facing
// mode. Keep the same packet/material caps as submission; the empty material
// table uses the renderer's unlit fallback and contributes no receivers.
inline Aabb particleShadowReceiverBounds(std::span<const RenderParticle> particles,
    std::span<const RenderParticleMaterial> materials) {
    Aabb bounds;
    if(materials.empty())return bounds;
    const auto count=std::min(particles.size(),MaxParticleDrawCount);
    const auto materialCount=std::min(materials.size(),MaxParticleMaterialCount);
    for(std::size_t index=0;index<count;++index) {
        const auto& particle=particles[index];
        if(particle.materialIndex>=materialCount)continue;
        const auto& material=materials[particle.materialIndex];
        const auto shading=material.graph?material.graph->shading:material.shading;
        if(shading==MaterialShading::Unlit)continue;
        if(!particle_rendering::valid(particle))continue;
        // Half the square's diagonal, expanded for the accepted .001 squared-
        // length and orthogonality tolerances of authored right/up vectors.
        const float radius=particle.size*.708f;
        const Vec3 extent{radius,radius,radius};
        math::include(bounds,particle.position-extent);math::include(bounds,particle.position+extent);
    }
    return bounds;
}
} // namespace crimson
