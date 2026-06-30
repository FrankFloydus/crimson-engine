////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/viewport_overlay_frame.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "render/overlay/overlay_mesh_types.hpp"

#include <cstdint>
#include <vector>

namespace quader {

class OverlayDepthStampVisibilityFilter;

/**
 * Enumerates OverlayDepthFunc values used by the render layer.
 */
enum class OverlayDepthFunc {
    Always,
    Equal,
    GreaterOrEqual,
};

/**
 * Enumerates OverlayCullingMode values used by the render layer.
 */
enum class OverlayCullingMode {
    None,
    Back,
};

/**
 * Enumerates OverlayTransparencyMode values used by the render layer.
 */
enum class OverlayTransparencyMode {
    Default,
    TwoPassesOneSide,
    TwoPassesTwoSides,
};

/**
 * Stores the Overlay Render State data contract used by the render extraction and Filament resource layer.
 */
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

/**
 * Represents an Overlay Face Fill Batch value used by the render extraction and Filament resource layer.
 */
struct OverlayFaceFillBatch {
    OverlayFaceFillMesh mesh;
    OverlayRenderState state;
};

/**
 * Represents an Overlay Line Batch value used by the render extraction and Filament resource layer.
 */
struct OverlayLineBatch {
    OverlayLineMesh mesh;
    OverlayRenderState state;
    float widthPixels = 1.0f;
    float depthBias = 0.0f;
};

/**
 * Represents an Overlay Point Batch value used by the render extraction and Filament resource layer.
 */
struct OverlayPointBatch {
    OverlayPointMesh mesh;
    OverlayRenderState state;
    float sizePixels = 1.0f;
    float innerRadius = 0.0f;
    float depthBias = 0.0f;
};

/**
 * Represents an Overlay Layer Build Plan value used by the render extraction and Filament resource layer.
 */
struct OverlayLayerBuildPlan {
    std::vector<OverlayFaceFillBatch> faceFillBatches;
    std::vector<OverlayLineBatch> lineBatches;
    std::vector<OverlayPointBatch> pointBatches;
};

/**
 * Builds Overlay Renderable Builder data for the render extraction and Filament resource layer.
 */
class OverlayRenderableBuilder {
public:
    static std::uint64_t frameSignature(const ViewportOverlayFrame& frame);
    static std::uint64_t cameraSignature(const ViewportOverlayCamera& camera);
    static std::uint64_t layerSignature(const ViewportOverlayFrame& frame, ViewportOverlayLayer layer);
    static std::uint64_t layerBuildSignature(
            const ViewportOverlayFrame& frame,
            const ViewportOverlayCamera& camera,
            ViewportOverlayLayer layer);
    static bool layerBuildDependsOnCamera(ViewportOverlayLayer layer);

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
    /**
     * Stores the Line Batch Key data contract used by the render extraction and Filament resource layer.
     */
    struct LineBatchKey {
        std::uint32_t color = 0;
        float widthPixels = 0.0f;
    };

    static std::vector<const ViewportOverlayLine*> linesForLayer(
            const ViewportOverlayFrame& frame,
            ViewportOverlayLayer layer,
            const ViewportOverlayCamera& camera,
            const OverlayDepthStampVisibilityFilter* visibility);
    static std::vector<const ViewportOverlayPoint*> pointsForLayer(
            const ViewportOverlayFrame& frame,
            ViewportOverlayLayer layer,
            const ViewportOverlayCamera& camera,
            const OverlayDepthStampVisibilityFilter* visibility);
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
