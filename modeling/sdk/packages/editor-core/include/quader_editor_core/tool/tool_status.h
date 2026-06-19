#pragma once

#include <string>

namespace quader_editor_core {

struct ToolStatus {
	bool ok = true;
	bool changed = false;
	std::string message;
};

} // namespace quader_editor_core
