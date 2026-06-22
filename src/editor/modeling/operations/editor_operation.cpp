////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <editor/modeling/operations/editor_operation.h>

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
