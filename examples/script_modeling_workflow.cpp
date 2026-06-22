////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/QuaderEditorApi.h"
#include "scene/document/ProjectSession.h"

int main() {
    quader::ProjectSession session;
    quader::QuaderEditorApi editor(session);

    const quader::ModelingBounds bounds{
            .min = {-1.0f, -1.0f, -1.0f},
            .max = {1.0f, 1.0f, 1.0f},
    };

    const quader::QuaderScriptResult result = editor.script("example workflow")
            .createBox("box", bounds, "Scripted Box")
            .select("box")
            .moveSelection({2.0f, 0.0f, 0.0f})
            .run();

    if (!result.ok) {
        return 1;
    }

    editor.commands().execute(quader::QuaderCommand::HistoryUndo);
    editor.commands().execute(quader::QuaderCommand::HistoryRedo);
    return 0;
}
