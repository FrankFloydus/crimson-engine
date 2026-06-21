#include "modeling/tools/commands/FlipHorizontalTool.h"

namespace quader {

ModelingCommandResult FlipHorizontalTool::executeRuntimeCommand(ModelingCommandExecutionContext& context) const {
    return context.modeling.flipSelection(ModelingFlipOrientation::Horizontal);
}

} // namespace quader
