////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <modeling/runtime/session.h>

namespace quader_editor_core {

SelectionEditOperation selection_edit_operation_from_id(int operation);
int selection_edit_operation_id(SelectionEditOperation operation);
SelectionEditOperation selection_edit_operation_from_modifiers(bool ctrl_pressed, bool shift_pressed);
bool selection_edit_operation_preserves_existing(SelectionEditOperation operation);

} // namespace quader_editor_core
