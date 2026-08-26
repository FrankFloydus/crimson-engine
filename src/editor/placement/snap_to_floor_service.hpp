////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/document/document_command_result.hpp"
#include "editor/modeling/modeling_result_types.hpp"
#include "editor/placement/scene_surface_query.hpp"

namespace quader {

class AssetRegistry;
class DocumentCommandService;
class ModelingSessionService;
class NativeViewportToolCoordinator;
class SceneDocument;
class SceneInstanceService;
class ViewportLocalVisibilityService;

/// Coordinates native scene placement with portable selected-vertex mutation.
class SnapToFloorService final {
public:
  SnapToFloorService(
      SceneDocument &scene, const AssetRegistry &assets,
      DocumentCommandService &document_commands,
      ModelingSessionService &modeling, SceneInstanceService &instances,
      NativeViewportToolCoordinator &native_tools,
      const ViewportLocalVisibilityService &local_visibility);

  [[nodiscard]] bool can_snap_selected_meshes_to_floor() const;
  DocumentCommandResult snap_selected_meshes_to_floor();

  [[nodiscard]] bool can_snap_selected_vertices_to_floor() const;
  ModelingCommandResult snap_selected_vertices_to_floor();

  [[nodiscard]] SceneSurfaceQueryStats surface_query_stats() const noexcept;
  void invalidate_surface_cache() noexcept;

private:
  SceneDocument &scene_;
  const AssetRegistry &assets_;
  DocumentCommandService &document_commands_;
  ModelingSessionService &modeling_;
  SceneInstanceService &instances_;
  NativeViewportToolCoordinator &native_tools_;
  const ViewportLocalVisibilityService &local_visibility_;
  SceneSurfaceQuery surface_query_;
};

} // namespace quader
