////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/UIDockPanel.h"

namespace quader {

/**
 * Owns Inspector Panel behavior within the ImGui editor presentation layer.
 */
class InspectorPanel final : public UIDockPanel {
public:
    UIPanelId id() const override { return UIPanelId::Inspector; }

private:
    float minimumWidth(const UIContext& context) const override;
    void drawContent(UIContext& context) override;
};

} // namespace quader
