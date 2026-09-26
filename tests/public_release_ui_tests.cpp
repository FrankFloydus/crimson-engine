////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_command_ids.hpp"
#include "api/quader_editor_api.hpp"
#include "core/localization_service.hpp"
#include "core/release_features.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/modeling/services/primitive_creation_tool_service.hpp"
#include "ui/qt_operation_input_hints.hpp"
#include "io/qdr/qdr_document_reader.hpp"
#include "io/qdr/qdr_document_writer.hpp"
#include "render/render_system.hpp"
#include "scene/components.hpp"
#include "support/command_observable_state.hpp"
#include "ui/qt_environment_settings_popup.hpp"
#include "ui/inspector_panel.hpp"
#include "ui/qt_command_palette.hpp"
#include "ui/qt_settings_dialog.hpp"
#include "ui/qt_main_menu.hpp"
#include "ui/ui_context.hpp"
#include "viewport/viewport_settings.hpp"
#include "ui/ui_settings.hpp"
#include "ui/ui_system.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
#include <QImage>
#include <QTableView>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QPushButton>
#include <QTemporaryDir>
#include <QToolBar>
#include <QTreeWidget>

#include <filesystem>
#include <fstream>
#include <memory>

namespace quader::test {
namespace {

/// Isolates retained window settings while keeping all test widgets hidden.
class PublicReleaseUiTests : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(QCoreApplication::instance(), nullptr);
    ASSERT_TRUE(profile_.isValid());
    had_local_app_data_ = qEnvironmentVariableIsSet("LOCALAPPDATA");
    previous_local_app_data_ = qgetenv("LOCALAPPDATA");
    ASSERT_TRUE(qputenv("LOCALAPPDATA", profile_.path().toUtf8()));
    application_ = std::make_unique<QApplication>(argc_, argv_);
    application_->setQuitOnLastWindowClosed(false);
    QApplication::setStyle(QStringLiteral("Fusion"));
    ASSERT_EQ(current_product_release(), kPublicRelease01);
  }

  void TearDown() override {
    application_.reset();
    if (had_local_app_data_) {
      (void)qputenv("LOCALAPPDATA", previous_local_app_data_);
    } else {
      (void)qunsetenv("LOCALAPPDATA");
    }
  }

  [[nodiscard]] static std::filesystem::path resource_root() {
    return std::filesystem::path(QUADER_SOURCE_DIR) / "resources";
  }

private:
  QTemporaryDir profile_;
  bool had_local_app_data_ = false;
  QByteArray previous_local_app_data_;
  int argc_ = 1;
  char application_name_[32] = "quader-public-ui-tests";
  char *argv_[2] = {application_name_, nullptr};
  std::unique_ptr<QApplication> application_;
};

TEST_F(PublicReleaseUiTests, StaleWindowStateCannotRestoreExcludedPanelsAndLaunchers) {
  const auto paths = UISettings::resolve_paths(resource_root());
  std::filesystem::create_directories(paths.directory);
  auto settings = UISettings::defaults();
  settings.set_panel_visible(UIPanelId::FaceTexture, true);
  settings.set_panel_visible(UIPanelId::Layers, true);
  ASSERT_TRUE(settings.save(paths.settings_file));
  {
    QMainWindow old_window;
    QDockWidget old_face_texture(&old_window);
    old_face_texture.setObjectName(QStringLiteral("dock_face_texture"));
    old_window.addDockWidget(Qt::RightDockWidgetArea, &old_face_texture);
    QDockWidget old_layers(&old_window);
    old_layers.setObjectName(QStringLiteral("dock_layers"));
    old_window.addDockWidget(Qt::RightDockWidgetArea, &old_layers);
    QToolBar old_texture_toolbar(&old_window);
    old_texture_toolbar.setObjectName(QStringLiteral("textureToolbar"));
    old_window.addToolBar(&old_texture_toolbar);
    const QByteArray old_state = old_window.saveState(1);
    std::ofstream output(paths.qt_window_state_file, std::ios::binary);
    ASSERT_TRUE(output.is_open());
    output.write(old_state.constData(), old_state.size());
    ASSERT_TRUE(output.good());
  }

  RenderSystem render_system;
  SceneSession session;
  QuaderEditorApi editor{
      QuaderApiSessionInternalAccess::from_scene_session(session)};
  UISystem ui;
  ASSERT_TRUE(ui.initialize(render_system, resource_root()));
  ASSERT_NE(ui.main_window(), nullptr);
  ui.main_window()->setAttribute(Qt::WA_DontShowOnScreen, true);
  ui.install_runtime_hooks(editor, session.scene(), session.assets(),
                           session.modeling(), session.selection(),
                           session.native_tools(), session.point_placement(),
                           kInvalidAssetId);
  const bool outliner_visible = editor.ui().panel_visible("outliner");
  for (const char *panel : {"face_texture", "Face Texture", "layers", "Layers"}) {
    const auto shown = editor.ui().show_panel(panel);
    EXPECT_FALSE(shown.ok());
    EXPECT_FALSE(shown.changed());
    const auto focused = editor.ui().focus_panel(panel);
    EXPECT_FALSE(focused.ok());
    EXPECT_FALSE(focused.changed());
    EXPECT_FALSE(editor.ui().panel_visible(panel));
  }
  ui.refresh({}, {});
  EXPECT_EQ(editor.ui().panel_visible("outliner"), outliner_visible);
  EXPECT_NE(ui.main_window()->findChild<QDockWidget *>(
                QStringLiteral("dock_outliner")), nullptr);
  EXPECT_NE(ui.main_window()->findChild<QDockWidget *>(
                QStringLiteral("dock_inspector")), nullptr);
  for (const char *name : {"dock_layers", "panel_layers",
                           "dock_face_texture", "face_texture.panel",
                           "textureToolbar", "materialBrowser.window",
                           "materialBrowser.embedded",
                           "materialFindReplace.dialog",
                           "standardMaterialEditor.window", "textureInfo.window"}) {
    EXPECT_EQ(ui.main_window()->findChild<QWidget *>(QString::fromUtf8(name)),
              nullptr) << name;
  }

  int camera_launchers = 0;
  for (QPushButton *button : ui.main_window()->findChildren<QPushButton *>()) {
    if (button->objectName().startsWith(QStringLiteral("viewportEnvironment_"))) {
      ++camera_launchers;

    }
    if (button->objectName().startsWith(QStringLiteral("viewportShading_"))) {
      ASSERT_NE(button->menu(), nullptr);
      for (const QAction *action : button->menu()->actions()) {
        EXPECT_FALSE(action->text().startsWith(QStringLiteral("Rendered")));
        if (action->menu() != nullptr) {
          EXPECT_NE(action->menu()->title(), QStringLiteral("Debug View"));
        }
      }
    }
  }
  EXPECT_EQ(camera_launchers, 0);
  EXPECT_EQ(ui.main_window()->findChild<QWidget *>(
                QStringLiteral("environmentSettings")), nullptr);
  ui.shutdown();
}

TEST_F(PublicReleaseUiTests, CameraSettingsHaveNoControlsAndNativeClippingRemainsAvailable) {
  SceneSession session;
  QuaderEditorApi editor{
      QuaderApiSessionInternalAccess::from_scene_session(session)};
  LocalizationService localization;
  ASSERT_TRUE(localization.load(resource_root() / "locales", "en-US"));
  QtEnvironmentSettingsPopup popup;
  popup.refresh(editor, session.scene(), &localization);
  auto *tree = popup.findChild<QTreeWidget *>(
      QStringLiteral("environmentSettings.tree"));
  ASSERT_NE(tree, nullptr);
  EXPECT_EQ(tree->topLevelItemCount(), 0);
  EXPECT_TRUE(popup.findChildren<QDoubleSpinBox *>().empty());
  const auto before = editor.query().scene_settings();
  auto settings = before;
  settings.camera.near_plane = 0.125f;
  settings.camera.far_plane = 1234.0f;
  ASSERT_TRUE(editor.scene().set_scene_settings(settings).ok());
  const auto after = editor.query().scene_settings();
  EXPECT_FLOAT_EQ(after.camera.near_plane, 0.125f);
  EXPECT_FLOAT_EQ(after.camera.far_plane, 1234.0f);
  EXPECT_FLOAT_EQ(after.camera.aperture, before.camera.aperture);
  EXPECT_FLOAT_EQ(after.camera.shutter_speed, before.camera.shutter_speed);
  EXPECT_FLOAT_EQ(after.camera.sensitivity, before.camera.sensitivity);
  EXPECT_EQ(after.environment.environment_id, before.environment.environment_id);
  EXPECT_FLOAT_EQ(after.environment.intensity, before.environment.intensity);
  EXPECT_EQ(after.fog.enabled, before.fog.enabled);
  EXPECT_EQ(after.post_process.enabled, before.post_process.enabled);

  popup.refresh(editor, session.scene(), &localization);
  const auto presentation_updates = popup.presentation_update_count();
  const auto settings_revision = session.scene().scene_settings_revision();
  ASSERT_TRUE(editor.scene().create_empty().ok());
  for (int frame = 0; frame < 8; ++frame) {
    popup.refresh(editor, session.scene(), &localization);
  }
  EXPECT_EQ(session.scene().scene_settings_revision(), settings_revision);
  EXPECT_EQ(popup.presentation_update_count(), presentation_updates);
  EXPECT_TRUE(popup.findChildren<QDoubleSpinBox *>().empty());
}

TEST_F(PublicReleaseUiTests,
       DefaultImagePreferenceWorksWithoutUnlockingAuthoredMaterialsOrChangingSaves) {
  const auto paths = UISettings::resolve_paths(resource_root());
  std::filesystem::create_directories(paths.directory);
  const auto source = paths.directory / "chosen-image.png";
  QImage image(4, 4, QImage::Format_ARGB32);
  image.fill(qRgb(40, 100, 180));
  ASSERT_TRUE(image.save(QString::fromStdWString(source.wstring()), "PNG"));

  RenderSystem render_system;
  SceneSession session;
  QuaderEditorApi editor{
      QuaderApiSessionInternalAccess::from_scene_session(session)};
  UISystem ui;
  ASSERT_TRUE(ui.initialize(render_system, resource_root()));
  ui.main_window()->setAttribute(Qt::WA_DontShowOnScreen, true);
  ui.install_runtime_hooks(editor, session.scene(), session.assets(),
                           session.modeling(), session.selection(),
                           session.native_tools(), session.point_placement(),
                           kInvalidAssetId);
  EXPECT_FALSE(release_has_feature(ReleaseFeature::Materials));
  auto *browse = ui.main_window()->findChild<QPushButton *>(
      QStringLiteral("settingsDefaultMaterialBrowse"));
  ASSERT_NE(browse, nullptr);
  EXPECT_TRUE(browse->isEnabled());
  EXPECT_NE(ui.main_window()->findChild<QLabel *>(
                QStringLiteral("settingsDefaultMaterialPreview")), nullptr);
  EXPECT_EQ(ui.main_window()->findChild<QWidget *>(
                QStringLiteral("standardMaterialEditor.window")), nullptr);

  const auto mesh = editor.modeling().create_box(
      {.min = {-1.f, -1.f, -1.f}, .max = {1.f, 1.f, 1.f}}, "Existing mesh");
  ASSERT_TRUE(mesh.ok()) << mesh.error_message();
  ASSERT_TRUE(editor.selection().select_object(mesh.value()).ok());
  ASSERT_TRUE(editor.modeling().begin_transform_preview().ok());
  ASSERT_TRUE(editor.modeling().update_transform_translate({0.25f, 0.f, 0.f}).ok());
  const auto before = capture_command_observable_state(editor);
  const auto scene_bytes = QdrDocumentWriter::serialize_project_document_bytes(session.document());
  int publications = 0;
  std::filesystem::path published;
  ui.set_default_material_texture_changed_callback(
      [&](const std::filesystem::path &path) { ++publications; published = path; });
  const auto chosen = editor.preferences().set_default_material_texture(source);
  ASSERT_TRUE(chosen.ok()) << chosen.error_message();
  EXPECT_TRUE(chosen.changed());
  EXPECT_EQ(publications, 1);
  const auto managed = editor.preferences().default_material_texture();
  EXPECT_EQ(published, managed);
  EXPECT_NE(managed, source);
  EXPECT_TRUE(std::filesystem::is_regular_file(managed));
  EXPECT_EQ(UISettings::load(paths.settings_file).default_material_texture(), managed);
  constexpr auto document_domains = kAllCommandObservableDomains &
      ~command_observable_domain_mask(CommandObservableDomain::Preferences);
  const auto difference = diff_command_observable_state(
      before, capture_command_observable_state(editor), document_domains);
  EXPECT_TRUE(difference.empty()) << difference.summary;
  const auto after_bytes = QdrDocumentWriter::serialize_project_document_bytes(session.document());
  EXPECT_EQ(after_bytes, scene_bytes);
  const auto reopened = QdrDocumentReader::deserialize_project_document(after_bytes, kPublicRelease01);
  EXPECT_TRUE(reopened.ok) << reopened.message;

  const auto repeated = editor.preferences().set_default_material_texture(source);
  EXPECT_TRUE(repeated.ok());
  EXPECT_FALSE(repeated.changed());
  EXPECT_EQ(publications, 1);
  const auto invalid = editor.preferences().set_default_material_texture(paths.directory / "missing.png");
  EXPECT_FALSE(invalid.ok());
  EXPECT_FALSE(invalid.changed());
  EXPECT_EQ(editor.preferences().default_material_texture(), managed);
  EXPECT_EQ(publications, 1);
  const auto reset = editor.preferences().set_default_material_texture({});
  EXPECT_TRUE(reset.ok());
  EXPECT_TRUE(reset.changed());
  EXPECT_EQ(publications, 2);
  EXPECT_TRUE(published.empty());
  EXPECT_TRUE(editor.preferences().default_material_texture().empty());
  ui.shutdown();
}

TEST_F(PublicReleaseUiTests, MeshInspectorRetainsDoubleSidedWithoutCastShadows) {
  SceneSession session;
  QuaderEditorApi editor{
      QuaderApiSessionInternalAccess::from_scene_session(session)};
  const auto box = editor.modeling().create_box(
      {.min = {-1.f, -1.f, -1.f}, .max = {1.f, 1.f, 1.f}}, "Inspector Box");
  ASSERT_TRUE(box.ok()) << box.error_message();
  ASSERT_TRUE(editor.selection().select_object(box.value()).ok());
  UISettings ui_settings = UISettings::defaults();
  ViewportSettings viewport_settings = ViewportSettings::defaults();
  LocalizationService localization;
  ASSERT_TRUE(localization.load(resource_root() / "locales", "en-US"));
  UIContext context{
      .intent = {.editor = editor},
      .scene = {.document = session.scene(), .assets = session.assets(),
                .modeling = session.modeling(), .selection = session.selection()},
      .preferences = {.ui = ui_settings, .viewport = viewport_settings},
      .localization = localization,
  };
  InspectorPanel panel;
  panel.refresh(context);
  EXPECT_EQ(panel.findChild<QCheckBox *>(
                QStringLiteral("inspector.lighting.cast_shadows")), nullptr);
  auto *double_sided = panel.findChild<QCheckBox *>(
      QStringLiteral("inspector.lighting.double_sided"));
  ASSERT_NE(double_sided, nullptr);
  const auto *mesh = session.scene().mesh(box.value().entity);
  ASSERT_NE(mesh, nullptr);
  EXPECT_EQ(panel.findChild<QComboBox *>(
                QStringLiteral("inspector.physics.collision_type")), nullptr);
  EXPECT_EQ(panel.findChild<QComboBox *>(
                QStringLiteral("inspector.physics.body_type")), nullptr);
  EXPECT_NE(panel.findChild<QCheckBox *>(
                QStringLiteral("inspector.physics.generate_collisions")), nullptr);
  EXPECT_NE(panel.findChild<QCheckBox *>(
                QStringLiteral("inspector.physics.display_collision")), nullptr);
  EXPECT_EQ(mesh->collision_type, MeshCollisionType::Mesh);
  EXPECT_EQ(mesh->body_type, MeshPhysicsBodyType::StaticBody);
  const bool before_shadows = mesh->cast_shadows;
  const bool next_double_sided = !mesh->double_sided;
  double_sided->setChecked(next_double_sided);
  mesh = session.scene().mesh(box.value().entity);
  ASSERT_NE(mesh, nullptr);
  EXPECT_EQ(mesh->double_sided, next_double_sided);
  EXPECT_EQ(mesh->cast_shadows, before_shadows);
}

TEST_F(PublicReleaseUiTests, AdvancedSelectionDisappearsOnlyFromDiscoverySurfaces) {
  SceneSession session;
  QuaderEditorApi editor{
      QuaderApiSessionInternalAccess::from_scene_session(session)};
  LocalizationService localization;
  ASSERT_TRUE(localization.load(resource_root() / "locales", "en-US"));
  UISettings ui_settings = UISettings::defaults();
  ViewportSettings viewport_settings = ViewportSettings::defaults();
  QMainWindow window;
  QtMainMenu menu;
  menu.initialize(*window.menuBar(), QtMainMenuContext{
      .editor = editor, .modeling = session.modeling(),
      .viewport_settings = viewport_settings, .localization = localization,
  });
  QtSettingsDialog settings;
  settings.set_context(ui_settings, viewport_settings, localization,
                        editor.commands(), {});
  auto *table = settings.findChild<QTableView *>(
      QStringLiteral("settingsKeybindingsTable"));
  ASSERT_NE(table, nullptr);
  ASSERT_NE(table->model(), nullptr);
  ASSERT_GT(table->model()->rowCount(), 0);
  QtCommandPalette palette(editor.commands(), localization);
  palette.setAttribute(Qt::WA_DontShowOnScreen, true);
  palette.open_palette();
  auto *search = palette.findChild<QLineEdit *>(
      QStringLiteral("commandPalette.search"));
  auto *results = palette.findChild<QTreeWidget *>(
      QStringLiteral("commandPalette.results"));
  ASSERT_NE(search, nullptr);
  ASSERT_NE(results, nullptr);
  for (const QuaderCommandId id : {
           quader_command::kSelectEdgeDirection,
           quader_command::kSelectCoplanarFaces,
           quader_command::kSelectFaceNormal,
           quader_command::kSelectMeshNGons,
           quader_command::kSelectSimilarMeshes}) {
    const QString target = QString::fromUtf8(id.string().data(),
                                            static_cast<qsizetype>(id.string().size()));
    EXPECT_TRUE(editor.commands().descriptor(id).has_value()) << id.string();
    for (const QAction *action : window.menuBar()->findChildren<QAction *>()) {
      EXPECT_NE(action->property("quaderCommandId").toString(), target);
    }
    for (int row = 0; row < table->model()->rowCount(); ++row) {
      EXPECT_NE(table->model()->index(row, 0).data(Qt::UserRole).toString(), target);
    }
    search->setText(target);
    for (int row = 0; row < results->topLevelItemCount(); ++row) {
      EXPECT_NE(results->topLevelItem(row)->data(0, Qt::UserRole).toString(), target);
    }
  }
  for (const QuaderCommandId id : {quader_command::kToolsActivateCreateTorus,
                                  quader_command::kToolsActivateCreateCone}) {
    const QString target = QString::fromUtf8(id.string().data(),
                                            static_cast<qsizetype>(id.string().size()));
    for (const QAction *action : window.menuBar()->findChildren<QAction *>()) {
      EXPECT_NE(action->property("quaderCommandId").toString(), target);
    }
  }
  EXPECT_EQ(window.menuBar()->findChild<QAction *>(QStringLiteral("panel_layers")),
            nullptr);
  for (const QString component : {QStringLiteral("edge"), QStringLiteral("face")}) {
    auto *component_menu = window.menuBar()->findChild<QMenu *>(
        QStringLiteral("menu_") + component);
    ASSERT_NE(component_menu, nullptr);
    EXPECT_EQ(component_menu->findChild<QAction *>(
                  QStringLiteral("command_modeling_bridge_") + component + 's'),
              nullptr);
    EXPECT_NE(component_menu->findChild<QAction *>(
                  QStringLiteral("command_modeling_interpolated_bridge_") + component + 's'),
              nullptr);
  }
  palette.reject();
}

TEST_F(PublicReleaseUiTests, ContextHintsRespectPublicAvailabilityAndCurrentBindings) {
  auto standalone = create_standalone_editor();
  auto &editor = standalone.editor();
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  const auto idle = editor.tools().active_input_hints();
  ASSERT_EQ(idle.size(), 3u);
  EXPECT_EQ(idle[0].label_fallback, "Orbit");
  EXPECT_EQ(idle[1].label_fallback, "Pan");
  EXPECT_EQ(idle[2].label_fallback, "Fly Navigation");
  const auto contains = [](const auto &hints, QuaderCommandId command) {
    for (const auto &hint : hints) if (hint.command.string() == command.string()) return true;
    return false;
  };
  for (const auto command : {quader_command::kToolsActivateCreateBox,
                             quader_command::kToolsActivateCreateCylinder,
                             quader_command::kToolsActivateCreateSphere}) {
    ASSERT_TRUE(editor.commands().execute(command).ok());
    const auto pending = editor.tools().active_input_hints();
    EXPECT_FALSE(contains(pending, quader_command::kToolsApplyPrimitiveCreation));
    EXPECT_TRUE(contains(pending, quader_command::kToolsCancelPrimitiveCreation));
  }
  for (const auto blocked : {quader_command::kToolsActivateCreateCone,
                             quader_command::kToolsActivateCreateTorus}) {
    EXPECT_FALSE(editor.commands().execute(blocked).ok());
    EXPECT_TRUE(contains(editor.tools().active_input_hints(), quader_command::kToolsTogglePrimitiveHemisphere));
  }
  LocalizationService localization;
  ASSERT_TRUE(localization.load(resource_root() / "locales", "en-US"));
  QtOperationInputHints widget;
  ASSERT_TRUE(editor.commands().set_shortcut(quader_command::kToolsFlipPrimitiveFaces, "Ctrl+K D").ok());
  widget.refresh(editor.tools(), editor.commands(), localization);
  bool rebound_found = false;
  for (const auto &hint : widget.hints()) rebound_found |= hint.badge == QStringLiteral("Ctrl+K D");
  EXPECT_TRUE(rebound_found);
  ASSERT_TRUE(editor.commands().remove_shortcut(quader_command::kToolsFlipPrimitiveFaces).ok());
  widget.refresh(editor.tools(), editor.commands(), localization);
  for (const auto &hint : widget.hints()) EXPECT_NE(hint.badge, QStringLiteral("Ctrl+K D"));

  QuaderOperationInputContext context;
  context.material_transfer = QuaderInputMaterialTransfer::ContinueTexture;
  EXPECT_TRUE(editor.tools().active_input_hints(context).empty());
  context = {};
  context.point_light_range_hovered = true;
  EXPECT_TRUE(editor.tools().active_input_hints(context).empty());
  context = {};
  context.selection_gesture = QuaderInputSelectionGesture::EnterInstance;
  EXPECT_TRUE(editor.tools().active_input_hints(context).empty());

  SceneSession session;
  QuaderEditorApi phase_editor{QuaderApiSessionInternalAccess::from_scene_session(session)};
  ASSERT_TRUE(phase_editor.tools().activate(ViewportToolId::CreatePrimitive).ok());
  ModelingBoxPreview preview;
  preview.ok = true;
  preview.valid = true;
  preview.drag_state = ModelingBoxDragState::Height;
  preview.corners = {QVec3{-1, 0, -1}, QVec3{-1, 0, 1}, QVec3{1, 0, 1}, QVec3{1, 0, -1},
                     QVec3{-1, 2, -1}, QVec3{-1, 2, 1}, QVec3{1, 2, 1}, QVec3{1, 2, -1}};
  preview.corner_count = preview.corners.size();
  ASSERT_TRUE(session.primitive_creation().accept_preview(preview, "Hint Box").ok);
  EXPECT_TRUE(contains(phase_editor.tools().active_input_hints(), quader_command::kToolsApplyPrimitiveCreation));
  context = {};
  context.pointer_dragging = true;
  EXPECT_FALSE(contains(phase_editor.tools().active_input_hints(context), quader_command::kToolsApplyPrimitiveCreation));
}

} // namespace
} // namespace quader::test
