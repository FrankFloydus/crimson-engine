#pragma once
#include "DecalModelSurface.h"
#include "scene/SceneDecal.h"
namespace crimson {struct ModelMaterial;}
namespace crimson::decals {
bool receiverPolicyAllows(const SceneDecalReceiverPolicy&,uint64_t seedEntity,uint64_t seedQuaderOwner,uint64_t candidateEntity,uint64_t candidateQuaderOwner);
bool receiverMaterialEligible(const ModelMaterial&,std::string* diagnostic=nullptr);
// Stable unit surface frame: world-up on walls, world-X on near-horizontal surfaces.
Mat4 surfacePlacementFrame(Vec3 position,Vec3 surfaceNormal);
// Reorient the complete authored linear block rigidly between stable surface
// bases. This retains metre scale, in-plane rotation, shear and reflection.
Mat4 reanchorDecalFrame(const Mat4& previousWorld,Vec3 previousSurfaceNormal,const Mat4& newSurfaceFrame);
struct SeedResolution {SurfaceHitStatus status{SurfaceHitStatus::Missing};std::optional<ModelSurfaceHit> hit;std::string diagnostic;};
// The frame remains authored authority. Only this same occurrence/draw/primitive
// may be rebound; the retained local hint never moves the authored frame.
SeedResolution resolveAuthoredSeed(const ModelSurface&,const SceneDecalSeed&,const Mat4& entityWorld,const Mat4& decalFrame);
}
