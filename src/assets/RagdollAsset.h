#pragma once

#include "ModelAsset.h"
#include "resources/AssetRef.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace crimson {

enum class RagdollShape { Capsule,Box,Sphere };

// The reference frame is the source joint's model-space bind transform with
// scale/shear removed. Offsets and dimensions are model metres, independently
// of inherited bone scale. Rotation quaternions use x,y,z,w.
struct RagdollJoint {
    Vec3 position{}; // Anchor offset from the child joint in its reference frame.
    Quat rotation{}; // Joint X is twist; Y/Z define the swing axes.
    float swingY{45},swingZ{45},twistMin{-30},twistMax{30}; // Degrees.
    float friction{}; // Maximum constraint friction torque, N m.
    bool operator==(const RagdollJoint&) const = default;
};
struct RagdollBody {
    std::uint32_t node{};
    std::int32_t parent{-1}; // Earlier body index: nearest selected bone ancestor.
    RagdollShape shape{RagdollShape::Capsule};
    Vec3 position{};
    Quat rotation{}; // Capsule axis is body-local +Y.
    Vec3 size{.2f,.4f,.2f}; // Full box dimensions.
    float radius{.1f},height{.4f}; // Capsule total height, including both caps.
    float mass{1},friction{.5f},restitution{};
    RagdollJoint joint; // Ignored for root bodies, retained when editing.
    bool operator==(const RagdollBody&) const = default;
};
struct RagdollAsset {
    std::string id,name;
    AssetRef<ModelAsset> sourceModel;
    std::string bindingSignature;
    std::uint32_t skin{};
    float gravityFactor{1};
    std::vector<RagdollBody> bodies; // Empty is a valid unfinished document.
    bool operator==(const RagdollAsset&) const = default;
};

// One right-handed rigid model-space reference frame per source node. Throws
// for singular/reflected or nonfinite reference transforms.
std::vector<Mat4> ragdollReferenceTransforms(const ModelAsset&);
void validateRagdollAsset(const RagdollAsset&);
void validateRagdollSource(const RagdollAsset&,const ModelAsset&);
RagdollAsset makeRagdollAsset(const ModelAsset&,std::uint32_t skin=0,const std::string& name={});
// Empty selection means all joints of the selected skin. This is a geometric
// starting point; no humanoid names or fixed skeleton layout are assumed.
void generateRagdollBodies(RagdollAsset&,const ModelAsset&,const std::vector<std::uint32_t>& selectedNodes={});
// Explicitly approved source change. Mapping has one target node per body;
// missing, duplicate or incompatible selections reject without changing data.
void rebindRagdollSource(RagdollAsset&,const ModelAsset&,std::uint32_t skin,
    const std::vector<std::uint32_t>& mappedNodes);
std::string serializeRagdollAsset(const RagdollAsset&);
RagdollAsset deserializeRagdollAsset(std::string_view,const std::string& expectedId={});
std::filesystem::path ragdollAssetPath(const std::filesystem::path& root,const std::string& id);
RagdollAsset loadRagdollAsset(const std::filesystem::path& root,const std::string& id);
// An initial destination is used only for a new asset. Existing assets retain
// their registered path; relocation belongs to the Content transaction API.
void saveRagdollAsset(const std::filesystem::path& root,const RagdollAsset&,
    const std::filesystem::path& initialDestination={});
RagdollAsset copyRagdollAsset(const std::filesystem::path& root,const RagdollAsset&,const std::string& name={});

} // namespace crimson
