////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/session/scene_session.hpp"
#include "editor/session/tool_session_defaults.hpp"

#include "diagnostics/profile.hpp"
#include "editor/array/linear_array_tool_service.hpp"
#include "editor/array/path_array_tool_service.hpp"
#include "editor/array/radial_array_tool_service.hpp"
#include "editor/commands/asset_command_service.hpp"
#include "editor/commands/scene_structural_lifecycle_service.hpp"
#include "editor/curve/curve_tool_service.hpp"
#include "editor/instances/scene_instance_service.hpp"
#include "editor/modeling/services/modeling_legacy_local_frame_upgrade.hpp"
#include "editor/modeling/services/modeling_mesh_sync_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/pivot/pivot_tool_service.hpp"
#include "editor/transform/mesh_transform_bake_service.hpp"
#include "editor/placement/scene_surface_query.hpp"
#include "editor/placement/snap_to_floor_service.hpp"
#include "editor/placement/viewport_point_placement_service.hpp"
#include "editor/selection/editor_selection_service.hpp"
#include "editor/tools/light_creation_tool_service.hpp"
#include "editor/tools/extrude_tool_service.hpp"
#include "editor/tools/native_viewport_tool_coordinator.hpp"
#include "editor/tools/uv_peel_tool_service.hpp"
#include "editor/session/viewport_local_visibility_service.hpp"
#include "editor/modeling/services/primitive_creation_tool_service.hpp"

#include <utility>

namespace quader {
namespace {

void upgrade_legacy_modeling_local_frames(SceneContentDocument &document) {
  ModelingMeshSyncService mesh_sync;
  (void)ModelingLegacyLocalFrameUpgradeService::upgrade_scene(
      document.scene(), document.assets(), mesh_sync);
}

} // namespace

SceneSession::SceneSession()
    : SceneSession(std::make_shared<PathArraySessionDefaults>()) {}

SceneSession::SceneSession(SceneContentDocument document)
    : SceneSession(std::make_shared<PathArraySessionDefaults>(),
                   std::move(document)) {}

SceneSession::SceneSession(
    std::shared_ptr<PathArraySessionDefaults> path_array_defaults,
    std::shared_ptr<ExtrudeSessionDefaults> extrude_defaults,
    std::shared_ptr<ToolSessionDefaults> tool_defaults)
    : tool_defaults_(tool_defaults ? std::move(tool_defaults)
                                  : std::make_shared<ToolSessionDefaults>()),
      path_array_defaults_(std::move(path_array_defaults)),
      extrude_defaults_(extrude_defaults ? std::move(extrude_defaults)
                                        : std::make_shared<ExtrudeSessionDefaults>()) {
  if (!path_array_defaults_) {
    path_array_defaults_ = std::make_shared<PathArraySessionDefaults>();
  }
  rebuild_services();
  mark_clean();
}

SceneSession::SceneSession(
    std::shared_ptr<PathArraySessionDefaults> path_array_defaults,
    SceneContentDocument document,
    std::shared_ptr<ExtrudeSessionDefaults> extrude_defaults,
    std::shared_ptr<ToolSessionDefaults> tool_defaults)
    : document_(std::move(document)),
      tool_defaults_(tool_defaults ? std::move(tool_defaults)
                                  : std::make_shared<ToolSessionDefaults>()),
      path_array_defaults_(std::move(path_array_defaults)),
      extrude_defaults_(extrude_defaults ? std::move(extrude_defaults)
                                        : std::make_shared<ExtrudeSessionDefaults>()) {
  if (!path_array_defaults_) {
    path_array_defaults_ = std::make_shared<PathArraySessionDefaults>();
  }
  rebuild_services();
  mark_clean();
}

SceneSession::~SceneSession() {
  // Modeling teardown may cancel an unfinished drag. Disconnect callbacks while
  // viewport state and the enclosing project still have complete lifetimes.
  content_settled_observer_ = {};
  if (history_) {
    history_->set_content_settled_observer({});
    history_->set_change_observer({});
  }
  if (modeling_) {
    modeling_->detach_preview_checkpoint_hooks();
  }
}

void SceneSession::record_settings_presentation_origin(
    std::string_view target_id, ViewportPaneId pane) {
  if (target_id.empty()) {
    return;
  }
  ++settings_presentation_origin_.serial;
  if (settings_presentation_origin_.serial == 0) {
    ++settings_presentation_origin_.serial;
  }
  settings_presentation_origin_.target_id.assign(target_id);
  settings_presentation_origin_.pane = pane;
}

void SceneSession::rebuild_services() {
  QDR_PROFILE_FUNCTION();
  invalidate_document_binding();
  instance_edit_context_.clear();
  history_ = std::make_unique<EditorHistoryService>();
  history_->set_max_steps(history_max_steps_);
  document_commands_ = std::make_unique<DocumentCommandService>(
      document_.scene(), *history_, &document_.assets());
  document_commands_->set_property_preview_hooks({
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
          .rollback_without_checkpoint = [this] {
            if (!preview_content_checkpoint_.has_value() &&
                content_settled_observer_) {
              content_settled_observer_(false);
            }
          },
      }, &tool_defaults_->modeling, &tool_defaults_->uv_peel);
  selection_ = std::make_unique<EditorSelectionService>(
      document_.scene(), *document_commands_, *modeling_);
  structural_lifecycle_ =
      std::make_unique<SceneStructuralLifecycleService>(
          document_.scene(), *history_, *document_commands_, *modeling_,
          selection_.get());
  document_commands_->set_selection_publication([this]() {
    if (!selection_) {
      return CommandResult{
          .ok = false,
          .message = "Editor selection authority is unavailable.",
      };
    }
    return selection_->publish_scene_selection();
  });
  instances_ = std::make_unique<SceneInstanceService>(
      document_.scene(), document_.assets(), *document_commands_, *history_,
      *modeling_, *selection_, *structural_lifecycle_, instance_edit_context_,
      document_.name());
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
      document_.scene(), document_.assets(), *history_, *modeling_,
      &tool_defaults_->pivot);
  mesh_transform_bake_ = std::make_unique<MeshTransformBakeService>(
      document_.scene(), document_.assets(), *history_, *modeling_);
  linear_array_ = std::make_unique<LinearArrayToolService>(
      document_.scene(), document_.assets(), *history_, *modeling_,
      *selection_, *instances_, &tool_defaults_->linear_array);
  radial_array_ = std::make_unique<RadialArrayToolService>(
      document_.scene(), document_.assets(), *history_, *modeling_,
      *selection_, *instances_, *structural_lifecycle_,
      &tool_defaults_->radial_array);
  curve_ = std::make_unique<CurveToolService>(
      document_.scene(), document_.assets(), *history_, *modeling_,
      *selection_, *document_commands_, &tool_defaults_->curve);
  selection_->set_sub_selection_provider(curve_.get());
  path_array_ = std::make_unique<PathArrayToolService>(
      document_.scene(), document_.assets(), *history_, *modeling_,
      *selection_, *document_commands_, *structural_lifecycle_,
      *path_array_defaults_);
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
                              }, &tool_defaults_->primitive);
  selection_->set_scene_selection_history_suppression_provider([this]() {
    return primitive_creation_ && primitive_creation_->active();
  });
  light_creation_ = std::make_unique<LightCreationToolService>();
  uv_peel_ = std::make_unique<UvPeelToolService>(*history_, *selection_, *modeling_);
  extrude_ = std::make_unique<ExtrudeToolService>(
      document_.scene(), *modeling_, *history_, *extrude_defaults_);
  selection_->set_selection_edit_policy(
      [this]() -> ModelingCommandResult {
        if (extrude_->preview_active()) {
          return {.ok = false,
                  .message = "Apply or cancel the extrusion before changing selection."};
        }
        return {.ok = true};
      },
      [this]() -> ModelingCommandResult {
        if (!extrude_->active() || extrude_->preview_active()) {
          return {.ok = true};
        }
        const CommandResult begun = extrude_->begin_selection_preview();
        return {.ok = begun.ok, .changed = begun.changed,
                .message = begun.message};
      });
  native_tools_ = std::make_unique<NativeViewportToolCoordinator>(
      *pivot_, *linear_array_, *radial_array_, *path_array_,
      *primitive_creation_, *light_creation_,
      *selection_, *modeling_, *curve_, *uv_peel_, *extrude_);
  viewport_local_visibility_ =
      std::make_unique<ViewportLocalVisibilityService>(
          document_.scene(), document_.assets(), *modeling_, *instances_,
          *native_tools_, viewport_workspace_,
          [this]() { (void)selection_->clear_hover(); });
  scene_surface_query_ = std::make_unique<SceneSurfaceQuery>(
      document_.scene(), document_.assets(), *viewport_local_visibility_);
  point_placement_ = std::make_unique<ViewportPointPlacementService>(
      *scene_surface_query_);
  snap_to_floor_ = std::make_unique<SnapToFloorService>(
      document_.scene(), document_.assets(), *document_commands_, *modeling_,
      *instances_, *native_tools_, *viewport_local_visibility_,
      *scene_surface_query_);
  history_->set_change_observer([this]() {
    if (modeling_) {
      modeling_->release_redundant_sdk_history();
    }
    if (viewport_local_visibility_) {
      viewport_local_visibility_->synchronize_scene_lifecycle();
    }
  });
  history_->set_content_settled_observer([this](bool committed) {
    if (!preview_content_checkpoint_.has_value() && content_settled_observer_) {
      content_settled_observer_(committed);
    }
  });
  modeling_->set_viewport_visibility_filter(
      [this](EntityId entity) {
        return viewport_local_visibility_->allows_selection_entity(entity);
      },
      [this]() { return viewport_local_visibility_->revision(); },
      [this]() { return viewport_local_visibility_->active(); });
  document_commands_->set_entity_mutation_blocker([this](EntityId entity) {
    return native_tools_ && native_tools_->blocks_document_mutation(entity);
  });
  // A SceneSession is the installed editor/runtime owner for its document.
  // Rebuild the portable modeling state and entity map immediately so a
  // document supplied by project/QDR loading is pickable on its first frame.
  // Empty/new documents take the same path and simply produce an empty
  // modeling session.
  upgrade_legacy_modeling_local_frames(document_);
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
  const bool committed =
      !restored_by_cancel || preview_content_checkpoint_->contaminated;
  preview_content_checkpoint_.reset();
  modeling_->release_redundant_sdk_history();
  if (content_settled_observer_) {
    content_settled_observer_(committed);
  }
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
  document_commands_->abandon_property_edit();
  document_ = std::move(document);
  invalidate_document_binding();
  scene_surface_query_->invalidate();
  point_placement_->invalidate();
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
    upgrade_legacy_modeling_local_frames(document_);
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

void SceneSession::invalidate_document_binding() {
  ++document_binding_generation_;
  if (document_binding_generation_ == 0) {
    ++document_binding_generation_;
  }
  settings_presentation_origin_ = {};
}

void SceneSession::set_document_replacement_finalizer(
    std::function<void(SceneSession &)> finalizer) {
  document_replacement_finalizer_ = std::move(finalizer);
}

void SceneSession::mark_clean() {
  clean_stamp_ = content_revision_stamp();
  preview_content_checkpoint_.reset();
}

void SceneSession::mark_clean_after_external_reference_update(
    SceneContentRevisionStamp previous_stamp) noexcept {
  if (!preview_content_checkpoint_.has_value() && clean_stamp_ == previous_stamp) {
    clean_stamp_ = content_revision_stamp();
  }
}

void SceneSession::set_content_settled_observer(std::function<void(bool)> observer) {
  content_settled_observer_ = std::move(observer);
}

void SceneSession::set_history_max_steps(std::size_t max_steps) {
  history_->set_max_steps(max_steps);
  history_max_steps_ = history_->max_steps();
  if (primitive_creation_) {
    primitive_creation_->apply_history_limit();
  }
}

void SceneSession::mark_clean_at(SceneContentRevisionStamp stamp) noexcept {
  clean_stamp_ = stamp;
  preview_content_checkpoint_.reset();
}

SceneSessionService::SceneSessionService()
    : path_array_defaults_(std::make_shared<PathArraySessionDefaults>()),
      extrude_defaults_(std::make_shared<ExtrudeSessionDefaults>()),
      tool_defaults_(std::make_shared<ToolSessionDefaults>()) {}

std::unique_ptr<SceneSession> SceneSessionService::create_session() const {
  return std::make_unique<SceneSession>(path_array_defaults_, extrude_defaults_,
                                       tool_defaults_);
}

std::unique_ptr<SceneSession>
SceneSessionService::create_session(SceneContentDocumentRecord record) const {
  return std::make_unique<SceneSession>(
      path_array_defaults_, SceneContentDocument(std::move(record)),
      extrude_defaults_, tool_defaults_);
}

std::unique_ptr<SceneSession>
SceneSessionService::open_session(SceneContentDocument document) const {
  return std::make_unique<SceneSession>(path_array_defaults_,
                                        std::move(document), extrude_defaults_,
                                        tool_defaults_);
}

} // namespace quader
