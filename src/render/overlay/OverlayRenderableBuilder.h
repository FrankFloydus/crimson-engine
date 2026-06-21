#pragma once

#include "render/ViewportOverlayFrame.h"
#include "render/ViewportOverlayPolicy.h"
#include "render/overlay/OverlayMeshTypes.h"

#include <cstdint>
#include <vector>

namespace quader {

enum class OverlayDepthFunc {
    Always,
    Equal,
    GreaterOrEqual,
};

enum class OverlayCullingMode {
    None,
    Back,
};

enum class OverlayTransparencyMode {
    Default,
    TwoPassesOneSide,
    TwoPassesTwoSides,
};

struct OverlayRenderState {
    bool depthWrite = false;
    bool colorWrite = true;
    bool depthCulling = false;
    OverlayDepthFunc depthFunc = OverlayDepthFunc::Always;
    OverlayCullingMode cullingMode = OverlayCullingMode::None;
    OverlayTransparencyMode transparencyMode = OverlayTransparencyMode::Default;
    float polygonOffsetScale = 0.0f;
    float polygonOffsetConstant = 0.0f;
    std::uint16_t blendOrder = 0;
};

struct OverlayFaceFillBatch {
    OverlayFaceFillMesh mesh;
    OverlayRenderState state;
};

struct OverlayLineBatch {
    OverlayLineMesh mesh;
    OverlayRenderState state;
    float widthPixels = 1.0f;
    float depthBias = 0.0f;
};

struct OverlayPointBatch {
    OverlayPointMesh mesh;
    OverlayRenderState state;
    float sizePixels = 1.0f;
    float innerRadius = 0.0f;
    float depthBias = 0.0f;
};

struct OverlayLayerBuildPlan {
    std::vector<OverlayFaceFillBatch> faceFillBatches;
    std::vector<OverlayLineBatch> lineBatches;
    std::vector<OverlayPointBatch> pointBatches;
};

class OverlayRenderableBuilder {
public:
    static std::uint64_t frameSignature(const ViewportOverlayFrame& frame);
    static std::uint64_t cameraSignature(const ViewportOverlayCamera& camera);
    static std::uint64_t layerSignature(const ViewportOverlayFrame& frame, ViewportOverlayLayer layer);

    static OverlayRenderState makeState(
            ViewportOverlayLayer layer,
            ViewportOverlayGeometryKind kind,
            std::uint16_t blendOrder,
            bool depthStamp = false,
            bool faceColorPass = false);

    static OverlayLayerBuildPlan buildLayer(
            const ViewportOverlayFrame& frame,
            const ViewportOverlayCamera& camera,
            ViewportOverlayLayer layer);

private:
    struct LineBatchKey {
        std::uint32_t color = 0;
        float widthPixels = 0.0f;
    };

    static std::uint64_t hashCombine(std::uint64_t hash, std::uint64_t value);
    static std::uint64_t hashFloat(std::uint64_t hash, float value);
    static std::uint64_t hashVec3(std::uint64_t hash, Vec3 value);

    static std::vector<const ViewportOverlayLine*> linesForLayer(
            const ViewportOverlayFrame& frame,
            ViewportOverlayLayer layer);
    static std::vector<const ViewportOverlayPoint*> pointsForLayer(
            const ViewportOverlayFrame& frame,
            ViewportOverlayLayer layer);
    static std::vector<LineBatchKey> lineBatchKeys(
            const std::vector<const ViewportOverlayLine*>& lines,
            ViewportOverlayLayer layer);
    static void appendPointPass(
            OverlayLayerBuildPlan& plan,
            const std::vector<const ViewportOverlayPoint*>& points,
            const ViewportOverlayCamera& camera,
            ViewportOverlayLayer layer,
            std::uint16_t& blendOrder,
            bool outline);
};

} // namespace quader
