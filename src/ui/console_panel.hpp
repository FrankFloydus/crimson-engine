////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/console_model.hpp"
#include "ui/ui_dock_panel.hpp"

namespace quader {

/**
 * Owns Console Panel behavior within the ImGui editor presentation layer.
 */
class ConsolePanel final : public UIDockPanel {
public:
    UIPanelId id() const override { return UIPanelId::Console; }

private:
  void draw_content(UIContext &context) override;

  ConsoleFilter filter_;
  bool auto_scroll_ = true;
};

} // namespace quader
