////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "ui/ui_dock_panel.hpp"

#include <memory>

namespace quader {

class SceneDocument;

/**
 * Owns Outliner Panel behavior within the ImGui editor presentation layer.
 */
class OutlinerPanel final : public UIDockPanel {
public:
    OutlinerPanel();
    ~OutlinerPanel() override;

    UIPanelId id() const override { return UIPanelId::Outliner; }

private:
    struct TreeCache;

    float minimum_width(const UIContext &context) const override;
    ImVec2 panel_window_padding(const UIContext &context) const override;
    ImGuiWindowFlags window_flags(const UIContext &context) const override;
    void draw_content(UIContext &context) override;
    const TreeCache &refresh_tree_cache(const SceneDocument &document);

    EntityId outliner_anchor_ = kInvalidEntityId;
    std::unique_ptr<TreeCache> tree_cache_;
};

} // namespace quader
