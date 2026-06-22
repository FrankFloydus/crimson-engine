////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <string>

namespace quader_editor_core {

/**
 * Represents a Tool Status value used by the modal modeling interaction layer.
 */
struct ToolStatus {
	bool ok = true;
	bool changed = false;
	std::string message;
};

} // namespace quader_editor_core
