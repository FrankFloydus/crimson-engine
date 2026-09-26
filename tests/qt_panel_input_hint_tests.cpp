////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "core/localization_service.hpp"
#include "editor/session/scene_session.hpp"
#include "ui/outliner_panel.hpp"
#include "ui/qt_contextual_input_hints.hpp"
#include "ui/qt_material_browser_window.hpp"
#include "ui/ui_context.hpp"
#include "ui/ui_settings.hpp"
#include "viewport/viewport_settings.hpp"

#include <QApplication>
#include <QDataStream>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QEnterEvent>
#include <QKeyEvent>
#include <QListView>
#include <QMimeData>
#include <QPushButton>
#include <QTreeView>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

#include <array>
#include <memory>

namespace quader {
namespace {

class QtPanelInputHintTests : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(QCoreApplication::instance(), nullptr);
    application_ = std::make_unique<QApplication>(argc_, argv_.data());
    application_->setQuitOnLastWindowClosed(false);
  }

  int argc_ = 1;
  char name_[32] = "quader-panel-input-hint-tests";
  std::array<char *, 2> argv_{name_, nullptr};
  std::unique_ptr<QApplication> application_;
};

void hover(QWidget &widget) {
  const QPoint point = widget.rect().center();
  QEnterEvent event(QPointF(point), QPointF(point),
                    QPointF(widget.mapToGlobal(point)));
  QCoreApplication::sendEvent(&widget, &event);
}

QTreeWidgetItem *folder(QTreeWidget &tree, const QString &path) {
  for (QTreeWidgetItemIterator it(&tree); *it != nullptr; ++it) {
    if ((*it)->data(0, Qt::UserRole).toString() == path) {
      return *it;
    }
  }
  return nullptr;
}

TEST_F(QtPanelInputHintTests, BrowserThumbnailHintRequiresBrowserFocusAndIsRetained) {
  QtMaterialBrowserWindow browser;
  browser.setAttribute(Qt::WA_DontShowOnScreen, true);
  browser.show();
  browser.activateWindow();
  auto *tree = browser.findChild<QTreeWidget *>(
      QStringLiteral("materialBrowser.folderTree"));
  ASSERT_NE(tree, nullptr);
  tree->setFocus();
  QCoreApplication::processEvents();
  hover(*tree->viewport());
  const int tile_size = browser.tile_size();
  const auto hints = qt_contextual_input_hints(*application_);
  ASSERT_EQ(hints.size(), 1u);
  EXPECT_EQ(hints.front().gesture, QuaderOperationInputGesture::ControlWheel);
  EXPECT_EQ(hints.front().label_key, "operation.hints.browser.thumbnail_size");
  const auto state = qt_contextual_input_hints_state(*application_);
  for (int index = 0; index < 5; ++index) {
    EXPECT_EQ(qt_contextual_input_hints_state(*application_).revision, state.revision);
    EXPECT_EQ(qt_contextual_input_hints(*application_).data(), hints.data());
  }
  EXPECT_EQ(browser.tile_size(), tile_size);
  QKeyEvent control(QEvent::KeyPress, Qt::Key_Control, Qt::ControlModifier);
  QCoreApplication::sendEvent(tree, &control);
  EXPECT_TRUE(qt_contextual_input_hints(*application_).front().active);
  tree->clearFocus();
  EXPECT_TRUE(qt_contextual_input_hints(*application_).empty());
  EXPECT_TRUE(qt_contextual_input_hints_state(*application_).overrides_viewport);
}

TEST_F(QtPanelInputHintTests, BrowserDragHintUsesOnlyValidatedFolderIntent) {
  QtMaterialBrowserWindow browser;
  browser.setAttribute(Qt::WA_DontShowOnScreen, true);
  browser.set_project_generation(5);
  browser.set_folders({QStringLiteral("Materials/Source"),
                       QStringLiteral("Materials/Target")});
  int mutations = 0;
  browser.set_move_folder_callback(
      [&](QString, QString, std::uint64_t) { ++mutations; });
  browser.show();
  QCoreApplication::processEvents();
  auto *tree = browser.findChild<QTreeWidget *>(
      QStringLiteral("materialBrowser.folderTree"));
  ASSERT_NE(tree, nullptr);
  tree->expandAll();
  auto *target = folder(*tree, QStringLiteral("Materials/Target"));
  auto *source = folder(*tree, QStringLiteral("Materials/Source"));
  ASSERT_NE(target, nullptr);
  ASSERT_NE(source, nullptr);
  QByteArray bytes;
  QDataStream stream(&bytes, QIODevice::WriteOnly);
  stream << quint64{5} << QStringLiteral("Materials/Source");
  QMimeData mime;
  mime.setData(QStringLiteral("application/x-quader-material-browser-folder"), bytes);
  const QPoint target_point = tree->visualItemRect(target).center();
  QDragEnterEvent enter(target_point, Qt::MoveAction, &mime,
                        Qt::LeftButton, Qt::ControlModifier);
  QCoreApplication::sendEvent(tree->viewport(), &enter);
  QDragMoveEvent move(target_point, Qt::MoveAction, &mime,
                       Qt::LeftButton, Qt::ControlModifier);
  QCoreApplication::sendEvent(tree->viewport(), &move);
  ASSERT_TRUE(move.isAccepted());
  auto hints = qt_contextual_input_hints(*application_);
  ASSERT_EQ(hints.size(), 1u);
  EXPECT_EQ(hints.front().gesture, QuaderOperationInputGesture::PrimaryDrag);
  EXPECT_EQ(hints.front().label_key, "operation.hints.browser.move_to");
  EXPECT_EQ(hints.front().detail, "Materials/Target");
  EXPECT_TRUE(hints.front().active);
  const auto revision = qt_contextual_input_hints_state(*application_).revision;
  QCoreApplication::sendEvent(tree->viewport(), &move);
  EXPECT_EQ(qt_contextual_input_hints_state(*application_).revision, revision);
  EXPECT_EQ(mutations, 0);

  browser.hide();
  browser.show();
  QCoreApplication::processEvents();
  hover(*tree->viewport());
  for (const auto &hint : qt_contextual_input_hints(*application_)) {
    EXPECT_NE(hint.label_key, "operation.hints.browser.move_to");
  }

  QCoreApplication::sendEvent(tree->viewport(), &enter);
  QCoreApplication::sendEvent(tree->viewport(), &move);
  ASSERT_EQ(qt_contextual_input_hints(*application_).size(), 1u);
  EXPECT_EQ(qt_contextual_input_hints(*application_).front().label_key,
            "operation.hints.browser.move_to");

  QDragMoveEvent invalid(tree->visualItemRect(source).center(), Qt::MoveAction,
                          &mime, Qt::LeftButton, Qt::NoModifier);
  QCoreApplication::sendEvent(tree->viewport(), &invalid);
  EXPECT_FALSE(invalid.isAccepted());
  EXPECT_TRUE(qt_contextual_input_hints(*application_).empty());
  browser.set_purpose(QtMaterialBrowserPurpose::Picker);
  QCoreApplication::sendEvent(tree->viewport(), &move);
  hints = qt_contextual_input_hints(*application_);
  for (const auto &hint : hints) {
    EXPECT_NE(hint.label_key, "operation.hints.browser.move_to");
  }
  EXPECT_EQ(mutations, 0);
}

TEST_F(QtPanelInputHintTests, OutlinerHintsRequireDragAndValidatedDropWithoutMutation) {
  SceneSession session;
  QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto source = editor.scene().create_entity({
      .name = "Source", .kind = QuaderEntityKind::Empty});
  const auto target = editor.scene().create_entity({
      .name = "Target", .kind = QuaderEntityKind::Empty});
  ASSERT_TRUE(source.ok());
  ASSERT_TRUE(target.ok());
  UISettings settings = UISettings::defaults();
  ViewportSettings viewport = ViewportSettings::defaults();
  LocalizationService localization;
  UIContext context{
      .intent = {.editor = editor},
      .scene = {.document = session.scene(), .assets = session.assets(),
                .modeling = session.modeling(), .selection = session.selection()},
      .preferences = {.ui = settings, .viewport = viewport},
      .localization = localization,
  };
  OutlinerPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.resize(360, 320);
  panel.refresh(context);
  panel.show();
  QCoreApplication::processEvents();
  auto *tree = panel.findChild<QTreeView *>(QStringLiteral("outliner.tree"));
  ASSERT_NE(tree, nullptr);
  auto *model = tree->model();
  const QModelIndex source_index = model->index(0, 0);
  const QModelIndex target_index = model->index(1, 0);
  ASSERT_EQ(source_index.data().toString(), QStringLiteral("Source"));
  ASSERT_EQ(target_index.data().toString(), QStringLiteral("Target"));
  std::unique_ptr<QMimeData> mime(model->mimeData({source_index}));
  ASSERT_NE(mime, nullptr);
  hover(*tree->viewport());
  EXPECT_TRUE(model->canDropMimeData(mime.get(), Qt::MoveAction, -1, 0, target_index));
  EXPECT_TRUE(qt_contextual_input_hints(*application_).empty());
  EXPECT_TRUE(qt_contextual_input_hints_state(*application_).overrides_viewport);
  QDragEnterEvent enter(tree->visualRect(target_index).center(), Qt::MoveAction,
                        mime.get(), Qt::LeftButton, Qt::ControlModifier);
  QCoreApplication::sendEvent(tree->viewport(), &enter);
  EXPECT_TRUE(model->canDropMimeData(mime.get(), Qt::MoveAction, -1, 0, target_index));
  auto hints = qt_contextual_input_hints(*application_);
  ASSERT_EQ(hints.size(), 1u);
  EXPECT_EQ(hints.front().label_key, "operation.hints.outliner.parent_to");
  EXPECT_EQ(hints.front().detail, "Target");
  EXPECT_EQ(hints.front().gesture, QuaderOperationInputGesture::PrimaryDrag);
  EXPECT_TRUE(model->canDropMimeData(mime.get(), Qt::MoveAction, 1, 0, {}));
  EXPECT_EQ(qt_contextual_input_hints(*application_).front().label_key,
            "operation.hints.outliner.reorder");
  EXPECT_TRUE(model->canDropMimeData(mime.get(), Qt::MoveAction, -1, 0, {}));
  EXPECT_EQ(qt_contextual_input_hints(*application_).front().label_key,
            "operation.hints.outliner.move_root");
  EXPECT_FALSE(model->canDropMimeData(mime.get(), Qt::MoveAction, -1, 0, source_index));
  EXPECT_TRUE(qt_contextual_input_hints(*application_).empty());
  EXPECT_EQ(session.scene().parent(source.value().id), kInvalidEntityId);
  panel.abandon_runtime();
  EXPECT_TRUE(qt_contextual_input_hints(*application_).empty());
}

} // namespace
} // namespace quader
