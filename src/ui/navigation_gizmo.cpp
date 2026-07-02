////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/navigation_gizmo.hpp"

#include "viewport/viewport_camera_adapter.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

#include <imgui_internal.h>

#include <editor/interactions/viewport_navigation.hpp>

namespace quader {
namespace {

std::uint8_t alpha_byte(float alpha) {
  return static_cast<std::uint8_t>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f);
}

ImU32 view_axis_im_color(quader_editor_core::ViewAxis axis,
                         float alpha = 1.0f) {
  const std::uint8_t a = alpha_byte(alpha);
  switch (axis) {
  case quader_editor_core::ViewAxis::X:
    return IM_COL32(235, 76, 82, a);
  case quader_editor_core::ViewAxis::Y:
    return IM_COL32(90, 205, 118, a);
  case quader_editor_core::ViewAxis::Z:
    return IM_COL32(78, 142, 255, a);
  }
  return IM_COL32(255, 255, 255, a);
}

ViewportViewAxis to_viewport_view_axis(quader_editor_core::ViewAxis axis) {
  switch (axis) {
  case quader_editor_core::ViewAxis::X:
    return ViewportViewAxis::X;
  case quader_editor_core::ViewAxis::Y:
    return ViewportViewAxis::Y;
  case quader_editor_core::ViewAxis::Z:
    return ViewportViewAxis::Z;
  }
  return ViewportViewAxis::Z;
}

quader::QVec3 to_runtime(QVec3 value) { return {value.x, value.y, value.z}; }

quader_editor_core::ViewportCameraState
make_runtime_camera(const ViewportCameraController &camera, ImVec2 item_size) {
  const ViewportCameraBasis camera_basis = ViewportCameraAdapter::basis(camera);

  quader_editor_core::ViewportCameraState runtime_camera;
  runtime_camera.projection =
      camera.state().projection == ViewportProjection::Orthographic
          ? quader_editor_core::CameraProjection::Orthographic
          : quader_editor_core::CameraProjection::Perspective;
  runtime_camera.transform.origin = to_runtime(camera_basis.eye);
  runtime_camera.transform.x_axis = to_runtime(camera_basis.right);
  runtime_camera.transform.y_axis = to_runtime(camera_basis.up);
  runtime_camera.transform.z_axis = to_runtime(camera_basis.forward * -1.0f);
  runtime_camera.viewport_size = {std::max(item_size.x, 1.0f),
                                  std::max(item_size.y, 1.0f)};
  runtime_camera.orthographic_size =
      std::max(camera.state().orthographic_size, 0.0001f);
  runtime_camera.fov_degrees = camera.state().fov_degrees;
  return runtime_camera;
}

} // namespace

void NavigationGizmo::set_visible(bool visible) {
  visible_ = visible;
  if (!visible_) {
    dragging_ = false;
    drag_pane_active_ = false;
    drag_started_ = false;
    pressed_face_ = false;
  }
}

bool NavigationGizmo::draw(ViewportCameraController &camera,
                           ViewportPaneId pane_id, ImVec2 item_min,
                           ImVec2 item_size, QVec2 captured_mouse_delta,
                           bool *viewport_cursor_capture_requested,
                           QVec2 *viewport_cursor_capture_point,
                           bool interactive) {
  if (!visible_) {
    return false;
  }

  quader_editor_core::ViewCubePrimitiveInput input;
  input.camera_transform = make_runtime_camera(camera, item_size).transform;
  input.viewport_size = {std::max(item_size.x, 1.0f),
                         std::max(item_size.y, 1.0f)};
  input.widget_size = 70.0;
  input.margin = {46.0f, 46.0f};

  const quader_editor_core::ViewCubePrimitives primitives =
      quader_editor_core::build_view_cube_primitives(input);
  if (!primitives.ok) {
    return false;
  }

  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  const ImGuiIO &io = ImGui::GetIO();
  const ImVec2 mouse = io.MousePos;
  const bool active_drag =
      dragging_ && drag_pane_active_ && drag_pane_id_ == pane_id;
  const bool mouse_inside_viewport = mouse.x >= item_min.x &&
                                     mouse.y >= item_min.y &&
                                     mouse.x <= item_min.x + item_size.x &&
                                     mouse.y <= item_min.y + item_size.y;

  quader_editor_core::ViewCubeHit hover_hit;
  if (interactive && mouse_inside_viewport) {
    quader_editor_core::ViewCubePickInput pick_input;
    pick_input.primitives = input;
    pick_input.screen_position = {mouse.x - item_min.x, mouse.y - item_min.y};
    hover_hit = quader_editor_core::pick_view_cube(pick_input);
  }

  auto to_view_cube_screen_point = [&](quader::QVec2 point) {
    return ImVec2{
        std::floor(item_min.x + point.x) + 0.5f,
        std::floor(item_min.y + point.y) + 0.5f,
    };
  };

  const ImDrawListFlags previous_draw_flags = draw_list->Flags;
  draw_list->Flags &= ~ImDrawListFlags_AntiAliasedFill;
  for (const quader_editor_core::ViewCubeFacePrimitive &face :
       primitives.faces) {
    std::vector<ImVec2> polygon;
    polygon.reserve(face.polygon.size());
    for (quader::QVec2 point : face.polygon) {
      polygon.push_back(to_view_cube_screen_point(point));
    }
    if (polygon.size() < 3u) {
      continue;
    }

    const float depth = std::clamp(static_cast<float>(face.depth), 0.0f, 1.0f);
    const std::uint8_t shade =
        static_cast<std::uint8_t>(178.0f + depth * 46.0f);
    const bool hover_face =
        hover_hit.hit &&
        hover_hit.kind == quader_editor_core::ViewCubeHitKind::Face &&
        hover_hit.axis == face.axis && hover_hit.sign == face.sign;
    const bool pressed_face =
        active_drag && !drag_started_ && pressed_face_ &&
        pressed_axis_ == to_viewport_view_axis(face.axis) &&
        pressed_sign_ == face.sign;
    const bool highlighted = hover_face || pressed_face;
    const ImU32 fill_color =
        highlighted
            ? IM_COL32(255, 221, 111, 255)
            : IM_COL32(shade, shade,
                       static_cast<std::uint8_t>(std::min<int>(shade + 6, 255)),
                       248);
    draw_list->AddConvexPolyFilled(
        polygon.data(), static_cast<int>(polygon.size()), fill_color);
  }
  draw_list->Flags = previous_draw_flags;

  constexpr ImU32 kNeutralEdgeColor = IM_COL32(248, 250, 252, 205);
  for (const quader_editor_core::ViewCubeEdgePrimitive &edge :
       primitives.edges) {
    draw_list->AddLine(to_view_cube_screen_point(edge.screen_start),
                       to_view_cube_screen_point(edge.screen_end),
                       kNeutralEdgeColor, 1.5f);
  }

  for (const quader_editor_core::ViewCubeEdgePrimitive &edge :
       primitives.axis_edges) {
    draw_list->AddLine(to_view_cube_screen_point(edge.screen_start),
                       to_view_cube_screen_point(edge.screen_end),
                       view_axis_im_color(edge.axis, 1.0f), 2.8f);
  }

  for (const quader_editor_core::ViewCubeAxisMarkerPrimitive &marker :
       primitives.axis_markers) {
    const ImVec2 marker_center{
        std::floor(item_min.x + marker.screen_position.x) + 0.5f,
        std::floor(item_min.y + marker.screen_position.y) + 0.5f};
    constexpr float kRadius = 8.0f;
    const ImU32 marker_color = view_axis_im_color(marker.axis, 1.0f);
    draw_list->AddCircleFilled(marker_center, kRadius, marker_color, 18);
    draw_list->AddCircle(marker_center, kRadius, IM_COL32(16, 20, 24, 112), 18,
                         1.2f);

    const std::string_view label =
        quader_editor_core::view_axis_label(marker.axis, marker.sign);
    constexpr float kLabelSize = 11.5f;
    ImFont *label_font = ImGui::GetFont();
    const ImVec2 text_size = label_font->CalcTextSizeA(
        kLabelSize, std::numeric_limits<float>::max(), 0.0f, label.data(),
        label.data() + label.size());
    draw_list->AddText(label_font, kLabelSize,
                       {marker_center.x - text_size.x * 0.5f,
                        marker_center.y - text_size.y * 0.5f - 0.5f},
                       IM_COL32(17, 22, 28, 255), label.data(),
                       label.data() + label.size());
  }

  auto request_capture = [&]() {
    if (viewport_cursor_capture_requested != nullptr) {
      *viewport_cursor_capture_requested = true;
    }
    if (viewport_cursor_capture_point != nullptr) {
      const ImGuiViewport *main_viewport = ImGui::GetMainViewport();
      *viewport_cursor_capture_point = {
          cursor_capture_screen_.x - main_viewport->Pos.x,
          cursor_capture_screen_.y - main_viewport->Pos.y,
      };
    }
  };

  if (active_drag) {
    request_capture();
    ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      const QVec2 fallback_delta{io.MouseDelta.x, io.MouseDelta.y};
      const float captured_delta_length_squared =
          captured_mouse_delta.x * captured_mouse_delta.x +
          captured_mouse_delta.y * captured_mouse_delta.y;
      const QVec2 delta = captured_delta_length_squared > 0.000001f
                              ? captured_mouse_delta
                              : fallback_delta;
      accumulated_drag_ = accumulated_drag_ + delta;
      const float accumulated_length_squared =
          accumulated_drag_.x * accumulated_drag_.x +
          accumulated_drag_.y * accumulated_drag_.y;
      if (accumulated_length_squared > 9.0f) {
        drag_started_ = true;
      }
      if (drag_started_ &&
          (delta.x * delta.x + delta.y * delta.y) > 0.000001f) {
        camera.update_drag_delta(delta);
      }
      return true;
    }

    if (!drag_started_ && pressed_face_) {
      camera.view_axis(pressed_axis_, pressed_sign_,
                       ViewportAxisLock::Unlocked);
    }
    camera.end_drag();
    dragging_ = false;
    drag_pane_active_ = false;
    drag_started_ = false;
    pressed_face_ = false;
    return true;
  }

  if (!interactive || !mouse_inside_viewport ||
      !ImGui::IsMouseClicked(ImGuiMouseButton_Left) || !hover_hit.hit) {
    return false;
  }

  dragging_ = true;
  drag_pane_active_ = true;
  drag_pane_id_ = pane_id;
  drag_started_ = false;
  pressed_face_ = hover_hit.kind == quader_editor_core::ViewCubeHitKind::Face;
  pressed_axis_ = to_viewport_view_axis(hover_hit.axis);
  pressed_sign_ = hover_hit.sign;
  accumulated_drag_ = {};
  cursor_capture_screen_ = {mouse.x, mouse.y};
  camera.unlock_orthographic_view();
  camera.begin_drag({mouse.x, mouse.y}, ViewportDragMode::Orbit);
  request_capture();
  ImGui::SetMouseCursor(ImGuiMouseCursor_None);
  return true;
}

} // namespace quader
