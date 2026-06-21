#include <quader_editor_core/editor_operation.h>

namespace quader_editor_core {

EditorOperation::~EditorOperation() = default;

CommandResult EditorOperation::begin_preview(EditorSession&, const EditorOperationSettings&) const
{
	return { false, false, "Operation does not support preview." };
}

CommandResult EditorOperation::update_preview(EditorSession&, const EditorOperationSettings&) const
{
	return { false, false, "Operation does not support preview." };
}

} // namespace quader_editor_core
