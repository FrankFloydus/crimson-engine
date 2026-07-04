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

    quader::QuaderCreateBoxArgs args;
    args.bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}};
    args.name = "Example Box";

    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> box =
        editor.modeling().create_box(args);
    if (!box.ok()) {
        return 1;
    }

    quader::QuaderObjectHandle object = editor.object(box.value());
    const quader::QuaderApiVoidResult renamed =
        object.rename("Selected Example Box");
    if (!renamed.ok()) {
        return 1;
    }
    const quader::QuaderApiVoidResult selected = object.select();
    if (!selected.ok()) {
        return 1;
    }

    const quader::QuaderApiVoidResult shading =
        editor.main_viewport().set_shading_mode(
        quader::QuaderShadingMode::Wireframe);
    if (!shading.ok()) {
        return 1;
    }

    return 0;
}
