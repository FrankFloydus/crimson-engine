#include "modeling/tools/commands/ClearSelectionTool.h"

namespace quader {

ModelingCommandResult ClearSelectionTool::executeCustomCommand(ModelingCommandExecutionContext& context) const {
    return context.modeling.clearSelection();
}

} // namespace quader
