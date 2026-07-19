////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/modeling_types.hpp"
#include "editor/modeling/runtime/modeling_object_runtime_map.hpp"
#include "editor/modeling/runtime_state/transform_drag_controller.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/transform/scene_transform_drag_service.hpp"
#include "math/types.hpp"

#include <optional>
#include <vector>

#include <editor/modeling/runtime/session.hpp>
#include <quader/modeling/neutral/polygon/document.hpp>

namespace quader_editor_core {
class EditorRuntime;
struct TransformDragState;
} // namespace quader_editor_core

namespace quader {

class AssetRegistry;
class DocumentCommandService;
class ModelingSessionMemento;
class ModelingSdkTransformHistoryDelta;
class SceneDocument;

/**
 * Port used by the transform drag owner for portable modeling previews and
 * host scene/history projection.
 */
class ModelingTransformDragPort {
public:
  virtual ~ModelingTransformDragPort() = default;

  [[nodiscard]] virtual bool has_scene_transform_selection() const = 0;
  [[nodiscard]] virtual bool has_portable_modeling_selection() const = 0;
  [[nodiscard]] virtual ModelingSelectionMode
  portable_modeling_selection_mode() const = 0;
  [[nodiscard]] virtual bool portable_modeling_preview_active() const = 0;
  /** True while a topology-stable transform is streamed as transient positions. */
  [[nodiscard]] virtual bool transient_transform_preview_active() const = 0;
  virtual ModelingCommandResult
  begin_portable_transform_preview(bool duplicate_selected,
                                   bool topology_stable_transform) = 0;
  virtual ModelingCommandResult update_portable_transform_preview(
      const quader_editor_core::TransformDragState &drag,
      double grid_size) = 0;
  virtual ModelingCommandResult commit_portable_translate_preview() = 0;
  virtual ModelingCommandResult cancel_portable_translate_preview() = 0;
  [[nodiscard]] virtual ModelingSdkTransformHistoryDelta
  take_committed_transform_history_delta() = 0;
  virtual ModelingCommandResult apply_transform_history_delta(
      const ModelingSdkTransformHistoryDelta &delta, bool use_after) = 0;
  virtual void record_portable_transform(
      const quader_editor_core::TransformDragState &drag,
      bool duplicate_selected) = 0;
  [[nodiscard]] virtual std::uint64_t modeling_content_revision() const = 0;
  [[nodiscard]] virtual std::uint64_t modeling_selection_revision() const = 0;
  [[nodiscard]] virtual QMat4
  scene_selection_display_from_scene() const = 0;
  [[nodiscard]] virtual quader_editor_core::Bounds3
  transform_drag_bounds() const = 0;
  virtual void invalidate_selection_overlay_caches() const = 0;
  virtual void sync_scene_selection_from_modeling() = 0;
  [[nodiscard]] virtual ModelingSessionMemento
  capture_active_preview_history_memento() const = 0;
  [[nodiscard]] virtual ModelingSessionMemento
  capture_history_memento() const = 0;
  virtual ModelingCommandResult
  restore_history_memento(const ModelingSessionMemento &memento) = 0;
  [[nodiscard]] virtual std::optional<QMat4>
  modeling_object_world_transform(int modeling_object_id) const = 0;
  [[nodiscard]] virtual const std::vector<
      quader_editor_core::PolygonObject> &
  cached_scene_transformed_objects_for_picking() const = 0;
  [[nodiscard]] virtual const std::vector<
      quader_editor_core::PolygonObject> &
  cached_local_objects_for_picking() const = 0;
  [[nodiscard]] virtual const std::vector<quader_editor_core::PickObjectBounds>
      &cached_pick_object_bounds() const = 0;
  [[nodiscard]] virtual const std::vector<
      quader_editor_core::PickObjectFaceGeometry> &
  cached_pick_face_geometry(quader_poly::PickOptions pick_options = {}) const =
      0;
  [[nodiscard]] virtual const std::vector<
      quader_editor_core::PickObjectFaceGeometry> &
  cached_local_pick_face_geometry(
      quader_poly::PickOptions pick_options = {}) const = 0;
  [[nodiscard]] virtual std::span<
      const quader_editor_core::PickObjectTransform>
  cached_local_picking_transforms() const = 0;
};

/**
 * Bundles mutable modeling/session state used during a viewport transform drag.
 */
struct ModelingTransformDragServiceContext {
  SceneDocument &scene;
  AssetRegistry &assets;
  /// Drag-lifetime projection borrowed by retained native input helpers.
  const quader_editor_core::EditorSession &session;
  quader_editor_core::EditorRuntime &runtime;
  ModelingTransformDragPort &port;
  DocumentCommandService *document_commands = nullptr;
  EditorHistoryService *history = nullptr;
};

/**
 * Owns viewport transform drag lifecycle orchestration for modeling/runtime and scene previews.
 */
class ModelingTransformDragService final {
public:
  [[nodiscard]] ModelingTransformDragPreview begin_transform_drag(
      ModelingTransformDragServiceContext context,
      const ModelingTransformDragBeginRequest &request);
  [[nodiscard]] ModelingTransformDragPreview update_transform_drag(
      ModelingTransformDragServiceContext context,
      const ModelingTransformDragUpdateRequest &request);
  [[nodiscard]] ModelingTransformDragPreview commit_transform_drag(
      ModelingTransformDragServiceContext context,
      EditorHistoryService *history = nullptr);
  ModelingCommandResult
  cancel_transform_drag(ModelingTransformDragServiceContext context);

  [[nodiscard]] bool preview_driven() const;
  [[nodiscard]] bool duplicate_selected() const;
  [[nodiscard]] bool scene_preview_active() const;

private:
  void clear_runtime_state(ModelingTransformDragServiceContext context);
  [[nodiscard]] ModelingTransformDragPreview transform_drag_preview(
      const ModelingTransformDragServiceContext &context, bool changed) const;
  [[nodiscard]] bool scene_preview_transform_for_drag(
      const quader_editor_core::TransformDragState &drag,
      quader_poly::Transform3 &out_transform) const;
  [[nodiscard]] ModelingTransformDragPreview apply_transform_drag_preview(
      ModelingTransformDragServiceContext context,
      const ModelingTransformDragUpdateRequest &request);

  TransformDragController controller_;
  SceneTransformDragService scene_transform_drag_service_;
  std::optional<EditorTransaction> scene_duplicate_transaction_;
  bool portable_scene_duplicate_active_ = false;
};

} // namespace quader
