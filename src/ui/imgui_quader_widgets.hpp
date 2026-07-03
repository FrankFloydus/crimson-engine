////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <imgui.h>

namespace quader {

/**
 * Configures the reusable two-column property table used by ImGui inspector
 * surfaces.
 */
struct QuaderPropertyTableOptions {
  ImGuiTreeNodeFlags header_flags = ImGuiTreeNodeFlags_None;
  float label_column_width = 132.0f;
  float minimum_label_column_width = 76.0f;
  float minimum_value_column_width = 92.0f;
};

bool quader_begin_combo(const char *label, const char *preview_value,
                        ImGuiComboFlags flags = ImGuiComboFlags_None,
                        bool close_on_repeated_click = true);
bool quader_collapsing_header(
    const char *label, ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None);
bool quader_begin_property_table(
    const char *stable_id,
    QuaderPropertyTableOptions options = QuaderPropertyTableOptions{});
bool quader_property_section(
    const char *stable_id, const char *label,
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None);
void quader_property_row(const char *stable_id, const char *label);
void quader_end_property_row();
void quader_end_property_table();
bool quader_slider_float(
    const char *label, float *value, float minimum, float maximum,
    const char *format = "%.3f",
    ImGuiSliderFlags flags = ImGuiSliderFlags_AlwaysClamp);
bool quader_slider_int(const char *label, int *value, int minimum, int maximum,
                       const char *format = "%d",
                       ImGuiSliderFlags flags = ImGuiSliderFlags_AlwaysClamp);

} // namespace quader
