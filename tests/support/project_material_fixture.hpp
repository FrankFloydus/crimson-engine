////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/modeling_types.hpp"
#include "scene/components.hpp"

#include <optional>

namespace quader {

class ProjectSession;

/**
 * Provides low-level fixture setup for public project-material behavior tests.
 * Production behavior remains exercised exclusively through QuaderEditorApi.
 */
class ProjectMaterialFixture final {
public:
  /// Selects a deterministic modeling-publication boundary for rollback tests.
  enum class ProjectionFailurePoint {
    None,
    AfterSdkMutation,
    CommittedSceneNotify,
    BeforeCommit,
  };

  explicit ProjectMaterialFixture(ProjectSession &project);

  [[nodiscard]] bool set_material(EntityId entity,
                                  MaterialComponent material) const;
  [[nodiscard]] bool clear_material(EntityId entity) const;
  [[nodiscard]] bool set_visible(EntityId entity, bool visible) const;
  [[nodiscard]] bool set_selectable(EntityId entity, bool selectable) const;
  [[nodiscard]] bool set_selectability(EntityId entity, bool selectable) const;
  [[nodiscard]] bool bind_shared_mesh(EntityId entity, AssetId mesh) const;
  [[nodiscard]] bool set_projection_failure(ProjectionFailurePoint point) const;
  /// Supplies a drawn footprint without accepting or assigning its material.
  [[nodiscard]] bool prepare_primitive_footprint(float origin_x = 0.0f) const;
  [[nodiscard]] std::optional<QMat4>
  instance_member_world_transform(EntityId member, EntityId occurrence) const;
  [[nodiscard]] ModelingSelectionExport selection() const;

private:
  ProjectSession *project_ = nullptr;
};

} // namespace quader
