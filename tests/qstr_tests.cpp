////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "core/q_str.hpp"

#include <gtest/gtest.h>

using namespace quader;

TEST(QStrTests, TrimsAsciiWhitespaceOnlyAtEdges) {
    EXPECT_EQ(QStr::trimAsciiWhitespace(" \t\r\nvalue \t"), "value");
    EXPECT_EQ(QStr::trimAsciiWhitespace("   "), "");
    EXPECT_EQ(QStr::trimAsciiWhitespace("a b"), "a b");
}

TEST(QStrTests, LowercasesAsciiSearchText) {
    EXPECT_EQ(QStr::toAsciiLower("Quader VIEWPORT 42"), "quader viewport 42");
    EXPECT_TRUE(QStr::equalsAsciiIgnoreCase("TRUE", "true"));
    EXPECT_FALSE(QStr::equalsAsciiIgnoreCase("true", "truth"));
}

TEST(QStrTests, ParsesBooleanTokens) {
    EXPECT_EQ(QStr::parseBoolToken(" YES "), std::optional<bool>(true));
    EXPECT_EQ(QStr::parseBoolToken("off"), std::optional<bool>(false));
    EXPECT_EQ(QStr::parseBoolToken("maybe"), std::nullopt);
}

TEST(QStrTests, ParsesFiniteFloatTokens) {
    EXPECT_EQ(QStr::parseFiniteFloatToken(" 1.5 "), std::optional<float>(1.5f));
    EXPECT_EQ(QStr::parseFiniteFloatToken("1.0x"), std::nullopt);
    EXPECT_EQ(QStr::parseFiniteFloatToken("inf"), std::nullopt);
}

TEST(QStrTests, SplitsOnceWithoutDroppingRemainder) {
    const std::optional<QStrSplitOnce> split = QStr::splitOnce("key=value=tail", '=');

    ASSERT_TRUE(split.has_value());
    EXPECT_EQ(split->before, "key");
    EXPECT_EQ(split->after, "value=tail");
    EXPECT_EQ(QStr::splitOnce("key", '='), std::nullopt);
}

TEST(QStrTests, SplitsLinesAsViews) {
    const std::vector<std::string_view> lines = QStr::splitLines("a\nb\n");

    ASSERT_EQ(lines.size(), 3u);
    EXPECT_EQ(lines[0], "a");
    EXPECT_EQ(lines[1], "b");
    EXPECT_EQ(lines[2], "");
}
