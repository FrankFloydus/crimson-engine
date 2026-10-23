#pragma once
#include "DecalModelSurface.h"
#include "DecalTransform.h"
namespace crimson {struct ModelMaterial;}
namespace crimson::decals {
bool receiverMaterialEligible(const ModelMaterial&,std::string* diagnostic=nullptr);
// Reorient the complete authored linear block rigidly between stable surface
// bases. This retains metre scale, in-plane rotation, shear and reflection.
Mat4 reanchorDecalFrame(const Mat4& previousWorld,Vec3 previousSurfaceNormal,const Mat4& newSurfaceFrame);
// Derived surface frame preserves the authored XY metric (size/shear/mirror)
// while remaining tangent even when ordinary XYZ rotation tilts an axis normal
// to the receiver. It never changes the authored transform or history.
std::optional<Mat4> conformDecalFrame(const Mat4& authored,Vec3 surfacePoint,Vec3 surfaceNormal);
}
