////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/project_material_document.hpp"
#include "assets/material_reference.hpp"
#include "editor/modeling/modeling_face_texture_settings.hpp"
#include <quader/modeling/mesh/face_texture_canvas.hpp>

namespace quader {

/// Host-resolved canvas data; the portable chart remains the geometry owner.
struct ModelingFaceTextureCanvas final {
  std::size_t selected_face_count = 0;
  bool editable = false;
  std::string disabled_reason;
  std::uint64_t selection_token = 0;
  ModelingFaceTextureTransferTarget target;
  modeling::FaceTextureCanvasChart chart;
  MaterialReference material;
  StandardMaterialTextureTransform material_transform;
  QVec2 texture_extent{64.0f, 64.0f};
  bool texture_available = false;
  double repeat_width = 0.0;
  double repeat_height = 0.0;
  double rotation_degrees = 0.0;
  modeling::FaceTextureTransform texture_transform;
  bool gesture_active = false;
};

/// An explicit numeric edit can retain scalar bookkeeping while the prepared
/// chart determines the effective mapping. Presentation never writes these fields.
struct ModelingFaceTextureCanvasEdit final {
  ModelingFaceTextureTransferTarget target;
  modeling::FaceTextureCanvasMap map;
  std::optional<modeling::FaceTextureTransformUpdate> scalar_update;
  bool apply_mapping = true;
  QVec2 texture_extent{64.0f, 64.0f};
};

/// Names the semantic mapping operation performed by one retained canvas gesture.
enum class ModelingFaceTextureGestureKind {
  Translate,
  ScaleWidth,
  ScaleHeight,
  Rotate,
};

/// Pins the initial pointer and pivot to the face selection that supplied the chart.
struct ModelingFaceTextureGestureBegin final {
  ModelingFaceTextureGestureKind kind = ModelingFaceTextureGestureKind::Translate;
  std::uint64_t selection_token = 0;
  QVec2 start;
  QVec2 pivot;
};

/// Carries an absolute pointer position and current precision/grid snap preferences.
struct ModelingFaceTextureGestureUpdate final {
  QVec2 current;
  bool precision = false;
  bool snap = false;
  double grid_step = 1.0;
  double rotation_step = 15.0;
};

/// Pure gesture arithmetic. Mutable mesh/history state remains in the session.
[[nodiscard]] modeling::Result<modeling::FaceTextureCanvasMap>
prepare_face_texture_gesture_map(
    const ModelingFaceTextureCanvas &baseline,
    const ModelingFaceTextureGestureBegin &begin,
    const ModelingFaceTextureGestureUpdate &update);

/// Composes the Standard Material shader transform or its inverse with a chart.
[[nodiscard]] modeling::FaceTextureCanvasMap transform_face_texture_canvas_map(
    const modeling::FaceTextureCanvasMap &map,
    const StandardMaterialTextureTransform &transform, bool inverse = false);
void measure_face_texture_canvas(ModelingFaceTextureCanvas &canvas);

} // namespace quader
