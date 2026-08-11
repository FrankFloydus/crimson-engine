////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/session/scene_session.hpp"

#include "diagnostics/profile.hpp"
#include "editor/array/linear_array_tool_service.hpp"
#include "editor/array/radial_array_tool_service.hpp"
#include "editor/commands/asset_command_service.hpp"
#include "editor/instances/scene_instance_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/pivot/pivot_tool_service.hpp"
#include "editor/placement/viewport_point_placement_service.hpp"
#include "editor/selection/editor_selection_service.hpp"
#include "editor/tools/native_viewport_tool_coordinator.hpp"
#include "editor/session/viewport_local_visibility_service.hpp"
#include "editor/modeling/services/primitive_creation_tool_service.hpp"

#include <utility>

namespace quader {

SceneSession::SceneSession() {
  (void)document_.ensure_scene_root();
  rebuild_services();
  mark_clean();
}

SceneSession::SceneSession(SceneContentDocument document)
    : document_(std::move(document)) {
  (void)document_.ensure_scene_root();
  rebuild_services();
  mark_clean();
}

SceneSession::~SceneSession() = default;

void SceneSession::rebuild_services() {
  QDR_PROFILE_FUNCTION();
  instance_edit_context_.clear();
  history_ = std::make_unique<EditorHistoryService>();
  document_commands_ = std::make_unique<DocumentCommandService>(
      document_.scene(), *history_, &document_.assets());
  asset_commands_ = std::make_unique<AssetCommandService>(
      document_.assets(), *history_, &document_.scene());
  modeling_ = std::make_unique<ModelingSessionService>(
      document_.scene(), document_.assets(),
      ModelingOperationPreviewCheckpointHooks{
          .begin = [this]() { begin_modeling_preview_checkpoint(); },
          .observe_before_projection = [this]() {
            observe_before_modeling_preview_projection();
          },
          .record_after_projection = [this]() {
            record_after_modeling_preview_projection();
          },
          .finish = [this](bool restored_by_cancel) {
            finish_modeling_preview_checkpoint(restored_by_cancel);
          },
      });
  selection_ = std::make_unique<EditorSelectionService>(
      document_.scene(), *document_commands_, *modeling_);
  instances_ = std::make_unique<SceneInstanceService>(
      document_.scene(), document_.assets(), *document_commands_, *history_,
      *modeling_, instance_edit_context_, document_.name());
  modeling_->set_instance_edit_scope_provider([this]() {
    if (!instances_) {
      return ModelingInstanceEditScope{};
    }
    const SceneInstanceEditState state = instances_->instance_edit_state();
    return ModelingInstanceEditScope{
        .occurrence = state.occurrence,
        .definition = state.definition,
    };
  });
  selection_->set_scene_selection_scope_policy(
      [this](EntityId entity) {
        return instances_->resolve_selection_target(entity);
      },
      [this](EntityId entity) {
        return instances_->is_selectable_in_current_scope(entity) &&
               (!viewport_local_visibility_ ||
                viewport_local_visibility_->allows_selection_entity(entity));
      });
  pivot_ = std::make_unique<PivotToolService>(
      document_.scene(), document_.assets(), *history_, *modeling_);
  linear_array_ = std::make_unique<LinearArrayToolService>(
      document_.scene(), document_.assets(), *history_, *modeling_,
      *selection_, *instances_);
  radial_array_ = std::make_unique<RadialArrayToolService>(
      document_.scene(), document_.assets(), *history_, *modeling_,
      *selection_, *instances_);
  primitive_creation_ = std::make_unique<PrimitiveCreationToolService>(
      *history_, *modeling_, ModelingOperationPreviewCheckpointHooks{
                                  .begin = [this]() {
                                    begin_modeling_preview_checkpoint();
                                  },
                                  .observe_before_projection = [this]() {
                                    observe_before_modeling_preview_projection();
                                  },
                                  .record_after_projection = [this]() {
                                    record_after_modeling_preview_projection();
                                  },
                                  .finish = [this](bool restored_by_cancel) {
                                    finish_modeling_preview_checkpoint(
                                        restored_by_cancel);
                                  },
                              });
  selection_->set_scene_selection_history_suppression_provider([this]() {
    return primitive_creation_ && primitive_creation_->active();
  });
  point_placement_ = std::make_unique<ViewportPointPlacementService>();
  native_tools_ = std::make_unique<NativeViewportToolCoordinator>(
      *pivot_, *linear_array_, *radial_array_, *primitive_creation_,
      *selection_, *modeling_);
  viewport_local_visibility_ =
      std::make_unique<ViewportLocalVisibilityService>(
          document_.scene(), document_.assets(), *modeling_, *instances_,
          *native_tools_, viewport_workspace_);
  history_->set_change_observer([this]() {
    if (viewport_local_visibility_) {
      viewport_local_visibility_->synchronize_scene_lifecycle();
    }
  });
  modeling_->set_viewport_visibility_filter(
      [this](EntityId entity) {
        return viewport_local_visibility_->allows_selection_entity(entity);
      },
      [this]() { return viewport_local_visibility_->revision(); },
      [this]() { return viewport_local_visibility_->active(); });
  point_placement_->set_viewport_visibility_filter(
      [this](EntityId entity) {
        return viewport_local_visibility_->allows_selection_entity(entity);
      },
      [this]() { return viewport_local_visibility_->revision(); });
  document_commands_->set_entity_mutation_blocker([this](EntityId entity) {
    return native_tools_ && native_tools_->blocks_document_mutation(entity);
  });
  // A SceneSession is the installed editor/runtime owner for its document.
  // Rebuild the portable modeling state and entity map immediately so a
  // document supplied by project/QDR loading is pickable on its first frame.
  // Empty/new documents take the same path and simply produce an empty
  // modeling session.
  modeling_->reset_from_scene();
}

SceneSessionReadView SceneSession::read_view() const {
  return {
      .document = document_,
      .scene = document_.scene(),
      .assets = document_.assets(),
      .modeling = *modeling_,
      .selection = *selection_,
  };
}

SceneContentRevisionStamp SceneSession::content_revision_stamp() const {
  return {
      .project_revision = document_.revision(),
      .scene_revision = document_.scene().content_revision(),
      .asset_revision = document_.assets().revision(),
      .modeling_content_revision = modeling_->content_revision(),
      .viewport_camera_pose_signature =
          viewport_workspace_
              .persisted_perspective_camera_pose_signature(),
  };
}

bool SceneSession::dirty() const {
  const SceneContentRevisionStamp current = content_revision_stamp();
  if (preview_content_checkpoint_.has_value() &&
      preview_content_checkpoint_->began_clean &&
      !preview_content_checkpoint_->contaminated &&
      current == preview_content_checkpoint_->expected_stamp) {
    return false;
  }
  return !(current == clean_stamp_);
}

void SceneSession::begin_modeling_preview_checkpoint() {
  const SceneContentRevisionStamp current = content_revision_stamp();
  preview_content_checkpoint_ = PreviewContentCheckpoint{
      .expected_stamp = current,
      .began_clean = current == clean_stamp_,
  };
}

void SceneSession::observe_before_modeling_preview_projection() {
  if (!preview_content_checkpoint_.has_value()) {
    return;
  }
  if (!(content_revision_stamp() ==
        preview_content_checkpoint_->expected_stamp)) {
    preview_content_checkpoint_->contaminated = true;
  }
}

void SceneSession::record_after_modeling_preview_projection() {
  if (preview_content_checkpoint_.has_value()) {
    preview_content_checkpoint_->expected_stamp = content_revision_stamp();
  }
}

void SceneSession::finish_modeling_preview_checkpoint(
    bool restored_by_cancel) {
  if (!preview_content_checkpoint_.has_value()) {
    return;
  }

  const SceneContentRevisionStamp current = content_revision_stamp();
  if (!(current == preview_content_checkpoint_->expected_stamp)) {
    preview_content_checkpoint_->contaminated = true;
  }
  if (restored_by_cancel && preview_content_checkpoint_->began_clean &&
      !preview_content_checkpoint_->contaminated) {
    clean_stamp_ = current;
  }
  preview_content_checkpoint_.reset();
}

SceneAssetMutationResult
SceneSession::replace_standalone_mesh(AssetId mesh, MeshAsset replacement) {
  const AssetCommandResult result =
      asset_commands().replace_mesh(mesh, std::move(replacement));
  return {
      .ok = result.ok,
      .changed = result.changed,
      .message = result.message,
  };
}

void SceneSession::replace_document(SceneContentDocument document) {
  replace_document_impl(std::move(document), std::nullopt);
}

bool SceneSession::replace_document_with_persisted_viewport_camera(
    SceneContentDocument document,
    const std::optional<ViewportCameraPose> &camera_pose) {
  if (camera_pose.has_value() &&
      !viewport_camera_pose_is_valid(*camera_pose)) {
    return false;
  }
  replace_document_impl(std::move(document), camera_pose);
  return true;
}

void SceneSession::replace_document_impl(
    SceneContentDocument document,
    const std::optional<ViewportCameraPose> &camera_pose) {
  QDR_PROFILE_FUNCTION();
  const std::uint64_t previous_scene_revision = document_.scene().revision();
  const std::uint64_t previous_asset_revision = document_.assets().revision();
  viewport_local_visibility_->reset_for_document_replacement();
  modeling_->clear_transient_state_for_document_replacement();
  native_tools_->reset_transient_state();
  instance_edit_context_.clear();
  preview_content_checkpoint_.reset();
  document_ = std::move(document);
  {
    QDR_PROFILE_SCOPE("SceneSession replace ensure scene root");
    (void)document_.ensure_scene_root();
  }
  {
    QDR_PROFILE_SCOPE("SceneSession replace advance scene revision");
    document_.scene().advance_revision_for_replacement(previous_scene_revision);
  }
  {
    QDR_PROFILE_SCOPE("SceneSession replace advance asset revision");
    document_.assets().advance_revision_for_replacement(
        previous_asset_revision);
  }
  {
    QDR_PROFILE_SCOPE("SceneSession replace clear history");
    history_->clear();
  }
  {
    QDR_PROFILE_SCOPE("SceneSession replace reset modeling from scene");
    modeling_->reset_from_scene();
  }
  if (document_replacement_finalizer_) {
    QDR_PROFILE_SCOPE("SceneSession replace finalize native document");
    document_replacement_finalizer_(*this);
  }
  {
    QDR_PROFILE_SCOPE("SceneSession replace restore viewport camera");
    (void)restore_persisted_viewport_camera_pose(camera_pose);
  }
  {
    QDR_PROFILE_SCOPE("SceneSession replace mark clean");
    mark_clean();
  }
}

void SceneSession::set_document_replacement_finalizer(
    std::function<void(SceneSession &)> finalizer) {
  document_replacement_finalizer_ = std::move(finalizer);
}

void SceneSession::mark_clean() {
  clean_stamp_ = content_revision_stamp();
  preview_content_checkpoint_.reset();
}

void SceneSession::mark_clean_at(SceneContentRevisionStamp stamp) noexcept {
  clean_stamp_ = stamp;
  preview_content_checkpoint_.reset();
}

std::unique_ptr<SceneSession> SceneSessionService::create_session() const {
  return std::make_unique<SceneSession>();
}

std::unique_ptr<SceneSession>
SceneSessionService::create_session(SceneContentDocumentRecord record) const {
  return std::make_unique<SceneSession>(SceneContentDocument(std::move(record)));
}

std::unique_ptr<SceneSession>
SceneSessionService::open_session(SceneContentDocument document) const {
  return std::make_unique<SceneSession>(std::move(document));
}

} // namespace quader
