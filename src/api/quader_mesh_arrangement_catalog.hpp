////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_types.hpp"
#include "api/quader_command_ids.hpp"

#include <array>
#include <span>
#include <string_view>

namespace quader {

/// Classifies one native mesh arrangement command for shared routing.
enum class QuaderMeshArrangementKind {
  Alignment,
  Distribution,
};

/// Describes one typed arrangement action shared by commands and menus.
struct QuaderMeshArrangementDescriptor {
  QuaderCommandId command_id;
  QuaderMeshArrangementKind kind = QuaderMeshArrangementKind::Alignment;
  QuaderTransformAxis axis = QuaderTransformAxis::X;
  QuaderMeshAlignment alignment = QuaderMeshAlignment::Center;
  QuaderMeshDistribution distribution = QuaderMeshDistribution::Centers;
  std::string_view localization_key;
  std::string_view fallback_label;
};

inline constexpr std::array<QuaderMeshArrangementDescriptor, 15>
    kQuaderMeshArrangementDescriptors{{
        {quader_command::kModelingAlignXMin,
         QuaderMeshArrangementKind::Alignment, QuaderTransformAxis::X,
         QuaderMeshAlignment::Minimum, QuaderMeshDistribution::Centers,
         "ui.menu.align.min", "Min"},
        {quader_command::kModelingAlignXCenter,
         QuaderMeshArrangementKind::Alignment, QuaderTransformAxis::X,
         QuaderMeshAlignment::Center, QuaderMeshDistribution::Centers,
         "ui.menu.align.center", "Center"},
        {quader_command::kModelingAlignXMax,
         QuaderMeshArrangementKind::Alignment, QuaderTransformAxis::X,
         QuaderMeshAlignment::Maximum, QuaderMeshDistribution::Centers,
         "ui.menu.align.max", "Max"},
        {quader_command::kModelingAlignYMin,
         QuaderMeshArrangementKind::Alignment, QuaderTransformAxis::Y,
         QuaderMeshAlignment::Minimum, QuaderMeshDistribution::Centers,
         "ui.menu.align.min", "Min"},
        {quader_command::kModelingAlignYCenter,
         QuaderMeshArrangementKind::Alignment, QuaderTransformAxis::Y,
         QuaderMeshAlignment::Center, QuaderMeshDistribution::Centers,
         "ui.menu.align.center", "Center"},
        {quader_command::kModelingAlignYMax,
         QuaderMeshArrangementKind::Alignment, QuaderTransformAxis::Y,
         QuaderMeshAlignment::Maximum, QuaderMeshDistribution::Centers,
         "ui.menu.align.max", "Max"},
        {quader_command::kModelingAlignZMin,
         QuaderMeshArrangementKind::Alignment, QuaderTransformAxis::Z,
         QuaderMeshAlignment::Minimum, QuaderMeshDistribution::Centers,
         "ui.menu.align.min", "Min"},
        {quader_command::kModelingAlignZCenter,
         QuaderMeshArrangementKind::Alignment, QuaderTransformAxis::Z,
         QuaderMeshAlignment::Center, QuaderMeshDistribution::Centers,
         "ui.menu.align.center", "Center"},
        {quader_command::kModelingAlignZMax,
         QuaderMeshArrangementKind::Alignment, QuaderTransformAxis::Z,
         QuaderMeshAlignment::Maximum, QuaderMeshDistribution::Centers,
         "ui.menu.align.max", "Max"},
        {quader_command::kModelingDistributeCentersX,
         QuaderMeshArrangementKind::Distribution, QuaderTransformAxis::X,
         QuaderMeshAlignment::Center, QuaderMeshDistribution::Centers,
         "ui.menu.align.axis_x", "X"},
        {quader_command::kModelingDistributeCentersY,
         QuaderMeshArrangementKind::Distribution, QuaderTransformAxis::Y,
         QuaderMeshAlignment::Center, QuaderMeshDistribution::Centers,
         "ui.menu.align.axis_y", "Y"},
        {quader_command::kModelingDistributeCentersZ,
         QuaderMeshArrangementKind::Distribution, QuaderTransformAxis::Z,
         QuaderMeshAlignment::Center, QuaderMeshDistribution::Centers,
         "ui.menu.align.axis_z", "Z"},
        {quader_command::kModelingDistributeGapsX,
         QuaderMeshArrangementKind::Distribution, QuaderTransformAxis::X,
         QuaderMeshAlignment::Center, QuaderMeshDistribution::Gaps,
         "ui.menu.align.axis_x", "X"},
        {quader_command::kModelingDistributeGapsY,
         QuaderMeshArrangementKind::Distribution, QuaderTransformAxis::Y,
         QuaderMeshAlignment::Center, QuaderMeshDistribution::Gaps,
         "ui.menu.align.axis_y", "Y"},
        {quader_command::kModelingDistributeGapsZ,
         QuaderMeshArrangementKind::Distribution, QuaderTransformAxis::Z,
         QuaderMeshAlignment::Center, QuaderMeshDistribution::Gaps,
         "ui.menu.align.axis_z", "Z"},
    }};

[[nodiscard]] inline constexpr std::span<const QuaderMeshArrangementDescriptor>
quader_mesh_arrangement_catalog() {
  return kQuaderMeshArrangementDescriptors;
}

} // namespace quader
