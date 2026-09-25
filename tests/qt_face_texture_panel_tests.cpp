////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"
#include "api/quader_polygon_document_internal_access.hpp"
#include "core/localization_service.hpp"
#include "ui/qt_application_theme.hpp"
#include "ui/qt_auxiliary_window_state.hpp"
#include "ui/qt_face_texture_canvas.hpp"
#include "ui/qt_face_texture_controls.hpp"
#include "ui/qt_face_texture_panel.hpp"
#include "ui/qt_material_browser_window.hpp"
#include "ui/qt_numeric_spin_box.hpp"

#include <gtest/gtest.h>
#include <quader/modeling/neutral/polygon/document.hpp>

#include <QApplication>
#include <QAbstractEventDispatcher>
#include <QBuffer>
#include <QCheckBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QGridLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QWheelEvent>

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace quader {
namespace {

QString face_texture_screenshot_directory() {
  const QString directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
  return directory.isEmpty()
             ? qEnvironmentVariable("QUADER_FACE_TEXTURE_SCREENSHOT_DIR")
             : directory;
}

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

  QToolButton *scale_link() const {
    return panel_->findChild<QToolButton *>(
        QStringLiteral("face_texture.texture.scale_link"));
  }

  void set_face_scales(int face, double u, double v) {
    ASSERT_TRUE(editor().selection().select_face(object_, face).ok());
    for (const auto [field, value] :
         {std::pair{QuaderFaceTextureField::ScaleX, u},
          std::pair{QuaderFaceTextureField::ScaleY, v}}) {
      ASSERT_TRUE(editor().modeling().set_face_texture_setting({
          .field = field, .value = value,
          .selection_token =
              editor().modeling().face_texture_settings().selection_token,
      }).ok());
    }
    panel_->refresh();
  }

  void enter_numeric_draft(QDoubleSpinBox &spin, double value) {
    application_->setActiveWindow(panel_.get());
    spin.setFocus(Qt::TabFocusReason);
    spin.selectAll();
    QKeyEvent draft(QEvent::KeyPress, 0, Qt::NoModifier,
                    spin.locale().toString(value, 'f', 4));
    QCoreApplication::sendEvent(&spin, &draft);
  }

  void accept_numeric_draft(QDoubleSpinBox &spin) {
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QCoreApplication::sendEvent(&spin, &enter);
  }

  void send_numeric_mouse(QLineEdit &line, QEvent::Type type,
                          const QPoint &position,
                          Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    const bool move = type == QEvent::MouseMove;
    const bool release = type == QEvent::MouseButtonRelease;
    QMouseEvent event(type, position, line.mapToGlobal(position),
                       move ? Qt::NoButton : Qt::LeftButton,
                       release ? Qt::NoButton : Qt::LeftButton,
                       modifiers);
    QCoreApplication::sendEvent(&line, &event);
  }

  QPoint begin_numeric_drag(QLineEdit &line,
                            Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    const QPoint press = line.rect().center();
    send_numeric_mouse(line, QEvent::MouseButtonPress, press, modifiers);
    const QPoint threshold = press + QPoint(QApplication::startDragDistance(), 0);
    send_numeric_mouse(line, QEvent::MouseMove, threshold, modifiers);
    return threshold;
  }

  void flush_numeric_timer(QDoubleSpinBox &spin) {
    auto *timer = spin.findChild<QTimer *>(QString(), Qt::FindDirectChildrenOnly);
    ASSERT_NE(timer, nullptr);
    ASSERT_TRUE(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
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
    const QtAuxiliaryWindowState defaults;
    panel_->browser().restore_presentation_state(
        defaults.face_texture_browser_grid_mode ? QtMaterialBrowserViewMode::Grid
                                                : QtMaterialBrowserViewMode::List,
        defaults.face_texture_browser_tile_size,
        defaults.face_texture_browser_sidebar_visible,
        defaults.face_texture_browser_sidebar_width);
  }

  void expect_presented_settings(bool editable) {
    const auto settings = editor().modeling().face_texture_settings();
    ASSERT_TRUE(settings.scale_x.has_value());
    ASSERT_TRUE(settings.scale_y.has_value());
    ASSERT_NE(scale_link(), nullptr);
    EXPECT_EQ(scale_link()->isEnabled(), editable);
    const std::array values{*settings.scale_x, *settings.scale_y,
                             settings.shift_x, settings.shift_y,
                             settings.rotation_degrees};
    const std::array names{"scale_x", "scale_y", "shift_x", "shift_y", "rotate"};
    for (std::size_t index = 0; index < names.size(); ++index) {
      const auto *spin = number(names[index]);
      ASSERT_NE(spin, nullptr);
      EXPECT_EQ(spin->isEnabled(), editable);
      const auto *label = panel_->findChild<QLabel *>(
          spin->objectName() + QStringLiteral(".label"));
      ASSERT_NE(label, nullptr);
      EXPECT_EQ(label->isEnabled(), editable);
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
       LinkedScaleTextPreservesProportionsAndCancelUndoAndUnlink) {
  set_face_scales(1, 2.0, -3.0);
  auto *link = scale_link();
  ASSERT_NE(link, nullptr);
  EXPECT_FALSE(link->isChecked());
  EXPECT_EQ(link->accessibleName(), QStringLiteral("Link Scale"));
  EXPECT_TRUE(link->toolTip().contains(QStringLiteral("proportions")));
  const auto revision = editor().revisions().modeling_content_revision;
  const auto undo_label = editor().history().snapshot().undo_label;
  link->click();
  ASSERT_TRUE(link->isChecked());
  EXPECT_EQ(editor().revisions().modeling_content_revision, revision);
  EXPECT_EQ(editor().history().snapshot().undo_label, undo_label);

  auto *u = number("scale_x");
  auto *v = number("scale_y");
  ASSERT_NE(u, nullptr);
  ASSERT_NE(v, nullptr);
  enter_numeric_draft(*u, -4.0);
  flush_numeric_timer(*u);
  EXPECT_EQ(editor().modeling().face_texture_settings().scale_x, -4.0);
  EXPECT_EQ(editor().modeling().face_texture_settings().scale_y, 6.0);
  QKeyEvent cancel(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  QCoreApplication::sendEvent(u, &cancel);
  EXPECT_EQ(editor().modeling().face_texture_settings().scale_x, 2.0);
  EXPECT_EQ(editor().modeling().face_texture_settings().scale_y, -3.0);
  EXPECT_FALSE(panel_->numeric_interaction_active());

  enter_numeric_draft(*u, 4.0);
  accept_numeric_draft(*u);
  EXPECT_EQ(editor().modeling().face_texture_settings().scale_x, 4.0);
  EXPECT_EQ(editor().modeling().face_texture_settings().scale_y, -6.0);
  ASSERT_TRUE(editor().history().undo().ok());
  EXPECT_EQ(editor().modeling().face_texture_settings().scale_x, 2.0);
  EXPECT_EQ(editor().modeling().face_texture_settings().scale_y, -3.0);
  ASSERT_TRUE(editor().history().redo().ok());
  panel_->refresh();
  EXPECT_DOUBLE_EQ(v->value(), -6.0);
  link->click();
  ASSERT_FALSE(link->isChecked());
  enter_numeric_draft(*v, -9.0);
  accept_numeric_draft(*v);
  EXPECT_EQ(editor().modeling().face_texture_settings().scale_x, 4.0);
  EXPECT_EQ(editor().modeling().face_texture_settings().scale_y, -9.0);
}

TEST_F(QtFaceTexturePanelTests,
       LinkedScaleScrubPublishesBothAxesAndEscapeRestoresMixedCompanions) {
  set_face_scales(1, 2.0, 6.0);
  set_face_scales(2, 2.0, -8.0);
  ASSERT_TRUE(editor().selection().select_faces(object_, {1, 2}).ok());
  panel_->refresh();
  auto *link = scale_link();
  ASSERT_NE(link, nullptr);
  link->click();
  auto *spin = number("scale_x");
  ASSERT_NE(spin, nullptr);
  auto *line = spin->findChild<QLineEdit *>();
  ASSERT_NE(line, nullptr);
  const auto revision = editor().revisions().modeling_content_revision;
  const QPoint anchor = begin_numeric_drag(*line);
  send_numeric_mouse(*line, QEvent::MouseMove, anchor + QPoint(100, 0));
  flush_numeric_timer(*spin);
  EXPECT_GT(editor().revisions().modeling_content_revision, revision);
  EXPECT_EQ(editor().modeling().face_texture_settings().scale_x, 3.0);
  QKeyEvent cancel(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  QCoreApplication::sendEvent(line, &cancel);
  EXPECT_FALSE(panel_->numeric_interaction_active());
  for (const auto [face, expected_v] :
       {std::pair{1, 6.0}, std::pair{2, -8.0}}) {
    ASSERT_TRUE(editor().selection().select_face(object_, face).ok());
    const auto settings = editor().modeling().face_texture_settings();
    EXPECT_EQ(settings.scale_x, 2.0);
    EXPECT_EQ(settings.scale_y, expected_v);
  }
  ASSERT_TRUE(editor().selection().select_faces(object_, {1, 2}).ok());
  panel_->refresh();
  const QPoint accepted_anchor = begin_numeric_drag(*line);
  send_numeric_mouse(*line, QEvent::MouseMove, accepted_anchor + QPoint(100, 0));
  flush_numeric_timer(*spin);
  send_numeric_mouse(*line, QEvent::MouseMove, accepted_anchor + QPoint(200, 0));
  send_numeric_mouse(*line, QEvent::MouseButtonRelease,
                      accepted_anchor + QPoint(200, 0));
  EXPECT_EQ(editor().modeling().face_texture_settings().scale_x, 4.0);
  for (const auto [face, expected_v] :
       {std::pair{1, 12.0}, std::pair{2, -16.0}}) {
    ASSERT_TRUE(editor().selection().select_face(object_, face).ok());
    EXPECT_EQ(editor().modeling().face_texture_settings().scale_y, expected_v);
  }
  ASSERT_TRUE(editor().history().undo().ok());
  for (const auto [face, expected_v] :
       {std::pair{1, 6.0}, std::pair{2, -8.0}}) {
    ASSERT_TRUE(editor().selection().select_face(object_, face).ok());
    const auto settings = editor().modeling().face_texture_settings();
    EXPECT_EQ(settings.scale_x, 2.0);
    EXPECT_EQ(settings.scale_y, expected_v);
  }
}

TEST_F(QtFaceTexturePanelTests,
       LinkedMixedScaleTextAndScrubDeferUntilAcceptedAndPreserveEachFaceRatio) {
  set_face_scales(1, 2.0, 6.0);
  set_face_scales(2, 4.0, -8.0);
  ASSERT_TRUE(editor().selection().select_faces(object_, {1, 2}).ok());
  panel_->refresh();
  auto *link = scale_link();
  ASSERT_NE(link, nullptr);
  link->click();
  auto *spin = number("scale_x");
  ASSERT_NE(spin, nullptr);
  auto *line = spin->findChild<QLineEdit *>();
  ASSERT_NE(line, nullptr);
  const auto revision = editor().revisions().modeling_content_revision;
  enter_numeric_draft(*spin, 8.0);
  flush_numeric_timer(*spin);
  EXPECT_EQ(editor().revisions().modeling_content_revision, revision);
  QKeyEvent cancel(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  QCoreApplication::sendEvent(spin, &cancel);
  EXPECT_EQ(editor().revisions().modeling_content_revision, revision);
  EXPECT_EQ(spin->text(), QStringLiteral("--"));
  const QPoint anchor = begin_numeric_drag(*line);
  send_numeric_mouse(*line, QEvent::MouseMove, anchor + QPoint(16, 0));
  flush_numeric_timer(*spin);
  EXPECT_EQ(editor().revisions().modeling_content_revision, revision);
  QKeyEvent cancel_drag(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  QCoreApplication::sendEvent(line, &cancel_drag);
  EXPECT_EQ(editor().revisions().modeling_content_revision, revision);
  EXPECT_EQ(spin->text(), QStringLiteral("--"));
  enter_numeric_draft(*spin, 8.0);
  accept_numeric_draft(*spin);
  EXPECT_FALSE(panel_->numeric_interaction_active());
  for (const auto [face, expected_v] :
       {std::pair{1, 24.0}, std::pair{2, -16.0}}) {
    ASSERT_TRUE(editor().selection().select_face(object_, face).ok());
    const auto settings = editor().modeling().face_texture_settings();
    EXPECT_EQ(settings.scale_x, 8.0);
    EXPECT_EQ(settings.scale_y, expected_v);
  }
  ASSERT_TRUE(editor().history().undo().ok());
  for (const auto &[face, scales] :
       {std::pair{1, std::pair{2.0, 6.0}},
        std::pair{2, std::pair{4.0, -8.0}}}) {
    ASSERT_TRUE(editor().selection().select_face(object_, face).ok());
    const auto settings = editor().modeling().face_texture_settings();
    EXPECT_EQ(settings.scale_x, scales.first);
    EXPECT_EQ(settings.scale_y, scales.second);
  }
}

TEST_F(QtFaceTexturePanelTests,
       ScaleLinkSettlesExistingDraftAndRetainsItsStateAcrossSelectionAndRebind) {
  set_face_scales(1, 2.0, 6.0);
  auto *link = scale_link();
  auto *spin = number("scale_x");
  ASSERT_NE(link, nullptr);
  ASSERT_NE(spin, nullptr);
  enter_numeric_draft(*spin, 4.0);
  link->click();
  ASSERT_TRUE(link->isChecked());
  EXPECT_EQ(editor().modeling().face_texture_settings().scale_x, 4.0);
  EXPECT_EQ(editor().modeling().face_texture_settings().scale_y, 6.0);
  EXPECT_FALSE(panel_->numeric_interaction_active());
  enter_numeric_draft(*spin, 8.0);
  link->click();
  ASSERT_FALSE(link->isChecked());
  EXPECT_EQ(editor().modeling().face_texture_settings().scale_x, 8.0);
  EXPECT_EQ(editor().modeling().face_texture_settings().scale_y, 12.0);
  enter_numeric_draft(*spin, 0.0);
  const auto revision = editor().revisions().modeling_content_revision;
  link->click();
  EXPECT_FALSE(link->isChecked());
  EXPECT_EQ(editor().revisions().modeling_content_revision, revision);
  QKeyEvent cancel(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  QCoreApplication::sendEvent(spin, &cancel);
  link->click();
  ASSERT_TRUE(link->isChecked());
  ASSERT_TRUE(editor().selection().clear().ok());
  panel_->refresh();
  EXPECT_FALSE(link->isEnabled());
  EXPECT_TRUE(link->isChecked());
  link->click();
  EXPECT_TRUE(link->isChecked());
  ASSERT_TRUE(editor().selection().select_face(object_, 2).ok());
  panel_->refresh();
  EXPECT_TRUE(link->isEnabled());
  EXPECT_TRUE(link->isChecked());
  auto rebound = create_standalone_editor();
  panel_->abandon();
  panel_->bind(rebound.editor());
  EXPECT_EQ(scale_link(), link);
  EXPECT_TRUE(link->isChecked());
  EXPECT_FALSE(link->isEnabled());
  panel_->abandon();
  panel_->bind(editor());
  EXPECT_TRUE(link->isEnabled());
  EXPECT_TRUE(link->isChecked());
}

TEST_F(QtFaceTexturePanelTests,
       ScaleScrubUsesHundredthsAndPublishesOneUndoableGestureOnRelease) {
  for (const char *axis : {"scale_x", "scale_y"}) {
    SCOPED_TRACE(axis);
    const bool horizontal = std::string_view(axis) == "scale_x";
    const auto field = horizontal ? QuaderFaceTextureField::ScaleX
                                  : QuaderFaceTextureField::ScaleY;
    ASSERT_TRUE(editor().modeling().set_face_texture_setting({
        .field = field, .value = 1.0,
        .selection_token = editor().modeling().face_texture_settings().selection_token,
    }).ok());
    panel_->refresh();
    auto *spin = number(axis);
    ASSERT_NE(spin, nullptr);
    auto *line = spin->findChild<QLineEdit *>();
    ASSERT_NE(line, nullptr);
    const QPoint anchor = begin_numeric_drag(*line);
    ASSERT_TRUE(qt_install_numeric_spin_box(*spin).is_dragging());
    send_numeric_mouse(*line, QEvent::MouseMove, anchor + QPoint(1, 0));
    EXPECT_DOUBLE_EQ(spin->value(), 1.01);
    flush_numeric_timer(*spin);
    const auto live = editor().modeling().face_texture_settings();
    EXPECT_NEAR((horizontal ? live.scale_x : live.scale_y).value_or(0.0),
                1.01, 0.000001);
    panel_->refresh();
    EXPECT_TRUE(panel_->numeric_interaction_active());
    send_numeric_mouse(*line, QEvent::MouseMove, anchor + QPoint(2, 0));
    EXPECT_DOUBLE_EQ(spin->value(), 1.02);
    send_numeric_mouse(*line, QEvent::MouseButtonRelease, anchor + QPoint(2, 0));
    EXPECT_FALSE(panel_->numeric_interaction_active());
    const auto committed = editor().modeling().face_texture_settings();
    EXPECT_NEAR((horizontal ? committed.scale_x : committed.scale_y).value_or(0.0),
                1.02, 0.000001);
    ASSERT_TRUE(editor().history().undo().ok());
    const auto restored = editor().modeling().face_texture_settings();
    EXPECT_EQ(horizontal ? restored.scale_x : restored.scale_y,
              std::optional<double>(1.0));
    panel_->refresh();
    const QPoint fine_anchor = begin_numeric_drag(*line, Qt::ShiftModifier);
    send_numeric_mouse(*line, QEvent::MouseMove, fine_anchor + QPoint(1, 0),
                        Qt::ShiftModifier);
    EXPECT_DOUBLE_EQ(spin->value(), 1.001);
    send_numeric_mouse(*line, QEvent::MouseButtonRelease,
                        fine_anchor + QPoint(1, 0), Qt::ShiftModifier);
    const auto fine = editor().modeling().face_texture_settings();
    EXPECT_NEAR((horizontal ? fine.scale_x : fine.scale_y).value_or(0.0),
                1.001, 0.000001);
    ASSERT_TRUE(editor().history().undo().ok());
    panel_->refresh();
  }
}

TEST_F(QtFaceTexturePanelTests,
       ContinuousTextureScrubPublishesWhileSamplesKeepArriving) {
  auto *spin = number("shift_x");
  ASSERT_NE(spin, nullptr);
  auto *line = spin->findChild<QLineEdit *>();
  ASSERT_NE(line, nullptr);
  const double original = editor().modeling().face_texture_settings().shift_x;
  const QPoint anchor = begin_numeric_drag(*line);
  ASSERT_TRUE(qt_install_numeric_spin_box(*spin).is_dragging());
  QEventLoop loop;
  QTimer samples;
  samples.setTimerType(Qt::PreciseTimer);
  samples.setInterval(4);
  QTimer deadline;
  deadline.setSingleShot(true);
  int sample_count = 0;
  bool published_while_sampling = false;
  QObject::connect(&samples, &QTimer::timeout, &loop, [&] {
    ++sample_count;
    send_numeric_mouse(*line, QEvent::MouseMove,
                        anchor + QPoint(sample_count * 8, 0));
    if (editor().modeling().face_texture_settings().shift_x != original) {
      published_while_sampling = true;
      loop.quit();
    }
  });
  QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
  samples.start();
  deadline.start(500);
  loop.exec();
  samples.stop();
  deadline.stop();
  EXPECT_TRUE(published_while_sampling)
      << "Continued pointer samples must not restart the dispatch timer";
  EXPECT_GT(sample_count, 1);
  EXPECT_TRUE(qt_install_numeric_spin_box(*spin).is_dragging());
  EXPECT_TRUE(panel_->numeric_interaction_active());
  const double final_value = spin->value();
  send_numeric_mouse(*line, QEvent::MouseButtonRelease,
                      anchor + QPoint(sample_count * 8, 0));
  EXPECT_FALSE(panel_->numeric_interaction_active());
  EXPECT_NEAR(editor().modeling().face_texture_settings().shift_x,
              final_value, 0.000001);
  ASSERT_TRUE(editor().history().undo().ok());
  EXPECT_DOUBLE_EQ(editor().modeling().face_texture_settings().shift_x, original);
}

TEST_F(QtFaceTexturePanelTests,
       ControlScaleScrubUsesApplicationGrid) {
  QObject grid_context;
  qt_set_numeric_snap_increment_provider(*application_, grid_context,
                                          [] { return 2.0; });
  ASSERT_TRUE(editor().modeling().set_face_texture_setting({
      .field = QuaderFaceTextureField::ScaleX, .value = 1.0,
      .selection_token = editor().modeling().face_texture_settings().selection_token,
  }).ok());
  panel_->refresh();
  auto *spin = number("scale_x");
  ASSERT_NE(spin, nullptr);
  auto *line = spin->findChild<QLineEdit *>();
  ASSERT_NE(line, nullptr);
  const QPoint anchor = begin_numeric_drag(*line, Qt::ControlModifier);
  send_numeric_mouse(*line, QEvent::MouseMove, anchor + QPoint(400, 0),
                      Qt::ControlModifier);
  EXPECT_DOUBLE_EQ(spin->value(), 6.0);
  send_numeric_mouse(*line, QEvent::MouseButtonRelease, anchor + QPoint(400, 0),
                      Qt::ControlModifier);
  EXPECT_FALSE(panel_->numeric_interaction_active());
  EXPECT_EQ(editor().modeling().face_texture_settings().scale_x,
            std::optional<double>(6.0));
  ASSERT_TRUE(editor().history().undo().ok());
  EXPECT_EQ(editor().modeling().face_texture_settings().scale_x,
            std::optional<double>(1.0));
}

TEST_F(QtFaceTexturePanelTests,
       CancelingScaleScrubRestoresPublishedValueAndDiscardsTrailingRelease) {
  auto *spin = number("scale_x");
  ASSERT_NE(spin, nullptr);
  auto *line = spin->findChild<QLineEdit *>();
  ASSERT_NE(line, nullptr);
  const double original = spin->value();
  const QPoint anchor = begin_numeric_drag(*line);
  send_numeric_mouse(*line, QEvent::MouseMove, anchor + QPoint(16, 0));
  ASSERT_NE(spin->value(), original);
  flush_numeric_timer(*spin);
  ASSERT_NE(editor().modeling().face_texture_settings().scale_x,
            std::optional<double>(original));
  QKeyEvent cancel(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  QCoreApplication::sendEvent(line, &cancel);
  EXPECT_FALSE(panel_->numeric_interaction_active());
  EXPECT_EQ(editor().modeling().face_texture_settings().scale_x,
            std::optional<double>(original));
  EXPECT_DOUBLE_EQ(spin->value(), original);
  const auto revision = editor().revisions().modeling_content_revision;
  send_numeric_mouse(*line, QEvent::MouseMove, anchor + QPoint(24, 0));
  send_numeric_mouse(*line, QEvent::MouseButtonRelease, anchor + QPoint(24, 0));
  flush_numeric_timer(*spin);
  EXPECT_EQ(editor().revisions().modeling_content_revision, revision);
}

TEST_F(QtFaceTexturePanelTests,
       MixedScaleScrubCancelsWithoutReplacingDistinctFaceValues) {
  for (const auto [face, scale] :
       {std::pair{1, 2.0}, std::pair{2, 8.0}}) {
    ASSERT_TRUE(editor().selection().select_face(object_, face).ok());
    ASSERT_TRUE(editor().modeling().set_face_texture_setting({
        .field = QuaderFaceTextureField::ScaleX, .value = scale,
        .selection_token = editor().modeling().face_texture_settings().selection_token,
    }).ok());
  }
  ASSERT_TRUE(editor().selection().select_faces(object_, {1, 2}).ok());
  panel_->refresh();
  ASSERT_FALSE(editor().modeling().face_texture_settings().scale_x.has_value());
  auto *spin = number("scale_x");
  ASSERT_NE(spin, nullptr);
  auto *line = spin->findChild<QLineEdit *>();
  ASSERT_NE(line, nullptr);
  const auto revision = editor().revisions().modeling_content_revision;
  const QPoint anchor = begin_numeric_drag(*line);
  send_numeric_mouse(*line, QEvent::MouseMove, anchor + QPoint(16, 0));
  EXPECT_DOUBLE_EQ(spin->value(), 1.16);
  flush_numeric_timer(*spin);
  EXPECT_EQ(editor().revisions().modeling_content_revision, revision);
  QKeyEvent cancel(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  QCoreApplication::sendEvent(line, &cancel);
  EXPECT_FALSE(panel_->numeric_interaction_active());
  EXPECT_EQ(spin->text(), QStringLiteral("--"));
  EXPECT_EQ(editor().revisions().modeling_content_revision, revision);
  for (const auto [face, scale] :
       {std::pair{1, 2.0}, std::pair{2, 8.0}}) {
    ASSERT_TRUE(editor().selection().select_face(object_, face).ok());
    EXPECT_EQ(editor().modeling().face_texture_settings().scale_x,
              std::optional<double>(scale));
  }
  ASSERT_TRUE(editor().selection().select_faces(object_, {1, 2}).ok());
  panel_->refresh();
  const QPoint committed_anchor = begin_numeric_drag(*line);
  send_numeric_mouse(*line, QEvent::MouseMove, committed_anchor + QPoint(16, 0));
  send_numeric_mouse(*line, QEvent::MouseButtonRelease,
                      committed_anchor + QPoint(16, 0));
  EXPECT_FALSE(panel_->numeric_interaction_active());
  EXPECT_NEAR(editor().modeling().face_texture_settings().scale_x.value_or(0.0),
              1.16, 0.000001);
}

TEST_F(QtFaceTexturePanelTests,
       SelectionChangeAbandonsHeldScrubBeforeRetainedFieldTargetsAnotherFace) {
  auto *spin = number("scale_x");
  ASSERT_NE(spin, nullptr);
  auto *line = spin->findChild<QLineEdit *>();
  ASSERT_NE(line, nullptr);
  const QPoint anchor = begin_numeric_drag(*line);
  send_numeric_mouse(*line, QEvent::MouseMove, anchor + QPoint(16, 0));
  ASSERT_TRUE(panel_->numeric_interaction_active());
  ASSERT_TRUE(editor().selection().select_face(object_, 2).ok());
  const auto settings = editor().modeling().face_texture_settings();
  panel_->refresh();
  EXPECT_FALSE(qt_install_numeric_spin_box(*spin).is_dragging());
  EXPECT_FALSE(panel_->numeric_interaction_active());
  const auto revision = editor().revisions().modeling_content_revision;
  send_numeric_mouse(*line, QEvent::MouseMove, anchor + QPoint(24, 0));
  send_numeric_mouse(*line, QEvent::MouseButtonRelease, anchor + QPoint(24, 0));
  flush_numeric_timer(*spin);
  EXPECT_EQ(editor().revisions().modeling_content_revision, revision);
  EXPECT_EQ(editor().modeling().face_texture_settings().scale_x, settings.scale_x);
}

TEST_F(QtFaceTexturePanelTests,
       NumericAndActionRowsAlignAtNarrowWidthsAndEveryInterfaceScale) {
  const QString screenshot_path = face_texture_screenshot_directory();
  if (!screenshot_path.isEmpty()) {
    const QString build_root = QDir(QString::fromUtf8(QUADER_SOURCE_DIR))
                                   .absoluteFilePath(QStringLiteral("build"));
    const QString target = QDir::cleanPath(QDir(screenshot_path).absolutePath());
    ASSERT_TRUE(target.startsWith(build_root + QLatin1Char('/'),
                                   Qt::CaseInsensitive));
    ASSERT_TRUE(QDir().mkpath(target));
    prepare_screenshot_scene();
  }
  apply_qt_obsidian_application_theme(QApplication::palette());
  QtUiScaleApplicationController scale_controller(*application_);
  int scale_iteration = 0;
  for (const float scale : {1.0f, 1.5f, 2.0f, 1.0f}) {
    SCOPED_TRACE(scale);
    SCOPED_TRACE(::testing::Message() << "scale_iteration=" << scale_iteration++);
    scale_controller.apply_factor(scale);
    const QtUiScaleMetrics metrics(scale, static_cast<std::uint64_t>(scale * 10));
    panel_->apply_ui_scale(metrics);
    for (const int width : {320, 700}) {
      SCOPED_TRACE(width);
      panel_->resize(metrics.px(width), metrics.px(1000));
      settle_layout();
      auto *numeric_content = panel_->findChild<QWidget *>(
          QStringLiteral("face_texture.section.texture.content"));
      auto *actions_content = panel_->findChild<QWidget *>(
          QStringLiteral("face_texture.section.edit_texture.content"));
      ASSERT_NE(numeric_content, nullptr);
      ASSERT_NE(actions_content, nullptr);
      const auto numeric_bounds = [numeric_content](const QWidget *widget) {
        return QRect(widget->mapTo(numeric_content, QPoint()), widget->size());
      };
      const auto *numeric_grid = qobject_cast<QGridLayout *>(numeric_content->layout());
      const auto *action_grid = qobject_cast<QGridLayout *>(actions_content->layout());
      ASSERT_NE(numeric_grid, nullptr);
      ASSERT_NE(action_grid, nullptr);
      EXPECT_EQ(numeric_grid->verticalSpacing(), metrics.px(4));
      EXPECT_EQ(numeric_grid->verticalSpacing(), action_grid->verticalSpacing());
      const auto *scale_x = number("scale_x");
      const auto *scale_y = number("scale_y");
      const auto *shift_x = number("shift_x");
      const auto *shift_y = number("shift_y");
      const auto *rotate = number("rotate");
      auto *link = scale_link();
      ASSERT_NE(scale_x, nullptr);
      ASSERT_NE(scale_y, nullptr);
      ASSERT_NE(shift_x, nullptr);
      ASSERT_NE(shift_y, nullptr);
      ASSERT_NE(rotate, nullptr);
      ASSERT_NE(link, nullptr);
      const auto *scale_y_label = panel_->findChild<QLabel *>(
          QStringLiteral("face_texture.texture.scale_y.label"));
      ASSERT_NE(scale_y_label, nullptr);
      EXPECT_LT(scale_x->geometry().right(), link->x());
      EXPECT_LT(link->geometry().right(), scale_y_label->x());
      EXPECT_EQ(link->x() - scale_x->geometry().right() - 1,
                numeric_grid->horizontalSpacing());
      EXPECT_EQ(scale_y_label->x() - link->geometry().right() - 1,
                numeric_grid->horizontalSpacing());
      EXPECT_TRUE(numeric_content->rect().contains(link->geometry()));
      EXPECT_GE(link->width(), metrics.px(24));
      EXPECT_EQ(link->iconSize(), metrics.size(QSize(16, 16)));
      EXPECT_EQ(scale_x->y(), scale_y->y());
      EXPECT_EQ(shift_x->y(), shift_y->y());
      EXPECT_LT(scale_x->geometry().right(), scale_y->x());
      EXPECT_LT(shift_x->geometry().right(), shift_y->x());
      EXPECT_EQ(scale_x->x(), shift_x->x());
      EXPECT_EQ(scale_x->width(), shift_x->width());
      EXPECT_EQ(scale_y->x(), shift_y->x());
      EXPECT_EQ(scale_y->width(), shift_y->width());
      EXPECT_GE(numeric_bounds(rotate).left(), scale_x->x());
      EXPECT_EQ(numeric_bounds(rotate).right(), scale_y->geometry().right());
      for (const auto &rows : {std::pair{scale_x, shift_x},
                                std::pair{shift_x, rotate}}) {
        const int gap = numeric_bounds(rows.second).top() -
                        numeric_bounds(rows.first).bottom() - 1;
        EXPECT_GE(gap, metrics.px(4));
      }
      std::array<QRect, 3> numeric_row_bounds;
      std::size_t numeric_index = 0;
      for (const char *id : {"scale_x", "scale_y", "shift_x", "shift_y", "rotate"}) {
        const auto *spin = number(id);
        ASSERT_NE(spin, nullptr);
        EXPECT_GE(spin->width(), metrics.px(64));
        EXPECT_TRUE(numeric_content->rect().contains(numeric_bounds(spin)));
        const auto *line = spin->findChild<QLineEdit *>();
        ASSERT_NE(line, nullptr);
        const int text_width = line->contentsRect().width() -
                               line->textMargins().left() -
                               line->textMargins().right();
        EXPECT_LE(line->fontMetrics().horizontalAdvance(line->displayText()),
                  text_width);
        const auto *label = panel_->findChild<QLabel *>(
            spin->objectName() + QStringLiteral(".label"));
        ASSERT_NE(label, nullptr);
        EXPECT_LT(label->geometry().right(), spin->x());
        EXPECT_LE(label->heightForWidth(label->width()), label->height());
        numeric_row_bounds[numeric_index / 2] =
            numeric_row_bounds[numeric_index / 2]
                .united(numeric_bounds(spin))
                .united(numeric_bounds(label));
        ++numeric_index;
      }
      numeric_row_bounds[0] = numeric_row_bounds[0].united(link->geometry());
      for (std::size_t row = 0; row < 2; ++row) {
        const int gap = numeric_row_bounds[row + 1].top() -
                        numeric_row_bounds[row].bottom() - 1;
        EXPECT_GE(gap, metrics.px(4));
        EXPECT_LE(gap, metrics.px(6));
      }
      const auto *align = panel_->findChild<QPushButton *>(
          QStringLiteral("face_texture.edit_texture.align_to_grid"));
      const auto *treat = panel_->findChild<QCheckBox *>(
          QStringLiteral("face_texture.edit_texture.treat_as_one"));
      ASSERT_NE(align, nullptr);
      ASSERT_NE(treat, nullptr);
      for (const char *id : {"rotate_clockwise", "flip_texture_horizontal",
                               "flip_face_horizontal", "fit_both", "justify_left"}) {
        const auto *action = panel_->findChild<QPushButton *>(
            QStringLiteral("face_texture.edit_texture.%1")
                .arg(QString::fromLatin1(id)));
        ASSERT_NE(action, nullptr);
        EXPECT_EQ(action->x(), align->x());
      }
      EXPECT_EQ(treat->x(), align->x());
      const auto *rotation_label = panel_->findChild<QLabel *>(
          QStringLiteral("face_texture.texture.rotate.label"));
      ASSERT_NE(rotation_label, nullptr);
      EXPECT_EQ(rotation_label->text(), QStringLiteral("Rotation (\u00b0)"));
      EXPECT_GE(rotation_label->contentsRect().width(),
                rotation_label->fontMetrics().horizontalAdvance(
                    rotation_label->text()));
      auto *scroll = panel_->findChild<QScrollArea *>(
          QStringLiteral("face_texture.controls_scroll"));
      ASSERT_NE(scroll, nullptr);
      EXPECT_EQ(scroll->horizontalScrollBar()->maximum(), 0);
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
        for (const bool linked : {false, true}) {
          if (link->isChecked() != linked) {
            link->click();
          }
          QImage image(panel_->size(), QImage::Format_ARGB32_Premultiplied);
          image.fill(Qt::transparent);
          panel_->render(&image);
          const QString filename =
              QStringLiteral("face-texture-panel-%1-%2-cycle%3-%4.png")
                  .arg(width).arg(static_cast<int>(scale * 100))
                  .arg(scale_iteration)
                  .arg(linked ? QStringLiteral("linked")
                              : QStringLiteral("unlinked"));
          EXPECT_TRUE(image.save(QDir(screenshot_path).filePath(filename)));
        }
      }
    }
  }
}

TEST_F(QtFaceTexturePanelTests,
       ScaleLinkCentralGutterKeepsPairedFieldsAlignedInRtlAfterScalingDown) {
  apply_qt_obsidian_application_theme(QApplication::palette());
  QtUiScaleApplicationController scale_controller(*application_);
  panel_->setLayoutDirection(Qt::RightToLeft);
  std::uint64_t revision = 1;
  for (const float factor : {1.0f, 2.0f, 1.0f}) {
    SCOPED_TRACE(factor);
    scale_controller.apply_factor(factor);
    const QtUiScaleMetrics metrics(factor, revision++);
    panel_->apply_ui_scale(metrics);
    panel_->resize(metrics.px(320), metrics.px(1000));
    settle_layout();
    const auto *u = number("scale_x");
    const auto *v = number("scale_y");
    const auto *shift_u = number("shift_x");
    const auto *shift_v = number("shift_y");
    const auto *rotation = number("rotate");
    const auto *link = scale_link();
    const auto *v_label = panel_->findChild<QLabel *>(
        QStringLiteral("face_texture.texture.scale_y.label"));
    const auto *rotation_label = panel_->findChild<QLabel *>(
        QStringLiteral("face_texture.texture.rotate.label"));
    const auto *numeric_content = panel_->findChild<QWidget *>(
        QStringLiteral("face_texture.section.texture.content"));
    ASSERT_NE(u, nullptr);
    ASSERT_NE(v, nullptr);
    ASSERT_NE(shift_u, nullptr);
    ASSERT_NE(shift_v, nullptr);
    ASSERT_NE(rotation, nullptr);
    ASSERT_NE(link, nullptr);
    ASSERT_NE(v_label, nullptr);
    ASSERT_NE(rotation_label, nullptr);
    ASSERT_NE(numeric_content, nullptr);
    EXPECT_LT(v_label->geometry().right(), link->x());
    EXPECT_LT(link->geometry().right(), u->x());
    EXPECT_EQ(u->x(), shift_u->x());
    EXPECT_EQ(u->width(), shift_u->width());
    EXPECT_EQ(v->x(), shift_v->x());
    EXPECT_EQ(v->width(), shift_v->width());
    const QRect rotation_bounds(rotation->mapTo(numeric_content, QPoint()),
                                 rotation->size());
    EXPECT_EQ(rotation_bounds.left(), v->x());
    EXPECT_LE(rotation_bounds.right(), u->geometry().right());
    EXPECT_GE(rotation_label->contentsRect().width(),
              rotation_label->fontMetrics().horizontalAdvance(
                  rotation_label->text()));
    for (const auto *spin : {u, v, shift_u, shift_v, rotation}) {
      EXPECT_GE(spin->width(), metrics.px(64));
      const auto *line = spin->findChild<QLineEdit *>();
      ASSERT_NE(line, nullptr);
      const int text_width = line->contentsRect().width() -
                             line->textMargins().left() -
                             line->textMargins().right();
      EXPECT_LE(line->fontMetrics().horizontalAdvance(line->displayText()),
                text_width);
    }
  }
}

TEST_F(QtFaceTexturePanelTests,
       SectionCollapseSettlesTheNumericDraftAndRetainsTheFaceAndControls) {
  auto *header = panel_->findChild<QToolButton *>(
      QStringLiteral("face_texture.section.texture.header"));
  auto *spin = number("scale_x");
  ASSERT_NE(header, nullptr);
  ASSERT_NE(spin, nullptr);
  const auto before = editor().modeling().face_texture_settings();
  const auto previous_undo = editor().history().snapshot().undo_label;
  const int canvas_height = panel_->canvas().height();
  application_->setActiveWindow(panel_.get());
  spin->setFocus(Qt::TabFocusReason);
  spin->selectAll();
  QKeyEvent draft(QEvent::KeyPress, 0, Qt::NoModifier,
                  spin->locale().toString(2.5, 'f', 1));
  QCoreApplication::sendEvent(spin, &draft);
  ASSERT_TRUE(spin->hasFocus());
  int edit_completions = 0;
  QObject::connect(spin, &QDoubleSpinBox::editingFinished, panel_.get(),
                   [&edit_completions]() { ++edit_completions; });
  header->click();
  settle_layout();
  EXPECT_FALSE(header->isChecked());
  EXPECT_EQ(header->arrowType(), Qt::RightArrow);
  EXPECT_FALSE(spin->isVisible());
  EXPECT_EQ(edit_completions, 1);
  EXPECT_FALSE(panel_->numeric_interaction_active());
  EXPECT_GE(panel_->canvas().height(), canvas_height);
  const auto collapsed = editor().modeling().face_texture_settings();
  EXPECT_EQ(collapsed.selection_token, before.selection_token);
  EXPECT_NEAR(collapsed.scale_x.value_or(0.0), 2.5, 0.000001);
  header->click();
  settle_layout();
  EXPECT_EQ(number("scale_x"), spin);
  EXPECT_TRUE(spin->isVisible());
  EXPECT_EQ(edit_completions, 1);
  ASSERT_TRUE(editor().history().undo().ok());
  EXPECT_EQ(editor().modeling().face_texture_settings().scale_x, before.scale_x);
  EXPECT_EQ(editor().history().snapshot().undo_label, previous_undo);
}

TEST_F(QtFaceTexturePanelTests,
       TextureAxesHaveConsistentNamesAndLocalizedHelp) {
  const std::array names{"scale_x", "scale_y", "shift_x", "shift_y"};
  const std::array english{"Scale U", "Scale V", "Shift U (px)", "Shift V (px)"};
  const std::array italian{"Scala U", "Scala V", "Spostamento U (px)",
                           "Spostamento V (px)"};
  std::array<QString, names.size()> fallback_help;
  for (std::size_t index = 0; index < names.size(); ++index) {
    const auto *spin = number(names[index]);
    ASSERT_NE(spin, nullptr);
    EXPECT_EQ(spin->accessibleName(), QString::fromLatin1(english[index]));
    fallback_help[index] = spin->toolTip();
    EXPECT_TRUE(fallback_help[index].contains(
        index % 2 == 0 ? QStringLiteral("width") : QStringLiteral("height")));
    EXPECT_TRUE(fallback_help[index].contains(QStringLiteral("mapped texture")));
    if (index < 2) {
      EXPECT_TRUE(fallback_help[index].contains(QStringLiteral("negative")));
      EXPECT_TRUE(fallback_help[index].contains(QStringLiteral("mirror")));
    }
  }
  const std::array action_ids{"fit_x", "fit_y", "flip_texture_horizontal",
                              "flip_texture_vertical", "flip_face_horizontal",
                              "flip_face_vertical"};
  std::array<QPushButton *, action_ids.size()> buttons;
  std::array<QString, action_ids.size()> fallback_names;
  std::array<QString, action_ids.size()> fallback_action_help;
  for (std::size_t index = 0; index < action_ids.size(); ++index) {
    buttons[index] = panel_->findChild<QPushButton *>(
        QStringLiteral("face_texture.edit_texture.%1")
            .arg(QString::fromLatin1(action_ids[index])));
    ASSERT_NE(buttons[index], nullptr);
    EXPECT_EQ(buttons[index]->text(),
              index % 2 == 0 ? QStringLiteral("U") : QStringLiteral("V"));
    fallback_names[index] = buttons[index]->accessibleName();
    fallback_action_help[index] = buttons[index]->toolTip();
  }
  LocalizationService localization;
  ASSERT_TRUE(localization.load(
      std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "locales", "en-US"));
  const auto content_revision = editor().revisions().modeling_content_revision;
  for (const char *locale : {"en-US", "it-IT", "en-US"}) {
    SCOPED_TRACE(locale);
    ASSERT_TRUE(localization.select_locale(locale));
    panel_->set_localization(localization);
    settle_layout();
    const bool is_english = localization.current_locale() == "en-US";
    for (std::size_t index = 0; index < names.size(); ++index) {
      const auto *spin = number(names[index]);
      const auto *label = panel_->findChild<QLabel *>(
          QStringLiteral("face_texture.texture.%1.label")
              .arg(QString::fromLatin1(names[index])));
      ASSERT_NE(spin, nullptr);
      ASSERT_NE(label, nullptr);
      const QString caption = QString::fromLatin1(
          is_english ? english[index] : italian[index]);
      EXPECT_EQ(label->text(), caption);
      EXPECT_EQ(spin->accessibleName(), caption);
      const auto help = localization.text(
          std::string("tool_settings.texture_") + names[index] + ".tooltip", "");
      EXPECT_EQ(spin->toolTip(), QString::fromStdString(help));
      EXPECT_EQ(label->toolTip(), spin->toolTip());
      if (is_english) {
        EXPECT_EQ(spin->toolTip(), fallback_help[index]);
      }
    }
    for (std::size_t index = 0; index < action_ids.size(); ++index) {
      auto *button = panel_->findChild<QPushButton *>(
          QStringLiteral("face_texture.edit_texture.%1")
              .arg(QString::fromLatin1(action_ids[index])));
      ASSERT_EQ(button, buttons[index]);
      EXPECT_EQ(button->text(),
                index % 2 == 0 ? QStringLiteral("U") : QStringLiteral("V"));
      EXPECT_EQ(button->accessibleName(), QString::fromStdString(localization.text(
          std::string("command.modeling.texture.") + action_ids[index], "")));
      EXPECT_TRUE(button->toolTip().contains(QString::fromStdString(localization.text(
          std::string("tool_settings.edit_texture.") + action_ids[index] + ".tooltip",
          ""))));
      if (is_english) {
        EXPECT_EQ(button->accessibleName(), fallback_names[index]);
        EXPECT_EQ(button->toolTip(), fallback_action_help[index]);
      }
    }
    EXPECT_EQ(editor().revisions().modeling_content_revision, content_revision);
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
       RestoringAnAutomaticEmptyLayoutKeepsControlsStableWhenFacesAreSelected) {
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
  EXPECT_EQ(controls->height(), empty_height);
  EXPECT_GE(controls->height(), controls->widget()->sizeHint().height());
  EXPECT_EQ(controls->verticalScrollBar()->maximum(), 0);
  EXPECT_EQ(controls->horizontalScrollBar()->maximum(), 0);
  reopened.abandon();
}

TEST_F(QtFaceTexturePanelTests,
       EmptySelectionRetainsDisabledControlsAndDiscardsPendingDrafts) {
  prepare_screenshot_scene();
  ASSERT_TRUE(editor().selection().select_face(object_, 1).ok());
  const std::array fields{QuaderFaceTextureField::ScaleX,
                          QuaderFaceTextureField::ScaleY,
                          QuaderFaceTextureField::ShiftX,
                          QuaderFaceTextureField::ShiftY,
                          QuaderFaceTextureField::Rotate};
  const std::array values{2.0, 4.0, 24.0, -16.0, 37.0};
  const std::array names{"scale_x", "scale_y", "shift_x", "shift_y", "rotate"};
  for (std::size_t index = 0; index < fields.size(); ++index) {
    ASSERT_TRUE(editor().modeling().set_face_texture_setting({
        .field = fields[index], .value = values[index],
        .selection_token = editor().modeling().face_texture_settings().selection_token,
    }).ok());
  }
  panel_->refresh();
  settle_layout();
  const auto settings = editor().modeling().face_texture_settings();
  const auto content_revision = editor().revisions().modeling_content_revision;
  const auto undo_label = editor().history().snapshot().undo_label;
  auto *section = panel_->findChild<QWidget *>(
      QStringLiteral("face_texture.section.texture"));
  auto *actions = panel_->findChild<QWidget *>(
      QStringLiteral("face_texture.section.edit_texture"));
  auto *treat_as_one = panel_->findChild<QCheckBox *>(
      QStringLiteral("face_texture.edit_texture.treat_as_one"));
  ASSERT_NE(section, nullptr);
  ASSERT_NE(actions, nullptr);
  ASSERT_NE(treat_as_one, nullptr);
  const auto buttons = actions->findChildren<QPushButton *>();
  ASSERT_EQ(buttons.size(), 17);
  std::array<QDoubleSpinBox *, names.size()> retained;
  for (std::size_t index = 0; index < names.size(); ++index) {
    retained[index] = number(names[index]);
    ASSERT_NE(retained[index], nullptr);
    EXPECT_FALSE(retained[index]->isReadOnly());
  }

  const auto expect_empty = [&] {
    EXPECT_FALSE(section->isHidden());
    EXPECT_FALSE(actions->isHidden());
    for (std::size_t index = 0; index < names.size(); ++index) {
      auto *spin = number(names[index]);
      ASSERT_EQ(spin, retained[index]);
      EXPECT_FALSE(spin->isEnabled());
      const auto *label = panel_->findChild<QLabel *>(
          spin->objectName() + QStringLiteral(".label"));
      ASSERT_NE(label, nullptr);
      EXPECT_FALSE(label->isEnabled());
      EXPECT_TRUE(spin->isReadOnly());
      EXPECT_EQ(spin->text(), QStringLiteral("\u2014"));
    }
    for (const auto *button : buttons) {
      EXPECT_FALSE(button->isEnabled()) << button->objectName().toStdString();
    }
    EXPECT_FALSE(treat_as_one->isEnabled());
    EXPECT_FALSE(panel_->numeric_interaction_active());
    EXPECT_EQ(panel_->canvas().presentation().selected_face_count, 0u);
    EXPECT_TRUE(panel_->canvas().presentation().outline.empty());
    QImage image(panel_->canvas().size(), QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    panel_->canvas().render(&image);
    QImage blank(image.size(), image.format());
    blank.fill(panel_->canvas().palette().color(QPalette::Base));
    EXPECT_EQ(image, blank) << "Empty selection must not paint a cached texture or gizmo";
  };
  ASSERT_TRUE(editor().selection().clear().ok());
  panel_->refresh();
  settle_layout();
  expect_empty();
  for (auto *spin : retained) {
    spin->setFocus(Qt::OtherFocusReason);
    spin->selectAll();
    QKeyEvent typing(QEvent::KeyPress, Qt::Key_9, Qt::NoModifier,
                      QStringLiteral("9"));
    QCoreApplication::sendEvent(spin, &typing);
    QKeyEvent stepping(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
    QCoreApplication::sendEvent(spin, &stepping);
    const QPoint center = spin->rect().center();
    QWheelEvent wheel(center, spin->mapToGlobal(center), {}, QPoint(0, 120),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(spin, &wheel);
    spin->stepUp();
    spin->stepDown();
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QCoreApplication::sendEvent(spin, &enter);
    EXPECT_EQ(spin->text(), QStringLiteral("\u2014"));
  }
  for (auto *button : buttons) {
    button->click();
  }
  treat_as_one->click();
  EXPECT_EQ(editor().revisions().modeling_content_revision, content_revision);
  EXPECT_EQ(editor().history().snapshot().undo_label, undo_label);
  EXPECT_FALSE(editor().modeling().face_texture_treat_as_one());
  number("rotate")->setToolTip(QStringLiteral("unchanged empty controls"));
  for (int frame = 0; frame < 30; ++frame) {
    panel_->refresh();
  }
  EXPECT_EQ(number("rotate")->toolTip(), QStringLiteral("unchanged empty controls"));
  expect_empty();

  ASSERT_TRUE(editor().selection().select_face(object_, 1).ok());
  panel_->refresh();
  expect_presented_settings(true);
  for (auto *spin : retained) {
    EXPECT_FALSE(spin->isReadOnly());
  }
  for (const auto *button : buttons) {
    EXPECT_TRUE(button->isEnabled());
  }
  auto *scale = number("scale_x");
  scale->setFocus(Qt::OtherFocusReason);
  scale->selectAll();
  QKeyEvent draft(QEvent::KeyPress, Qt::Key_9, Qt::NoModifier, QStringLiteral("9"));
  QCoreApplication::sendEvent(scale, &draft);
  ASSERT_TRUE(panel_->numeric_interaction_active());
  ASSERT_TRUE(editor().selection().clear().ok());
  panel_->refresh();
  QEventLoop loop;
  QTimer::singleShot(75, &loop, &QEventLoop::quit);
  loop.exec();
  settle_layout();
  expect_empty();
  EXPECT_EQ(editor().revisions().modeling_content_revision, content_revision);
  EXPECT_EQ(editor().history().snapshot().undo_label, undo_label);
  ASSERT_TRUE(editor().selection().select_face(object_, 1).ok());
  panel_->refresh();
  expect_presented_settings(true);
  const auto restored = editor().modeling().face_texture_settings();
  EXPECT_EQ(restored.scale_x, settings.scale_x);
  EXPECT_EQ(restored.scale_y, settings.scale_y);
  EXPECT_EQ(restored.shift_x, settings.shift_x);
  EXPECT_EQ(restored.shift_y, settings.shift_y);
  EXPECT_EQ(restored.rotation_degrees, settings.rotation_degrees);

  ASSERT_TRUE(editor().selection().clear().ok());
  panel_->refresh();
  apply_qt_obsidian_application_theme(QApplication::palette());
  QtUiScaleApplicationController scale_controller(*application_);
  const QString screenshot_path = face_texture_screenshot_directory();
  for (const float factor : {1.0f, 1.5f, 2.0f}) {
    SCOPED_TRACE(factor);
    scale_controller.apply_factor(factor);
    const QtUiScaleMetrics metrics(factor, static_cast<std::uint64_t>(factor * 100));
    panel_->apply_ui_scale(metrics);
    panel_->resize(metrics.px(700), metrics.px(1000));
    settle_layout();
    expect_empty();
    if (!screenshot_path.isEmpty()) {
      const QString build_root = QDir(QString::fromUtf8(QUADER_SOURCE_DIR))
                                     .absoluteFilePath(QStringLiteral("build"));
      const QString target = QDir::cleanPath(QDir(screenshot_path).absolutePath());
      ASSERT_TRUE(target.startsWith(build_root + QLatin1Char('/'),
                                     Qt::CaseInsensitive));
      ASSERT_TRUE(QDir().mkpath(target));
      QImage image(panel_->size(), QImage::Format_ARGB32_Premultiplied);
      image.fill(Qt::transparent);
      panel_->render(&image);
      EXPECT_TRUE(image.save(QDir(target).filePath(
          QStringLiteral("face-texture-panel-empty-%1.png")
              .arg(static_cast<int>(factor * 100)))));
    }
  }
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
  auto *link = scale_link();
  ASSERT_NE(link, nullptr);
  link->click();
  link->setToolTip(QStringLiteral("retained link sentinel"));
  for (int frame = 0; frame < 30; ++frame) {
    panel_->refresh();
  }
  EXPECT_EQ(rotate->toolTip(), QStringLiteral("retained presentation sentinel"));
  EXPECT_EQ(scale_link(), link);
  EXPECT_TRUE(link->isChecked());
  EXPECT_EQ(link->toolTip(), QStringLiteral("retained link sentinel"));
  EXPECT_EQ(canvas_splitter()->sizes(), sizes);
}

} // namespace
} // namespace quader
