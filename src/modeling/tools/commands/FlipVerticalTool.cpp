#include "modeling/tools/commands/FlipVerticalTool.h"

namespace quader {

ModelingCommandResult FlipVerticalTool::executeRuntimeCommand(ModelingCommandExecutionContext& context) const {
    return context.modeling.flipSelection(ModelingFlipOrientation::Vertical);
}

} // namespace quader
