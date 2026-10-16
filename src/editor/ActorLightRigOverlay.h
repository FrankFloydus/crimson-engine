#pragma once

#include "crimson-renderer/LightingSettings.h"
#include "math/Math.h"

struct ImDrawList;

namespace crimson {
class Scene;
void drawSceneLightRigOverlays(ImDrawList&,const Scene&,const Mat4& viewProjection,const Vec4& viewport,ClipDepth depth);
// Passive world-space character directions, local range and darkness sphere in Scene
// and Game. They never claim mouse or keyboard ownership.
void drawActorLightRigOverlay(ImDrawList&,const ActorLightRigSettings&,const DarknessRevealSettings&,const Vec3& characterPosition,float characterHeight,
    const Mat4& viewProjection,const Vec4& viewport,ClipDepth);
}
