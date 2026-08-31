////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/retained_ui_presenter.hpp"

#include <gtest/gtest.h>

#include <cstdint>

using namespace quader;

namespace {

struct TestPresentationStamp final {
  std::uint64_t owner_revision = 0u;
  std::uint64_t localization_revision = 0u;

  friend bool operator==(const TestPresentationStamp &,
                         const TestPresentationStamp &) = default;
};

TEST(RetainedUiPresenterTests, OwnsTypedUnchangedStateGate) {
  RetainedUiPresenter<TestPresentationStamp> presenter;
  const TestPresentationStamp first{
      .owner_revision = 2u,
      .localization_revision = 3u,
  };
  EXPECT_FALSE(presenter.valid());
  EXPECT_TRUE(presenter.needs_refresh(first));

  presenter.mark_presented(first);
  ASSERT_TRUE(presenter.valid());
  ASSERT_NE(presenter.presented_stamp(), nullptr);
  EXPECT_EQ(*presenter.presented_stamp(), first);
  EXPECT_FALSE(presenter.needs_refresh(first));
  EXPECT_TRUE(presenter.needs_refresh(TestPresentationStamp{
      .owner_revision = 2u,
      .localization_revision = 4u,
  }));

  presenter.invalidate();
  EXPECT_FALSE(presenter.valid());
  EXPECT_TRUE(presenter.needs_refresh(first));
}

} // namespace

