#pragma once
#include "crimson-renderer/MaterialGraphShader.h"

namespace crimson {
// The build warmer and runtime must prepare the same first usable programs.
// Coverage and vertex work is cheap; expensive surface/domain optimization is
// deferred without changing shader semantics or coherent publication.
constexpr bool materialGraphInteractivePass(MaterialGraphPass pass) {
    switch(pass) {
    case MaterialGraphPass::Decal:case MaterialGraphPass::Model:case MaterialGraphPass::Particle:
    case MaterialGraphPass::ModelTransmission:case MaterialGraphPass::ParticleTransmission:
    case MaterialGraphPass::ModelReflection:case MaterialGraphPass::ParticleReflection:return true;
    default:return false;
    }
}
}
