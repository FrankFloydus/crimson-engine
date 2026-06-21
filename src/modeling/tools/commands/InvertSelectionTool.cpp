#include "modeling/tools/commands/InvertSelectionTool.h"

namespace quader {

ModelingCommandResult InvertSelectionTool::executeCustomCommand(ModelingCommandExecutionContext& context) const {
    return context.modeling.invertSelection();
}

} // namespace quader
