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
#include "editor/modeling/services/modeling_history_bridge.hpp"
#include "editor/transform/scene_transform_drag_service.hpp"

#include <vector>

#include <editor/modeling/runtime/session.hpp>
#include <mesh/polygon/document.hpp>

namespace quader {

class AssetRegistry;
class EditorHistoryService;
class ModelingSessionMemento;
class SceneDocument;

/**
 * Port used by the transform drag owner for session-level sync, history, and cache hooks that
 * still live behind ModelingSessionService while the broad facade is being split.
 */
class ModelingTransformDragPort {
public:
  virtual ~ModelingTransformDragPort() = default;

  [[nodiscard]] virtual bool has_scene_transform_selection() const = 0;
  [[nodiscard]] virtual quader_editor_core::Bounds3
  transform_drag_bounds() const = 0;
  virtual void invalidate_selection_overlay_caches() const = 0;
  virtual void sync_scene_objects_from_modeling(bool force) = 0;
  virtual void sync_scene_selection_from_modeling() = 0;
  [[nodiscard]] virtual std::vector<ModelingObjectSceneIdentity>
  capture_selected_duplicate_scene_identities() const = 0;
  virtual void apply_scene_identities_to_selected_duplicates(
      const std::vector<ModelingObjectSceneIdentity> &source_identities) = 0;
  [[nodiscard]] virtual ModelingSessionMemento
  capture_active_preview_history_memento() const = 0;
  [[nodiscard]] virtual ModelingSessionMemento
  capture_history_memento() const = 0;
  virtual ModelingCommandResult
  restore_history_memento(const ModelingSessionMemento &memento) = 0;
  [[nodiscard]] virtual const std::vector<quader_editor_core::PickObjectBounds>
      &cached_pick_object_bounds() const = 0;
  [[nodiscard]] virtual const std::vector<
      quader_editor_core::PickObjectFaceGeometry> &
  cached_pick_face_geometry(quader_poly::PickOptions pick_options = {}) const =
      0;
};

/**
 * Bundles mutable modeling/session state used during a viewport transform drag.
 */
struct ModelingTransformDragServiceContext {
  SceneDocument &scene;
  AssetRegistry &assets;
  quader_editor_core::EditorSession &session;
  quader_editor_core::EditorRuntime &runtime;
  ModelingHistoryBridge &history_bridge;
  ModelingTransformDragPort &port;
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
};

} // namespace quader
