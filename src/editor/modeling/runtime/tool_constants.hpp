////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <cstddef>

namespace quader_editor_core {

inline constexpr double kToolEpsilon = 0.000001;
inline constexpr double kScreenEpsilon = 0.0001;
inline constexpr double kMovePixelsPerGridStep = 48.0;
inline constexpr double kScalePixelsPerFactor = 96.0;
inline constexpr double kMinScaleFactor = 0.01;
inline constexpr double kMinExtrudeDistance = 0.001;
inline constexpr double kExtrudePixelsPerGridStep = 48.0;
inline constexpr int kExtrudeCapFaceIndex = 1;
inline constexpr double kMinBoxSize = 0.001;
inline constexpr double kBoxHeightPixelsPerGridStep = 48.0;
inline constexpr double kNavigationMinCameraDistance = 0.05;
inline constexpr double kNavigationDefaultFrameDistance = 8.0;
inline constexpr double kNavigationZoomStepFactor = 1.18;
inline constexpr double kNavigationMinOrthographicSize = 0.01;
inline constexpr double kNavigationMaxOrthographicSize = 4096.0;
inline constexpr double kNavigationMaxOrthographicZoomOutSize = 1200.0;
inline constexpr double kNavigationDefaultFovDegrees = 70.0;
inline constexpr double kNavigationOrbitRadiansPerPixel = 0.006;
inline constexpr double kNavigationFlyLookRadiansPerPixel = 0.004;
inline constexpr double kNavigationFlyBaseSpeed = 6.0;
inline constexpr double kNavigationFlyFastMultiplier = 4.0;
inline constexpr double kNavigationFlySlowMultiplier = 0.25;
inline constexpr double kNavigationFlyMaxDeltaSeconds = 0.25;
inline constexpr double kNavigationOrthogonalSnapDot = 0.965925826289068;
inline constexpr double kViewCubeMinSize = 48.0;
inline constexpr double kViewCubeMaxSize = 128.0;
inline constexpr double kViewCubeHalfFactor = 0.23;
inline constexpr double kViewCubeAxisFactor = 0.45;
inline constexpr double kViewCubeFaceVisibilityEpsilon = 0.0001;
inline constexpr double kSelectionBoxDefaultDragThreshold = 4.0;
inline constexpr double kTransformGizmoTau = 6.28318530717958647692;
inline constexpr double kTransformGizmoAxisHideScore = 0.02;
inline constexpr double kTransformGizmoAxisFullScore = 0.1;
inline constexpr float kPickCycleDefaultMaxScreenDistance = 4.0F;
inline constexpr float kDefaultMapUnitsPerWorldUnit = 32.0F;
inline constexpr float kDefaultSceneFootprintMapUnits = 128.0F;
inline constexpr float kDefaultSceneHeightMapUnits = 32.0F;
inline constexpr std::size_t kEditorHistoryDefaultMaxDepth = 128;

} // namespace quader_editor_core
