#include "modeling/tools/commands/CopySelectionTool.h"

namespace quader {

ModelingCommandResult CopySelectionTool::executeCustomCommand(ModelingCommandExecutionContext& context) const {
    return context.modeling.copySelection();
}

} // namespace quader
