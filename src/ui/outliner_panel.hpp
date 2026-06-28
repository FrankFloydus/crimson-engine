////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "ui/ui_dock_panel.hpp"

namespace quader {

/**
 * Owns Outliner Panel behavior within the ImGui editor presentation layer.
 */
class OutlinerPanel final : public UIDockPanel {
public:
    UIPanelId id() const override { return UIPanelId::Outliner; }

private:
    float minimumWidth(const UIContext& context) const override;
    void drawContent(UIContext& context) override;

    EntityId outlinerAnchor_ = InvalidEntityId;
};

} // namespace quader
