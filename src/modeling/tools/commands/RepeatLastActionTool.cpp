#include "modeling/tools/commands/RepeatLastActionTool.h"

namespace quader {

ModelingCommandResult RepeatLastActionTool::executeCustomCommand(ModelingCommandExecutionContext& context) const {
    return context.modeling.repeatLastAction();
}

} // namespace quader
