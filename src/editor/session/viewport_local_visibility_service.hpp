////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "viewport/viewport_local_visibility.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace quader {

class AssetRegistry;
class ModelingSessionService;
class NativeViewportToolCoordinator;
class SceneDocument;
class SceneInstanceService;
class ViewportWorkspaceService;

/**
 * Reports one editor-owned Local Visibility state transition.
 */
struct ViewportLocalVisibilityMutation final {
  bool ok = true;
  bool changed = false;
  std::string message;
};

/**
 * Maps editor selection and scene lifecycle state into the runtime-free
 * viewport Local Visibility snapshot owned by ViewportWorkspaceService.
 */
class ViewportLocalVisibilityService final {
public:
  using ResolvedHoverClearer = std::function<void()>;

  ViewportLocalVisibilityService(
      SceneDocument &scene, AssetRegistry &assets,
      ModelingSessionService &modeling, SceneInstanceService &instances,
      NativeViewportToolCoordinator &native_tools,
      ViewportWorkspaceService &workspace,
      ResolvedHoverClearer clear_resolved_hover);

  [[nodiscard]] bool active() const;
  [[nodiscard]] bool can_enter() const;
  [[nodiscard]] bool can_toggle() const;
  [[nodiscard]] bool
  allows(ViewportLocalVisibilityEntityId entity) const;
  [[nodiscard]] bool
  allows_selection_entity(ViewportLocalVisibilityEntityId entity) const;
  [[nodiscard]] std::uint64_t revision() const;
  [[nodiscard]] ViewportLocalVisibilitySnapshot snapshot() const;
  void synchronize_scene_lifecycle() const;

  [[nodiscard]] ViewportLocalVisibilityMutation toggle();
  void reset_for_document_replacement();

private:
  void reconcile_scene_lifecycle() const;
  [[nodiscard]] bool transient_interaction_active() const;
  [[nodiscard]] bool component_selection_can_enter() const;

  SceneDocument &scene_;
  AssetRegistry &assets_;
  ModelingSessionService &modeling_;
  SceneInstanceService &instances_;
  NativeViewportToolCoordinator &native_tools_;
  ViewportWorkspaceService &workspace_;
  ResolvedHoverClearer clear_resolved_hover_;
  mutable std::uint64_t last_reconciled_scene_content_revision_ = 0u;
  mutable std::uint64_t last_reconciled_workspace_revision_ = 0u;
  mutable bool lifecycle_reconcile_valid_ = false;
  mutable std::uint64_t component_availability_selection_revision_ = 0u;
  mutable std::uint64_t component_availability_object_map_revision_ = 0u;
  mutable std::uint64_t component_availability_visibility_revision_ = 0u;
  mutable std::uint64_t component_availability_layer_revision_ = 0u;
  mutable bool component_availability_value_ = false;
  mutable bool component_availability_valid_ = false;
};

} // namespace quader
