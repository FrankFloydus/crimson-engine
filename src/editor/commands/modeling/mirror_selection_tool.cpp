////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/commands/modeling/mirror_selection_tool.hpp"

#include "editor/modeling/services/modeling_session_service.hpp"

namespace quader {

MirrorSelectionTool::MirrorSelectionTool(ModelingMirrorAxis axis, std::string_view id, std::string_view label)
        : axis_(axis),
          id_(id),
          label_(label) {}

CommandToolDescriptor MirrorSelectionTool::descriptor() const {
  return {
      .id = id_,
      .operation_id = id_,
      .label = label_,
      .kind = CommandToolKind::Instant,
      .preview_kind = ModelingActionPreviewKind::None,
      .rebuilds_mesh_on_success = true,
      .host_shortcut_target_id =
          axis_ == ModelingMirrorAxis::X   ? "mirror_axis_x"
          : axis_ == ModelingMirrorAxis::Y ? "mirror_axis_y"
                                           : "mirror_axis_z",
  };
}

ModelingMirrorAxis MirrorSelectionTool::axis() const { return axis_; }

ModelingCommandResult MirrorSelectionTool::execute_runtime_command(ModelingCommandExecutionContext& context) const {
  return context.modeling.mirror_selection(axis_);
}

} // namespace quader
