////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/ui_panel.hpp"
#include "ui/ui_settings.hpp"

#include <memory>
#include <vector>

namespace quader {

/**
 * Owns UI Panel Manager behavior within the ImGui editor presentation layer.
 */
class UIPanelManager {
public:
    void add(std::unique_ptr<UIPanel> panel);
    void applySettings(const UISettings& settings);
    void writeSettings(UISettings& settings) const;

    bool isVisible(UIPanelId id) const;
    void setVisible(UIPanelId id, bool visible);
    UIPanel* panel(UIPanelId id);

    void draw(UIContext& context);
    const std::vector<std::unique_ptr<UIPanel>>& panels() const { return panels_; }

private:
    bool defaultVisibility(UIPanelId id) const;

    std::vector<std::unique_ptr<UIPanel>> panels_;
    std::vector<bool> visibility_;
};

} // namespace quader
