#pragma once
#include "assets/RagdollAsset.h"
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace crimson {
enum class RagdollAuthoringOperation { Translate,Rotate,Resize };

// Bone frames are right-handed rigid posed model/world frames. They carry no
// inherited bone scale; dimensions and offsets remain model metres. For Resize
// the proxy's axis lengths are full box dimensions, sphere diameters, or
// capsule diameter/full-height/diameter. Other operations use unit axes.
std::optional<Mat4> ragdollBodyGizmoMatrix(const RagdollBody&,const Mat4& rigidBoneFrame,
    RagdollAuthoringOperation,std::string* diagnostic=nullptr);
// Recompute every gesture update from the captured body, never the preceding
// preview. Resize keeps its captured centre and rotation. Sphere scales are
// uniform; capsule X/Z radii are coupled and full height includes both caps.
std::optional<RagdollBody> applyRagdollBodyGizmoTransform(const RagdollBody& baseline,
    const Mat4& rigidBoneFrame,const Mat4& manipulated,RagdollAuthoringOperation,
    bool lockProportions=false,std::string* diagnostic=nullptr);

struct RagdollBodyPick {
    std::size_t body{};
    float distance{};
    Vec3 position{},normal{};
};
// Exact analytic authored shapes; frames are indexed by ModelNode, while the
// returned body index addresses the supplied body array. Distance is metres
// along a normalized world ray. An inside hit returns the forward exit surface.
std::optional<RagdollBodyPick> pickRagdollBodies(std::span<const RagdollBody>,
    std::span<const Mat4> rigidBoneFrames,const Ray&,float maximumDistance=1e6f);
enum class RagdollSimulationScope { All,SelectedBody,SelectedBranch };
// SelectedBranch includes the selected body and its physical descendants.
// The pin mask fixes every body outside the selected simulation scope. An
// optional extra pin fixes each simulated component's highest physical root.
std::vector<std::uint8_t> ragdollSimulationMask(std::span<const RagdollBody>,std::size_t selected,
    RagdollSimulationScope);
std::vector<std::uint8_t> ragdollScopePinMask(std::span<const RagdollBody>,std::span<const std::uint8_t> simulated,
    bool pinScopeRoot=false);
} // namespace crimson
