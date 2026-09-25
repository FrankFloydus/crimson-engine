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
#include <quader/modeling/mesh/face_texture_actions.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace quader {

/// Native editing intent; normalized UV conversion remains outside the SDK.
enum class ModelingFaceTextureField { ScaleX, ScaleY, ShiftX, ShiftY, Rotate };

using ModelingFaceTextureAction = modeling::FaceTextureAction;

[[nodiscard]] inline constexpr bool
modeling_face_texture_action_is_projection(ModelingFaceTextureAction action) {
  return action == ModelingFaceTextureAction::ProjectCube ||
         action == ModelingFaceTextureAction::ProjectCylinder ||
         action == ModelingFaceTextureAction::ProjectSphere;
}

/// Captures action settings once; repeat resolves the current targets afresh.
struct ModelingFaceTextureActionRequest final {
  modeling::FaceTextureActionOptions options;
  std::uint64_t selection_token = 0;
};

/// Host-resolved input to the portable selection-wide texture preparation.
struct ModelingFaceTextureActionTarget final {
  int modeling_object_id = -1;
  QMat4 world_from_local;
  QMat4 sampling_from_local;
  std::vector<modeling::FaceTextureActionFace> faces;
};

/// A validated face occurrence and its host-owned mapping frames. The portable
/// SDK resolves the UV chart and prepares the replacement without scene access.
struct ModelingFaceTextureTransferTarget final {
  int modeling_object_id = -1;
  modeling::FaceId face;
  QMat4 world_from_local;
  QMat4 sampling_from_local;
};

/// One canonical fallback label for native history and public command catalogs.
[[nodiscard]] inline std::string_view
modeling_face_texture_action_label(ModelingFaceTextureAction action) {
  switch (action) {
  case ModelingFaceTextureAction::AlignToGrid: return "Align Texture to Grid";
  case ModelingFaceTextureAction::AlignToFace: return "Align Texture to Face";
  case ModelingFaceTextureAction::AlignToView: return "Align Texture to View";
  case ModelingFaceTextureAction::ProjectCube: return "Cube UV Projection";
  case ModelingFaceTextureAction::ProjectCylinder: return "Cylinder UV Projection";
  case ModelingFaceTextureAction::ProjectSphere: return "Sphere UV Projection";
  case ModelingFaceTextureAction::ScaleXUp: return "Increase Texture Scale X";
  case ModelingFaceTextureAction::ScaleXDown: return "Decrease Texture Scale X";
  case ModelingFaceTextureAction::ScaleYUp: return "Increase Texture Scale Y";
  case ModelingFaceTextureAction::ScaleYDown: return "Decrease Texture Scale Y";
  case ModelingFaceTextureAction::ShiftLeft: return "Shift Texture Left";
  case ModelingFaceTextureAction::ShiftRight: return "Shift Texture Right";
  case ModelingFaceTextureAction::ShiftUp: return "Shift Texture Up";
  case ModelingFaceTextureAction::ShiftDown: return "Shift Texture Down";
  case ModelingFaceTextureAction::RotateClockwise: return "Rotate Texture Clockwise";
  case ModelingFaceTextureAction::RotateCounterClockwise:
    return "Rotate Texture Counterclockwise";
  case ModelingFaceTextureAction::FlipTextureHorizontal:
    return "Flip Texture Horizontally";
  case ModelingFaceTextureAction::FlipTextureVertical:
    return "Flip Texture Vertically";
  case ModelingFaceTextureAction::FlipFaceHorizontal:
    return "Mirror Face UVs Horizontally";
  case ModelingFaceTextureAction::FlipFaceVertical:
    return "Mirror Face UVs Vertically";
  case ModelingFaceTextureAction::FitBoth: return "Fit Texture Both Axes";
  case ModelingFaceTextureAction::FitX: return "Fit Texture X";
  case ModelingFaceTextureAction::FitY: return "Fit Texture Y";
  case ModelingFaceTextureAction::JustifyLeft: return "Justify Texture Left";
  case ModelingFaceTextureAction::JustifyRight: return "Justify Texture Right";
  case ModelingFaceTextureAction::JustifyTop: return "Justify Texture Top";
  case ModelingFaceTextureAction::JustifyBottom: return "Justify Texture Bottom";
  case ModelingFaceTextureAction::JustifyCenter: return "Justify Texture Center";
  }
  return {};
}

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
  bool treat_as_one = false;
  ModelingFaceTextureBlockReason block_reason =
      ModelingFaceTextureBlockReason::NoSelection;
  std::string disabled_reason;
  std::optional<double> scale_x;
  std::optional<double> scale_y;
  double shift_x = 0.0;
  double shift_y = 0.0;
  double rotation_degrees = 0.0;
  bool rotation_mixed = false;
  std::uint64_t selection_token = 0;
};

/// One scalar edit bound to the selection epoch that supplied its displayed value.
struct ModelingFaceTextureEdit final {
  ModelingFaceTextureField field = ModelingFaceTextureField::ScaleX;
  double value = 1.0;
  std::uint64_t selection_token = 0;
  bool preserve_scale_ratio = false;
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
