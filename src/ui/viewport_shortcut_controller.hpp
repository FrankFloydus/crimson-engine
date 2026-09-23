////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/modeling_types.hpp"
#include "viewport/viewport_input_router.hpp"

namespace quader {

class QuaderEditorApi;

/**
 * Stores the Viewport Shortcut Controller Result data contract used by the Qt editor presentation layer.
 */
struct ViewportShortcutControllerResult {
    bool command_executed = false;
    bool invalidate_hover = false;
    bool mark_overlay_dirty = false;
};

/**
 * Coordinates Viewport Shortcut Controller state transitions for the Qt editor presentation layer.
 */
class ViewportShortcutController {
public:
  [[nodiscard]] ViewportShortcutControllerResult
  handle_modeling_shortcut(const ViewportInputResult &input_result,
                           QuaderEditorApi &editor) const;
};

} // namespace quader
