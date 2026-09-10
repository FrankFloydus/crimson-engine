////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "core/localization_service.hpp"
#include "ui/qt_color_swatch_button.hpp"
#include "ui/qt_standard_material_editor_window.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QGroupBox>
#include <QImage>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPalette>
#include <QPixmap>
#include <QProxyStyle>
#include <QPushButton>
#include <QSlider>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <filesystem>
#include <utility>
#include <vector>

namespace quader {
namespace {

/// Observes tooltip publications independently of paint scheduling.
class MaterialTooltipEvents final : public QObject {
public:
  int changes = 0;

protected:
  bool eventFilter(QObject *, QEvent *event) override {
    if (event->type() == QEvent::ToolTipChange) {
      ++changes;
    }
    return false;
  }
};

QAction *find_action(QtStandardMaterialEditorWindow &window,
                     const char *object_name) {
  return window.findChild<QAction *>(QString::fromUtf8(object_name));
}

class PaletteTextureIconStyle final : public QProxyStyle {
public:
  PaletteTextureIconStyle() : QProxyStyle(QStringLiteral("fusion")) {}

  QIcon standardIcon(StandardPixmap standard_pixmap,
                     const QStyleOption *option,
                     const QWidget *widget) const override {
    QPalette::ColorRole role = QPalette::NoRole;
    switch (standard_pixmap) {
    case SP_DialogOpenButton:
      role = QPalette::ButtonText;
      break;
    case SP_DialogCancelButton:
      role = QPalette::Text;
      break;
    case SP_FileIcon:
      role = QPalette::Highlight;
      break;
    default:
      return QProxyStyle::standardIcon(standard_pixmap, option, widget);
    }
    const QPalette palette =
        widget != nullptr ? widget->palette() : QApplication::palette();
    QPixmap pixmap(16, 16);
    pixmap.fill(palette.color(role));
    return QIcon(pixmap);
  }
};

QColor icon_center_color(const QIcon &icon) {
  return icon.pixmap(QSize(16, 16)).toImage().pixelColor(8, 8);
}

TEST(QtStandardMaterialEditorWindowTests,
     DiffuseModelIsRetainedLocalizedAndSeparateFromPreviewShading) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-material-diffuse-model";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  QtStandardMaterialEditorWindow window;
  QPointer<QComboBox> diffuse = window.findChild<QComboBox *>(
      QStringLiteral("standardMaterialEditor.diffuseModel"));
  auto *shading = window.findChild<QComboBox *>(
      QStringLiteral("standardMaterialEditor.previewShading"));
  ASSERT_NE(diffuse, nullptr);
  ASSERT_NE(shading, nullptr);
  EXPECT_NE(diffuse.data(), shading);
  EXPECT_FALSE(diffuse->isEnabled());
  EXPECT_EQ(diffuse->count(), 2);
  EXPECT_EQ(diffuse->currentData().toInt(), static_cast<int>(QtStandardMaterialDiffuseModel::Burley));
  EXPECT_EQ(diffuse->accessibleName(), QStringLiteral("Diffuse Model"));
  EXPECT_EQ(diffuse->itemText(0), QStringLiteral("Burley"));
  EXPECT_EQ(diffuse->itemText(1), QStringLiteral("Lambert"));

  QtStandardMaterialEditorDocument document;
  document.stable_id = QStringLiteral("diffuse-model-document");
  document.session_generation = 1u;
  document.name = QStringLiteral("Diffuse Model Test");
  std::vector<QtStandardMaterialDiffuseModel> edits;
  window.set_diffuse_model_edited_callback([&](QtStandardMaterialDiffuseModel model) {
    edits.push_back(model);
    document.diffuse_model = model;
    document.dirty = true;
    window.set_document(document);
  });
  window.set_document(document);
  EXPECT_TRUE(diffuse->isEnabled());
  const int previous_shading = shading->currentIndex();
  const int lambert = diffuse->findData(static_cast<int>(QtStandardMaterialDiffuseModel::Lambert));
  ASSERT_GE(lambert, 0);
  diffuse->setCurrentIndex(lambert);
  diffuse->activated(lambert);
  ASSERT_EQ(edits.size(), 1u);
  EXPECT_EQ(edits.front(), QtStandardMaterialDiffuseModel::Lambert);
  EXPECT_EQ(document.diffuse_model, QtStandardMaterialDiffuseModel::Lambert);
  EXPECT_EQ(shading->currentIndex(), previous_shading);
  EXPECT_EQ(window.preview_shading(), QtStandardMaterialPreviewShading::Rendered);
  ASSERT_NE(diffuse, nullptr);
  EXPECT_EQ(window.findChild<QComboBox *>(
                QStringLiteral("standardMaterialEditor.diffuseModel")), diffuse.data());

  int value_signals = 0;
  QObject::connect(diffuse.data(), &QComboBox::currentIndexChanged,
                   [&](int) { ++value_signals; });
  window.set_document(document);
  EXPECT_EQ(value_signals, 0);
  EXPECT_EQ(edits.size(), 1u);
  QtStandardMaterialEditorLabels labels;
  labels.diffuse_model = QStringLiteral("Modello diffuso");
  labels.diffuse_burley = QStringLiteral("Burley locale");
  labels.diffuse_lambert = QStringLiteral("Lambert locale");
  window.set_labels(labels);
  ASSERT_NE(diffuse, nullptr);
  EXPECT_EQ(diffuse->accessibleName(), labels.diffuse_model);
  EXPECT_EQ(diffuse->itemText(0), labels.diffuse_burley);
  EXPECT_EQ(diffuse->itemText(1), labels.diffuse_lambert);
  EXPECT_EQ(diffuse->currentData().toInt(), static_cast<int>(QtStandardMaterialDiffuseModel::Lambert));
  EXPECT_EQ(edits.size(), 1u);
  EXPECT_EQ(value_signals, 0);

  document.session_generation = 2u;
  document.diffuse_model = QtStandardMaterialDiffuseModel::Burley;
  document.dirty = false;
  window.set_document(document);
  ASSERT_NE(diffuse, nullptr);
  EXPECT_EQ(diffuse->currentData().toInt(), static_cast<int>(QtStandardMaterialDiffuseModel::Burley));
  EXPECT_EQ(edits.size(), 1u);
  window.clear_document();
  EXPECT_FALSE(diffuse->isEnabled());
  EXPECT_EQ(edits.size(), 1u);
}

TEST(QtStandardMaterialEditorWindowTests,
     RefreshesOnlyPaletteDerivedTextureIconsOnPaletteChanges) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-standard-material-palette-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  application.setStyle(new PaletteTextureIconStyle());

  QPalette palette = QApplication::palette();
  const QColor initial_browse(211, 37, 53);
  const QColor initial_clear(29, 197, 83);
  const QColor initial_fallback(37, 89, 223);
  palette.setColor(QPalette::ButtonText, initial_browse);
  palette.setColor(QPalette::Text, initial_clear);
  palette.setColor(QPalette::Highlight, initial_fallback);
  QApplication::setPalette(palette);

  QtStandardMaterialEditorWindow window;
  QtStandardMaterialEditorDocument document;
  document.stable_id = QStringLiteral("palette-material");
  document.name = QStringLiteral("Palette Material");
  document.textures[0].preview_image =
      QImage(QSize(8, 8), QImage::Format_RGBA8888);
  document.textures[0].preview_image.fill(QColor(101, 103, 107));
  document.textures[0].pixel_size = QSize(8, 8);
  document.textures[0].content_revision = 1u;
  window.set_document(document);

  auto *browse = window.findChild<QToolButton *>(
      QStringLiteral("standardMaterialEditor.albedoTexture.browse"));
  auto *clear = window.findChild<QToolButton *>(
      QStringLiteral("standardMaterialEditor.albedoTexture.clear"));
  auto *image_preview = window.findChild<QToolButton *>(
      QStringLiteral("standardMaterialEditor.albedoTexture.preview"));
  auto *fallback_preview = window.findChild<QToolButton *>(
      QStringLiteral("standardMaterialEditor.roughnessTexture.preview"));
  auto *name = window.findChild<QLineEdit *>(
      QStringLiteral("standardMaterialEditor.name"));
  ASSERT_NE(browse, nullptr);
  ASSERT_NE(clear, nullptr);
  ASSERT_NE(image_preview, nullptr);
  ASSERT_NE(fallback_preview, nullptr);
  ASSERT_NE(name, nullptr);
  const qint64 initial_browse_key = browse->icon().cacheKey();
  const qint64 initial_clear_key = clear->icon().cacheKey();
  const qint64 initial_image_key = image_preview->icon().cacheKey();
  const qint64 initial_fallback_key = fallback_preview->icon().cacheKey();
  EXPECT_EQ(icon_center_color(browse->icon()), initial_browse);
  EXPECT_EQ(icon_center_color(clear->icon()), initial_clear);
  EXPECT_EQ(icon_center_color(fallback_preview->icon()), initial_fallback);

  const QColor application_browse(233, 151, 31);
  const QColor application_clear(53, 181, 229);
  const QColor application_fallback(173, 73, 229);
  palette.setColor(QPalette::ButtonText, application_browse);
  palette.setColor(QPalette::Text, application_clear);
  palette.setColor(QPalette::Highlight, application_fallback);
  QApplication::setPalette(palette);
  QCoreApplication::processEvents();
  const qint64 application_browse_key = browse->icon().cacheKey();
  const qint64 application_clear_key = clear->icon().cacheKey();
  const qint64 application_fallback_key = fallback_preview->icon().cacheKey();
  EXPECT_NE(application_browse_key, initial_browse_key);
  EXPECT_NE(application_clear_key, initial_clear_key);
  EXPECT_NE(application_fallback_key, initial_fallback_key);
  EXPECT_EQ(image_preview->icon().cacheKey(), initial_image_key);
  EXPECT_EQ(icon_center_color(browse->icon()), application_browse);
  EXPECT_EQ(icon_center_color(clear->icon()), application_clear);
  EXPECT_EQ(icon_center_color(fallback_preview->icon()),
            application_fallback);
  EXPECT_EQ(name->text(), document.name);

  const QColor local_browse(83, 211, 127);
  const QColor local_clear(229, 83, 97);
  const QColor local_fallback(79, 131, 241);
  palette.setColor(QPalette::ButtonText, local_browse);
  palette.setColor(QPalette::Text, local_clear);
  palette.setColor(QPalette::Highlight, local_fallback);
  window.setPalette(palette);
  QCoreApplication::processEvents();
  EXPECT_NE(browse->icon().cacheKey(), application_browse_key);
  EXPECT_NE(clear->icon().cacheKey(), application_clear_key);
  EXPECT_NE(fallback_preview->icon().cacheKey(), application_fallback_key);
  EXPECT_EQ(image_preview->icon().cacheKey(), initial_image_key);
  EXPECT_EQ(icon_center_color(browse->icon()), local_browse);
  EXPECT_EQ(icon_center_color(clear->icon()), local_clear);
  EXPECT_EQ(icon_center_color(fallback_preview->icon()), local_fallback);
  EXPECT_EQ(name->text(), document.name);
}

TEST(QtStandardMaterialEditorWindowTests,
     PresentsDraftAndDispatchesOnlySemanticEditorIntent) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-standard-material-editor-tests";
  char *argv[] = {application_name, nullptr};
  QLocale::setDefault(QLocale::c());
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  application.setQuitOnLastWindowClosed(false);

  QWidget owner;
  auto *window = new QtStandardMaterialEditorWindow(&owner);
  window->setAttribute(Qt::WA_DontShowOnScreen, true);
  EXPECT_EQ(window->windowType(), Qt::Tool);
  EXPECT_FALSE(window->testAttribute(Qt::WA_DeleteOnClose));
  EXPECT_TRUE(window->windowFlags().testFlag(Qt::WindowMaximizeButtonHint));
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
    EXPECT_TRUE(action->icon().isNull());
  }
  QAction *close_action =
      find_action(*window, "standardMaterialEditor.file.close");
  ASSERT_NE(close_action, nullptr);
  EXPECT_EQ(close_action->shortcut(),
            QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_W)));
  EXPECT_EQ(close_action->shortcutContext(), Qt::WindowShortcut);
  EXPECT_TRUE(close_action->icon().isNull());

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

  QImage albedo_preview(8, 4, QImage::Format_RGBA8888);
  albedo_preview.fill(QColor(205, 127, 50));
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
                      QStringLiteral("Textures/bronze_albedo.png"),
                  .preview_image = albedo_preview,
                  .format = QStringLiteral("RGBA8"),
                  .color_space = QStringLiteral("sRGB"),
                  .pixel_size = QSize(8, 4),
                  .encoded_byte_size = 96},
              QtStandardMaterialTexturePresentation{},
              QtStandardMaterialTexturePresentation{
                  .project_relative_path =
                      QStringLiteral("Textures/bronze_metallic.png")},
              QtStandardMaterialTexturePresentation{
                  .diagnostic = QStringLiteral("Normal preview fallback")},
          },
      .texture_transform =
          QtStandardMaterialTextureTransform{
              .center_u = 0.25,
              .center_v = 0.75,
              .scale_u = 2.0,
              .scale_v = -1.0,
              .offset_u = 0.125,
              .offset_v = -0.25,
              .rotation_degrees = 45.0,
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
  auto *albedo_button = window->findChild<QPushButton *>(
      QStringLiteral("standardMaterialEditor.albedo"));
  auto *albedo = dynamic_cast<QtColorSwatchButton *>(albedo_button);
  auto *roughness = window->findChild<QDoubleSpinBox *>(
      QStringLiteral("standardMaterialEditor.roughness"));
  auto *metallic = window->findChild<QDoubleSpinBox *>(
      QStringLiteral("standardMaterialEditor.metallic"));
  auto *normal_strength = window->findChild<QDoubleSpinBox *>(
      QStringLiteral("standardMaterialEditor.normalStrength"));
  auto *roughness_slider = window->findChild<QSlider *>(
      QStringLiteral("standardMaterialEditor.roughness.slider"));
  auto *metallic_slider = window->findChild<QSlider *>(
      QStringLiteral("standardMaterialEditor.metallic.slider"));
  auto *normal_strength_slider = window->findChild<QSlider *>(
      QStringLiteral("standardMaterialEditor.normalStrength.slider"));
  auto *direct_x = window->findChild<QCheckBox *>(
      QStringLiteral("standardMaterialEditor.directXNormal"));
  auto *albedo_path = window->findChild<QLabel *>(
      QStringLiteral("standardMaterialEditor.albedoTexture.path"));
  auto *metallic_path = window->findChild<QLabel *>(
      QStringLiteral("standardMaterialEditor.metallicTexture.path"));
  auto *normal_path = window->findChild<QLabel *>(
      QStringLiteral("standardMaterialEditor.normalTexture.path"));
  ASSERT_NE(properties, nullptr);
  ASSERT_NE(name, nullptr);
  ASSERT_NE(albedo, nullptr);
  ASSERT_NE(roughness, nullptr);
  ASSERT_NE(metallic, nullptr);
  ASSERT_NE(normal_strength, nullptr);
  ASSERT_NE(roughness_slider, nullptr);
  ASSERT_NE(metallic_slider, nullptr);
  ASSERT_NE(normal_strength_slider, nullptr);
  ASSERT_NE(direct_x, nullptr);
  ASSERT_NE(albedo_path, nullptr);
  ASSERT_NE(metallic_path, nullptr);
  ASSERT_NE(normal_path, nullptr);
  EXPECT_TRUE(properties->isEnabled());
  EXPECT_EQ(name->text(), QStringLiteral("Bronze"));
  EXPECT_EQ(window->findChild<QWidget *>(
                QStringLiteral("standardMaterialEditor.type")),
            nullptr);
  EXPECT_EQ(albedo->swatch_color(), QColor(205, 127, 50));
  EXPECT_TRUE(albedo->text().isEmpty());
  EXPECT_EQ(albedo->accessibleName(), QStringLiteral("Albedo"));
  EXPECT_DOUBLE_EQ(roughness->value(), 0.35);
  EXPECT_DOUBLE_EQ(metallic->value(), 0.8);
  EXPECT_DOUBLE_EQ(normal_strength->value(), 1.25);
  EXPECT_EQ(roughness_slider->minimum(), 0);
  EXPECT_EQ(roughness_slider->maximum(), 100);
  EXPECT_EQ(roughness_slider->singleStep(), 1);
  EXPECT_EQ(roughness_slider->value(), 35);
  EXPECT_EQ(metallic_slider->singleStep(), 1);
  EXPECT_EQ(metallic_slider->value(), 80);
  EXPECT_EQ(normal_strength->minimum(), -16.0);
  EXPECT_EQ(normal_strength->maximum(), 16.0);
  EXPECT_EQ(normal_strength_slider->minimum(), -1600);
  EXPECT_EQ(normal_strength_slider->maximum(), 1600);
  EXPECT_EQ(normal_strength_slider->singleStep(), 1);
  EXPECT_EQ(normal_strength_slider->value(), 125);
  EXPECT_TRUE(direct_x->isChecked());
  const QList<QDoubleSpinBox *> decimal_inputs =
      window->findChildren<QDoubleSpinBox *>();
  ASSERT_FALSE(decimal_inputs.isEmpty());
  for (const QDoubleSpinBox *decimal_input : decimal_inputs) {
    EXPECT_EQ(decimal_input->locale().decimalPoint(), QStringLiteral("."));
  }
  EXPECT_EQ(roughness->text(), QStringLiteral("0.35"));
  QLineEdit *roughness_input = roughness->findChild<QLineEdit *>();
  ASSERT_NE(roughness_input, nullptr);
  roughness_input->setText(QStringLiteral("0.51"));
  roughness->interpretText();
  EXPECT_DOUBLE_EQ(roughness->value(), 0.51);
  roughness->setValue(0.35);
  EXPECT_EQ(direct_x->text(), QStringLiteral("DirectX"));
  EXPECT_EQ(albedo_path->property("fullText").toString(),
            QStringLiteral("Textures/bronze_albedo.png"));
  EXPECT_EQ(metallic_path->property("fullText").toString(),
            QStringLiteral("Textures/bronze_metallic.png"));
  EXPECT_EQ(normal_path->toolTip(), QStringLiteral("Normal preview fallback"));
  albedo_path->resize(72, albedo_path->height());
  QCoreApplication::processEvents();
  EXPECT_NE(albedo_path->text(), QStringLiteral("Textures/bronze_albedo.png"));
  EXPECT_TRUE(albedo_path->text().endsWith(QStringLiteral(".png")));

  QString edited_name;
  std::vector<std::pair<QtStandardMaterialScalar, double>> scalar_edits;
  bool direct_x_edit = true;
  std::vector<
      std::pair<QtStandardMaterialTextureSlot, QtStandardMaterialTextureAction>>
      texture_actions;
  std::vector<std::pair<QtStandardMaterialTextureTransformComponent, double>>
      transform_edits;
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
  window->set_texture_transform_edited_callback(
      [&transform_edits](QtStandardMaterialTextureTransformComponent component,
                         double value) {
        transform_edits.emplace_back(component, value);
      });

  name->setText(QStringLiteral("Aged Bronze"));
  ASSERT_TRUE(
      QMetaObject::invokeMethod(name, "editingFinished", Qt::DirectConnection));
  roughness->setValue(0.5);
  auto *roughness_dispatch_timer = window->findChild<QTimer *>(
      QStringLiteral("standardMaterialEditor.roughness.slider.dispatchTimer"));
  ASSERT_NE(roughness_dispatch_timer, nullptr);
  EXPECT_TRUE(roughness_dispatch_timer->isSingleShot());
  roughness_slider->setSliderDown(true);
  roughness_slider->setValue(51);
  roughness_slider->setValue(67);
  roughness_slider->setValue(83);
  EXPECT_DOUBLE_EQ(roughness->value(), 0.83)
      << "the paired spin box must follow every live slider sample";
  ASSERT_EQ(scalar_edits.size(), 1U)
      << "live slider samples stay pending until the bounded dispatch";
  EXPECT_TRUE(roughness_dispatch_timer->isActive());
  ASSERT_TRUE(QMetaObject::invokeMethod(
      roughness_dispatch_timer, "timeout", Qt::DirectConnection));
  ASSERT_EQ(scalar_edits.size(), 2U);
  EXPECT_EQ(scalar_edits.back().first,
            QtStandardMaterialScalar::Roughness);
  EXPECT_DOUBLE_EQ(scalar_edits.back().second, 0.83)
      << "one interval dispatches only the latest pending sample";
  EXPECT_FALSE(roughness_dispatch_timer->isActive());
  roughness_slider->setValue(84);
  roughness_slider->setValue(91);
  EXPECT_DOUBLE_EQ(roughness->value(), 0.91);
  ASSERT_EQ(scalar_edits.size(), 2U);
  roughness_slider->setSliderDown(false);
  ASSERT_EQ(scalar_edits.size(), 3U);
  EXPECT_EQ(scalar_edits.back().first,
            QtStandardMaterialScalar::Roughness);
  EXPECT_DOUBLE_EQ(scalar_edits.back().second, 0.91)
      << "release must synchronously flush the exact final value";
  EXPECT_FALSE(roughness_dispatch_timer->isActive());
  direct_x->setChecked(false);
  auto *normal_browse = window->findChild<QToolButton *>(
      QStringLiteral("standardMaterialEditor.normalTexture.browse"));
  auto *metallic_clear = window->findChild<QToolButton *>(
      QStringLiteral("standardMaterialEditor.metallicTexture.clear"));
  ASSERT_NE(normal_browse, nullptr);
  ASSERT_NE(metallic_clear, nullptr);
  const QImage expected_browse_icon =
      window->style()
          ->standardIcon(QStyle::SP_DialogOpenButton)
          .pixmap(QSize(16, 16))
          .toImage();
  const QImage expected_clear_icon =
      window->style()
          ->standardIcon(QStyle::SP_DialogCancelButton)
          .pixmap(QSize(16, 16))
          .toImage();
  const std::array<const char *, 4> texture_names{
      "albedoTexture", "roughnessTexture", "metallicTexture",
      "normalTexture"};
  for (const char *texture_name : texture_names) {
    auto *browse = window->findChild<QToolButton *>(
        QStringLiteral("standardMaterialEditor.%1.browse").arg(texture_name));
    auto *clear = window->findChild<QToolButton *>(
        QStringLiteral("standardMaterialEditor.%1.clear").arg(texture_name));
    ASSERT_NE(browse, nullptr);
    ASSERT_NE(clear, nullptr);
    EXPECT_EQ(browse->icon().cacheKey(), normal_browse->icon().cacheKey());
    EXPECT_EQ(clear->icon().cacheKey(), metallic_clear->icon().cacheKey());
    EXPECT_EQ(browse->icon().pixmap(QSize(16, 16)).toImage(),
              expected_browse_icon);
    EXPECT_EQ(clear->icon().pixmap(QSize(16, 16)).toImage(),
              expected_clear_icon);

    const QList<QLabel *> labels = window->findChildren<QLabel *>();
    const auto buddy_label = std::find_if(
        labels.cbegin(), labels.cend(),
        [browse](const QLabel *label) { return label->buddy() == browse; });
    ASSERT_NE(buddy_label, labels.cend())
        << "each texture label must focus its actionable Browse control";
  }
  normal_browse->click();
  metallic_clear->click();
  EXPECT_EQ(edited_name, QStringLiteral("Aged Bronze"));
  ASSERT_EQ(scalar_edits.size(), 3U);
  EXPECT_EQ(scalar_edits[0].first, QtStandardMaterialScalar::Roughness);
  EXPECT_DOUBLE_EQ(scalar_edits[0].second, 0.5);
  EXPECT_EQ(scalar_edits[1].first, QtStandardMaterialScalar::Roughness);
  EXPECT_DOUBLE_EQ(scalar_edits[1].second, 0.83);
  EXPECT_EQ(scalar_edits[2].first, QtStandardMaterialScalar::Roughness);
  EXPECT_DOUBLE_EQ(scalar_edits[2].second, 0.91);
  EXPECT_FALSE(direct_x_edit);
  ASSERT_EQ(texture_actions.size(), 2U);
  EXPECT_EQ(texture_actions[0].first, QtStandardMaterialTextureSlot::Normal);
  EXPECT_EQ(texture_actions[0].second, QtStandardMaterialTextureAction::Browse);
  EXPECT_EQ(texture_actions[1].first, QtStandardMaterialTextureSlot::Metallic);
  EXPECT_EQ(texture_actions[1].second, QtStandardMaterialTextureAction::Clear);

  auto *center_u = window->findChild<QDoubleSpinBox *>(
      QStringLiteral("standardMaterialEditor.textureTransform.centerU"));
  auto *scale_v_slider = window->findChild<QSlider *>(
      QStringLiteral("standardMaterialEditor.textureTransform.scaleV.slider"));
  auto *scale_v = window->findChild<QDoubleSpinBox *>(
      QStringLiteral("standardMaterialEditor.textureTransform.scaleV"));
  auto *rotation = window->findChild<QDoubleSpinBox *>(
      QStringLiteral("standardMaterialEditor.textureTransform.rotation"));
  ASSERT_NE(center_u, nullptr);
  ASSERT_NE(scale_v_slider, nullptr);
  ASSERT_NE(scale_v, nullptr);
  ASSERT_NE(rotation, nullptr);
  EXPECT_DOUBLE_EQ(center_u->value(), 0.25);
  EXPECT_EQ(scale_v_slider->singleStep(), 1);
  EXPECT_EQ(scale_v_slider->value(), -100);
  EXPECT_DOUBLE_EQ(rotation->value(), 45.0);
  auto *scale_v_dispatch_timer = window->findChild<QTimer *>(
      QStringLiteral("standardMaterialEditor.textureTransform.scaleV.slider."
                     "dispatchTimer"));
  ASSERT_NE(scale_v_dispatch_timer, nullptr);
  EXPECT_TRUE(scale_v_dispatch_timer->isSingleShot());
  scale_v_slider->setSliderDown(true);
  scale_v_slider->setValue(-99);
  scale_v_slider->setValue(-25);
  scale_v_slider->setValue(125);
  EXPECT_DOUBLE_EQ(scale_v->value(), 1.25)
      << "transform presentation remains live while owner dispatch is bounded";
  EXPECT_TRUE(transform_edits.empty());
  EXPECT_TRUE(scale_v_dispatch_timer->isActive());
  ASSERT_TRUE(QMetaObject::invokeMethod(
      scale_v_dispatch_timer, "timeout", Qt::DirectConnection));
  ASSERT_EQ(transform_edits.size(), 1U);
  EXPECT_EQ(transform_edits.back().first,
            QtStandardMaterialTextureTransformComponent::ScaleV);
  EXPECT_DOUBLE_EQ(transform_edits.back().second, 1.25);
  EXPECT_FALSE(scale_v_dispatch_timer->isActive());
  scale_v_slider->setValue(150);
  scale_v_slider->setValue(225);
  EXPECT_DOUBLE_EQ(scale_v->value(), 2.25);
  ASSERT_EQ(transform_edits.size(), 1U);
  scale_v_slider->setSliderDown(false);
  ASSERT_EQ(transform_edits.size(), 2U);
  EXPECT_EQ(transform_edits.back().first,
            QtStandardMaterialTextureTransformComponent::ScaleV);
  EXPECT_DOUBLE_EQ(transform_edits.back().second, 2.25)
      << "transform release must synchronously flush the final sample";
  EXPECT_FALSE(scale_v_dispatch_timer->isActive());
  rotation->setValue(90.0);
  ASSERT_EQ(transform_edits.size(), 3U);
  EXPECT_EQ(transform_edits[0].first,
            QtStandardMaterialTextureTransformComponent::ScaleV);
  EXPECT_DOUBLE_EQ(transform_edits[0].second, 1.25);
  EXPECT_EQ(transform_edits[1].first,
            QtStandardMaterialTextureTransformComponent::ScaleV);
  EXPECT_DOUBLE_EQ(transform_edits[1].second, 2.25);
  EXPECT_EQ(transform_edits[2].first,
            QtStandardMaterialTextureTransformComponent::RotationDegrees);
  EXPECT_DOUBLE_EQ(transform_edits[2].second, 90.0);

  auto *albedo_texture_preview = window->findChild<QToolButton *>(
      QStringLiteral("standardMaterialEditor.albedoTexture.preview"));
  ASSERT_NE(albedo_texture_preview, nullptr);
  EXPECT_TRUE(albedo_texture_preview->isEnabled());
  EXPECT_EQ(albedo_texture_preview->size(), QSize(64, 64));
  EXPECT_EQ(albedo_texture_preview->iconSize(), QSize(56, 56));
  const QSize unscaled_editor_size = window->size();
  const qint64 unscaled_texture_icon_key =
      albedo_texture_preview->icon().cacheKey();
  window->apply_ui_scale(QtUiScaleMetrics(2.0F, 1U));
  EXPECT_EQ(albedo_texture_preview->size(), QSize(128, 128));
  EXPECT_EQ(albedo_texture_preview->iconSize(), QSize(112, 112));
  EXPECT_EQ(albedo_path->margin(), 6);
  EXPECT_EQ(albedo_path->minimumWidth(), 96);
  EXPECT_NE(albedo_texture_preview->icon().cacheKey(),
            unscaled_texture_icon_key)
      << "scale-dependent texture pixmaps must be regenerated";
  EXPECT_EQ(window->size(), unscaled_editor_size)
      << "scaling retained chrome must preserve the editor window geometry";
  const qint64 scaled_texture_icon_key =
      albedo_texture_preview->icon().cacheKey();
  window->apply_ui_scale(QtUiScaleMetrics(2.0F, 1U));
  EXPECT_EQ(albedo_texture_preview->icon().cacheKey(),
            scaled_texture_icon_key)
      << "reapplying one revision must not churn raster caches";
  albedo_texture_preview->click();
  QCoreApplication::processEvents();
  auto *texture_inspector = window->findChild<QMainWindow *>(
      QStringLiteral("standardMaterialEditor.textureInspector"));
  ASSERT_NE(texture_inspector, nullptr);
  EXPECT_EQ(texture_inspector->windowType(), Qt::Tool);
  EXPECT_FALSE(texture_inspector->testAttribute(Qt::WA_DeleteOnClose));
  EXPECT_FALSE(
      texture_inspector->windowFlags().testFlag(Qt::WindowMaximizeButtonHint));
  auto *texture_information = texture_inspector->findChild<QLabel *>(
      QStringLiteral("standardMaterialEditor.textureInspector.info"));
  auto *texture_canvas = texture_inspector->findChild<QWidget *>(
      QStringLiteral("standardMaterialEditor.textureInspector.canvas"));
  ASSERT_NE(texture_information, nullptr);
  ASSERT_NE(texture_canvas, nullptr);
  EXPECT_EQ(texture_canvas->minimumSize(), QSize(480, 360))
      << "a lazily created inspector must receive the current scale";
  window->apply_ui_scale(QtUiScaleMetrics(1.0F, 2U));
  EXPECT_EQ(albedo_texture_preview->size(), QSize(64, 64));
  EXPECT_EQ(albedo_texture_preview->iconSize(), QSize(56, 56));
  EXPECT_EQ(albedo_path->margin(), 3);
  EXPECT_EQ(texture_canvas->minimumSize(), QSize(240, 180));
  EXPECT_TRUE(texture_information->text().contains(
      QStringLiteral("bronze_albedo.png")));
  EXPECT_TRUE(texture_information->text().contains(QStringLiteral("RGBA8")));
  EXPECT_TRUE(texture_information->text().contains(QStringLiteral("sRGB")));
  document.textures[0].project_relative_path =
      QStringLiteral("Textures/replaced_albedo.png");
  document.textures[0].content_revision = 2u;
  window->set_document(document);
  EXPECT_TRUE(texture_information->text().contains(
      QStringLiteral("replaced_albedo.png")));
  EXPECT_TRUE(texture_information->text().contains(QStringLiteral("8 × 4")));
  const double texture_zoom_before =
      texture_canvas->property("zoom").toDouble();
  QWheelEvent texture_zoom(QPointF(30.0, 30.0), QPointF(30.0, 30.0), QPoint{},
                           QPoint{0, 120}, Qt::NoButton, Qt::NoModifier,
                           Qt::NoScrollPhase, false);
  QCoreApplication::sendEvent(texture_canvas, &texture_zoom);
  EXPECT_GT(texture_canvas->property("zoom").toDouble(), texture_zoom_before);
  QMouseEvent texture_pan_press(QEvent::MouseButtonPress, QPointF(10.0, 10.0),
                                QPointF(10.0, 10.0), Qt::LeftButton,
                                Qt::LeftButton, Qt::NoModifier);
  QCoreApplication::sendEvent(texture_canvas, &texture_pan_press);
  QMouseEvent texture_pan_move(QEvent::MouseMove, QPointF(30.0, 25.0),
                               QPointF(30.0, 25.0), Qt::NoButton,
                               Qt::LeftButton, Qt::NoModifier);
  QCoreApplication::sendEvent(texture_canvas, &texture_pan_move);
  EXPECT_NE(texture_canvas->property("panX").toDouble(), 0.0);

  const double inspected_zoom = texture_canvas->property("zoom").toDouble();
  const double inspected_pan_x = texture_canvas->property("panX").toDouble();
  document.roughness = 0.4;
  window->set_document(document);
  EXPECT_DOUBLE_EQ(texture_canvas->property("zoom").toDouble(),
                   inspected_zoom);
  EXPECT_DOUBLE_EQ(texture_canvas->property("panX").toDouble(),
                   inspected_pan_x);

  document.textures[0].preview_image = {};
  document.textures[0].content_revision = 3u;
  window->set_document(document);
  EXPECT_FALSE(texture_inspector->isVisible());

  int presentation_changes = 0;
  int resets = 0;
  window->set_presentation_state_changed_callback(
      [&presentation_changes]() { ++presentation_changes; });
  window->set_preview_reset_callback([&resets]() { ++resets; });
  window->set_preview_environments({
      QtStandardMaterialPreviewEnvironment{
          .stable_id = QStringLiteral("lightroom_14b"),
          .display_name = QStringLiteral("Lightroom")},
      QtStandardMaterialPreviewEnvironment{
          .stable_id = QStringLiteral("pillars_2k"),
          .display_name = QStringLiteral("Pillars")},
  });
  QAction *rounded_box =
      find_action(*window, "standardMaterialEditor.previewMesh.roundedBox");
  auto *shading = window->findChild<QComboBox *>(
      QStringLiteral("standardMaterialEditor.previewShading"));
  QAction *reset = find_action(*window, "standardMaterialEditor.preview.reset");
  QAction *pillars = find_action(
      *window, "standardMaterialEditor.environment.pillars_2k");
  QAction *show_background = find_action(
      *window, "standardMaterialEditor.environment.showBackground");
  EXPECT_EQ(find_action(*window, "standardMaterialEditor.view.showGrid"),
            nullptr);
  ASSERT_NE(rounded_box, nullptr);
  ASSERT_NE(shading, nullptr);
  ASSERT_NE(reset, nullptr);
  ASSERT_NE(pillars, nullptr);
  ASSERT_NE(show_background, nullptr);
  rounded_box->trigger();
  shading->setCurrentIndex(0);
  pillars->trigger();
  show_background->trigger();
  reset->trigger();
  EXPECT_EQ(window->preview_mesh(), QtStandardMaterialPreviewMesh::RoundedBox);
  EXPECT_EQ(window->preview_shading(),
            QtStandardMaterialPreviewShading::Wireframe);
  EXPECT_EQ(window->preview_environment(), QStringLiteral("pillars_2k"));
  EXPECT_TRUE(window->preview_environment_background_visible());
  EXPECT_EQ(presentation_changes, 4);
  EXPECT_EQ(resets, 1);
  std::size_t checked_meshes = 0;
  const std::array<const char *, 5> mesh_action_names{
      "standardMaterialEditor.previewMesh.sphere",
      "standardMaterialEditor.previewMesh.box",
      "standardMaterialEditor.previewMesh.roundedBox",
      "standardMaterialEditor.previewMesh.cylinder",
      "standardMaterialEditor.previewMesh.plane"};
  for (const char *mesh_action_name : mesh_action_names) {
    QAction *mesh_action = find_action(*window, mesh_action_name);
    ASSERT_NE(mesh_action, nullptr);
    checked_meshes += mesh_action->isChecked() ? 1U : 0U;
  }
  EXPECT_EQ(checked_meshes, 1U);
  std::size_t checked_environments = 0;
  for (const char *environment_action_name : {
           "standardMaterialEditor.environment.lightroom_14b",
           "standardMaterialEditor.environment.pillars_2k"}) {
    QAction *environment_action =
        find_action(*window, environment_action_name);
    ASSERT_NE(environment_action, nullptr);
    checked_environments += environment_action->isChecked() ? 1U : 0U;
  }
  EXPECT_EQ(checked_environments, 1U);
  window->restore_presentation_state(QtStandardMaterialPreviewMesh::Plane,
                                     QtStandardMaterialPreviewShading::Shaded,
                                     QStringLiteral("lightroom_14b"), false);
  EXPECT_EQ(window->preview_environment(),
            QStringLiteral("lightroom_14b"));
  EXPECT_FALSE(window->preview_environment_background_visible());
  EXPECT_EQ(presentation_changes, 4)
      << "restoring preferences must not write them back";

  auto *preview = new QLabel(QStringLiteral("Native preview container"));
  window->install_preview_widget(preview);
  EXPECT_EQ(preview->parentWidget(), window->preview_surface_host());
  EXPECT_TRUE(window->preview_surface_host()->toolTip().contains(
      QStringLiteral("Ctrl+MMB")));
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
  EXPECT_FALSE(texture_inspector->isVisible());
  EXPECT_EQ(close_requests, 2);
  EXPECT_FALSE(window->testAttribute(Qt::WA_DeleteOnClose));

  window->clear_document();
  EXPECT_FALSE(window->has_document());
  EXPECT_FALSE(properties->isEnabled());
}

TEST(QtStandardMaterialEditorWindowTests,
     PendingSliderEditsRespectDocumentAndLifecycleBoundaries) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-standard-material-editor-lifecycle-tests";
  char *argv[] = {application_name, nullptr};
  QLocale::setDefault(QLocale::c());
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  application.setQuitOnLastWindowClosed(false);

  QWidget owner;
  auto *window = new QtStandardMaterialEditorWindow(&owner);
  window->setAttribute(Qt::WA_DontShowOnScreen, true);
  auto *roughness = window->findChild<QDoubleSpinBox *>(
      QStringLiteral("standardMaterialEditor.roughness"));
  auto *roughness_slider = window->findChild<QSlider *>(
      QStringLiteral("standardMaterialEditor.roughness.slider"));
  auto *roughness_timer = window->findChild<QTimer *>(
      QStringLiteral("standardMaterialEditor.roughness.slider.dispatchTimer"));
  auto *scale_v = window->findChild<QDoubleSpinBox *>(
      QStringLiteral("standardMaterialEditor.textureTransform.scaleV"));
  auto *scale_v_slider = window->findChild<QSlider *>(
      QStringLiteral("standardMaterialEditor.textureTransform.scaleV.slider"));
  auto *scale_v_timer = window->findChild<QTimer *>(
      QStringLiteral("standardMaterialEditor.textureTransform.scaleV.slider."
                     "dispatchTimer"));
  ASSERT_NE(roughness, nullptr);
  ASSERT_NE(roughness_slider, nullptr);
  ASSERT_NE(roughness_timer, nullptr);
  ASSERT_NE(scale_v, nullptr);
  ASSERT_NE(scale_v_slider, nullptr);
  ASSERT_NE(scale_v_timer, nullptr);

  QtStandardMaterialEditorDocument first{
      .stable_id = QStringLiteral("material-first"),
      .name = QStringLiteral("First"),
      .roughness = 0.2,
      .texture_transform =
          QtStandardMaterialTextureTransform{.scale_v = 1.0},
  };
  QtStandardMaterialEditorDocument replacement{
      .stable_id = QStringLiteral("material-replacement"),
      .name = QStringLiteral("Replacement"),
      .roughness = 0.7,
      .texture_transform =
          QtStandardMaterialTextureTransform{.scale_v = 2.0},
  };
  std::vector<std::pair<QtStandardMaterialScalar, double>> scalar_edits;
  std::vector<std::pair<QtStandardMaterialTextureTransformComponent, double>>
      transform_edits;
  window->set_scalar_edited_callback(
      [&scalar_edits](QtStandardMaterialScalar scalar, double value) {
        scalar_edits.emplace_back(scalar, value);
      });
  window->set_texture_transform_edited_callback(
      [&transform_edits](QtStandardMaterialTextureTransformComponent component,
                         double value) {
        transform_edits.emplace_back(component, value);
      });

  window->set_document(first);
  roughness_slider->setSliderDown(true);
  roughness_slider->setValue(41);
  ASSERT_TRUE(roughness_timer->isActive());
  window->set_document(replacement);
  EXPECT_TRUE(scalar_edits.empty());
  EXPECT_FALSE(roughness_timer->isActive());
  EXPECT_DOUBLE_EQ(roughness->value(), 0.7)
      << "an authoritative replacement must restore its own value";
  ASSERT_TRUE(QMetaObject::invokeMethod(roughness_timer, "timeout",
                                        Qt::DirectConnection));
  EXPECT_TRUE(scalar_edits.empty())
      << "a stale timeout must not edit the replacement document";
  roughness_slider->setSliderDown(false);

  window->set_document(first);
  scale_v_slider->setSliderDown(true);
  scale_v_slider->setValue(230);
  ASSERT_TRUE(scale_v_timer->isActive());
  window->clear_document();
  EXPECT_TRUE(transform_edits.empty());
  EXPECT_FALSE(scale_v_timer->isActive());
  ASSERT_TRUE(QMetaObject::invokeMethod(scale_v_timer, "timeout",
                                        Qt::DirectConnection));
  EXPECT_TRUE(transform_edits.empty())
      << "a stale timeout must not edit after the document is cleared";
  scale_v_slider->setSliderDown(false);

  QtStandardMaterialEditorDocument authoritative = first;
  bool file_action_saw_latest_edit = false;
  window->set_scalar_edited_callback(
      [&](QtStandardMaterialScalar scalar, double value) {
        scalar_edits.emplace_back(scalar, value);
        authoritative.roughness = value;
        authoritative.dirty = true;
        window->set_document(authoritative);
      });
  window->set_file_action_callback(
      [&](QtStandardMaterialEditorFileAction action) {
        file_action_saw_latest_edit =
            action == QtStandardMaterialEditorFileAction::Save &&
            authoritative.dirty && authoritative.roughness == 0.64;
      });
  window->set_document(authoritative);
  roughness_slider->setSliderDown(true);
  roughness_slider->setValue(64);
  ASSERT_TRUE(roughness_timer->isActive());
  QAction *save_action =
      find_action(*window, "standardMaterialEditor.file.save");
  ASSERT_NE(save_action, nullptr);
  ASSERT_TRUE(save_action->isEnabled());
  save_action->trigger();
  EXPECT_TRUE(file_action_saw_latest_edit)
      << "file lifecycle decisions must observe the latest slider edit";
  EXPECT_FALSE(roughness_timer->isActive());
  roughness_slider->setSliderDown(false);

  bool close_request_saw_latest_edit = false;
  window->set_texture_transform_edited_callback(
      [&](QtStandardMaterialTextureTransformComponent component,
          double value) {
        transform_edits.emplace_back(component, value);
        authoritative.texture_transform.scale_v = value;
        authoritative.dirty = true;
        window->set_document(authoritative);
      });
  window->set_close_request_handler([&]() {
    close_request_saw_latest_edit =
        authoritative.dirty &&
        authoritative.texture_transform.scale_v == 2.75;
    return true;
  });
  authoritative.dirty = false;
  authoritative.texture_transform.scale_v = 1.0;
  window->set_document(authoritative);
  window->show();
  QCoreApplication::processEvents();
  scale_v_slider->setSliderDown(true);
  scale_v_slider->setValue(275);
  ASSERT_TRUE(scale_v_timer->isActive());
  EXPECT_TRUE(window->close());
  EXPECT_TRUE(close_request_saw_latest_edit)
      << "dirty-close resolution must run after the pending edit is applied";
  EXPECT_FALSE(scale_v_timer->isActive());
}

TEST(QtStandardMaterialEditorWindowTests,
     AlbedoPickerDispatchesLiveColorsBeforeAcceptAndSupportsCancel) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-material-live-color-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  application.setQuitOnLastWindowClosed(false);

  QWidget owner;
  auto *window = new QtStandardMaterialEditorWindow(&owner);
  window->setAttribute(Qt::WA_DontShowOnScreen, true);
  QtStandardMaterialEditorDocument document{
      .stable_id = QStringLiteral("live-color-material"),
      .name = QStringLiteral("Live Color"),
      .albedo = QColor(30, 60, 90),
  };
  window->set_document(document);
  auto *button = static_cast<QtColorSwatchButton *>(
      window->findChild<QPushButton *>(
          QStringLiteral("standardMaterialEditor.albedo")));
  ASSERT_NE(button, nullptr);
  int begin_count = 0;
  QColor original_color;
  bool original_dirty = false;
  std::vector<QColor> live_colors;
  std::vector<bool> finishes;
  window->set_albedo_preview_begin_callback([&]() {
    ++begin_count;
    original_color = document.albedo;
    original_dirty = document.dirty;
    return true;
  });
  window->set_albedo_edited_callback([&](QColor color) {
    live_colors.push_back(color);
    document.albedo = color;
    window->set_document(document);
  });
  window->set_albedo_preview_finish_callback([&](bool accept) {
    finishes.push_back(accept);
    if (accept) {
      document.dirty = original_dirty || document.albedo != original_color;
    } else {
      document.albedo = original_color;
      document.dirty = original_dirty;
    }
    window->set_document(document);
  });

  const QColor accepted_color(100, 150, 200);
  std::size_t live_count_before_accept = 0u;
  QTimer::singleShot(0, window, [&]() {
    auto *dialog = window->findChild<QColorDialog *>();
    ASSERT_NE(dialog, nullptr);
    EXPECT_TRUE(dialog->isVisible());
    EXPECT_EQ(begin_count, 1);
    dialog->setCurrentColor(QColor(70, 100, 130));
    EXPECT_EQ(document.albedo, QColor(70, 100, 130));
    EXPECT_TRUE(finishes.empty());
    EXPECT_FALSE(document.dirty);
    dialog->setCurrentColor(accepted_color);
    EXPECT_EQ(document.albedo, accepted_color);
    EXPECT_EQ(button->swatch_color(), accepted_color);
    EXPECT_TRUE(finishes.empty());
    live_count_before_accept = live_colors.size();
    dialog->accept();
  });
  button->click();
  EXPECT_EQ(document.albedo, accepted_color);
  EXPECT_TRUE(document.dirty);
  ASSERT_EQ(finishes.size(), 1u);
  EXPECT_TRUE(finishes.front());
  EXPECT_GE(live_count_before_accept, 2u);
  EXPECT_EQ(live_colors.size(), live_count_before_accept)
      << "acceptance must not dispatch the last color for a second edit";
  EXPECT_EQ(window->findChild<QPushButton *>(
                QStringLiteral("standardMaterialEditor.albedo")),
            button);

  QTimer::singleShot(0, window, [&]() {
    auto *dialog = window->findChild<QColorDialog *>();
    ASSERT_NE(dialog, nullptr);
    EXPECT_EQ(dialog->currentColor(), accepted_color);
    dialog->setCurrentColor(QColor(220, 100, 40));
    EXPECT_EQ(document.albedo, QColor(220, 100, 40));
    EXPECT_EQ(finishes.size(), 1u);
    dialog->reject();
  });
  button->click();
  EXPECT_EQ(begin_count, 2);
  ASSERT_EQ(finishes.size(), 2u);
  EXPECT_FALSE(finishes.back());
  EXPECT_EQ(document.albedo, accepted_color);
  EXPECT_EQ(button->swatch_color(), accepted_color);
  EXPECT_TRUE(document.dirty);
}

TEST(QtStandardMaterialEditorWindowTests,
     AlbedoPickerAbandonsCallbacksAcrossDocumentAndRuntimeBoundaries) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-material-color-lifecycle-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  QWidget owner;
  auto *window = new QtStandardMaterialEditorWindow(&owner);
  window->setAttribute(Qt::WA_DontShowOnScreen, true);
  const QtStandardMaterialEditorDocument first{
      .stable_id = QStringLiteral("color-first"),
      .session_generation = 1u,
      .name = QStringLiteral("First"),
      .albedo = QColor(25, 50, 75),
  };
  const QtStandardMaterialEditorDocument replacement{
      .stable_id = QStringLiteral("color-replacement"),
      .name = QStringLiteral("Replacement"),
      .albedo = QColor(150, 175, 200),
  };
  QtStandardMaterialEditorDocument reopened = first;
  reopened.session_generation = first.session_generation + 1u;
  reopened.albedo = QColor(175, 125, 75);
  auto *button = static_cast<QtColorSwatchButton *>(
      window->findChild<QPushButton *>(
          QStringLiteral("standardMaterialEditor.albedo")));
  ASSERT_NE(button, nullptr);
  int begin_count = 0;
  int color_count = 0;
  int finish_count = 0;
  window->set_albedo_preview_begin_callback([&]() {
    ++begin_count;
    return true;
  });
  window->set_albedo_edited_callback([&](QColor) { ++color_count; });
  window->set_albedo_preview_finish_callback([&](bool) { ++finish_count; });

  for (int boundary = 0; boundary < 4; ++boundary) {
    SCOPED_TRACE(boundary);
    window->set_document(first);
    const int colors_before = color_count;
    QTimer::singleShot(0, window, [&]() {
      auto *dialog = window->findChild<QColorDialog *>();
      ASSERT_NE(dialog, nullptr);
      // Owner-free abandonment preserves the last swatch presentation; use a
      // fresh sample in each case so Qt emits an actual color-change signal.
      const QColor next_color(50 + boundary * 20, 100, 150);
      EXPECT_NE(dialog->currentColor(), next_color);
      dialog->setCurrentColor(next_color);
      EXPECT_GT(color_count, colors_before);
      const int dispatched_colors = color_count;
      if (boundary == 0) {
        window->abandon_interactions();
      } else if (boundary == 1) {
        window->set_document(replacement);
      } else if (boundary == 2) {
        window->clear_document();
      } else {
        window->set_document(reopened);
      }
      EXPECT_FALSE(dialog->isVisible());
      dialog->setCurrentColor(QColor(200, 100, 50));
      EXPECT_EQ(color_count, dispatched_colors)
          << "late picker signals must not reach an outgoing owner";
      EXPECT_EQ(finish_count, 0);
    });
    button->click();
    EXPECT_EQ(finish_count, 0)
        << "abandonment must not invoke the outgoing finish callback";
    if (boundary == 1) {
      EXPECT_TRUE(window->has_document());
      EXPECT_EQ(button->swatch_color(), replacement.albedo);
    } else if (boundary == 2) {
      EXPECT_FALSE(window->has_document());
    } else if (boundary == 3) {
      EXPECT_TRUE(window->has_document());
      EXPECT_EQ(button->swatch_color(), reopened.albedo);
    }
  }
  EXPECT_EQ(begin_count, 4);

  window->set_document(first);
  window->set_albedo_preview_begin_callback([&]() {
    ++begin_count;
    return false;
  });
  bool rejected_begin_opened_dialog = false;
  const int colors_before_rejected_begin = color_count;
  QTimer::singleShot(0, window, [&]() {
    if (auto *dialog = window->findChild<QColorDialog *>()) {
      rejected_begin_opened_dialog = true;
      dialog->reject();
    }
  });
  button->click();
  QCoreApplication::processEvents();
  EXPECT_EQ(begin_count, 5);
  EXPECT_FALSE(rejected_begin_opened_dialog);
  EXPECT_EQ(color_count, colors_before_rejected_begin);
  EXPECT_EQ(finish_count, 0);
}

TEST(QtStandardMaterialEditorWindowTests,
     MaterialUvHelpExplainsSharedTilingAndRetainsLocalizedControls) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-material-uv-help-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QtStandardMaterialEditorWindow window;
  int edits = 0;
  window.set_texture_transform_edited_callback(
      [&](QtStandardMaterialTextureTransformComponent, double) { ++edits; });
  const QtStandardMaterialEditorDocument document{
      .stable_id = QStringLiteral("uv-help-material"),
      .name = QStringLiteral("UV Help"),
  };
  window.set_document(document);
  auto *group = window.findChild<QGroupBox *>(
      QStringLiteral("standardMaterialEditor.textureTransform"));
  ASSERT_NE(group, nullptr);
  constexpr std::array<const char *, 7> kNames{
      "centerU", "centerV", "scaleU", "scaleV", "offsetU", "offsetV", "rotation"};
  std::array<QPointer<QDoubleSpinBox>, kNames.size()> retained;
  for (std::size_t index = 0; index < kNames.size(); ++index) {
    retained[index] = window.findChild<QDoubleSpinBox *>(
        QStringLiteral("standardMaterialEditor.textureTransform.%1")
            .arg(QString::fromUtf8(kNames[index])));
    ASSERT_NE(retained[index], nullptr);
  }
  EXPECT_EQ(retained[2]->accessibleName(), QStringLiteral("UV Tiling U"));
  EXPECT_TRUE(retained[2]->toolTip().contains(QStringLiteral("smaller")));
  EXPECT_TRUE(group->toolTip().contains(QStringLiteral("every surface")));

  LocalizationService localization;
  ASSERT_TRUE(localization.load(
      std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "locales", "en-US"));
  for (const char *locale : {"en-US", "it-IT"}) {
    SCOPED_TRACE(locale);
    ASSERT_TRUE(localization.select_locale(locale));
    const auto text = [&](std::string_view suffix) {
      const auto value = localization.text(
          std::string("ui.standard_material_editor.texture_transform") +
              std::string(suffix), "");
      EXPECT_FALSE(value.empty());
      return QString::fromStdString(value);
    };
    QtStandardMaterialEditorLabels labels;
    labels.texture_transform = text("");
    labels.texture_center = text(".center");
    labels.texture_scale = text(".scale");
    labels.texture_offset = text(".offset");
    labels.texture_rotation = text(".rotation");
    labels.texture_transform_tooltip = text(".tooltip");
    labels.texture_center_tooltip = text(".center.tooltip");
    labels.texture_scale_tooltip = text(".scale.tooltip");
    labels.texture_offset_tooltip = text(".offset.tooltip");
    labels.texture_rotation_tooltip = text(".rotation.tooltip");
    window.set_labels(labels);
    QCoreApplication::processEvents();
    const std::array<QString, 7> tooltips{
        labels.texture_center_tooltip, labels.texture_center_tooltip,
        labels.texture_scale_tooltip, labels.texture_scale_tooltip,
        labels.texture_offset_tooltip, labels.texture_offset_tooltip,
        labels.texture_rotation_tooltip};
    EXPECT_EQ(group->toolTip(), labels.texture_transform_tooltip);
    MaterialTooltipEvents events;
    group->installEventFilter(&events);
    for (std::size_t index = 0; index < kNames.size(); ++index) {
      const auto stable_name =
          QStringLiteral("standardMaterialEditor.textureTransform.%1")
              .arg(QString::fromUtf8(kNames[index]));
      auto *spin = window.findChild<QDoubleSpinBox *>(stable_name);
      auto *slider = window.findChild<QSlider *>(stable_name + QStringLiteral(".slider"));
      ASSERT_EQ(spin, retained[index].data());
      ASSERT_NE(slider, nullptr);
      EXPECT_EQ(spin->toolTip(), tooltips[index]);
      EXPECT_EQ(slider->toolTip(), tooltips[index]);
      spin->installEventFilter(&events);
      slider->installEventFilter(&events);
    }
    EXPECT_EQ(retained[2]->accessibleName(), labels.texture_scale + QStringLiteral(" U"));
    for (int frame = 0; frame < 20; ++frame) {
      window.set_document(document);
    }
    EXPECT_EQ(events.changes, 0);
    EXPECT_EQ(edits, 0);
  }
}

} // namespace
} // namespace quader
