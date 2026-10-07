////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modes/editor_mode_types.hpp"

namespace quader {

/** Declares the editor contexts in which an authored operation is meaningful. */
enum class EditorModeSupport { Object, Edit, ObjectAndEdit };

[[nodiscard]] constexpr bool editor_mode_supported(EditorModeSupport support,
                                                   EditorMode mode) noexcept {
  return (support == EditorModeSupport::ObjectAndEdit &&
          (mode == EditorMode::Object || mode == EditorMode::Edit)) ||
         (support == EditorModeSupport::Object && mode == EditorMode::Object) ||
         (support == EditorModeSupport::Edit && mode == EditorMode::Edit);
}

} // namespace quader
