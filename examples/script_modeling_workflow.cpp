////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"

int main() {
    quader::QuaderStandaloneEditor standalone =
        quader::create_standalone_editor();
    quader::QuaderEditorApi &editor = standalone.editor();

    const quader::QuaderBounds bounds{
            .min = {-1.0f, -1.0f, -1.0f},
            .max = {1.0f, 1.0f, 1.0f},
    };

    const quader::QuaderScriptResult result = editor.script("example workflow")
            .create_box("box", bounds, "Scripted Box")
            .select("box")
            .move_selection({2.0f, 0.0f, 0.0f})
            .run();

    if (!result.ok()) {
        return 1;
    }

    const quader::QuaderApiVoidResult undo =
        editor.commands().execute(quader::quader_command::kEditUndo);
    if (!undo.ok()) {
        return 1;
    }

    const quader::QuaderApiVoidResult redo =
        editor.commands().execute(quader::quader_command::kEditRedo);
    if (!redo.ok()) {
        return 1;
    }

    return 0;
}
