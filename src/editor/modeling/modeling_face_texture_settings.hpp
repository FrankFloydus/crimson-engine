////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "editor/modeling/modeling_result_types.hpp"
#include "math/types.hpp"

#include <quader/modeling/payloads.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace quader {

/// Native editing intent; normalized UV conversion remains outside the SDK.
enum class ModelingFaceTextureField { ScaleX, ScaleY, ShiftX, ShiftY, Rotate };

/// Stable causes that let presentation explain why selected face edits are blocked.
enum class ModelingFaceTextureBlockReason {
  None,
  NoSelection,
  PreviewActive,
  TargetUnavailable,
};

/// One narrow SDK projection of an object's selected face properties.
struct ModelingSelectedFaceTextureObject final {
  int modeling_object_id = -1;
  EntityId entity = kInvalidEntityId;
  std::vector<modeling::FaceTextureSettings> faces;
};

/// Owns selected properties only, never a copied mesh or authored topology.
struct ModelingSelectedFaceTextureSnapshot final {
  ModelingCommandResult command{.ok = true};
  std::vector<ModelingSelectedFaceTextureObject> objects;
};

/// Carries an already resolved atomic texture edit for one SDK object.
struct ModelingFaceTextureUpdateBatch final {
  int modeling_object_id = -1;
  std::vector<modeling::FaceTextureTransformUpdate> updates;
};

/// Native selected-face property projection shared by API and history owners.
struct ModelingFaceTextureSettings final {
  std::size_t selected_face_count = 0;
  bool editable = false;
  ModelingFaceTextureBlockReason block_reason =
      ModelingFaceTextureBlockReason::NoSelection;
  std::string disabled_reason;
  std::optional<double> scale_x;
  std::optional<double> scale_y;
  double shift_x = 0.0;
  double shift_y = 0.0;
  double rotation_degrees = 0.0;
  std::uint64_t selection_token = 0;
};

/// One scalar edit bound to the selection epoch that supplied its displayed value.
struct ModelingFaceTextureEdit final {
  ModelingFaceTextureField field = ModelingFaceTextureField::ScaleX;
  double value = 1.0;
  std::uint64_t selection_token = 0;
};

/// The minimal retained rotation baseline; it is not another UV source of truth.
struct ModelingFaceTextureRotationBaseline final {
  int modeling_object_id = -1;
  modeling::FaceId face;
  float rotation_degrees = 0.0f;
};

/// Command-owned presentation restore data, independent of mesh undo payloads.
struct ModelingFaceTextureEditState final {
  std::vector<ModelingFaceTextureRotationBaseline> rotations;
  double rotation_degrees = 0.0;
};

} // namespace quader
