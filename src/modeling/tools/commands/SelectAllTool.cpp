#include "modeling/tools/commands/SelectAllTool.h"

namespace quader {

ModelingCommandResult SelectAllTool::executeCustomCommand(ModelingCommandExecutionContext& context) const {
    return context.modeling.selectAll();
}

} // namespace quader
