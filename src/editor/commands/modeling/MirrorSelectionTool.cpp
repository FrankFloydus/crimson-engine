////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/commands/modeling/MirrorSelectionTool.h"

#include "editor/modeling/services/ModelingSessionService.h"

namespace quader {

MirrorSelectionTool::MirrorSelectionTool(ModelingMirrorAxis axis, std::string_view id, std::string_view label)
        : axis_(axis),
          id_(id),
          label_(label) {}

CommandToolDescriptor MirrorSelectionTool::descriptor() const {
    return {
            .id = id_,
            .operationId = id_,
            .label = label_,
            .kind = CommandToolKind::Instant,
            .previewKind = ModelingActionPreviewKind::None,
            .rebuildsMeshOnSuccess = true,
            .hostShortcutTargetId = axis_ == ModelingMirrorAxis::X ? "mirror_axis_x" :
                    axis_ == ModelingMirrorAxis::Y ? "mirror_axis_y" :
                    "mirror_axis_z",
    };
}

ModelingMirrorAxis MirrorSelectionTool::axis() const { return axis_; }

ModelingCommandResult MirrorSelectionTool::executeRuntimeCommand(ModelingCommandExecutionContext& context) const {
    return context.modeling.mirrorSelection(axis_);
}

} // namespace quader
