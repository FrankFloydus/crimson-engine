////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/modeling_types.hpp"
#include "editor/modeling/runtime/editor_runtime.hpp"
#include "editor/modeling/runtime/selection_types.hpp"
#include "render/viewport_overlay_frame.hpp"

#include <cstdint>
#include <vector>

#include <editor/interactions/selection_edit.hpp>
#include <quader/modeling/neutral/polygon/document.hpp>

namespace quader_editor_core {
class EditorSession;
}

namespace quader {

class ModelingObjectRuntimeMap;
class ModelingSdkProjectionAdapter;
class ModelingSelectionService;
class SceneDocument;

/**
 * Caches Selection Overlay Cache derived state for the modeling application service layer.
 */
class SelectionOverlayCache {
public:
    void invalidate() const;

    [[nodiscard]] bool
    matches(std::uint64_t content_revision, std::uint64_t selection_revision,
            std::uint64_t scene_revision,
            std::uint64_t local_visibility_revision,
            quader_editor_core::SelectionMode selection_mode,
            std::uint64_t overlay_signature,
            const quader_editor_core::SelectionHoverState &hover) const;
    [[nodiscard]] const ViewportOverlayFrame& frame() const;
    void store(ViewportOverlayFrame frame, std::uint64_t content_revision,
               std::uint64_t selection_revision, std::uint64_t scene_revision,
               std::uint64_t local_visibility_revision,
               quader_editor_core::SelectionMode selection_mode,
               std::uint64_t overlay_signature,
               const quader_editor_core::SelectionHoverState &hover) const;
    [[nodiscard]] static bool
    hover_matches(const quader_editor_core::SelectionHoverState &left,
                  const quader_editor_core::SelectionHoverState &right);

  private:
    mutable bool valid_ = false;
    mutable std::uint64_t content_revision_ = 0;
    mutable std::uint64_t selection_revision_ = 0;
    mutable std::uint64_t scene_revision_ = 0;
    mutable std::uint64_t local_visibility_revision_ = 0;
    mutable quader_editor_core::SelectionMode selection_mode_ =
        quader_editor_core::SelectionMode::Object;
    mutable std::uint64_t overlay_signature_ = 0;
    mutable quader_editor_core::SelectionHoverState hover_;
    mutable ViewportOverlayFrame frame_;
};

/**
 * Coordinates Modeling Overlay Service responsibilities for the modeling application service layer.
 */
class ModelingOverlayService {
public:
  [[nodiscard]] ModelingToolStatus tool_status(
      const quader_editor_core::EditorRuntime &runtime,
      bool modal_tool_owns_viewport) const;
  [[nodiscard]] static bool has_modal_overlay(const ModelingToolStatus &status);
  void invalidate() const;
  void invalidate_composed_frame() const;
  [[nodiscard]] std::uint64_t selection_overlay_signature(
      const quader_editor_core::EditorRuntime &runtime,
      ModelingSelectionOverlayOptions options = {}) const;
  [[nodiscard]] std::uint64_t volatile_overlay_signature(
      const quader_editor_core::EditorRuntime &runtime) const;

  [[nodiscard]] bool selection_frame_matches(
      std::uint64_t content_revision, std::uint64_t selection_revision,
      std::uint64_t scene_revision,
      std::uint64_t local_visibility_revision,
      quader_editor_core::SelectionMode selection_mode,
      std::uint64_t overlay_signature,
      const quader_editor_core::SelectionHoverState &hover) const;
  [[nodiscard]] const ViewportOverlayFrame &selection_frame() const;
  void store_selection_frame(
      ViewportOverlayFrame frame, std::uint64_t content_revision,
      std::uint64_t selection_revision, std::uint64_t scene_revision,
      std::uint64_t local_visibility_revision,
      quader_editor_core::SelectionMode selection_mode,
      std::uint64_t overlay_signature,
      const quader_editor_core::SelectionHoverState &hover) const;
  [[nodiscard]] const quader_editor_core::SelectionOverlayFrame &
  selection_overlay_base_frame(
      const quader_editor_core::EditorSession &session,
      ModelingSelectionOverlayOptions options = {}) const;
  [[nodiscard]] quader_editor_core::SelectionOverlayFrame
  selection_overlay_current_frame(
      const quader_editor_core::EditorSession &session,
      const quader_editor_core::EditorRuntime &runtime,
      const quader_editor_core::SelectionHoverState &hover,
      ModelingSelectionOverlayOptions options = {}) const;
  [[nodiscard]] ViewportOverlayFrame selection_overlay_frame(
      const quader_editor_core::EditorSession &session,
      const quader_editor_core::EditorRuntime &runtime,
      const SceneDocument &scene,
      const ModelingObjectRuntimeMap &object_registry,
      const ModelingSelectionService &selection_service,
      const ModelingSdkProjectionAdapter &sdk_projection,
      ModelingSelectionOverlayOptions options = {}) const;
  [[nodiscard]] ViewportOverlayFrame volatile_overlay_frame(
      const quader_editor_core::EditorSession &session,
      const quader_editor_core::EditorRuntime &runtime,
      const SceneDocument &scene,
      const ModelingObjectRuntimeMap &object_registry,
      const ModelingSelectionService &selection_service,
      const ModelingSdkProjectionAdapter &sdk_projection) const;
  [[nodiscard]] std::vector<ModelingToolPreviewMesh>
  tool_preview_meshes(
      const quader_editor_core::EditorRuntime &runtime,
      const ModelingSdkProjectionAdapter &sdk_projection) const;

private:
    /**
     * Represents an Overlay Base Object Cache Entry value used by the modeling application service layer.
     */
    struct OverlayBaseObjectCacheEntry {
      int object_id = -1;
      std::uint64_t geometry_signature = 0;
      quader_editor_core::SelectionMode selection_mode =
          quader_editor_core::SelectionMode::Object;
      bool selected = false;
      bool component_edit_target = false;
      bool active_source_wire_object = false;
      quader_poly::Selection selection;
      quader_editor_core::SelectionOverlayFrame frame;
    };

    /**
     * Represents an Overlay Base Cache value used by the modeling application service layer.
     */
    struct OverlayBaseCache {
        bool valid = false;
        std::uint64_t content_revision = 0;
        std::uint64_t selection_revision = 0;
        int active_object_id = -1;
        quader_editor_core::SelectionMode selection_mode =
            quader_editor_core::SelectionMode::Object;
        bool suppress_persistent_object_selection = false;
        quader_editor_core::SelectionOverlayFrame frame;
        std::vector<OverlayBaseObjectCacheEntry> object_entries;
    };

    /// Retains the expensive world projection of the semantic selection base.
    struct ProjectedOverlayBaseCache {
      bool valid = false;
      std::uint64_t content_revision = 0;
      std::uint64_t selection_revision = 0;
      std::uint64_t scene_revision = 0;
      std::uint64_t local_visibility_revision = 0;
      std::uint64_t object_map_revision = 0;
      quader_editor_core::SelectionMode selection_mode =
          quader_editor_core::SelectionMode::Object;
      ModelingInstanceEditScope instance_edit_scope;
      bool suppress_persistent_object_selection = false;
      ViewportOverlayFrame frame;
    };

    mutable SelectionOverlayCache selection_overlay_cache_;
    mutable OverlayBaseCache overlay_base_cache_;
    mutable ProjectedOverlayBaseCache projected_overlay_base_cache_;
};

} // namespace quader
