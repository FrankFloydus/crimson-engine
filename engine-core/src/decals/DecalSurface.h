#pragma once

#include "math/Math.h"
#include <array>
#include <compare>
#include <cstdint>
#include <memory>
#include <stop_token>
#include <string>
#include <vector>

namespace crimson::decals {
struct ReceiverId {
    uint64_t entity{};
    uint32_t draw{},primitive{};
    auto operator<=>(const ReceiverId&) const = default;
};
struct SurfaceTriangle {
    uint32_t triangle{};
    std::array<Vec3,3> positions{}; // Receiver-local original LOD0.
    std::array<Vec3,3> normals{};   // Unperturbed receiver shading normals; zero uses geometric normal.
};
// A refined MeshPaint perimeter point retains its original, straight source
// edge and exact dyadic parameter. This is construction identity, independent
// of rounded render positions, shading aliases and paint weights.
struct SurfaceBoundaryPoint { Vec3 first{},last{};double parameter{}; };
struct SurfaceBoundaryTriangle {
    uint32_t triangle{};
    uint8_t corners{};
    std::array<SurfaceBoundaryPoint,3> points{};
};
struct SurfaceBoundaryLineage {
    std::vector<SurfaceBoundaryTriangle> triangles; // Sorted original render triangle IDs.
};
struct SurfaceReceiver {
    ReceiverId id{};
    uint64_t generation{};
    Mat4 localToWorld{};
    std::vector<SurfaceTriangle> triangles;
    std::shared_ptr<const SurfaceBoundaryLineage> boundary;
};
struct DecalSurfaceSnapshot { std::vector<SurfaceReceiver> receivers; };
struct DecalSeed {
    ReceiverId receiver{};
    uint32_t triangle{};
    Vec3 barycentrics{1,0,0};
};
struct DecalBuildLimits {
    uint32_t maximumTriangles{16384},maximumInputTriangles{65536};
    double maximumStretch{1.25};
    double sampleSpacing{}; // Zero chooses footprint-relative spacing.
    double seamTolerance{}; // Zero uses min(1 mm, 0.1% of smaller footprint axis).
};
struct DecalBuildRequest {
    DecalSeed seed{};
    Mat4 frame{}; // Full world affine frame. X/Y columns span a one-metre square; translation is placement.
    DecalBuildLimits limits{};
};
enum class DecalPatchStatus { Ready,Incomplete,InvalidInput,MissingSeed,AmbiguousSeed,InvalidChart,LimitExceeded,Cancelled };
struct DecalPatchVertex {
    Vec3 position{},baseNormal{}; // World space, transformed exactly once from source geometry.
    Vec2 uv{};
    uint32_t sourceTriangle{};
    Vec3 barycentrics{};
};
struct DecalPatchChunk {
    ReceiverId receiver{};
    uint64_t generation{};
    std::vector<DecalPatchVertex> vertices;
    std::vector<uint32_t> indices;
};
struct DecalBuildStatistics {
    uint32_t inputTriangles{},analysisTriangles{},outputTriangles{},joinedEdges{},ambiguousEdges{};
    double topologyMilliseconds{},solveMilliseconds{},maximumStretch{},maximumSeamError{};
    bool usedDevelopableUnfolding{};
};
struct DecalPatch {
    DecalPatchStatus status{DecalPatchStatus::InvalidInput};
    std::string message;
    std::vector<DecalPatchChunk> chunks;
    DecalBuildStatistics statistics{};
    bool usable() const { return status==DecalPatchStatus::Ready || status==DecalPatchStatus::Incomplete; }
};

// Synchronous pure CPU preparation. Caller supplies an immutable, eligibility-filtered snapshot.
// No scene/material files, collision, GPU state or global workers are accessed.
DecalPatch buildDecalPatch(const DecalSurfaceSnapshot& snapshot,const DecalBuildRequest& request,std::stop_token stop={});
const char* decalPatchStatusName(DecalPatchStatus status);
}
