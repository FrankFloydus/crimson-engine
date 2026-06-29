////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
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
    EntityId primary = InvalidEntityId;
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
    float minimumWidth(const UIContext& context) const override;
    ImVec2 panelWindowPadding(const UIContext& context) const override;
    ImGuiWindowFlags windowFlags(const UIContext& context) const override;
    void drawContent(UIContext& context) override;

    std::optional<InspectorTransformEditState> activeTransformEdit_;
    UIContinuousEditTracker activeContinuousEdit_;
};

} // namespace quader
