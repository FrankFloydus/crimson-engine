#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "core/localization_service.hpp"
#include "core/release_features.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/viewport/quader_viewport_api_runtime_access.hpp"
#include "ui/qt_main_menu.hpp"
#include "ui/qt_tool_toolbar.hpp"
#include "ui/qt_viewport_panel.hpp"
#include "viewport/viewport_settings.hpp"
#include <QAction>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <gtest/gtest.h>

TEST(PublicModelingProfile, EngineUsesPublic01ToolsAndKeepsObjectTransforms) {
    ASSERT_EQ(quader::current_product_release(),quader::kPublicRelease01);
    EXPECT_FALSE(quader::release_has_feature(quader::ReleaseFeature::UvEditing));
    EXPECT_FALSE(quader::release_has_feature(quader::ReleaseFeature::Materials));
    quader::SceneSession session;
    quader::QuaderEditorApi editor(quader::QuaderApiSessionInternalAccess::from_scene_session(session));
    quader::LocalizationService localization;
    auto settings=quader::ViewportSettings::defaults();
    QMainWindow window;
    quader::QtMainMenu menu;
    menu.initialize(*window.menuBar(),{.editor=editor,.modeling=session.modeling(),
        .viewport_settings=settings,.localization=localization});
    quader::QtToolToolbar tools;
    tools.synchronize(editor,editor.selection().summary());
    EXPECT_EQ(window.findChild<QMenu *>("menu_uv"),nullptr);
    EXPECT_EQ(window.findChild<QAction *>("panel_face_texture"),nullptr);
    EXPECT_EQ(tools.tool_action(quader::ViewportToolId::UvPeel),nullptr);
    for(const auto mode:{quader::QuaderWorkspaceMode::Default,quader::QuaderWorkspaceMode::Modeling}) {
        ASSERT_TRUE(editor.mode().set_workspace_mode(mode).ok());
        menu.refresh();tools.synchronize(editor,editor.selection().summary());
        for(const auto tool:{quader::ViewportToolId::Select,quader::ViewportToolId::Move,
                            quader::ViewportToolId::Rotate,quader::ViewportToolId::Scale}) {
            auto* action=tools.tool_action(tool);ASSERT_NE(action,nullptr);EXPECT_TRUE(action->isVisible());
        }
        EXPECT_EQ(window.findChild<QAction *>("panel_face_texture"),nullptr);
        EXPECT_EQ(tools.tool_action(quader::ViewportToolId::UvPeel),nullptr);
    }
}

TEST(PublicModelingProfile, EngineRenderingCapabilityKeepsPublicModelingRestrictions) {
    quader::SceneSession session;
    quader::QuaderEditorApi editor(quader::QuaderApiSessionInternalAccess::from_scene_session(session));
    EXPECT_FALSE(editor.viewports().rendered_presentation_available());
    EXPECT_FALSE(editor.viewports().set_shading_mode(quader::ViewportPaneId::Perspective,quader::QuaderShadingMode::Rendered).ok());
    quader::QuaderViewportApiRuntimeAccess::set_scene_rendering_available(editor.viewports(),true);
    EXPECT_TRUE(editor.viewports().rendered_presentation_available());
    EXPECT_TRUE(editor.viewports().set_shading_mode(quader::ViewportPaneId::Perspective,quader::QuaderShadingMode::Rendered).ok());
    EXPECT_EQ(editor.viewports().shading_mode(quader::ViewportPaneId::Perspective),quader::QuaderShadingMode::Rendered);
    quader::LocalizationService localization;
    quader::QtViewportPanel panel(nullptr,false,quader::RenderCompositionApi::Direct3D12,true);
    panel.attach_workspace(quader::QuaderViewportApiRuntimeAccess::workspace(editor.viewports()));
    panel.set_localization(localization);
    QAction* rendered=nullptr;
    for(auto* action:panel.findChildren<QAction*>())if(action->text()==QStringLiteral("Rendered")){rendered=action;break;}
    ASSERT_NE(rendered,nullptr);EXPECT_TRUE(rendered->isEnabled());
    ASSERT_TRUE(editor.viewports().set_shading_mode(quader::ViewportPaneId::Perspective,quader::QuaderShadingMode::Shaded).ok());
    rendered->trigger();
    EXPECT_EQ(editor.viewports().shading_mode(quader::ViewportPaneId::Perspective),quader::QuaderShadingMode::Rendered);
    EXPECT_FALSE(quader::release_has_feature(quader::ReleaseFeature::RenderedViewport));
    EXPECT_FALSE(quader::release_has_feature(quader::ReleaseFeature::UvEditing));
    EXPECT_FALSE(quader::release_has_feature(quader::ReleaseFeature::Materials));
}
