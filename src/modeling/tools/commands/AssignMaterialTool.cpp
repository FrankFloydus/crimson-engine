#include "modeling/tools/commands/AssignMaterialTool.h"

namespace quader {

ModelingCommandResult AssignMaterialTool::executeCustomCommand(ModelingCommandExecutionContext& context) const {
    return context.modeling.applyActiveMaterial();
}

} // namespace quader
