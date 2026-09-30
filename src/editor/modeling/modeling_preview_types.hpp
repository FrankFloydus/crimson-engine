////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/mesh_asset.hpp"
#include "assets/mesh_geometry_projection.hpp"
#include "core/types.hpp"
#include "math/types.hpp"
#include "scene/components.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace quader {

/**
 * Carries one renderer-neutral native modeling preview mesh.
 */
struct ModelingToolPreviewMesh {
  int source_object_id = -1;
  EntityId source_entity = kInvalidEntityId;
  // The public scene entity whose placement is being previewed. This differs
  // from source_entity when a canonical instance-definition member is shown
  // through one occurrence.
  EntityId visibility_entity = kInvalidEntityId;
  // Preserve the source entity's complete binding. A primary-only reference
  // loses per-primitive slot overrides and cannot faithfully represent a
  // project-owned material while the source renderable is suppressed.
  MaterialComponent material;
  // Source metadata and fallback geometry. A present geometry_projection owns
  // the effective compiled streams while this source remains immutable.
  MeshAsset mesh;
  QMat4 transform;
  std::vector<QMat4> instance_transforms;
  // Optional per-placement material sampling transforms. When populated, this
  // is parallel to instance_transforms and carries the exact texture-lock plan
  // that commit will author for every repeated placement.
  std::vector<QMat4> instance_sampling_from_local;
  // Source-less generated previews remain visible from either side by default.
  // Source-backed previews, including additive Mirror copies, must preserve
  // the source mesh component's culling policy so transient and committed
  // rendering agree.
  bool double_sided = true;
  bool cast_shadows = false;
  bool dynamic_positions = false;
  bool suppress_source = false;
  std::uint64_t layout_key = 0u;
  // Participant-local token for the packed position stream. A collection
  // update must leave this value and the payload unchanged for participants
  // whose packed positions did not change.
  std::uint64_t position_revision = 0u;
  // Position-only transform previews retain immutable topology in `mesh` and
  // publish one separately pinned packed position stream per revision. This
  // keeps render snapshots valid without cloning indices or primitive spans.
  std::shared_ptr<const std::vector<QVec3>> dynamic_position_payload;
  /// Canonical derived streams over the retained source mesh, when available.
  std::shared_ptr<const MeshGeometryProjection> geometry_projection;
};

/**
 * Immutable accepted coordinates for a retained component-overlay base.
 * These are derived committed presentation data, never an active tool preview.
 * Source revisions identify the frozen semantic frame whose element bindings
 * resolve through these complete, owned coordinate streams.
 */
struct ModelingAcceptedOverlayCoordinates final {
  std::uint64_t source_content_revision = 0u;
  std::uint64_t source_selection_revision = 0u;
  std::uint64_t source_scene_revision = 0u;
  std::uint64_t source_object_map_revision = 0u;
  std::vector<ModelingToolPreviewMesh> meshes;
};

/** Carries one renderer-neutral authored Curve preview participant. */
struct ModelingToolPreviewCurve final {
  EntityId source_entity = kInvalidEntityId;
  EntityId visibility_entity = kInvalidEntityId;
  AssetId curve = kInvalidAssetId;
  MaterialComponent material;
  std::vector<QMat4> instance_transforms;
  bool solidify = false;
  int solidify_resolution = kCurveSolidifyDefaultResolution;
  float solidify_radius = static_cast<float>(kCurveSolidifyDefaultRadius);
  bool suppress_source = false;
  std::uint64_t layout_key = 0u;
};

/** Carries one renderer-neutral authored Light preview participant. */
struct ModelingToolPreviewLight final {
  EntityId source_entity = kInvalidEntityId;
  EntityId visibility_entity = kInvalidEntityId;
  LightComponent light;
  std::vector<QMat4> instance_transforms;
  bool suppress_source = false;
  std::uint64_t layout_key = 0u;
};

/** Draws one source-root-local bounds cage at every preview placement. */
struct ModelingToolPreviewGroupMarker final {
  EntityId source_entity = kInvalidEntityId;
  QVec3 local_min;
  QVec3 local_max;
  std::vector<QMat4> instance_transforms;
  std::uint64_t layout_key = 0u;
};

/** Suppresses one canonical definition member in one public occurrence. */
struct ModelingToolPreviewOccurrenceSuppression final {
  EntityId source_entity = kInvalidEntityId;
  EntityId occurrence = kInvalidEntityId;

  friend bool operator==(const ModelingToolPreviewOccurrenceSuppression &,
                         const ModelingToolPreviewOccurrenceSuppression &) =
      default;
};

/** Explicit preview-only coverage for a replaced scene subtree. */
struct ModelingToolPreviewSuppression final {
  std::vector<EntityId> entities;
  std::vector<ModelingToolPreviewOccurrenceSuppression> occurrences;
};

/**
 * Publishes one compound, renderer-neutral tool preview generation.
 *
 * Scene entities remain authored and untouched. Render and selection adapters
 * consume the explicit suppression coverage while the tool owns this state.
 */
struct ModelingToolPreviewScene final {
  std::uint64_t revision = 0u;
  std::vector<ModelingToolPreviewMesh> meshes;
  std::vector<ModelingToolPreviewCurve> curves;
  std::vector<ModelingToolPreviewLight> lights;
  std::vector<ModelingToolPreviewGroupMarker> group_markers;
  ModelingToolPreviewSuppression suppression;
};

} // namespace quader
