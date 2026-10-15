#pragma once
#include "assets/ModelAnimation.h"
#include <bgfx/bgfx.h>
#include <memory>
#include <span>

namespace crimson {
// Immutable auxiliary inputs shared by every instance of a resident original LOD.
struct ModelDeformationSource {
    bgfx::DynamicVertexBufferHandle skin{BGFX_INVALID_HANDLE},morph{BGFX_INVALID_HANDLE};
    std::uint32_t vertexCount{},morphCount{};
    bool skinned{};
    std::size_t bytes{},skinFloats{},morphFloats{},skinUploaded{},morphUploaded{};
    bool ready() const{return skinUploaded==skinFloats && morphUploaded==morphFloats;}
    ~ModelDeformationSource();
};
std::shared_ptr<ModelDeformationSource> createModelDeformationSource(const ModelPrimitive&);
std::size_t uploadModelDeformationSource(ModelDeformationSource&,const ModelPrimitive&,std::size_t byteBudget);
struct DeformedModelVertices {
    bgfx::DynamicVertexBufferHandle vertices{BGFX_INVALID_HANDLE},poseInput{BGFX_INVALID_HANDLE};
    std::uint32_t vertexCount{},vertexCapacity{},poseCapacity{};
    std::size_t bytes{};
    std::shared_ptr<const ModelDeformationSource> source;
    std::shared_ptr<const ModelPose> pose;
    std::shared_ptr<const void> geometryLease;
    ~DeformedModelVertices();
    void bind() const;
};
// Outputs are immutable per pose. Neither another camera nor a thumbnail can
// overwrite vertices already queued for picking, depth, color or shadows.
class ModelDeformationGpu {
public:
    explicit ModelDeformationGpu(bgfx::ProgramHandle);
    ~ModelDeformationGpu();
    void advance(std::uint64_t frame);
    void setProgram(bgfx::ProgramHandle);
    std::shared_ptr<const DeformedModelVertices> prepare(std::shared_ptr<const ModelDeformationSource>,
        bgfx::DynamicVertexBufferHandle,bgfx::VertexBufferHandle,std::shared_ptr<const void> geometryLease,
        std::shared_ptr<const ModelPose>,std::int32_t node,bgfx::ViewId);
    std::size_t bytes() const;
    void trim(std::size_t targetBytes);
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
