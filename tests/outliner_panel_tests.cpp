////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "ui/outliner_panel.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QLayout>
#include <QTreeView>

using quader::OutlinerPropertyState;

TEST(OutlinerPanelTests, TreeFillsThePanelContentsRect) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-outliner-layout-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  quader::OutlinerPanel panel;
  panel.resize(320, 480);
  ASSERT_NE(panel.layout(), nullptr);
  ASSERT_TRUE(panel.layout()->activate());

  auto *tree = panel.findChild<QTreeView *>(QStringLiteral("outliner.tree"));
  ASSERT_NE(tree, nullptr);
  EXPECT_EQ(tree->geometry(), panel.contentsRect());
}

TEST(OutlinerPanelTests, PropertyStatesDistinguishLocalAndInheritedValues) {
  EXPECT_EQ(quader::outliner_visibility_property_state(false, false),
            OutlinerPropertyState::Off);
  EXPECT_EQ(quader::outliner_visibility_property_state(true, true),
            OutlinerPropertyState::On);
  EXPECT_EQ(quader::outliner_visibility_property_state(true, false),
            OutlinerPropertyState::Inherited);

  EXPECT_EQ(quader::outliner_lock_property_state(false, false),
            OutlinerPropertyState::Off);
  EXPECT_EQ(quader::outliner_lock_property_state(true, true),
            OutlinerPropertyState::On);
  EXPECT_EQ(quader::outliner_lock_property_state(false, true),
            OutlinerPropertyState::Inherited);
}

TEST(OutlinerPanelTests,
     RepeatedCollapseExpandWithStableSelectionDoesNotResynchronize) {
  constexpr std::uint64_t kSelectionRevision = 41;
  for (int interaction = 0; interaction < 1000; ++interaction) {
    EXPECT_FALSE(quader::outliner_selection_sync_required(
        false, true, kSelectionRevision, kSelectionRevision));
  }

  EXPECT_TRUE(quader::outliner_selection_sync_required(
      true, true, kSelectionRevision, kSelectionRevision));
  EXPECT_TRUE(quader::outliner_selection_sync_required(
      false, false, 0, kSelectionRevision));
  EXPECT_TRUE(quader::outliner_selection_sync_required(
      false, true, kSelectionRevision, kSelectionRevision + 1));
}
