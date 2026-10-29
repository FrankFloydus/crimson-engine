////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modes/editor_mode_types.hpp"
#include <string_view>

namespace quader {

[[nodiscard]] constexpr bool workspace_tool_allowed(EditorWorkspaceMode mode,std::string_view id) noexcept {
  return mode==EditorWorkspaceMode::Modeling||id=="select"||id=="lasso_select"||id=="paint_select"||id=="move"||id=="rotate"||id=="scale"||id=="light"||id=="light_creation";
}
[[nodiscard]] constexpr bool workspace_action_allowed(EditorWorkspaceMode mode,std::string_view id) noexcept {
  return mode==EditorWorkspaceMode::Modeling||workspace_tool_allowed(mode,id)||id=="select_all"||id=="deselect_all"||id=="invert_selection"||id=="clear_selection"||id=="toggle_group_selection"||id=="delete_selection"||id=="delete"||id=="duplicate_meshes"||id=="duplicate_selection"||id=="duplicate"||id=="snap_meshes_to_floor";
}

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
