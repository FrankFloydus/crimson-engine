////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <string_view>

namespace quader {

inline constexpr std::string_view kFilamentMaterialDirectory = "materials";
inline constexpr std::string_view kQuaderMeshMaterialFile = "quader_mesh.filamat";
inline constexpr std::string_view kQuaderMeshLitMaterialFile = "quader_mesh_lit.filamat";
inline constexpr std::string_view kVertexColorOverlayMaterialFile = "vertex_color_overlay.filamat";
inline constexpr std::string_view kInstanceSelectionFillMaterialFile = "quader_instance_selection_fill.filamat";
inline constexpr std::string_view kOverlayLineMaterialFile = "quader_overlay_line.filamat";
inline constexpr std::string_view kSelectedObjectLineMaterialFile = "quader_selected_object_line.filamat";
inline constexpr std::string_view kSelectedMeshOutlineMaskMaterialFile = "quader_selection_mask.filamat";
inline constexpr std::string_view kSelectedMeshOutlineCompositeMaterialFile = "quader_selection_outline.filamat";
inline constexpr std::string_view kEditWireLineMaterialFile = "quader_edit_wire_line.filamat";
inline constexpr std::string_view kOverlayPointMaterialFile = "quader_overlay_point.filamat";
inline constexpr std::string_view kOverlayIconMaterialFile = "quader_overlay_icon.filamat";
inline constexpr std::string_view kGroundGridMaterialFile = "ground_grid.filamat";
inline constexpr std::string_view kOverlayStencilResetMaterialFile = "overlay_stencil_reset.filamat";
inline constexpr std::string_view kComponentSelectionMaterialFile = "quader_component_selection.filamat";

} // namespace quader
