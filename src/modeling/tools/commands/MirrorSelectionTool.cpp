#include "modeling/tools/commands/MirrorSelectionTool.h"

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
    };
}

ModelingMirrorAxis MirrorSelectionTool::axis() const { return axis_; }

ModelingCommandResult MirrorSelectionTool::executeRuntimeCommand(ModelingCommandExecutionContext& context) const {
    return context.modeling.mirrorSelection(axis_);
}

} // namespace quader
