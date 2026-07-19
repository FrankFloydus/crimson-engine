////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/document/document_command_result.hpp"

namespace quader {

class AssetRegistry;
class DocumentCommandService;
class SceneDocument;

/// Selects the world axis used by a native mesh arrangement operation.
enum class MeshArrangementAxis {
  X,
  Y,
  Z,
};

/// Selects the world-AABB coordinate matched by native mesh alignment.
enum class MeshAlignmentMode {
  Minimum,
  Center,
  Maximum,
};

/// Selects center or signed-gap spacing for native mesh distribution.
enum class MeshDistributionMode {
  Centers,
  Gaps,
};

/// Configures optional per-object delta snapping for mesh arrangement.
struct MeshArrangementOptions {
  bool snap_to_grid = false;
  double grid_size = 1.0;
};

/// Captures Object-mode and active-mesh state supplied by the modeling host.
struct MeshArrangementSelectionContext {
  bool object_mode = false;
  EntityId active_mesh = kInvalidEntityId;
};

/// Summarizes selection validation shared by every arrangement command.
struct MeshArrangementAvailability {
  bool can_align = false;
  bool can_distribute = false;
};

/// Arranges selected scene mesh roots through atomic document transforms.
class MeshArrangementService final {
public:
  MeshArrangementService(SceneDocument &scene, const AssetRegistry &assets,
                         MeshArrangementSelectionContext selection_context,
                         DocumentCommandService &document_commands);

  [[nodiscard]] MeshArrangementAvailability availability() const;
  [[nodiscard]] bool can_align_selected_meshes() const;
  [[nodiscard]] bool can_distribute_selected_meshes() const;

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
