#pragma once

#include <quader_editor_core/session.h>

namespace quader_editor_core {

SelectionEditOperation selection_edit_operation_from_id(int operation);
int selection_edit_operation_id(SelectionEditOperation operation);
SelectionEditOperation selection_edit_operation_from_modifiers(bool ctrl_pressed, bool shift_pressed);
bool selection_edit_operation_preserves_existing(SelectionEditOperation operation);

} // namespace quader_editor_core
