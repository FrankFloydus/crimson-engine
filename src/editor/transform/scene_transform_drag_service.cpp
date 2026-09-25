////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/transform/scene_transform_drag_service.hpp"

#include "api/quader_editor_api.hpp"
#include "api/quader_api_type_conversions.hpp"
#include "api/quader_scene_api.hpp"
#include "api/quader_preferences_api.hpp"
#include "assets/asset_registry.hpp"
#include "editor/commands/document_command_service.hpp"
#include "editor/commands/document/document_command_edits.hpp"
#include "editor/history/editor_history_service.hpp"
#include "math/glm_bridge.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_transform_pivot.hpp"
#include "scene/scene_transform.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace quader {
namespace {

bool entity_selected(const SceneDocument &document, EntityId entity) {
  const SelectionComponent *selection = document.selection(entity);
  return selection != nullptr && selection->selected;
}

bool has_selected_ancestor(const SceneDocument &document, EntityId entity) {
  EntityId parent = document.parent(entity);
  while (parent != kInvalidEntityId) {
    if (entity_selected(document, parent)) {
      return true;
    }
    parent = document.parent(parent);
  }
  return false;
}

QVec3 component_scale_ratio(QVec3 next, QVec3 previous) {
  auto ratio = [](float next_value, float previous_value) {
    const float denominator =
        std::abs(previous_value) <= 0.000001f ? 1.0f : previous_value;
    return next_value / denominator;
  };
  return {ratio(next.x, previous.x), ratio(next.y, previous.y),
          ratio(next.z, previous.z)};
}

QVec3 multiply_components(QVec3 left, QVec3 right) {
  return {left.x * right.x, left.y * right.y, left.z * right.z};
}

QVec3 euler_radians_after_local_rotation_delta(
    QVec3 current_euler_radians, QVec3 local_rotation_delta_radians) {
  const glm::quat current =
      to_glm(quat_from_euler_radians(current_euler_radians));
  const glm::quat delta =
      to_glm(quat_from_euler_radians(local_rotation_delta_radians));
  return euler_radians_from_rotation_closest_to(current * delta,
                                               current_euler_radians);
}

QVec3 clamp_scale(QVec3 scale) {
  return {
      std::max(scale.x, 0.001f),
      std::max(scale.y, 0.001f),
      std::max(scale.z, 0.001f),
  };
}

QVec3 selected_transform_pivot(const SceneDocument &document,
                               const AssetRegistry &assets, EntityId primary,
                               const TransformComponent &primary_transform) {
  if (const std::optional<QVec3> pivot =
          resolve_scene_selection_visible_pivot(document, assets)) {
    return *pivot;
  }

  if (document.exists(primary)) {
    return transform_point(document.world_transform_matrix(primary), {});
  }
  return primary_transform.position;
}

/// Converts a world-space move delta into one entity's compensated channel frame.
struct ParentTranslationBasis {
  QMat4 parent_from_world;
  QVec3 world_x = {1.0f, 0.0f, 0.0f};
  QVec3 world_y = {0.0f, 1.0f, 0.0f};
  QVec3 world_z = {0.0f, 0.0f, 1.0f};
  bool valid = true;
};

bool finite_vector(QVec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

ParentTranslationBasis parent_translation_basis(
    const QMat4 &display_parent_world) {
  const std::optional<QMat4> parent_from_world =
      try_scene_matrix_inverse(display_parent_world);
  if (!parent_from_world.has_value()) {
    return {.valid = false};
  }
  ParentTranslationBasis basis{
      .parent_from_world = *parent_from_world,
      .world_x = transform_vector(*parent_from_world, {1.0f, 0.0f, 0.0f}),
      .world_y = transform_vector(*parent_from_world, {0.0f, 1.0f, 0.0f}),
      .world_z = transform_vector(*parent_from_world, {0.0f, 0.0f, 1.0f}),
  };
  basis.valid = finite_vector(basis.world_x) && finite_vector(basis.world_y) &&
                finite_vector(basis.world_z);
  return basis;
}

QVec3 parent_space_translation(const SceneTransformDragTarget &target,
                               QVec3 world_delta) {
  return target.parent_space_world_x * world_delta.x +
         target.parent_space_world_y * world_delta.y +
         target.parent_space_world_z * world_delta.z;
}

QMat4 world_translation_matrix(QVec3 world_delta) {
  QMat4 transform;
  transform.values[12] = world_delta.x;
  transform.values[13] = world_delta.y;
  transform.values[14] = world_delta.z;
  return transform;
}

std::optional<TransformComponent> transformed_target_from_world_delta(
    const SceneDocument &document, const SceneTransformDragTarget &target,
    const QMat4 &world_transform_delta) {
  const QMat4 preview_world_transform =
      scene_transform_multiply(world_transform_delta, target.world_transform);
  const QMat4 preview_scene_transform = scene_transform_multiply(
      target.scene_from_display, preview_world_transform);
  const EntityId parent = document.parent(target.entity);
  const QMat4 parent_world_transform =
      parent == kInvalidEntityId ? scene_transform_identity_matrix()
                                 : document.world_transform_matrix(parent);
  std::optional<TransformComponent> transformed =
      try_scene_local_transform_preserving_world(
          parent_world_transform, preview_scene_transform, target.transform);
  const std::optional<QMat4> parent_from_world =
      try_scene_matrix_inverse(scene_compensated_parent_matrix(
          parent_world_transform, target.transform));
  if (!transformed.has_value() || !parent_from_world.has_value()) {
    return std::nullopt;
  }
  // Decomposition may move a pair of scale signs into a half-turn. Preserve
  // the authored signs when handedness is unchanged, so zeroing rotation still
  // restores the same local mesh basis after rotating a mirrored object.
  const auto scale_sign_adjustment = [](float next, float captured) {
    return std::signbit(next) == std::signbit(captured) ? 1.0f : -1.0f;
  };
  const QVec3 sign_adjustment{
      scale_sign_adjustment(transformed->scale.x, target.transform.scale.x),
      scale_sign_adjustment(transformed->scale.y, target.transform.scale.y),
      scale_sign_adjustment(transformed->scale.z, target.transform.scale.z),
  };
  if (sign_adjustment.x * sign_adjustment.y * sign_adjustment.z > 0.0f &&
      (sign_adjustment.x < 0.0f || sign_adjustment.y < 0.0f ||
       sign_adjustment.z < 0.0f)) {
    glm::dmat3 rotation = glm::mat3_cast(glm::dquat{
        to_glm(quat_from_euler_radians(transformed->rotation_euler))});
    rotation[0] *= static_cast<double>(sign_adjustment.x);
    rotation[1] *= static_cast<double>(sign_adjustment.y);
    rotation[2] *= static_cast<double>(sign_adjustment.z);
    transformed->rotation_euler = euler_radians_from_rotation_closest_to(
        glm::quat_cast(rotation), target.transform.rotation_euler);
    transformed->scale =
        multiply_components(transformed->scale, sign_adjustment);
  }
  // A TRS decomposition can discard shear. Never publish that lossy result as
  // though it were the requested gizmo transform.
  const QMat4 requested_local_transform = scene_transform_multiply(
      *parent_from_world, preview_scene_transform);
  const QMat4 represented_local_transform =
      compose_transform(transformed->spatial_transform());
  if (!scene_matrix_near_equal_normalized_basis(represented_local_transform,
                                                requested_local_transform)) {
    return std::nullopt;
  }
  return transformed;
}

void pin_target_local_pivot_to_world_point(
    const SceneDocument &document, const SceneTransformDragTarget &target,
    TransformComponent &transform, QVec3 local_pivot, QVec3 world_pivot) {
  QTransform spatial = transform.spatial_transform();
  spatial.position = {};
  const EntityId parent = document.parent(target.entity);
  const QMat4 parent_world = parent == kInvalidEntityId
                                 ? scene_transform_identity_matrix()
                                 : document.world_transform_matrix(parent);
  const QVec3 scene_pivot =
      transform_point(target.scene_from_display, world_pivot);
  const QVec3 parent_space_pivot =
      transform_point(inverse(scene_compensated_parent_matrix(parent_world,
                                                              transform)),
                      scene_pivot);
  transform.position =
      parent_space_pivot -
      transform_point(compose_transform(spatial), local_pivot);
}

template <typename BuildTransform>
std::vector<DocumentTransformEdit>
build_target_transform_edits(const SceneDocument &document,
                             std::span<const SceneTransformDragTarget> targets,
                             BuildTransform build_transform) {
  std::vector<DocumentTransformEdit> edits;
  edits.reserve(targets.size());
  for (const SceneTransformDragTarget &target : targets) {
    if (!document.exists(target.entity) ||
        document.is_effectively_locked(target.entity)) {
      continue;
    }
    edits.push_back(DocumentTransformEdit{
        .entity = target.entity,
        .before = target.transform,
        .after = build_transform(target),
    });
  }
  return edits;
}

std::vector<DocumentTransformEdit> build_primary_rotation_edits(
    const SceneDocument &document,
    std::span<const SceneTransformDragTarget> targets, EntityId primary,
    QVec3 captured_primary_euler, QVec3 requested_primary_euler,
    QVec3 world_pivot) {
  const glm::quat previous =
      to_glm(quat_from_euler_radians(captured_primary_euler));
  const glm::quat requested =
      to_glm(quat_from_euler_radians(requested_primary_euler));
  const glm::quat delta = glm::normalize(requested * glm::conjugate(previous));
  return build_target_transform_edits(
      document, targets, [&](const SceneTransformDragTarget &target) {
        TransformComponent next = target.transform;
        // Numeric fields author an absolute local orientation. Selected peers
        // follow its relative rotation in their own parent-coordinate frame.
        if (target.entity == primary) {
          next.rotation_euler = requested_primary_euler;
        } else {
          const glm::quat current =
              to_glm(quat_from_euler_radians(target.transform.rotation_euler));
          next.rotation_euler = euler_radians_from_rotation_closest_to(
              delta * current, target.transform.rotation_euler);
        }
        pin_target_local_pivot_to_world_point(
            document, target, next, target.local_pivot, world_pivot);
        return next;
      });
}

QuaderApiVoidResult unchanged_transform_api_result(QuaderEditorApi &editor,
                                                   const std::string &label) {
  return {
      .ok = true,
      .changed = false,
      .message = label,
      .mutation_kind = QuaderMutationKind::None,
      .revisions = editor.revisions(),
  };
}

QuaderApiVoidResult invalid_transform_api_result(QuaderEditorApi &editor,
                                                 std::string message) {
  return {
      .ok = false,
      .changed = false,
      .message = std::move(message),
      .mutation_kind = QuaderMutationKind::None,
      .revisions = editor.revisions(),
  };
}

} // namespace

SceneTransformDragService::SceneTransformDragService() = default;

SceneTransformDragService::~SceneTransformDragService() = default;

SceneTransformDragService::SceneTransformDragService(SceneTransformDragService&& other) noexcept
        : preview_(std::move(other.preview_))
        , targets_(std::move(other.targets_))
        , label_(std::move(other.label_)) {
    other.preview_.reset();
}

SceneTransformDragService& SceneTransformDragService::operator=(SceneTransformDragService&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    reset();
    targets_ = std::move(other.targets_);
    label_ = std::move(other.label_);
    if (other.preview_) {
        preview_.emplace(std::move(*other.preview_));
        other.preview_.reset();
    }
    return *this;
}

bool SceneTransformDragService::begin(SceneDocument &document,
                                      QVec3 world_pivot, bool start_preview,
                                      std::string label,
                                      QMat4 display_from_scene,
                                      const AssetRegistry *visible_pivot_assets,
                                      TextureLockSettings texture_locks) {
  reset();
  label_ = std::move(label);
  world_pivot_ = world_pivot;

  if (capture_targets(document, world_pivot, display_from_scene,
                      visible_pivot_assets) !=
      CaptureTargetsStatus::Captured) {
    reset();
    return false;
  }

  if (start_preview) {
    std::vector<EntityId> preview_entities;
    preview_entities.reserve(targets_.size());
    for (const SceneTransformDragTarget &target : targets_) {
      preview_entities.push_back(target.entity);
    }
    preview_.emplace(document, std::move(preview_entities), label_, texture_locks);
    if (preview_->empty()) {
      reset();
      return false;
    }
  }
  return true;
}

bool SceneTransformDragService::begin_move_selected_to_primary_position_preview(
    SceneDocument &document, EntityId primary, std::string label,
    TextureLockSettings texture_locks) {
  if (document.transform(primary) == nullptr) {
    reset();
    return false;
  }
  return begin(document, {}, true, std::move(label), {}, nullptr, texture_locks);
}

bool SceneTransformDragService::begin_rotate_selected_to_primary_euler_preview(
    SceneDocument &document, const AssetRegistry &assets, EntityId primary,
    std::string label, TextureLockSettings texture_locks) {
  const TransformComponent *primary_transform = document.transform(primary);
  if (primary_transform == nullptr) {
    reset();
    return false;
  }
  return begin(document,
               selected_transform_pivot(document, assets, primary,
                                        *primary_transform),
               true, std::move(label), {}, nullptr, texture_locks);
}

bool SceneTransformDragService::begin_scale_selected_to_primary_scale_preview(
    SceneDocument &document, const AssetRegistry &assets, EntityId primary,
    std::string label, TextureLockSettings texture_locks) {
  const TransformComponent *primary_transform = document.transform(primary);
  if (primary_transform == nullptr) {
    reset();
    return false;
  }
  return begin(document,
               selected_transform_pivot(document, assets, primary,
                                        *primary_transform),
               true, std::move(label), {}, nullptr, texture_locks);
}

bool SceneTransformDragService::preview_active() const {
  return preview_.has_value() && preview_->active();
}

const std::vector<SceneTransformDragTarget>& SceneTransformDragService::targets() const {
    return targets_;
}

DocumentCommandResult
SceneTransformDragService::preview_transform(EntityId entity,
                                             TransformComponent transform) {
  if (!preview_) {
    return document_command_invalid_request_result(
        "Transform preview is not active");
  }
  return preview_->preview_transform(entity, transform);
}

DocumentCommandResult
SceneTransformDragService::preview_selected_to_primary_position(
    SceneDocument &document, EntityId primary, QVec3 next_primary_position) {
  if (!preview_) {
    return document_command_invalid_request_result(
        "Transform preview is not active");
  }
  const TransformComponent *primary_transform =
      captured_primary_transform(document, primary);
  if (primary_transform == nullptr) {
    return document_command_missing_entity_result();
  }

  const QVec3 delta = next_primary_position - primary_transform->position;
  std::vector<DocumentTransformEdit> edits = build_target_transform_edits(
      document, targets_, [&](const SceneTransformDragTarget &target) {
        TransformComponent next = target.transform;
        next.position = next.position + delta;
        return next;
      });
  return preview_->preview_transforms(std::move(edits), SceneTextureTransformKind::Translation);
}

DocumentCommandResult
SceneTransformDragService::preview_selected_to_primary_euler(
    SceneDocument &document, EntityId primary, QVec3 next_primary_euler_radians) {
  if (!preview_) {
    return document_command_invalid_request_result(
        "Transform preview is not active");
  }
  const TransformComponent *primary_transform =
      captured_primary_transform(document, primary);
  if (primary_transform == nullptr) {
    return document_command_missing_entity_result();
  }

  if (!finite_vector(next_primary_euler_radians)) {
    return document_command_invalid_request_result("Rotation must be finite");
  }
  std::vector<DocumentTransformEdit> edits = build_primary_rotation_edits(
      document, targets_, primary, primary_transform->rotation_euler,
      next_primary_euler_radians, world_pivot_);
  return preview_->preview_transforms(std::move(edits), SceneTextureTransformKind::Rotation);
}

DocumentCommandResult
SceneTransformDragService::preview_selected_to_primary_scale(
    SceneDocument &document, EntityId primary, QVec3 next_primary_scale) {
  if (!preview_) {
    return document_command_invalid_request_result(
        "Transform preview is not active");
  }
  const TransformComponent *primary_transform =
      captured_primary_transform(document, primary);
  if (primary_transform == nullptr) {
    return document_command_missing_entity_result();
  }

  const QVec3 ratio =
      component_scale_ratio(next_primary_scale, primary_transform->scale);
  std::vector<DocumentTransformEdit> edits = build_target_transform_edits(
      document, targets_, [&](const SceneTransformDragTarget &target) {
        TransformComponent next = target.transform;
        next.scale = clamp_scale(multiply_components(next.scale, ratio));
        pin_target_local_pivot_to_world_point(
            document, target, next, target.local_pivot, world_pivot_);
        return next;
      });
  return preview_->preview_transforms(std::move(edits), SceneTextureTransformKind::Scale);
}

DocumentCommandResult SceneTransformDragService::preview_world_transform_delta(
    SceneDocument &document, const QMat4 &world_transform_delta,
    SceneTextureTransformKind kind) {
  if (!preview_) {
    return document_command_invalid_request_result(
        "Transform preview is not active");
  }

  std::vector<DocumentTransformEdit> preview_edits;
  preview_edits.reserve(targets_.size());
  for (const SceneTransformDragTarget &target : targets_) {
    if (!document.exists(target.entity)) {
      continue;
    }
    const std::optional<TransformComponent> transformed =
        transformed_target_from_world_delta(document, target,
                                            world_transform_delta);
    if (!transformed.has_value()) {
      return document_command_invalid_request_result(
          "World transform cannot be represented without shear");
    }
    preview_edits.push_back(DocumentTransformEdit{
        .entity = target.entity,
        .before = target.transform,
        .after = *transformed,
    });
  }
  return preview_->preview_transforms(std::move(preview_edits), kind);
}

DocumentCommandResult
SceneTransformDragService::preview_world_translation_delta(
    SceneDocument &document, QVec3 world_translation_delta) {
  if (!preview_) {
    return document_command_invalid_request_result(
        "Transform preview is not active");
  }

  std::vector<DocumentTransformEdit> preview_edits;
  preview_edits.reserve(targets_.size());
  std::optional<QMat4> fallback_world_transform;
  for (const SceneTransformDragTarget &target : targets_) {
    if (!document.exists(target.entity)) {
      continue;
    }
    TransformComponent preview_transform;
    if (target.parent_translation_basis_valid) {
      preview_transform = target.translation_preview_transform;
      preview_transform.position =
          target.translation_preview_transform.position +
          parent_space_translation(target, world_translation_delta);
    } else {
      if (!fallback_world_transform.has_value()) {
        fallback_world_transform =
            world_translation_matrix(world_translation_delta);
      }
      const std::optional<TransformComponent> transformed =
          transformed_target_from_world_delta(document, target,
                                              *fallback_world_transform);
      if (!transformed.has_value()) {
        return document_command_invalid_request_result(
            "World translation cannot be represented by the target transform");
      }
      preview_transform = *transformed;
    }
    preview_edits.push_back(DocumentTransformEdit{
        .entity = target.entity,
        .before = target.transform,
        .after = preview_transform,
    });
  }
  return preview_->preview_transforms(std::move(preview_edits), SceneTextureTransformKind::Translation);
}

DocumentCommandResult SceneTransformDragService::preview_local_rotation_delta(
    SceneDocument &document, QVec3 local_rotation_delta_radians) {
  if (!preview_) {
    return document_command_invalid_request_result(
        "Transform preview is not active");
  }
  if (!finite_vector(local_rotation_delta_radians)) {
    return document_command_invalid_request_result(
        "Local rotation delta must be finite");
  }

  std::vector<DocumentTransformEdit> preview_edits;
  preview_edits.reserve(targets_.size());
  for (const SceneTransformDragTarget &target : targets_) {
    if (!document.exists(target.entity) ||
        document.is_effectively_locked(target.entity)) {
      continue;
    }
    TransformComponent next = target.transform;
    next.rotation_euler = euler_radians_after_local_rotation_delta(
        target.transform.rotation_euler, local_rotation_delta_radians);
    pin_target_local_pivot_to_world_point(
        document, target, next, target.local_visible_pivot,
        target.display_visible_pivot);
    if (!scene_transform_is_finite(next)) {
      return document_command_invalid_request_result(
          "Local rotation cannot be represented by the target transform");
    }
    preview_edits.push_back(DocumentTransformEdit{
        .entity = target.entity,
        .before = target.transform,
        .after = next,
    });
  }
  return preview_->preview_transforms(std::move(preview_edits), SceneTextureTransformKind::Rotation);
}

DocumentCommandResult SceneTransformDragService::preview_local_scale_delta(
    SceneDocument &document, QVec3 local_scale_factors) {
  if (!preview_) {
    return document_command_invalid_request_result(
        "Transform preview is not active");
  }
  if (!finite_vector(local_scale_factors) || local_scale_factors.x <= 0.0f ||
      local_scale_factors.y <= 0.0f || local_scale_factors.z <= 0.0f) {
    return document_command_invalid_request_result(
        "Local scale factors must be finite and positive");
  }

  std::vector<DocumentTransformEdit> preview_edits;
  preview_edits.reserve(targets_.size());
  for (const SceneTransformDragTarget &target : targets_) {
    if (!document.exists(target.entity) ||
        document.is_effectively_locked(target.entity)) {
      continue;
    }
    TransformComponent next = target.transform;
    next.scale = multiply_components(target.transform.scale,
                                     local_scale_factors);
    pin_target_local_pivot_to_world_point(
        document, target, next, target.local_visible_pivot,
        target.display_visible_pivot);
    if (!scene_transform_is_finite(next)) {
      return document_command_invalid_request_result(
          "Local scale cannot be represented by the target transform");
    }
    preview_edits.push_back(DocumentTransformEdit{
        .entity = target.entity,
        .before = target.transform,
        .after = next,
    });
  }
  return preview_->preview_transforms(std::move(preview_edits), SceneTextureTransformKind::Scale);
}

DocumentCommandResult SceneTransformDragService::accept() {
    if (!preview_) {
        targets_.clear();
        return document_command_invalid_request_result(
            "Transform preview is not active");
    }

    DocumentCommandResult result = preview_->accept();
    reset();
    return result;
}

DocumentCommandResult SceneTransformDragService::commit(EditorHistoryService& history) {
    if (!preview_) {
        targets_.clear();
        return document_command_invalid_request_result(
            "Transform preview is not active");
    }

    DocumentCommandResult result = preview_->commit(history);
    reset();
    return result;
}

QuaderApiVoidResult SceneTransformDragService::commit(QuaderEditorApi& editor) {
    if (!preview_) {
      return {
          .ok = false,
          .changed = false,
          .message = "Transform preview is not active",
          .mutation_kind = QuaderMutationKind::None,
      };
    }

    QuaderApiVoidResult result =
        editor.scene().commit_transform_preview(*preview_, label_);
    reset();
    return result;
}

DocumentCommandResult SceneTransformDragService::cancel() {
    if (!preview_) {
        targets_.clear();
        return document_command_invalid_request_result(
            "Transform preview is not active");
    }

    DocumentCommandResult result = preview_->cancel();
    reset();
    return result;
}

void SceneTransformDragService::abandon() noexcept {
  if (preview_) {
    preview_->abandon();
  }
  preview_.reset();
  targets_.clear();
  label_ = "Transform Selection";
  world_pivot_ = {};
}

void SceneTransformDragService::reset() {
    preview_.reset();
    targets_.clear();
    label_ = "Transform Selection";
    world_pivot_ = {};
}

SceneTransformDragService::CaptureTargetsStatus
SceneTransformDragService::capture_targets(SceneDocument &document,
                                           QVec3 world_pivot,
                                           QMat4 display_from_scene,
                                           const AssetRegistry *visible_pivot_assets) {
  targets_.clear();
  if (!finite_vector(world_pivot)) {
    return CaptureTargetsStatus::Invalid;
  }
  const std::vector<EntityId> selected = document.selected_entities();
  targets_.reserve(selected.size());
  const std::optional<QMat4> scene_from_display =
      try_scene_matrix_inverse(display_from_scene);
  if (!scene_from_display.has_value()) {
    return CaptureTargetsStatus::Invalid;
  }
  for (const EntityId entity : selected) {
    if (document.is_effectively_locked(entity)) {
      continue;
    }
    if (has_selected_ancestor(document, entity)) {
      continue;
    }
    if (const TransformComponent *selected_transform =
            document.transform(entity)) {
      const EntityId parent = document.parent(entity);
      const QMat4 parent_world =
          parent == kInvalidEntityId
              ? scene_transform_identity_matrix()
              : document.world_transform_matrix(parent);
      // Siblings can have distinct stored compensation. Capture the inverse
      // once per target, never share it merely because parent IDs match.
      const ParentTranslationBasis basis = parent_translation_basis(
          scene_transform_multiply(
              display_from_scene,
              scene_compensated_parent_matrix(parent_world,
                                               *selected_transform)));
      const QMat4 scene_world_transform =
          document.world_transform_matrix(entity);
      const QMat4 world_transform = scene_transform_multiply(
          display_from_scene, scene_world_transform);
      const std::optional<TransformComponent> validated_local_transform =
          try_scene_local_transform_preserving_world(
              parent_world, scene_world_transform, *selected_transform);
      if (!validated_local_transform.has_value()) {
        targets_.clear();
        return CaptureTargetsStatus::Invalid;
      }
      const std::optional<QMat4> world_inverse =
          try_scene_matrix_inverse(world_transform);
      if (!world_inverse.has_value()) {
        targets_.clear();
        return CaptureTargetsStatus::Invalid;
      }
      QVec3 display_visible_pivot = transform_point(world_transform, {});
      if (visible_pivot_assets != nullptr) {
        const std::optional<QVec3> scene_visible_pivot =
            resolve_entity_transform_visible_pivot(
                document, *visible_pivot_assets, entity);
        if (scene_visible_pivot.has_value()) {
          display_visible_pivot =
              transform_point(display_from_scene, *scene_visible_pivot);
        }
      } else if (selected.size() == 1U) {
        // Preserve the established single-target contract for callers that
        // already resolved the visible pivot before beginning the drag.
        display_visible_pivot = world_pivot;
      }
      if (!finite_vector(display_visible_pivot)) {
        targets_.clear();
        return CaptureTargetsStatus::Invalid;
      }
      targets_.push_back(SceneTransformDragTarget{
          .entity = entity,
          .transform = *selected_transform,
          // Translation changes position only. Re-decomposing the captured TRS
          // would rewrite authored Euler values and mirrored scale signs.
          .translation_preview_transform = *selected_transform,
          .local_pivot = transform_point(*world_inverse, world_pivot),
          .local_visible_pivot =
              transform_point(*world_inverse, display_visible_pivot),
          .display_visible_pivot = display_visible_pivot,
          .world_transform = world_transform,
          .scene_from_display = *scene_from_display,
          .parent_space_world_x = basis.world_x,
          .parent_space_world_y = basis.world_y,
          .parent_space_world_z = basis.world_z,
          .parent_translation_basis_valid = basis.valid,
      });
    }
  }
  return targets_.empty() ? CaptureTargetsStatus::Empty
                          : CaptureTargetsStatus::Captured;
}

const TransformComponent *
SceneTransformDragService::captured_primary_transform(
    const SceneDocument &document, EntityId primary) const {
  for (const SceneTransformDragTarget &target : targets_) {
    if (target.entity == primary) {
      return &target.transform;
    }
  }
  return document.transform(primary);
}

QuaderApiVoidResult
SceneTransformDragService::move_selected_to_primary_position(
    QuaderEditorApi &editor, SceneDocument &document, EntityId primary,
    QVec3 next_primary_position, std::string label) {
  reset();
  label_ = std::move(label);
  const TransformComponent *primary_transform = document.transform(primary);
  if (primary_transform == nullptr) {
    return unchanged_transform_api_result(editor, label_);
  }

  const QVec3 delta = next_primary_position - primary_transform->position;
  const CaptureTargetsStatus capture = capture_targets(document, {});
  if (capture != CaptureTargetsStatus::Captured) {
    reset();
    if (capture == CaptureTargetsStatus::Invalid) {
      return invalid_transform_api_result(
          editor, "Selected transforms cannot be represented safely");
    }
    return unchanged_transform_api_result(editor, label_);
  }

  const std::vector<DocumentTransformEdit> edits = build_target_transform_edits(
      document, targets_, [&](const SceneTransformDragTarget &target) {
        TransformComponent next = target.transform;
        next.position = next.position + delta;
        return next;
      });
  QuaderApiVoidResult result =
      commit_edits(editor, document, edits, SceneTextureTransformKind::Translation);
  reset();
  return result;
}

QuaderApiVoidResult SceneTransformDragService::
    rotate_selected_to_primary_euler_around_selection_pivot(
        QuaderEditorApi &editor, SceneDocument &document,
        const AssetRegistry &assets, EntityId primary,
        QVec3 next_primary_euler_radians, std::string label) {
  reset();
  label_ = std::move(label);
  const TransformComponent *primary_transform = document.transform(primary);
  if (primary_transform == nullptr) {
    return unchanged_transform_api_result(editor, label_);
  }

  if (!finite_vector(next_primary_euler_radians)) {
    return invalid_transform_api_result(editor, "Rotation must be finite");
  }
  const QVec3 world_pivot =
      selected_transform_pivot(document, assets, primary, *primary_transform);
  const CaptureTargetsStatus capture = capture_targets(document, world_pivot);
  if (capture != CaptureTargetsStatus::Captured) {
    reset();
    if (capture == CaptureTargetsStatus::Invalid) {
      return invalid_transform_api_result(
          editor, "Selected transforms cannot be represented safely");
    }
    return unchanged_transform_api_result(editor, label_);
  }

  const std::vector<DocumentTransformEdit> edits = build_primary_rotation_edits(
      document, targets_, primary, primary_transform->rotation_euler,
      next_primary_euler_radians, world_pivot);
  QuaderApiVoidResult result =
      commit_edits(editor, document, edits, SceneTextureTransformKind::Rotation);
  reset();
  return result;
}

QuaderApiVoidResult SceneTransformDragService::
    scale_selected_to_primary_scale_around_selection_pivot(
        QuaderEditorApi &editor, SceneDocument &document,
        const AssetRegistry &assets, EntityId primary, QVec3 next_primary_scale,
        std::string label) {
  reset();
  label_ = std::move(label);
  const TransformComponent *primary_transform = document.transform(primary);
  if (primary_transform == nullptr) {
    return unchanged_transform_api_result(editor, label_);
  }

  const QVec3 world_pivot =
      selected_transform_pivot(document, assets, primary, *primary_transform);
  const QVec3 ratio =
      component_scale_ratio(next_primary_scale, primary_transform->scale);
  const CaptureTargetsStatus capture = capture_targets(document, world_pivot);
  if (capture != CaptureTargetsStatus::Captured) {
    reset();
    if (capture == CaptureTargetsStatus::Invalid) {
      return invalid_transform_api_result(
          editor, "Selected transforms cannot be represented safely");
    }
    return unchanged_transform_api_result(editor, label_);
  }

  const std::vector<DocumentTransformEdit> edits = build_target_transform_edits(
      document, targets_, [&](const SceneTransformDragTarget &target) {
        TransformComponent next = target.transform;
        next.scale = clamp_scale(multiply_components(next.scale, ratio));
        pin_target_local_pivot_to_world_point(
            document, target, next, target.local_pivot, world_pivot);
        return next;
      });
  QuaderApiVoidResult result =
      commit_edits(editor, document, edits, SceneTextureTransformKind::Scale);
  reset();
  return result;
}

DocumentCommandResult SceneTransformDragService::move_selected_by_world_delta(
    DocumentCommandService &document_commands, SceneDocument &document,
    QVec3 world_delta, std::string label, QMat4 display_from_scene) {
  const QMat4 world_transform_delta =
      from_glm(glm::translate(glm::mat4{1.0f}, to_glm(world_delta)));
  return transform_selected_by_world_delta(
      document_commands, document, world_transform_delta, std::move(label),
      display_from_scene, SceneTextureTransformKind::Translation);
}

DocumentCommandResult
SceneTransformDragService::transform_selected_by_world_delta(
    DocumentCommandService &document_commands, SceneDocument &document,
    const QMat4 &world_transform_delta, std::string label,
    QMat4 display_from_scene, SceneTextureTransformKind kind) {
  reset();
  label_ = label.empty() ? "Transform Selection" : std::move(label);
  const CaptureTargetsStatus capture =
      capture_targets(document, {}, display_from_scene);
  if (capture != CaptureTargetsStatus::Captured) {
    reset();
    if (capture == CaptureTargetsStatus::Invalid) {
      return document_command_invalid_request_result(
          "Selected transforms cannot be represented safely");
    }
    return document_command_unchanged_result();
  }

  std::vector<DocumentTransformEdit> edits;
  edits.reserve(targets_.size());
  for (const SceneTransformDragTarget &target : targets_) {
    if (!document.exists(target.entity) ||
        document.is_effectively_locked(target.entity)) {
      continue;
    }
    const std::optional<TransformComponent> next_transform =
        transformed_target_from_world_delta(document, target,
                                            world_transform_delta);
    if (!next_transform.has_value()) {
      reset();
      return document_command_invalid_request_result(
          "World transform cannot be represented without shear");
    }
    edits.push_back(DocumentTransformEdit{
        .entity = target.entity,
        .before = target.transform,
        .after = *next_transform,
    });
  }
  DocumentCommandResult result =
      document_commands.set_transforms(std::move(edits), label_, kind);
  reset();
  return result;
}

QuaderApiVoidResult SceneTransformDragService::commit_edits(
    QuaderEditorApi &editor, SceneDocument &document,
    std::vector<DocumentTransformEdit> edits, SceneTextureTransformKind kind) {
  std::vector<EntityId> entities;
  entities.reserve(edits.size());
  for (const DocumentTransformEdit &edit : edits) {
    entities.push_back(edit.entity);
  }
  const TextureLockSettings settings{
      .texture_lock = editor.preferences().texture_lock_enabled(),
      .texture_lock_scale = editor.preferences().texture_lock_scale_enabled()};
  SceneTransformPreviewTransaction transaction(document, std::move(entities),
                                                label_, settings);
  const DocumentCommandResult preview = transaction.preview_transforms(std::move(edits), kind);
  if (!preview.ok) {
    return invalid_transform_api_result(editor, preview.message);
  }
  return editor.scene().commit_transform_preview(transaction, label_);
}

} // namespace quader
