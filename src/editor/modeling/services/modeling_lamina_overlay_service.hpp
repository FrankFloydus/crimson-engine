////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/runtime/modeling_object_runtime_map.hpp"

#include <cstdint>
#include <memory>

namespace quader {

class ModelingSelectionProjectionSnapshot;
class ModelingSelectionService;
class ModelingSdkProjectionAdapter;
class SceneDocument;
struct ViewportOverlayFrame;

/// Measures retained lamina classification independently of selection and cameras.
struct ModelingLaminaOverlayStats final {
  std::uint64_t build_count = 0u;
  std::uint64_t local_geometry_build_count = 0u;
  std::uint64_t world_face_build_count = 0u;
  std::uint64_t preview_extraction_count = 0u;
  std::uint64_t broadphase_comparison_count = 0u;
  std::uint64_t candidate_pair_count = 0u;
};

/// Derives automatic coplanar face diagnostics from the accepted modeling view.
class ModelingLaminaOverlayService final {
public:
  ModelingLaminaOverlayService();
  ~ModelingLaminaOverlayService();
  ModelingLaminaOverlayService(const ModelingLaminaOverlayService &) = delete;
  ModelingLaminaOverlayService &operator=(const ModelingLaminaOverlayService &) = delete;

  [[nodiscard]] const ViewportOverlayFrame &update(
      const ModelingSelectionProjectionSnapshot &snapshot,
      const SceneDocument &scene, const ModelingObjectRuntimeMap &object_map,
      const ModelingSelectionService &selection,
      const ModelingSdkProjectionAdapter &projection);
  [[nodiscard]] ModelingLaminaOverlayStats stats() const;
  [[nodiscard]] std::uint64_t build_count() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace quader
