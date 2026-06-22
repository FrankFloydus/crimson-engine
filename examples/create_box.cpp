////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/QuaderEditorApi.h"
#include "document/ProjectSession.h"

int main() {
    quader::ProjectSession session;
    quader::QuaderEditorApi editor(session);

    quader::QuaderCreateBoxArgs args;
    args.bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}};
    args.name = "Example Box";

    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> box =
            editor.modeling().createBox(args);
    if (!box.ok()) {
        return 1;
    }

    editor.object(box.value())
            .rename("Selected Example Box")
            .select();
    editor.mainViewport().setShadingMode(quader::QuaderShadingMode::Wireframe);
    return 0;
}
