////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "ui/qt_color_swatch_button.hpp"
#include "ui/qt_standard_material_editor_window.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStatusBar>

#include <array>
#include <utility>
#include <vector>

namespace quader {
namespace {

QAction *find_action(QtStandardMaterialEditorWindow &window,
                     const char *object_name) {
  return window.findChild<QAction *>(QString::fromUtf8(object_name));
}

TEST(QtStandardMaterialEditorWindowTests,
     PresentsDraftAndDispatchesOnlySemanticEditorIntent) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-standard-material-editor-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  application.setQuitOnLastWindowClosed(false);

  QWidget owner;
  auto *window = new QtStandardMaterialEditorWindow(&owner);
  window->setAttribute(Qt::WA_DontShowOnScreen, true);
  EXPECT_EQ(window->windowType(), Qt::Tool);
  EXPECT_FALSE(window->testAttribute(Qt::WA_DeleteOnClose));
  EXPECT_FALSE(window->has_document());

  const std::array file_actions{
      QtStandardMaterialEditorFileAction::New,
      QtStandardMaterialEditorFileAction::Open,
      QtStandardMaterialEditorFileAction::Save,
      QtStandardMaterialEditorFileAction::SaveAs,
  };
  std::vector<QtStandardMaterialEditorFileAction> dispatched_file_actions;
  window->set_file_action_callback(
      [&dispatched_file_actions](QtStandardMaterialEditorFileAction action) {
        dispatched_file_actions.push_back(action);
      });
  const std::array<const char *, 4> action_names{
      "standardMaterialEditor.file.new",
      "standardMaterialEditor.file.open",
      "standardMaterialEditor.file.save",
      "standardMaterialEditor.file.saveAs",
  };
  const std::array expected_shortcuts{
      QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_N)),
      QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_O)),
      QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_S)),
      QKeySequence(
          QKeyCombination(Qt::ControlModifier | Qt::AltModifier, Qt::Key_S)),
  };
  for (std::size_t index = 0; index < action_names.size(); ++index) {
    QAction *action = find_action(*window, action_names[index]);
    ASSERT_NE(action, nullptr);
    EXPECT_EQ(action->shortcut(), expected_shortcuts[index]);
    EXPECT_EQ(action->shortcutContext(), Qt::WindowShortcut);
  }
  QAction *close_action =
      find_action(*window, "standardMaterialEditor.file.close");
  ASSERT_NE(close_action, nullptr);
  EXPECT_EQ(close_action->shortcut(),
            QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_W)));
  EXPECT_EQ(close_action->shortcutContext(), Qt::WindowShortcut);

  for (const char *action_name : action_names) {
    QAction *action = find_action(*window, action_name);
    ASSERT_NE(action, nullptr);
    if (action->isEnabled()) {
      action->trigger();
    }
  }
  ASSERT_EQ(dispatched_file_actions.size(), 2U)
      << "Save actions stay unavailable until a draft is supplied";
  EXPECT_EQ(dispatched_file_actions[0], file_actions[0]);
  EXPECT_EQ(dispatched_file_actions[1], file_actions[1]);

  QtStandardMaterialEditorDocument document{
      .stable_id = QStringLiteral("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa"),
      .name = QStringLiteral("Bronze"),
      .project_relative_path = QStringLiteral("Materials/Metals/bronze.qmat"),
      .albedo = QColor(205, 127, 50),
      .roughness = 0.35,
      .metallic = 0.8,
      .normal_strength = 1.25,
      .direct_x_normal = true,
      .textures =
          {
              QtStandardMaterialTexturePresentation{
                  .project_relative_path =
                      QStringLiteral("Textures/bronze_albedo.png")},
              QtStandardMaterialTexturePresentation{},
              QtStandardMaterialTexturePresentation{
                  .project_relative_path =
                      QStringLiteral("Textures/bronze_metallic.png")},
              QtStandardMaterialTexturePresentation{
                  .diagnostic = QStringLiteral("Normal preview fallback")},
          },
      .dirty = true,
      .status_message = QStringLiteral("Normal preview fallback"),
  };
  window->set_document(document);
  EXPECT_TRUE(window->has_document());
  EXPECT_EQ(window->windowTitle(),
            QStringLiteral("Bronze * - Standard Material Editor"));
  EXPECT_EQ(window->statusBar()->currentMessage(),
            QStringLiteral("Materials/Metals/bronze.qmat | "
                           "Normal preview fallback"));

  auto *properties = window->findChild<QGroupBox *>(
      QStringLiteral("standardMaterialEditor.properties"));
  auto *name = window->findChild<QLineEdit *>(
      QStringLiteral("standardMaterialEditor.name"));
  auto *type = window->findChild<QLineEdit *>(
      QStringLiteral("standardMaterialEditor.type"));
  auto *albedo_button = window->findChild<QPushButton *>(
      QStringLiteral("standardMaterialEditor.albedo"));
  auto *albedo = dynamic_cast<QtColorSwatchButton *>(albedo_button);
  auto *roughness = window->findChild<QDoubleSpinBox *>(
      QStringLiteral("standardMaterialEditor.roughness"));
  auto *metallic = window->findChild<QDoubleSpinBox *>(
      QStringLiteral("standardMaterialEditor.metallic"));
  auto *normal_strength = window->findChild<QDoubleSpinBox *>(
      QStringLiteral("standardMaterialEditor.normalStrength"));
  auto *direct_x = window->findChild<QCheckBox *>(
      QStringLiteral("standardMaterialEditor.directXNormal"));
  auto *albedo_path = window->findChild<QLineEdit *>(
      QStringLiteral("standardMaterialEditor.albedoTexture.path"));
  auto *metallic_path = window->findChild<QLineEdit *>(
      QStringLiteral("standardMaterialEditor.metallicTexture.path"));
  auto *normal_path = window->findChild<QLineEdit *>(
      QStringLiteral("standardMaterialEditor.normalTexture.path"));
  ASSERT_NE(properties, nullptr);
  ASSERT_NE(name, nullptr);
  ASSERT_NE(type, nullptr);
  ASSERT_NE(albedo, nullptr);
  ASSERT_NE(roughness, nullptr);
  ASSERT_NE(metallic, nullptr);
  ASSERT_NE(normal_strength, nullptr);
  ASSERT_NE(direct_x, nullptr);
  ASSERT_NE(albedo_path, nullptr);
  ASSERT_NE(metallic_path, nullptr);
  ASSERT_NE(normal_path, nullptr);
  EXPECT_TRUE(properties->isEnabled());
  EXPECT_EQ(name->text(), QStringLiteral("Bronze"));
  EXPECT_TRUE(type->isReadOnly());
  EXPECT_EQ(type->text(), QStringLiteral("Standard Material"));
  EXPECT_EQ(albedo->swatch_color(), QColor(205, 127, 50));
  EXPECT_DOUBLE_EQ(roughness->value(), 0.35);
  EXPECT_DOUBLE_EQ(metallic->value(), 0.8);
  EXPECT_DOUBLE_EQ(normal_strength->value(), 1.25);
  EXPECT_TRUE(direct_x->isChecked());
  EXPECT_EQ(albedo_path->text(), QStringLiteral("Textures/bronze_albedo.png"));
  EXPECT_EQ(metallic_path->text(),
            QStringLiteral("Textures/bronze_metallic.png"));
  EXPECT_EQ(normal_path->toolTip(), QStringLiteral("Normal preview fallback"));

  QString edited_name;
  std::vector<std::pair<QtStandardMaterialScalar, double>> scalar_edits;
  bool direct_x_edit = true;
  std::vector<
      std::pair<QtStandardMaterialTextureSlot, QtStandardMaterialTextureAction>>
      texture_actions;
  window->set_name_edited_callback(
      [&edited_name](QString value) { edited_name = std::move(value); });
  window->set_scalar_edited_callback(
      [&scalar_edits](QtStandardMaterialScalar scalar, double value) {
        scalar_edits.emplace_back(scalar, value);
      });
  window->set_normal_convention_edited_callback(
      [&direct_x_edit](bool direct_x_value) {
        direct_x_edit = direct_x_value;
      });
  window->set_texture_action_callback(
      [&texture_actions](QtStandardMaterialTextureSlot slot,
                         QtStandardMaterialTextureAction action) {
        texture_actions.emplace_back(slot, action);
      });

  name->setText(QStringLiteral("Aged Bronze"));
  ASSERT_TRUE(
      QMetaObject::invokeMethod(name, "editingFinished", Qt::DirectConnection));
  roughness->setValue(0.5);
  direct_x->setChecked(false);
  auto *normal_browse = window->findChild<QPushButton *>(
      QStringLiteral("standardMaterialEditor.normalTexture.browse"));
  auto *metallic_clear = window->findChild<QPushButton *>(
      QStringLiteral("standardMaterialEditor.metallicTexture.clear"));
  ASSERT_NE(normal_browse, nullptr);
  ASSERT_NE(metallic_clear, nullptr);
  normal_browse->click();
  metallic_clear->click();
  EXPECT_EQ(edited_name, QStringLiteral("Aged Bronze"));
  ASSERT_EQ(scalar_edits.size(), 1U);
  EXPECT_EQ(scalar_edits.front().first, QtStandardMaterialScalar::Roughness);
  EXPECT_DOUBLE_EQ(scalar_edits.front().second, 0.5);
  EXPECT_FALSE(direct_x_edit);
  ASSERT_EQ(texture_actions.size(), 2U);
  EXPECT_EQ(texture_actions[0].first, QtStandardMaterialTextureSlot::Normal);
  EXPECT_EQ(texture_actions[0].second, QtStandardMaterialTextureAction::Browse);
  EXPECT_EQ(texture_actions[1].first, QtStandardMaterialTextureSlot::Metallic);
  EXPECT_EQ(texture_actions[1].second, QtStandardMaterialTextureAction::Clear);

  int presentation_changes = 0;
  int resets = 0;
  window->set_presentation_state_changed_callback(
      [&presentation_changes]() { ++presentation_changes; });
  window->set_preview_reset_callback([&resets]() { ++resets; });
  QAction *rounded_box =
      find_action(*window, "standardMaterialEditor.previewMesh.roundedBox");
  auto *shading = window->findChild<QComboBox *>(
      QStringLiteral("standardMaterialEditor.previewShading"));
  QAction *reset = find_action(*window, "standardMaterialEditor.preview.reset");
  ASSERT_NE(rounded_box, nullptr);
  ASSERT_NE(shading, nullptr);
  ASSERT_NE(reset, nullptr);
  rounded_box->trigger();
  shading->setCurrentIndex(0);
  reset->trigger();
  EXPECT_EQ(window->preview_mesh(), QtStandardMaterialPreviewMesh::RoundedBox);
  EXPECT_EQ(window->preview_shading(),
            QtStandardMaterialPreviewShading::Wireframe);
  EXPECT_EQ(presentation_changes, 2);
  EXPECT_EQ(resets, 1);
  window->restore_presentation_state(QtStandardMaterialPreviewMesh::Plane,
                                     QtStandardMaterialPreviewShading::Shaded);
  EXPECT_EQ(presentation_changes, 2)
      << "restoring preferences must not write them back";

  auto *preview = new QLabel(QStringLiteral("Native preview container"));
  window->install_preview_widget(preview);
  EXPECT_EQ(preview->parentWidget(), window->preview_surface_host());
  auto *placeholder = window->findChild<QLabel *>(
      QStringLiteral("standardMaterialEditor.previewPlaceholder"));
  ASSERT_NE(placeholder, nullptr);
  EXPECT_TRUE(placeholder->isHidden());

  for (std::size_t index = 2; index < action_names.size(); ++index) {
    QAction *action = find_action(*window, action_names[index]);
    ASSERT_NE(action, nullptr);
    ASSERT_TRUE(action->isEnabled());
    action->trigger();
  }
  ASSERT_EQ(dispatched_file_actions.size(), 4U);
  EXPECT_EQ(dispatched_file_actions[2], file_actions[2]);
  EXPECT_EQ(dispatched_file_actions[3], file_actions[3]);

  bool allow_close = false;
  int close_requests = 0;
  window->set_close_request_handler([&allow_close, &close_requests]() {
    ++close_requests;
    return allow_close;
  });
  window->show();
  QCoreApplication::processEvents();
  EXPECT_TRUE(window->isVisible());
  EXPECT_FALSE(window->close());
  EXPECT_TRUE(window->isVisible());
  allow_close = true;
  EXPECT_TRUE(window->close());
  EXPECT_FALSE(window->isVisible());
  EXPECT_EQ(close_requests, 2);
  EXPECT_FALSE(window->testAttribute(Qt::WA_DeleteOnClose));

  window->clear_document();
  EXPECT_FALSE(window->has_document());
  EXPECT_FALSE(properties->isEnabled());
}

} // namespace
} // namespace quader
