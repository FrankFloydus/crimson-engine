////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_scene_api.hpp"
#include "editor/history/scene_transform_preview_transaction.hpp"
#include "api/quader_api_session_internal_access.hpp"

#include "api/quader_api_type_conversions.hpp"
#include "api/quader_api_mutation_runner.hpp"
#include "core/undo_redo_execute_options.hpp"
#include "editor/commands/scene_structural_lifecycle_service.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/commands/document/document_command_edits.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"
#include "editor/selection/editor_selection_service.hpp"
#include "editor/tools/native_viewport_tool_coordinator.hpp"
#include "scene/scene_bounds.hpp"
#include "scene/scene_entity_naming.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace quader {
namespace {

SceneSession &scene_session(QuaderApiSession session) {
  return QuaderApiSessionInternalAccess::scene_session(session);
}

std::string batch_label_or_default(std::string label) {
  return label.empty() ? "Scene Batch" : std::move(label);
}

bool is_default_transform(const TransformComponent &transform) {
  return transform.position.x == 0.0f && transform.position.y == 0.0f &&
         transform.position.z == 0.0f && transform.rotation_euler.x == 0.0f &&
         transform.rotation_euler.y == 0.0f &&
         transform.rotation_euler.z == 0.0f && transform.scale.x == 1.0f &&
         transform.scale.y == 1.0f && transform.scale.z == 1.0f;
}

TransformComponent
group_transform_or_selected_bounds_center(SceneSession &session,
                                          QuaderTransform transform) {
  TransformComponent scene_transform = to_scene_transform(transform);
  if (!is_default_transform(scene_transform)) {
    return scene_transform;
  }

  if (const std::optional<SceneBounds> bounds =
          selected_world_bounds(session.scene(), session.assets())) {
    scene_transform.position = bounds_center(*bounds);
  }
  return scene_transform;
}

TransformComponent scene_transform_preserving_pivot_marker(
    const SceneSession &session, EntityId entity, QuaderTransform transform) {
  TransformComponent scene_transform = to_scene_transform(transform);
  if (const TransformComponent *current = session.scene().transform(entity)) {
    scene_transform.custom_pivot = current->custom_pivot;
  }
  return scene_transform;
}

std::vector<DocumentTransformEdit>
to_document_transform_edits(const SceneSession &session,
                            std::span<const QuaderSceneTransformEdit> edits) {
  std::vector<DocumentTransformEdit> document_edits;
  document_edits.reserve(edits.size());
  for (const QuaderSceneTransformEdit &edit : edits) {
    document_edits.push_back(DocumentTransformEdit{
        .entity = edit.entity,
        .before = scene_transform_preserving_pivot_marker(
            session, edit.entity, edit.before),
        .after = scene_transform_preserving_pivot_marker(
            session, edit.entity, edit.after),
    });
  }
  return document_edits;
}

std::vector<DocumentVisibilityEdit>
to_document_visibility_edits(std::span<const QuaderSceneVisibilityEdit> edits) {
  std::vector<DocumentVisibilityEdit> document_edits;
  document_edits.reserve(edits.size());
  for (const QuaderSceneVisibilityEdit &edit : edits) {
    document_edits.push_back(DocumentVisibilityEdit{
        .entity = edit.entity,
        .before = edit.before,
        .after = edit.after,
    });
  }
  return document_edits;
}

std::vector<DocumentLockEdit>
to_document_lock_edits(std::span<const QuaderSceneLockEdit> edits) {
  std::vector<DocumentLockEdit> document_edits;
  document_edits.reserve(edits.size());
  for (const QuaderSceneLockEdit &edit : edits) {
    document_edits.push_back(DocumentLockEdit{
        .entity = edit.entity,
        .before = edit.before,
        .after = edit.after,
    });
  }
  return document_edits;
}

UndoRedoExecuteOptions to_undo_redo_options(QuaderSceneEditOptions options) {
  return UndoRedoExecuteOptions{
      .merge_key = std::move(options.merge_key),
  };
}


} // namespace

QuaderSceneBatch::QuaderSceneBatch(QuaderSceneApi &scene, std::string label)
    : scene_(&scene), scene_session_id_(scene.mutations_.scene_session_id()),
      label_(batch_label_or_default(std::move(label))) {}

QuaderSceneBatch::QuaderSceneBatch(QuaderSceneBatch&& other) noexcept
    : scene_(other.scene_)
    , scene_session_id_(other.scene_session_id_)
    , label_(std::move(other.label_))
    , operations_(std::move(other.operations_))
    , committed_(other.committed_) {
    other.scene_ = nullptr;
    other.committed_ = true;
}

QuaderSceneBatch& QuaderSceneBatch::operator=(QuaderSceneBatch&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    scene_ = other.scene_;
    scene_session_id_ = other.scene_session_id_;
    label_ = std::move(other.label_);
    operations_ = std::move(other.operations_);
    committed_ = other.committed_;
    other.scene_ = nullptr;
    other.committed_ = true;
    return *this;
}

QuaderSceneBatch& QuaderSceneBatch::rename(EntityId entity, std::string name) {
    operations_.push_back(Operation{
            .kind = OperationKind::Rename,
            .entity = entity,
            .name = std::move(name),
    });
    return *this;
}

QuaderSceneBatch &QuaderSceneBatch::reparent(EntityId entity, EntityId parent,
                                             bool preserve_world_transform) {
  operations_.push_back(Operation{
      .kind = OperationKind::Reparent,
      .entity = entity,
      .parent = parent,
      .preserve_world_transform = preserve_world_transform,
  });
  return *this;
}

QuaderSceneBatch &QuaderSceneBatch::set_transform(EntityId entity,
                                                  QuaderTransform transform) {
  operations_.push_back(Operation{
      .kind = OperationKind::SetTransform,
      .entity = entity,
      .transform = transform,
  });
  return *this;
}

QuaderSceneBatch &QuaderSceneBatch::set_visible(EntityId entity, bool visible) {
  operations_.push_back(Operation{
      .kind = OperationKind::SetVisible,
      .entity = entity,
      .flag = visible,
  });
  return *this;
}

QuaderSceneBatch &QuaderSceneBatch::set_locked(EntityId entity, bool locked) {
  operations_.push_back(Operation{
      .kind = OperationKind::SetLocked,
      .entity = entity,
      .flag = locked,
  });
  return *this;
}

QuaderSceneBatch &QuaderSceneBatch::set_mesh_component(
    EntityId entity, QuaderMeshComponent mesh) {
  operations_.push_back(Operation{
      .kind = OperationKind::SetMeshComponent,
      .entity = entity,
      .mesh = mesh,
  });
  return *this;
}

QuaderApiVoidResult QuaderSceneBatch::commit() {
    if (scene_ == nullptr) {
      return {
          .ok = false,
          .changed = false,
          .message = "Scene batch is not attached.",
          .mutation_kind = QuaderMutationKind::None,
      };
    }
    if (committed_) {
      return {
          .ok = false,
          .changed = false,
          .message = "Scene batch was already committed.",
          .mutation_kind = QuaderMutationKind::None,
      };
    }
    if (!scene_->mutations_.belongs_to_active_scene(scene_session_id_)) {
      committed_ = true;
      return scene_->mutations_.stale_scene_reference_status("Scene batch");
    }
    committed_ = true;
    return scene_->commit_batch(*this);
}

QuaderSceneApi::QuaderSceneApi(QuaderApiSession session, QuaderApiMutationRunner& mutations)
    : session_(session)
    , mutations_(mutations) {}

QuaderApiResult<QuaderEntityRef>
QuaderSceneApi::create_entity(const QuaderCreateEntityArgs &args) {
  return mutations_.create_entity(args);
}

QuaderApiResult<QuaderEntityRef> QuaderSceneApi::create_empty() {
  SceneSession &session = scene_session(session_);
  return create_entity({
      .name = SceneEntityNaming::next_name_for_kind(
          session.scene(), SceneEntityKind::Empty),
      .kind = QuaderEntityKind::Empty,
      .parent = kInvalidEntityId,
      .transform = {},
      .select_created = true,
  });
}

QuaderApiResult<QuaderEntityRef>
QuaderSceneApi::create_point_light(const QuaderCreatePointLightArgs &args) {
  return mutations_.create_point_light(args);
}

QuaderApiResult<QuaderEntityRef> QuaderSceneApi::create_directional_light(
    const QuaderCreateDirectionalLightArgs &args) {
  return mutations_.create_directional_light(args);
}

QuaderApiVoidResult QuaderSceneApi::destroy(EntityId entity) {
  SceneStructuralLifecycleService &lifecycle =
      scene_session(session_).structural_lifecycle();
  if (lifecycle.hierarchy_contains_modeling_content(
          std::span<const EntityId>(&entity, 1U)) &&
      !mutations_.can_mutate_modeling_content()) {
    return mutations_.modeling_content_blocked_status(
        "destroying scene content.");
  }
  return mutations_.run_document_command(
      lifecycle.destroy_entity(entity),
      "Destroy Entity");
}

QuaderApiVoidResult QuaderSceneApi::rename(EntityId entity, std::string name) {
  return mutations_.run_document_result(
      scene_session(session_).document_commands().rename_entity(
          entity, std::move(name)),
      "Rename Entity");
}

QuaderApiVoidResult QuaderSceneApi::reparent(EntityId entity, EntityId parent,
                                             bool preserve_world_transform) {
  return mutations_.run_document_result(
      scene_session(session_).document_commands().reparent_entity(
          entity, parent, preserve_world_transform),
      "Reparent Entity");
}

QuaderApiVoidResult QuaderSceneApi::move_entities(
    std::span<const EntityId> entities, EntityId parent,
    EntityId before_sibling) {
  return mutations_.run_document_result(
      scene_session(session_).document_commands().move_entities(
          entities, parent, before_sibling),
      "Move Entities");
}

QuaderApiVoidResult QuaderSceneApi::set_transform(EntityId entity,
                                                  QuaderTransform transform) {
  SceneSession &session = scene_session(session_);
  return mutations_.run_document_result(
      session.document_commands().set_transform(
          entity,
          scene_transform_preserving_pivot_marker(session, entity, transform)),
      "Set Transform");
}

QuaderApiVoidResult QuaderSceneApi::set_transforms(
    std::span<const QuaderSceneTransformEdit> edits) {
  SceneSession &session = scene_session(session_);
  return mutations_.run_document_result(
      session.document_commands().set_transforms(
          to_document_transform_edits(session, edits)),
      "Set Transform");
}

QuaderApiVoidResult QuaderSceneApi::commit_transform_preview(
    SceneTransformPreviewTransaction &preview, const std::string &label) {
  return mutations_.run_document_result(
      preview.commit(scene_session(session_).history()), label);
}

QuaderApiVoidResult QuaderSceneApi::set_visible(EntityId entity, bool visible) {
  return mutations_.run_document_result(
      scene_session(session_).document_commands().set_visible(entity,
                                                                visible),
      "Set Visibility");
}

QuaderApiVoidResult QuaderSceneApi::set_visibility(
    std::span<const QuaderSceneVisibilityEdit> edits) {
  return mutations_.run_document_result(
      scene_session(session_).document_commands().set_visibility(
          to_document_visibility_edits(edits)),
      "Set Visibility");
}

QuaderApiVoidResult QuaderSceneApi::set_locked(EntityId entity, bool locked) {
  SceneSession &session = scene_session(session_);
  return mutations_.run_document_result(
      session.document_commands().set_locked(entity, locked), "Set Lock");
}

QuaderApiVoidResult
QuaderSceneApi::set_locks(std::span<const QuaderSceneLockEdit> edits) {
  SceneSession &session = scene_session(session_);
  return mutations_.run_document_result(
      session.document_commands().set_locks(to_document_lock_edits(edits)),
      "Set Lock");
}

QuaderApiVoidResult
QuaderSceneApi::set_mesh_component(EntityId entity, QuaderMeshComponent mesh) {
  return mutations_.run_document_result(
      scene_session(session_).document_commands().set_mesh_component(
          entity, to_scene_mesh_component(mesh)),
      "Set Mesh Component");
}

QuaderApiVoidResult
QuaderSceneApi::set_light_component(EntityId entity, QuaderLightComponent light,
                                    QuaderSceneEditOptions options) {
  return mutations_.run_document_result(
      scene_session(session_).document_commands().set_light_component(
          entity, to_scene_light_component(light),
          to_undo_redo_options(std::move(options))),
      "Set Light Component");
}

QuaderApiResult<QuaderEntityRef>
QuaderSceneApi::begin_point_light_range_edit(EntityId entity) {
  SceneSession &session = scene_session(session_);
  if (session.modeling().modal_tool_owns_viewport() ||
      session.modeling().can_cancel_active_preview_for_undo() ||
      session.native_tools().has_pending_history_preview()) {
    return quader_api_result_from_status<QuaderEntityRef>(
        mutations_.run_runtime_result(document_command_invalid_request_result(
            "Apply or cancel the active tool before editing a light radius"),
            "Edit Point Light Radius"));
  }
  const auto status = mutations_.run_document_result(
      session.document_commands().begin_point_light_range_edit(entity),
      "Edit Point Light Radius");
  auto result = quader_api_result_from_status<QuaderEntityRef>(status);
  if (result.ok()) {
    result.value = QuaderEntityRef{
        .scene_session_id = mutations_.scene_session_id(), .id = entity,
        .uuid = session.scene().uuid(entity),
        .editor_object_id = session.scene().editor_object_id(entity)};
  }
  return result;
}

QuaderApiVoidResult QuaderSceneApi::update_point_light_range_edit(
    QuaderEntityRef light, float range) {
  if (!mutations_.belongs_to_active_scene(light.scene_session_id) ||
      scene_session(session_).scene().uuid(light.id) != light.uuid) {
    return mutations_.stale_scene_reference_status("Point light range edit");
  }
  auto result = mutations_.run_document_result(
      scene_session(session_).document_commands().update_point_light_range_edit(
          light.id, range), "Edit Point Light Radius");
  return result;
}

QuaderApiVoidResult QuaderSceneApi::finish_point_light_range_edit(
    QuaderEntityRef light, bool cancel) {
  if (!mutations_.belongs_to_active_scene(light.scene_session_id) ||
      scene_session(session_).scene().uuid(light.id) != light.uuid) {
    return mutations_.stale_scene_reference_status("Point light range edit");
  }
  const auto finished = scene_session(session_).document_commands()
                            .finish_point_light_range_edit(light.id, cancel);
  return mutations_.run_document_result(finished, "Edit Point Light Radius");
}

QuaderApiResult<QuaderScenePropertyEditRef>
QuaderSceneApi::begin_light_color_edit(EntityId entity) {
  SceneSession &session = scene_session(session_);
  if (session.modeling().modal_tool_owns_viewport() ||
      session.modeling().can_cancel_active_preview_for_undo() ||
      session.native_tools().has_pending_history_preview()) {
    return quader_api_result_from_status<QuaderScenePropertyEditRef>(
        mutations_.run_runtime_result(document_command_invalid_request_result(
            "Apply or cancel the active tool before editing a color"),
            "Edit Light Color"));
  }
  const auto status = mutations_.run_document_result(
      session.document_commands().begin_light_color_edit(entity),
      "Edit Light Color");
  auto result = quader_api_result_from_status<QuaderScenePropertyEditRef>(status);
  if (result.ok()) {
    result.value = QuaderScenePropertyEditRef{
        .entity = {.scene_session_id = mutations_.scene_session_id(), .id = entity,
                   .uuid = session.scene().uuid(entity),
                   .editor_object_id = session.scene().editor_object_id(entity)},
        .generation = session.document_commands().property_edit_generation()};
  }
  return result;
}

QuaderApiVoidResult QuaderSceneApi::update_light_color_edit(
    QuaderScenePropertyEditRef light, QVec3 color) {
  if (!mutations_.belongs_to_active_scene(light.entity.scene_session_id) ||
      scene_session(session_).scene().uuid(light.entity.id) != light.entity.uuid) {
    return mutations_.stale_scene_reference_status("Light color edit");
  }
  return mutations_.run_document_result(
      scene_session(session_).document_commands().update_light_color_edit(
          light.entity.id, light.generation, color), "Edit Light Color");
}

QuaderApiVoidResult QuaderSceneApi::finish_light_color_edit(
    QuaderScenePropertyEditRef light, bool cancel) {
  if (!mutations_.belongs_to_active_scene(light.entity.scene_session_id) ||
      scene_session(session_).scene().uuid(light.entity.id) != light.entity.uuid) {
    return mutations_.stale_scene_reference_status("Light color edit");
  }
  return mutations_.run_document_result(
      scene_session(session_).document_commands().finish_light_color_edit(
          light.entity.id, light.generation, cancel), "Edit Light Color");
}

QuaderApiResult<QuaderScenePropertyEditRef>
QuaderSceneApi::begin_environment_color_edit(EntityId entity) {
  SceneSession &session = scene_session(session_);
  if (session.modeling().modal_tool_owns_viewport() ||
      session.modeling().can_cancel_active_preview_for_undo() ||
      session.native_tools().has_pending_history_preview()) {
    return quader_api_result_from_status<QuaderScenePropertyEditRef>(
        mutations_.run_runtime_result(document_command_invalid_request_result(
            "Apply or cancel the active tool before editing a color"),
            "Edit Environment Color"));
  }
  const auto status = mutations_.run_document_result(
      session.document_commands().begin_environment_color_edit(entity),
      "Edit Environment Color");
  auto result = quader_api_result_from_status<QuaderScenePropertyEditRef>(status);
  if (result.ok()) {
    result.value = QuaderScenePropertyEditRef{
        .entity = {.scene_session_id = mutations_.scene_session_id(), .id = entity,
                   .uuid = session.scene().uuid(entity),
                   .editor_object_id = session.scene().editor_object_id(entity)},
        .generation = session.document_commands().property_edit_generation()};
  }
  return result;
}

QuaderApiVoidResult QuaderSceneApi::update_environment_color_edit(
    QuaderScenePropertyEditRef environment, QVec3 color) {
  if (!mutations_.belongs_to_active_scene(environment.entity.scene_session_id) ||
      scene_session(session_).scene().uuid(environment.entity.id) != environment.entity.uuid) {
    return mutations_.stale_scene_reference_status("Environment color edit");
  }
  return mutations_.run_document_result(
      scene_session(session_).document_commands().update_environment_color_edit(
          environment.entity.id, environment.generation, color), "Edit Environment Color");
}

QuaderApiVoidResult QuaderSceneApi::finish_environment_color_edit(
    QuaderScenePropertyEditRef environment, bool cancel) {
  if (!mutations_.belongs_to_active_scene(environment.entity.scene_session_id) ||
      scene_session(session_).scene().uuid(environment.entity.id) != environment.entity.uuid) {
    return mutations_.stale_scene_reference_status("Environment color edit");
  }
  return mutations_.run_document_result(
      scene_session(session_).document_commands().finish_environment_color_edit(
          environment.entity.id, environment.generation, cancel), "Edit Environment Color");
}

QuaderApiResult<QuaderScenePropertyEditRef>
QuaderSceneApi::begin_fog_color_edit(EntityId entity) {
  SceneSession &session = scene_session(session_);
  if (session.modeling().modal_tool_owns_viewport() ||
      session.modeling().can_cancel_active_preview_for_undo() ||
      session.native_tools().has_pending_history_preview()) {
    return quader_api_result_from_status<QuaderScenePropertyEditRef>(
        mutations_.run_runtime_result(document_command_invalid_request_result(
            "Apply or cancel the active tool before editing a color"),
            "Edit Fog Color"));
  }
  const auto status = mutations_.run_document_result(
      session.document_commands().begin_fog_color_edit(entity),
      "Edit Fog Color");
  auto result = quader_api_result_from_status<QuaderScenePropertyEditRef>(status);
  if (result.ok()) {
    result.value = QuaderScenePropertyEditRef{
        .entity = {.scene_session_id = mutations_.scene_session_id(), .id = entity,
                   .uuid = session.scene().uuid(entity),
                   .editor_object_id = session.scene().editor_object_id(entity)},
        .generation = session.document_commands().property_edit_generation()};
  }
  return result;
}

QuaderApiVoidResult QuaderSceneApi::update_fog_color_edit(
    QuaderScenePropertyEditRef fog, QVec3 color) {
  if (!mutations_.belongs_to_active_scene(fog.entity.scene_session_id) ||
      scene_session(session_).scene().uuid(fog.entity.id) != fog.entity.uuid) {
    return mutations_.stale_scene_reference_status("Fog color edit");
  }
  return mutations_.run_document_result(
      scene_session(session_).document_commands().update_fog_color_edit(
          fog.entity.id, fog.generation, color), "Edit Fog Color");
}

QuaderApiVoidResult QuaderSceneApi::finish_fog_color_edit(
    QuaderScenePropertyEditRef fog, bool cancel) {
  if (!mutations_.belongs_to_active_scene(fog.entity.scene_session_id) ||
      scene_session(session_).scene().uuid(fog.entity.id) != fog.entity.uuid) {
    return mutations_.stale_scene_reference_status("Fog color edit");
  }
  return mutations_.run_document_result(
      scene_session(session_).document_commands().finish_fog_color_edit(
          fog.entity.id, fog.generation, cancel), "Edit Fog Color");
}

QuaderApiVoidResult
QuaderSceneApi::set_scene_settings(EntityId entity,
                                   QuaderSceneSettings settings,
                                   QuaderSceneEditOptions options) {
  return mutations_.run_document_result(
      scene_session(session_).document_commands().set_scene_settings(
          entity, to_scene_settings_component(settings),
          to_undo_redo_options(std::move(options))),
      "Set Scene Settings");
}

QuaderApiVoidResult QuaderSceneApi::select(std::span<const EntityId> entities, EntityId active) {
  return mutations_.run_document_result(
      scene_session(session_).selection().set_scene_selection(entities,
                                                                active),
      "Select Entities");
}

QuaderApiVoidResult QuaderSceneApi::select(std::initializer_list<EntityId> entities, EntityId active) {
    return select(std::span<const EntityId>(entities.begin(), entities.size()), active);
}

QuaderApiVoidResult QuaderSceneApi::select(EntityId entity) {
    return select({entity}, entity);
}

QuaderApiResult<QuaderEntityRef> QuaderSceneApi::duplicate(EntityId entity) {
  SceneStructuralLifecycleService &lifecycle =
      scene_session(session_).structural_lifecycle();
  if (lifecycle.hierarchy_contains_modeling_content(
          std::span<const EntityId>(&entity, 1U)) &&
      !mutations_.can_mutate_modeling_content()) {
    return quader_api_result_from_status<QuaderEntityRef>(
        mutations_.modeling_content_blocked_status(
            "duplicating scene content."));
  }
  SceneStructuralDuplicateResult command =
      lifecycle.duplicate_entity(entity);
  EntityId duplicate_entity = kInvalidEntityId;
  if (command.ok && command.changed && command.clones.size() == 1U &&
      command.clones.front().source == entity) {
    duplicate_entity = command.clones.front().duplicate;
  }

  const QuaderApiVoidResult status =
      mutations_.run_document_command(std::move(command), "Duplicate Entity");
  QuaderApiResult<QuaderEntityRef> result =
      quader_api_result_from_status<QuaderEntityRef>(status);
  if (result.ok && result.changed) {
    result.value = QuaderEntityRef{
        .scene_session_id = mutations_.scene_session_id(),
        .id = duplicate_entity,
        .uuid = scene_session(session_).scene().uuid(duplicate_entity),
        .editor_object_id = scene_session(session_).scene().editor_object_id(
            duplicate_entity),
    };
  }
    return result;
}

QuaderApiResult<QuaderDuplicateHierarchyResult>
QuaderSceneApi::duplicate_selected_hierarchy() {
  SceneSession &session = scene_session(session_);
  SceneStructuralLifecycleService &lifecycle =
      session.structural_lifecycle();
  const std::vector<EntityId> roots = lifecycle.selected_roots();
  if (lifecycle.hierarchy_contains_modeling_content(roots) &&
      !mutations_.can_mutate_modeling_content()) {
    return quader_api_result_from_status<QuaderDuplicateHierarchyResult>(
        mutations_.modeling_content_blocked_status(
            "duplicating scene content."));
  }
  SceneStructuralDuplicateResult command =
      lifecycle.duplicate_hierarchy(roots, session.modeling().selection_mode(),
                                    "Duplicate Hierarchy");
  std::vector<EntityId> duplicate_roots;
  duplicate_roots.reserve(command.clones.size());
  for (const SceneStructuralClone &clone : command.clones) {
    duplicate_roots.push_back(clone.duplicate);
  }

  const QuaderApiVoidResult status =
      mutations_.run_document_command(std::move(command),
                                      "Duplicate Hierarchy");
  QuaderApiResult<QuaderDuplicateHierarchyResult> result =
      quader_api_result_from_status<QuaderDuplicateHierarchyResult>(status);
  if (result.ok && result.changed) {
    result.value.root_entities.reserve(duplicate_roots.size());
    for (const EntityId duplicate : duplicate_roots) {
      result.value.root_entities.push_back(QuaderEntityRef{
          .scene_session_id = mutations_.scene_session_id(),
          .id = duplicate,
          .uuid = scene_session(session_).scene().uuid(duplicate),
          .editor_object_id =
              scene_session(session_).scene().editor_object_id(duplicate),
      });
    }
  }
  return result;
}

QuaderApiResult<QuaderEntityRef>
QuaderSceneApi::group_selection(std::string group_name,
                                QuaderTransform transform) {
  SceneSession &session = scene_session(session_);
  DocumentGroupSelectionResult command =
      session.structural_lifecycle().group_selection(
          std::move(group_name), group_transform_or_selected_bounds_center(
                                     session, transform));
  const EntityId group_entity = command.group_entity;
  const QuaderApiVoidResult status = mutations_.run_document_command(
      std::move(command), "Group Selection");
  QuaderApiResult<QuaderEntityRef> result =
      quader_api_result_from_status<QuaderEntityRef>(status);
  if (result.ok && result.changed) {
    result.value = QuaderEntityRef{
        .scene_session_id = mutations_.scene_session_id(),
        .id = group_entity,
        .uuid = scene_session(session_).scene().uuid(group_entity),
        .editor_object_id =
            scene_session(session_).scene().editor_object_id(group_entity),
    };
  }
  return result;
}

QuaderApiVoidResult QuaderSceneApi::ungroup_selection() {
  SceneSession &session = scene_session(session_);
  DocumentCommandResult command =
      session.structural_lifecycle().ungroup_selection();
  return mutations_.run_document_command(std::move(command),
                                         "Ungroup Selection");
}

QuaderSceneBatch QuaderSceneApi::batch(std::string label) {
    return QuaderSceneBatch(*this, std::move(label));
}

QuaderApiVoidResult
QuaderSceneApi::commit_batch(const QuaderSceneBatch &batch) {
  std::vector<DocumentSceneBatchOperation> operations;
  operations.reserve(batch.operations_.size());
  SceneSession &session = scene_session(session_);
  for (const QuaderSceneBatch::Operation &operation : batch.operations_) {
    DocumentSceneBatchOperation document_operation{
        .entity = operation.entity,
        .parent = operation.parent,
        .preserve_world_transform = operation.preserve_world_transform,
        .flag = operation.flag,
        .transform = scene_transform_preserving_pivot_marker(
            session, operation.entity, operation.transform),
        .mesh = to_scene_mesh_component(operation.mesh),
        .name = operation.name,
    };
    switch (operation.kind) {
    case QuaderSceneBatch::OperationKind::Rename:
      document_operation.kind = DocumentSceneBatchOperationKind::Rename;
      break;
    case QuaderSceneBatch::OperationKind::Reparent:
      document_operation.kind = DocumentSceneBatchOperationKind::Reparent;
      break;
    case QuaderSceneBatch::OperationKind::SetTransform:
      document_operation.kind = DocumentSceneBatchOperationKind::SetTransform;
      break;
    case QuaderSceneBatch::OperationKind::SetVisible:
      document_operation.kind = DocumentSceneBatchOperationKind::SetVisible;
      break;
    case QuaderSceneBatch::OperationKind::SetLocked:
      document_operation.kind = DocumentSceneBatchOperationKind::SetLocked;
      break;
    case QuaderSceneBatch::OperationKind::SetMeshComponent:
      document_operation.kind = DocumentSceneBatchOperationKind::SetMeshComponent;
      break;
    }
    operations.push_back(std::move(document_operation));
  }

  DocumentCommandResult result = session.document_commands().apply_scene_batch(
      batch.label_, std::move(operations));
  return mutations_.run_document_result(std::move(result), batch.label_);
}

} // namespace quader
