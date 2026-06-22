#include "api/QuaderEditorApi.h"
#include "document/ProjectSession.h"

#include <gtest/gtest.h>

#include <algorithm>

TEST(EditorApiTests, AggregatesDocumentApisAndCommandRouting) {
    quader::ProjectSession session;
    quader::QuaderEditorApi editor(session);

    EXPECT_FALSE(editor.project().dirty());
    EXPECT_EQ(editor.project().metadata().name, "Untitled Project");

    const std::vector<quader::QuaderCommandDescriptor> commands = editor.commands().available();
    EXPECT_NE(std::find_if(commands.begin(), commands.end(), [](const quader::QuaderCommandDescriptor& command) {
                  return command.id == "history.undo";
              }),
            commands.end());
    EXPECT_EQ(editor.commands().shortcutFor("history.undo"), "Ctrl+Z");
    EXPECT_FALSE(editor.commands().execute("__missing_command__").ok);

#if QUADER_HAS_MODELING_SDK
    quader::QuaderCreateBoxArgs args;
    args.bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}};
    args.name = "Editor API Box";

    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> created = editor.modeling().createBox(args);
    ASSERT_TRUE(created.ok) << created.message;
    EXPECT_EQ(editor.query().entities().size(), 1u);

    const quader::QuaderApiVoidResult undo = editor.commands().execute("history.undo");
    EXPECT_TRUE(undo.ok) << undo.message;
    EXPECT_TRUE(undo.changed);
#endif
}

TEST(EditorApiTests, ViewportHandlesRouteThroughRuntimeHooks) {
    quader::ProjectSession session;
    quader::QuaderEditorApi editor(session);

    quader::ViewportLayoutId layout = quader::ViewportLayoutId::Single;
    quader::QuaderShadingMode shading = quader::QuaderShadingMode::Solid;
    quader::ViewportView view = quader::ViewportView::Perspective;
    bool framed = false;

    editor.viewports().setRuntimeHooks(quader::QuaderViewportRuntimeHooks{
            .layout = [&]() {
                return layout;
            },
            .setLayout = [&](quader::ViewportLayoutId next) {
                layout = next;
                return quader::QuaderApiVoidResult{.ok = true, .changed = true};
            },
            .shadingMode = [&](quader::ViewportPaneId) {
                return shading;
            },
            .setShadingMode = [&](quader::ViewportPaneId, quader::QuaderShadingMode next) {
                shading = next;
                return quader::QuaderApiVoidResult{.ok = true, .changed = true};
            },
            .view = [&](quader::ViewportPaneId) {
                return view;
            },
            .setView = [&](quader::ViewportPaneId, quader::ViewportView next) {
                view = next;
                return quader::QuaderApiVoidResult{.ok = true, .changed = true};
            },
            .frameSelection = [&](quader::ViewportPaneId) {
                framed = true;
                return quader::QuaderApiVoidResult{.ok = true, .changed = false};
            },
    });

    EXPECT_TRUE(editor.viewports().setLayout(quader::ViewportLayoutId::FourQuad).ok);
    EXPECT_EQ(editor.viewports().layout(), quader::ViewportLayoutId::FourQuad);

    auto viewport = editor.viewports().main();
    EXPECT_TRUE(viewport.setShadingMode(quader::QuaderShadingMode::Wireframe).ok);
    EXPECT_EQ(editor.viewports().shadingMode(quader::ViewportPaneId::Perspective), quader::QuaderShadingMode::Wireframe);

    EXPECT_TRUE(viewport.setView(quader::ViewportView::Top).ok);
    EXPECT_EQ(editor.viewports().view(quader::ViewportPaneId::Perspective), quader::ViewportView::Top);

    EXPECT_TRUE(viewport.camera().frameSelection().ok);
    EXPECT_TRUE(framed);
}
