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
#include <unordered_map>
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

/// Converts a world-space move delta into one selected entity's parent space.
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

ParentTranslationBasis
parent_translation_basis(const SceneDocument &document, EntityId parent) {
  if (parent == kInvalidEntityId) {
    return {};
  }
  const std::optional<QMat4> parent_from_world =
      try_scene_matrix_inverse(document.world_transform_matrix(parent));
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
  const EntityId parent = document.parent(target.entity);
  const QMat4 parent_world_transform =
      parent == kInvalidEntityId ? scene_transform_identity_matrix()
                                 : document.world_transform_matrix(parent);
  return try_scene_local_transform_preserving_world(
      parent_world_transform, preview_world_transform, target.transform);
}

void pin_target_local_pivot_to_world_point(
    const SceneDocument &document, const SceneTransformDragTarget &target,
    TransformComponent &transform, QVec3 world_pivot) {
  QTransform spatial = transform.spatial_transform();
  spatial.position = {};
  const EntityId parent = document.parent(target.entity);
  const QMat4 parent_world = parent == kInvalidEntityId
                                 ? scene_transform_identity_matrix()
                                 : document.world_transform_matrix(parent);
  const QVec3 parent_space_pivot =
      transform_point(inverse(parent_world), world_pivot);
  transform.position =
      parent_space_pivot -
      transform_point(compose_transform(spatial), target.local_pivot);
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

QuaderApiVoidResult commit_scene_transform_edits_through_api(
    QuaderEditorApi &editor, std::span<const DocumentTransformEdit> edits,
    const std::string &label) {
  std::vector<DocumentTransformEdit> changed_edits;
  changed_edits.reserve(edits.size());
  for (const DocumentTransformEdit &edit : edits) {
    if (!document_command_same_transform(edit.before, edit.after)) {
      changed_edits.push_back(edit);
    }
  }
  if (changed_edits.empty()) {
    return unchanged_transform_api_result(editor, label);
  }

  QuaderSceneBatch batch = editor.scene().batch(label);
  for (const DocumentTransformEdit &edit : changed_edits) {
    batch.set_transform(edit.entity, to_api_transform(edit.after));
  }
  return batch.commit();
}

QuaderApiVoidResult commit_scene_transform_preview_through_api(
    QuaderEditorApi &editor, SceneTransformPreviewTransaction &transaction,
    const std::string &label) {
  std::vector<DocumentTransformEdit> changed_edits;
  changed_edits.reserve(transaction.edits().size());
  for (const DocumentTransformEdit &edit : transaction.edits()) {
    if (!document_command_same_transform(edit.before, edit.after)) {
      changed_edits.push_back(edit);
    }
  }

  const DocumentCommandResult reset = transaction.cancel();
  if (!reset.ok) {
    return {
        .ok = false,
        .changed = false,
        .message = reset.message,
        .mutation_kind = QuaderMutationKind::None,
    };
  }
  if (changed_edits.empty()) {
    return {
        .ok = true,
        .changed = false,
        .message = label,
        .mutation_kind = QuaderMutationKind::None,
        .revisions = editor.revisions(),
    };
  }

  QuaderSceneBatch batch = editor.scene().batch(label);
  for (const DocumentTransformEdit &edit : changed_edits) {
    batch.set_transform(edit.entity, to_api_transform(edit.after));
  }
  return batch.commit();
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
                                      std::string label) {
  reset();
  label_ = std::move(label);
  world_pivot_ = world_pivot;

  if (capture_targets(document, world_pivot) !=
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
    preview_.emplace(document, std::move(preview_entities), label_);
    if (preview_->empty()) {
      reset();
      return false;
    }
  }
  return true;
}

bool SceneTransformDragService::begin_move_selected_to_primary_position_preview(
    SceneDocument &document, EntityId primary, std::string label) {
  if (document.transform(primary) == nullptr) {
    reset();
    return false;
  }
  return begin(document, {}, true, std::move(label));
}

bool SceneTransformDragService::begin_rotate_selected_to_primary_euler_preview(
    SceneDocument &document, const AssetRegistry &assets, EntityId primary,
    std::string label) {
  const TransformComponent *primary_transform = document.transform(primary);
  if (primary_transform == nullptr) {
    reset();
    return false;
  }
  return begin(document,
               selected_transform_pivot(document, assets, primary,
                                        *primary_transform),
               true, std::move(label));
}

bool SceneTransformDragService::begin_scale_selected_to_primary_scale_preview(
    SceneDocument &document, const AssetRegistry &assets, EntityId primary,
    std::string label) {
  const TransformComponent *primary_transform = document.transform(primary);
  if (primary_transform == nullptr) {
    reset();
    return false;
  }
  return begin(document,
               selected_transform_pivot(document, assets, primary,
                                        *primary_transform),
               true, std::move(label));
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
  return preview_->preview_transforms(std::move(edits));
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

  const QVec3 previous_euler =
      positive_euler_radians(primary_transform->rotation_euler);
  const QVec3 delta = next_primary_euler_radians - previous_euler;
  std::vector<DocumentTransformEdit> edits = build_target_transform_edits(
      document, targets_, [&](const SceneTransformDragTarget &target) {
        TransformComponent next = target.transform;
        next.rotation_euler = euler_radians_after_world_rotation_delta(
            next.rotation_euler, delta);
        pin_target_local_pivot_to_world_point(document, target, next,
                                              world_pivot_);
        return next;
      });
  return preview_->preview_transforms(std::move(edits));
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
        pin_target_local_pivot_to_world_point(document, target, next,
                                              world_pivot_);
        return next;
      });
  return preview_->preview_transforms(std::move(edits));
}

DocumentCommandResult SceneTransformDragService::preview_world_transform_delta(
    SceneDocument &document, const QMat4 &world_transform_delta) {
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
  return preview_->preview_transforms(std::move(preview_edits));
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
  return preview_->preview_transforms(std::move(preview_edits));
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
        commit_scene_transform_preview_through_api(editor, *preview_, label_);
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

void SceneTransformDragService::reset() {
    preview_.reset();
    targets_.clear();
    label_ = "Transform Selection";
    world_pivot_ = {};
}

SceneTransformDragService::CaptureTargetsStatus
SceneTransformDragService::capture_targets(SceneDocument &document,
                                           QVec3 world_pivot) {
  targets_.clear();
  if (!finite_vector(world_pivot)) {
    return CaptureTargetsStatus::Invalid;
  }
  const std::vector<EntityId> selected = document.selected_entities();
  targets_.reserve(selected.size());
  std::unordered_map<EntityId, ParentTranslationBasis> basis_by_parent;
  basis_by_parent.reserve(selected.size());
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
      const auto [basis_it, inserted] = basis_by_parent.try_emplace(parent);
      if (inserted) {
        basis_it->second = parent_translation_basis(document, parent);
      }
      const ParentTranslationBasis &basis = basis_it->second;
      const QMat4 world_transform = document.world_transform_matrix(entity);
      const QMat4 parent_world =
          parent == kInvalidEntityId
              ? scene_transform_identity_matrix()
              : document.world_transform_matrix(parent);
      const std::optional<TransformComponent> translation_preview_transform =
          try_scene_local_transform_preserving_world(
              parent_world, world_transform, *selected_transform);
      if (!translation_preview_transform.has_value()) {
        targets_.clear();
        return CaptureTargetsStatus::Invalid;
      }
      const std::optional<QMat4> world_inverse =
          try_scene_matrix_inverse(world_transform);
      if (!world_inverse.has_value()) {
        targets_.clear();
        return CaptureTargetsStatus::Invalid;
      }
      targets_.push_back(SceneTransformDragTarget{
          .entity = entity,
          .transform = *selected_transform,
          .translation_preview_transform = *translation_preview_transform,
          .local_pivot = transform_point(*world_inverse, world_pivot),
          .world_transform = world_transform,
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
      commit_scene_transform_edits_through_api(editor, edits, label_);
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

  const QVec3 world_pivot =
      selected_transform_pivot(document, assets, primary, *primary_transform);
  const QVec3 previous_euler =
      positive_euler_radians(primary_transform->rotation_euler);
  const QVec3 delta = next_primary_euler_radians - previous_euler;
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
        next.rotation_euler = euler_radians_after_world_rotation_delta(
            next.rotation_euler, delta);
        pin_target_local_pivot_to_world_point(document, target, next,
                                              world_pivot);
        return next;
      });
  QuaderApiVoidResult result =
      commit_scene_transform_edits_through_api(editor, edits, label_);
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
        pin_target_local_pivot_to_world_point(document, target, next,
                                              world_pivot);
        return next;
      });
  QuaderApiVoidResult result =
      commit_scene_transform_edits_through_api(editor, edits, label_);
  reset();
  return result;
}

DocumentCommandResult SceneTransformDragService::move_selected_by_world_delta(
    DocumentCommandService &document_commands, SceneDocument &document,
    QVec3 world_delta, std::string label) {
  const QMat4 world_transform_delta =
      from_glm(glm::translate(glm::mat4{1.0f}, to_glm(world_delta)));
  return transform_selected_by_world_delta(
      document_commands, document, world_transform_delta, std::move(label));
}

DocumentCommandResult
SceneTransformDragService::transform_selected_by_world_delta(
    DocumentCommandService &document_commands, SceneDocument &document,
    const QMat4 &world_transform_delta, std::string label) {
  reset();
  label_ = label.empty() ? "Transform Selection" : std::move(label);
  const CaptureTargetsStatus capture = capture_targets(document, {});
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
      document_commands.set_transforms(std::move(edits), label_);
  reset();
  return result;
}

} // namespace quader
