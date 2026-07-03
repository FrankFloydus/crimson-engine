////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "editor/transform/scene_transform_drag_service.hpp"
#include "ui/ui_continuous_edit_tracker.hpp"
#include "ui/ui_dock_panel.hpp"

#include <array>
#include <optional>
#include <vector>

namespace quader {

/**
 * Enumerates active Inspector transform fields with staged drag-edit values.
 */
enum class InspectorTransformEditKind {
    Position,
    Rotation,
    Scale,
};

/**
 * Stores one in-progress Inspector transform field edit before it is committed to history.
 */
struct InspectorTransformEditState {
    InspectorTransformEditKind kind = InspectorTransformEditKind::Position;
    EntityId primary = kInvalidEntityId;
    std::vector<EntityId> selection;
    std::array<float, 3> values{};
};

/**
 * Owns Inspector Panel behavior within the ImGui editor presentation layer.
 */
class InspectorPanel final : public UIDockPanel {
public:
    UIPanelId id() const override { return UIPanelId::Inspector; }

private:
  float minimum_width(const UIContext &context) const override;
  ImVec2 panel_window_padding(const UIContext &context) const override;
  ImGuiWindowFlags window_flags(const UIContext &context) const override;
  void draw_content(UIContext &context) override;
  void cancel_active_transform_edit();

  std::optional<InspectorTransformEditState> active_transform_edit_;
  SceneTransformDragService active_transform_preview_;
  UIContinuousEditTracker active_continuous_edit_;
};

} // namespace quader
