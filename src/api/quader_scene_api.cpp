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
#include "editor/session/scene_session.hpp"
#include "editor/commands/document/document_command_edits.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"
#include "editor/selection/editor_selection_service.hpp"
#include "scene/scene_bounds.hpp"

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

QuaderApiVoidResult run_document_result_and_project_selection(
    QuaderApiMutationRunner &mutations, SceneSession &session,
    DocumentCommandResult result, std::string_view label) {
  if (result.ok && result.changed) {
    session.modeling().project_object_selection_from_scene(true);
  }
  return mutations.run_document_result(std::move(result), label);
}

void synchronize_modeling_selection_after_grouping(
    SceneSession &session, bool grouped,
    ModelingSelectionMode selection_mode_before) {
  if (is_component_selection_mode(selection_mode_before)) {
    return;
  }
  if (grouped || selection_mode_before == ModelingSelectionMode::Group) {
    session.modeling()
        .clear_runtime_object_selection_for_scene_owned_selection();
    return;
  }
  session.modeling().project_object_selection_from_scene(true);
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

QuaderApiResult<QuaderEntityRef>
QuaderSceneApi::create_point_light(const QuaderCreatePointLightArgs &args) {
  return mutations_.create_point_light(args);
}

QuaderApiResult<QuaderEntityRef> QuaderSceneApi::create_directional_light(
    const QuaderCreateDirectionalLightArgs &args) {
  return mutations_.create_directional_light(args);
}

QuaderApiVoidResult QuaderSceneApi::destroy(EntityId entity) {
  return mutations_.run_document_result(
      scene_session(session_).document_commands().destroy_entity(entity),
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
  return run_document_result_and_project_selection(
      mutations_, session,
      session.document_commands().set_locked(entity, locked), "Set Lock");
}

QuaderApiVoidResult
QuaderSceneApi::set_locks(std::span<const QuaderSceneLockEdit> edits) {
  SceneSession &session = scene_session(session_);
  return run_document_result_and_project_selection(
      mutations_, session,
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
  DocumentDuplicateEntityResult command =
      scene_session(session_).document_commands().duplicate_entity(entity);
  const EntityId duplicate_entity = command.duplicate_entity;

  const QuaderApiVoidResult status =
      mutations_.run_document_result(std::move(command), "Duplicate Entity");
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
  DocumentDuplicateHierarchyResult command =
      scene_session(session_)
          .document_commands()
          .duplicate_selected_hierarchy();
  const std::vector<EntityId> duplicate_roots = command.duplicate_roots;

  const QuaderApiVoidResult status =
      mutations_.run_document_result(std::move(command), "Duplicate Hierarchy");
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
  const ModelingSelectionMode selection_mode_before =
      session.modeling().selection_mode();
  DocumentGroupSelectionResult command =
      session.document_commands().group_selection(
          std::move(group_name), group_transform_or_selected_bounds_center(
                                     session, transform));
  const EntityId group_entity = command.group_entity;

  if (command.ok && command.changed) {
    synchronize_modeling_selection_after_grouping(
        session, true, selection_mode_before);
  }
  const QuaderApiVoidResult status = mutations_.run_document_result(
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
  const ModelingSelectionMode selection_mode_before =
      session.modeling().selection_mode();
  DocumentCommandResult command =
      session.document_commands().ungroup_selection();
  if (command.ok && command.changed) {
    synchronize_modeling_selection_after_grouping(
        session, false, selection_mode_before);
  }
  return mutations_.run_document_result(std::move(command),
                                        "Ungroup Selection");
}

QuaderSceneBatch QuaderSceneApi::batch(std::string label) {
    return QuaderSceneBatch(*this, std::move(label));
}

QuaderApiVoidResult
QuaderSceneApi::commit_batch(const QuaderSceneBatch &batch) {
  std::vector<DocumentSceneBatchOperation> operations;
  operations.reserve(batch.operations_.size());
  bool project_selection_after_batch = false;
  SceneSession &session = scene_session(session_);
  for (const QuaderSceneBatch::Operation &operation : batch.operations_) {
    DocumentSceneBatchOperation document_operation{
        .entity = operation.entity,
        .parent = operation.parent,
        .preserve_world_transform = operation.preserve_world_transform,
        .flag = operation.flag,
        .transform = scene_transform_preserving_pivot_marker(
            session, operation.entity, operation.transform),
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
      project_selection_after_batch = true;
      break;
    }
    operations.push_back(std::move(document_operation));
  }

  DocumentCommandResult result = session.document_commands().apply_scene_batch(
      batch.label_, std::move(operations));
  if (project_selection_after_batch) {
    return run_document_result_and_project_selection(
        mutations_, session, std::move(result), batch.label_);
  }
  return mutations_.run_document_result(std::move(result), batch.label_);
}

} // namespace quader
