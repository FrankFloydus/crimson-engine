////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/document/document_command_edits.hpp"
#include "editor/commands/document/document_command_result.hpp"
#include "scene/scene_bounds.hpp"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace quader {

class AssetRegistry;
class DocumentCommandService;
class SceneDocument;

/// Selects the world axis used by a native object arrangement operation.
enum class MeshArrangementAxis {
  X,
  Y,
  Z,
};

/// Selects the world-AABB coordinate matched by native object alignment.
enum class MeshAlignmentMode {
  Minimum,
  Center,
  Maximum,
};

/// Retains legacy request values; both resolve to equal signed-gap spacing.
enum class MeshDistributionMode {
  Centers,
  Gaps,
};

/// Requests optional compatible-grid search; legacy grid_size is not a candidate.
struct MeshArrangementOptions {
  bool snap_to_grid = false;
  double grid_size = 1.0;
};

/// Captures Object mode; active-object metadata does not affect arrangement.
struct MeshArrangementSelectionContext {
  bool object_mode = false;
  EntityId active_object = kInvalidEntityId;
};

/// Summarizes selection validation shared by every arrangement command.
struct MeshArrangementAvailability {
  bool can_align = false;
  bool can_distribute = false;
};

/// Identifies one axis of an arrangement draft; None retains original placement.
enum class MeshArrangementMode {
  None,
  Minimum,
  Center,
  Maximum,
  Distribute,
};

/// Combines X, Y and Z intent against one immutable activation baseline.
struct MeshArrangementIntent {
  std::array<MeshArrangementMode, 3> axes{};

  [[nodiscard]] bool operator==(const MeshArrangementIntent &) const = default;
};

/// Captures one root's placement and the original world-to-editable channel basis.
struct MeshArrangementBaselineEntry {
  EntityId entity = kInvalidEntityId;
  SceneBounds bounds;
  TransformComponent transform;
  QMat4 channel_from_world;
};

/// Holds an operation-local immutable projection of eligible selected roots.
struct MeshArrangementBaseline {
  bool ok = false;
  std::string message;
  std::vector<MeshArrangementBaselineEntry> entries;
};

/// Supplies supported world-unit grid spacings independently of the active preset.
struct MeshArrangementSnapRequest {
  bool enabled = false;
  std::vector<double> candidate_grid_sizes;
};

/// Describes exact fallback when no supported grid preserves the arrangement.
[[nodiscard]] std::string_view mesh_arrangement_grid_alignment_warning();

/// Reports a pure transform plan without publishing scene state or history.
struct MeshArrangementTransformPlan {
  bool ok = false;
  std::string message;
  std::vector<DocumentTransformEdit> edits;
  bool snapping_requested = false;
  bool used_grid_alignment = false;
  std::optional<double> snap_grid_size;
  bool grid_alignment_unavailable = false;
};

/// Arranges selected scene spatial roots through atomic document transforms.
class MeshArrangementService final {
public:
  MeshArrangementService(SceneDocument &scene, const AssetRegistry &assets,
                         MeshArrangementSelectionContext selection_context,
                         DocumentCommandService &document_commands);

  [[nodiscard]] MeshArrangementAvailability availability() const;
  [[nodiscard]] bool can_align_selected_meshes() const;
  [[nodiscard]] bool can_distribute_selected_meshes() const;

  [[nodiscard]] MeshArrangementBaseline capture_baseline() const;
  [[nodiscard]] static MeshArrangementTransformPlan
  plan(const MeshArrangementBaseline &baseline, MeshArrangementIntent intent,
       const MeshArrangementSnapRequest &snapping = {});

  DocumentCommandResult
  align_selected_meshes(MeshArrangementAxis axis, MeshAlignmentMode alignment,
                        MeshArrangementOptions options = {});
  DocumentCommandResult
  distribute_selected_meshes(MeshArrangementAxis axis,
                             MeshDistributionMode distribution,
                             MeshArrangementOptions options = {});

private:
  SceneDocument &scene_;
  const AssetRegistry &assets_;
  MeshArrangementSelectionContext selection_context_;
  DocumentCommandService &document_commands_;
};

} // namespace quader
