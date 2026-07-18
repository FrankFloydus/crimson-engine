////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <string_view>
#include <type_traits>

namespace qdr_test {

template <typename Left, typename Right, typename Epsilon = double>
inline bool nearly_equal(Left left, Right right, Epsilon epsilon = static_cast<Epsilon>(0.0001))
{
	using Common = std::common_type_t<Left, Right, Epsilon>;
	return std::abs(static_cast<Common>(left) - static_cast<Common>(right)) <= static_cast<Common>(epsilon);
}

} // namespace qdr_test

inline ::testing::AssertionResult require_test(bool condition, std::string_view message)
{
	if (condition) {
		return ::testing::AssertionSuccess();
	}
	return ::testing::AssertionFailure() << message;
}
