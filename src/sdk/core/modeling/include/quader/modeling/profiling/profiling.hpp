////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace quader::modeling {

/**
 * Enumerates measured SDK and host-adapter profiling scenarios.
 */
enum class ProfilingScenario {
  IdleLargeScene,
  OrbitCamera,
  DenseMeshHover,
  ComponentDragSingleObject,
  BoxPreview,
  MirrorPreview,
  OverlayWireframe,
};

/**
 * Enumerates profiled architecture stages where dirty gates matter.
 */
enum class ProfilingStage {
  RenderSnapshotBuild,
  RenderSync,
  ModelingMeshProjection,
  ObjectMeshCompilation,
  PickingGeometrySignature,
  PreviewMeshGeneration,
  OverlaySnapshotBuild,
};

/**
 * Stores one before-and-after profiling measurement.
 */
struct ProfilingMeasurement {
  ProfilingScenario scenario = ProfilingScenario::IdleLargeScene;
  ProfilingStage stage = ProfilingStage::RenderSnapshotBuild;
  std::uint64_t before_microseconds = 0;
  std::uint64_t after_microseconds = 0;
  std::string note;
};

/**
 * Stores the profiling measurements captured for SDK migration scenarios.
 */
struct ProfilingReport {
  std::vector<ProfilingMeasurement> measurements;
};

} // namespace quader::modeling
