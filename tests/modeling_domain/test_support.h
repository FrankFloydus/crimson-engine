////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#ifdef QUADER_MODELING_TEST_SUPPORT_CATCH2

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

inline void require_test(bool condition, std::string_view message)
{
	INFO(std::string(message));
	REQUIRE(condition);
}

#else

#include <cstdlib>
#include <iostream>
#include <string_view>

inline void require_test(bool condition, std::string_view message)
{
	if (!condition) {
		std::cerr << "Modeling-domain test failure: " << message << "\n";
		std::exit(1);
	}
}

#endif
