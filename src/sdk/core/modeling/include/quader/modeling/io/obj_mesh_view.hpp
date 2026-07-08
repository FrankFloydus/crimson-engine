////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <quader/modeling/payloads.hpp>
#include <quader/modeling/result.hpp>

#include <string>
#include <string_view>

namespace quader::modeling {

[[nodiscard]] Result<std::string> serialize_obj_mesh(const MeshPayload &mesh,
                                                     std::string_view name);

} // namespace quader::modeling
