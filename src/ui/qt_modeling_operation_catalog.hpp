////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_types.hpp"
#include "api/quader_command_ids.hpp"

#include <QString>

#include <span>
#include <string_view>

namespace quader {

class LocalizationService;

/// Retains marking-menu commands and their memorized compass directions.
struct QtModelingOperationSpec {
  QuaderSelectionMode mode;
  QuaderCommandId command_id;
  std::string_view fallback;
  /// Fixed compass slot clockwise from north; RMB policy owns the lower menu.
  int marking_slot;
};

[[nodiscard]] std::span<const QtModelingOperationSpec>
qt_modeling_operation_specs();

[[nodiscard]] QString qt_modeling_operation_label(
    const LocalizationService &localization, QuaderCommandId command_id,
    std::string_view fallback);

} // namespace quader
