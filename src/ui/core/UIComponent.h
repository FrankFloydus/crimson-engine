////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/core/UIElement.h"

namespace quader {

/**
 * Base interface for retained UI components.
 *
 * Components own presentation state and produce backend-neutral UIElement
 * snapshots. Domain/editor mutations must still route through public editor
 * APIs, commands, or existing owner services.
 */
class UIComponent {
public:
    virtual ~UIComponent() = default;

    [[nodiscard]] virtual UIElement render() const = 0;
};

} // namespace quader
