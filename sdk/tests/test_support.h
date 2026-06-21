#pragma once

#ifdef QUADER_SDK_TEST_SUPPORT_CATCH2

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
		std::cerr << "SDK focused test failure: " << message << "\n";
		std::exit(1);
	}
}

#endif
