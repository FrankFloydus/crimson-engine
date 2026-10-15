#pragma once

#include "crimson-renderer/LightingSettings.h"
#include "math/Math.h"

struct ImDrawList;

namespace crimson {
// Passive world-space character directions, local range and darkness sphere in Scene
// and Game. They never claim mouse or keyboard ownership.
void drawPlayerLightRigOverlay(ImDrawList&,const PlayerLightRigSettings&,const CharacterRevealSettings&,const Vec3& characterPosition,float characterHeight,
    const Mat4& viewProjection,const Vec4& viewport,ClipDepth);
}
