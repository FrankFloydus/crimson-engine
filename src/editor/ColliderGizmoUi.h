#pragma once
#include "ColliderGizmo.h"
struct ImDrawList;
namespace crimson {
struct ColliderGizmoResult { bool hovered{},started{},canceled{}; };
// Call over the viewport image, in its top overlay draw channel. UI events stay
// in this ImGui context; SceneDocument owns every resulting authored mutation.
ColliderGizmoResult drawColliderGizmo(ImDrawList&,SceneDocument&,ColliderGizmoDrag&,const Mat4& viewProjection,const Vec4& viewport,
                                     bool visible,bool allowStart,bool focused,bool cancelRequested);
} // namespace crimson
