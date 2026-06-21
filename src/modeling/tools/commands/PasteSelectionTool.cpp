#include "modeling/tools/commands/PasteSelectionTool.h"

namespace quader {

ModelingCommandResult PasteSelectionTool::executeCustomCommand(ModelingCommandExecutionContext& context) const {
    return context.modeling.pasteSelection();
}

} // namespace quader
