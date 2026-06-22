////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <editor/modeling/runtime/runtime_command.h>

namespace quader {

/**
 * Coordinates Modeling Clipboard Service responsibilities for the modeling application service layer.
 */
class ModelingClipboardService {
public:
    [[nodiscard]] static quader_editor_core::EditorRuntimeCommandId copyCommand();
    [[nodiscard]] static quader_editor_core::EditorRuntimeCommandId pasteCommand();
    [[nodiscard]] static quader_editor_core::EditorRuntimeCommand makeCopyCommand();
    [[nodiscard]] static quader_editor_core::EditorRuntimeCommand makePasteCommand();
};

} // namespace quader
