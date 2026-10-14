#pragma once

namespace crimson {

// Authored sprite-facing policy, independent of material and simulation space.
// Serialized identities are stable; append future modes rather than renumbering.
enum class ParticleBillboardMode { Disabled=0, Enabled=1, FixedY=2 };

} // namespace crimson
