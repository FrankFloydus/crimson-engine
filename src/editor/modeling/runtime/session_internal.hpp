////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <editor/modeling/runtime/session.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace quader_editor_core {

inline constexpr float kTransformPointEpsilon = 0.00000001F;

CommandResult ok_result(bool changed);
CommandResult error_result(std::string message);
std::uint32_t saturated_history_size(std::size_t size);

int object_index_by_id(const std::vector<PolygonObject>& objects, int object_id);
Bounds3 document_bounds(const PolygonObject& object);

} // namespace quader_editor_core
