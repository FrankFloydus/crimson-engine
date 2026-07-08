////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <string>

namespace quader::modeling {

/**
 * Describes one SDK command without exposing native app command IDs.
 */
struct CommandDescriptor {
  std::string id;
  std::string label;
};

} // namespace quader::modeling
