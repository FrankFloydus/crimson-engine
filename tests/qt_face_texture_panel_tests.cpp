////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"
#include "api/quader_polygon_document_internal_access.hpp"
#include "ui/qt_application_theme.hpp"
#include "ui/qt_face_texture_canvas.hpp"
#include "ui/qt_face_texture_controls.hpp"
#include "ui/qt_face_texture_panel.hpp"
#include "ui/qt_material_browser_window.hpp"

#include <gtest/gtest.h>
#include <quader/modeling/neutral/polygon/document.hpp>

#include <QApplication>
#include <QAbstractEventDispatcher>
#include <QBuffer>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QGridLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>

#include <array>
#include <memory>
#include <optional>
#include <utility>

namespace quader {
namespace {

class QtFaceTexturePanelTests : public ::testing::Test {
protected:
  void settle_layout() {
    auto *dispatcher = QAbstractEventDispatcher::instance();
    ASSERT_NE(dispatcher, nullptr);
    for (int iteration = 0; iteration < 64; ++iteration) {
      if (!dispatcher->processEvents(QEventLoop::AllEvents)) {
        return;
      }
    }
    FAIL() << "Face Texture layout event queue did not settle";
  }

  void SetUp() override {
    ASSERT_EQ(QCoreApplication::instance(), nullptr);
    application_ = std::make_unique<QApplication>(argc_, argv_.data());
    QApplication::setStyle(QStringLiteral("Fusion"));
    standalone_.emplace(create_standalone_editor());
    const auto created = editor().modeling().create_box(
        {.min = {-64.0f, -32.0f, -16.0f},
         .max = {64.0f, 32.0f, 16.0f}}, "Face Texture Panel");
    ASSERT_TRUE(created.ok()) << created.error_message();
    object_ = created.value();
    ASSERT_TRUE(editor().selection().set_mode(QuaderSelectionMode::Face).ok());
    ASSERT_TRUE(editor().selection().select_face(object_, 1).ok());
    panel_ = std::make_unique<QtFaceTexturePanel>();
    panel_->setAttribute(Qt::WA_DontShowOnScreen);
    panel_->resize(700, 1000);
    panel_->bind(editor());
    panel_->show();
    settle_layout();
  }

  void TearDown() override {
    if (panel_ != nullptr) {
      panel_->abandon();
    }
    panel_.reset();
    standalone_.reset();
    application_.reset();
  }

  QuaderEditorApi &editor() { return standalone_->editor(); }

  QSplitter *canvas_splitter() const {
    return panel_->findChild<QSplitter *>(
        QStringLiteral("face_texture.canvas_splitter"));
  }

  QSplitter *browser_splitter() const {
    return panel_->findChild<QSplitter *>(
        QStringLiteral("face_texture.splitter"));
  }

  QDoubleSpinBox *number(const char *name) const {
    return panel_->findChild<QDoubleSpinBox *>(
        QStringLiteral("face_texture.texture.%1").arg(QString::fromLatin1(name)));
  }

  void move_canvas_handle(int distance) {
    auto *inner = canvas_splitter();
    ASSERT_NE(inner, nullptr);
    auto *handle = inner->handle(1);
    ASSERT_NE(handle, nullptr);
    const QPoint start = handle->rect().center();
    const QPoint global = handle->mapToGlobal(start);
    const QPoint movement(0, distance);
    QMouseEvent press(QEvent::MouseButtonPress, start, global,
                       Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(handle, &press);
    QMouseEvent move(QEvent::MouseMove, start + movement, global + movement,
                      Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(handle, &move);
    QMouseEvent release(QEvent::MouseButtonRelease, start, global + movement,
                         Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(handle, &release);
    settle_layout();
  }

  void prepare_screenshot_scene() {
    quader_poly::Document document;
    const std::array vertices{
        quader_poly::add_vertex(document, {-64.0f, -48.0f, 0.0f}),
        quader_poly::add_vertex(document, {64.0f, -48.0f, 0.0f}),
        quader_poly::add_vertex(document, {0.0f, 64.0f, 0.0f})};
    const auto face = quader_poly::add_face(document, vertices);
    document.faces.front().uvs = {{0.0f, 1.0f}, {1.0f, 1.0f}, {0.5f, 0.0f}};
    const auto created = editor().modeling().create_mesh(
        QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)),
        {.name = "Synthetic texture alignment triangle"});
    ASSERT_TRUE(created.ok()) << created.error_message();
    ASSERT_TRUE(editor().selection().select_face(created.value(), face).ok());
    ASSERT_TRUE(editor().modeling().apply_face_texture_action({
        .action = QuaderFaceTextureAction::FitBoth,
        .selection_token = editor().modeling().face_texture_settings().selection_token,
    }).ok());
    panel_->refresh();
    QImage checker(64, 64, QImage::Format_RGB32);
    for (int y = 0; y < checker.height(); ++y) {
      for (int x = 0; x < checker.width(); ++x) {
        const bool grout = x % 16 == 0 || y % 16 == 0;
        const bool alternate = (x / 16 + y / 16) % 2 != 0;
        checker.setPixelColor(x, y, grout ? QColor(45, 49, 52)
            : alternate ? QColor(119, 125, 130) : QColor(94, 101, 107));
      }
    }
    QByteArray encoded;
    QBuffer buffer(&encoded);
    ASSERT_TRUE(buffer.open(QIODevice::WriteOnly));
    ASSERT_TRUE(checker.save(&buffer, "PNG"));
    QtMaterialPresentationItem material;
    material.reference = panel_->canvas_material_reference();
    material.stable_id = qt_material_reference_stable_id(material.reference);
    material.name = QStringLiteral("Alignment grid");
    material.project_relative_path = QStringLiteral("Materials/Alignment_grid.qmat");
    material.encoded_albedo = encoded;
    material.has_valid_albedo = true;
    material.content_revision = 1;
    material.thumbnail = make_qt_material_thumbnail(material);
    panel_->set_material_presentation(material);
    panel_->browser().set_materials({material});
  }

  void expect_presented_settings(bool editable) {
    const auto settings = editor().modeling().face_texture_settings();
    ASSERT_TRUE(settings.scale_x.has_value());
    ASSERT_TRUE(settings.scale_y.has_value());
    const std::array values{*settings.scale_x, *settings.scale_y,
                             settings.shift_x, settings.shift_y,
                             settings.rotation_degrees};
    const std::array names{"scale_x", "scale_y", "shift_x", "shift_y", "rotate"};
    for (std::size_t index = 0; index < names.size(); ++index) {
      const auto *spin = number(names[index]);
      ASSERT_NE(spin, nullptr);
      EXPECT_EQ(spin->isEnabled(), editable);
      EXPECT_NEAR(spin->value(), values[index], 0.000001);
    }
  }

  int argc_ = 1;
  char executable_[32] = "quader-face-panel-tests";
  std::array<char *, 2> argv_{executable_, nullptr};
  std::unique_ptr<QApplication> application_;
  std::optional<QuaderStandaloneEditor> standalone_;
  QuaderModelingObjectRef object_;
  std::unique_ptr<QtFaceTexturePanel> panel_;
};

TEST_F(QtFaceTexturePanelTests,
       NumericAndActionRowsKeepMatchingCompactGapsAtEveryInterfaceScale) {
  const QString screenshot_path = qEnvironmentVariable("QUADER_FACE_TEXTURE_SCREENSHOT_DIR");
  if (!screenshot_path.isEmpty()) {
    const QString build_root = QDir(QString::fromUtf8(QUADER_SOURCE_DIR))
                                   .absoluteFilePath(QStringLiteral("build"));
    const QString target = QDir::cleanPath(QDir(screenshot_path).absolutePath());
    ASSERT_TRUE(target.startsWith(build_root + QLatin1Char('/'),
                                   Qt::CaseInsensitive));
    ASSERT_TRUE(QDir().mkpath(target));
    prepare_screenshot_scene();
  }
  QApplication::setPalette(qt_obsidian_application_palette(QApplication::palette()));
  QtUiScaleApplicationController scale_controller(*application_);
  for (const float scale : {1.0f, 1.5f, 2.0f}) {
    SCOPED_TRACE(scale);
    scale_controller.apply_factor(scale);
    const QtUiScaleMetrics metrics(scale, static_cast<std::uint64_t>(scale * 10));
    panel_->apply_ui_scale(metrics);
    panel_->resize(metrics.px(700), metrics.px(1000));
    settle_layout();
    auto *numeric_content = panel_->findChild<QWidget *>(
        QStringLiteral("face_texture.section.texture.content"));
    auto *actions_content = panel_->findChild<QWidget *>(
        QStringLiteral("face_texture.section.edit_texture.content"));
    ASSERT_NE(numeric_content, nullptr);
    ASSERT_NE(actions_content, nullptr);
    const auto *numeric_grid = qobject_cast<QGridLayout *>(numeric_content->layout());
    const auto *action_grid = qobject_cast<QGridLayout *>(actions_content->layout());
    ASSERT_NE(numeric_grid, nullptr);
    ASSERT_NE(action_grid, nullptr);
    EXPECT_EQ(numeric_grid->verticalSpacing(), metrics.px(4));
    EXPECT_EQ(numeric_grid->verticalSpacing(), action_grid->verticalSpacing());
    const auto *scale_x = number("scale_x");
    const auto *shift_x = number("shift_x");
    const auto *rotate = number("rotate");
    ASSERT_NE(scale_x, nullptr);
    ASSERT_NE(shift_x, nullptr);
    ASSERT_NE(rotate, nullptr);
    EXPECT_LE(shift_x->y() - scale_x->geometry().bottom() - 1, metrics.px(6));
    EXPECT_LE(rotate->y() - shift_x->geometry().bottom() - 1, metrics.px(6));
    const auto *rotation_label = panel_->findChild<QLabel *>(
        QStringLiteral("face_texture.texture.rotate.label"));
    ASSERT_NE(rotation_label, nullptr);
    EXPECT_EQ(rotation_label->text(), QStringLiteral("Rotation (\u00b0)"));
    auto *scroll = panel_->findChild<QScrollArea *>(
        QStringLiteral("face_texture.controls_scroll"));
    ASSERT_NE(scroll, nullptr);
    EXPECT_EQ(scroll->verticalScrollBar()->maximum(), 0)
        << "viewport=" << scroll->viewport()->height()
        << " widget=" << scroll->widget()->height()
        << " hint=" << scroll->widget()->sizeHint().height()
        << " min_hint=" << scroll->widget()->minimumSizeHint().height()
        << " hfw=" << scroll->widget()->heightForWidth(scroll->viewport()->width())
        << " area_hint=" << scroll->sizeHint().height();
    const int content_bottom = actions_content->mapTo(
        scroll->viewport(), QPoint(0, actions_content->height())).y();
    const int trailing_space = scroll->viewport()->height() - content_bottom;
    EXPECT_GE(trailing_space, 0);
    EXPECT_LE(trailing_space, metrics.px(12));
    if (!screenshot_path.isEmpty()) {
      QImage image(panel_->size(), QImage::Format_ARGB32_Premultiplied);
      image.fill(Qt::transparent);
      panel_->render(&image);
      const QString filename = QStringLiteral("face-texture-panel-%1.png")
                                   .arg(static_cast<int>(scale * 100));
      EXPECT_TRUE(image.save(QDir(screenshot_path).filePath(filename)));
    }
  }
}

TEST_F(QtFaceTexturePanelTests,
       CanvasHandleResizesWithoutMovingTheBrowserAndRetainsItsChosenHeight) {
  auto *inner = canvas_splitter();
  auto *outer = browser_splitter();
  ASSERT_NE(inner, nullptr);
  ASSERT_NE(outer, nullptr);
  const auto old_canvas = inner->sizes();
  const auto old_browser = outer->sizes();
  move_canvas_handle(50);
  EXPECT_GT(inner->sizes()[0], old_canvas[0]);
  EXPECT_EQ(outer->sizes(), old_browser);
  const auto chosen = inner->sizes();
  ASSERT_TRUE(editor().selection().clear().ok());
  panel_->refresh();
  ASSERT_TRUE(editor().selection().select_face(object_, 1).ok());
  panel_->refresh();
  settle_layout();
  EXPECT_EQ(inner->sizes(), chosen);
  EXPECT_EQ(outer->sizes(), old_browser);
}

TEST_F(QtFaceTexturePanelTests,
       SavesBothDividersAndMigratesTheOriginalTwoPartSplitterState) {
  auto *inner = canvas_splitter();
  auto *outer = browser_splitter();
  ASSERT_NE(inner, nullptr);
  ASSERT_NE(outer, nullptr);
  outer->setSizes({650, 350});
  inner->setSizes({350, 300});
  move_canvas_handle(1);
  const auto expected_browser = outer->sizes();
  const auto expected_canvas = inner->sizes();
  const QByteArray complete = panel_->save_splitter_state();
  const QByteArray legacy = outer->saveState();
  outer->setSizes({500, 500});
  inner->setSizes({100, 400});
  ASSERT_TRUE(panel_->restore_splitter_state(complete));
  EXPECT_EQ(outer->sizes(), expected_browser);
  EXPECT_EQ(inner->sizes(), expected_canvas);
  EXPECT_FALSE(panel_->restore_splitter_state(complete.chopped(1)));
  EXPECT_EQ(outer->sizes(), expected_browser);
  EXPECT_EQ(inner->sizes(), expected_canvas);
  outer->setSizes({500, 500});
  ASSERT_TRUE(panel_->restore_splitter_state(legacy));
  EXPECT_EQ(outer->sizes(), expected_browser);
  EXPECT_FALSE(panel_->restore_splitter_state({}));
}

TEST_F(QtFaceTexturePanelTests,
       RestoringAnAutomaticEmptyLayoutExpandsControlsWhenFacesAreSelected) {
  ASSERT_TRUE(editor().selection().clear().ok());
  panel_->refresh();
  settle_layout();
  const QByteArray empty_layout = panel_->save_splitter_state();

  QtFaceTexturePanel reopened;
  reopened.setAttribute(Qt::WA_DontShowOnScreen);
  reopened.resize(700, 1000);
  reopened.bind(editor());
  ASSERT_TRUE(reopened.restore_splitter_state(empty_layout));
  reopened.show();
  settle_layout();
  auto *controls = reopened.findChild<QScrollArea *>(
      QStringLiteral("face_texture.controls_scroll"));
  ASSERT_NE(controls, nullptr);
  const int empty_height = controls->height();
  ASSERT_TRUE(editor().selection().select_face(object_, 1).ok());
  reopened.refresh();
  settle_layout();
  EXPECT_GT(controls->height(), empty_height);
  EXPECT_GE(controls->height(), controls->widget()->sizeHint().height());
  EXPECT_EQ(controls->verticalScrollBar()->maximum(), 0);
  EXPECT_EQ(controls->horizontalScrollBar()->maximum(), 0);
  reopened.abandon();
}

TEST_F(QtFaceTexturePanelTests,
       PreviewValuesStayLiveWhileNumericEditingIsDisabledAndCancelRestoresThem) {
  const auto initial_settings = editor().modeling().face_texture_settings();
  const std::array kinds{QuaderFaceTextureGestureKind::Translate,
                          QuaderFaceTextureGestureKind::ScaleWidth,
                          QuaderFaceTextureGestureKind::ScaleHeight,
                          QuaderFaceTextureGestureKind::Rotate};
  for (const auto kind : kinds) {
    SCOPED_TRACE(static_cast<int>(kind));
    const auto presentation = editor().modeling().face_texture_canvas();
    ASSERT_TRUE(editor().modeling().begin_face_texture_gesture({
        .kind = kind, .selection_token = presentation.selection_token,
        .start = {8.0f, 8.0f}, .pivot = {0.0f, 0.0f}}).ok());
    panel_->refresh();
    expect_presented_settings(false);
    for (const QVec2 current : {QVec2{12.0f, 16.0f}, QVec2{20.0f, 24.0f}}) {
      ASSERT_TRUE(editor().modeling().update_face_texture_gesture(
          {.current = current}).ok());
      panel_->refresh();
      expect_presented_settings(false);
      const auto current_settings = editor().modeling().face_texture_settings();
      switch (kind) {
      case QuaderFaceTextureGestureKind::Translate:
        EXPECT_TRUE(current_settings.shift_x != initial_settings.shift_x ||
                    current_settings.shift_y != initial_settings.shift_y);
        break;
      case QuaderFaceTextureGestureKind::ScaleWidth:
        EXPECT_NE(current_settings.scale_x, initial_settings.scale_x);
        break;
      case QuaderFaceTextureGestureKind::ScaleHeight:
        EXPECT_NE(current_settings.scale_y, initial_settings.scale_y);
        break;
      case QuaderFaceTextureGestureKind::Rotate:
        EXPECT_NE(current_settings.rotation_degrees,
                   initial_settings.rotation_degrees);
        break;
      }
    }
    ASSERT_TRUE(editor().modeling().cancel_face_texture_gesture().ok());
    panel_->refresh();
    expect_presented_settings(true);
    EXPECT_NEAR(number("scale_x")->value(), *initial_settings.scale_x, 0.0001);
    EXPECT_NEAR(number("scale_y")->value(), *initial_settings.scale_y, 0.0001);
    EXPECT_NEAR(number("shift_x")->value(), initial_settings.shift_x, 0.0001);
    EXPECT_NEAR(number("shift_y")->value(), initial_settings.shift_y, 0.0001);
    EXPECT_NEAR(number("rotate")->value(), initial_settings.rotation_degrees, 0.0001);
  }
}

TEST_F(QtFaceTexturePanelTests,
       DifferentSelectedAnglesUseMixedPresentationAndAnAbsoluteEditUnifiesThem) {
  for (const auto [face, angle] : {std::pair{1, 30.0}, std::pair{2, 75.0}}) {
    ASSERT_TRUE(editor().selection().select_face(object_, face).ok());
    ASSERT_TRUE(editor().modeling().set_face_texture_setting({
        .field = QuaderFaceTextureField::Rotate, .value = angle,
        .selection_token = editor().modeling().face_texture_settings().selection_token,
    }).ok());
  }
  ASSERT_TRUE(editor().selection().select_faces(object_, {1, 2}).ok());
  panel_->refresh();
  auto *rotate = number("rotate");
  ASSERT_NE(rotate, nullptr);
  auto *line = rotate->findChild<QLineEdit *>();
  ASSERT_NE(line, nullptr);
  EXPECT_EQ(line->text(), QStringLiteral("--"));
  ASSERT_TRUE(editor().modeling().set_face_texture_setting({
      .field = QuaderFaceTextureField::Rotate, .value = 45.0,
      .selection_token = editor().modeling().face_texture_settings().selection_token,
  }).ok());
  panel_->refresh();
  EXPECT_NE(line->text(), QStringLiteral("--"));
  EXPECT_NEAR(rotate->value(), 45.0, 0.0001);
  for (const int face : {1, 2}) {
    ASSERT_TRUE(editor().selection().select_face(object_, face).ok());
    panel_->refresh();
    EXPECT_NEAR(rotate->value(), 45.0, 0.0001);
  }
}

TEST_F(QtFaceTexturePanelTests,
       UnchangedRefreshPreservesTheRetainedControlsAndCanvasSplit) {
  auto *rotate = number("rotate");
  ASSERT_NE(rotate, nullptr);
  const auto sizes = canvas_splitter()->sizes();
  rotate->setToolTip(QStringLiteral("retained presentation sentinel"));
  for (int frame = 0; frame < 30; ++frame) {
    panel_->refresh();
  }
  EXPECT_EQ(rotate->toolTip(), QStringLiteral("retained presentation sentinel"));
  EXPECT_EQ(canvas_splitter()->sizes(), sizes);
}

} // namespace
} // namespace quader
