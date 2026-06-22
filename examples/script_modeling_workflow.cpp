#include "api/QuaderEditorApi.h"
#include "document/ProjectSession.h"

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
