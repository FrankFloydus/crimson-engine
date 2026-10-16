#pragma once

#include "scene/CameraTemplates.h"
#include <functional>

namespace crimson {
enum class CameraAuthoringControl : unsigned { Offset,Rotation,Fov,Near,Far };
struct CameraAuthoringControlsState {
    EntityId camera{};
    Mat4 shownBasis{},rotationBaseline{};
    math::AffineTransformFields rotationFields{};
    Vec3 degrees{};
    unsigned activeRotationItem{};
    bool valid{},rotationEditing{};
};
// Called immediately after each real property widget so the owning panel can
// start/update/finish its scene transaction using that item's activation state.
using CameraAuthoringCallback=std::function<void(CameraAuthoringControl,bool changed)>;
void drawCameraAuthoringControls(CameraAuthoringControlsState&,EntityId camera,CameraTemplate&,
                                 const CameraAuthoringCallback&);
}
