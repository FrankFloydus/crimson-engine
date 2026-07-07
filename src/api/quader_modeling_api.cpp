////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_modeling_api.hpp"
#include "api/quader_api_session_internal_access.hpp"

#include "api/quader_api_mutation_runner.hpp"
#include "api/quader_api_status_internal.hpp"
#include "api/quader_api_type_conversions.hpp"
#include "api/quader_polygon_document_internal_access.hpp"
#include "diagnostics/profile.hpp"
#include "editor/commands/modeling/commit_box_preview_command.hpp"
#include "editor/history/modeling_memento_command.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"
#include "editor/selection/editor_selection_service.hpp"
#include "editor/session/project_session.hpp"
#include "editor/transform/scene_transform_drag_service.hpp"
#include "math/glm_bridge.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <memory>
#include <utility>

namespace quader {
namespace {

ProjectSession &project_session(QuaderApiSession session) {
  return QuaderApiSessionInternalAccess::project_session(session);
}

QuaderApiResult<QuaderModelingObjectRef>
result_from_create(QuaderApiVoidResult status, QuaderModelingObjectRef value) {
  return quader_api_result_from_status(status, value);
}

QuaderApiResult<QuaderTransformDragPreview>
result_from_transform_drag_preview(const ProjectSession &session,
                                   ModelingTransformDragPreview preview) {
  QuaderApiResult<QuaderTransformDragPreview> result;
  result.ok = preview.ok;
  result.changed = preview.changed;
  result.message = preview.message;
  result.mutation_kind = quader_api_preview_mutation_kind(
      preview.ok, preview.changed, preview.active,
      QuaderMutationKind::Modeling);
  result.revisions = quader_api_current_revisions(session);
  result.value = to_api_transform_drag_preview(preview);
  return result;
}

QuaderTransform3 rotation_transform(QVec3 euler_radians) {
  const glm::mat3 rotation =
      glm::mat3_cast(to_glm(quat_from_euler_radians(euler_radians)));
  return {
      .x_axis = from_glm(glm::vec3(rotation[0])),
      .y_axis = from_glm(glm::vec3(rotation[1])),
      .z_axis = from_glm(glm::vec3(rotation[2])),
      .origin = {},
  };
}

QuaderTransform3 scale_transform(QVec3 scale) {
  return {
      .x_axis = {scale.x, 0.0f, 0.0f},
      .y_axis = {0.0f, scale.y, 0.0f},
      .z_axis = {0.0f, 0.0f, scale.z},
      .origin = {},
  };
}

bool entity_selection_owns_transforms(ProjectSession &session) {
  const ModelingSelectionSummary summary = session.selection().summary();
  return summary.has_selection &&
         selection_mode_uses_scene_document(summary.mode);
}

QMat4 world_transform_delta_from_api_transform(
    const QuaderTransform3 &transform, QVec3 pivot) {
  glm::mat4 local_transform{1.0f};
  local_transform[0] = glm::vec4{to_glm(transform.x_axis), 0.0f};
  local_transform[1] = glm::vec4{to_glm(transform.y_axis), 0.0f};
  local_transform[2] = glm::vec4{to_glm(transform.z_axis), 0.0f};
  local_transform[3] = glm::vec4{to_glm(transform.origin), 1.0f};

  const glm::mat4 around_pivot =
      glm::translate(glm::mat4{1.0f}, to_glm(pivot)) * local_transform *
      glm::translate(glm::mat4{1.0f}, -to_glm(pivot));
  return from_glm(around_pivot);
}

} // namespace

QuaderModelingApi::QuaderModelingApi(QuaderApiSession session,
                                     QuaderApiMutationRunner &mutations)
    : session_(session), mutations_(mutations) {}

QuaderApiResult<QuaderModelingObjectRef>
QuaderModelingApi::create_box(const QuaderCreateBoxArgs &args) {
  auto command = std::make_unique<CreateBoxFromBoundsHistoryCommand>(
      project_session(session_).modeling(),
      ModelingCreateBoxRequest{
          .bounds = to_modeling_bounds(args.bounds),
          .name = args.name,
      });
  CreateBoxFromBoundsHistoryCommand *create_command = command.get();
  QuaderApiVoidResult status =
      mutations_.run_modeling_command(std::move(command), "Create Box");

  QuaderModelingObjectRef created;
  if (status.ok && status.changed) {
    created = ref_from_record(
        to_api_modeling_object_record(create_command->created().object));
  }
  return result_from_create(std::move(status), created);
}

QuaderApiResult<QuaderModelingObjectRef>
QuaderModelingApi::create_box(QuaderBounds bounds, std::string name) {
  return create_box(QuaderCreateBoxArgs{
      .bounds = bounds,
      .name = std::move(name),
  });
}

QuaderApiResult<QuaderModelingObjectRef>
QuaderModelingApi::commit_box_preview(std::string name,
                                      AssetId default_material) {
  auto command = std::make_unique<CommitBoxPreviewCommand>(
      project_session(session_).modeling(), project_session(session_).scene(),
      std::move(name), default_material);
  CommitBoxPreviewCommand *commit_command = command.get();
  QuaderApiVoidResult status = mutations_.run_modeling_undoable_command(
      std::move(command), "Create Box");

  QuaderModelingObjectRef created;
  if (status.ok && status.changed) {
    created = ref_from_record(
        to_api_modeling_object_record(commit_command->committed().object));
  }
  return result_from_create(std::move(status), created);
}

QuaderApiResult<QuaderModelingObjectRef>
QuaderModelingApi::create_mesh(QuaderPolygonDocument document,
                               QuaderCreateMeshArgs args) {
  ModelingCreateMeshRequest request;
  request.document = QuaderPolygonDocumentInternalAccess::to_backend(document);
  request.name = std::move(args.name);
  request.material_slot = args.material_slot;
  auto command = std::make_unique<CreateMeshHistoryCommand>(
      project_session(session_).modeling(), std::move(request));
  CreateMeshHistoryCommand *create_command = command.get();
  QuaderApiVoidResult status =
      mutations_.run_modeling_command(std::move(command), "Create Mesh");

  QuaderModelingObjectRef created;
  if (status.ok && status.changed) {
    created = ref_from_record(
        to_api_modeling_object_record(create_command->created().object));
  }
  return result_from_create(std::move(status), created);
}

QuaderApiVoidResult
QuaderModelingApi::set_selection_mode(QuaderSelectionMode mode) {
  return mutations_.run_modeling_result(
      project_session(session_).selection().set_mode(
          to_modeling_selection_mode(mode)),
      "Set Selection Mode");
}

bool QuaderModelingApi::can_execute_tool(std::string_view tool_id) const {
  return mutations_.can_run_tracked_modeling_tool(tool_id);
}

QuaderApiVoidResult QuaderModelingApi::execute_tool(std::string_view tool_id) {
  return mutations_.run_tracked_modeling_tool(tool_id);
}

QuaderApiVoidResult QuaderModelingApi::remove_doubles() {
  return mutations_.run_tracked_modeling_tool("remove_doubles");
}

QuaderApiVoidResult
QuaderModelingApi::begin_preview(std::string_view action_id) {
  return mutations_.begin_tracked_modeling_preview(action_id);
}

QuaderApiVoidResult
QuaderModelingApi::update_preview(const QuaderPreviewSettings &settings) {
  return mutations_.run_modeling_result(
      project_session(session_).modeling().set_interaction_settings(
          to_modeling_interaction_settings(settings.interaction)),
      "Update Operation Preview");
}

QuaderApiVoidResult QuaderModelingApi::commit_preview(std::string label) {
  return mutations_.commit_modeling_preview(std::move(label));
}

QuaderApiVoidResult QuaderModelingApi::cancel_preview() {
  return mutations_.cancel_modeling_preview();
}

bool QuaderModelingApi::can_cancel_active_tool_or_preview() const {
  return project_session(session_)
      .modeling()
      .can_cancel_active_tool_or_preview();
}

QuaderApiVoidResult
QuaderModelingApi::begin_transform_preview(bool duplicate_selected) {
  return mutations_.run_modeling_result(
      project_session(session_).modeling().begin_transform_preview(
          duplicate_selected),
      "Begin Transform Preview");
}

QuaderApiVoidResult QuaderModelingApi::update_transform_translate(QVec3 delta) {
  return mutations_.run_modeling_result(
      project_session(session_).modeling().update_transform_translate_preview(
          delta),
      "Update Transform Preview");
}

QuaderApiVoidResult
QuaderModelingApi::update_transform_matrix(const QuaderTransform3 &transform,
                                           QVec3 pivot) {
  return mutations_.run_modeling_result(
      project_session(session_).modeling().update_transform_matrix_preview(
          to_modeling_transform3(transform), pivot),
      "Update Transform Preview");
}

QuaderApiVoidResult
QuaderModelingApi::update_transform_extrude(QVec3 offset,
                                            float closed_edge_ledge_size) {
  return mutations_.run_modeling_result(
      project_session(session_).modeling().update_transform_extrude_preview(
          offset, closed_edge_ledge_size),
      "Update Transform Preview");
}

QuaderApiVoidResult
QuaderModelingApi::update_transform_inset(const QuaderTransform3 &transform,
                                          QVec3 pivot) {
  return mutations_.run_modeling_result(
      project_session(session_).modeling().update_transform_inset_preview(
          to_modeling_transform3(transform), pivot),
      "Update Transform Preview");
}

QuaderApiVoidResult
QuaderModelingApi::commit_transform_preview(std::string label) {
  return mutations_.run_modeling_command(
      std::make_unique<CommitTransformPreviewHistoryCommand>(
          project_session(session_).modeling()),
      label.empty() ? "Commit Transform Preview" : label);
}

QuaderApiVoidResult QuaderModelingApi::cancel_transform_preview() {
  return mutations_.run_modeling_result(
      project_session(session_).modeling().cancel_transform_preview(),
      "Cancel Transform Preview");
}

QuaderApiResult<QuaderTransformDragPreview>
QuaderModelingApi::begin_transform_drag(
    const QuaderTransformDragBeginRequest &request) {
  QDR_PROFILE_FUNCTION();
  return result_from_transform_drag_preview(
      project_session(session_),
      project_session(session_).modeling().begin_transform_drag(
          to_modeling_transform_drag_begin_request(request)));
}

QuaderApiResult<QuaderTransformDragPreview>
QuaderModelingApi::update_transform_drag(
    const QuaderTransformDragUpdateRequest &request) {
  QDR_PROFILE_FUNCTION();
  return result_from_transform_drag_preview(
      project_session(session_),
      project_session(session_).modeling().update_transform_drag(
          to_modeling_transform_drag_update_request(request)));
}

QuaderApiResult<QuaderTransformDragPreview>
QuaderModelingApi::commit_transform_drag() {
  QDR_PROFILE_FUNCTION();
  return result_from_transform_drag_preview(
      project_session(session_),
      project_session(session_).modeling().commit_transform_drag(
          project_session(session_).history()));
}

QuaderApiVoidResult QuaderModelingApi::cancel_transform_drag() {
  return mutations_.run_modeling_result(
      project_session(session_).modeling().cancel_transform_drag(),
      "Cancel Transform Drag");
}

QuaderApiVoidResult QuaderModelingApi::move_selection(QVec3 delta) {
  ProjectSession &session = project_session(session_);
  if (entity_selection_owns_transforms(session)) {
    SceneTransformDragService transforms;
    return mutations_.run_document_result(
        transforms.move_selected_by_world_delta(session.document_commands(),
                                                session.scene(), delta,
                                                "Move Selection"),
        "Move Selection");
  }

  return mutations_.run_modeling_command(
      std::make_unique<MoveSelectionHistoryCommand>(session.modeling(), delta),
      "Move Selection");
}

QuaderApiVoidResult QuaderModelingApi::rotate_selection(QVec3 euler_radians,
                                                        QVec3 pivot) {
  return transform_selection(rotation_transform(euler_radians), pivot);
}

QuaderApiVoidResult QuaderModelingApi::scale_selection(QVec3 scale,
                                                       QVec3 pivot) {
  return transform_selection(scale_transform(scale), pivot);
}

QuaderApiVoidResult
QuaderModelingApi::transform_selection(const QuaderTransform3 &transform,
                                       QVec3 pivot) {
  ProjectSession &session = project_session(session_);
  if (entity_selection_owns_transforms(session)) {
    SceneTransformDragService transforms;
    return mutations_.run_document_result(
        transforms.transform_selected_by_world_delta(
            session.document_commands(), session.scene(),
            world_transform_delta_from_api_transform(transform, pivot),
            "Transform Selection"),
        "Transform Selection");
  }

  return mutations_.run_modeling_command(
      std::make_unique<TransformSelectionHistoryCommand>(
          session.modeling(), to_modeling_transform3(transform), pivot),
      "Transform Selection");
}

bool QuaderModelingApi::can_duplicate_selected_entity_hierarchy() const {
  QDR_PROFILE_FUNCTION();
  const ProjectSession &session = project_session(session_);
  const QuaderSelectionSummary selection = selection_summary();
  if (!selection.has_selection ||
      (selection.mode != QuaderSelectionMode::Object &&
       selection.mode != QuaderSelectionMode::Group)) {
    return false;
  }

  const SceneDocument &scene = session.scene();
  for (const EntityId entity : scene.selected_entities()) {
    if (!scene.is_scene_root(entity) && !scene.is_effectively_locked(entity)) {
      return true;
    }
  }
  return false;
}

QuaderApiVoidResult QuaderModelingApi::duplicate_selected_entity_hierarchy() {
  QDR_PROFILE_FUNCTION();
  ProjectSession &session = project_session(session_);
  EditorTransaction transaction =
      mutations_.begin_transaction("Duplicate Selection");
  if (!transaction.active()) {
    return mutations_.run_document_command(
        command_invalid_result("Cannot duplicate selection while another "
                               "editor transaction is active."),
        "Duplicate Selection");
  }
  const ModelingCommandResult duplicate = [&]() {
    QDR_PROFILE_SCOPE("Duplicate selected entity hierarchy owner");
    return session.modeling().duplicate_selected_entity_hierarchy(
        session.document_commands());
  }();
  if (!duplicate.ok) {
    const CommandResult cancel = transaction.cancel();
    if (!cancel.ok) {
      return mutations_.run_document_command(cancel, "Duplicate Selection");
    }
    return mutations_.run_modeling_result(duplicate, "Duplicate Selection");
  }
  if (!duplicate.changed) {
    const CommandResult cancel = transaction.cancel();
    if (!cancel.ok) {
      return mutations_.run_document_command(cancel, "Duplicate Selection");
    }
    return mutations_.run_modeling_result(duplicate, "Duplicate Selection");
  }

  {
    QDR_PROFILE_SCOPE("Duplicate selection commit transaction");
    return mutations_.run_document_command(transaction.commit(),
                                           "Duplicate Selection");
  }
}

QuaderApiVoidResult
QuaderModelingApi::duplicate_selected_hierarchy_for_group_drag() {
  QDR_PROFILE_FUNCTION();
  ProjectSession &session = project_session(session_);
  EditorTransaction transaction =
      mutations_.begin_transaction("Duplicate Group Selection");
  if (!transaction.active()) {
    return mutations_.run_document_command(
        command_invalid_result("Cannot duplicate selection while another "
                               "editor transaction is active."),
        "Duplicate Group Selection");
  }

  const ModelingCommandResult duplicate = [&]() {
    QDR_PROFILE_SCOPE("Duplicate group hierarchy owner");
    return session.modeling().duplicate_selected_hierarchy_for_group_drag(
        session.document_commands());
  }();
  if (!duplicate.ok) {
    const CommandResult cancel = transaction.cancel();
    if (!cancel.ok) {
      return mutations_.run_document_command(cancel,
                                             "Duplicate Group Selection");
    }
    return mutations_.run_modeling_result(duplicate,
                                          "Duplicate Group Selection");
  }
  if (!duplicate.changed) {
    const CommandResult cancel = transaction.cancel();
    if (!cancel.ok) {
      return mutations_.run_document_command(cancel,
                                             "Duplicate Group Selection");
    }
    return mutations_.run_modeling_result(duplicate,
                                          "Duplicate Group Selection");
  }

  {
    QDR_PROFILE_SCOPE("Duplicate group commit transaction");
    return mutations_.run_document_command(transaction.commit(),
                                           "Duplicate Group Selection");
  }
}

QuaderApiVoidResult
QuaderModelingApi::apply_active_material(std::uint32_t material_slot) {
  return mutations_.run_modeling_command(
      std::make_unique<ApplyActiveMaterialHistoryCommand>(
          project_session(session_).modeling(), material_slot),
      "Apply Active Material");
}

QuaderApiVoidResult QuaderModelingApi::assign_material_slot_to_selection(
    std::uint32_t material_slot) {
  return mutations_.run_modeling_command(
      std::make_unique<AssignMaterialSlotToSelectionHistoryCommand>(
          project_session(session_).modeling(), material_slot),
      "Assign Material Slot");
}

QuaderApiVoidResult
QuaderModelingApi::assign_material_slot_to_face(QuaderModelingObjectRef object,
                                                QuaderElementId face,
                                                std::uint32_t material_slot) {
  return mutations_.run_modeling_command(
      std::make_unique<AssignMaterialSlotToFaceHistoryCommand>(
          project_session(session_).modeling(), object.modeling_object_id, face,
          material_slot),
      "Assign Face Material Slot");
}

QuaderActionSet
QuaderModelingApi::available_action_set(const QuaderActionQuery &query) const {
  return to_api_action_set(
      project_session(session_).modeling().context_action_set(
          to_modeling_action_query(query)));
}

std::vector<QuaderActionItem>
QuaderModelingApi::available_actions(const QuaderActionQuery &query) const {
  const std::vector<ModelingActionItem> actions =
      project_session(session_).modeling().context_actions(
          to_modeling_action_query(query));
  std::vector<QuaderActionItem> result;
  result.reserve(actions.size());
  for (const ModelingActionItem &action : actions) {
    result.push_back(to_api_action_item(action));
  }
  return result;
}

std::vector<QuaderInstantOperationDescriptor>
QuaderModelingApi::instant_operations() const {
  const std::vector<ModelingInstantOperationDescriptor> operations =
      project_session(session_).modeling().instant_operations();
  std::vector<QuaderInstantOperationDescriptor> result;
  result.reserve(operations.size());
  for (const ModelingInstantOperationDescriptor &operation : operations) {
    result.push_back(to_api_instant_operation_descriptor(operation));
  }
  return result;
}

QuaderSelectionSummary QuaderModelingApi::selection_summary() const {
  return to_api_selection_summary(
      project_session(session_).selection().summary());
}

std::vector<QuaderModelingObjectRecord> QuaderModelingApi::objects() const {
  const std::vector<ModelingObjectRecord> records =
      project_session(session_).modeling().objects();
  std::vector<QuaderModelingObjectRecord> result;
  result.reserve(records.size());
  for (const ModelingObjectRecord &record : records) {
    result.push_back(to_api_modeling_object_record(record));
  }
  return result;
}

QuaderModelingObjectRef QuaderModelingApi::ref_from_record(
    const QuaderModelingObjectRecord &record) const {
  QuaderModelingObjectRef ref;
  ref.entity = record.entity;
  ref.entity_uuid = project_session(session_).scene().uuid(record.entity);
  ref.editor_object_id =
      project_session(session_).scene().editor_object_id(record.entity);
  ref.modeling_object_id = record.modeling_object_id;
  ref.mesh = record.mesh;
  if (const MaterialComponent *material =
          project_session(session_).scene().material(record.entity)) {
    ref.material = material->material;
  }
  return ref;
}

} // namespace quader
