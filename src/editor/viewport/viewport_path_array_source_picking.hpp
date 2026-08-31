////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/picking/curve_picking_cache.hpp"
#include "editor/selection/editor_selection_service.hpp"
#include "editor/tools/viewport_native_tool_interaction.hpp"
#include "editor/viewport/viewport_interaction_frame.hpp"
#include "editor/viewport/viewport_scene_hit_policy.hpp"

#include <optional>
#include <span>

namespace quader {

/** Immutable provider inputs used by the modal Path Array source picker. */
struct PathArraySourcePickQuery final {
  const ViewportCameraController &camera;
  QVec2 local_mouse{};
  std::span<const ViewportSceneBillboardPickCandidate> billboards;
  std::span<const ViewportSceneCurvePickCandidate> curves;
  const SceneSurfaceQuerySnapshot *scene_surfaces = nullptr;
  std::span<const EntityId> excluded_entities;
  std::span<const ViewportSceneOccurrenceExclusion> excluded_occurrences;
  bool select_backfaces = true;
  double curve_radius_pixels = 8.0;
  SceneSurfaceQueryScratch *surface_scratch = nullptr;
};

/**
 * Resolves mesh, occurrence, Curve, and billboard providers through the same
 * depth policy as ordinary selection. Source-kind validation remains with the
 * tool.
 */
[[nodiscard]] inline std::optional<EntityId>
resolve_path_array_source_hit(const PathArraySourcePickQuery &query) {
  const std::optional<ViewportSceneHitCandidate> hit =
      resolve_viewport_scene_point_hit({
          .camera = query.camera,
          .local_mouse = query.local_mouse,
          .billboards = query.billboards,
          .curves = query.curves,
          .scene_surfaces = query.scene_surfaces,
          .excluded_entities = query.excluded_entities,
          .excluded_occurrences = query.excluded_occurrences,
          .include_mesh_surfaces = true,
          .select_backfaces = query.select_backfaces,
          .curve_radius_pixels = query.curve_radius_pixels,
          .surface_scratch = query.surface_scratch,
      });
  if (!hit.has_value()) {
    return std::nullopt;
  }
  const std::optional<ResolvedSelectionTarget> resolved =
      resolved_selection_target(*hit);
  const EntityId raw = resolved.has_value() ? resolved->entity : hit->entity;
  return raw != kInvalidEntityId ? std::optional<EntityId>{raw} : std::nullopt;
}

/** Applies the canonical group-promotion policy to a resolved provider hit. */
[[nodiscard]] inline std::optional<EntityId>
resolve_path_array_source_pick(const PathArraySourcePickQuery &query,
                               const EditorSelectionService &selection) {
  const std::optional<EntityId> raw = resolve_path_array_source_hit(query);
  if (!raw.has_value()) {
    return std::nullopt;
  }
  const EntityId promoted = selection.group_promoted_scene_target(*raw);
  return promoted != kInvalidEntityId ? std::optional<EntityId>{promoted}
                                      : std::nullopt;
}

/**
 * Frame-local adapter that keeps spatial-query details outside native tool
 * lifecycle code while reusing the canonical viewport hit policy.
 */
class ViewportPathArraySourcePickingAdapter final
    : public ViewportPathArraySourcePickingPort {
public:
  ViewportPathArraySourcePickingAdapter(
      const ViewportCameraController &camera,
      std::span<const ViewportSceneBillboardPickCandidate> billboards,
      std::span<const ViewportSceneCurvePickCandidate> curves,
      const SceneSurfaceQuerySnapshot *scene_surfaces)
      : camera_(&camera), billboards_(billboards), curves_(curves),
        scene_surfaces_(scene_surfaces) {}

  [[nodiscard]] std::optional<EntityId> pick(
      QVec2 mouse_local, std::span<const EntityId> excluded_entities,
      std::span<const SceneSurfaceOccurrenceExclusion> excluded_occurrences,
      bool select_backfaces) const override {
    return resolve_path_array_source_hit({
        .camera = *camera_,
        .local_mouse = mouse_local,
        .billboards = billboards_,
        .curves = curves_,
        .scene_surfaces = scene_surfaces_,
        .excluded_entities = excluded_entities,
        .excluded_occurrences = excluded_occurrences,
        .select_backfaces = select_backfaces,
        .surface_scratch = &surface_scratch_,
    });
  }

private:
  const ViewportCameraController *camera_ = nullptr;
  std::span<const ViewportSceneBillboardPickCandidate> billboards_;
  std::span<const ViewportSceneCurvePickCandidate> curves_;
  const SceneSurfaceQuerySnapshot *scene_surfaces_ = nullptr;
  mutable SceneSurfaceQueryScratch surface_scratch_;
};

} // namespace quader
