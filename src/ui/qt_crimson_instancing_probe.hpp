////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <filesystem>

class QApplication;

namespace quader {

/** Explicit opt-in GPU acceptance; creates no authored document or settings. */
[[nodiscard]] int run_qt_crimson_instancing_probe(
    QApplication &application, const std::filesystem::path &output_directory,
    const std::filesystem::path &resources);

} // namespace quader
