////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <cstdint>
#include <optional>

namespace quader {

/**
 * Supplies native hit-test pass-through for a display-only child overlay.
 * Call only for messages addressed to that overlay; all other messages retain
 * the host's normal handling. Native message types remain platform-owned.
 */
[[nodiscard]] std::optional<std::intptr_t>
platform_transparent_overlay_input_result(const void *native_message) noexcept;

/**
 * Identifies a real ancestor-window move or resize before native pixels move.
 * The child handle must already exist; its own geometry, visibility-only
 * messages, and unchanged native positions do not invalidate the overlay.
 */
[[nodiscard]] bool platform_native_ancestor_geometry_changes(
    const void *native_message, std::uintptr_t native_child_window) noexcept;

} // namespace quader
