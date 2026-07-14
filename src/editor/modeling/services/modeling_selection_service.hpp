////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/modeling_types.hpp"
#include "editor/modeling/picking/modeling_picking_cache.hpp"
#include "editor/modeling/runtime/modeling_object_runtime_map.hpp"

#include <cstdint>
#include <optional>
#include <vector>

#include <editor/interactions/selection_box.hpp>
#include <editor/modeling/runtime/session.hpp>
#include <mesh/polygon/document.hpp>

namespace quader {

class SceneDocument;

/**
 * Reports whether a runtime selection application was attempted and changed
 * selection state.
 */
struct ModelingSelectionApplyResult {
  bool applied = false;
  bool changed = false;
};

/**
 * Coordinates Modeling Selection Service responsibilities for the modeling application service layer.
 */
class ModelingSelectionService {
public:
  [[nodiscard]] static bool
  has_component_selection(const ModelingSelectionSummary &summary);
  [[nodiscard]] ModelingSelectionMode
  selection_mode(const quader_editor_core::EditorSession &session,
                 ModelingSelectionMode app_selection_mode) const;
  [[nodiscard]] ModelingSelectionSummary
  selection_summary(const quader_editor_core::EditorSession &session,
                    const SceneDocument &scene,
                    ModelingSelectionMode app_selection_mode) const;

  void invalidate_picking_cache() const;
  [[nodiscard]] std::uint64_t
  transformed_picking_projection_build_count() const;
  [[nodiscard]] std::uint64_t
  rectangle_picking_projection_build_count() const;
  [[nodiscard]] std::uint64_t
  rectangle_selection_resolution_build_count() const;
  [[nodiscard]] const std::vector<quader_editor_core::PolygonObject> &
  cached_scene_transformed_objects_for_picking(
      const quader_editor_core::EditorSession &session,
      const SceneDocument &scene,
      const ModelingObjectRuntimeMap &object_map) const;
  [[nodiscard]] std::vector<quader_editor_core::PolygonObject>
  scene_transformed_objects_for_picking(
      const quader_editor_core::EditorSession &session,
      const SceneDocument &scene,
      const ModelingObjectRuntimeMap &object_map) const;
  [[nodiscard]] const std::vector<quader_editor_core::PickObjectBounds> &
  cached_pick_object_bounds(const quader_editor_core::EditorSession &session,
                            const SceneDocument &scene,
                            const ModelingObjectRuntimeMap &object_map) const;
  [[nodiscard]] const std::vector<quader_editor_core::PickObjectFaceGeometry> &
  cached_pick_face_geometry(const quader_editor_core::EditorSession &session,
                            const SceneDocument &scene,
                            const ModelingObjectRuntimeMap &object_map,
                            quader_poly::PickOptions pick_options = {}) const;
  [[nodiscard]] ModelingSelectionApplyResult
  apply_pick_selection(quader_editor_core::EditorSession &session,
                       const SceneDocument &scene,
                       const ModelingObjectRuntimeMap &object_map,
                       const ModelingPickSelectRequest &request) const;
  [[nodiscard]] std::optional<int>
  pick_object_id(const quader_editor_core::EditorSession &session,
                 const SceneDocument &scene,
                 const ModelingObjectRuntimeMap &object_map,
                 const ModelingPickSelectRequest &request) const;
  [[nodiscard]] quader_editor_core::RectangleSelectionState
  rectangle_selection_state(
      const quader_editor_core::EditorSession &session) const;
  [[nodiscard]] const quader_editor_core::RectangleSelectionUniverse &
  rectangle_selection_universe(
      const quader_editor_core::EditorSession &session) const;
  [[nodiscard]] quader_editor_core::RectangleProjectedHitsRequest
  rectangle_projected_hits_request(
      const std::vector<quader_editor_core::PolygonObject> &picking_objects,
      quader_editor_core::SelectionMode selection_mode,
      const ModelingBoxSelectRequest &request) const;
  [[nodiscard]] std::vector<int>
  box_pick_object_ids(const quader_editor_core::EditorSession &session,
                      const SceneDocument &scene,
                      const ModelingObjectRuntimeMap &object_map,
                      const ModelingBoxSelectRequest &request) const;
  [[nodiscard]] bool apply_rectangle_selection(
      quader_editor_core::EditorSession &session,
      const quader_editor_core::RectangleSelectionState &selection) const;
  [[nodiscard]] ModelingSelectionApplyResult apply_rectangle_selection(
      quader_editor_core::EditorSession &session, const SceneDocument &scene,
      const ModelingObjectRuntimeMap &object_map,
      const ModelingBoxSelectRequest &request,
      const quader_editor_core::RectangleSelectionState &baseline) const;
  ModelingCommandResult
  set_group_selection_mode(quader_editor_core::EditorSession &session,
                           ModelingSelectionMode &app_selection_mode) const;
  ModelingCommandResult
  set_runtime_selection_mode(quader_editor_core::EditorSession &session,
                             quader_editor_core::EditorRuntime &runtime,
                             ModelingSelectionMode mode) const;
  ModelingCommandResult set_component_selection(
      quader_editor_core::EditorSession &session,
      const ModelingComponentSelectionRequest &request) const;
  [[nodiscard]] static bool
  selection_matches(const quader_poly::Selection &left,
                    const quader_poly::Selection &right);

private:
  /**
   * Caches screen-space rectangle candidates for one geometry/camera key.
   */
  struct RectangleProjectionCache {
    bool valid = false;
    std::uint64_t content_revision = 0;
    std::uint64_t scene_transform_revision = 0;
    std::uint64_t scene_visibility_revision = 0;
    std::uint64_t scene_mesh_binding_revision = 0;
    std::uint64_t object_map_revision = 0;
    quader_editor_core::SelectionMode selection_mode =
        quader_editor_core::SelectionMode::Object;
    ModelingViewportCamera camera;
    quader_editor_core::RectangleProjectedHitsRequest request;
  };

  /**
   * Caches stable topology IDs used to resolve component hit sets.
   */
  struct RectangleUniverseCache {
    bool valid = false;
    std::uint64_t content_revision = 0;
    quader_editor_core::RectangleSelectionUniverse universe;
  };

  /**
   * Caches the last resolved live marquee hit set and its baseline.
   */
  struct RectangleResolutionCache {
    bool valid = false;
    std::uint64_t projected_candidates_revision = 0;
    std::uint64_t content_revision = 0;
    std::uint64_t selection_revision = 0;
    quader_editor_core::SelectionEditOperation operation =
        quader_editor_core::SelectionEditOperation::Replace;
    quader_editor_core::RectangleSelectionState baseline;
    quader_editor_core::RectangleSelectionHits hits;
  };

  [[nodiscard]] const quader_editor_core::RectangleProjectedHitsRequest &
  cached_rectangle_projected_hits_request(
      const quader_editor_core::EditorSession &session,
      const SceneDocument &scene,
      const ModelingObjectRuntimeMap &object_map,
      quader_editor_core::SelectionMode selection_mode,
      const ModelingBoxSelectRequest &request) const;

  mutable ModelingPickingCache picking_cache_;
  mutable RectangleProjectionCache rectangle_projection_cache_;
  mutable RectangleUniverseCache rectangle_universe_cache_;
  mutable RectangleResolutionCache rectangle_resolution_cache_;
  mutable std::uint64_t transformed_picking_projection_build_count_ = 0;
  mutable std::uint64_t rectangle_picking_projection_build_count_ = 0;
  mutable std::uint64_t rectangle_selection_resolution_build_count_ = 0;
};

} // namespace quader
