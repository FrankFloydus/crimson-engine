////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "core/qstr.hpp"

#include <gtest/gtest.h>

using namespace quader;

TEST(QStrTests, TrimsAsciiWhitespaceOnlyAtEdges) {
  EXPECT_EQ(QStr::trim_ascii_whitespace(" \t\r\nvalue \t"), "value");
  EXPECT_EQ(QStr::trim_ascii_whitespace("   "), "");
  EXPECT_EQ(QStr::trim_ascii_whitespace("a b"), "a b");
}

TEST(QStrTests, LowercasesAsciiSearchText) {
  EXPECT_EQ(QStr::to_ascii_lower("Quader VIEWPORT 42"), "quader viewport 42");
  EXPECT_TRUE(QStr::equals_ascii_ignore_case("TRUE", "true"));
  EXPECT_FALSE(QStr::equals_ascii_ignore_case("true", "truth"));
}

TEST(QStrTests, ParsesBooleanTokens) {
  EXPECT_EQ(QStr::parse_bool_token(" YES "), std::optional<bool>(true));
  EXPECT_EQ(QStr::parse_bool_token("off"), std::optional<bool>(false));
  EXPECT_EQ(QStr::parse_bool_token("maybe"), std::nullopt);
}

TEST(QStrTests, ParsesFiniteFloatTokens) {
  EXPECT_EQ(QStr::parse_finite_float_token(" 1.5 "),
            std::optional<float>(1.5f));
  EXPECT_EQ(QStr::parse_finite_float_token("1.0x"), std::nullopt);
  EXPECT_EQ(QStr::parse_finite_float_token("inf"), std::nullopt);
}

TEST(QStrTests, SplitsOnceWithoutDroppingRemainder) {
  const std::optional<QStrSplitOnce> split =
      QStr::split_once("key=value=tail", '=');

  ASSERT_TRUE(split.has_value());
  EXPECT_EQ(split->before, "key");
  EXPECT_EQ(split->after, "value=tail");
  EXPECT_EQ(QStr::split_once("key", '='), std::nullopt);
}

TEST(QStrTests, SplitsLinesAsViews) {
  const std::vector<std::string_view> lines = QStr::split_lines("a\nb\n");

  ASSERT_EQ(lines.size(), 3u);
  EXPECT_EQ(lines[0], "a");
  EXPECT_EQ(lines[1], "b");
  EXPECT_EQ(lines[2], "");
}
