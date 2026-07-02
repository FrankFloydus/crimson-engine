////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/modal_tool_overlay_renderer.hpp"

#include "core/localization_service.hpp"
#include "ui/viewport_screen_geometry.hpp"

#include <array>
#include <cstdio>
#include <string>

namespace quader {
namespace {

void draw_world_line(ImDrawList *draw_list, QVec3 a, QVec3 b,
                     const ViewportCameraController &camera, ImVec2 item_min,
                     ImVec2 item_size, ImU32 color, float thickness) {
  const std::optional<ImVec2> screen_a =
      project_world_to_viewport(a, camera, item_min, item_size);
  const std::optional<ImVec2> screen_b =
      project_world_to_viewport(b, camera, item_min, item_size);
  if (screen_a && screen_b) {
    draw_list->AddLine(*screen_a, *screen_b, color, thickness);
  }
}

} // namespace

void ModalToolOverlayRenderer::draw_box_preview(
    ImDrawList *draw_list, const ModelingBoxPreview &preview,
    const ViewportCameraController &camera, ImVec2 item_min,
    ImVec2 item_size) const {
  if (!preview.valid) {
    return;
  }

  const ImU32 color = IM_COL32(255, 235, 41, 209);
  if (preview.corner_count >= 8U) {
    constexpr std::array<std::array<int, 2>, 12> kEdges = {{
        {{0, 1}},
        {{1, 2}},
        {{2, 3}},
        {{3, 0}},
        {{4, 5}},
        {{5, 6}},
        {{6, 7}},
        {{7, 4}},
        {{0, 4}},
        {{1, 5}},
        {{2, 6}},
        {{3, 7}},
    }};

    for (const auto &edge : kEdges) {
      draw_world_line(draw_list, preview.corners[edge[0]],
                      preview.corners[edge[1]], camera, item_min, item_size,
                      color, 2.0f);
    }
    return;
  }

  if (preview.footprint_count >= 4U) {
    for (std::size_t index = 0; index < preview.footprint_count; ++index) {
      const QVec3 a = preview.footprint[index];
      const QVec3 b = preview.footprint[(index + 1U) % preview.footprint_count];
      draw_world_line(draw_list, a, b, camera, item_min, item_size, color,
                      2.0f);
    }
  }
}

void ModalToolOverlayRenderer::draw_fly_navigation_overlay(
    ImDrawList *draw_list, ImVec2 item_min, ImVec2 item_size,
    bool draw_crosshair, bool draw_speed, bool draw_reset_hint, float speed,
    const LocalizationService &localization) const {
  const ImVec2 center{item_min.x + item_size.x * 0.5f,
                      item_min.y + item_size.y * 0.5f};
  if (draw_crosshair) {
    const float gap = 4.0f;
    const float length = 9.0f;
    const ImU32 shadow = ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.65f));
    const ImU32 color = ImGui::GetColorU32(ImVec4(0.94f, 0.96f, 1.0f, 0.88f));
    draw_list->AddLine({center.x - length - 1.0f, center.y},
                       {center.x - gap - 1.0f, center.y}, shadow, 2.0f);
    draw_list->AddLine({center.x + gap + 1.0f, center.y},
                       {center.x + length + 1.0f, center.y}, shadow, 2.0f);
    draw_list->AddLine({center.x, center.y - length - 1.0f},
                       {center.x, center.y - gap - 1.0f}, shadow, 2.0f);
    draw_list->AddLine({center.x, center.y + gap + 1.0f},
                       {center.x, center.y + length + 1.0f}, shadow, 2.0f);
    draw_list->AddLine({center.x - length, center.y},
                       {center.x - gap, center.y}, color, 1.25f);
    draw_list->AddLine({center.x + gap, center.y},
                       {center.x + length, center.y}, color, 1.25f);
    draw_list->AddLine({center.x, center.y - length},
                       {center.x, center.y - gap}, color, 1.25f);
    draw_list->AddLine({center.x, center.y + gap},
                       {center.x, center.y + length}, color, 1.25f);
  }

  if (!draw_speed) {
    return;
  }

  char speed_value[32]{};
  std::snprintf(speed_value, sizeof(speed_value), "%.1f", speed);
  std::string label;
  if (draw_reset_hint) {
    label = localization.format("viewport.fly_navigation.speed_with_reset",
                                "Speed {speed} (Middle Mouse to reset)",
                                {{"speed", speed_value}});
  } else {
    label = localization.format("viewport.fly_navigation.speed",
                                "Speed {speed}", {{"speed", speed_value}});
  }
  const ImVec2 text_size = ImGui::CalcTextSize(label.c_str());
  const ImVec2 padding{10.0f, 5.0f};
  const ImVec2 min{
      item_min.x + item_size.x * 0.5f - text_size.x * 0.5f - padding.x,
      item_min.y + item_size.y - 36.0f,
  };
  const ImVec2 max{min.x + text_size.x + padding.x * 2.0f,
                   min.y + text_size.y + padding.y * 2.0f};
  draw_list->AddRectFilled(
      min, max, ImGui::GetColorU32(ImVec4(0.05f, 0.06f, 0.07f, 0.78f)), 4.0f);
  draw_list->AddText({min.x + padding.x, min.y + padding.y},
                     ImGui::GetColorU32(ImVec4(0.92f, 0.95f, 1.0f, 1.0f)),
                     label.c_str());
}

} // namespace quader
