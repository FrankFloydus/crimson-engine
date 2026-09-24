////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "io/qdr/qdr_binary_container.hpp"
#include "io/qdr/qdr_project_write_snapshot.hpp"

namespace quader::migration {

/**
 * Converts the retired bgfx metadata-10/geometry-2 rootless mesh dialect.
 * This deliberately bounded, offline converter rejects unsupported content
 * instead of adding that incompatible dialect to ordinary scene opening.
 * Throws std::runtime_error on malformed or unrepresentable input.
 */
[[nodiscard]] QdrProjectWriteSnapshot decode_retired_schema10(
    const quader_io::QdrBinaryContainerReadResult &container);

} // namespace quader::migration
