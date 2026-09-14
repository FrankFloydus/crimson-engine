////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/services/face_texture_edit_context.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <utility>

namespace quader {
namespace {

std::uint64_t next_selection_token() {
  // Tokens also distinguish replaced/recreated scene services with equal IDs.
  static std::atomic<std::uint64_t> next{1};
  return next.fetch_add(1, std::memory_order_relaxed);
}

std::size_t selected_face_count(
    const ModelingSelectedFaceTextureSnapshot &snapshot) {
  std::size_t count = 0;
  for (const auto &object : snapshot.objects) {
    count += object.faces.size();
  }
  return count;
}

ModelingCommandResult rejected(std::string message) {
  return {.ok = false, .changed = false, .message = std::move(message)};
}

} // namespace

bool FaceTextureEditContext::membership_matches(
    const ModelingSelectedFaceTextureSnapshot &snapshot) const {
  if (selected_face_count(snapshot) != state_.rotations.size()) {
    return false;
  }
  std::size_t index = 0;
  for (const auto &object : snapshot.objects) {
    for (const auto &face : object.faces) {
      const auto &baseline = state_.rotations[index++];
      if (baseline.modeling_object_id != object.modeling_object_id ||
          baseline.face != face.face) {
        return false;
      }
    }
  }
  return true;
}

void FaceTextureEditContext::reset_baselines(
    const ModelingSelectedFaceTextureSnapshot &snapshot,
    std::uint64_t selection_revision) {
  state_.rotations.clear();
  state_.rotations.reserve(selected_face_count(snapshot));
  for (const auto &object : snapshot.objects) {
    for (const auto &face : object.faces) {
      state_.rotations.push_back({
          .modeling_object_id = object.modeling_object_id,
          .face = face.face,
          .rotation_degrees = face.texture_transform.rotation_degrees,
      });
    }
  }
  state_.rotation_degrees = 0.0;
  selection_revision_ = selection_revision;
  selection_token_ = next_selection_token();
  initialized_ = true;
  ++revision_;
}

ModelingFaceTextureSettings FaceTextureEditContext::synchronize(
    const ModelingSelectedFaceTextureSnapshot &snapshot,
    std::uint64_t selection_revision, QVec2 texture_extent) {
  if (!initialized_ || selection_revision_ != selection_revision ||
      !membership_matches(snapshot)) {
    reset_baselines(snapshot, selection_revision);
  }

  ModelingFaceTextureSettings result{
      .selected_face_count = selected_face_count(snapshot),
      .editable = snapshot.command.ok && !snapshot.objects.empty(),
      .treat_as_one = treat_as_one_,
      .block_reason = snapshot.command.ok && !snapshot.objects.empty()
                          ? ModelingFaceTextureBlockReason::None
                          : ModelingFaceTextureBlockReason::NoSelection,
      .disabled_reason = snapshot.command.message,
      .rotation_degrees = state_.rotation_degrees,
      .selection_token = selection_token_,
  };
  bool first = true;
  for (const auto &object : snapshot.objects) {
    for (const auto &face : object.faces) {
      const auto &transform = face.texture_transform;
      if (first) {
        result.rotation_degrees = transform.rotation_degrees;
        result.scale_x = transform.scale.x;
        result.scale_y = transform.scale.y;
        result.shift_x = static_cast<double>(transform.shift_uv.x) *
                         texture_extent.x;
        result.shift_y = static_cast<double>(transform.shift_uv.y) *
                         texture_extent.y;
        first = false;
      } else {
        result.rotation_mixed = result.rotation_mixed ||
            result.rotation_degrees != transform.rotation_degrees;
        if (result.scale_x != transform.scale.x) {
          result.scale_x.reset();
        }
        if (result.scale_y != transform.scale.y) {
          result.scale_y.reset();
        }
      }
    }
  }
  return result;
}

ModelingCommandResult FaceTextureEditContext::prepare_edit(
    const ModelingFaceTextureEdit &edit,
    const ModelingSelectedFaceTextureSnapshot &snapshot, QVec2 texture_extent,
    std::vector<ModelingFaceTextureUpdateBatch> &batches) const {
  if (!initialized_ || edit.selection_token == 0 ||
      edit.selection_token != selection_token_ || !membership_matches(snapshot)) {
    return rejected("The face selection changed before the texture edit.");
  }
  if (!snapshot.command.ok || state_.rotations.empty()) {
    return rejected("Select one or more editable faces first.");
  }
  if (!std::isfinite(edit.value) ||
      std::abs(edit.value) > std::numeric_limits<float>::max()) {
    return rejected("Texture settings must be finite numbers.");
  }
  const float value = static_cast<float>(edit.value);
  if ((edit.field == ModelingFaceTextureField::ScaleX ||
       edit.field == ModelingFaceTextureField::ScaleY) &&
      value == 0.0f) {
    return rejected("Texture scale must not be zero.");
  }
  if ((edit.field == ModelingFaceTextureField::ShiftX ||
       edit.field == ModelingFaceTextureField::ShiftY) &&
      state_.rotations.size() != 1u) {
    return rejected("Texture shift requires exactly one selected face.");
  }

  batches.clear();
  batches.reserve(snapshot.objects.size());
  for (const auto &object : snapshot.objects) {
    ModelingFaceTextureUpdateBatch batch{
        .modeling_object_id = object.modeling_object_id,
    };
    batch.updates.reserve(object.faces.size());
    for (const auto &face : object.faces) {
      modeling::FaceTextureTransformUpdate update{.face = face.face};
      switch (edit.field) {
      case ModelingFaceTextureField::ScaleX:
        update.scale_x = value;
        break;
      case ModelingFaceTextureField::ScaleY:
        update.scale_y = value;
        break;
      case ModelingFaceTextureField::ShiftX:
        update.shift_x = value / texture_extent.x;
        break;
      case ModelingFaceTextureField::ShiftY:
        update.shift_y = value / texture_extent.y;
        break;
      case ModelingFaceTextureField::Rotate: {
        const double rotation = edit.value;
        if (!std::isfinite(rotation) ||
            std::abs(rotation) > std::numeric_limits<float>::max()) {
          return rejected("Texture rotation is outside the supported range.");
        }
        update.rotation_degrees = static_cast<float>(rotation);
        break;
      }
      default:
        return rejected("Unknown face texture setting.");
      }
      batch.updates.push_back(std::move(update));
    }
    batches.push_back(std::move(batch));
  }
  return {.ok = true, .changed = false};
}

ModelingFaceTextureEditState FaceTextureEditContext::capture() const {
  return state_;
}

void FaceTextureEditContext::record_applied_edit(
    const ModelingFaceTextureEdit &edit) {
  if (edit.field == ModelingFaceTextureField::Rotate &&
      edit.selection_token == selection_token_) {
    state_.rotation_degrees = edit.value;
  }
}

void FaceTextureEditContext::record_applied_rotation_delta(double degrees) {
  state_.rotation_degrees += degrees;
}

bool FaceTextureEditContext::set_treat_as_one(bool enabled) {
  if (treat_as_one_ == enabled) {
    return false;
  }
  treat_as_one_ = enabled;
  ++revision_;
  return true;
}

void FaceTextureEditContext::restore(
    const ModelingFaceTextureEditState &state,
    const ModelingSelectedFaceTextureSnapshot &snapshot,
    std::uint64_t selection_revision) {
  state_ = state;
  if (!membership_matches(snapshot)) {
    reset_baselines(snapshot, selection_revision);
    return;
  }
  selection_revision_ = selection_revision;
  selection_token_ = next_selection_token();
  initialized_ = true;
  ++revision_;
}

void FaceTextureEditContext::invalidate() {
  initialized_ = false;
  selection_token_ = next_selection_token();
  ++revision_;
}

} // namespace quader
