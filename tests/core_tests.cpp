////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "app/app_window_settings.hpp"
#include "assets/asset_registry.hpp"
#include "assets/bake_service.hpp"
#include "assets/default_material_catalog.hpp"
#include "assets/material_document.hpp"
#include "core/localization_service.hpp"
#include "core/logger.hpp"
#include "core/public_command_targets.hpp"
#include "core/undo_redo_manager.hpp"
#include "editor/commands/asset_command_service.hpp"
#include "editor/commands/document/apply_entity_transform_command.hpp"
#include "editor/commands/document/batch_document_command.hpp"
#include "editor/commands/document/create_entity_command.hpp"
#include "editor/commands/document/destroy_entity_command.hpp"
#include "editor/commands/document/duplicate_entity_command.hpp"
#include "editor/commands/document/group_selection_command.hpp"
#include "editor/commands/document/rename_entity_command.hpp"
#include "editor/commands/document/reparent_entity_command.hpp"
#include "editor/commands/document/select_entities_command.hpp"
#include "editor/commands/document/set_lock_command.hpp"
#include "editor/commands/document/set_visibility_command.hpp"
#include "editor/commands/document/ungroup_selection_command.hpp"
#include "editor/commands/document_command_service.hpp"
#include "editor/commands/editor_command_service.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/history/modeling_memento_command.hpp"
#include "editor/history/scene_transform_preview_transaction.hpp"
#include "editor/metadata/host_contract.hpp"
#include "editor/modeling/runtime/modeling_object_runtime_map.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/pivot/pivot_tool_service.hpp"
#include "editor/projection/selection_overlay_projection.hpp"
#include "editor/selection/editor_selection_service.hpp"
#include "editor/session/project_asset_bootstrap_service.hpp"
#include "editor/session/project_session.hpp"
#include "editor/tools/viewport_tool.hpp"
#include "editor/tools/viewport_tool_descriptor_catalog.hpp"
#include "editor/transform/scene_transform_drag_service.hpp"
#include "platform/platform_window.hpp"
#include "platform/sdl_event_utils.hpp"
#include "platform/sdl_input_adapter.hpp"
#include "render/filament_material_library.hpp"
#include "render/light_renderable_cache.hpp"
#include "render/material_binding_service.hpp"
#include "render/mesh_renderable_cache.hpp"
#include "render/mesh_renderable_factory.hpp"
#include "render/mesh_renderable_proxy.hpp"
#include "render/overlay/overlay_proxy_factory.hpp"
#include "render/overlay/overlay_renderable_cache.hpp"
#include "render/overlay/overlay_renderable_proxy.hpp"
#include "render/overlay/selected_object_overlay_cache.hpp"
#include "render/overlay/viewport_overlay_style_resolver.hpp"
#include "render/render_system.hpp"
#include "render/scene_render_bridge.hpp"
#include "render/scene_render_sync_service.hpp"
#include "render/texture_binding_service.hpp"
#include "render/texture_registry.hpp"
#include "render/tool_preview_renderable_cache.hpp"
#include "render/ui_icon_texture_service.hpp"
#include "render/viewport_grid_renderer.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "render/viewport_render_target.hpp"
#include "render/viewport_renderer.hpp"
#include "scene/document/project_document.hpp"
#include "scene/scene_bounds.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_entity_naming.hpp"
#include "scene/transform_pivot.hpp"
#include "support/guardrail_file_scan.hpp"
#include "ui/console_model.hpp"
#include "ui/ui_settings.hpp"
#include "ui/viewport_context_menu_policy.hpp"
#include "ui/viewport_input_frame_builder.hpp"
#include "ui/viewport_pane_overlay_cache.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"
#include "viewport/viewport_camera_adapter.hpp"
#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_grid_placement_policy.hpp"
#include "viewport/viewport_input_router.hpp"
#include "viewport/viewport_layout.hpp"
#include "viewport/viewport_settings.hpp"
#include "viewport/viewport_workspace_service.hpp"

#include <gtest/gtest.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>

#include <editor/interactions/viewport_navigation.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

using namespace quader;
using quader::test::guardrail::read_source_file;

#ifndef QUADER_SOURCE_DIR
#define QUADER_SOURCE_DIR "."
#endif

std::size_t count_occurrences(std::string_view haystack,
                              std::string_view needle) {
  std::size_t count = 0;
  std::size_t offset = 0;
  while (!needle.empty()) {
    offset = haystack.find(needle, offset);
    if (offset == std::string_view::npos) {
      break;
    }
    ++count;
    offset += needle.size();
  }
  return count;
}

std::string compact_source_text(std::string_view text) {
  std::string compact;
  compact.reserve(text.size());
  for (const char character : text) {
    if (!std::isspace(static_cast<unsigned char>(character))) {
      compact.push_back(character);
    }
  }
  return compact;
}

bool source_contains_compact(std::string_view haystack,
                             std::string_view needle) {
  const std::string compact_haystack = compact_source_text(haystack);
  const std::string compact_needle = compact_source_text(needle);
  return compact_haystack.find(compact_needle) != std::string::npos;
}

template <typename T> constexpr bool render_owner_is_non_transferable() {
  static_assert(std::is_default_constructible_v<T>);
  static_assert(!std::is_copy_constructible_v<T>);
  static_assert(!std::is_copy_assignable_v<T>);
  static_assert(!std::is_move_constructible_v<T>);
  static_assert(!std::is_move_assignable_v<T>);
  return true;
}

bool metadata_method_returns_literal(std::string_view source,
                                     std::string_view signature) {
  std::size_t offset = 0;
  while (true) {
    const std::size_t signature_pos = source.find(signature, offset);
    if (signature_pos == std::string_view::npos) {
      return false;
    }
    const std::size_t body_start = source.find('{', signature_pos);
    const std::size_t body_end = source.find('}', body_start);
    if (body_start == std::string_view::npos ||
        body_end == std::string_view::npos) {
      return false;
    }
    const std::size_t literal_return = source.find("return \"", body_start);
    if (literal_return != std::string_view::npos && literal_return < body_end) {
      return true;
    }
    offset = body_end + 1;
  }
}

ViewportShortcut test_tool_shortcut(ViewportToolId tool) {
  return {
      .kind = ViewportShortcutKind::ActivateTool,
      .context = ViewportShortcutContext::Viewport,
      .tool = tool,
  };
}

ViewportShortcut test_selection_shortcut(ViewportSelectionMode mode) {
  return {
      .kind = ViewportShortcutKind::SetSelectionMode,
      .context = ViewportShortcutContext::Viewport,
      .selection_mode = mode,
  };
}

ViewportShortcut test_grid_shortcut(int preset) {
  return {
      .kind = ViewportShortcutKind::SetGridPreset,
      .context = ViewportShortcutContext::Viewport,
      .grid_preset = preset,
  };
}

ViewportShortcut test_view_shortcut(ViewportView view) {
  return {
      .kind = ViewportShortcutKind::SetView,
      .context = ViewportShortcutContext::Viewport,
      .view = view,
  };
}

ViewportShortcut test_router_shortcut(
    ViewportShortcutKind kind,
    ViewportShortcutContext context = ViewportShortcutContext::Viewport) {
  return {
      .kind = kind,
      .context = context,
  };
}

ViewportShortcut test_mirror_shortcut(ViewportMirrorAxis axis) {
  return {
      .kind = ViewportShortcutKind::MirrorAxis,
      .context = ViewportShortcutContext::Viewport,
      .mirror_axis = axis,
  };
}

void set_resolved_shortcut(ViewportInputFrame &input,
                           ViewportShortcut shortcut) {
  input.resolved_shortcuts = {shortcut};
}

void set_navigation_pressed(ViewportInputFrame &input,
                            ViewportInputActionKind action) {
  input.navigation_bindings = {};
  switch (action) {
  case ViewportInputActionKind::Orbit:
    input.navigation_bindings.orbit_pressed = true;
    input.navigation_bindings.orbit_down = true;
    break;
  case ViewportInputActionKind::OrbitSnap:
    input.navigation_bindings.orbit_snap_pressed = true;
    input.navigation_bindings.orbit_snap_down = true;
    break;
  case ViewportInputActionKind::Pan:
    input.navigation_bindings.pan_pressed = true;
    input.navigation_bindings.pan_down = true;
    break;
  case ViewportInputActionKind::Fly:
    input.navigation_bindings.fly_pressed = true;
    input.navigation_bindings.fly_down = true;
    break;
  default:
    break;
  }
}

TEST(ViewportInputFrameBuilderTests, ModifierHelperPreservesCanonicalKeyMods) {
  ViewportModifiers modifiers = viewport_modifiers_from_imgui_state(
      false, false, false, false, ImGuiMod_Ctrl | ImGuiMod_Shift);
  EXPECT_TRUE(modifiers.primary);
  EXPECT_TRUE(modifiers.shift);
  EXPECT_FALSE(modifiers.alt);

  modifiers = viewport_modifiers_from_imgui_state(false, true, false, false,
                                                  ImGuiMod_Alt);
  EXPECT_TRUE(modifiers.primary);
  EXPECT_FALSE(modifiers.shift);
  EXPECT_TRUE(modifiers.alt);

  modifiers = viewport_modifiers_from_imgui_state(true, false, true, true, 0);
  EXPECT_TRUE(modifiers.primary);
  EXPECT_TRUE(modifiers.shift);
  EXPECT_TRUE(modifiers.alt);
}

TEST(SdlInputAdapterTests, KeyEventModifiersBuildPressedShortcutChord) {
  SDL_Event event{};
  event.type = SDL_EVENT_KEY_DOWN;
  event.key.key = SDLK_C;
  event.key.mod = SDL_KMOD_CTRL | SDL_KMOD_SHIFT;

  const std::optional<QuaderInputChord> chord =
      quader_pressed_input_chord_from_sdl_event(event);

  ASSERT_TRUE(chord.has_value());
  EXPECT_EQ(chord->device, QuaderInputDevice::Keyboard);
  EXPECT_EQ(chord->key, QuaderKeyCode::C);
  EXPECT_TRUE(chord->primary);
  EXPECT_TRUE(chord->shift);
  EXPECT_FALSE(chord->alt);

  event.key.repeat = true;
  EXPECT_FALSE(quader_pressed_input_chord_from_sdl_event(event).has_value());
}

/**
 * Owns a temporary ImGui context for UI/input behavior tests.
 */
class ScopedImGuiTestContext final {
public:
  ScopedImGuiTestContext()
      : previous_(ImGui::GetCurrentContext()),
        current_(ImGui::CreateContext()) {}
  ~ScopedImGuiTestContext() {
    ImGui::DestroyContext(current_);
    ImGui::SetCurrentContext(previous_);
  }

  [[nodiscard]] bool valid() const { return current_ != nullptr; }

private:
  ImGuiContext *previous_ = nullptr;
  ImGuiContext *current_ = nullptr;
};

TEST(ViewportInputFrameBuilderTests,
     EventPressedChordsOverrideImGuiPressedFallback) {
  ScopedImGuiTestContext imgui_context;
  ASSERT_TRUE(imgui_context.valid());

  const QuaderInputChord event_chord =
      keyboard_input_chord(QuaderKeyCode::C, {.primary = true, .shift = true});
  ViewportInputFrameBuilder builder;
  const ViewportInputFrame frame = builder.build({
      .hovered = true,
      .focused = true,
      .item_size = {800.0f, 600.0f},
      .pressed_input_chords =
          std::span<const QuaderInputChord>(&event_chord, 1),
  });

  ASSERT_EQ(frame.pressed_input_chords.size(), 1u);
  EXPECT_EQ(frame.pressed_input_chords.front().key, QuaderKeyCode::C);
  EXPECT_TRUE(frame.pressed_input_chords.front().primary);
  EXPECT_TRUE(frame.pressed_input_chords.front().shift);
}

void set_navigation_down(ViewportInputFrame &input,
                         ViewportInputActionKind action) {
  input.navigation_bindings = {};
  switch (action) {
  case ViewportInputActionKind::Orbit:
    input.navigation_bindings.orbit_down = true;
    break;
  case ViewportInputActionKind::OrbitSnap:
    input.navigation_bindings.orbit_snap_down = true;
    break;
  case ViewportInputActionKind::Pan:
    input.navigation_bindings.pan_down = true;
    break;
  case ViewportInputActionKind::Fly:
    input.navigation_bindings.fly_down = true;
    break;
  default:
    break;
  }
}

void set_navigation_released(ViewportInputFrame &input,
                             ViewportInputActionKind action) {
  input.navigation_bindings = {};
  switch (action) {
  case ViewportInputActionKind::Orbit:
    input.navigation_bindings.orbit_released = true;
    break;
  case ViewportInputActionKind::OrbitSnap:
    input.navigation_bindings.orbit_snap_released = true;
    break;
  case ViewportInputActionKind::Pan:
    input.navigation_bindings.pan_released = true;
    break;
  case ViewportInputActionKind::Fly:
    input.navigation_bindings.fly_released = true;
    break;
  default:
    break;
  }
}

/**
 * Carries EditorSelectionTestHarness state used by the test layer.
 */
struct EditorSelectionTestHarness {
  explicit EditorSelectionTestHarness(SceneDocument &scene,
                                      AssetRegistry &assets,
                                      ModelingSessionService &modeling)
      : document_commands(scene, history),
        selection(scene, document_commands, modeling) {
    (void)assets;
  }

  EditorHistoryService history;
  DocumentCommandService document_commands;
  EditorSelectionService selection;
};

DocumentCommandResult
select_through_editor(EditorSelectionService &selection,
                      std::initializer_list<EntityId> entities,
                      EntityId active = kInvalidEntityId) {
  std::vector<EntityId> selected(entities);
  const EntityId resolved_active =
      active != kInvalidEntityId || selected.empty() ? active : selected.back();
  return selection.set_scene_selection(
      std::span<const EntityId>(selected.data(), selected.size()),
      resolved_active);
}

static_assert(std::is_base_of_v<IUndoableCommand, ModelingMementoCommand>);
static_assert(
    std::is_base_of_v<ModelingMementoCommand, DuplicateMeshesHistoryCommand>);

/**
 * Applies Test Value Command behavior as a command in the automated test suite.
 */
class TestValueCommand final : public IUndoableCommand {
public:
  TestValueCommand(std::string label, int &value, int before, int after,
                   bool *fail_undo = nullptr, bool *fail_redo = nullptr,
                   bool undo_changed = true, bool redo_changed = true,
                   std::string merge_key = {})
      : label_(std::move(label)), value_(value), before_(before), after_(after),
        fail_undo_(fail_undo), fail_redo_(fail_redo),
        undo_changed_(undo_changed), redo_changed_(redo_changed),
        merge_key_(std::move(merge_key)) {}

  [[nodiscard]] const std::string &label() const override { return label_; }

  CommandResult undo() override {
    if (fail_undo_ != nullptr && *fail_undo_) {
      return command_invalid_result("undo failed");
    }
    value_ = before_;
    return {
        .ok = true,
        .changed = undo_changed_,
    };
  }

  CommandResult redo() override {
    if (fail_redo_ != nullptr && *fail_redo_) {
      return command_invalid_result("redo failed");
    }
    value_ = after_;
    return {
        .ok = true,
        .changed = redo_changed_,
    };
  }

  bool merge_with(const IUndoableCommand &newer_command,
                  std::string_view merge_key) override {
    const auto *newer = dynamic_cast<const TestValueCommand *>(&newer_command);
    if (newer == nullptr || merge_key.empty() || merge_key_ != merge_key ||
        newer->merge_key_ != merge_key || &newer->value_ != &value_) {
      return false;
    }
    after_ = newer->after_;
    redo_changed_ = redo_changed_ || newer->redo_changed_;
    return true;
  }

private:
  std::string label_;
  int &value_;
  int before_ = 0;
  int after_ = 0;
  bool *fail_undo_ = nullptr;
  bool *fail_redo_ = nullptr;
  bool undo_changed_ = true;
  bool redo_changed_ = true;
  std::string merge_key_;
};

void expect_color_near(ViewportSettingColor actual,
                       ViewportSettingColor expected) {
  EXPECT_NEAR(actual.r, expected.r, 0.001f);
  EXPECT_NEAR(actual.g, expected.g, 0.001f);
  EXPECT_NEAR(actual.b, expected.b, 0.001f);
  EXPECT_NEAR(actual.a, expected.a, 0.001f);
}

ViewportSettingColor overlay_policy_setting_color(std::uint32_t abgr) {
  return {
      static_cast<float>(abgr & 0xffu) / 255.0f,
      static_cast<float>((abgr >> 8) & 0xffu) / 255.0f,
      static_cast<float>((abgr >> 16) & 0xffu) / 255.0f,
      static_cast<float>((abgr >> 24) & 0xffu) / 255.0f,
  };
}

void expect_vec3_near(QVec3 actual, QVec3 expected) {
  EXPECT_NEAR(actual.x, expected.x, 0.001f);
  EXPECT_NEAR(actual.y, expected.y, 0.001f);
  EXPECT_NEAR(actual.z, expected.z, 0.001f);
}

double mesh_position_fingerprint(const MeshAsset &mesh) {
  double value = 0.0;
  for (std::size_t index = 0; index < mesh.vertices.size(); ++index) {
    const QVec3 &position = mesh.vertices[index].position;
    const double weight = static_cast<double>(index + 1);
    value += weight * (static_cast<double>(position.x * position.x) * 0.31 +
                       static_cast<double>(position.y * position.y) * 0.53 +
                       static_cast<double>(position.z * position.z) * 0.71);
  }
  return value;
}

int preview_element_id(quader_poly::ElementId id) {
  return static_cast<int>(id);
}

std::pair<int, int> preview_edge_pair(quader_poly::Edge edge) {
  return {preview_element_id(edge.a), preview_element_id(edge.b)};
}

/**
 * Stores closed-edge fixture data for edge-bevel preview selection rebuild
 * tests.
 */
struct PreviewClosedEdgeFixture {
  quader_poly::Document document;
  std::pair<int, int> first_edge = {-1, -1};
  std::pair<int, int> second_edge = {-1, -1};
};

PreviewClosedEdgeFixture make_preview_closed_edge_fixture() {
  PreviewClosedEdgeFixture fixture;
  fixture.document = quader_poly::make_cube(quader_poly::CubePrimitiveSpec{});
  const std::vector<quader_poly::Edge> edges =
      quader_poly::document_edges(fixture.document);
  EXPECT_GE(edges.size(), 2u);
  if (edges.size() >= 2u) {
    fixture.first_edge = preview_edge_pair(edges[0]);
    fixture.second_edge = preview_edge_pair(edges[1]);
  }
  return fixture;
}

/**
 * Stores two disconnected quad faces for face-preview selection rebuild tests.
 */
struct PreviewParallelQuadFixture {
  quader_poly::Document document;
  int front_face = -1;
  int back_face = -1;
};

void append_preview_parallel_quad_pair(quader_poly::Document &document,
                                       float x_offset, int &front_face,
                                       int &back_face) {
  const quader_poly::ElementId a =
      quader_poly::add_vertex(document, {-1.0f + x_offset, -1.0f, -1.0f});
  const quader_poly::ElementId b =
      quader_poly::add_vertex(document, {1.0f + x_offset, -1.0f, -1.0f});
  const quader_poly::ElementId c =
      quader_poly::add_vertex(document, {1.0f + x_offset, 1.0f, -1.0f});
  const quader_poly::ElementId d =
      quader_poly::add_vertex(document, {-1.0f + x_offset, 1.0f, -1.0f});
  const quader_poly::ElementId e =
      quader_poly::add_vertex(document, {-1.0f + x_offset, -1.0f, 1.0f});
  const quader_poly::ElementId f =
      quader_poly::add_vertex(document, {1.0f + x_offset, -1.0f, 1.0f});
  const quader_poly::ElementId g =
      quader_poly::add_vertex(document, {1.0f + x_offset, 1.0f, 1.0f});
  const quader_poly::ElementId h =
      quader_poly::add_vertex(document, {-1.0f + x_offset, 1.0f, 1.0f});
  const quader_poly::ElementId first_face =
      quader_poly::add_face(document, std::array{a, b, c, d});
  const quader_poly::ElementId second_face =
      quader_poly::add_face(document, std::array{h, g, f, e});
  EXPECT_NE(first_face, quader_poly::kInvalidElementId);
  EXPECT_NE(second_face, quader_poly::kInvalidElementId);
  front_face = preview_element_id(first_face);
  back_face = preview_element_id(second_face);
}

PreviewParallelQuadFixture
make_preview_parallel_quad_fixture(float x_offset = 0.0f) {
  PreviewParallelQuadFixture fixture;
  append_preview_parallel_quad_pair(fixture.document, x_offset,
                                    fixture.front_face, fixture.back_face);
  return fixture;
}

/**
 * Stores two independent bridgeable face pairs for bridge preview selection
 * rebuild tests.
 */
struct PreviewBridgeFixture {
  quader_poly::Document document;
  int first_front_face = -1;
  int first_back_face = -1;
  int second_front_face = -1;
  int second_back_face = -1;
};

PreviewBridgeFixture make_preview_bridge_fixture() {
  PreviewBridgeFixture fixture;
  append_preview_parallel_quad_pair(fixture.document, 0.0f,
                                    fixture.first_front_face,
                                    fixture.first_back_face);
  append_preview_parallel_quad_pair(fixture.document, 5.0f,
                                    fixture.second_front_face,
                                    fixture.second_back_face);
  return fixture;
}

/**
 * Stores independent near-vertex pairs for merge-by-distance preview selection
 * rebuild tests.
 */
struct PreviewMergeDistanceFixture {
  quader_poly::Document document;
  int first_near_a = -1;
  int first_near_b = -1;
  int second_near_a = -1;
  int second_near_b = -1;
};

PreviewMergeDistanceFixture make_preview_merge_distance_fixture() {
  PreviewMergeDistanceFixture fixture;
  auto add_pair = [&](float x_offset, int &near_a, int &near_b) {
    const quader_poly::ElementId a =
        quader_poly::add_vertex(fixture.document, {x_offset, 0.0f, 0.0f});
    const quader_poly::ElementId b = quader_poly::add_vertex(
        fixture.document, {x_offset + 1.0f, 0.0f, 0.0f});
    const quader_poly::ElementId c =
        quader_poly::add_vertex(fixture.document, {x_offset, 1.0f, 0.0f});
    const quader_poly::ElementId d =
        quader_poly::add_vertex(fixture.document, {x_offset, 1.1f, 0.0f});
    const quader_poly::ElementId e = quader_poly::add_vertex(
        fixture.document, {x_offset - 1.0f, 1.0f, 0.0f});
    const quader_poly::ElementId f = quader_poly::add_vertex(
        fixture.document, {x_offset - 1.0f, 0.0f, 0.0f});
    const quader_poly::ElementId first_face =
        quader_poly::add_face(fixture.document, std::array{a, b, c});
    const quader_poly::ElementId second_face =
        quader_poly::add_face(fixture.document, std::array{d, e, f});
    EXPECT_NE(first_face, quader_poly::kInvalidElementId);
    EXPECT_NE(second_face, quader_poly::kInvalidElementId);
    near_a = preview_element_id(c);
    near_b = preview_element_id(d);
  };
  add_pair(0.0f, fixture.first_near_a, fixture.first_near_b);
  add_pair(5.0f, fixture.second_near_a, fixture.second_near_b);
  return fixture;
}

ModelingViewportCamera test_orthographic_viewport_camera() {
  return {
      .orthographic = true,
      .origin = {0.0f, 0.0f, 10.0f},
      .x_axis = {1.0f, 0.0f, 0.0f},
      .y_axis = {0.0f, 1.0f, 0.0f},
      .z_axis = {0.0f, 0.0f, 1.0f},
      .viewport_size = {100.0f, 100.0f},
      .orthographic_size = 100.0,
      .fov_degrees = 60.0,
  };
}

ModelingRay test_viewport_forward_ray(float x = 0.0f) {
  return {
      .origin = {x, 0.0f, 10.0f},
      .direction = {0.0f, 0.0f, -1.0f},
  };
}

ModelingToolRayEvent test_insert_edge_loop_event(float edge_y = 0.0f,
                                                 float edge_x = 1.0f) {
  return {
      .screen_position = {},
      .ray =
          {
              .origin = {edge_x, edge_y, 5.0f},
              .direction = {0.0f, 0.0f, -1.0f},
          },
      .camera = test_orthographic_viewport_camera(),
      .grid_size = 1.0,
      .vertex_radius = 0.3,
      .edge_radius = 0.2,
      .snap_enabled = false,
  };
}

ModelingToolRayEvent test_drawn_plane_event(float x, bool dragging = false) {
  ModelingViewportCamera camera = test_orthographic_viewport_camera();
  camera.orthographic = false;
  return {
      .screen_position = {},
      .ray =
          {
              .origin = {x, 2.0f, 0.0f},
              .direction = {0.0f, -1.0f, 0.0f},
          },
      .camera = camera,
      .grid_size = 0.5,
      .snap_enabled = true,
      .dragging = dragging,
  };
}

ModelingTransformDragBeginRequest
test_x_move_drag_begin_request(QVec3 pivot = {},
                               ModelingTransformOperation operation =
                                   ModelingTransformOperation::Transform,
                               bool duplicate_selected = false) {
  return {
      .tool = ModelingTransformTool::Move,
      .operation = operation,
      .axis = ModelingTransformAxis::X,
      .pivot = pivot,
      .screen_position = {0.0f, 0.0f},
      .screen_pivot = {0.0f, 0.0f},
      .screen_tip = {10.0f, 0.0f},
      .has_world_direction = true,
      .world_direction = {1.0f, 0.0f, 0.0f},
      .ray = test_viewport_forward_ray(),
      .camera = test_orthographic_viewport_camera(),
      .grid_size = 1.0,
      .vertex_snap_radius = 0.3,
      .grid_snap_enabled = true,
      .duplicate_selected = duplicate_selected,
  };
}

ModelingTransformDragUpdateRequest
test_x_move_drag_update_request(float screen_x) {
  return {
      .screen_position = {screen_x, 0.0f},
      .ray = test_viewport_forward_ray(),
      .camera = test_orthographic_viewport_camera(),
      .grid_size = 1.0,
      .vertex_snap_radius = 0.3,
      .grid_snap_enabled = true,
      .precision = false,
  };
}

QuaderRay test_api_viewport_forward_ray(float x = 0.0f) {
  return {
      .origin = {x, 0.0f, 10.0f},
      .direction = {0.0f, 0.0f, -1.0f},
  };
}

QuaderViewportCamera test_api_orthographic_viewport_camera() {
  return {
      .orthographic = true,
      .origin = {0.0f, 0.0f, 10.0f},
      .x_axis = {1.0f, 0.0f, 0.0f},
      .y_axis = {0.0f, 1.0f, 0.0f},
      .z_axis = {0.0f, 0.0f, 1.0f},
      .viewport_size = {100.0f, 100.0f},
      .orthographic_size = 100.0,
      .fov_degrees = 60.0,
  };
}

QuaderTransformDragBeginRequest test_api_x_move_drag_begin_request() {
  return {
      .tool = QuaderTransformTool::Move,
      .operation = QuaderTransformOperation::Transform,
      .axis = QuaderTransformAxis::X,
      .screen_position = {0.0f, 0.0f},
      .screen_pivot = {0.0f, 0.0f},
      .screen_tip = {10.0f, 0.0f},
      .has_world_direction = true,
      .world_direction = {1.0f, 0.0f, 0.0f},
      .ray = test_api_viewport_forward_ray(),
      .camera = test_api_orthographic_viewport_camera(),
      .grid_size = 1.0,
      .vertex_snap_radius = 0.3,
      .grid_snap_enabled = true,
  };
}

QuaderTransformDragUpdateRequest
test_api_x_move_drag_update_request(float screen_x) {
  return {
      .screen_position = {screen_x, 0.0f},
      .ray = test_api_viewport_forward_ray(),
      .camera = test_api_orthographic_viewport_camera(),
      .grid_size = 1.0,
      .vertex_snap_radius = 0.3,
      .grid_snap_enabled = true,
      .precision = false,
  };
}

MeshAsset make_test_triangle_mesh(std::string name = "Test Mesh") {
  MeshAsset mesh;
  mesh.name = std::move(name);
  mesh.vertices = {
      MeshVertex{.position = {-0.5f, 0.0f, 0.0f},
                 .normal = {0.0f, 1.0f, 0.0f},
                 .uv = {0.0f, 0.0f}},
      MeshVertex{.position = {0.5f, 0.0f, 0.0f},
                 .normal = {0.0f, 1.0f, 0.0f},
                 .uv = {1.0f, 0.0f}},
      MeshVertex{.position = {0.0f, 0.0f, 1.0f},
                 .normal = {0.0f, 1.0f, 0.0f},
                 .uv = {0.5f, 1.0f}},
  };
  mesh.indices = {0u, 1u, 2u};
  mesh.primitives = {MeshPrimitiveSpan{
      .index_offset = 0u, .index_count = 3u, .material_slot = 0u}};
  return mesh;
}

TEST(LoggerTests, RoutesMessagesToMemorySink) {
  Logger::initialize(
      LoggerConfig{.name = "test", .file_path = "test.log", .max_entries = 16});
  Logger::clear_memory();

  LOG_INFO("hello {}", "quader");

  const auto entries = Logger::entries_snapshot();
  ASSERT_FALSE(entries.empty());
  EXPECT_NE(entries.back().message.find("hello quader"), std::string::npos);
}

TEST(FatalErrorHandlerTests, MainInstallsReportableFatalErrorBoundary) {
  const std::string main_source = read_source_file("src/main.cpp");
  const std::string handler_source =
      read_source_file("src/platform/fatal_error_handler.cpp");
  const std::string cmake_source = read_source_file("CMakeLists.txt");

  EXPECT_NE(main_source.find("install_fatal_error_handler(log_path)"),
            std::string::npos);
  EXPECT_NE(main_source.find("report_fatal_error_and_exit"), std::string::npos);
  EXPECT_NE(main_source.find("FatalErrorCode::UnhandledCppException"),
            std::string::npos);
  EXPECT_NE(handler_source.find("Quader Fatal Error"), std::string::npos);
  EXPECT_NE(handler_source.find("Error code:"), std::string::npos);
  EXPECT_NE(handler_source.find("Quick message:"), std::string::npos);
  EXPECT_NE(handler_source.find("Crash report:"), std::string::npos);
  EXPECT_NE(handler_source.find("crash-reports"), std::string::npos);
  EXPECT_NE(handler_source.find("MiniDumpWriteDump"), std::string::npos);
  EXPECT_NE(handler_source.find("write_crash_artifacts"), std::string::npos);
  EXPECT_NE(handler_source.find("append_text_file(g_log_path"),
            std::string::npos);
  EXPECT_NE(handler_source.find("module_description_for_address"),
            std::string::npos);
  EXPECT_NE(handler_source.find("Stack trace (most recent call first):"),
            std::string::npos);
  EXPECT_NE(handler_source.find("CaptureStackBackTrace"), std::string::npos);
  EXPECT_NE(handler_source.find("StackWalk64"), std::string::npos);
  EXPECT_NE(handler_source.find("SymFromAddr"), std::string::npos);
  EXPECT_NE(handler_source.find("QDR-FATAL-0000"), std::string::npos);
  EXPECT_NE(handler_source.find("QDR-FATAL-0001"), std::string::npos);
  EXPECT_NE(handler_source.find("QDR-FATAL-0002"), std::string::npos);
  EXPECT_NE(handler_source.find("Internal runtime abort."), std::string::npos);
  EXPECT_NE(handler_source.find("SIGABRT"), std::string::npos);
  EXPECT_NE(handler_source.find("_set_abort_behavior"), std::string::npos);
  EXPECT_NE(cmake_source.find("Dbghelp"), std::string::npos);
}

TEST(FatalErrorHandlerTests, SentryNativeWrapsFatalAndVerificationEvents) {
  const std::string main_source = read_source_file("src/main.cpp");
  const std::string sentry_header =
      read_source_file("src/platform/sentry_client.hpp");
  const std::string sentry_source =
      read_source_file("src/platform/sentry_client.cpp");

  EXPECT_NE(sentry_header.find("kQuaderSentryDsn"), std::string::npos);
  EXPECT_NE(sentry_header.find("enum class SentryEventLevel"),
            std::string::npos);
  EXPECT_NE(sentry_header.find("Fatal"), std::string::npos);
  EXPECT_NE(main_source.find("--sentry-verify"), std::string::npos);
  EXPECT_NE(main_source.find("\"It works!\""), std::string::npos);
  EXPECT_NE(main_source.find("Unhandled exception in Quader main"),
            std::string::npos);
  EXPECT_NE(main_source.find("shutdown_sentry_client()"), std::string::npos);
  EXPECT_NE(sentry_source.find("SENTRY_LEVEL_FATAL"), std::string::npos);
  EXPECT_NE(sentry_source.find("sentry_close()"), std::string::npos);
}

TEST(ConsoleModelTests, FiltersByLevelAndSearch) {
  std::vector<LogEntry> entries = {
      {.level = spdlog::level::info, .message = "engine ready"},
      {.level = spdlog::level::err, .message = "vulkan failed"},
  };

  const auto filtered = ConsoleModel::filter(
      entries, {.minimum_level = spdlog::level::warn, .search_text = "vulkan"});

  ASSERT_EQ(filtered.size(), 1u);
  EXPECT_EQ(filtered[0].message, "vulkan failed");
}

TEST(ConsoleModelTests, RespectsMinimumSeverityLevels) {
  std::vector<LogEntry> entries = {
      {.level = spdlog::level::trace, .message = "trace"},
      {.level = spdlog::level::debug, .message = "debug"},
      {.level = spdlog::level::info, .message = "info"},
      {.level = spdlog::level::warn, .message = "warn"},
      {.level = spdlog::level::err, .message = "error"},
      {.level = spdlog::level::critical, .message = "critical"},
  };

  EXPECT_EQ(
      ConsoleModel::filter(entries, {.minimum_level = spdlog::level::trace})
          .size(),
      6u);
  EXPECT_EQ(
      ConsoleModel::filter(entries, {.minimum_level = spdlog::level::info})
          .size(),
      4u);
  EXPECT_EQ(
      ConsoleModel::filter(entries, {.minimum_level = spdlog::level::warn})
          .size(),
      3u);
  EXPECT_EQ(ConsoleModel::filter(entries, {.minimum_level = spdlog::level::err})
                .size(),
            2u);
  EXPECT_TRUE(
      ConsoleModel::filter(entries, {.minimum_level = spdlog::level::off})
          .empty());
}

TEST(AppWindowSettingsTests, RoundTripsNormalBoundsAndMaximizedState) {
  const std::filesystem::path path = std::filesystem::temp_directory_path() /
                                     "quader_window_settings_round_trip.ini";
  std::filesystem::remove(path);

  AppWindowSettings settings = AppWindowSettings::defaults();
  settings.set_placement({
      .normal_frame =
          {
              .x = -120,
              .y = 84,
              .width = 1440,
              .height = 900,
          },
      .maximized = true,
  });

  ASSERT_TRUE(settings.save(path));
  const AppWindowSettings loaded = AppWindowSettings::load(path);

  ASSERT_TRUE(loaded.placement().has_value());
  EXPECT_EQ(loaded.placement()->normal_frame.x, -120);
  EXPECT_EQ(loaded.placement()->normal_frame.y, 84);
  EXPECT_EQ(loaded.placement()->normal_frame.width, 1440);
  EXPECT_EQ(loaded.placement()->normal_frame.height, 900);
  EXPECT_TRUE(loaded.placement()->maximized);
}

TEST(AppWindowSettingsTests, IgnoresIncompleteWindowPlacementFiles) {
  const std::filesystem::path path = std::filesystem::temp_directory_path() /
                                     "quader_window_settings_incomplete.ini";
  std::filesystem::remove(path);

  {
    std::ofstream file(path, std::ios::trunc);
    ASSERT_TRUE(file.is_open());
    file << "[Window]\n";
    file << "NormalX=10\n";
    file << "NormalY=20\n";
    file << "Maximized=true\n";
  }

  const AppWindowSettings loaded = AppWindowSettings::load(path);
  EXPECT_FALSE(loaded.placement().has_value());
}

TEST(UISettingsTests, DefaultsKeepPanelVisibilityLocaleAndNavigationGizmo) {
  const UISettings settings = UISettings::defaults();

  EXPECT_EQ(settings.locale(), "en-US");
  EXPECT_TRUE(settings.is_panel_visible(UIPanelId::Viewport));
  EXPECT_TRUE(settings.is_panel_visible(UIPanelId::Outliner));
  EXPECT_TRUE(settings.is_panel_visible(UIPanelId::Inspector));
  EXPECT_FALSE(settings.is_panel_visible(UIPanelId::Console));
  EXPECT_TRUE(settings.navigation_gizmo_visible());
}

TEST(UISettingsTests, RoundTripsPanelVisibilityLocaleAndNavigationGizmo) {
  const std::filesystem::path path = std::filesystem::temp_directory_path() /
                                     "quader_ui_settings_round_trip.ini";
  std::filesystem::remove(path);

  UISettings settings = UISettings::defaults();
  settings.set_locale(" it-IT ");
  settings.set_panel_visible(UIPanelId::Outliner, false);
  settings.set_panel_visible(UIPanelId::Inspector, false);
  settings.set_panel_visible(UIPanelId::Console, true);
  settings.set_navigation_gizmo_visible(false);

  ASSERT_TRUE(settings.save(path));
  std::ifstream saved(path);
  ASSERT_TRUE(saved.is_open());
  const std::string saved_text{std::istreambuf_iterator<char>(saved),
                               std::istreambuf_iterator<char>()};
  EXPECT_NE(saved_text.find("outliner=false"), std::string::npos);
  EXPECT_NE(saved_text.find("inspector=false"), std::string::npos);
  EXPECT_NE(saved_text.find("console=true"), std::string::npos);
  EXPECT_EQ(saved_text.find("Outliner="), std::string::npos);
  EXPECT_EQ(saved_text.find("Inspector="), std::string::npos);
  EXPECT_EQ(saved_text.find("Console="), std::string::npos);
  saved.close();

  const UISettings loaded = UISettings::load(path);

  EXPECT_EQ(loaded.locale(), "it-IT");
  EXPECT_FALSE(loaded.is_panel_visible(UIPanelId::Outliner));
  EXPECT_FALSE(loaded.is_panel_visible(UIPanelId::Inspector));
  EXPECT_TRUE(loaded.is_panel_visible(UIPanelId::Console));
  EXPECT_FALSE(loaded.navigation_gizmo_visible());

  std::filesystem::remove(path);
}

TEST(UISettingsTests, LoadsLegacyDisplayPanelKeysAndSavesStableKeys) {
  const std::filesystem::path path = std::filesystem::temp_directory_path() /
                                     "quader_ui_settings_legacy_panel_keys.ini";
  std::filesystem::remove(path);

  {
    std::ofstream file(path, std::ios::trunc);
    ASSERT_TRUE(file.is_open());
    file << "[Panels]\n";
    file << "Outliner=false\n";
    file << "Inspector=false\n";
    file << "Console=true\n";
  }

  UISettings loaded = UISettings::load(path);
  EXPECT_FALSE(loaded.is_panel_visible(UIPanelId::Outliner));
  EXPECT_FALSE(loaded.is_panel_visible(UIPanelId::Inspector));
  EXPECT_TRUE(loaded.is_panel_visible(UIPanelId::Console));

  ASSERT_TRUE(loaded.save(path));
  std::ifstream saved(path);
  ASSERT_TRUE(saved.is_open());
  const std::string saved_text{std::istreambuf_iterator<char>(saved),
                               std::istreambuf_iterator<char>()};
  EXPECT_NE(saved_text.find("outliner=false"), std::string::npos);
  EXPECT_NE(saved_text.find("inspector=false"), std::string::npos);
  EXPECT_NE(saved_text.find("console=true"), std::string::npos);
  EXPECT_EQ(saved_text.find("Outliner="), std::string::npos);
  EXPECT_EQ(saved_text.find("Inspector="), std::string::npos);
  EXPECT_EQ(saved_text.find("Console="), std::string::npos);
  saved.close();

  std::filesystem::remove(path);
}

TEST(UISettingsTests, IgnoresMalformedPanelVisibilityAndEmptyLocale) {
  const std::filesystem::path path = std::filesystem::temp_directory_path() /
                                     "quader_ui_settings_malformed.ini";
  std::filesystem::remove(path);

  {
    std::ofstream file(path, std::ios::trunc);
    ASSERT_TRUE(file.is_open());
    file << "[Panels]\n";
    file << "Outliner=maybe\n";
    file << "Inspector=0\n";
    file << "Console=yes\n";
    file << "[Show]\n";
    file << "NavigationGizmo=no\n";
    file << "[Localization]\n";
    file << "Locale=   \n";
  }

  const UISettings loaded = UISettings::load(path);

  EXPECT_EQ(loaded.locale(), "en-US");
  EXPECT_TRUE(loaded.is_panel_visible(UIPanelId::Outliner));
  EXPECT_FALSE(loaded.is_panel_visible(UIPanelId::Inspector));
  EXPECT_TRUE(loaded.is_panel_visible(UIPanelId::Console));
  EXPECT_FALSE(loaded.navigation_gizmo_visible());

  std::filesystem::remove(path);
}

TEST(PlatformWindowChromeTests,
     HitTestSupportsResizeEdgesToolbarDragAndWindowControls) {
  PlatformWindowChromeHitTest hit_test{
      .window_width = 800,
      .window_height = 600,
      .resize_border = 6,
      .title_bar_height = 35,
      .left_interactive_width = 440,
      .right_controls_width = 105,
  };

  auto hit = [&](int x, int y) {
    hit_test.point_x = x;
    hit_test.point_y = y;
    return platform_window_chrome_hit_test_result(hit_test);
  };

  EXPECT_EQ(hit(0, 0), SDL_HITTEST_RESIZE_TOPLEFT);
  EXPECT_EQ(hit(799, 0), SDL_HITTEST_RESIZE_TOPRIGHT);
  EXPECT_EQ(hit(799, 599), SDL_HITTEST_RESIZE_BOTTOMRIGHT);
  EXPECT_EQ(hit(0, 599), SDL_HITTEST_RESIZE_BOTTOMLEFT);
  EXPECT_EQ(hit(400, 0), SDL_HITTEST_RESIZE_TOP);
  EXPECT_EQ(hit(799, 300), SDL_HITTEST_RESIZE_RIGHT);
  EXPECT_EQ(hit(400, 599), SDL_HITTEST_RESIZE_BOTTOM);
  EXPECT_EQ(hit(0, 300), SDL_HITTEST_RESIZE_LEFT);
  EXPECT_EQ(hit(80, 24), SDL_HITTEST_NORMAL);
  EXPECT_EQ(hit(400, 24), SDL_HITTEST_NORMAL);
  EXPECT_EQ(hit(500, 24), SDL_HITTEST_DRAGGABLE);
  EXPECT_EQ(hit(700, 24), SDL_HITTEST_NORMAL);
  EXPECT_EQ(hit(400, 60), SDL_HITTEST_NORMAL);

  hit_test.maximized = true;
  EXPECT_EQ(hit(80, 24), SDL_HITTEST_NORMAL);
  EXPECT_EQ(hit(400, 0), SDL_HITTEST_NORMAL);
  EXPECT_EQ(hit(500, 0), SDL_HITTEST_DRAGGABLE);
  EXPECT_EQ(hit(799, 599), SDL_HITTEST_NORMAL);
}

TEST(PlatformWindowChromeTests,
     ResizeBorderUsesSystemMetricsWhenLargerThanFallback) {
  EXPECT_EQ(platform_window_chrome_resize_border_for_system_metrics(6, 4, 4),
            8);
  EXPECT_EQ(platform_window_chrome_resize_border_for_system_metrics(6, 1, 1),
            6);
  EXPECT_EQ(platform_window_chrome_resize_border_for_system_metrics(0, 0, 0),
            1);
}

TEST(PlatformWindowChromeTests, RecoversPersistedWindowFrameToVisibleWorkArea) {
  const std::vector<PlatformWindowChromeFrame> work_areas = {
      {.x = 0, .y = 0, .width = 1920, .height = 1040},
      {.x = -1280, .y = 0, .width = 1280, .height = 984},
  };

  const PlatformWindowChromeFrame offscreen =
      platform_window_recover_frame_to_display_work_area(
          {.x = 4000, .y = 2000, .width = 1600, .height = 900}, work_areas);
  EXPECT_EQ(offscreen.x, 320);
  EXPECT_EQ(offscreen.y, 140);
  EXPECT_EQ(offscreen.width, 1600);
  EXPECT_EQ(offscreen.height, 900);

  const PlatformWindowChromeFrame secondary =
      platform_window_recover_frame_to_display_work_area(
          {.x = -1200, .y = 100, .width = 900, .height = 700}, work_areas);
  EXPECT_EQ(secondary.x, -1200);
  EXPECT_EQ(secondary.y, 100);
  EXPECT_EQ(secondary.width, 900);
  EXPECT_EQ(secondary.height, 700);

  const PlatformWindowChromeFrame oversized =
      platform_window_recover_frame_to_display_work_area(
          {.x = 0, .y = 0, .width = 5000, .height = 3000}, work_areas);
  EXPECT_EQ(oversized.x, 0);
  EXPECT_EQ(oversized.y, 0);
  EXPECT_EQ(oversized.width, 1920);
  EXPECT_EQ(oversized.height, 1040);
}

TEST(PlatformWindowChromeTests, ManualResizeFrameSupportsEdgesAndCorners) {
  PlatformWindowChromeFrame frame{
      .x = 100,
      .y = 80,
      .width = 800,
      .height = 600,
  };

  auto resize = [&](SDL_HitTestResult edge, int dx, int dy) {
    return platform_window_chrome_resize_frame({
        .start = frame,
        .delta_x = dx,
        .delta_y = dy,
        .min_width = 320,
        .min_height = 240,
        .edge = edge,
    });
  };

  EXPECT_EQ(resize(SDL_HITTEST_RESIZE_RIGHT, 40, 0).width, 840);
  EXPECT_EQ(resize(SDL_HITTEST_RESIZE_BOTTOM, 0, 30).height, 630);

  const PlatformWindowChromeFrame left = resize(SDL_HITTEST_RESIZE_LEFT, 50, 0);
  EXPECT_EQ(left.x, 150);
  EXPECT_EQ(left.width, 750);

  const PlatformWindowChromeFrame top_left =
      resize(SDL_HITTEST_RESIZE_TOPLEFT, -20, -10);
  EXPECT_EQ(top_left.x, 80);
  EXPECT_EQ(top_left.y, 70);
  EXPECT_EQ(top_left.width, 820);
  EXPECT_EQ(top_left.height, 610);

  const PlatformWindowChromeFrame clamped =
      resize(SDL_HITTEST_RESIZE_TOPLEFT, 700, 500);
  EXPECT_EQ(clamped.x, 580);
  EXPECT_EQ(clamped.y, 440);
  EXPECT_EQ(clamped.width, 320);
  EXPECT_EQ(clamped.height, 240);
}

TEST(PlatformWindowChromeTests,
     ManualResizeFrameDefaultsToApplicationMinimumSize) {
  EXPECT_EQ(kPlatformWindowMinimumWidth, 800);
  EXPECT_EQ(kPlatformWindowMinimumHeight, 600);

  const PlatformWindowChromeFrame frame{
      .x = 100,
      .y = 80,
      .width = 1200,
      .height = 900,
  };

  const PlatformWindowChromeFrame clamped =
      platform_window_chrome_resize_frame({
          .start = frame,
          .delta_x = 1000,
          .delta_y = 800,
          .edge = SDL_HITTEST_RESIZE_TOPLEFT,
      });

  EXPECT_EQ(clamped.x, 500);
  EXPECT_EQ(clamped.y, 380);
  EXPECT_EQ(clamped.width, 800);
  EXPECT_EQ(clamped.height, 600);
}

TEST(PlatformWindowChromeTests,
     RenderFrameAvailabilityRejectsHiddenOrMinimizedWindowStates) {
  EXPECT_TRUE(platform_window_can_render_frames(0u, 1600, 900));
  EXPECT_TRUE(
      platform_window_can_render_frames(SDL_WINDOW_INPUT_FOCUS, 1600, 900));
  EXPECT_TRUE(
      platform_window_can_render_frames(SDL_WINDOW_MAXIMIZED, 1600, 900));
  EXPECT_TRUE(
      platform_window_can_render_frames(SDL_WINDOW_OCCLUDED, 1600, 900));
  EXPECT_TRUE(platform_window_can_render_frames(
      SDL_WINDOW_MAXIMIZED | SDL_WINDOW_OCCLUDED, 1600, 900));

  EXPECT_FALSE(
      platform_window_can_render_frames(SDL_WINDOW_MINIMIZED, 1600, 900));
  EXPECT_FALSE(platform_window_can_render_frames(SDL_WINDOW_HIDDEN, 1600, 900));
  EXPECT_FALSE(platform_window_can_render_frames(
      SDL_WINDOW_MINIMIZED | SDL_WINDOW_INPUT_FOCUS, 1600, 900));
  EXPECT_FALSE(platform_window_can_render_frames(
      SDL_WINDOW_MINIMIZED | SDL_WINDOW_OCCLUDED, 1600, 900));
  EXPECT_FALSE(platform_window_can_render_frames(0u, 0, 900));
  EXPECT_FALSE(platform_window_can_render_frames(0u, 1600, 0));
}

TEST(SdlEventUtilsTests, ExtractsWindowIdsForRuntimeRoutedEvents) {
  constexpr SDL_WindowID kWindowId = 42u;
  SDL_Event event{};

  event.type = SDL_EVENT_WINDOW_RESIZED;
  event.window.windowID = kWindowId;
  EXPECT_EQ(sdl_event_window_id(event), kWindowId);

  event = {};
  event.type = SDL_EVENT_MOUSE_MOTION;
  event.motion.windowID = kWindowId;
  EXPECT_EQ(sdl_event_window_id(event), kWindowId);

  event = {};
  event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
  event.button.windowID = kWindowId;
  EXPECT_EQ(sdl_event_window_id(event), kWindowId);

  event = {};
  event.type = SDL_EVENT_MOUSE_WHEEL;
  event.wheel.windowID = kWindowId;
  EXPECT_EQ(sdl_event_window_id(event), kWindowId);

  event = {};
  event.type = SDL_EVENT_KEY_DOWN;
  event.key.windowID = kWindowId;
  EXPECT_EQ(sdl_event_window_id(event), kWindowId);

  event = {};
  event.type = SDL_EVENT_TEXT_INPUT;
  event.text.windowID = kWindowId;
  EXPECT_EQ(sdl_event_window_id(event), kWindowId);

  event = {};
  event.type = SDL_EVENT_WINDOW_OCCLUDED;
  event.window.windowID = kWindowId;
  EXPECT_EQ(sdl_event_window_id(event), kWindowId);

  event = {};
  event.type = SDL_EVENT_QUIT;
  EXPECT_FALSE(sdl_event_window_id(event).has_value());
}

TEST(ViewportSettingsTests, DefaultsMatchNativeViewportSettingsState) {
  const ViewportSettings settings = ViewportSettings::defaults();

  ASSERT_EQ(viewport_settings_sections().size(), 6u);
  EXPECT_EQ(viewport_settings_sections()[0].label, "Grid");
  EXPECT_FALSE(viewport_settings_sections()[0].default_expanded);
  EXPECT_EQ(viewport_settings_sections()[1].label, "Selection");
  EXPECT_EQ(viewport_settings_sections()[2].label, "Overlay");
  EXPECT_EQ(viewport_settings_sections()[3].label, "Vertices");
  EXPECT_EQ(viewport_settings_sections()[4].label, "Picking");
  EXPECT_EQ(viewport_settings_sections()[5].label, "Gizmo");

  expect_color_near(settings.color(ViewportColorSettingId::SelectionWire),
                    overlay_policy_setting_color(kOverlaySelectionWireColor));
  expect_color_near(settings.color(ViewportColorSettingId::SourceWire),
                    overlay_policy_setting_color(kOverlaySourceWireColor));
  expect_color_near(settings.color(ViewportColorSettingId::GridMinor),
                    {150.0f / 255.0f, 150.0f / 255.0f, 150.0f / 255.0f, 1.0f});
  expect_color_near(settings.color(ViewportColorSettingId::GridMajor),
                    {210.0f / 255.0f, 210.0f / 255.0f, 210.0f / 255.0f, 1.0f});
  expect_color_near(settings.color(ViewportColorSettingId::MeshGridMinor),
                    {0.02f, 0.02f, 0.02f, 1.0f});
  expect_color_near(settings.color(ViewportColorSettingId::MeshGridMajor),
                    {1.0f, 0.84f, 0.22f, 1.0f});
  const SceneSettingsComponent scene_settings_defaults;
  expect_vec3_near(scene_settings_defaults.environment.clear_color,
                   {2.0f / 255.0f, 2.0f / 255.0f, 2.0f / 255.0f});
  EXPECT_FLOAT_EQ(
      settings.float_value(ViewportFloatSettingId::GridMinorLineSize), 0.325f);
  EXPECT_FLOAT_EQ(
      settings.float_value(ViewportFloatSettingId::GridMajorLineSize), 0.250f);
  EXPECT_FLOAT_EQ(
      settings.float_value(ViewportFloatSettingId::GridAxisLineSize), 1.0f);
  EXPECT_FLOAT_EQ(
      settings.float_value(ViewportFloatSettingId::SelectionEdgeLineSize),
      2.5f);
  EXPECT_FLOAT_EQ(
      settings.float_value(ViewportFloatSettingId::PickVertexRadius), 0.3f);
  EXPECT_FLOAT_EQ(settings.float_value(ViewportFloatSettingId::GizmoSize),
                  1.0f);
  ASSERT_EQ(viewport_bool_settings().size(), 7u);
  EXPECT_TRUE(settings.bool_value(ViewportBoolSettingId::ShowGrid));
  EXPECT_FALSE(settings.bool_value(ViewportBoolSettingId::ShowMeshGrid));
  EXPECT_TRUE(settings.bool_value(ViewportBoolSettingId::ShowGizmos));
  EXPECT_TRUE(settings.bool_value(ViewportBoolSettingId::ShowOverlays));
  EXPECT_FALSE(settings.bool_value(ViewportBoolSettingId::ShowInfo));
  EXPECT_TRUE(settings.bool_value(ViewportBoolSettingId::GridSnap));
  EXPECT_TRUE(settings.bool_value(ViewportBoolSettingId::VertexSnap));
}

TEST(ViewportSettingsTests,
     SnapSettingsOwnTemporaryGridToggleAndGlobalVertexSnap) {
  ViewportSettings settings = ViewportSettings::defaults();

  EXPECT_TRUE(viewport_grid_snap_active(settings, false));
  EXPECT_FALSE(viewport_grid_snap_active(settings, true));
  EXPECT_FLOAT_EQ(viewport_vertex_snap_radius(settings), 0.3f);

  EXPECT_TRUE(settings.set_bool_value(ViewportBoolSettingId::GridSnap, false));
  EXPECT_FALSE(viewport_grid_snap_active(settings, false));
  EXPECT_TRUE(viewport_grid_snap_active(settings, true));

  EXPECT_TRUE(
      settings.set_bool_value(ViewportBoolSettingId::VertexSnap, false));
  EXPECT_FLOAT_EQ(viewport_vertex_snap_radius(settings), 0.0f);
}

TEST(ViewportSettingsTests, OverlayDefaultsComeFromRenderOverlayPolicy) {
  const ViewportSettings settings = ViewportSettings::defaults();
  const auto &color_settings = viewport_color_settings();
  const auto &float_settings = viewport_float_settings();

  const auto color_default = [&](ViewportColorSettingId id) {
    const std::size_t idx = static_cast<std::size_t>(id);
    EXPECT_EQ(color_settings[idx].id, id);
    return color_settings[idx].default_value;
  };
  const auto float_default = [&](ViewportFloatSettingId id) {
    const std::size_t idx = static_cast<std::size_t>(id);
    EXPECT_EQ(float_settings[idx].id, id);
    return float_settings[idx].default_value;
  };
  const auto expect_overlay_color_default = [&](ViewportColorSettingId id,
                                                std::uint32_t policy_color) {
    const ViewportSettingColor expected =
        overlay_policy_setting_color(policy_color);
    expect_color_near(color_default(id), expected);
    expect_color_near(settings.color(id), expected);
  };
  const auto expect_overlay_float_default = [&](ViewportFloatSettingId id,
                                                float policy_value) {
    EXPECT_FLOAT_EQ(float_default(id), policy_value);
    EXPECT_FLOAT_EQ(settings.float_value(id), policy_value);
  };

  expect_overlay_color_default(ViewportColorSettingId::SelectionFace,
                               kOverlaySelectionFaceColor);
  expect_overlay_color_default(ViewportColorSettingId::SelectionWire,
                               kOverlaySelectionWireColor);
  expect_overlay_color_default(ViewportColorSettingId::SourceWire,
                               kOverlaySourceWireColor);
  expect_overlay_color_default(ViewportColorSettingId::OpenEdge,
                               kOverlayOpenEdgeColor);
  expect_overlay_color_default(ViewportColorSettingId::DiagnosticEdge,
                               kOverlayDiagnosticEdgeColor);
  expect_overlay_color_default(ViewportColorSettingId::HoverFace,
                               kOverlayHoverFaceColor);
  expect_overlay_color_default(ViewportColorSettingId::HoverWire,
                               kOverlayHoverWireColor);
  expect_overlay_color_default(ViewportColorSettingId::RemoveSelectionFace,
                               kOverlayRemoveFaceColor);
  expect_overlay_color_default(ViewportColorSettingId::RemoveSelectionWire,
                               kOverlayRemoveWireColor);
  expect_overlay_color_default(ViewportColorSettingId::Vertex,
                               kOverlayVertexColor);
  expect_overlay_color_default(ViewportColorSettingId::SelectedVertex,
                               kOverlaySelectedVertexColor);
  expect_overlay_color_default(ViewportColorSettingId::HoverVertex,
                               kOverlayHoverVertexColor);
  expect_overlay_color_default(ViewportColorSettingId::RemoveSelectionVertex,
                               kOverlayRemoveWireColor);

  expect_overlay_float_default(ViewportFloatSettingId::SourceWireLineSize,
                               kOverlaySourceWireLineWidthPixels);
  expect_overlay_float_default(
      ViewportFloatSettingId::SelectionFaceWireLineSize,
      kOverlaySelectionWireLineWidthPixels);
  expect_overlay_float_default(ViewportFloatSettingId::SelectionEdgeLineSize,
                               kOverlaySelectionWireLineWidthPixels);
  expect_overlay_float_default(ViewportFloatSettingId::HoverWireLineSize,
                               kOverlayHoverWireLineWidthPixels);
  expect_overlay_float_default(ViewportFloatSettingId::OpenEdgeLineSize,
                               kOverlayOpenEdgeLineWidthPixels);
  expect_overlay_float_default(ViewportFloatSettingId::DiagnosticEdgeLineSize,
                               kOverlayDiagnosticEdgeLineWidthPixels);
  expect_overlay_float_default(ViewportFloatSettingId::VertexSize,
                               kOverlayVertexSizePixels);
  expect_overlay_float_default(ViewportFloatSettingId::SelectedVertexGrowth,
                               kOverlaySelectedVertexGrowthPixels);
  expect_overlay_float_default(ViewportFloatSettingId::HoverVertexGrowth,
                               kOverlayHoverVertexGrowthPixels);
  expect_overlay_float_default(ViewportFloatSettingId::VertexOutlineSize,
                               kOverlayVertexOutlineSizePixels);
}

TEST(ViewportSettingsTests, SavesLoadsAndClampsViewportPreferenceKeys) {
  const std::filesystem::path path = std::filesystem::temp_directory_path() /
                                     "quader_viewport_settings_round_trip.ini";
  std::filesystem::remove(path);

  ViewportSettings settings = ViewportSettings::defaults();
  EXPECT_TRUE(settings.set_color(ViewportColorSettingId::GridMinor,
                                 {0.25f, 0.5f, 0.75f, 0.6f}));
  EXPECT_TRUE(settings.set_color(ViewportColorSettingId::GridMajor,
                                 {0.0f, 0.0f, 0.0f, 1.0f}));
  EXPECT_TRUE(settings.set_color(ViewportColorSettingId::MeshGridMinor,
                                 {0.1f, 0.2f, 0.3f, 0.4f}));
  EXPECT_TRUE(settings.set_color(ViewportColorSettingId::MeshGridMajor,
                                 {0.8f, 0.7f, 0.6f, 0.5f}));
  EXPECT_TRUE(
      settings.set_float_value(ViewportFloatSettingId::GizmoSize, 10.0f));
  EXPECT_TRUE(settings.set_bool_value(ViewportBoolSettingId::ShowGrid, false));
  EXPECT_TRUE(
      settings.set_bool_value(ViewportBoolSettingId::ShowMeshGrid, true));
  EXPECT_TRUE(settings.set_bool_value(ViewportBoolSettingId::ShowInfo, true));
  EXPECT_TRUE(settings.set_bool_value(ViewportBoolSettingId::GridSnap, false));
  EXPECT_TRUE(
      settings.set_bool_value(ViewportBoolSettingId::VertexSnap, false));
  ASSERT_TRUE(settings.save(path));

  const ViewportSettings loaded = ViewportSettings::load(path);
  expect_color_near(loaded.color(ViewportColorSettingId::GridMinor),
                    {0.25098f, 0.501961f, 0.74902f, 0.6f});
  expect_color_near(loaded.color(ViewportColorSettingId::GridMajor),
                    {0.0f, 0.0f, 0.0f, 1.0f});
  expect_color_near(loaded.color(ViewportColorSettingId::MeshGridMinor),
                    {0.101961f, 0.2f, 0.301961f, 0.4f});
  expect_color_near(loaded.color(ViewportColorSettingId::MeshGridMajor),
                    {0.8f, 0.701961f, 0.6f, 0.501961f});
  EXPECT_FLOAT_EQ(loaded.float_value(ViewportFloatSettingId::GizmoSize), 2.0f);
  EXPECT_FALSE(loaded.bool_value(ViewportBoolSettingId::ShowGrid));
  EXPECT_TRUE(loaded.bool_value(ViewportBoolSettingId::ShowMeshGrid));
  EXPECT_TRUE(loaded.bool_value(ViewportBoolSettingId::ShowInfo));
  EXPECT_FALSE(loaded.bool_value(ViewportBoolSettingId::GridSnap));
  EXPECT_FALSE(loaded.bool_value(ViewportBoolSettingId::VertexSnap));

  {
    std::ifstream source_file(path);
    ASSERT_TRUE(source_file.is_open()) << path.string();
    const std::string source{std::istreambuf_iterator<char>(source_file),
                             std::istreambuf_iterator<char>()};
    EXPECT_NE(source.find("QVertexWorks.Quader.Viewport.GridMinorColor"),
              std::string::npos);
    EXPECT_NE(
        source.find("QVertexWorks.Quader.Viewport.GridMajorColor=000000FF"),
        std::string::npos);
    EXPECT_NE(source.find("QVertexWorks.Quader.Viewport.MeshGridMinorColor"),
              std::string::npos);
    EXPECT_NE(source.find("QVertexWorks.Quader.Viewport.MeshGridMajorColor"),
              std::string::npos);
    EXPECT_NE(source.find("QVertexWorks.Quader.Viewport.GizmoSize"),
              std::string::npos);
    EXPECT_NE(source.find("QVertexWorks.Quader.Viewport.ShowGrid"),
              std::string::npos);
    EXPECT_NE(source.find("QVertexWorks.Quader.Viewport.ShowMeshGrid"),
              std::string::npos);
    EXPECT_NE(source.find("QVertexWorks.Quader.Viewport.ShowInfo"),
              std::string::npos);
    EXPECT_NE(source.find("QVertexWorks.Quader.Viewport.GridSnap"),
              std::string::npos);
    EXPECT_NE(source.find("QVertexWorks.Quader.Viewport.VertexSnap"),
              std::string::npos);
    EXPECT_EQ(source.find("QVertexWorks.Quader.Viewport.BackgroundColor"),
              std::string::npos);
    EXPECT_EQ(source.find("QVertexWorks.Quader.Viewport.ShowSpecialTextures"),
              std::string::npos);
  }

  std::filesystem::remove(path);
}

TEST(GroundGridMaterialTests, UsesUserWorldAnchoring) {
  const std::string source =
      read_source_file("resources/materials/ground_grid.mat");
  const std::string grid_renderer_source =
      read_source_file("src/render/viewport_grid_renderer.cpp");

  EXPECT_NE(source.find("shadingModel : unlit"), std::string::npos);
  EXPECT_NE(source.find("depthWrite : false"), std::string::npos);
  EXPECT_NE(source.find("getUserWorldPosition()"), std::string::npos);
  EXPECT_EQ(source.find("getWorldPosition()"), std::string::npos);
  EXPECT_NE(source.find("localPosition"), std::string::npos);
  EXPECT_NE(source.find("variable_localPosition"), std::string::npos);
  EXPECT_NE(source.find("cameraSize"), std::string::npos);
  EXPECT_NE(source.find("cameraFarPlane"), std::string::npos);
  EXPECT_NE(source.find("cameraFarFade"), std::string::npos);
  EXPECT_NE(source.find("visibleGridSize"), std::string::npos);
  EXPECT_NE(source.find("minGridWorldSpacing"), std::string::npos);
  EXPECT_NE(source.find("cameraDistance = length(worldPosition - "
                        "materialParams.cameraPosition)"),
            std::string::npos);
  EXPECT_NE(source.find("farHalf = max(materialParams.cameraFarPlane * 0.5"),
            std::string::npos);
  EXPECT_NE(source.find("farClipFade = mix(1.0, farClipFade"),
            std::string::npos);
  EXPECT_NE(source.find("distanceFade * edgeFade * farClipFade"),
            std::string::npos);
  EXPECT_NE(source.find("majorCoverage = parentGridValue * "
                        "materialParams.majorGridColor.a"),
            std::string::npos);
  EXPECT_NE(source.find("minorCoverage = gridValue * "
                        "materialParams.gridColor.a * (1.0 - clamp("),
            std::string::npos);
  EXPECT_EQ(source.find("alpha = max(alpha, parentGridValue * "
                        "materialParams.majorGridColor.a)"),
            std::string::npos);
  EXPECT_NE(source.find("vec4(color * alpha, alpha)"), std::string::npos);
  EXPECT_EQ(source.find("getUV0()"), std::string::npos);
  EXPECT_EQ(source.find("planeCenter"), std::string::npos);
  EXPECT_NE(grid_renderer_source.find("\"cameraFarPlane\""),
            std::string::npos);
  EXPECT_NE(grid_renderer_source.find("\"cameraFarFade\""),
            std::string::npos);
  EXPECT_NE(grid_renderer_source.find(
                "ViewportProjection::Perspective ? 1.0f"),
            std::string::npos);
  EXPECT_NE(grid_renderer_source.find(".castShadows(false)"),
            std::string::npos);
  EXPECT_NE(grid_renderer_source.find(".receiveShadows(false)"),
            std::string::npos);
}

TEST(QuaderMeshMaterialTests, UsesOverlaySelectionInsteadOfStandaloneTint) {
  const std::string source =
      read_source_file("resources/materials/quader_mesh.mat");

  EXPECT_EQ(source.find("selectionTint"), std::string::npos);
  EXPECT_EQ(source.find("getColor()"), std::string::npos);
  EXPECT_EQ(source.find("vertexColor"), std::string::npos);
  EXPECT_EQ(source.find("color,"), std::string::npos);
}

TEST(QuaderMeshMaterialTests, UsesLitTextureTransformTimesColorAndPbrFactors) {
  const std::string shaded_source =
      read_source_file("resources/materials/quader_mesh.mat");
  const std::string source =
      read_source_file("resources/materials/quader_mesh_lit.mat");

  EXPECT_NE(shaded_source.find("shadingModel : unlit"), std::string::npos);
  EXPECT_NE(shaded_source.find("vec2 uv = vec2(getUV0().x, 1.0 - getUV0().y)"),
            std::string::npos);
  EXPECT_NE(shaded_source.find("texture(materialParams_baseColorTexture, uv)"),
            std::string::npos);
  EXPECT_NE(
      shaded_source.find("material.baseColor = vec4(baseColor, baseSample.a * "
                         "materialParams.baseColorFactor.a)"),
      std::string::npos);
  EXPECT_EQ(shaded_source.find("roughnessTexture"), std::string::npos);
  EXPECT_EQ(shaded_source.find("editor_unlit"), std::string::npos);
  EXPECT_NE(source.find("shadingModel : lit"), std::string::npos);
  EXPECT_EQ(source.find("doubleSided : true"), std::string::npos);
  EXPECT_NE(source.find("vec2 uv = vec2(getUV0().x, 1.0 - getUV0().y)"),
            std::string::npos);
  EXPECT_NE(source.find("texture(materialParams_baseColorTexture, uv)"),
            std::string::npos);
  EXPECT_NE(source.find("texture(materialParams_roughnessTexture, uv).r"),
            std::string::npos);
  EXPECT_NE(source.find("baseSample.rgb * materialParams.baseColorFactor.rgb"),
            std::string::npos);
  EXPECT_NE(source.find("material.baseColor = vec4(baseColor, baseSample.a * "
                        "materialParams.baseColorFactor.a)"),
            std::string::npos);
  EXPECT_NE(source.find("material.roughness = clamp(roughnessSample * "
                        "materialParams.roughnessFactor"),
            std::string::npos);
  EXPECT_NE(
      source.find("material.metallic = clamp(materialParams.metallicFactor"),
      std::string::npos);
  EXPECT_EQ(source.find("editor_unlit"), std::string::npos);
}

TEST(QuaderMeshMaterialTests, SupportsOptionalWorldSpaceSurfaceGrid) {
  const std::array<std::string, 2> sources = {
      read_source_file("resources/materials/quader_mesh.mat"),
      read_source_file("resources/materials/quader_mesh_lit.mat"),
  };

  for (const std::string &source : sources) {
    EXPECT_NE(source.find("surfaceGridMinorColor"), std::string::npos);
    EXPECT_NE(source.find("surfaceGridMajorColor"), std::string::npos);
    EXPECT_NE(source.find("surfaceGridEnabled"), std::string::npos);
    EXPECT_NE(source.find("tangents"), std::string::npos);
    EXPECT_NE(source.find("getUserWorldPosition()"), std::string::npos);
    EXPECT_NE(source.find("getWorldGeometricNormalVector()"),
              std::string::npos);
    EXPECT_NE(source.find("surfaceGridCoordinates("), std::string::npos);
    EXPECT_NE(source.find("surfaceGridFloor("), std::string::npos);
    EXPECT_NE(source.find("surfaceGridLineCoverage("), std::string::npos);
    EXPECT_NE(source.find("float worldPixelWidth = max(length(dFdx(worldPosition)), "
                          "length(dFdy(worldPosition)))"),
              std::string::npos);
    EXPECT_NE(source.find("surfaceGridMinorLineThickness"),
              std::string::npos);
    EXPECT_NE(source.find("surfaceGridMajorLineThickness"),
              std::string::npos);
    EXPECT_NE(source.find("baseColor = applySurfaceGrid(baseColor)"),
              std::string::npos);
    EXPECT_NE(source.find("float derivativeWidth = fwidth(coord)"),
              std::string::npos);
    EXPECT_NE(source.find("float lodSpacing = max(surfaceGridFloor"),
              std::string::npos);
    EXPECT_NE(source.find("return mix(currentCoverage, nextCoverage, blend);"),
              std::string::npos);
    EXPECT_EQ(source.find("surfaceGridVisibility("), std::string::npos);
    EXPECT_EQ(source.find("coverage * surfaceGridVisibility"),
              std::string::npos);
    EXPECT_EQ(source.find("cross(dFdx(worldPosition), dFdy(worldPosition))"),
              std::string::npos);
    EXPECT_EQ(source.find("Strip test"), std::string::npos);
    EXPECT_EQ(source.find("vec2 coords = worldPosition.xz;"),
              std::string::npos);
    EXPECT_EQ(source.find("return coverage;"), std::string::npos);
    EXPECT_EQ(source.find("return coverage * min(thickness, 1.0)"),
              std::string::npos);
  }
}

TEST(OverlayMaterialTests, PremultipliesVertexAlphaForTransparentFaceFill) {
  const std::string source =
      read_source_file("resources/materials/vertex_color_overlay.mat");

  EXPECT_NE(source.find("blending : transparent"), std::string::npos);
  EXPECT_NE(source.find("depthWrite : false"), std::string::npos);
  EXPECT_NE(source.find("vec4(vertexColor.rgb * vertexColor.a, vertexColor.a)"),
            std::string::npos);
}

TEST(OverlayMaterialTests,
     LineAndPointMaterialsUseScreenSpaceOverlayExpansion) {
  const std::string line_source =
      read_source_file("resources/materials/quader_overlay_line.mat");

  EXPECT_NE(line_source.find("vertexDomain : device"), std::string::npos);
  EXPECT_NE(line_source.find("requires : [\n        color,\n        custom0"),
            std::string::npos);
  EXPECT_NE(line_source.find("material.lineDistancePixels = getCustom0()"),
            std::string::npos);
  EXPECT_EQ(line_source.find("material.world_position"), std::string::npos);
  EXPECT_EQ(line_source.find("getClipFromViewMatrix()"), std::string::npos);
  EXPECT_EQ(line_source.find("name : nearPlane"), std::string::npos);
  EXPECT_NE(line_source.find("variable_lineDistancePixels.x"),
            std::string::npos);

  const std::string point_source =
      read_source_file("resources/materials/quader_overlay_point.mat");
  const std::string icon_source =
      read_source_file("resources/materials/quader_overlay_icon.mat");

  EXPECT_NE(point_source.find("vertexDomain : device"), std::string::npos);
  EXPECT_NE(point_source.find("requires : [\n        uv0,\n        color"),
            std::string::npos);
  EXPECT_EQ(point_source.find("material.world_position"), std::string::npos);
  EXPECT_EQ(point_source.find("getClipFromViewMatrix()"), std::string::npos);
  EXPECT_EQ(point_source.find("name : nearPlane"), std::string::npos);
  EXPECT_NE(point_source.find("variable_pointUv.xy"), std::string::npos);
  EXPECT_NE(point_source.find("materialParams.innerRadius"), std::string::npos);
  const std::size_t inner_radius_guard =
      point_source.find("if (materialParams.innerRadius > 0.0)");
  const std::size_t outer_circle_discard =
      point_source.find("if (distanceFromCenter > 0.5)");
  ASSERT_NE(outer_circle_discard, std::string::npos);
  ASSERT_NE(inner_radius_guard, std::string::npos);
  EXPECT_LT(inner_radius_guard, outer_circle_discard);

  EXPECT_NE(icon_source.find("vertexDomain : device"), std::string::npos);
  EXPECT_NE(icon_source.find("requires : [\n        uv0,\n        color"),
            std::string::npos);
  EXPECT_NE(icon_source.find("type : sampler2d"), std::string::npos);
  EXPECT_NE(icon_source.find("name : iconTexture"), std::string::npos);
  EXPECT_NE(icon_source.find("materialParams_iconTexture"), std::string::npos);
  EXPECT_EQ(icon_source.find("material.world_position"), std::string::npos);
  EXPECT_EQ(icon_source.find("getClipFromViewMatrix()"), std::string::npos);
  EXPECT_EQ(icon_source.find("name : nearPlane"), std::string::npos);
  EXPECT_NE(icon_source.find("sampled.rgb * tint.rgb * alpha"),
            std::string::npos);
}

TEST(OverlayMaterialTests,
     ImGuiMaterialCanCompositePremultipliedDisplayTextures) {
  const std::string source = read_source_file("resources/materials/imgui.mat");
  const std::string renderer =
      read_source_file("src/render/imgui_filament_renderer.cpp");

  EXPECT_NE(source.find("name : premultipliedAlpha"), std::string::npos);
  EXPECT_NE(source.find("vec3 straightRgb = sampled.rgb * tint.rgb * alpha"),
            std::string::npos);
  EXPECT_NE(
      source.find("vec3 premultipliedRgb = sampled.rgb * tint.rgb * tint.a"),
      std::string::npos);
  EXPECT_NE(
      source.find("mix(straightRgb, premultipliedRgb, premultipliedAlpha)"),
      std::string::npos);
  EXPECT_NE(
      renderer.find("TextureSampleColorSpace::DisplaySrgbPremultipliedAlpha"),
      std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      renderer, "material_instance->setParameter(\"premultipliedAlpha\", "
                "premultiplied_alpha)"));
  EXPECT_NE(renderer.find("clear_options.discard = false"), std::string::npos);
  EXPECT_NE(renderer.find("renderer.setClearOptions(clear_options)"),
            std::string::npos);
  EXPECT_NE(renderer.find("framebuffer_width_int"), std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      renderer, "std::clamp(static_cast<int>(std::floor(clip_min.x))"));
  EXPECT_TRUE(source_contains_compact(
      renderer, ".scissor_bottom = static_cast<std::uint32_t>("
                "framebuffer_height_int - scissor_bottom)"));
  EXPECT_NE(renderer.find("material_instance->unsetScissor();"),
            std::string::npos);
}

TEST(ViewportOverlayPolicyTests, MatchesNativeLayerDrawOrder) {
  const std::array<ViewportOverlayLayer, kViewportOverlayLayerCount> expected{{
      ViewportOverlayLayer::SelectedVisibleFace,
      ViewportOverlayLayer::SelectedFaceMask,
      ViewportOverlayLayer::WireframeBase,
      ViewportOverlayLayer::WireframeXray,
      ViewportOverlayLayer::SelectedXrayFace,
      ViewportOverlayLayer::HoverFace,
      ViewportOverlayLayer::SourceWireDepthStamp,
      ViewportOverlayLayer::SourceWire,
      ViewportOverlayLayer::OpenEdge,
      ViewportOverlayLayer::DiagnosticEdge,
      ViewportOverlayLayer::SelectedFaceEdge,
      ViewportOverlayLayer::SelectedEdge,
      ViewportOverlayLayer::GroupBounds,
      ViewportOverlayLayer::HoverFaceEdge,
      ViewportOverlayLayer::HoverEdge,
      ViewportOverlayLayer::UnselectedVertex,
      ViewportOverlayLayer::SelectedVertex,
      ViewportOverlayLayer::HoverVertex,
      ViewportOverlayLayer::SnapTargetVertex,
      ViewportOverlayLayer::PointLightGizmo,
      ViewportOverlayLayer::ToolPlane,
      ViewportOverlayLayer::ToolPreviewFill,
      ViewportOverlayLayer::ToolPlaneLine,
      ViewportOverlayLayer::ToolPreviewLine,
      ViewportOverlayLayer::ToolPreviewSeam,
      ViewportOverlayLayer::ToolHandle,
  }};

  EXPECT_EQ(kViewportOverlayDrawOrder, expected);
  for (std::size_t index = 0; index < expected.size(); ++index) {
    EXPECT_EQ(viewport_overlay_draw_order(expected[index]),
              static_cast<int>(index));
  }
}

TEST(ViewportOverlayPolicyTests, MatchesDepthAndHoverSuppressionContract) {
  EXPECT_TRUE(viewport_overlay_depth_tested(
      ViewportOverlayLayer::WireframeBase, ViewportOverlayGeometryKind::Lines));
  EXPECT_FALSE(viewport_overlay_depth_tested(
      ViewportOverlayLayer::WireframeXray, ViewportOverlayGeometryKind::Lines));
  EXPECT_FALSE(viewport_overlay_depth_tested(
      ViewportOverlayLayer::SourceWire, ViewportOverlayGeometryKind::Lines));
  EXPECT_TRUE(viewport_overlay_depth_tested(
      ViewportOverlayLayer::OpenEdge, ViewportOverlayGeometryKind::Lines));
  EXPECT_FALSE(viewport_overlay_depth_tested(
      ViewportOverlayLayer::HoverFaceEdge, ViewportOverlayGeometryKind::Lines));
  EXPECT_FALSE(viewport_overlay_depth_tested(
      ViewportOverlayLayer::HoverEdge, ViewportOverlayGeometryKind::Lines));
  EXPECT_TRUE(
      viewport_overlay_depth_tested(ViewportOverlayLayer::UnselectedVertex,
                                    ViewportOverlayGeometryKind::Points));

  EXPECT_EQ(viewport_overlay_depth_mode(ViewportOverlayLayer::SourceWire,
                                        ViewportOverlayGeometryKind::Lines),
            ViewportOverlayDepthMode::DrawOnTop);
  EXPECT_EQ(viewport_overlay_depth_mode(ViewportOverlayLayer::WireframeXray,
                                        ViewportOverlayGeometryKind::Lines),
            ViewportOverlayDepthMode::DrawOnTop);
  EXPECT_EQ(viewport_overlay_depth_mode(ViewportOverlayLayer::SelectedFaceEdge,
                                        ViewportOverlayGeometryKind::Lines),
            ViewportOverlayDepthMode::DrawOnTop);
  EXPECT_EQ(viewport_overlay_depth_mode(ViewportOverlayLayer::SelectedEdge,
                                        ViewportOverlayGeometryKind::Lines),
            ViewportOverlayDepthMode::DrawOnTop);
  EXPECT_EQ(viewport_overlay_depth_mode(ViewportOverlayLayer::HoverFaceEdge,
                                        ViewportOverlayGeometryKind::Lines),
            ViewportOverlayDepthMode::DrawOnTop);
  EXPECT_EQ(viewport_overlay_depth_mode(ViewportOverlayLayer::HoverEdge,
                                        ViewportOverlayGeometryKind::Lines),
            ViewportOverlayDepthMode::DrawOnTop);
  EXPECT_EQ(viewport_overlay_depth_mode(ViewportOverlayLayer::GroupBounds,
                                        ViewportOverlayGeometryKind::Lines),
            ViewportOverlayDepthMode::DrawOnTop);
  EXPECT_EQ(viewport_overlay_depth_mode(ViewportOverlayLayer::DiagnosticEdge,
                                        ViewportOverlayGeometryKind::Lines),
            ViewportOverlayDepthMode::DrawOnTop);
  EXPECT_EQ(viewport_overlay_depth_mode(ViewportOverlayLayer::SelectedVertex,
                                        ViewportOverlayGeometryKind::Points),
            ViewportOverlayDepthMode::DepthTested);
  EXPECT_EQ(viewport_overlay_depth_mode(ViewportOverlayLayer::HoverVertex,
                                        ViewportOverlayGeometryKind::Points),
            ViewportOverlayDepthMode::DepthTested);
  EXPECT_EQ(viewport_overlay_depth_mode(ViewportOverlayLayer::ToolPlane,
                                        ViewportOverlayGeometryKind::Triangles),
            ViewportOverlayDepthMode::DrawOnTop);
  EXPECT_EQ(viewport_overlay_depth_mode(ViewportOverlayLayer::ToolPlaneLine,
                                        ViewportOverlayGeometryKind::Lines),
            ViewportOverlayDepthMode::DrawOnTop);
  EXPECT_EQ(viewport_overlay_depth_mode(ViewportOverlayLayer::ToolHandle,
                                        ViewportOverlayGeometryKind::Points),
            ViewportOverlayDepthMode::DrawOnTop);
  EXPECT_EQ(viewport_overlay_depth_mode(ViewportOverlayLayer::PointLightGizmo,
                                        ViewportOverlayGeometryKind::Icons),
            ViewportOverlayDepthMode::DrawOnTop);
  EXPECT_FALSE(
      viewport_overlay_depth_tested(ViewportOverlayLayer::PointLightGizmo,
                                    ViewportOverlayGeometryKind::Icons));

  EXPECT_TRUE(viewport_overlay_layer_uses_depth_stamped_face_fill(
      ViewportOverlayLayer::SelectedVisibleFace));
  EXPECT_TRUE(viewport_overlay_layer_uses_depth_stamped_face_fill(
      ViewportOverlayLayer::SelectedXrayFace));
  EXPECT_TRUE(viewport_overlay_layer_uses_depth_stamped_face_fill(
      ViewportOverlayLayer::SelectedFaceMask));
  EXPECT_TRUE(viewport_overlay_layer_uses_depth_stamped_face_fill(
      ViewportOverlayLayer::HoverFace));
  EXPECT_TRUE(viewport_overlay_triangle_is_two_sided(
      ViewportOverlayLayer::SelectedVisibleFace));
  EXPECT_TRUE(viewport_overlay_triangle_is_two_sided(
      ViewportOverlayLayer::SelectedXrayFace));
  EXPECT_TRUE(viewport_overlay_triangle_is_two_sided(
      ViewportOverlayLayer::SelectedFaceMask));
  EXPECT_TRUE(
      viewport_overlay_triangle_is_two_sided(ViewportOverlayLayer::HoverFace));
  EXPECT_FALSE(viewport_overlay_layer_uses_depth_only_face_fill(
      ViewportOverlayLayer::SourceWireDepthStamp));
  EXPECT_FALSE(viewport_overlay_triangle_is_two_sided(
      ViewportOverlayLayer::SourceWireDepthStamp));

  EXPECT_TRUE(viewport_overlay_layer_writes_transient_hover_suppression_mask(
      ViewportOverlayLayer::HoverFace));
  EXPECT_TRUE(viewport_overlay_layer_writes_transient_hover_suppression_mask(
      ViewportOverlayLayer::HoverEdge));
  EXPECT_FALSE(viewport_overlay_layer_writes_transient_hover_suppression_mask(
      ViewportOverlayLayer::SourceWire));
  EXPECT_TRUE(viewport_overlay_layer_is_suppressed_by_transient_hover(
      ViewportOverlayLayer::SelectedEdge));
  EXPECT_FALSE(viewport_overlay_layer_is_suppressed_by_transient_hover(
      ViewportOverlayLayer::SourceWire));
}

TEST(ViewportOverlayPolicyTests,
     ComponentSelectionWiresAndVertexPointsUseDepthTestedState) {
  const ViewportOverlayLine source_wire{
      .layer = ViewportOverlayLayer::SourceWire,
      .source = ViewportOverlaySource::Selection,
  };
  const ViewportOverlayLine object_selected_edge{
      .layer = ViewportOverlayLayer::SelectedEdge,
      .source = ViewportOverlaySource::Selection,
  };
  const ViewportOverlayLine component_selected_edge{
      .layer = ViewportOverlayLayer::SelectedEdge,
      .source = ViewportOverlaySource::ComponentSelection,
  };
  const ViewportOverlayLine component_hover_edge{
      .layer = ViewportOverlayLayer::HoverEdge,
      .source = ViewportOverlaySource::ComponentHover,
  };
  const ViewportOverlayPoint object_selected_vertex{
      .layer = ViewportOverlayLayer::SelectedVertex,
      .source = ViewportOverlaySource::Selection,
  };
  const ViewportOverlayPoint component_selected_vertex{
      .layer = ViewportOverlayLayer::SelectedVertex,
      .source = ViewportOverlaySource::ComponentSelection,
  };

  EXPECT_EQ(viewport_overlay_depth_mode(source_wire),
            ViewportOverlayDepthMode::DrawOnTop);
  EXPECT_EQ(viewport_overlay_depth_mode(object_selected_edge),
            ViewportOverlayDepthMode::DrawOnTop);
  EXPECT_EQ(viewport_overlay_depth_mode(component_selected_edge),
            ViewportOverlayDepthMode::DepthTested);
  EXPECT_EQ(viewport_overlay_depth_mode(component_hover_edge),
            ViewportOverlayDepthMode::DepthTested);
  EXPECT_EQ(viewport_overlay_depth_mode(object_selected_vertex),
            ViewportOverlayDepthMode::DepthTested);
  EXPECT_EQ(viewport_overlay_depth_mode(component_selected_vertex),
            ViewportOverlayDepthMode::DepthTested);
}

TEST(ViewportOverlayPolicyTests, MatchesNativeColorsAndRuntimePointSizes) {
  EXPECT_EQ(
      viewport_overlay_color_for(ViewportOverlayLayer::SelectedVisibleFace,
                                 rgba8(255, 255, 255)),
      kOverlaySelectionFaceColor);
  EXPECT_EQ(viewport_overlay_color_for(ViewportOverlayLayer::SelectedFaceEdge,
                                       rgba8(255, 255, 255)),
            kOverlaySelectionWireColor);
  EXPECT_EQ(viewport_overlay_color_for(ViewportOverlayLayer::WireframeBase,
                                       rgba8(255, 255, 255)),
            rgba8(255, 255, 255));
  EXPECT_EQ(viewport_overlay_color_for(ViewportOverlayLayer::WireframeXray,
                                       rgba8(255, 255, 255)),
            rgba8(255, 255, 255));
  EXPECT_EQ(viewport_overlay_color_for(ViewportOverlayLayer::SourceWire,
                                       kOverlaySelectedVertexColor),
            kOverlaySelectionWireColor);
  EXPECT_EQ(viewport_overlay_color_for(ViewportOverlayLayer::SourceWire,
                                       rgba8(255, 255, 255)),
            kOverlaySourceWireColor);
  EXPECT_EQ(viewport_overlay_color_for(ViewportOverlayLayer::HoverFace,
                                       kOverlayRemoveWireColor),
            kOverlayRemoveFaceColor);
  EXPECT_EQ(viewport_overlay_color_for(ViewportOverlayLayer::HoverEdge,
                                       kOverlayRemoveFaceColor),
            kOverlayRemoveWireColor);
  EXPECT_EQ(viewport_overlay_color_for(ViewportOverlayLayer::GroupBounds,
                                       rgba8(255, 255, 255)),
            kOverlayGroupBoundsColor);
  EXPECT_EQ(viewport_overlay_color_for(ViewportOverlayLayer::ToolPlane,
                                       rgba8(255, 255, 255)),
            kOverlayToolPlaneColor);
  EXPECT_EQ(viewport_overlay_color_for(ViewportOverlayLayer::ToolPlaneLine,
                                       rgba8(255, 255, 255)),
            kOverlayToolPlaneLineColor);
  EXPECT_EQ(viewport_overlay_color_for(ViewportOverlayLayer::ToolHandle,
                                       rgba8(255, 255, 255)),
            kOverlayToolHandleColor);

  EXPECT_FLOAT_EQ(viewport_overlay_line_width_pixels(
                      ViewportOverlayLayer::WireframeBase, 0.0f),
                  1.0f);
  EXPECT_FLOAT_EQ(viewport_overlay_line_width_pixels(
                      ViewportOverlayLayer::WireframeXray, 0.0f),
                  kOverlayWireframeXrayLineWidthPixels);
  EXPECT_FLOAT_EQ(viewport_overlay_line_width_pixels(
                      ViewportOverlayLayer::SourceWire, 3.0f),
                  3.0f);
  EXPECT_FLOAT_EQ(viewport_overlay_line_width_pixels(
                      ViewportOverlayLayer::SelectedEdge, 3.0f),
                  3.0f);
  EXPECT_FLOAT_EQ(
      viewport_overlay_line_width_pixels(ViewportOverlayLayer::HoverEdge, 3.0f),
      3.0f);
  EXPECT_FLOAT_EQ(
      viewport_overlay_line_width_pixels(ViewportOverlayLayer::OpenEdge, 2.0f),
      2.0f);
  EXPECT_FLOAT_EQ(
      viewport_overlay_depth_bias(ViewportOverlayLayer::SourceWire,
                                  ViewportOverlayGeometryKind::Lines),
      0.0f);
  EXPECT_FLOAT_EQ(viewport_overlay_line_width_pixels(
                      ViewportOverlayLayer::GroupBounds, 0.0f),
                  1.5f);
  EXPECT_FLOAT_EQ(viewport_overlay_line_width_pixels(
                      ViewportOverlayLayer::ToolPlaneLine, 1.0f),
                  1.0f);
  EXPECT_FLOAT_EQ(viewport_overlay_line_width_pixels(
                      ViewportOverlayLayer::ToolHandle, 2.0f),
                  2.0f);
  EXPECT_FLOAT_EQ(viewport_overlay_point_size_pixels(
                      ViewportOverlayLayer::UnselectedVertex, 8.0f),
                  8.0f);
  EXPECT_FLOAT_EQ(viewport_overlay_point_size_pixels(
                      ViewportOverlayLayer::SelectedVertex, 9.0f),
                  9.0f);
  EXPECT_FLOAT_EQ(viewport_overlay_point_size_pixels(
                      ViewportOverlayLayer::HoverVertex, 8.0f),
                  8.0f);
  EXPECT_FLOAT_EQ(viewport_overlay_point_size_pixels(
                      ViewportOverlayLayer::ToolHandle, 8.0f),
                  8.0f);
  EXPECT_FLOAT_EQ(
      viewport_overlay_point_size_pixels(ViewportOverlayLayer::PointLightGizmo,
                                         kOverlayPointLightGizmoSizePixels),
      kOverlayPointLightGizmoSizePixels);
  EXPECT_GE(kOverlayPointLightGizmoSizePixels, 64.0f);
  EXPECT_TRUE(
      viewport_overlay_point_has_outline(ViewportOverlayLayer::SelectedVertex));
  EXPECT_FLOAT_EQ(viewport_overlay_point_outline_growth_pixels(
                      ViewportOverlayLayer::SelectedVertex),
                  1.0f);
  EXPECT_TRUE(viewport_overlay_point_is_outline_only(
      ViewportOverlayLayer::SnapTargetVertex));
  EXPECT_FLOAT_EQ(viewport_overlay_point_inner_radius(
                      ViewportOverlayLayer::SnapTargetVertex),
                  0.62f);
}

TEST(ViewportOverlayPolicyTests,
     PersistentObjectSelectionCacheOwnsFaceFillsOnly) {
  EXPECT_FALSE(selected_object_face_fills_enabled_for_selection_count(0u));
  EXPECT_TRUE(selected_object_face_fills_enabled_for_selection_count(1u));
  EXPECT_TRUE(selected_object_face_fills_enabled_for_selection_count(
      kSelectedObjectFaceFillSelectionLimit));
  EXPECT_FALSE(selected_object_face_fills_enabled_for_selection_count(
      kSelectedObjectFaceFillSelectionLimit + 1u));

  const std::string cache_source =
      read_source_file("src/render/overlay/selected_object_overlay_cache.cpp");
  EXPECT_NE(cache_source.find(
                "synced_selection_revision_ == snapshot.selection_revision"),
            std::string::npos);
  EXPECT_EQ(cache_source.find("SelectedObjectLineVertex"), std::string::npos);
  EXPECT_EQ(cache_source.find("update_line_proxy"), std::string::npos);
  EXPECT_EQ(cache_source.find("line_proxy_"), std::string::npos);
  EXPECT_EQ(cache_source.find("setParameter(\"nearPlane\""),
            std::string::npos);
  EXPECT_NE(cache_source.find("destroy_face_entries()"), std::string::npos);
}

TEST(ViewportOverlayPolicyTests,
     NativeViewportKeepsTransientObjectSelectionWireBeforeProjection) {
  const std::string viewport_panel_source =
      read_source_file("src/ui/viewport_panel.cpp");
  const std::string overlay_service_source = read_source_file(
      "src/editor/modeling/services/modeling_session_services.cpp");

  EXPECT_TRUE(source_contains_compact(
      viewport_panel_source,
      "suppress_persistent_object_selection = overlay_summary.mode == "
      "ModelingSelectionMode::Object"));
  EXPECT_NE(viewport_panel_source.find(
                "modeling.selection_overlay_signature(overlay_options)"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find(
                "modeling.selection_overlay_frame(overlay_options)"),
            std::string::npos);
  EXPECT_NE(
      viewport_panel_source.find(
          "refresh_cached_selection_overlay_frame(modeling, document, assets)"),
      std::string::npos);
  EXPECT_NE(viewport_panel_source.find(".scene_revision = document.revision()"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find(".asset_revision = assets.revision()"),
            std::string::npos);
  EXPECT_EQ(viewport_panel_source.find("strip_persistent_object_selection("),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      overlay_service_source,
      "options.suppress_persistent_object_selection && selection_mode == "
      "quader_editor_core::SelectionMode::Object && object.selected"));
  EXPECT_NE(overlay_service_source.find(
                "remove_persistent_object_face_fill_triangles(entry.frame)"),
            std::string::npos);
}

TEST(ViewportOverlayPolicyTests,
     ObjectTransformDragBoundsPreferSceneWorldAabb) {
  const std::string source = read_source_file(
      "src/editor/modeling/services/modeling_session_service.cpp");

  const std::size_t object_mode =
      source.find("session_.selection_mode()");
  ASSERT_NE(object_mode, std::string::npos);
  const std::size_t object_selection =
      source.find("quader_editor_core::SelectionMode::Object", object_mode);
  ASSERT_NE(object_selection, std::string::npos);
  const std::size_t scene_bounds =
      source.find("selected_transform_world_bounds(scene_, assets_)",
                  object_selection);
  ASSERT_NE(scene_bounds, std::string::npos);
  const std::size_t runtime_bounds =
      source.find("session_.selection_bounds()", object_selection);
  ASSERT_NE(runtime_bounds, std::string::npos);
  EXPECT_LT(scene_bounds, runtime_bounds);
}

TEST(ViewportOverlayPolicyTests,
     WireframeCacheUsesSelectionStyleForSelectedMeshes) {
  const std::string cache_source =
      read_source_file("src/render/overlay/scene_wireframe_overlay_cache.cpp");
  EXPECT_NE(cache_source.find("mesh.selected"), std::string::npos);
  EXPECT_TRUE(source_contains_compact(cache_source,
                                      "ViewportOverlayLayer::SelectedEdge : "
                                      "ViewportOverlayLayer::WireframeXray"));
  EXPECT_NE(cache_source.find(
                "kOverlaySelectionWireColor : kOverlayWireframeXrayColor"),
            std::string::npos);
}

TEST(ViewportOverlayPolicyTests,
     RuntimeSelectionOverlayDefaultsAreSemanticRoleTokens) {
  const quader_editor_core::SelectionOverlayStyle style;
  EXPECT_TRUE(quader_editor_core::selection_overlay_style_token_is_semantic(
      style.source_wire_color_abgr));
  EXPECT_TRUE(quader_editor_core::selection_overlay_style_token_is_semantic(
      style.selection_face_color_abgr));
  EXPECT_TRUE(quader_editor_core::selection_overlay_style_token_is_semantic(
      style.selected_edge_color_abgr));
  EXPECT_TRUE(quader_editor_core::selection_overlay_style_token_is_semantic(
      style.hover_selected_line_color_abgr));
  EXPECT_TRUE(quader_editor_core::selection_overlay_style_token_is_semantic(
      style.diagnostic_edge_color_abgr));

  quader_editor_core::SelectionOverlayFrame frame;
  frame.triangles.push_back({
      .layer = quader_editor_core::SelectionOverlayLayer::HoverFace,
      .a = {0.0f, 0.0f, 0.0f},
      .b = {1.0f, 0.0f, 0.0f},
      .c = {0.0f, 1.0f, 0.0f},
      .color_abgr = style.hover_selected_face_color_abgr,
  });
  frame.lines.push_back({
      .layer = quader_editor_core::SelectionOverlayLayer::SourceWire,
      .start = {0.0f, 0.0f, 0.0f},
      .end = {1.0f, 0.0f, 0.0f},
      .color_abgr = style.source_wire_color_abgr,
  });
  frame.points.push_back({
      .layer = quader_editor_core::SelectionOverlayLayer::HoverVertex,
      .position = {0.0f, 0.0f, 0.0f},
      .color_abgr = style.hover_selected_line_color_abgr,
  });

  ModelingObjectRuntimeMap object_registry;
  SceneDocument scene;
  ViewportOverlayFrame projected =
      SelectionOverlayProjection::project_selection_overlay_frame(
          frame, object_registry, scene,
          quader_editor_core::SelectionMode::Object);

  ASSERT_EQ(projected.triangles.size(), 1u);
  EXPECT_EQ(projected.triangles[0].style_role,
            ViewportOverlayStyleRole::RemoveSelectionFace);
  EXPECT_EQ(projected.triangles[0].color, kOverlayRemoveFaceColor);
  ASSERT_EQ(projected.lines.size(), 1u);
  EXPECT_EQ(projected.lines[0].style_role,
            ViewportOverlayStyleRole::SourceWire);
  EXPECT_EQ(projected.lines[0].color, kOverlaySourceWireColor);
  ASSERT_EQ(projected.points.size(), 1u);
  EXPECT_EQ(projected.points[0].style_role,
            ViewportOverlayStyleRole::RemoveSelectionVertex);
  EXPECT_EQ(projected.points[0].color, kOverlayRemoveWireColor);

  const std::string runtime_header =
      read_source_file("src/editor/modeling/runtime/session.hpp");
  const std::string projection = read_source_file(
      "src/editor/projection/selection_overlay_projection.cpp");
  EXPECT_NE(runtime_header.find("SelectionOverlayStyleRole"),
            std::string::npos);
  EXPECT_NE(runtime_header.find("selection_overlay_style_token("),
            std::string::npos);
  EXPECT_EQ(runtime_header.find("0xebffe05cU"), std::string::npos);
  EXPECT_EQ(runtime_header.find("0xff1fadffU"), std::string::npos);
  EXPECT_EQ(runtime_header.find("0xffffe66bU"), std::string::npos);
  EXPECT_NE(projection.find("selection_overlay_style_role_from_token("),
            std::string::npos);
  EXPECT_EQ(
      projection.find("viewport_overlay_is_native_remove_color(color_abgr)"),
      std::string::npos);
  EXPECT_EQ(projection.find(
                "viewport_overlay_is_native_selection_wire_color(color_abgr)"),
            std::string::npos);
}

TEST(SelectionOverlayProjectionTests,
     ComponentModeMarksSourceWireSelectedAndHoverHandles) {
  const quader_editor_core::SelectionOverlayStyle style;
  quader_editor_core::SelectionOverlayFrame frame;
  frame.lines.push_back({
      .layer = quader_editor_core::SelectionOverlayLayer::SourceWire,
      .start = {0.0f, 0.0f, 0.0f},
      .end = {1.0f, 0.0f, 0.0f},
      .color_abgr = style.source_wire_color_abgr,
  });
  frame.lines.push_back({
      .layer = quader_editor_core::SelectionOverlayLayer::SelectedEdge,
      .start = {0.0f, 1.0f, 0.0f},
      .end = {1.0f, 1.0f, 0.0f},
      .color_abgr = style.selected_edge_color_abgr,
  });
  frame.lines.push_back({
      .layer = quader_editor_core::SelectionOverlayLayer::HoverEdge,
      .start = {0.0f, 2.0f, 0.0f},
      .end = {1.0f, 2.0f, 0.0f},
      .color_abgr = style.hover_edge_color_abgr,
  });
  frame.points.push_back({
      .layer = quader_editor_core::SelectionOverlayLayer::SelectedVertex,
      .position = {0.0f, 0.0f, 0.0f},
      .color_abgr = style.selected_handle_color_abgr,
  });

  ModelingObjectRuntimeMap object_registry;
  SceneDocument scene;
  const ViewportOverlayFrame component_projected =
      SelectionOverlayProjection::project_selection_overlay_frame(
          frame, object_registry, scene,
          quader_editor_core::SelectionMode::Edge);
  ASSERT_EQ(component_projected.lines.size(), 3u);
  EXPECT_EQ(component_projected.lines[0].layer,
            ViewportOverlayLayer::SourceWire);
  EXPECT_EQ(component_projected.lines[0].source,
            ViewportOverlaySource::ComponentSelection);
  EXPECT_EQ(component_projected.lines[1].layer,
            ViewportOverlayLayer::SelectedEdge);
  EXPECT_EQ(component_projected.lines[1].source,
            ViewportOverlaySource::ComponentSelection);
  EXPECT_EQ(component_projected.lines[2].source,
            ViewportOverlaySource::ComponentHover);
  ASSERT_EQ(component_projected.points.size(), 1u);
  EXPECT_EQ(component_projected.points[0].source,
            ViewportOverlaySource::ComponentSelection);

  const ViewportOverlayFrame object_projected =
      SelectionOverlayProjection::project_selection_overlay_frame(
          frame, object_registry, scene,
          quader_editor_core::SelectionMode::Object);
  ASSERT_EQ(object_projected.lines.size(), 3u);
  EXPECT_EQ(object_projected.lines[1].source, ViewportOverlaySource::Selection);
  EXPECT_EQ(object_projected.lines[2].source, ViewportOverlaySource::Hover);
}

TEST(ViewportOverlayStyleResolverTests,
     AppliesUserSettingsToRuntimeOverlayFrame) {
  ViewportSettings settings = ViewportSettings::defaults();
  ASSERT_TRUE(settings.set_color(ViewportColorSettingId::SourceWire,
                                 {1.0f, 0.0f, 0.0f, 1.0f}));
  ASSERT_TRUE(settings.set_color(ViewportColorSettingId::SelectionWire,
                                 {0.0f, 1.0f, 0.0f, 1.0f}));
  ASSERT_TRUE(settings.set_color(ViewportColorSettingId::RemoveSelectionWire,
                                 {0.0f, 0.0f, 1.0f, 1.0f}));
  ASSERT_TRUE(settings.set_color(ViewportColorSettingId::RemoveSelectionFace,
                                 {1.0f, 0.0f, 1.0f, 1.0f}));
  ASSERT_TRUE(settings.set_color(ViewportColorSettingId::HoverVertex,
                                 {1.0f, 1.0f, 0.0f, 1.0f}));
  ASSERT_TRUE(settings.set_float_value(
      ViewportFloatSettingId::SourceWireLineSize, 2.0f));
  ASSERT_TRUE(settings.set_float_value(
      ViewportFloatSettingId::SelectionFaceWireLineSize, 5.0f));
  ASSERT_TRUE(settings.set_float_value(
      ViewportFloatSettingId::SelectionEdgeLineSize, 3.0f));
  ASSERT_TRUE(settings.set_float_value(
      ViewportFloatSettingId::HoverWireLineSize, 4.0f));
  ASSERT_TRUE(
      settings.set_float_value(ViewportFloatSettingId::VertexSize, 8.0f));
  ASSERT_TRUE(settings.set_float_value(
      ViewportFloatSettingId::HoverVertexGrowth, 2.0f));
  ASSERT_TRUE(settings.set_float_value(
      ViewportFloatSettingId::VertexOutlineSize, 3.0f));

  ViewportOverlayFrame frame;
  frame.triangles.push_back({
      .layer = ViewportOverlayLayer::HoverFace,
      .color = rgba8(255, 255, 255),
      .style_role = ViewportOverlayStyleRole::RemoveSelectionFace,
  });
  frame.lines.push_back({
      .layer = ViewportOverlayLayer::SourceWire,
      .color = kOverlaySelectionWireColor,
      .style_role = ViewportOverlayStyleRole::SourceWire,
  });
  frame.lines.push_back({
      .layer = ViewportOverlayLayer::SourceWire,
      .color = rgba8(255, 255, 255),
      .style_role = ViewportOverlayStyleRole::SelectedSourceWire,
  });
  frame.lines.push_back({
      .layer = ViewportOverlayLayer::HoverEdge,
      .color = rgba8(255, 255, 255),
      .style_role = ViewportOverlayStyleRole::RemoveSelectionWire,
  });
  frame.lines.push_back({
      .layer = ViewportOverlayLayer::SelectedEdge,
      .color = rgba8(255, 255, 255),
      .style_role = ViewportOverlayStyleRole::SelectionWire,
  });
  frame.lines.push_back({
      .layer = ViewportOverlayLayer::GroupBounds,
      .color = rgba8(255, 255, 255),
      .width_pixels = 1.5f,
      .style_role = ViewportOverlayStyleRole::GroupBounds,
  });
  frame.points.push_back({
      .layer = ViewportOverlayLayer::HoverVertex,
      .color = rgba8(255, 255, 255),
      .size_pixels = 6.0f,
      .style_role = ViewportOverlayStyleRole::HoverVertex,
  });
  frame.points.push_back({.layer = ViewportOverlayLayer::SnapTargetVertex,
                          .color = rgba8(255, 0, 0),
                          .size_pixels = 14.0f});

  ViewportOverlayStyleResolver::apply(frame, settings);

  ASSERT_EQ(frame.triangles.size(), 1u);
  ASSERT_EQ(frame.lines.size(), 5u);
  ASSERT_EQ(frame.points.size(), 2u);
  EXPECT_EQ(frame.triangles[0].color, rgba8(255, 0, 255));
  EXPECT_EQ(frame.lines[0].color, rgba8(255, 0, 0));
  EXPECT_FLOAT_EQ(frame.lines[0].width_pixels, 2.0f);
  EXPECT_EQ(frame.lines[1].color, rgba8(0, 255, 0));
  EXPECT_FLOAT_EQ(frame.lines[1].width_pixels, 5.0f);
  EXPECT_EQ(frame.lines[2].color, rgba8(0, 0, 255));
  EXPECT_FLOAT_EQ(frame.lines[2].width_pixels, 4.0f);
  EXPECT_EQ(frame.lines[3].color, rgba8(0, 255, 0));
  EXPECT_FLOAT_EQ(frame.lines[3].width_pixels, 3.0f);
  EXPECT_EQ(frame.lines[4].color, kOverlayGroupBoundsColor);
  EXPECT_FLOAT_EQ(frame.lines[4].width_pixels, 1.5f);
  EXPECT_EQ(frame.points[0].color, rgba8(255, 255, 0));
  EXPECT_FLOAT_EQ(frame.points[0].size_pixels, 10.0f);
  ASSERT_TRUE(frame.points[0].outline_growth_pixels.has_value());
  EXPECT_FLOAT_EQ(*frame.points[0].outline_growth_pixels, 3.0f);
  EXPECT_EQ(frame.points[1].color, rgba8(255, 0, 0));
  EXPECT_FLOAT_EQ(frame.points[1].size_pixels, 14.0f);
  ASSERT_TRUE(frame.points[1].outline_growth_pixels.has_value());
  EXPECT_FLOAT_EQ(*frame.points[1].outline_growth_pixels, 3.0f);
}

TEST(ViewportOverlayStyleResolverTests, SupportsSelectionModeScopedOverrides) {
  ViewportSettings settings = ViewportSettings::defaults();
  ASSERT_TRUE(settings.set_color(ViewportColorSettingId::SourceWire,
                                 {1.0f, 0.0f, 0.0f, 1.0f}));

  ViewportOverlayFrame frame;
  frame.lines.push_back({
      .layer = ViewportOverlayLayer::SourceWire,
      .color = rgba8(255, 255, 255),
      .style_role = ViewportOverlayStyleRole::SourceWire,
  });

  ViewportOverlayStyleResolver::apply(
      frame, settings,
      ViewportOverlayStyleContext{
          .selection_mode = ViewportOverlaySelectionContext::Edge,
          .overrides =
              {
                  {.role = ViewportOverlayStyleRole::SourceWire,
                   .selection_mode = ViewportOverlaySelectionContext::Face,
                   .color = rgba8(128, 0, 255)},
                  {.role = ViewportOverlayStyleRole::SourceWire,
                   .selection_mode = ViewportOverlaySelectionContext::Edge,
                   .color = rgba8(255, 0, 128)},
              },
      });

  ASSERT_EQ(frame.lines.size(), 1u);
  EXPECT_EQ(frame.lines[0].color, rgba8(255, 0, 128));
}

TEST(ViewportOverlayStyleResolverTests,
     PrioritizesDiagnosticsThenExternalOverrides) {
  ViewportSettings settings = ViewportSettings::defaults();
  ASSERT_TRUE(settings.set_color(ViewportColorSettingId::SelectionWire,
                                 {0.0f, 1.0f, 0.0f, 1.0f}));
  ASSERT_TRUE(settings.set_color(ViewportColorSettingId::DiagnosticEdge,
                                 {0.25f, 0.0f, 1.0f, 1.0f}));
  ASSERT_TRUE(settings.set_float_value(
      ViewportFloatSettingId::DiagnosticEdgeLineSize, 6.0f));

  ViewportOverlayFrame frame;
  frame.lines.push_back({
      .layer = ViewportOverlayLayer::DiagnosticEdge,
      .color = rgba8(0, 255, 0),
      .width_pixels = 1.0f,
  });

  ViewportOverlayStyleResolver::apply(frame, settings);

  ASSERT_EQ(frame.lines.size(), 1u);
  EXPECT_EQ(frame.lines[0].color, rgba8(64, 0, 255));
  EXPECT_FLOAT_EQ(frame.lines[0].width_pixels, 6.0f);

  ViewportOverlayStyleResolver::apply(
      frame, settings,
      ViewportOverlayStyleContext{
          .selection_mode = ViewportOverlaySelectionContext::Edge,
          .overrides =
              {
                  {.role = ViewportOverlayStyleRole::DiagnosticEdge,
                   .selection_mode = ViewportOverlaySelectionContext::Face,
                   .color = rgba8(128, 0, 255),
                   .line_width_pixels = 3.0f},
                  {.role = ViewportOverlayStyleRole::DiagnosticEdge,
                   .selection_mode = ViewportOverlaySelectionContext::Edge,
                   .color = rgba8(255, 0, 128),
                   .line_width_pixels = 2.0f},
              },
      });

  EXPECT_EQ(frame.lines[0].color, rgba8(255, 0, 128));
  EXPECT_FLOAT_EQ(frame.lines[0].width_pixels, 2.0f);
}

TEST(PolygonOperationRegistryTests, MetadataComesFromDescriptorOwner) {
  const std::string descriptor =
      read_source_file("src/mesh/polygon/poly_operation_descriptor.hpp");
  const std::string registry = read_source_file(
      "src/mesh/polygon/operations/poly_operation_registry.cpp");

  EXPECT_NE(descriptor.find("kPolyOperationDescriptors"), std::string::npos);
  EXPECT_NE(registry.find("poly_operation_descriptors()"), std::string::npos);
  EXPECT_EQ(registry.find("std::array<PolyOperationMetadata"),
            std::string::npos);
  EXPECT_EQ(registry.find("k_poly_operations"), std::string::npos);

  const std::filesystem::path source_root =
      std::filesystem::path(QUADER_SOURCE_DIR);
  const std::filesystem::path operations_dir =
      source_root / "src/mesh/polygon/operations";
  ASSERT_TRUE(std::filesystem::exists(operations_dir));
  for (const std::filesystem::directory_entry &entry :
       std::filesystem::directory_iterator(operations_dir)) {
    if (entry.path().extension() != ".cpp" ||
        entry.path().filename() == "poly_operation_registry.cpp") {
      continue;
    }
    const std::string relative_path =
        std::filesystem::relative(entry.path(), source_root).generic_string();
    const std::string source = read_source_file(relative_path);
    if (source.find("PolyOperation") == std::string::npos) {
      continue;
    }
    SCOPED_TRACE(relative_path);
    EXPECT_FALSE(metadata_method_returns_literal(
        source, "std::string_view id() const override"));
    EXPECT_FALSE(metadata_method_returns_literal(
        source, "std::string_view label() const override"));
  }
}

TEST(ModelingSessionServiceOverlayTests, MapsRuntimeOverlayLayersExplicitly) {
  const std::string source = read_source_file(
      "src/editor/projection/selection_overlay_projection.cpp");
  const std::string modeling_service = read_source_file(
      "src/editor/modeling/services/modeling_session_service.cpp");
  const std::string overlay_service = read_source_file(
      "src/editor/modeling/services/modeling_session_services.cpp");

  EXPECT_EQ(
      source.find("static_cast<ViewportOverlayLayer>(static_cast<int>(layer))"),
      std::string::npos);
  EXPECT_NE(source.find(
                "case quader_editor_core::SelectionOverlayLayer::SourceWire:"),
            std::string::npos);
  EXPECT_NE(source.find(
                "case "
                "quader_editor_core::SelectionOverlayLayer::SnapTargetVertex:"),
            std::string::npos);
  EXPECT_NE(
      source.find(
          "case quader_editor_core::SelectionOverlayLayer::DiagnosticEdge:"),
      std::string::npos);
  EXPECT_NE(source.find("case "
                        "quader_editor_core::SelectionOverlayLayer::"
                        "SourceWireDepthStamp:"),
            std::string::npos);
  EXPECT_NE(source.find("line_style_role"), std::string::npos);
  EXPECT_NE(source.find("ViewportOverlayStyleRole::SelectedSourceWire"),
            std::string::npos);
  EXPECT_NE(source.find("append_projected_overlay_lines"), std::string::npos);
  EXPECT_EQ(modeling_service.find("runtime_.knife_state().preview_lines"),
            std::string::npos);
  EXPECT_NE(overlay_service.find("runtime.knife_state().preview_lines"),
            std::string::npos);
  EXPECT_NE(overlay_service.find("ViewportOverlayLayer::ToolPreviewLine"),
            std::string::npos);
  EXPECT_EQ(overlay_service.find("knife.preview_lines.begin()"),
            std::string::npos);
}

TEST(ViewportCameraAdapterTests,
     BuildsOverlayAndModelingCamerasFromSharedBasis) {
  ViewportCameraController camera;
  camera.set_clip_planes(0.01f, 250.0f);
  const ViewportOverlayCamera overlay =
      ViewportCameraAdapter::overlay_camera(camera, {0.0f, 240.0f});
  const ModelingViewportCamera modeling =
      ViewportCameraAdapter::modeling_camera(camera, {640.0f, 360.0f});

  EXPECT_EQ(overlay.projection, ViewportOverlayProjection::Perspective);
  EXPECT_FALSE(modeling.orthographic);
  EXPECT_FLOAT_EQ(overlay.viewport_size.x, 1.0f);
  EXPECT_FLOAT_EQ(overlay.viewport_size.y, 240.0f);
  EXPECT_FLOAT_EQ(overlay.near_plane, 0.01f);
  EXPECT_FLOAT_EQ(overlay.far_plane, 250.0f);
  expect_vec3_near(modeling.origin, overlay.eye);
  expect_vec3_near(modeling.x_axis, overlay.right);
  expect_vec3_near(modeling.y_axis, overlay.up);
  expect_vec3_near(modeling.z_axis, overlay.forward * -1.0f);
  EXPECT_NEAR(length(overlay.forward), 1.0f, 0.001f);
  EXPECT_NEAR(length(overlay.right), 1.0f, 0.001f);
  EXPECT_NEAR(length(overlay.up), 1.0f, 0.001f);
}

TEST(ViewportGridPlacementPolicyTests,
     ResolvesPerspectiveAndOrthographicPlanesWithoutFilament) {
  ViewportCameraController camera;
  camera.set_viewport_size(200.0f, 100.0f);

  const ViewportGridPlacement perspective =
      ViewportGridPlacementPolicy::placement(camera, 200u, 100u);
  EXPECT_EQ(perspective.plane, ViewportGridPlane::XZ);
  EXPECT_EQ(perspective.rotation, ViewportGridRotation::Identity);
  EXPECT_FLOAT_EQ(perspective.plane_size.x, kViewportGridDefaultPlaneSize);
  EXPECT_FLOAT_EQ(perspective.plane_size.y, kViewportGridDefaultPlaneSize);
  expect_vec3_near(perspective.axes.u_axis, {1.0f, 0.0f, 0.0f});
  expect_vec3_near(perspective.axes.v_axis, {0.0f, 0.0f, -1.0f});
  EXPECT_EQ(perspective.axes.u_color, ViewportColorSettingId::GridXAxis);
  EXPECT_EQ(perspective.axes.v_color, ViewportColorSettingId::GridZAxis);

  camera.view_axis(ViewportViewAxis::X, 1);
  const ViewportGridPlacement side =
      ViewportGridPlacementPolicy::placement(camera, 200u, 100u);
  const float ortho_plane_height =
      camera.state().orthographic_size * kViewportGridOrthographicMargin;
  EXPECT_EQ(side.plane, ViewportGridPlane::YZ);
  EXPECT_EQ(side.rotation, ViewportGridRotation::NegativeHalfPiAroundZ);
  EXPECT_FLOAT_EQ(side.origin.x, kViewportGridPlaneOffset);
  EXPECT_FLOAT_EQ(side.plane_size.x, ortho_plane_height);
  EXPECT_FLOAT_EQ(side.plane_size.y, ortho_plane_height * 2.0f);
  expect_vec3_near(side.axes.u_axis, {0.0f, 0.0f, -1.0f});
  expect_vec3_near(side.axes.v_axis, {0.0f, 1.0f, 0.0f});
  EXPECT_EQ(side.axes.u_color, ViewportColorSettingId::GridZAxis);
  EXPECT_EQ(side.axes.v_color, ViewportColorSettingId::GridYAxis);

  camera.view_axis(ViewportViewAxis::Z, 1);
  const ViewportGridPlacement front =
      ViewportGridPlacementPolicy::placement(camera, 200u, 100u);
  EXPECT_EQ(front.plane, ViewportGridPlane::XY);
  EXPECT_EQ(front.rotation, ViewportGridRotation::PositiveHalfPiAroundX);
  EXPECT_FLOAT_EQ(front.origin.z, kViewportGridPlaneOffset);
  EXPECT_FLOAT_EQ(front.plane_size.x, camera.state().orthographic_size *
                                          kViewportGridOrthographicMargin *
                                          2.0f);
  EXPECT_FLOAT_EQ(front.plane_size.y, camera.state().orthographic_size *
                                          kViewportGridOrthographicMargin);
  expect_vec3_near(front.axes.u_axis, {1.0f, 0.0f, 0.0f});
  expect_vec3_near(front.axes.v_axis, {0.0f, 1.0f, 0.0f});
}

TEST(SceneRenderBridgeTests, UsesDepthStampedFaceFillForOverlayTriangles) {
  const std::string bridge_source =
      read_source_file("src/render/scene_render_bridge.cpp");
  const std::string builder_source =
      read_source_file("src/render/overlay/overlay_renderable_builder.cpp");
  const std::string proxy_factory_source =
      read_source_file("src/render/overlay/overlay_proxy_factory.cpp");
  const std::string tool_preview_cache_source =
      read_source_file("src/render/tool_preview_renderable_cache.cpp");

  EXPECT_NE(builder_source.find(
                "viewport_overlay_layer_uses_depth_stamped_face_fill"),
            std::string::npos);
  EXPECT_NE(proxy_factory_source.find("setColorWrite(state.color_write)"),
            std::string::npos);
  EXPECT_NE(proxy_factory_source.find("setDepthWrite(state.depth_write)"),
            std::string::npos);
  EXPECT_NE(proxy_factory_source.find(
                "setDepthFunc(to_filament_depth_func(state.depth_func))"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      proxy_factory_source, "setTransparencyMode(to_filament_transparency_mode("
                            "state.transparency_mode))"));
  EXPECT_TRUE(source_contains_compact(
      proxy_factory_source, "setPolygonOffset(state.polygon_offset_scale, "
                            "state.polygon_offset_constant)"));
  EXPECT_NE(proxy_factory_source.find("DepthFunc::GE"), std::string::npos);
  EXPECT_NE(proxy_factory_source.find("DepthFunc::E"), std::string::npos);
  EXPECT_NE(builder_source.find("OverlayCullingMode::Back"), std::string::npos);
  EXPECT_NE(builder_source.find("OverlayTransparencyMode::TwoPassesOneSide"),
            std::string::npos);
  EXPECT_NE(
      builder_source.find("viewport_overlay_triangle_is_two_sided(layer)"),
      std::string::npos);
  EXPECT_NE(builder_source.find("OverlayCullingMode::None"), std::string::npos);
  EXPECT_NE(builder_source.find("OverlayTransparencyMode::TwoPassesTwoSides"),
            std::string::npos);
  EXPECT_NE(
      bridge_source.find("std::vector<RenderToolPreviewMeshRecord> meshes"),
      std::string::npos);
  EXPECT_NE(bridge_source.find("tool_preview_cache_.set_meshes(meshes)"),
            std::string::npos);
  EXPECT_NE(tool_preview_cache_source.find("mesh_proxies_"), std::string::npos);
  EXPECT_NE(tool_preview_cache_source.find(
                "void ToolPreviewRenderableCache::clear()"),
            std::string::npos);
}

TEST(SceneRenderBridgeTests, UsesQuadMaterialsForSelectionLineAndPointOverlay) {
  const std::string proxy_factory_source =
      read_source_file("src/render/overlay/overlay_proxy_factory.cpp");
  const std::string line_builder_source =
      read_source_file("src/render/overlay/overlay_line_mesh_builder.cpp");
  const std::string point_builder_source =
      read_source_file("src/render/overlay/overlay_point_mesh_builder.cpp");
  const std::string renderable_builder_source =
      read_source_file("src/render/overlay/overlay_renderable_builder.hpp");
  const std::string renderable_builder_impl =
      read_source_file("src/render/overlay/overlay_renderable_builder.cpp");
  const std::string projector_source =
      read_source_file("src/render/overlay/overlay_camera_projector.cpp");

  EXPECT_NE(line_builder_source.find("append_line_quad"), std::string::npos);
  EXPECT_NE(point_builder_source.find("append_point_quad"), std::string::npos);
  EXPECT_NE(proxy_factory_source.find("material.createInstance()"),
            std::string::npos);
  EXPECT_NE(proxy_factory_source.find("*overlay_line_material_"),
            std::string::npos);
  EXPECT_NE(proxy_factory_source.find("*overlay_point_material_"),
            std::string::npos);
  EXPECT_NE(proxy_factory_source.find("*overlay_icon_material_"),
            std::string::npos);
  EXPECT_NE(proxy_factory_source.find("setParameter(\"iconTexture\""),
            std::string::npos);
  const std::size_t point_light_icon_path =
      proxy_factory_source.find("resource_root / \"icons\" / \"point_light.png\"");
  ASSERT_NE(point_light_icon_path, std::string::npos);
  const std::size_t point_light_icon_load_start =
      proxy_factory_source.rfind("load_png_texture", point_light_icon_path);
  ASSERT_NE(point_light_icon_load_start, std::string::npos);
  const std::size_t point_light_icon_load_end =
      proxy_factory_source.find("});", point_light_icon_path);
  ASSERT_NE(point_light_icon_load_end, std::string::npos);
  const std::string point_light_icon_load = proxy_factory_source.substr(
      point_light_icon_load_start,
      point_light_icon_load_end - point_light_icon_load_start);
  EXPECT_NE(point_light_icon_load.find(".flip_y = false"), std::string::npos);
  EXPECT_EQ(point_light_icon_load.find(".flip_y = true"), std::string::npos);
  EXPECT_NE(proxy_factory_source.find("OverlayRenderableProxyKind::Icon"),
            std::string::npos);
  EXPECT_NE(proxy_factory_source.find("filament::VertexAttribute::CUSTOM0"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      proxy_factory_source,
      "AttributeType::FLOAT4, offsetof(OverlayLineVertex, position)"));
  EXPECT_TRUE(source_contains_compact(
      proxy_factory_source,
      "AttributeType::FLOAT4, offsetof(OverlayPointVertex, position)"));
  EXPECT_NE(projector_source.find("OverlayProjectedPoint project"),
            std::string::npos);
  EXPECT_NE(projector_source.find("near_plane_distance(a)"), std::string::npos);
  EXPECT_NE(proxy_factory_source.find(
                "setParameter(\"lineWidth\", batch.width_pixels)"),
            std::string::npos);
  EXPECT_EQ(proxy_factory_source.find(
                "setParameter(\"point_size\", point_size_pixels)"),
            std::string::npos);
  EXPECT_EQ(proxy_factory_source.find("setParameter(\"nearPlane\""),
            std::string::npos);
  EXPECT_NE(renderable_builder_source.find("LineBatchKey"), std::string::npos);
  EXPECT_NE(renderable_builder_source.find("IconBatchKey"), std::string::npos);
  EXPECT_NE(
      renderable_builder_impl.find("OverlayPointMeshBuilder::build_icons"),
      std::string::npos);
  EXPECT_EQ(proxy_factory_source.find("PrimitiveType::LINES"),
            std::string::npos);
  EXPECT_EQ(proxy_factory_source.find("PrimitiveType::POINTS"),
            std::string::npos);
}

TEST(SceneRenderBridgeTests, BindsNormalsAndBuildsPrimitiveSpans) {
  const std::string source =
      read_source_file("src/render/mesh_renderable_factory.cpp");

  EXPECT_NE(source.find("tangent_frame_from_normal(vertex.normal)"),
            std::string::npos);
  EXPECT_NE(source.find("filament::VertexAttribute::TANGENTS"),
            std::string::npos);
  EXPECT_NE(source.find("coalesced_render_mesh_primitive_spans(mesh)"),
            std::string::npos);
  EXPECT_NE(source.find("primitive.index_offset"), std::string::npos);
  EXPECT_NE(source.find("primitive.index_count"), std::string::npos);
  EXPECT_NE(source.find("primitive.material_slot"), std::string::npos);
  EXPECT_NE(source.find("material->material_for_slot(primitive.material_slot)"),
            std::string::npos);
}

TEST(SceneRenderBridgeTests, UsesRenderSceneSnapshotsForIncrementalMeshSync) {
  const std::string snapshot_header =
      read_source_file("src/render/render_scene_snapshot.hpp");
  const std::string sync_header =
      read_source_file("src/render/scene_render_sync_service.hpp");
  const std::string sync_source =
      read_source_file("src/render/scene_render_sync_service.cpp");
  const std::string cache_source =
      read_source_file("src/render/mesh_renderable_cache.cpp");
  const std::string mesh_record = quader::test::guardrail::source_slice(
      snapshot_header, "struct RenderSceneMeshRecord",
      "struct RenderSceneLightRecord");
  const std::string light_record = quader::test::guardrail::source_slice(
      snapshot_header, "struct RenderSceneLightRecord",
      "struct RenderSceneSnapshotDirtyState");

  EXPECT_NE(snapshot_header.find("struct RenderSceneSnapshot"),
            std::string::npos);
  EXPECT_NE(snapshot_header.find("RenderSceneMeshRecord"), std::string::npos);
  EXPECT_NE(snapshot_header.find("RenderSceneLightRecord"), std::string::npos);
  EXPECT_NE(snapshot_header.find("scene_revision"), std::string::npos);
  EXPECT_NE(snapshot_header.find("asset_revision"), std::string::npos);
  EXPECT_NE(sync_header.find("sync(const RenderSceneSnapshot"),
            std::string::npos);
  EXPECT_EQ(sync_header.find("sync(const SceneDocument"), std::string::npos);
  EXPECT_EQ(sync_header.find("sync(const AssetRegistry"), std::string::npos);

  EXPECT_NE(sync_source.find("synced_asset_revision_"), std::string::npos);
  EXPECT_NE(sync_source.find("render_asset_revisions_changed"),
            std::string::npos);
  EXPECT_NE(sync_source.find("snapshot.scene_revision"), std::string::npos);
  EXPECT_NE(sync_source.find("snapshot.asset_revision"), std::string::npos);
  EXPECT_EQ(
      sync_source.find("build_scene_render_extraction_plan(document, assets,"),
      std::string::npos);
  EXPECT_EQ(sync_source.find("const SceneDocument"), std::string::npos);
  EXPECT_EQ(sync_source.find("const AssetRegistry"), std::string::npos);
  EXPECT_NE(sync_source.find("mesh_cache_->needs_rebuild"), std::string::npos);
  EXPECT_EQ(mesh_record.find("const MeshAsset *"), std::string::npos);
  EXPECT_EQ(mesh_record.find("const MeshComponent *"), std::string::npos);
  EXPECT_EQ(mesh_record.find("const MaterialComponent *"), std::string::npos);
  EXPECT_EQ(light_record.find("const LightComponent *"), std::string::npos);
  EXPECT_NE(cache_source.find(
                "existing->second.mesh_revision != mesh.mesh.mesh_revision"),
            std::string::npos);
  EXPECT_NE(cache_source.find(
                "existing->second.material_revision != mesh.material_revision"),
            std::string::npos);
  EXPECT_NE(
      cache_source.find(
          "factory_->update_transform(existing->second, mesh.world_transform)"),
      std::string::npos);
  const std::size_t change_start = sync_source.find(
      "bool SceneRenderSyncService::render_asset_revisions_changed");
  ASSERT_NE(change_start, std::string::npos);
  const std::size_t rebuild_start =
      sync_source.find("void SceneRenderSyncService::rebuild(");
  ASSERT_NE(rebuild_start, std::string::npos);
  const std::string change_source =
      sync_source.substr(change_start, rebuild_start - change_start);
  EXPECT_EQ(change_source.find("document.entities()"), std::string::npos);
  EXPECT_EQ(change_source.find("document.is_effectively_visible"),
            std::string::npos);
  EXPECT_EQ(change_source.find("assets.mesh("), std::string::npos);
  EXPECT_EQ(change_source.find("material_binding_->material_revision"),
            std::string::npos);
  const std::string rebuild_source = sync_source.substr(rebuild_start);
  EXPECT_NE(rebuild_source.find(
                "mesh_cache_->remove_except(snapshot.live_mesh_entities)"),
            std::string::npos);
  EXPECT_EQ(rebuild_source.find("mesh_proxies_.clear()"), std::string::npos);
}

TEST(ViewportPaneOverlayCacheTests,
     SceneAndAssetRevisionsInvalidateSelectionOverlayFrame) {
  ViewportPaneOverlayCache cache;
  const ViewportPaneOverlayCacheKey key{
      .content_revision = 1u,
      .selection_revision = 2u,
      .scene_revision = 3u,
      .asset_revision = 4u,
      .overlay_signature = 5u,
      .selection_mode = ModelingSelectionMode::Object,
  };
  cache.update({}, key);

  EXPECT_EQ(cache.refresh_reason(key),
            ViewportPaneOverlayCacheRefreshReason::None);

  ViewportPaneOverlayCacheKey scene_changed = key;
  scene_changed.scene_revision = key.scene_revision + 1u;
  EXPECT_EQ(cache.refresh_reason(scene_changed),
            ViewportPaneOverlayCacheRefreshReason::SceneRevision);

  ViewportPaneOverlayCacheKey asset_changed = key;
  asset_changed.asset_revision = key.asset_revision + 1u;
  EXPECT_EQ(cache.refresh_reason(asset_changed),
            ViewportPaneOverlayCacheRefreshReason::AssetRevision);
}

TEST(SceneRenderBridgeTests, UsesAnisotropicSamplingForMeshTextures) {
  const std::string source =
      read_source_file("src/render/material_binding_service.cpp");

  EXPECT_NE(source.find("kMeshTextureAnisotropy"), std::string::npos);
  EXPECT_NE(source.find("sampler.setAnisotropy(kMeshTextureAnisotropy)"),
            std::string::npos);
  EXPECT_NE(
      source.find("filament::TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR"),
      std::string::npos);
}

TEST(RenderSystemTests, ShadedViewportUsesClearColorWithoutSkybox) {
  const std::string source = read_source_file("src/render/render_system.cpp");

  EXPECT_NE(source.find("renderer_->setClearOptions(clear)"),
            std::string::npos);
  EXPECT_NE(source.find("clear.discard = false"), std::string::npos);
  EXPECT_NE(source.find("clear.clearColor = {environment.clear_color.x"),
            std::string::npos);
  EXPECT_EQ(source.find("scene_->setSkybox(skybox_)"), std::string::npos);
  EXPECT_EQ(source.find("Skybox::Builder()"), std::string::npos);
}

TEST(RenderSystemTests,
     ExposesCompletedFramePerformanceStatsFromRendererHistory) {
  const std::string header = read_source_file("src/render/render_system.hpp");
  const std::string source = read_source_file("src/render/render_system.cpp");

  EXPECT_NE(header.find(
                "RenderFramePerformanceStats frame_performance_stats() const"),
            std::string::npos);
  EXPECT_NE(header.find("RenderFramePerformanceStats frame_performance_stats_"),
            std::string::npos);
  EXPECT_NE(header.find("frame_rate_sample_begin_"), std::string::npos);
  EXPECT_NE(header.find("frame_rate_sample_count_"), std::string::npos);
  EXPECT_NE(source.find("frame_cpu_begin_ = std::chrono::steady_clock::now()"),
            std::string::npos);
  EXPECT_NE(source.find("std::chrono::duration<double, std::milli>"),
            std::string::npos);
  EXPECT_NE(source.find("kFrameRateSampleSeconds"), std::string::npos);
  EXPECT_NE(source.find("frames_per_second"), std::string::npos);
  EXPECT_NE(source.find("renderer_->getFrameInfoHistory(history_size)"),
            std::string::npos);
  EXPECT_NE(source.find("renderer_->getMaxFrameHistorySize()"),
            std::string::npos);
  EXPECT_NE(source.find("info.denoisedGpuFrameDuration"), std::string::npos);
  EXPECT_NE(source.find("info.gpuFrameDuration"), std::string::npos);
  EXPECT_NE(source.find("filament::Renderer::FrameInfo::INVALID"),
            std::string::npos);
  EXPECT_NE(source.find("filament::Renderer::FrameInfo::PENDING"),
            std::string::npos);
}

TEST(ViewportRendererTests,
     EnvironmentClearColorAndRenderedBackgroundPathsStaySceneOwned) {
  const std::string renderer =
      read_source_file("src/render/viewport_renderer.cpp");
  const std::string renderer_header =
      read_source_file("src/render/viewport_renderer.hpp");
  const std::string environment_library =
      read_source_file("src/render/filament_environment_library.cpp");

  EXPECT_NE(renderer.find(
                "viewport_clear_color(settings.environment, rendered_mode)"),
            std::string::npos);
  EXPECT_NE(renderer.find("if (rendered_mode)"), std::string::npos);
  EXPECT_NE(renderer.find("environment.clear_color.x"), std::string::npos);
  EXPECT_EQ(renderer.find("ViewportColorSettingId::Background"),
            std::string::npos);
  EXPECT_EQ(renderer.find("settings.color(ViewportColorSettingId"),
            std::string::npos);
  EXPECT_EQ(renderer_header.find("camera_near_plane_"), std::string::npos);
  EXPECT_EQ(renderer_header.find("camera_far_plane_"), std::string::npos);
  EXPECT_NE(renderer.find("controller.state().near_plane"), std::string::npos);
  EXPECT_NE(renderer.find("controller.state().far_plane"), std::string::npos);
  EXPECT_NE(
      environment_library.find(
          "settings.show_background && settings.background_intensity > 0.0f"),
      std::string::npos);
}

TEST(RenderSystemTests, InitializeUnwindsPartialFilamentStateOnFailure) {
  const std::string header = read_source_file("src/render/render_system.hpp");
  const std::string source = read_source_file("src/render/render_system.cpp");

  EXPECT_NE(header.find("[[nodiscard]] bool initialized() const"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      source, "shutdown(); LOG_INFO(\"Initializing Filament renderer"));
  EXPECT_NE(source.find("const auto fail = [&](std::string_view message)"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(source, "shutdown(); return false;"));
  EXPECT_NE(source.find("if (!swap_chain_)"), std::string::npos);
  EXPECT_NE(source.find("if (!renderer_)"), std::string::npos);
  EXPECT_NE(source.find("if (!scene_)"), std::string::npos);
  EXPECT_NE(source.find(
                "if (!material_library_.initialize(*engine_, executable_dir))"),
            std::string::npos);
  EXPECT_NE(source.find("return begin_frame_with_swap_chain(swap_chain_)"),
            std::string::npos);
  EXPECT_NE(source.find("if (!renderer_->beginFrame(swap_chain))"),
            std::string::npos);
}

TEST(RenderProxyLifecycleTests, RenderableProxyHandlesAreMoveOnly) {
  static_assert(!std::is_copy_constructible_v<MeshRenderableProxy>);
  static_assert(!std::is_copy_assignable_v<MeshRenderableProxy>);
  static_assert(std::is_move_constructible_v<MeshRenderableProxy>);
  static_assert(!std::is_copy_constructible_v<OverlayRenderableProxy>);
  static_assert(!std::is_copy_assignable_v<OverlayRenderableProxy>);
  static_assert(std::is_move_constructible_v<OverlayRenderableProxy>);

  const std::string mesh_proxy_header =
      read_source_file("src/render/mesh_renderable_proxy.hpp");
  const std::string mesh_factory_source =
      read_source_file("src/render/mesh_renderable_factory.cpp");
  const std::string overlay_proxy_header =
      read_source_file("src/render/overlay/overlay_renderable_proxy.hpp");
  const std::string overlay_factory_source =
      read_source_file("src/render/overlay/overlay_proxy_factory.cpp");
  const std::string light_cache_header =
      read_source_file("src/render/light_renderable_cache.hpp");
  const std::string light_cache_source =
      read_source_file("src/render/light_renderable_cache.cpp");

  EXPECT_NE(mesh_proxy_header.find(
                "MeshRenderableProxy(const MeshRenderableProxy&) = delete"),
            std::string::npos);
  EXPECT_NE(mesh_proxy_header.find(
                "MeshRenderableProxy(MeshRenderableProxy&& other) noexcept"),
            std::string::npos);
  EXPECT_NE(
      mesh_proxy_header.find("std::exchange(other.vertex_buffer, nullptr)"),
      std::string::npos);
  EXPECT_NE(mesh_factory_source.find("class MeshRenderableCreationGuard"),
            std::string::npos);
  EXPECT_NE(mesh_factory_source.find("destroy_mesh_renderable_proxy_resources"),
            std::string::npos);
  EXPECT_NE(mesh_factory_source.find("creation_guard.release()"),
            std::string::npos);
  EXPECT_NE(
      overlay_proxy_header.find(
          "OverlayRenderableProxy(const OverlayRenderableProxy&) = delete"),
      std::string::npos);
  EXPECT_NE(
      overlay_proxy_header.find(
          "OverlayRenderableProxy(OverlayRenderableProxy &&other) noexcept"),
      std::string::npos);
  EXPECT_NE(overlay_factory_source.find("class OverlayProxyCreationGuard"),
            std::string::npos);
  EXPECT_NE(overlay_factory_source.find("destroy_overlay_proxy_resources"),
            std::string::npos);
  EXPECT_NE(overlay_factory_source.find("creation_guard.release()"),
            std::string::npos);
  EXPECT_NE(overlay_factory_source.find("proxies.push_back(std::move(proxy))"),
            std::string::npos);
  EXPECT_NE(overlay_factory_source.find("void append_overlay_proxy("),
            std::string::npos);
  EXPECT_EQ(count_occurrences(overlay_factory_source,
                              "filament::RenderableManager::Builder(1)"),
            1u);
  EXPECT_EQ(
      count_occurrences(overlay_factory_source, "scene.addEntity(entity)"), 1u);
  EXPECT_EQ(overlay_factory_source.find("scene_->addEntity(entity)"),
            std::string::npos);
  EXPECT_EQ(count_occurrences(overlay_factory_source,
                              "proxies.push_back(std::move(proxy))"),
            1u);
  EXPECT_NE(light_cache_header.find("LightProxy(const LightProxy&) = delete"),
            std::string::npos);
  EXPECT_NE(light_cache_header.find("LightProxy(LightProxy &&other) noexcept"),
            std::string::npos);
  EXPECT_NE(light_cache_source.find(
                "light_proxies_.emplace(light.entity_id, std::move(proxy))"),
            std::string::npos);
  EXPECT_NE(light_cache_source.find("LightProxyCreationGuard creation_guard"),
            std::string::npos);
  EXPECT_NE(light_cache_source.find("creation_guard.release()"),
            std::string::npos);
}

TEST(RenderProxyLifecycleTests, FilamentResourceCreationUsesRenderOwnedGuards) {
  const std::string light_cache_source =
      read_source_file("src/render/light_renderable_cache.cpp");
  const std::string grid_renderer_source =
      read_source_file("src/render/viewport_grid_renderer.cpp");
  const std::string render_target_source =
      read_source_file("src/render/viewport_render_target.cpp");

  EXPECT_NE(light_cache_source.find("LightProxyCreationGuard creation_guard"),
            std::string::npos);
  EXPECT_NE(light_cache_source.find("creation_guard.release()"),
            std::string::npos);
  EXPECT_NE(grid_renderer_source.find("struct GroundGridCreationGuard"),
            std::string::npos);
  EXPECT_NE(grid_renderer_source.find("creation_guard.vertex_buffer"),
            std::string::npos);
  EXPECT_NE(grid_renderer_source.find("creation_guard.release()"),
            std::string::npos);
  EXPECT_NE(render_target_source.find("struct RenderTargetCreationGuard"),
            std::string::npos);
  EXPECT_NE(render_target_source.find("creation_guard.color_texture"),
            std::string::npos);
  EXPECT_NE(render_target_source.find("creation_guard.release()"),
            std::string::npos);
  const std::size_t register_texture =
      render_target_source.find("texture_registry_->register_texture(");
  ASSERT_NE(register_texture, std::string::npos);
  EXPECT_NE(render_target_source.find("creation_guard.color_texture",
                                      register_texture),
            std::string::npos);
  EXPECT_LT(register_texture,
            render_target_source.find("creation_guard.release()"));
}

TEST(RenderProxyLifecycleTests, UiIconTexturesAreRenderOwned) {
  const std::string service_header =
      read_source_file("src/render/ui_icon_texture_service.hpp");
  const std::string service_source =
      read_source_file("src/render/ui_icon_texture_service.cpp");
  const std::string png_loader_source =
      read_source_file("src/render/png_texture_loader.cpp");
  const std::string render_system_header =
      read_source_file("src/render/render_system.hpp");
  const std::string ui_system_source = read_source_file("src/ui/ui_system.cpp");

  EXPECT_NE(service_header.find("class UiIconTextureService"),
            std::string::npos);
  EXPECT_NE(service_header.find("RenderUiIconSet"), std::string::npos);
  EXPECT_NE(service_source.find("load_png_texture("), std::string::npos);
  EXPECT_NE(png_loader_source.find("filament::Texture::Builder()"),
            std::string::npos);
  EXPECT_NE(service_source.find("textures_->register_texture(texture)"),
            std::string::npos);
  EXPECT_NE(service_source.find("textures_->unregister_texture(record.handle)"),
            std::string::npos);
  EXPECT_NE(service_source.find("engine_->destroy(record.texture)"),
            std::string::npos);
  EXPECT_NE(render_system_header.find("UiIconTextureService ui_icon_textures_"),
            std::string::npos);
  EXPECT_NE(ui_system_source.find("to_ui_icon_set("), std::string::npos);
  EXPECT_EQ(ui_system_source.find("filament::Texture::Builder"),
            std::string::npos);
}

TEST(RenderProxyLifecycleTests, RenderResourceOwnersAreNonTransferable) {
  EXPECT_TRUE(render_owner_is_non_transferable<RenderSystem>());
  EXPECT_TRUE(render_owner_is_non_transferable<FilamentMaterialLibrary>());
  EXPECT_TRUE(render_owner_is_non_transferable<UiIconTextureService>());
  EXPECT_TRUE(render_owner_is_non_transferable<TextureRegistry>());
  EXPECT_TRUE(render_owner_is_non_transferable<TextureBindingService>());
  EXPECT_TRUE(render_owner_is_non_transferable<MaterialBindingService>());
  EXPECT_TRUE(render_owner_is_non_transferable<MeshRenderableFactory>());
  EXPECT_TRUE(render_owner_is_non_transferable<MeshRenderableCache>());
  EXPECT_TRUE(render_owner_is_non_transferable<ToolPreviewRenderableCache>());
  EXPECT_TRUE(render_owner_is_non_transferable<LightRenderableCache>());
  EXPECT_TRUE(render_owner_is_non_transferable<SceneRenderSyncService>());
  EXPECT_TRUE(render_owner_is_non_transferable<SceneRenderBridge>());
  EXPECT_TRUE(render_owner_is_non_transferable<OverlayProxyFactory>());
  EXPECT_TRUE(render_owner_is_non_transferable<OverlayRenderableCache>());
  EXPECT_TRUE(render_owner_is_non_transferable<ViewportGridRenderer>());
  EXPECT_TRUE(render_owner_is_non_transferable<ViewportRenderTarget>());
  EXPECT_TRUE(render_owner_is_non_transferable<ViewportRenderer>());

  const std::string render_system_header =
      read_source_file("src/render/render_system.hpp");
  const std::string scene_render_bridge_header =
      read_source_file("src/render/scene_render_bridge.hpp");
  const std::string viewport_renderer_header =
      read_source_file("src/render/viewport_renderer.hpp");

  EXPECT_NE(
      render_system_header.find("RenderSystem(const RenderSystem&) = delete"),
      std::string::npos);
  EXPECT_NE(scene_render_bridge_header.find(
                "SceneRenderBridge(SceneRenderBridge&&) = delete"),
            std::string::npos);
  EXPECT_NE(viewport_renderer_header.find(
                "ViewportRenderer(const ViewportRenderer&) = delete"),
            std::string::npos);
}

TEST(ViewportContextMenuPolicyTests,
     OpensForRightClickReleaseInsideViewportWithActions) {
  EXPECT_TRUE(should_open_viewport_context_menu({
      .right_press_tracked = true,
      .right_released = true,
      .release_inside_viewport = true,
      .target_hit = true,
      .has_actions = true,
      .press_screen = {200.0f, 180.0f},
      .release_screen = {201.0f, 181.0f},
  }));
}

TEST(ViewportContextMenuPolicyTests, BlocksModalToolsMissingActionsAndDrags) {
  ViewportContextMenuOpenRequest request{
      .right_press_tracked = true,
      .right_released = true,
      .release_inside_viewport = true,
      .target_hit = true,
      .has_actions = true,
      .press_screen = {200.0f, 180.0f},
      .release_screen = {201.0f, 181.0f},
  };

  request.modal_tool_owns_viewport = true;
  EXPECT_FALSE(should_open_viewport_context_menu(request));

  request.modal_tool_owns_viewport = false;
  request.target_hit = false;
  EXPECT_FALSE(should_open_viewport_context_menu(request));

  request.target_hit = true;
  request.has_actions = false;
  EXPECT_FALSE(should_open_viewport_context_menu(request));

  request.has_actions = true;
  request.release_screen = {240.0f, 180.0f};
  EXPECT_FALSE(should_open_viewport_context_menu(request));

  request.release_screen = {201.0f, 181.0f};
  request.accumulated_drag_delta = {20.0f, 0.0f};
  EXPECT_FALSE(should_open_viewport_context_menu(request));
}

TEST(ViewportContextMenuPolicyTests, PreservesHoverOnlyForClickLikeRightPress) {
  EXPECT_TRUE(should_preserve_viewport_context_menu_hover({
      .right_press_tracked = true,
      .fly_hud_active = false,
      .accumulated_drag_delta = {1.0f, 1.0f},
  }));

  EXPECT_FALSE(should_preserve_viewport_context_menu_hover({
      .right_press_tracked = false,
      .fly_hud_active = false,
      .accumulated_drag_delta = {1.0f, 1.0f},
  }));
  EXPECT_FALSE(should_preserve_viewport_context_menu_hover({
      .right_press_tracked = true,
      .fly_hud_active = true,
      .accumulated_drag_delta = {1.0f, 1.0f},
  }));
  EXPECT_FALSE(should_preserve_viewport_context_menu_hover({
      .right_press_tracked = true,
      .fly_hud_active = false,
      .accumulated_drag_delta = {20.0f, 0.0f},
  }));
}

TEST(ModelingInteractionTests, AxisDragDistanceMathLivesInSharedHelper) {
  const std::string extrude_source =
      read_source_file("src/editor/interactions/extrude_tool.cpp");
  const std::string box_source =
      read_source_file("src/editor/interactions/box_tool.cpp");
  const std::string drag_math_source =
      read_source_file("src/editor/interactions/tool_drag_math.cpp");

  EXPECT_NE(drag_math_source.find("DragDistanceResult axis_distance_from_ray"),
            std::string::npos);
  EXPECT_NE(extrude_source.find("axis_distance_from_ray("), std::string::npos);
  EXPECT_NE(box_source.find("axis_distance_from_ray("), std::string::npos);
  EXPECT_EQ(extrude_source.find("geometry_axis_drag_distance"),
            std::string::npos);
  EXPECT_EQ(box_source.find("box_geometry_axis_drag_distance"),
            std::string::npos);
}

TEST(ModelingSessionServiceTests,
     SelectedHoverOverlayKeepsRemovePreviewContract) {
  const std::string app_overlay = read_source_file(
      "src/editor/modeling/services/modeling_session_services.cpp");
  const std::string runtime_overlay = read_source_file(
      "src/editor/modeling/runtime/session_selection_overlay.cpp");

  EXPECT_EQ(app_overlay.find(
                "selectedHoverTarget && !hover.selected_modifier_pressed"),
            std::string::npos);
  EXPECT_EQ(app_overlay.find("selection_hover_target_matches_selection(session."
                             "objects(), session.selection_mode(), hover)"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      app_overlay, "selection_hover_target_is_selected(session.objects(), "
                   "session.selection_mode(), hover)"));
  EXPECT_EQ(app_overlay.find("if (hover_frame.hover_target_selected)"),
            std::string::npos);
  EXPECT_EQ(runtime_overlay.find("suppress_hover_overlay"), std::string::npos);
  EXPECT_NE(runtime_overlay.find(
                "frame.hover_target_selected = hover.selected_modifier_pressed "
                "&& selected_hover_target"),
            std::string::npos);
  EXPECT_NE(runtime_overlay.find("hover.kind == SelectionHoverKind::None"),
            std::string::npos);
}

TEST(SceneRenderBridgeTests, UsesExplicitTextureColorSpaceForMaterialUploads) {
  const std::string texture_source =
      read_source_file("src/render/texture_binding_service.cpp");
  const std::string png_loader_source =
      read_source_file("src/render/png_texture_loader.cpp");
  const std::string material_source =
      read_source_file("src/render/material_binding_service.cpp");
  const std::string factory_source =
      read_source_file("src/render/mesh_renderable_factory.cpp");
  const std::string app_source = read_source_file("src/app/app.cpp");
  const std::string bootstrap_source = read_source_file(
      "src/editor/session/project_asset_bootstrap_service.cpp");
  const std::string material_library_source =
      read_source_file("src/render/filament_material_library.cpp");
  const std::string scene_bridge_source =
      read_source_file("src/render/scene_render_bridge.cpp");

  EXPECT_NE(
      texture_source.find(
          "create_solid_texture(255, 255, 255, 255, TextureColorSpace::Srgb)"),
      std::string::npos);
  EXPECT_NE(texture_source.find("create_solid_texture(255, 255, 255, 255, "
                                "TextureColorSpace::Linear)"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(texture_source,
                                      "load_png_texture(*engine_, path, "
                                      "PngTextureLoadOptions{"));
  EXPECT_TRUE(source_contains_compact(
      png_loader_source, "color_space == PngTextureColorSpace::Srgb ? "
                         "filament::Texture::InternalFormat::SRGB8_A8 : "
                         "filament::Texture::InternalFormat::RGBA8"));
  EXPECT_NE(material_source.find("MinFilter::LINEAR_MIPMAP_LINEAR"),
            std::string::npos);
  EXPECT_NE(png_loader_source.find("mip_level_count(width, height)"),
            std::string::npos);
  EXPECT_NE(png_loader_source.find("Texture::Usage::GEN_MIPMAPPABLE"),
            std::string::npos);
  EXPECT_NE(png_loader_source.find("texture->generateMipmaps(engine)"),
            std::string::npos);
  EXPECT_NE(material_source.find("material_instance->setCullingMode("),
            std::string::npos);
  EXPECT_NE(material_source.find("double_sided ?"), std::string::npos);
  EXPECT_EQ(material_source.find("colorCulling"), std::string::npos);
  EXPECT_EQ(material_source.find("setCullingMode(\n            colorCulling"),
            std::string::npos);
  EXPECT_NE(
      material_source.find("filament::MaterialInstance::CullingMode::NONE"),
      std::string::npos);
  EXPECT_NE(
      material_source.find(": filament::MaterialInstance::CullingMode::BACK"),
      std::string::npos);
  EXPECT_EQ(
      material_source.find(
          "setCullingMode(filament::MaterialInstance::CullingMode::BACK);"),
      std::string::npos);
  EXPECT_NE(bootstrap_source.find(
                "session.assets().add_texture(std::move(roughness));"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      bootstrap_source, "set_material_texture_slot(default_material, "
                        "material_texture_slot::kBaseColor, albedo_texture)"));
  EXPECT_EQ(
      bootstrap_source.find(
          "make_default_quader_material(\"Default Quader\", albedo_texture"),
      std::string::npos);
  EXPECT_EQ(bootstrap_source.find("const AssetId roughnessTexture = "
                                  "session.assets().add_texture"),
            std::string::npos);
  EXPECT_EQ(app_source.find("set_material_texture_slot("), std::string::npos);
  EXPECT_EQ(app_source.find("make_default_quader_material("),
            std::string::npos);
  EXPECT_NE(app_source.find(
                "render_system_.initialize_scene_render_bridge(render_bridge_"),
            std::string::npos);
  EXPECT_EQ(app_source.find("render_system_.quader_mesh_material()"),
            std::string::npos);
  EXPECT_EQ(app_source.find("render_system_.quader_mesh_lit_material()"),
            std::string::npos);
  EXPECT_NE(material_library_source.find("filament_material_catalog.hpp"),
            std::string::npos);
  EXPECT_NE(
      material_library_source.find("std::string(kFilamentMaterialDirectory)"),
      std::string::npos);
  EXPECT_NE(
      material_library_source.find("std::string(kQuaderMeshMaterialFile)"),
      std::string::npos);
  EXPECT_NE(
      material_library_source.find("std::string(kQuaderMeshLitMaterialFile)"),
      std::string::npos);
  EXPECT_EQ(
      material_library_source.find("materials\" / \"quader_mesh.filamat\""),
      std::string::npos);
  EXPECT_EQ(
      material_library_source.find("materials\" / \"quader_mesh_lit.filamat\""),
      std::string::npos);
  EXPECT_NE(scene_bridge_source.find("material_binding_.initialize(shaded_mesh_"
                                     "material, rendered_mesh_material,"),
            std::string::npos);
  EXPECT_NE(scene_bridge_source.find("texture_binding_)"), std::string::npos);
  EXPECT_NE(
      factory_source.find("material_binding_->create_mesh_material_instance("),
      std::string::npos);
  EXPECT_NE(
      factory_source.find("primitive_material, double_sided, editor_unlit)"),
      std::string::npos);
  EXPECT_NE(
      material_source.find(
          "editor_unlit ? *shaded_mesh_material_ : *rendered_mesh_material_"),
      std::string::npos);
  EXPECT_NE(material_source.find("if (!editor_unlit)"), std::string::npos);
  EXPECT_NE(material_source.find("apply_mesh_surface_grid"),
            std::string::npos);
  EXPECT_NE(material_source.find("surfaceGridMinorColor"), std::string::npos);
  EXPECT_NE(material_source.find("surfaceGridMajorSize"), std::string::npos);
  EXPECT_NE(material_source.find("ViewportBoolSettingId::ShowMeshGrid"),
            std::string::npos);
  EXPECT_NE(material_source.find("ViewportColorSettingId::MeshGridMinor"),
            std::string::npos);
  EXPECT_NE(material_source.find("ViewportColorSettingId::MeshGridMajor"),
            std::string::npos);
  EXPECT_NE(material_source.find("ViewportFloatSettingId::GridMinorLineSize"),
            std::string::npos);
  EXPECT_EQ(
      material_source.find("material_instance->setParameter(\"editor_unlit\""),
      std::string::npos);
  EXPECT_EQ(factory_source.find(".culling(!doubleSided)"), std::string::npos);
  EXPECT_NE(texture_source.find("TextureCacheKey cache_key"),
            std::string::npos);
  EXPECT_NE(texture_source.find(".color_space = texture->color_space"),
            std::string::npos);
  EXPECT_EQ(texture_source.find("bool srgb"), std::string::npos);
  EXPECT_EQ(material_source.find("bool srgb"), std::string::npos);
}

TEST(SceneRenderBridgeTests,
     TextureBindingServiceEvictsObsoleteTextureRevisions) {
  const std::string texture_header =
      read_source_file("src/render/texture_binding_service.hpp");
  const std::string texture_source =
      read_source_file("src/render/texture_binding_service.cpp");

  EXPECT_NE(texture_header.find("void evict_obsolete_texture_revisions(const "
                                "TextureCacheKey &current_key);"),
            std::string::npos);
  EXPECT_NE(texture_source.find("evict_obsolete_texture_revisions(cache_key);"),
            std::string::npos);
  EXPECT_NE(texture_source.find(
                "void TextureBindingService::evict_obsolete_texture_revisions"),
            std::string::npos);
  EXPECT_NE(texture_source.find("it->first.id == current_key.id"),
            std::string::npos);
  EXPECT_NE(
      texture_source.find("it->first.color_space == current_key.color_space"),
      std::string::npos);
  EXPECT_NE(texture_source.find("it->first.revision != current_key.revision"),
            std::string::npos);
  EXPECT_NE(texture_source.find("destroy_texture(it->second);"),
            std::string::npos);
  EXPECT_NE(texture_source.find("texture_cache_.erase(it)"), std::string::npos);
}

TEST(SceneRenderBridgeTests,
     SkipsOverlayProxyRebuildWhenOverlayPayloadIsUnchanged) {
  const std::string cache_source =
      read_source_file("src/render/overlay/overlay_renderable_cache.cpp");
  const std::string cache_header =
      read_source_file("src/render/overlay/overlay_renderable_cache.hpp");
  const std::string builder_source =
      read_source_file("src/render/overlay/overlay_renderable_builder.cpp");
  const std::string signature_header =
      read_source_file("src/render/render_signature_builder.hpp");
  const std::string proxy_factory_source =
      read_source_file("src/render/overlay/overlay_proxy_factory.cpp");

  EXPECT_NE(
      cache_source.find("OverlayRenderableBuilder::frame_signatures(frame)"),
      std::string::npos);
  EXPECT_NE(
      cache_source.find(
          "OverlayRenderableBuilder::layer_build_signature(slot.signatures"),
      std::string::npos);
  EXPECT_NE(cache_header.find(
                "std::array<OverlaySlot, kMaxViewportOverlaySlots> slots_"),
            std::string::npos);
  EXPECT_NE(cache_header.find("OverlayFrameSignatures signatures"),
            std::string::npos);
  EXPECT_NE(cache_source.find("slot.proxy_groups"), std::string::npos);
  EXPECT_NE(signature_header.find("struct OverlayFrameSignatures"),
            std::string::npos);
  EXPECT_NE(builder_source.find(
                "RenderSignatureBuilder::overlay_frame_signatures(frame)"),
            std::string::npos);
  EXPECT_NE(cache_source.find("slot.frame_signature"), std::string::npos);
  EXPECT_NE(cache_source.find("OverlayRenderableBuilder::camera_signature"),
            std::string::npos);
  EXPECT_NE(cache_source.find("const bool camera_changed = camera_signature != "
                              "slot.camera_signature"),
            std::string::npos);
  EXPECT_NE(cache_source.find(
                "const bool layer_changed = layer_mask != slot.layer_mask"),
            std::string::npos);
  EXPECT_NE(cache_source.find(
                "if (!frame_changed && !camera_changed && !layer_changed)"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      cache_source, "slot.frame = std::move(frame); return false;"));
  EXPECT_NE(cache_source.find("group.signature = 0;"), std::string::npos);
  EXPECT_NE(
      cache_source.find("rebuild_slot(slot, frame_changed || layer_changed);"),
      std::string::npos);
  EXPECT_NE(cache_source.find("if (group.signature == layer_signature)"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      cache_source, "layer_has_renderable_primitives(slot.signatures, layer)"));
  EXPECT_NE(cache_source.find("factory_->update_layer_proxies(group.proxies, "
                              "plan, slot.layer_mask)"),
            std::string::npos);
  EXPECT_NE(cache_source.find(
                "factory_->create_layer_proxies(plan, slot.layer_mask)"),
            std::string::npos);
  EXPECT_NE(proxy_factory_source.find(
                "bool OverlayProxyFactory::update_layer_proxies"),
            std::string::npos);
  EXPECT_NE(builder_source.find("viewport_overlay_draw_order(layer) * 64"),
            std::string::npos);
}

TEST(ModelingSessionServiceTests,
     CachesSelectionOverlayFrameByRevisionAndHoverState) {
  const std::string source = read_source_file(
      "src/editor/modeling/services/modeling_session_service.cpp");
  const std::string split_source = read_source_file(
      "src/editor/modeling/services/modeling_session_services.cpp");
  const std::string overlay_header = read_source_file(
      "src/editor/modeling/services/modeling_overlay_service.hpp");
  const std::string drawn_plane_projection = read_source_file(
      "src/editor/projection/drawn_plane_overlay_projection.cpp");
  const std::string cut_preview_projection = read_source_file(
      "src/editor/projection/cut_preview_overlay_projection.cpp");
  const std::string tool_preview_projection = read_source_file(
      "src/editor/projection/tool_preview_mesh_projection.cpp");
  const std::string selection_header = read_source_file(
      "src/editor/modeling/services/modeling_selection_service.hpp");

  EXPECT_NE(
      overlay_header.find("SelectionOverlayCache selection_overlay_cache_"),
      std::string::npos);
  EXPECT_NE(split_source.find("selection_overlay_cache_.matches("),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      split_source, "content_revision, selection_revision, scene_revision"));
  EXPECT_TRUE(source_contains_compact(
      split_source, "selection_mode, overlay_signature, hover"));
  EXPECT_NE(source.find(
                "return overlay_service_.selection_overlay_signature(runtime_, "
                "options);"),
            std::string::npos);
  EXPECT_EQ(source.find("selectionOverlayToolSignature()"), std::string::npos);
  EXPECT_NE(
      split_source.find("ModelingOverlayService::selection_overlay_signature("),
      std::string::npos);
  EXPECT_NE(split_source.find("runtime.cut_state()"), std::string::npos);
  EXPECT_NE(split_source.find("runtime.mirror_state()"), std::string::npos);
  EXPECT_NE(split_source.find("selection_overlay_cache_.frame()"),
            std::string::npos);
  EXPECT_NE(split_source.find(
                "SelectionOverlayProjection::project_selection_overlay_frame("),
            std::string::npos);
  EXPECT_EQ(source.find(
                "SelectionOverlayProjection::project_selection_overlay_frame("),
            std::string::npos);
  EXPECT_NE(split_source.find("selection_overlay_cache_.store("),
            std::string::npos);
  EXPECT_NE(split_source.find("selection_overlay_base_frame(session, options)"),
            std::string::npos);
  EXPECT_NE(split_source.find("suppress_persistent_object_selection"),
            std::string::npos);
  EXPECT_NE(overlay_header.find("overlay_base_cache_"), std::string::npos);
  EXPECT_NE(split_source.find("build_selection_hover_overlay_frame"),
            std::string::npos);
  EXPECT_NE(split_source.find("append_selected_hover_filtered_base_frame"),
            std::string::npos);
  EXPECT_NE(split_source.find("DrawnPlaneOverlayProjection::append("),
            std::string::npos);
  EXPECT_NE(
      split_source.find("CutPreviewOverlayProjection::append_result_lines("),
      std::string::npos);
  EXPECT_NE(
      split_source.find("ToolPreviewMeshProjection::mirror_preview_meshes("),
      std::string::npos);
  EXPECT_NE(drawn_plane_projection.find("DrawnPlanePreviewFrame"),
            std::string::npos);
  EXPECT_NE(cut_preview_projection.find("CutPreviewEdgeAccumulator"),
            std::string::npos);
  EXPECT_NE(tool_preview_projection.find(
                "ToolPreviewMeshProjection::box_preview_mesh_from_descriptor"),
            std::string::npos);
  EXPECT_EQ(split_source.find("struct DrawnPlanePreviewFrame"),
            std::string::npos);
  EXPECT_EQ(split_source.find("struct CutPreviewEdgeAccumulator"),
            std::string::npos);
  EXPECT_EQ(split_source.find("previewMeshFromDocuments("), std::string::npos);
  EXPECT_EQ(split_source.find(
                "std::optional<MeshAsset> box_preview_mesh_from_descriptor"),
            std::string::npos);
  EXPECT_NE(source.find("cached_pick_object_bounds()"), std::string::npos);
  EXPECT_NE(source.find("cached_pick_face_geometry(pick_options)"),
            std::string::npos);
  EXPECT_NE(
      split_source.find("ModelingSelectionService::apply_pick_selection("),
      std::string::npos);
  EXPECT_NE(split_source.find("quader_editor_core::apply_pick_selection("),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      source, "selection_service_.apply_pick_selection(session_,"));
  EXPECT_NE(source.find("scene_, object_registry_, effective_request)"),
            std::string::npos);
  EXPECT_NE(split_source.find(
                "cached_pick_object_bounds(session, scene, object_registry)"),
            std::string::npos);
  EXPECT_NE(split_source.find("cached_pick_face_geometry(session, scene,"),
            std::string::npos);
  EXPECT_NE(split_source.find("object_registry, pick_options)"),
            std::string::npos);
  EXPECT_NE(split_source.find("picking_cache_.face_geometry_entries"),
            std::string::npos);
  EXPECT_NE(split_source.find(
                "PickingCacheProjection::object_face_geometry_signature("),
            std::string::npos);
  EXPECT_NE(split_source.find("objects[index], pick_options)"),
            std::string::npos);
  EXPECT_EQ(split_source.find("applyPickedSelectionToSession"),
            std::string::npos);
  EXPECT_EQ(split_source.find("sessionHasComponentSelection"),
            std::string::npos);
  EXPECT_EQ(split_source.find("pickSelectRequestHitsTarget"),
            std::string::npos);
  const std::string picking_projection =
      read_source_file("src/editor/projection/picking_cache_projection.cpp");
  EXPECT_NE(picking_projection.find("pick_options.vertex_radius"),
            std::string::npos);
  EXPECT_NE(picking_projection.find("pick_options.edge_radius"),
            std::string::npos);
  EXPECT_NE(picking_projection.find("pick_options.has_hidden_material_slot"),
            std::string::npos);
  EXPECT_EQ(split_source.find("(void)pick_options;"), std::string::npos);
}

TEST(ModelingSessionServiceTests,
     TransformDragBuildsVertexSnapCachesOnlyWhenRuntimeCanUseThem) {
  const std::string source = read_source_file(
      "src/editor/modeling/services/modeling_transform_drag_service.cpp");

  EXPECT_NE(source.find("bool transform_drag_can_use_vertex_snap("),
            std::string::npos);
  EXPECT_NE(
      source.find(
          "drag.tool == quader_editor_core::ViewportInteractionTool::Move"),
      std::string::npos);
  EXPECT_NE(source.find("drag.move.active"), std::string::npos);
  EXPECT_NE(source.find("transform_drag_axis_supports_vertex_snap(drag.axis)"),
            std::string::npos);
  EXPECT_NE(source.find("request.vertex_snap_radius > 0.0 &&"),
            std::string::npos);
  EXPECT_NE(
      source.find("transform_drag_can_use_vertex_snap(drag_before_update)"),
      std::string::npos);
  EXPECT_NE(source.find("context.port.cached_pick_object_bounds()"),
            std::string::npos);
  EXPECT_NE(source.find("context.port.cached_pick_face_geometry()"),
            std::string::npos);
}

TEST(SceneRenderBridgeTests,
     ConvertsSceneOverlayColorsToLinearBeforeViewportEncode) {
  const std::string source =
      read_source_file("src/render/overlay/overlay_face_fill_builder.cpp");

  EXPECT_NE(source.find("rgba8_srgb_to_linear(triangle.color)"),
            std::string::npos);
  EXPECT_NE(source.find("linear_color"), std::string::npos);
}

TEST(SceneRenderBridgeTests,
     MeshRenderablesReceiveShadowsAndViewportEnablesShadowing) {
  const std::string mesh_factory =
      read_source_file({"src", "render", "mesh_renderable_factory.cpp"});
  const std::string mesh_cache =
      read_source_file({"src", "render", "mesh_renderable_cache.cpp"});
  const std::string viewport_renderer =
      read_source_file({"src", "render", "viewport_renderer.cpp"});
  const std::string viewport_panel =
      read_source_file({"src", "ui", "viewport_panel.cpp"});

  EXPECT_NE(mesh_factory.find(".castShadows(cast_shadows)"), std::string::npos);
  EXPECT_NE(mesh_factory.find(".receiveShadows(!editor_unlit && !depth_only)"),
            std::string::npos);
  EXPECT_NE(mesh_factory.find("material_instance->setColorWrite(false)"),
            std::string::npos);
  EXPECT_NE(mesh_factory.find("material_instance->setDepthWrite(true)"),
            std::string::npos);
  EXPECT_NE(mesh_factory.find("material_instance->setDepthCulling(true)"),
            std::string::npos);
  EXPECT_NE(mesh_factory.find(".channel(channel)"), std::string::npos);
  EXPECT_NE(mesh_cache.find("kViewportRenderedSceneLayerMask"),
            std::string::npos);
  EXPECT_NE(mesh_cache.find("kViewportShadedSceneLayerMask"),
            std::string::npos);
  EXPECT_NE(mesh_cache.find("kViewportOverlayLayerMask"), std::string::npos);
  EXPECT_NE(mesh_cache.find("kViewportOverlayDepthChannel"), std::string::npos);
  EXPECT_NE(mesh_cache.find("rendered_mesh_proxies_"), std::string::npos);
  EXPECT_NE(mesh_cache.find("shaded_mesh_proxies_"), std::string::npos);
  EXPECT_NE(mesh_cache.find("overlay_depth_mesh_proxies_"), std::string::npos);
  EXPECT_NE(viewport_renderer.find("view_->setShadowingEnabled(true)"),
            std::string::npos);
  EXPECT_NE(
      viewport_renderer.find("view_->setShadowType(filament::ShadowType::PCF)"),
      std::string::npos);
  EXPECT_NE(
      viewport_renderer.find("overlay_view_->setPostProcessingEnabled(false)"),
      std::string::npos);
  EXPECT_NE(viewport_renderer.find(
                "overlay_view_->setAntiAliasing(filament::AntiAliasing::NONE)"),
            std::string::npos);
  EXPECT_NE(viewport_renderer.find(
                "overlay_view_->setDithering(filament::Dithering::NONE)"),
            std::string::npos);
  EXPECT_NE(viewport_renderer.find("overlay_view_->setColorGrading(nullptr)"),
            std::string::npos);
  EXPECT_NE(viewport_renderer.find("overlay_view_->setShadowingEnabled(false)"),
            std::string::npos);
  EXPECT_NE(viewport_renderer.find("overlay_view_->setBlendMode(filament::View:"
                                   ":BlendMode::TRANSLUCENT)"),
            std::string::npos);
  EXPECT_NE(viewport_panel.find("pane_state.renderer.set_scene_layer_mask"),
            std::string::npos);
  EXPECT_NE(viewport_panel.find("kViewportRenderedSceneLayerMask"),
            std::string::npos);
  EXPECT_NE(viewport_panel.find("kViewportShadedSceneLayerMask"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      viewport_panel,
      "pane_state.renderer.set_shadowing_enabled(pane_workspace.shading == "
      "ViewportShadingMode::Rendered)"));
}

TEST(ViewportRendererTests, OverlayPassUsesNeutralExposureCamera) {
  const std::string header =
      read_source_file({"src", "render", "viewport_renderer.hpp"});
  const std::string source =
      read_source_file({"src", "render", "viewport_renderer.cpp"});

  EXPECT_NE(header.find("filament::Camera *overlay_camera_ = nullptr"),
            std::string::npos);
  EXPECT_NE(header.find("utils::Entity overlay_camera_entity_"),
            std::string::npos);
  EXPECT_NE(
      source.find(
          "overlay_camera_ = engine_->createCamera(overlay_camera_entity_)"),
      std::string::npos);
  EXPECT_NE(source.find("overlay_view_->setCamera(overlay_camera_)"),
            std::string::npos);
  EXPECT_EQ(source.find("overlay_view_->setCamera(camera_)"),
            std::string::npos);
  EXPECT_NE(source.find("apply_look_at(overlay_camera_)"), std::string::npos);
  EXPECT_NE(source.find("apply_projection(overlay_camera_)"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      source, "camera_->setExposure(std::max(settings.camera.aperture"));
  EXPECT_NE(
      source.find("apply_neutral_overlay_exposure(filament::Camera *camera)"),
      std::string::npos);
  EXPECT_NE(source.find("kViewportOverlayCameraAperture"), std::string::npos);
  EXPECT_NE(source.find("kViewportOverlayCameraShutterSpeed"),
            std::string::npos);
  EXPECT_NE(source.find("kViewportOverlayCameraSensitivity"),
            std::string::npos);
  EXPECT_EQ(count_occurrences(source, "overlay_camera_->setExposure("), 0u);
  EXPECT_GE(count_occurrences(
                source, "apply_neutral_overlay_exposure(overlay_camera_)"),
            3u);
}

TEST(ViewportRendererTests,
     PresentsSceneAndOverlayViewportTexturesThroughUiEncoder) {
  const std::string source =
      read_source_file("src/render/viewport_render_target.cpp");
  const std::string renderer_header =
      read_source_file("src/render/viewport_renderer.hpp");
  const std::string renderer_source =
      read_source_file("src/render/viewport_renderer.cpp");
  const std::string coordinator_source =
      read_source_file("src/render/viewport_render_pass_coordinator.cpp");
  const std::string viewport_panel_source =
      read_source_file("src/ui/viewport_panel.cpp");
  const std::string grid_source =
      read_source_file("src/render/viewport_grid_renderer.cpp");

  EXPECT_NE(source.find(".format(Texture::InternalFormat::RGBA8)"),
            std::string::npos);
  EXPECT_NE(source.find("kViewportRenderTargetCapacityStep = 128u"),
            std::string::npos);
  EXPECT_NE(source.find("viewport_render_target_capacity_for_size"),
            std::string::npos);
  EXPECT_NE(source.find(
                "view->setViewport({0, 0, viewport_width_, viewport_height_})"),
            std::string::npos);
  EXPECT_NE(source.find("void ViewportRenderTarget::set_sample_color_space"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      source,
      "texture_registry_->update_texture(texture_handle_, color_texture_, "
      "sample_color_space_)"));
  EXPECT_TRUE(source_contains_compact(
      source,
      "texture_registry_->register_texture(creation_guard.color_texture, "
      "sample_color_space_)"));
  EXPECT_NE(renderer_header.find(
                "RenderTextureHandle overlay_texture_handle() const"),
            std::string::npos);
  EXPECT_NE(renderer_header.find("ViewportRenderTarget overlay_render_target_"),
            std::string::npos);
  EXPECT_NE(renderer_header.find("float texture_max_u() const"),
            std::string::npos);
  EXPECT_NE(renderer_header.find("float texture_max_v() const"),
            std::string::npos);
  EXPECT_NE(renderer_header.find("scene_texture_sample_color_space_"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      renderer_source,
      "render_target_.resize(width_, height_, view_, "
      "scene_texture_sample_color_space_)"));
  EXPECT_TRUE(source_contains_compact(
      renderer_source,
      "return post_processing_enabled ? TextureSampleColorSpace::DisplaySrgb : "
      "TextureSampleColorSpace::LinearSrgb"));
  EXPECT_TRUE(source_contains_compact(
      renderer_source,
      "render_target_.set_sample_color_space("
      "scene_texture_sample_color_space_)"));
  EXPECT_TRUE(source_contains_compact(
      renderer_source,
      "static_cast<float>(width_) / static_cast<float>(std::max(1u, "
      "render_target_.width()))"));
  EXPECT_NE(renderer_source.find(
                "TextureSampleColorSpace::DisplaySrgbPremultipliedAlpha"),
            std::string::npos);
  EXPECT_NE(coordinator_source.find(
                "overlay_clear.clear = overlay_uses_separate_target"),
            std::string::npos);
  EXPECT_NE(coordinator_source.find(
                "overlay_clear.clearColor = {0.0f, 0.0f, 0.0f, 0.0f}"),
            std::string::npos);
  EXPECT_NE(coordinator_source.find("overlay_visible_layer_mask() == 0u && "
                                    "!overlay_uses_separate_target"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find(
                "pane_state.renderer.overlay_texture_handle()"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find("pane_state.renderer.texture_max_u()"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find("pane_state.renderer.texture_max_v()"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find("ImGui::GetWindowDrawList()->AddImage("),
            std::string::npos);
  EXPECT_NE(
      grid_source.find("settings.color(ViewportColorSettingId::GridMinor)"),
      std::string::npos);
  EXPECT_NE(
      grid_source.find(
          "settings.float_value(ViewportFloatSettingId::GridMinorLineSize)"),
      std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      grid_source, "setDepthCulling(controller.state().projection != "
                   "ViewportProjection::Orthographic)"));
  EXPECT_EQ(source.find(".format(Texture::InternalFormat::SRGB8_A8)"),
            std::string::npos);
}

TEST(ViewportRendererTests,
     ViewportInfoOverlayUsesRendererTimingAndVisibleMeshSubmissionStats) {
  const std::string app_source = read_source_file("src/app/app.cpp");
  const std::string ui_context_header =
      read_source_file("src/ui/ui_context.hpp");
  const std::string ui_system_header = read_source_file("src/ui/ui_system.hpp");
  const std::string ui_system_source = read_source_file("src/ui/ui_system.cpp");
  const std::string viewport_panel_header =
      read_source_file("src/ui/viewport_panel.hpp");
  const std::string viewport_panel_source =
      read_source_file("src/ui/viewport_panel.cpp");
  const std::string viewport_info_stats_source =
      read_source_file("src/render/viewport_info_stats.cpp");

  EXPECT_NE(app_source.find("ui_.set_render_performance_stats(render_system_."
                            "frame_performance_stats())"),
            std::string::npos);
  EXPECT_NE(
      ui_context_header.find("RenderFramePerformanceStats render_performance"),
      std::string::npos);
  EXPECT_NE(ui_system_header.find(
                "void set_render_performance_stats(RenderFramePerformanceStats "
                "stats)"),
            std::string::npos);
  EXPECT_NE(
      ui_system_source.find(".render_performance = render_performance_stats_"),
      std::string::npos);
  EXPECT_NE(viewport_panel_header.find("draw_viewport_info_overlay"),
            std::string::npos);
  EXPECT_NE(viewport_panel_header.find("viewport_info_cached_scene_revision_"),
            std::string::npos);
  EXPECT_NE(viewport_panel_header.find("viewport_info_cached_asset_revision_"),
            std::string::npos);
  EXPECT_NE(viewport_panel_header.find("viewport_info_cached_camera_"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find("same_viewport_info_camera"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find(
                "collect_viewport_info_scene_stats(document, assets, camera)"),
            std::string::npos);
  EXPECT_NE(
      viewport_info_stats_source.find(
          "document.is_effectively_visible(entity_id)"),
      std::string::npos);
  EXPECT_NE(
      viewport_info_stats_source.find("coalesced_mesh_primitive_spans(*mesh)"),
      std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      viewport_panel_source,
      "const bool cache_hit = viewport_info_scene_stats_valid_"));
  EXPECT_NE(viewport_panel_source.find("scene_revision = document.revision()"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find("asset_revision = assets.revision()"),
            std::string::npos);
  EXPECT_NE(viewport_info_stats_source.find("viewport_info_bounds_visible"),
            std::string::npos);
  EXPECT_NE(viewport_info_stats_source.find("stats.draw_calls += spans.size()"),
            std::string::npos);
  EXPECT_NE(
      viewport_info_stats_source.find(
          "stats.primitives += span.index_count / 3u"),
      std::string::npos);
  EXPECT_NE(viewport_panel_source.find("ViewportBoolSettingId::ShowInfo"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find("ui.viewport.info.draw_calls"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find("render_stats.frames_per_second"),
            std::string::npos);
  EXPECT_EQ(viewport_panel_source.find(
                "1.0 / ui_context.viewport->frame_timing.raw_delta_seconds"),
            std::string::npos);
  EXPECT_EQ(viewport_panel_source.find("1000.0 / render_stats.gpu_frame_ms"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find("draw_viewport_info_text("),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find("draw_list.AddText"), std::string::npos);
  const std::size_t text_helper =
      viewport_panel_source.find("void draw_viewport_info_text(");
  const std::size_t next_helper =
      viewport_panel_source.find("hit_drawn_plane_captured_point", text_helper);
  ASSERT_NE(text_helper, std::string::npos);
  ASSERT_NE(next_helper, std::string::npos);
  const std::string text_helper_source =
      viewport_panel_source.substr(text_helper, next_helper - text_helper);
  EXPECT_EQ(text_helper_source.find("AddRectFilled"), std::string::npos);
}

TEST(ViewportRendererTests,
     MultiViewportRenderDoesNotMutateSceneInsideFilamentFrame) {
  const std::string renderer_source =
      read_source_file("src/render/viewport_renderer.cpp");
  const std::string grid_renderer_source =
      read_source_file("src/render/viewport_grid_renderer.cpp");
  const std::string coordinator_source =
      read_source_file("src/render/viewport_render_pass_coordinator.cpp");
  const std::string app_source = read_source_file("src/app/app.cpp");
  const std::string ui_system_source = read_source_file("src/ui/ui_system.cpp");
  const std::string viewport_panel_source =
      read_source_file("src/ui/viewport_panel.cpp");
  const std::string overlay_cache_source =
      read_source_file("src/render/overlay/overlay_renderable_cache.cpp");
  const std::string overlay_proxy_factory_source =
      read_source_file("src/render/overlay/overlay_proxy_factory.cpp");
  const std::string scene_bridge_source =
      read_source_file("src/render/scene_render_bridge.cpp");
  const std::string mesh_cache_source =
      read_source_file("src/render/mesh_renderable_cache.cpp");
  const std::string mesh_factory_source =
      read_source_file("src/render/mesh_renderable_factory.cpp");

  const std::size_t render_start =
      renderer_source.find("void ViewportRenderer::render");
  const std::size_t render_end =
      renderer_source.find("void ViewportRenderer::update_visible_layers");
  ASSERT_NE(render_start, std::string::npos);
  ASSERT_NE(render_end, std::string::npos);
  const std::string render_function =
      renderer_source.substr(render_start, render_end - render_start);
  EXPECT_EQ(render_function.find("scene_->addEntity"), std::string::npos);
  EXPECT_EQ(render_function.find("scene_->remove"), std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      renderer_source, "grid_renderer_.update(controller, width(), height(), "
                       "grid_world_spacing, settings, visible)"));
  EXPECT_TRUE(source_contains_compact(
      grid_renderer_source,
      ".layerMask(kViewportAllLayerMask, grid_layer_mask_)"));
  EXPECT_NE(renderer_source.find("render_pass_coordinator_.apply_visible_"
                                 "layers(view_, overlay_view_)"),
            std::string::npos);
  EXPECT_EQ(renderer_source.find("setChannelDepthClearEnabled("),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      renderer_source,
      "render_pass_coordinator_.render(renderer, view_, overlay_view_, "
      "surface_ == ViewportRenderSurface::Texture)"));
  EXPECT_TRUE(source_contains_compact(
      coordinator_source, "scene_view->setVisibleLayers(kViewportAllLayerMask, "
                          "scene_visible_layer_mask())"));
  EXPECT_TRUE(source_contains_compact(
      coordinator_source,
      "overlay_view->setVisibleLayers(kViewportAllLayerMask, "
      "overlay_visible_layer_mask())"));
  EXPECT_NE(coordinator_source.find("layer_mask | kViewportOverlayLayerMask"),
            std::string::npos);
  EXPECT_NE(coordinator_source.find("clear.discard = false"),
            std::string::npos);
  EXPECT_NE(coordinator_source.find(
                "overlay_clear.clear = overlay_uses_separate_target"),
            std::string::npos);
  EXPECT_NE(coordinator_source.find("renderer.setClearOptions(clear);"),
            std::string::npos);

  const std::size_t prepare_viewports =
      app_source.find("ui_.prepare_viewports(*editor_, render_bridge_,");
  const std::size_t ui_begin_frame = app_source.find(
      "ui_.begin_frame(frame_timing, framebuffer_width, framebuffer_height)");
  const std::size_t prepare_render_data = app_source.find(
      "ui_.prepare_render_data(framebuffer_width, framebuffer_height)");
  const std::size_t begin_frame =
      app_source.find("render_system_.begin_frame()");
  ASSERT_NE(prepare_viewports, std::string::npos);
  ASSERT_NE(ui_begin_frame, std::string::npos);
  ASSERT_NE(prepare_render_data, std::string::npos);
  ASSERT_NE(begin_frame, std::string::npos);
  EXPECT_LT(ui_begin_frame, prepare_viewports);
  EXPECT_LT(prepare_viewports, prepare_render_data);
  EXPECT_LT(ui_begin_frame, begin_frame);
  EXPECT_LT(prepare_viewports, begin_frame);
  EXPECT_LT(prepare_render_data, begin_frame);

  EXPECT_NE(ui_system_source.find("void UISystem::begin_frame("),
            std::string::npos);
  EXPECT_NE(ui_system_source.find("void UISystem::prepare_viewports("),
            std::string::npos);
  EXPECT_NE(ui_system_source.find("viewport_panel_->prepare_viewports("),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find("ViewportWorkspaceService &workspace"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find("const SceneDocument &document"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find("const AssetRegistry &assets"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find(
                "pane_state.renderer.update_camera(pane_workspace.camera)"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      viewport_panel_source,
      "pane_workspace.camera.set_clip_planes( "
      "scene_settings->camera.near_plane, "
      "scene_settings->camera.far_plane); "
      "pane_state.renderer.apply_scene_settings("));
  EXPECT_NE(
      viewport_panel_source.find("pane_state.renderer.set_scene_layer_mask"),
      std::string::npos);
  EXPECT_NE(
      viewport_panel_source.find("pane_state.renderer.set_shadowing_enabled"),
      std::string::npos);
  EXPECT_NE(viewport_panel_source.find("pane_state.renderer.set_scene_visible"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find("ViewportBoolSettingId::ShowGrid"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find("ViewportBoolSettingId::ShowGizmos"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find("ViewportBoolSettingId::ShowOverlays"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find("ViewportBoolSettingId::ShowInfo"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      viewport_panel_source,
      "pane_state.renderer.update_ground_grid(pane_workspace.camera, "
      "grid_world_size, settings, grid_visible)"));
  EXPECT_TRUE(source_contains_compact(
      viewport_panel_source,
      "render_bridge.update_mesh_surface_grid(grid_world_size, settings)"));
  EXPECT_TRUE(source_contains_compact(
      scene_bridge_source,
      "mesh_cache_.update_surface_grid(grid_world_spacing, settings)"));
  EXPECT_TRUE(source_contains_compact(
      mesh_cache_source,
      "update_surface_grid_proxy_map(*factory_, rendered_mesh_proxies_, "
      "grid_world_spacing, settings)"));
  EXPECT_TRUE(source_contains_compact(
      mesh_cache_source,
      "update_surface_grid_proxy_map(*factory_, shaded_mesh_proxies_, "
      "grid_world_spacing, settings)"));
  EXPECT_EQ(
      mesh_cache_source.find(
          "update_surface_grid_proxy_map(*factory_, overlay_depth_mesh_proxies_"),
      std::string::npos);
  EXPECT_NE(mesh_factory_source.find(
                "material_binding_->apply_mesh_surface_grid("),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      viewport_panel_source,
      "pane_state.renderer.set_overlay_visible(overlays_visible ||"));
  EXPECT_TRUE(source_contains_compact(
      viewport_panel_source,
      "bool viewport_shading_shows_point_light_gizmos(ViewportShadingMode "
      "shading)"));
  EXPECT_NE(viewport_panel_source.find(
                "shading == ViewportShadingMode::Shaded ||"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find(
                "shading == ViewportShadingMode::Rendered"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      viewport_panel_source,
      "const bool point_light_gizmos_visible = "
      "point_light_gizmos_visible_for_pane(gizmos_visible, "
      "pane_workspace.shading)"));
  EXPECT_TRUE(source_contains_compact(
      viewport_panel_source,
      "if ((overlays_visible && selection_overlay_cache_.valid()) || "
      "point_light_gizmos_visible)"));
  EXPECT_NE(viewport_panel_source.find(".point_light_gizmos_visible = "
                                       "point_light_gizmos_visible"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find("render_bridge.set_overlay_frame("),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find(
                "viewport_grid_layer_mask(viewport_pane_index(pane_id))"),
            std::string::npos);
  EXPECT_NE(overlay_proxy_factory_source.find(
                ".layerMask(kViewportAllLayerMask, layer_mask)"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      overlay_cache_source,
      "void OverlayRenderableCache::rebuild_slot(OverlaySlot &slot, "
      "bool rebuild_camera_independent_layers)"));
}

TEST(RenderLifecycleTests, ResourceOwnersShutdownBeforeInitialize) {
  for (const char *relative_path : {
           "src/render/scene_render_bridge.cpp",
           "src/render/texture_binding_service.cpp",
           "src/render/viewport_grid_renderer.cpp",
       }) {
    const std::string source = read_source_file(relative_path);
    const std::size_t initialize = source.find("::initialize");
    ASSERT_NE(initialize, std::string::npos) << relative_path;
    const std::size_t shutdown = source.find("shutdown();", initialize);
    ASSERT_NE(shutdown, std::string::npos) << relative_path;
    EXPECT_LT(shutdown, source.find("engine_ =", initialize)) << relative_path;
  }
}

TEST(TextureRegistryTests, TracksPerTexturePresentationColorSpace) {
  const std::string header_source =
      read_source_file("src/render/texture_registry.hpp");

  EXPECT_NE(header_source.find("enum class TextureSampleColorSpace"),
            std::string::npos);
  EXPECT_NE(header_source.find("DisplaySrgb"), std::string::npos);
  EXPECT_NE(header_source.find("DisplaySrgbPremultipliedAlpha"),
            std::string::npos);
  EXPECT_NE(header_source.find("LinearSrgb"), std::string::npos);
  EXPECT_NE(header_source.find("using RenderTextureHandle = std::uintptr_t"),
            std::string::npos);
  EXPECT_NE(
      header_source.find("TextureSampleColorSpace "
                         "sample_color_space(RenderTextureHandle id) const"),
      std::string::npos);
  EXPECT_NE(header_source.find("void update_texture(RenderTextureHandle id"),
            std::string::npos);

  const std::string source =
      read_source_file("src/render/texture_registry.cpp");

  EXPECT_NE(source.find(".sample_color_space = sample_color_space"),
            std::string::npos);
  EXPECT_NE(source.find("TextureSampleColorSpace::DisplaySrgb"),
            std::string::npos);
  EXPECT_NE(source.find("it->second.sample_color_space"), std::string::npos);
}

TEST(AppStartupTests, DoesNotCreateDefaultModelingMesh) {
  const std::string source = read_source_file("src/app/app.cpp");

  EXPECT_EQ(source.find("create_box_from_bounds({"), std::string::npos);
  EXPECT_EQ(source.find("Startup scene created modeling box"),
            std::string::npos);
  EXPECT_EQ(source.find("startup box"), std::string::npos);
  EXPECT_EQ(source.find("Fake Directional Light"), std::string::npos);
  EXPECT_EQ(source.find("document_.set_light"), std::string::npos);
}

TEST(AppStartupTests, DelegatesDefaultAssetsAndUsesSessionReadView) {
  const std::string app_header = read_source_file("src/app/app.hpp");
  const std::string app_source = read_source_file("src/app/app.cpp");
  const std::string bootstrap_source = read_source_file(
      "src/editor/session/project_asset_bootstrap_service.cpp");
  const std::string build_source = read_source_file("CMakeLists.txt");

  EXPECT_NE(app_source.find("ProjectAssetBootstrapService{}."
                            "bootstrap_default_assets(project_session_)"),
            std::string::npos);
  EXPECT_NE(app_source.find("const ProjectSessionReadView session_view = "
                            "project_session_.read_view();"),
            std::string::npos);
  EXPECT_NE(app_source.find("ui_.prepare_viewports(*editor_, render_bridge_, "
                            "session_view.scene"),
            std::string::npos);
  EXPECT_EQ(app_header.find("create_startup_scene"), std::string::npos);
  EXPECT_EQ(app_source.find("create_startup_scene"), std::string::npos);
  EXPECT_EQ(app_source.find("default_material_catalog.hpp"), std::string::npos);
  EXPECT_EQ(app_source.find("make_default_quader_material("),
            std::string::npos);
  EXPECT_EQ(app_source.find(".assets().add_texture("), std::string::npos);
  EXPECT_EQ(app_source.find(".assets().add_material("), std::string::npos);
  EXPECT_NE(bootstrap_source.find("default_material_catalog.hpp"),
            std::string::npos);
  EXPECT_NE(bootstrap_source.find(
                "ProjectAssetBootstrapService::bootstrap_default_assets"),
            std::string::npos);
  EXPECT_NE(build_source.find(
                "src/editor/session/project_asset_bootstrap_service.cpp"),
            std::string::npos);
}

TEST(AppLoopTests,
     SuspendsGpuSubmissionForNonDrawableWindowsAndIdlesBetweenFrames) {
  const std::string app_header = read_source_file("src/app/app.hpp");
  const std::string app_source = read_source_file("src/app/app.cpp");
  const std::string platform_header =
      read_source_file("src/platform/platform_window.hpp");
  const std::string platform_source =
      read_source_file("src/platform/platform_window.cpp");
  const std::string ui_header = read_source_file("src/ui/ui_system.hpp");
  const std::string ui_source = read_source_file("src/ui/ui_system.cpp");
  const std::string viewport_panel_header =
      read_source_file("src/ui/viewport_panel.hpp");

  EXPECT_NE(platform_header.find("platform_window_can_render_frames"),
            std::string::npos);
  EXPECT_NE(
      platform_header.find("[[nodiscard]] bool can_render_frames() const"),
      std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      platform_header, "note_frame_presented(std::uint32_t framebuffer_width, "
                       "std::uint32_t framebuffer_height)"));
  EXPECT_NE(platform_header.find(
                "using LiveResizeFrameCallback = std::function<bool()>"),
            std::string::npos);
  EXPECT_NE(platform_header.find("schedule_live_resize_frame_callback"),
            std::string::npos);
  EXPECT_NE(platform_header.find("native_live_resize_frame_active() const"),
            std::string::npos);
  EXPECT_NE(platform_source.find("SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN"),
            std::string::npos);
  EXPECT_NE(platform_source.find("SDL occlusion can remain stale"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(platform_source,
                                      "pixel_width > 0 && pixel_height > 0"));
  EXPECT_TRUE(source_contains_compact(
      platform_source,
      "GetClientRect(static_cast<HWND>(native_chrome_window_handle_), "
      "&client_rect)"));
  EXPECT_TRUE(source_contains_compact(
      platform_source, "owner.native_chrome_message_depth_ == 0 && "
                       "owner.live_resize_frame_callback_pending_"));
  EXPECT_NE(platform_source.find("owner.schedule_live_resize_frame_callback()"),
            std::string::npos);
  EXPECT_NE(platform_source.find("dispatch_live_resize_frame_callback()"),
            std::string::npos);
  EXPECT_NE(platform_source.find("kLiveResizeFrameMessage = WM_APP"),
            std::string::npos);
  EXPECT_NE(platform_source.find("message == kLiveResizeFrameMessage"),
            std::string::npos);
  EXPECT_NE(platform_source.find("message == WM_ERASEBKGND"),
            std::string::npos);
  EXPECT_NE(platform_source.find("message == WM_PAINT"), std::string::npos);
  EXPECT_NE(platform_source.find("live_resize_frame_paint_scheduled_ = false"),
            std::string::npos);
  EXPECT_NE(platform_source.find("BeginPaint(hwnd, &paint)"),
            std::string::npos);
  EXPECT_NE(platform_source.find("EndPaint(hwnd, &paint)"), std::string::npos);
  EXPECT_NE(platform_source.find("PostMessageW(hwnd, kLiveResizeFrameMessage"),
            std::string::npos);
  EXPECT_NE(platform_source.find("native_chrome_message_depth_ > 0"),
            std::string::npos);
  EXPECT_NE(
      platform_source.find(
          "RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE)"),
      std::string::npos);
  EXPECT_EQ(platform_source.find("RDW_UPDATENOW"), std::string::npos);
  EXPECT_EQ(platform_source.find(
                "dispatch_live_resize_frame_callback();\n            return;"),
            std::string::npos);
  EXPECT_NE(platform_source.find("native_live_resize_frame_active_ = true"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      platform_source, "message == WM_SIZE && native_move_size_active_ && "
                       "w_param != SIZE_MINIMIZED"));
  EXPECT_NE(platform_source.find("redraw_native_resize_frame"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      platform_source, "const bool redraw_native_resize_frame = "
                       "(message != WM_SIZE || w_param != SIZE_MINIMIZED)"));
  EXPECT_EQ(platform_source.find("message == WM_SIZING ||\n                    "
                                 "message == WM_WINDOWPOSCHANGED"),
            std::string::npos);
  EXPECT_EQ(platform_source.find("message == WM_WINDOWPOSCHANGED ||\n          "
                                 "          message == WM_DPICHANGED"),
            std::string::npos);
  EXPECT_NE(platform_source.find("prime_live_resize_frame_coalescing();"),
            std::string::npos);
  EXPECT_NE(platform_source.find(
                "live_resize_frame_pending_width_pixels_ = width_pixels_"),
            std::string::npos);
  EXPECT_NE(platform_source.find(
                "live_resize_frame_pending_width_pixels_ = pending_width"),
            std::string::npos);
  EXPECT_NE(platform_source.find(
                "live_resize_frame_pending_height_pixels_ = pending_height"),
            std::string::npos);
  EXPECT_NE(platform_source.find("if (live_resize_frame_callback_())"),
            std::string::npos);
  EXPECT_NE(platform_source.find(
                "note_frame_presented(pending_width, pending_height)"),
            std::string::npos);
  EXPECT_EQ(
      platform_source.find(
          "void PlatformWindow::prime_live_resize_frame_coalescing() {\n    "
          "refresh_pixel_size();\n    live_resize_frame_size_valid_ = true;"),
      std::string::npos);
  EXPECT_EQ(
      platform_source.find(
          "native_move_size_active_ &&\n            native_resize_active_ &&"),
      std::string::npos);
  EXPECT_EQ(platform_source.find("message == WM_MOVING"), std::string::npos);
  EXPECT_EQ(platform_source.find("message == WM_TIMER"), std::string::npos);
  EXPECT_EQ(platform_source.find("SetTimer("), std::string::npos);
  EXPECT_EQ(platform_source.find("KillTimer("), std::string::npos);
  EXPECT_NE(app_header.find("wait_until_next_frame()"), std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      app_source,
      "if (window_.can_render_frames()) { render_frame(frame_timing);"));
  EXPECT_NE(app_source.find("render_frame(frame_clock_.tick())"),
            std::string::npos);
  EXPECT_NE(app_source.find("render_system_.begin_frame()"), std::string::npos);
  EXPECT_NE(
      app_source.find(
          "const std::uint32_t framebuffer_width = window_.width_pixels()"),
      std::string::npos);
  EXPECT_NE(app_source.find("ui_.begin_frame(frame_timing, framebuffer_width, "
                            "framebuffer_height)"),
            std::string::npos);
  EXPECT_NE(
      app_source.find(
          "ui_.prepare_render_data(framebuffer_width, framebuffer_height)"),
      std::string::npos);
  EXPECT_NE(app_source.find("window_.note_frame_presented(framebuffer_width, "
                            "framebuffer_height)"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      app_source,
      "if (window_.native_chrome_message_active() && "
      "!window_.native_live_resize_frame_active()) { return false;"));
  EXPECT_NE(
      app_source.find(
          "const bool frame_submitted = render_frame(frame_clock_.tick())"),
      std::string::npos);
  EXPECT_NE(app_source.find("return frame_submitted;"), std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      app_source,
      "ui_.set_viewport_render_resize_suspended(window_.chrome_resize_active() "
      "|| window_.native_chrome_interaction_active())"));
  EXPECT_TRUE(source_contains_compact(
      app_source,
      "if (window_.native_chrome_interaction_active()) { return false;"));
  EXPECT_EQ(app_source.find("!window_.chrome_resize_active() && "
                            "!window_.native_chrome_interaction_active()"),
            std::string::npos);
  EXPECT_NE(app_source.find(
                "chrome_action_invalidates_current_frame(*chrome_action)"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      app_source,
      "if (!window_.can_render_frames()) { rendering_frame_ = false;"));
  EXPECT_NE(app_source.find("ui_.end_frame_without_render();"),
            std::string::npos);
  EXPECT_NE(app_source.find("platform_wait_for_event_timeout(wait_ms)"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      app_source, "window_.native_chrome_interaction_active() ? "
                  "kActiveFrameWaitMs"));
  EXPECT_NE(app_source.find("kIdleFrameWaitMs = 100"), std::string::npos);
  EXPECT_NE(app_source.find("kSuspendedWindowWaitMs = 250"), std::string::npos);
  EXPECT_NE(app_source.find("!window_.can_render_frames())"),
            std::string::npos);
  EXPECT_NE(ui_header.find("end_frame_without_render()"), std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      ui_header, "begin_frame(const FrameTiming &frame_timing, std::uint32_t "
                 "framebuffer_width, std::uint32_t framebuffer_height)"));
  EXPECT_NE(ui_source.find("io.DisplaySize = {"), std::string::npos);
  EXPECT_NE(ui_source.find("io.DisplayFramebufferScale = {1.0f, 1.0f}"),
            std::string::npos);
  EXPECT_NE(
      ui_header.find("[[nodiscard]] bool needs_continuous_rendering() const"),
      std::string::npos);
  EXPECT_NE(ui_source.find("relative_mouse_captured_ ||"), std::string::npos);
  EXPECT_NE(viewport_panel_header.find("last_fly_navigation_active_"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(viewport_panel_header,
                                      "active_viewport_settings().bool_value("
                                      "ViewportBoolSettingId::ShowInfo)"));
  EXPECT_NE(viewport_panel_header.find("navigation_gizmo_.input_locked()"),
            std::string::npos);
  EXPECT_NE(viewport_panel_header.find("interaction_controller_.dragging()"),
            std::string::npos);
}

TEST(SceneDocumentTests, CreatesStableEntitiesAndComponents) {
  SceneDocument scene;
  const EntityId sphere = scene.create_entity("Sphere");
  const EntityId light = scene.create_entity("Light");

  scene.set_mesh(sphere, MeshComponent{.mesh = 11});
  scene.set_modeling_object(
      sphere, ModelingObjectComponent{.object_id = 42, .material_slot = 3});
  scene.set_selected(sphere, true);
  scene.rename_entity(sphere, "Box");
  scene.set_visible(light, false);
  scene.set_light(light, LightComponent{});

  EXPECT_NE(sphere, light);
  EXPECT_FALSE(scene.uuid(sphere).empty());
  EXPECT_FALSE(scene.uuid(light).empty());
  EXPECT_NE(scene.uuid(sphere), scene.uuid(light));
  EXPECT_EQ(scene.entity_for_uuid(scene.uuid(sphere)), sphere);
  EXPECT_EQ(scene.entity_for_uuid(scene.uuid(light)), light);
  EXPECT_TRUE(scene.exists(sphere));
  EXPECT_TRUE(scene.entt_entity(sphere) != entt::null);
  EXPECT_EQ(scene.entity_id(scene.entt_entity(sphere)), sphere);
  ASSERT_NE(scene.name(sphere), nullptr);
  EXPECT_EQ(scene.name(sphere)->name, "Box");
  ASSERT_NE(scene.mesh(sphere), nullptr);
  EXPECT_EQ(scene.mesh(sphere)->mesh, 11u);
  EXPECT_FALSE(scene.mesh(sphere)->double_sided);
  ASSERT_NE(scene.modeling_object(sphere), nullptr);
  EXPECT_EQ(scene.modeling_object(sphere)->object_id, 42);
  EXPECT_EQ(scene.modeling_object(sphere)->material_slot, 3u);
  ASSERT_NE(scene.light(light), nullptr);
  ASSERT_NE(scene.visibility(light), nullptr);
  EXPECT_FALSE(scene.visibility(light)->visible);
  EXPECT_EQ(scene.primary_selection(), sphere);
  ASSERT_EQ(scene.selected_entities().size(), 1u);
  EXPECT_EQ(scene.selected_entities().front(), sphere);

  const std::vector<EntityRecord> records = scene.entity_records();
  ASSERT_EQ(records.size(), 2u);
  EXPECT_EQ(records[0].id, sphere);
  EXPECT_EQ(records[0].uuid, scene.uuid(sphere));
  EXPECT_EQ(records[0].name, "Box");
  EXPECT_TRUE(records[0].selected);
  EXPECT_TRUE(records[0].has_mesh);
  EXPECT_TRUE(records[0].has_modeling_object);
  EXPECT_EQ(records[1].id, light);
  EXPECT_FALSE(records[1].visible);
  EXPECT_TRUE(records[1].has_light);

  scene.clear_selection();
  EXPECT_TRUE(scene.selected_entities().empty());
  EXPECT_EQ(scene.primary_selection(), kInvalidEntityId);
  EXPECT_GT(scene.revision(), 0u);
}

TEST(SceneDocumentTests, ComponentAccessorsAreReadOnlyOutsideDocumentMutators) {
  static_assert(
      std::is_same_v<decltype(std::declval<SceneDocument &>().name(EntityId{})),
                     const NameComponent *>);
  static_assert(std::is_same_v<
                decltype(std::declval<SceneDocument &>().transform(EntityId{})),
                const TransformComponent *>);
  static_assert(
      std::is_same_v<decltype(std::declval<SceneDocument &>().visibility(
                         EntityId{})),
                     const VisibilityComponent *>);
  static_assert(std::is_same_v<
                decltype(std::declval<SceneDocument &>().selection(EntityId{})),
                const SelectionComponent *>);
  static_assert(
      std::is_same_v<decltype(std::declval<SceneDocument &>().mesh(EntityId{})),
                     const MeshComponent *>);
  static_assert(std::is_same_v<
                decltype(std::declval<SceneDocument &>().material(EntityId{})),
                const MaterialComponent *>);
  static_assert(std::is_same_v<decltype(std::declval<SceneDocument &>().light(
                                   EntityId{})),
                               const LightComponent *>);

  const std::string header = read_source_file("src/scene/scene_document.hpp");
  EXPECT_EQ(header.find("TransformComponent* transform(EntityId id);"),
            std::string::npos);
  EXPECT_EQ(header.find("VisibilityComponent* visibility(EntityId id);"),
            std::string::npos);
  EXPECT_EQ(header.find("MeshComponent* mesh(EntityId id);"),
            std::string::npos);
  EXPECT_NE(header.find("TransformComponent *mutable_transform(EntityId id);"),
            std::string::npos);
  EXPECT_NE(
      header.find("VisibilityComponent *mutable_visibility(EntityId id);"),
      std::string::npos);

  SceneDocument scene;
  const EntityId entity = scene.create_entity("Box");
  const std::uint64_t after_create = scene.revision();
  ASSERT_NE(scene.transform(entity), nullptr);

  TransformComponent moved = *scene.transform(entity);
  moved.position.x = 5.0f;
  scene.set_transform(entity, moved);

  EXPECT_GT(scene.revision(), after_create);
  ASSERT_NE(scene.transform(entity), nullptr);
  EXPECT_FLOAT_EQ(scene.transform(entity)->position.x, 5.0f);
}

TEST(SceneDocumentTests, TracksDomainRevisionsForOverlayCacheInvalidation) {
  SceneDocument scene;
  const EntityId entity = scene.create_entity("Box");

  const std::uint64_t selection_before = scene.selection_revision();
  const std::uint64_t transform_before = scene.transform_revision();
  const std::uint64_t visibility_before = scene.visibility_revision();
  const std::uint64_t mesh_before = scene.mesh_binding_revision();

  ASSERT_TRUE(scene.set_selected(entity, true));
  EXPECT_GT(scene.selection_revision(), selection_before);
  EXPECT_EQ(scene.transform_revision(), transform_before);
  EXPECT_EQ(scene.visibility_revision(), visibility_before);
  EXPECT_EQ(scene.mesh_binding_revision(), mesh_before);

  const std::uint64_t transform_selection = scene.selection_revision();
  TransformComponent moved;
  moved.position.x = 3.0f;
  scene.set_transform(entity, moved);
  EXPECT_GT(scene.transform_revision(), transform_before);
  EXPECT_EQ(scene.selection_revision(), transform_selection);

  const std::uint64_t visibility_revision = scene.visibility_revision();
  ASSERT_TRUE(scene.set_visible(entity, false));
  EXPECT_GT(scene.visibility_revision(), visibility_revision);
  const std::uint64_t unchanged_visibility_revision =
      scene.visibility_revision();
  ASSERT_TRUE(scene.set_visible(entity, false));
  EXPECT_EQ(scene.visibility_revision(), unchanged_visibility_revision);

  scene.set_mesh(entity, MeshComponent{.mesh = 7});
  EXPECT_GT(scene.mesh_binding_revision(), mesh_before);

  const std::uint64_t clear_selection_before = scene.selection_revision();
  scene.clear_selection();
  EXPECT_GT(scene.selection_revision(), clear_selection_before);
}

TEST(SceneEntityNamingTests, MeshNamesStartAtBaseNameThenUseOneBasedSuffixes) {
  SceneDocument scene;

  EXPECT_EQ(SceneEntityNaming::next_name_for_kind(scene, SceneEntityKind::Mesh),
            "Mesh");

  const EntityId first = scene.create_entity("Mesh");
  scene.set_mesh(first, MeshComponent{.mesh = 1});
  EXPECT_EQ(SceneEntityNaming::next_name_for_kind(scene, SceneEntityKind::Mesh),
            "Mesh_1");

  const EntityId second = scene.create_entity("Mesh_1");
  scene.set_mesh(second, MeshComponent{.mesh = 2});
  EXPECT_EQ(SceneEntityNaming::next_name_for_kind(scene, SceneEntityKind::Mesh),
            "Mesh_2");
  EXPECT_NE(SceneEntityNaming::next_name_for_kind(scene, SceneEntityKind::Mesh),
            "Mesh_0");
}

TEST(SceneEntityNamingTests,
     BatchAllocatorUsesDocumentNamesAndReservesGeneratedNames) {
  SceneDocument scene;
  const EntityId first = scene.create_entity("Mesh");
  scene.set_mesh(first, MeshComponent{.mesh = 1});
  const EntityId second = scene.create_entity("Mesh_1");
  scene.set_mesh(second, MeshComponent{.mesh = 2});

  SceneEntityNameAllocator names(scene);

  EXPECT_EQ(names.next_name_for_kind(SceneEntityKind::Mesh), "Mesh_2");
  EXPECT_EQ(names.next_name_for_kind(SceneEntityKind::Mesh), "Mesh_3");

  names.reserve_name("Mesh_4");
  EXPECT_EQ(names.next_name_for_kind(SceneEntityKind::Mesh), "Mesh_5");
}

TEST(SceneDocumentTests, BatchedTransformAssignmentsDirtyOnceAndSkipNoOps) {
  SceneDocument scene;
  const EntityId first = scene.create_entity("First");
  const EntityId second = scene.create_entity("Second");
  const std::uint64_t before = scene.revision();

  const std::array<SceneTransformAssignment, 2u> assignments{{
      {.entity = first,
       .transform = TransformComponent{.position = {1.0f, 0.0f, 0.0f}}},
      {.entity = second,
       .transform = TransformComponent{.position = {2.0f, 0.0f, 0.0f}}},
  }};

  scene.set_transforms(assignments);

  EXPECT_EQ(scene.revision(), before + 1u);
  ASSERT_NE(scene.transform(first), nullptr);
  ASSERT_NE(scene.transform(second), nullptr);
  EXPECT_FLOAT_EQ(scene.transform(first)->position.x, 1.0f);
  EXPECT_FLOAT_EQ(scene.transform(second)->position.x, 2.0f);

  scene.set_transforms(assignments);
  EXPECT_EQ(scene.revision(), before + 1u);
}

TEST(SceneDocumentTests, TracksMultipleSelectedEntitiesInStableOrder) {
  SceneDocument scene;
  const EntityId first = scene.create_entity("First");
  const EntityId second = scene.create_entity("Second");
  const EntityId third = scene.create_entity("Third");

  scene.set_selected(third, true);
  EXPECT_EQ(scene.primary_selection(), third);
  scene.set_selected(first, true);

  const std::vector<EntityId> selected = scene.selected_entities();
  ASSERT_EQ(selected.size(), 2u);
  EXPECT_EQ(selected[0], first);
  EXPECT_EQ(selected[1], third);
  EXPECT_EQ(scene.primary_selection(), first);

  const std::vector<EntityRecord> records = scene.entity_records();
  ASSERT_EQ(records.size(), 3u);
  EXPECT_TRUE(records[0].selected);
  EXPECT_TRUE(records[0].active);
  EXPECT_FALSE(records[1].selected);
  EXPECT_FALSE(records[1].active);
  EXPECT_TRUE(records[2].selected);
  EXPECT_FALSE(records[2].active);

  EXPECT_FALSE(scene.set_active_selection(second));
  EXPECT_TRUE(scene.set_active_selection(third));
  EXPECT_EQ(scene.primary_selection(), third);

  scene.set_selected(third, false);
  ASSERT_EQ(scene.selected_entities().size(), 1u);
  EXPECT_EQ(scene.primary_selection(), first);

  scene.set_selected(first, false);
  EXPECT_TRUE(scene.selected_entities().empty());
  EXPECT_EQ(scene.primary_selection(), kInvalidEntityId);
}

TEST(SceneDocumentTests, PromotesPrimarySelectionWhenActiveEntityIsDestroyed) {
  SceneDocument scene;
  const EntityId first = scene.create_entity("First");
  const EntityId second = scene.create_entity("Second");

  scene.set_selected(first, true);
  scene.set_selected(second, true);
  EXPECT_EQ(scene.primary_selection(), second);

  EXPECT_TRUE(scene.destroy_entity(second));
  EXPECT_EQ(scene.primary_selection(), first);
  ASSERT_EQ(scene.selected_entities().size(), 1u);
  EXPECT_EQ(scene.selected_entities().front(), first);
}

TEST(SceneDocumentTests, TracksHierarchyTreeOrderAndInheritedState) {
  SceneDocument scene;
  const EntityId group = scene.create_empty("Empty");
  const EntityId child = scene.create_entity("Mesh");
  const EntityId grandchild = scene.create_entity("Nested");
  const EntityId sibling = scene.create_entity("Sibling");

  scene.set_transform(group,
                      TransformComponent{.position = {10.0f, 0.0f, 0.0f}});
  scene.set_transform(child,
                      TransformComponent{.position = {2.0f, 0.0f, 0.0f}});
  scene.set_transform(grandchild,
                      TransformComponent{.position = {0.0f, 3.0f, 0.0f}});
  ASSERT_TRUE(scene.set_parent(child, group, false));
  ASSERT_TRUE(scene.set_parent(grandchild, child, false));

  EXPECT_EQ(scene.parent(child), group);
  EXPECT_TRUE(scene.is_descendant_of(grandchild, group));
  EXPECT_FALSE(scene.set_parent(group, grandchild));

  const std::vector<EntityId> tree_order = scene.entity_tree_order();
  ASSERT_EQ(tree_order.size(), 4u);
  EXPECT_EQ(tree_order[0], group);
  EXPECT_EQ(tree_order[1], child);
  EXPECT_EQ(tree_order[2], grandchild);
  EXPECT_EQ(tree_order[3], sibling);

  const std::vector<EntityRecord> records = scene.entity_records_tree_order();
  ASSERT_EQ(records.size(), 4u);
  EXPECT_EQ(records[0].kind, EntityKind::Empty);
  EXPECT_TRUE(records[0].has_children);
  EXPECT_EQ(records[1].parent, group);
  EXPECT_EQ(records[1].depth, 1);
  EXPECT_EQ(records[2].depth, 2);

  const QVec3 world_point =
      transform_point(scene.world_transform_matrix(grandchild), {});
  EXPECT_FLOAT_EQ(world_point.x, 12.0f);
  EXPECT_FLOAT_EQ(world_point.y, 3.0f);
  EXPECT_FLOAT_EQ(world_point.z, 0.0f);

  ASSERT_TRUE(scene.set_visible(group, false));
  EXPECT_TRUE(scene.visibility(child)->visible);
  EXPECT_FALSE(scene.is_effectively_visible(child));
  ASSERT_TRUE(scene.set_locked(group, true));
  EXPECT_TRUE(scene.is_effectively_locked(grandchild));
}

TEST(SceneDocumentTests, ReparentCanPreserveWorldTransformAndDestroyChildren) {
  SceneDocument scene;
  const EntityId group = scene.create_empty("Group");
  const EntityId child = scene.create_entity("Child");
  const EntityId leaf = scene.create_entity("Leaf");

  scene.set_transform(group,
                      TransformComponent{.position = {10.0f, 0.0f, 0.0f}});
  scene.set_transform(child,
                      TransformComponent{.position = {2.0f, 0.0f, 0.0f}});
  scene.set_transform(leaf, TransformComponent{.position = {1.0f, 0.0f, 0.0f}});
  ASSERT_TRUE(scene.set_parent(child, group, false));

  const QVec3 before = transform_point(scene.world_transform_matrix(child), {});
  ASSERT_TRUE(scene.set_parent(child, kInvalidEntityId, true));
  const QVec3 after = transform_point(scene.world_transform_matrix(child), {});
  EXPECT_FLOAT_EQ(before.x, after.x);
  EXPECT_FLOAT_EQ(before.y, after.y);
  ASSERT_NE(scene.transform(child), nullptr);
  EXPECT_FLOAT_EQ(scene.transform(child)->position.x, 12.0f);

  ASSERT_TRUE(scene.set_parent(leaf, group, false));
  scene.set_selected(leaf, true);
  EXPECT_TRUE(scene.destroy_entity(group));
  EXPECT_FALSE(scene.exists(group));
  EXPECT_FALSE(scene.exists(leaf));
  EXPECT_TRUE(scene.exists(child));
  EXPECT_TRUE(scene.selected_entities().empty());
}

TEST(SceneBoundsTests, ComputesSelectedWorldBoundsFromSpatialTransforms) {
  AssetRegistry assets;
  MeshAsset cube;
  cube.vertices = {
      {.position = {-1.0f, -1.0f, -1.0f}},
      {.position = {1.0f, 1.0f, 1.0f}},
  };
  const AssetId cube_mesh = assets.add_mesh(std::move(cube));

  SceneDocument scene;
  const EntityId mesh_entity = scene.create_entity("Scaled Mesh");
  scene.set_mesh(mesh_entity, MeshComponent{.mesh = cube_mesh});
  scene.set_transform(mesh_entity, TransformComponent{
                                       .position = {10.0f, 0.0f, 0.0f},
                                       .scale = {2.0f, 3.0f, 4.0f},
                                   });
  scene.set_selected(mesh_entity, true);

  const EntityId point_entity = scene.create_entity("Light");
  scene.set_transform(point_entity,
                      TransformComponent{.position = {-4.0f, 5.0f, 2.0f}});
  scene.set_selected(point_entity, true);

  const EntityId hidden_entity = scene.create_entity("Hidden");
  scene.set_transform(
      hidden_entity,
      TransformComponent{.position = {-100.0f, -100.0f, -100.0f}});
  scene.set_visible(hidden_entity, false);
  scene.set_selected(hidden_entity, true);

  const std::optional<SceneBounds> bounds =
      selected_world_bounds(scene, assets);
  ASSERT_TRUE(bounds.has_value());
  EXPECT_NEAR(bounds->min.x, -4.0f, 0.001f);
  EXPECT_NEAR(bounds->min.y, -3.0f, 0.001f);
  EXPECT_NEAR(bounds->min.z, -4.0f, 0.001f);
  EXPECT_NEAR(bounds->max.x, 12.0f, 0.001f);
  EXPECT_NEAR(bounds->max.y, 5.0f, 0.001f);
  EXPECT_NEAR(bounds->max.z, 4.0f, 0.001f);

  const QVec3 center = bounds_center(*bounds);
  EXPECT_NEAR(center.x, 4.0f, 0.001f);
  EXPECT_NEAR(center.y, 1.0f, 0.001f);
  EXPECT_NEAR(center.z, 0.0f, 0.001f);
}

TEST(SceneBoundsTests, CentersBottomOriginMeshOnItsSelectedWorldBounds) {
  AssetRegistry assets;
  MeshAsset bottom_origin_box;
  bottom_origin_box.vertices = {
      {.position = {-1.0f, 0.0f, -1.0f}},
      {.position = {1.0f, 2.0f, 1.0f}},
  };
  const AssetId box_mesh = assets.add_mesh(std::move(bottom_origin_box));

  SceneDocument scene;
  const EntityId entity = scene.create_entity("Bottom Origin Box");
  scene.set_mesh(entity, MeshComponent{.mesh = box_mesh});
  scene.set_transform(entity,
                      TransformComponent{.position = {0.0f, 0.0f, 0.0f}});
  scene.set_selected(entity, true);

  const std::optional<SceneBounds> bounds =
      selected_world_bounds(scene, assets);
  ASSERT_TRUE(bounds.has_value());
  const QVec3 center = bounds_center(*bounds);
  EXPECT_NEAR(center.x, 0.0f, 0.001f);
  EXPECT_NEAR(center.y, 1.0f, 0.001f);
  EXPECT_NEAR(center.z, 0.0f, 0.001f);
}

TEST(TransformPivotTests, KeepsNonOriginVisualPivotFixedDuringRotation) {
  TransformComponent transform;
  const QVec3 world_pivot{0.0f, 1.0f, 0.0f};
  const QVec3 local_pivot = local_pivot_for_world_point(transform, world_pivot);

  TransformComponent rotated = transform;
  rotated.rotation_euler.z = 1.57079632679f;
  pin_local_pivot_to_world_point(rotated, local_pivot, world_pivot);

  const QVec3 pinned_world_pivot = transform_point(
      compose_transform(rotated.spatial_transform()), local_pivot);
  EXPECT_NEAR(pinned_world_pivot.x, world_pivot.x, 0.001f);
  EXPECT_NEAR(pinned_world_pivot.y, world_pivot.y, 0.001f);
  EXPECT_NEAR(pinned_world_pivot.z, world_pivot.z, 0.001f);
  EXPECT_NEAR(rotated.position.x, 1.0f, 0.001f);
  EXPECT_NEAR(rotated.position.y, 1.0f, 0.001f);
}

TEST(TransformPivotTests, KeepsNonOriginVisualPivotFixedDuringScale) {
  TransformComponent transform;
  const QVec3 world_pivot{0.0f, 1.0f, 0.0f};
  const QVec3 local_pivot = local_pivot_for_world_point(transform, world_pivot);

  TransformComponent scaled = transform;
  scaled.scale = {2.0f, 3.0f, 4.0f};
  pin_local_pivot_to_world_point(scaled, local_pivot, world_pivot);

  const QVec3 pinned_world_pivot = transform_point(
      compose_transform(scaled.spatial_transform()), local_pivot);
  EXPECT_NEAR(pinned_world_pivot.x, world_pivot.x, 0.001f);
  EXPECT_NEAR(pinned_world_pivot.y, world_pivot.y, 0.001f);
  EXPECT_NEAR(pinned_world_pivot.z, world_pivot.z, 0.001f);
  EXPECT_NEAR(scaled.position.x, 0.0f, 0.001f);
  EXPECT_NEAR(scaled.position.y, -2.0f, 0.001f);
}

TEST(TransformPivotTests, AppliesRotationDeltaInGlobalSpace) {
  constexpr float kHalfPi = 1.57079632679f;
  TransformComponent transform;
  transform.rotation_euler = {0.0f, 0.0f, kHalfPi};

  TransformComponent rotated = transform;
  apply_world_rotation_delta_around_pivot(rotated, {kHalfPi, 0.0f, 0.0f}, {},
                                          {});

  const glm::mat4 current =
      glm::mat4_cast(to_glm(quat_from_euler_radians(transform.rotation_euler)));
  const glm::mat4 delta =
      glm::mat4_cast(to_glm(quat_from_euler_radians({kHalfPi, 0.0f, 0.0f})));
  const QMat4 expected_world_rotation = from_glm(delta * current);
  const QMat4 actual_world_rotation =
      compose_transform(rotated.spatial_transform());

  const std::array<QVec3, 3> basis = {{
      {1.0f, 0.0f, 0.0f},
      {0.0f, 1.0f, 0.0f},
      {0.0f, 0.0f, 1.0f},
  }};
  for (const QVec3 axis : basis) {
    const QVec3 expected = transform_vector(expected_world_rotation, axis);
    const QVec3 actual = transform_vector(actual_world_rotation, axis);
    EXPECT_NEAR(actual.x, expected.x, 0.001f);
    EXPECT_NEAR(actual.y, expected.y, 0.001f);
    EXPECT_NEAR(actual.z, expected.z, 0.001f);
  }

  TransformComponent locally_added = transform;
  locally_added.rotation_euler =
      locally_added.rotation_euler + QVec3{kHalfPi, 0.0f, 0.0f};
  const QVec3 world_space_y =
      transform_vector(actual_world_rotation, {0.0f, 1.0f, 0.0f});
  const QVec3 local_space_y = transform_vector(
      compose_transform(locally_added.spatial_transform()), {0.0f, 1.0f, 0.0f});
  EXPECT_GT(length(world_space_y - local_space_y), 0.1f);
}

TEST(TransformPivotTests, KeepsEquivalentEulerAnglesNearReferenceValues) {
  constexpr float kPi = 3.14159265358979323846f;
  const QVec3 reference{};
  const QVec3 decomposed_equivalent{kPi, kPi / 3.0f, kPi};

  const QVec3 stable =
      equivalent_euler_radians_closest_to(decomposed_equivalent, reference);
  EXPECT_NEAR(stable.x, 0.0f, 0.001f);
  EXPECT_NEAR(stable.y, 2.0f * kPi / 3.0f, 0.001f);
  EXPECT_NEAR(stable.z, 0.0f, 0.001f);
}

TEST(TransformPivotTests, NormalizesEulerDisplayAnglesToPositiveTurn) {
  constexpr float kPi = 3.14159265358979323846f;
  const QVec3 normalized =
      positive_euler_radians({0.0f, -kPi / 12.0f, kPi * 2.0f});

  EXPECT_NEAR(normalized.x, 0.0f, 0.001f);
  EXPECT_NEAR(normalized.y, kPi * 23.0f / 12.0f, 0.001f);
  EXPECT_NEAR(normalized.z, 0.0f, 0.001f);
}

TEST(MathTypesTests, ComposeTransformRoundTripsPoints) {
  const QTransform transform{
      .position = {2.0f, 3.0f, 4.0f},
      .rotation = {},
      .scale = {2.0f, 3.0f, 4.0f},
  };

  const QMat4 local_to_world = compose_transform(transform);
  const QMat4 world_to_local = inverse(local_to_world);

  const QVec3 world_point = transform_point(local_to_world, {1.0f, 1.0f, 1.0f});
  EXPECT_NEAR(world_point.x, 4.0f, 0.001f);
  EXPECT_NEAR(world_point.y, 6.0f, 0.001f);
  EXPECT_NEAR(world_point.z, 8.0f, 0.001f);

  const QVec3 local_point = transform_point(world_to_local, world_point);
  EXPECT_NEAR(local_point.x, 1.0f, 0.001f);
  EXPECT_NEAR(local_point.y, 1.0f, 0.001f);
  EXPECT_NEAR(local_point.z, 1.0f, 0.001f);
}

namespace {

void expect_viewport_placement(const ViewportPanePlacement &placement,
                               ViewportPaneId pane_id, float x, float y,
                               float width, float height) {
  EXPECT_EQ(placement.pane_id, pane_id);
  EXPECT_NEAR(placement.normalized_rect.x, x, 0.001f);
  EXPECT_NEAR(placement.normalized_rect.y, y, 0.001f);
  EXPECT_NEAR(placement.normalized_rect.width, width, 0.001f);
  EXPECT_NEAR(placement.normalized_rect.height, height, 0.001f);
}

void expect_viewport_split_handle(const ViewportSplitHandle &handle,
                                  ViewportSplitHandleId id,
                                  ViewportSplitHandleAxis axis, float position,
                                  float start, float end) {
  EXPECT_EQ(handle.id, id);
  EXPECT_EQ(handle.axis, axis);
  EXPECT_NEAR(handle.position, position, 0.001f);
  EXPECT_NEAR(handle.start, start, 0.001f);
  EXPECT_NEAR(handle.end, end, 0.001f);
}

ViewportLayoutState make_distinct_viewport_layout_state() {
  ViewportLayoutState state;
  state.set_handle_position(ViewportSplitHandleId::TwoHorizontal, 0.25f);
  state.set_handle_position(ViewportSplitHandleId::TwoVertical, 0.35f);
  state.set_handle_position(ViewportSplitHandleId::ThreePrimary, 0.60f);
  state.set_handle_position(ViewportSplitHandleId::ThreeSecondary, 0.40f);
  state.set_handle_position(ViewportSplitHandleId::FourVertical, 0.30f);
  state.set_handle_position(ViewportSplitHandleId::FourHorizontal, 0.70f);
  return state;
}

} // namespace

TEST(ViewportLayoutTests, OptionLabelsMatchViewportLayoutModel) {
  const auto &options = viewport_layout_options();

  ASSERT_EQ(options.size(), 8u);
  EXPECT_EQ(options[0].id, ViewportLayoutId::Single);
  EXPECT_EQ(options[0].label, "Single View");
  EXPECT_EQ(options[1].id, ViewportLayoutId::TwoHorizontal);
  EXPECT_EQ(options[1].label, "Two-View Horizontal");
  EXPECT_EQ(options[2].id, ViewportLayoutId::TwoVertical);
  EXPECT_EQ(options[2].label, "Two-View Vertical");
  EXPECT_EQ(options[3].id, ViewportLayoutId::ThreePrimaryRight);
  EXPECT_EQ(options[3].label, "Three-View Primary Right");
  EXPECT_EQ(options[4].id, ViewportLayoutId::ThreePrimaryLeft);
  EXPECT_EQ(options[4].label, "Three-View Primary Left");
  EXPECT_EQ(options[5].id, ViewportLayoutId::ThreePrimaryBottom);
  EXPECT_EQ(options[5].label, "Three-View Primary Bottom");
  EXPECT_EQ(options[6].id, ViewportLayoutId::ThreePrimaryTop);
  EXPECT_EQ(options[6].label, "Three-View Primary Top");
  EXPECT_EQ(options[7].id, ViewportLayoutId::FourQuad);
  EXPECT_EQ(options[7].label, "Four-View Quad");

  EXPECT_EQ(viewport_layout_label(ViewportLayoutId::ThreePrimaryTop),
            "Three-View Primary Top");
}

TEST(ViewportLayoutTests, BuildPlacementsMatchNormalizedRects) {
  const ViewportLayoutState state = make_distinct_viewport_layout_state();

  std::vector<ViewportPanePlacement> placements =
      viewport_layout_placements(ViewportLayoutId::Single, state);
  ASSERT_EQ(placements.size(), 1u);
  expect_viewport_placement(placements[0], ViewportPaneId::Perspective, 0.0f,
                            0.0f, 1.0f, 1.0f);

  placements =
      viewport_layout_placements(ViewportLayoutId::TwoHorizontal, state);
  ASSERT_EQ(placements.size(), 2u);
  expect_viewport_placement(placements[0], ViewportPaneId::Perspective, 0.0f,
                            0.0f, 0.25f, 1.0f);
  expect_viewport_placement(placements[1], ViewportPaneId::Top, 0.25f, 0.0f,
                            0.75f, 1.0f);

  placements = viewport_layout_placements(ViewportLayoutId::TwoVertical, state);
  ASSERT_EQ(placements.size(), 2u);
  expect_viewport_placement(placements[0], ViewportPaneId::Perspective, 0.0f,
                            0.0f, 1.0f, 0.35f);
  expect_viewport_placement(placements[1], ViewportPaneId::Top, 0.0f, 0.35f,
                            1.0f, 0.65f);

  placements =
      viewport_layout_placements(ViewportLayoutId::ThreePrimaryRight, state);
  ASSERT_EQ(placements.size(), 3u);
  expect_viewport_placement(placements[0], ViewportPaneId::Top, 0.0f, 0.0f,
                            0.60f, 0.40f);
  expect_viewport_placement(placements[1], ViewportPaneId::Side, 0.0f, 0.40f,
                            0.60f, 0.60f);
  expect_viewport_placement(placements[2], ViewportPaneId::Perspective, 0.60f,
                            0.0f, 0.40f, 1.0f);

  placements =
      viewport_layout_placements(ViewportLayoutId::ThreePrimaryLeft, state);
  ASSERT_EQ(placements.size(), 3u);
  expect_viewport_placement(placements[0], ViewportPaneId::Perspective, 0.0f,
                            0.0f, 0.60f, 1.0f);
  expect_viewport_placement(placements[1], ViewportPaneId::Top, 0.60f, 0.0f,
                            0.40f, 0.40f);
  expect_viewport_placement(placements[2], ViewportPaneId::Side, 0.60f, 0.40f,
                            0.40f, 0.60f);

  placements =
      viewport_layout_placements(ViewportLayoutId::ThreePrimaryBottom, state);
  ASSERT_EQ(placements.size(), 3u);
  expect_viewport_placement(placements[0], ViewportPaneId::Top, 0.0f, 0.0f,
                            0.40f, 0.60f);
  expect_viewport_placement(placements[1], ViewportPaneId::Side, 0.40f, 0.0f,
                            0.60f, 0.60f);
  expect_viewport_placement(placements[2], ViewportPaneId::Perspective, 0.0f,
                            0.60f, 1.0f, 0.40f);

  placements =
      viewport_layout_placements(ViewportLayoutId::ThreePrimaryTop, state);
  ASSERT_EQ(placements.size(), 3u);
  expect_viewport_placement(placements[0], ViewportPaneId::Perspective, 0.0f,
                            0.0f, 1.0f, 0.60f);
  expect_viewport_placement(placements[1], ViewportPaneId::Top, 0.0f, 0.60f,
                            0.40f, 0.40f);
  expect_viewport_placement(placements[2], ViewportPaneId::Side, 0.40f, 0.60f,
                            0.60f, 0.40f);

  placements = viewport_layout_placements(ViewportLayoutId::FourQuad, state);
  ASSERT_EQ(placements.size(), 4u);
  expect_viewport_placement(placements[0], ViewportPaneId::Perspective, 0.0f,
                            0.0f, 0.30f, 0.70f);
  expect_viewport_placement(placements[1], ViewportPaneId::Top, 0.30f, 0.0f,
                            0.70f, 0.70f);
  expect_viewport_placement(placements[2], ViewportPaneId::Front, 0.0f, 0.70f,
                            0.30f, 0.30f);
  expect_viewport_placement(placements[3], ViewportPaneId::Side, 0.30f, 0.70f,
                            0.70f, 0.30f);
}

TEST(ViewportLayoutTests, BuildSplitHandlesMatchAxesAndSpans) {
  const ViewportLayoutState state = make_distinct_viewport_layout_state();

  EXPECT_TRUE(
      viewport_layout_split_handles(ViewportLayoutId::Single, state).empty());

  std::vector<ViewportSplitHandle> handles =
      viewport_layout_split_handles(ViewportLayoutId::TwoHorizontal, state);
  ASSERT_EQ(handles.size(), 1u);
  expect_viewport_split_handle(handles[0], ViewportSplitHandleId::TwoHorizontal,
                               ViewportSplitHandleAxis::Vertical, 0.25f, 0.0f,
                               1.0f);

  handles = viewport_layout_split_handles(ViewportLayoutId::TwoVertical, state);
  ASSERT_EQ(handles.size(), 1u);
  expect_viewport_split_handle(handles[0], ViewportSplitHandleId::TwoVertical,
                               ViewportSplitHandleAxis::Horizontal, 0.35f, 0.0f,
                               1.0f);

  handles =
      viewport_layout_split_handles(ViewportLayoutId::ThreePrimaryRight, state);
  ASSERT_EQ(handles.size(), 2u);
  expect_viewport_split_handle(handles[0], ViewportSplitHandleId::ThreePrimary,
                               ViewportSplitHandleAxis::Vertical, 0.60f, 0.0f,
                               1.0f);
  expect_viewport_split_handle(
      handles[1], ViewportSplitHandleId::ThreeSecondary,
      ViewportSplitHandleAxis::Horizontal, 0.40f, 0.0f, 0.60f);

  handles =
      viewport_layout_split_handles(ViewportLayoutId::ThreePrimaryLeft, state);
  ASSERT_EQ(handles.size(), 2u);
  expect_viewport_split_handle(handles[0], ViewportSplitHandleId::ThreePrimary,
                               ViewportSplitHandleAxis::Vertical, 0.60f, 0.0f,
                               1.0f);
  expect_viewport_split_handle(
      handles[1], ViewportSplitHandleId::ThreeSecondary,
      ViewportSplitHandleAxis::Horizontal, 0.40f, 0.60f, 1.0f);

  handles = viewport_layout_split_handles(ViewportLayoutId::ThreePrimaryBottom,
                                          state);
  ASSERT_EQ(handles.size(), 2u);
  expect_viewport_split_handle(handles[0], ViewportSplitHandleId::ThreePrimary,
                               ViewportSplitHandleAxis::Horizontal, 0.60f, 0.0f,
                               1.0f);
  expect_viewport_split_handle(
      handles[1], ViewportSplitHandleId::ThreeSecondary,
      ViewportSplitHandleAxis::Vertical, 0.40f, 0.0f, 0.60f);

  handles =
      viewport_layout_split_handles(ViewportLayoutId::ThreePrimaryTop, state);
  ASSERT_EQ(handles.size(), 2u);
  expect_viewport_split_handle(handles[0], ViewportSplitHandleId::ThreePrimary,
                               ViewportSplitHandleAxis::Horizontal, 0.60f, 0.0f,
                               1.0f);
  expect_viewport_split_handle(
      handles[1], ViewportSplitHandleId::ThreeSecondary,
      ViewportSplitHandleAxis::Vertical, 0.40f, 0.60f, 1.0f);

  handles = viewport_layout_split_handles(ViewportLayoutId::FourQuad, state);
  ASSERT_EQ(handles.size(), 2u);
  expect_viewport_split_handle(handles[0], ViewportSplitHandleId::FourVertical,
                               ViewportSplitHandleAxis::Vertical, 0.30f, 0.0f,
                               1.0f);
  expect_viewport_split_handle(
      handles[1], ViewportSplitHandleId::FourHorizontal,
      ViewportSplitHandleAxis::Horizontal, 0.70f, 0.0f, 1.0f);
}

TEST(ViewportLayoutTests, SplitStateDefaultsAndClamps) {
  ViewportLayoutState state;

  EXPECT_FLOAT_EQ(state.two_horizontal_split(), 0.5f);
  EXPECT_FLOAT_EQ(state.two_vertical_split(), 0.5f);
  EXPECT_FLOAT_EQ(state.three_primary_split(), 0.5f);
  EXPECT_FLOAT_EQ(state.three_secondary_split(), 0.5f);
  EXPECT_FLOAT_EQ(state.four_vertical_split(), 0.5f);
  EXPECT_FLOAT_EQ(state.four_horizontal_split(), 0.5f);

  state.set_handle_position(ViewportSplitHandleId::TwoHorizontal, -1.0f);
  state.set_handle_position(ViewportSplitHandleId::TwoVertical, 2.0f);
  state.set_handle_position(ViewportSplitHandleId::ThreePrimary, 0.14f);
  state.set_handle_position(ViewportSplitHandleId::ThreeSecondary, 0.86f);
  state.set_handle_position(ViewportSplitHandleId::FourVertical, 0.40f);
  state.set_handle_position(ViewportSplitHandleId::FourHorizontal, 0.65f);

  EXPECT_FLOAT_EQ(state.two_horizontal_split(), ViewportLayoutState::kMinSplit);
  EXPECT_FLOAT_EQ(state.two_vertical_split(), ViewportLayoutState::kMaxSplit);
  EXPECT_FLOAT_EQ(state.three_primary_split(), ViewportLayoutState::kMinSplit);
  EXPECT_FLOAT_EQ(state.three_secondary_split(),
                  ViewportLayoutState::kMaxSplit);
  EXPECT_FLOAT_EQ(state.four_vertical_split(), 0.40f);
  EXPECT_FLOAT_EQ(state.four_horizontal_split(), 0.65f);
}

TEST(ViewportWorkspaceServiceTests,
     EmbeddedSinglePanePolicyRejectsMultiPaneLayouts) {
  ViewportWorkspaceService workspace(
      ViewportWorkspaceLayoutPolicy::SinglePaneOnly);

  EXPECT_EQ(workspace.layout_policy(),
            ViewportWorkspaceLayoutPolicy::SinglePaneOnly);
  EXPECT_TRUE(workspace.supports_layout(ViewportLayoutId::Single));
  EXPECT_FALSE(workspace.supports_layout(ViewportLayoutId::FourQuad));
  EXPECT_FALSE(workspace.set_layout(ViewportLayoutId::FourQuad));
  EXPECT_EQ(workspace.layout(), ViewportLayoutId::Single);

  std::vector<ViewportPanePlacement> placements =
      workspace.active_pane_placements();
  ASSERT_EQ(placements.size(), 1u);
  expect_viewport_placement(placements[0], ViewportPaneId::Perspective, 0.0f,
                            0.0f, 1.0f, 1.0f);
}

TEST(ViewportWorkspaceServiceTests, MaximizedPaneStateOwnsActiveLayout) {
  ViewportWorkspaceService workspace(ViewportWorkspaceLayoutPolicy::MultiPane);
  EXPECT_EQ(workspace.grid_preset(), viewport_default_grid_preset());
  ASSERT_TRUE(workspace.set_layout(ViewportLayoutId::FourQuad));
  ASSERT_TRUE(workspace.focus_pane(ViewportPaneId::Side));

  EXPECT_FALSE(workspace.focused_pane_maximized());
  EXPECT_EQ(workspace.active_pane_placements().size(), 4u);
  EXPECT_EQ(workspace.active_split_handles().size(), 2u);

  ASSERT_TRUE(workspace.toggle_focused_pane_maximized());

  EXPECT_TRUE(workspace.focused_pane_maximized());
  std::vector<ViewportPanePlacement> placements =
      workspace.active_pane_placements();
  ASSERT_EQ(placements.size(), 1u);
  expect_viewport_placement(placements[0], ViewportPaneId::Side, 0.0f, 0.0f,
                            1.0f, 1.0f);
  EXPECT_TRUE(workspace.active_split_handles().empty());

  ASSERT_TRUE(workspace.set_focused_pane_maximized(false));

  EXPECT_FALSE(workspace.focused_pane_maximized());
  EXPECT_EQ(workspace.active_pane_placements().size(), 4u);
  EXPECT_EQ(workspace.active_split_handles().size(), 2u);
}

TEST(ViewportWorkspaceServiceTests, GridPresetIsClampedAtWorkspaceOwner) {
  ViewportWorkspaceService workspace;
  ASSERT_TRUE(workspace.set_grid_preset(-50));
  EXPECT_EQ(workspace.grid_preset(), 1);

  ASSERT_TRUE(workspace.set_grid_preset(999));
  EXPECT_EQ(
      workspace.grid_preset(),
      static_cast<int>(quader_host_contract::grid_preset_catalog().size()));

  ViewportInputRouter router;
  router.sync_grid_preset_snapshot(999);
  EXPECT_EQ(
      router.state().grid_preset,
      static_cast<int>(quader_host_contract::grid_preset_catalog().size()));

  const std::string router_source =
      read_source_file("src/viewport/viewport_input_router.cpp");
  const std::string runtime_source =
      read_source_file("src/editor/tools/viewport_tool_runtime.cpp");
  const std::string viewport_panel_source =
      read_source_file("src/ui/viewport_panel.cpp");

  EXPECT_NE(
      router_source.find(
          "void ViewportInputRouter::sync_grid_preset_snapshot(int preset)"),
      std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      runtime_source, "result.requested_grid_preset = "
                      "viewport_clamp_grid_preset(shortcut.grid_preset)"));
  EXPECT_EQ(
      runtime_source.find("state.grid_preset = "
                          "viewport_clamp_grid_preset(shortcut.grid_preset)"),
      std::string::npos);
  EXPECT_NE(
      viewport_panel_source.find(
          "viewport_workspace.set_grid_preset(result.requested_grid_preset)"),
      std::string::npos);
  EXPECT_NE(
      viewport_panel_source.find("input_router_.sync_grid_preset_snapshot("
                                 "viewport_workspace.grid_preset())"),
      std::string::npos);
}

TEST(ViewportWorkspaceServiceTests,
     WorkspaceFocusDistancesUseHostViewportDefaults) {
  const std::string source =
      read_source_file("src/viewport/viewport_workspace_service.cpp");
  EXPECT_NE(source.find("viewport_host_defaults().focus_origin_distance"),
            std::string::npos);
  EXPECT_EQ(source.find("focus_origin(64.0f)"), std::string::npos);
}

TEST(ViewportCameraTests, FlySpeedDefaultsUseHostViewportDefaults) {
  const ViewportHostDefaults defaults = viewport_host_defaults();
  EXPECT_EQ(defaults.fly_base_speed,
            quader_host_contract::viewport_defaults().fly_base_speed);
  EXPECT_EQ(defaults.fly_fast_multiplier,
            quader_host_contract::viewport_defaults().fly_fast_multiplier);
  EXPECT_EQ(defaults.fly_slow_multiplier,
            quader_host_contract::viewport_defaults().fly_slow_multiplier);

  ViewportCameraController camera;
  EXPECT_NEAR(camera.fly_speed(), static_cast<float>(defaults.fly_base_speed),
              0.001f);

  const std::string source =
      read_source_file("src/viewport/viewport_camera_controller.cpp");
  EXPECT_EQ(source.find("kDefaultFlySpeed"), std::string::npos);
  EXPECT_NE(source.find("viewport_host_defaults().fly_base_speed"),
            std::string::npos);
}

TEST(ViewportWorkspaceServiceTests, LayoutChangeClearsMaximizedPaneState) {
  ViewportWorkspaceService workspace(ViewportWorkspaceLayoutPolicy::MultiPane);
  ASSERT_TRUE(workspace.set_layout(ViewportLayoutId::FourQuad));
  ASSERT_TRUE(workspace.focus_pane(ViewportPaneId::Side));
  ASSERT_TRUE(workspace.set_focused_pane_maximized(true));

  ASSERT_TRUE(workspace.set_layout(ViewportLayoutId::Single));

  EXPECT_FALSE(workspace.focused_pane_maximized());
  EXPECT_EQ(workspace.focused_pane(), ViewportPaneId::Perspective);
  std::vector<ViewportPanePlacement> placements =
      workspace.active_pane_placements();
  ASSERT_EQ(placements.size(), 1u);
  expect_viewport_placement(placements[0], ViewportPaneId::Perspective, 0.0f,
                            0.0f, 1.0f, 1.0f);
  EXPECT_TRUE(workspace.active_split_handles().empty());
}

TEST(ViewportWorkspaceServiceTests,
     ViewReflectsCameraMutationsOutsideSetViewForConstPaneState) {
  ViewportWorkspaceService workspace;
  ASSERT_TRUE(
      workspace.set_view(ViewportPaneId::Perspective, ViewportView::Top));
  ASSERT_EQ(workspace.view(ViewportPaneId::Perspective), ViewportView::Top);

  reset_viewport_camera_for_view(workspace.camera(ViewportPaneId::Perspective),
                                 ViewportView::Front);
  const ViewportWorkspaceService &const_workspace = workspace;
  const ViewportWorkspacePaneState &const_pane =
      const_workspace.pane(ViewportPaneId::Perspective);

  EXPECT_EQ(const_workspace.view(ViewportPaneId::Perspective),
            ViewportView::Front);
  EXPECT_EQ(viewport_view_for_camera(const_pane.camera), ViewportView::Front);
}

TEST(ViewportWorkspaceServiceTests,
     AuxiliaryViewportRecordsOwnWindowedCameraAndRenderState) {
  ViewportWorkspaceService workspace;

  ViewportWorkspaceAuxiliaryViewportState &created =
      workspace.ensure_auxiliary_viewport("viewport-2", 1u);
  EXPECT_EQ(created.stable_id, "viewport-2");
  EXPECT_EQ(created.layer_index, 1u);
  EXPECT_EQ(workspace.auxiliary_viewport_count(), 1u);
  EXPECT_NE(workspace.auxiliary_viewport("viewport-2"), nullptr);
  EXPECT_EQ(workspace.auxiliary_viewport("missing"), nullptr);

  EXPECT_TRUE(workspace.set_auxiliary_viewport_render_size("viewport-2",
                                                           {640.0f, 360.0f}));
  ASSERT_NE(workspace.auxiliary_viewport("viewport-2"), nullptr);
  EXPECT_EQ(workspace.auxiliary_viewport("viewport-2")->render_size.x, 640.0f);
  EXPECT_EQ(workspace.auxiliary_viewport("viewport-2")->render_size.y, 360.0f);
  EXPECT_EQ(workspace.auxiliary_viewport_view("viewport-2"),
            ViewportView::Perspective);
  EXPECT_TRUE(
      workspace.set_auxiliary_viewport_view("viewport-2", ViewportView::Left));
  EXPECT_EQ(workspace.auxiliary_viewport_view("viewport-2"),
            ViewportView::Left);
  EXPECT_FALSE(
      workspace.set_auxiliary_viewport_view("viewport-2", ViewportView::Left));
  EXPECT_FALSE(
      workspace.set_auxiliary_viewport_view("missing", ViewportView::Top));
  workspace.set_auxiliary_viewport_visible("viewport-2", true);
  EXPECT_TRUE(workspace.auxiliary_viewport_visible("viewport-2"));

  workspace.clear_pane_visibility();
  EXPECT_FALSE(workspace.auxiliary_viewport_visible("viewport-2"));

  ViewportWorkspaceAuxiliaryViewportState &existing =
      workspace.ensure_auxiliary_viewport("viewport-2", 2u);
  EXPECT_EQ(workspace.auxiliary_viewport_count(), 1u);
  EXPECT_EQ(existing.layer_index, 2u);

  EXPECT_TRUE(workspace.remove_auxiliary_viewport("viewport-2"));
  EXPECT_EQ(workspace.auxiliary_viewport_count(), 0u);
  EXPECT_FALSE(workspace.remove_auxiliary_viewport("viewport-2"));
}

TEST(ViewportWorkspaceServiceTests, PaneStateDoesNotStoreDuplicateView) {
  const std::string header =
      read_source_file("src/viewport/viewport_workspace_service.hpp");
  const std::string source =
      read_source_file("src/viewport/viewport_workspace_service.cpp");

  EXPECT_EQ(header.find("ViewportView view ="), std::string::npos);
  EXPECT_EQ(source.find("pane_state.view"), std::string::npos);
}

TEST(ViewportCameraTests, OrbitPanAndZoomChangePose) {
  ViewportCameraController camera;
  const auto before = camera.pose();
  const float initial_pitch = camera.state().pitch_radians;
  const float initial_distance = camera.state().distance;

  camera.set_viewport_size(800.0f, 600.0f);
  camera.begin_drag({100.0f, 100.0f}, ViewportDragMode::Orbit);
  camera.update_drag({160.0f, 120.0f});
  camera.end_drag();
  camera.scroll(1.0f);

  const auto after = camera.pose();
  EXPECT_NE(before.eye.x, after.eye.x);
  EXPECT_LT(camera.state().pitch_radians, initial_pitch);
  EXPECT_LT(camera.state().distance, initial_distance);
}

TEST(ViewportCameraTests, DefaultPerspectiveMatchesViewportContract) {
  ViewportCameraController camera;
  const ViewportCameraPose pose = camera.pose();

  EXPECT_EQ(camera.state().projection, ViewportProjection::Perspective);
  EXPECT_NEAR(camera.state().fov_degrees, 60.0f, 0.001f);
  EXPECT_NEAR(camera.state().distance, 11.18034f, 0.001f);
  EXPECT_NEAR(pose.eye.x, 6.0f, 0.001f);
  EXPECT_NEAR(pose.eye.y, 5.0f, 0.001f);
  EXPECT_NEAR(pose.eye.z, 8.0f, 0.001f);
}

TEST(ViewportCameraTests, CtrlWheelZoomsTowardCursor) {
  ViewportCameraController camera;
  const float initial_distance = camera.state().distance;
  camera.set_viewport_size(800.0f, 600.0f);

  camera.scroll_to_cursor(1.0f, {700.0f, 100.0f});

  EXPECT_LT(camera.state().distance, initial_distance);
  EXPECT_GT(camera.state().target.x, 0.0f);
  EXPECT_GT(camera.state().target.y, 0.0f);
}

TEST(ViewportCameraTests, BuildsPickingRaysFromViewportPoints) {
  ViewportCameraController camera;
  camera.set_viewport_size(800.0f, 600.0f);

  const ViewportRay center_ray =
      camera.ray_from_viewport_point({400.0f, 300.0f});
  const ViewportCameraPose pose = camera.pose();
  const QVec3 forward = normalize(pose.target - pose.eye);

  EXPECT_NEAR(center_ray.origin.x, pose.eye.x, 0.001f);
  EXPECT_NEAR(center_ray.origin.y, pose.eye.y, 0.001f);
  EXPECT_NEAR(center_ray.origin.z, pose.eye.z, 0.001f);
  EXPECT_NEAR(dot(center_ray.direction, forward), 1.0f, 0.001f);

  const ViewportRay corner_ray = camera.ray_from_viewport_point({800.0f, 0.0f});
  EXPECT_GT(corner_ray.direction.x, center_ray.direction.x);
  EXPECT_GT(corner_ray.direction.y, center_ray.direction.y);
  EXPECT_NEAR(length(corner_ray.direction), 1.0f, 0.001f);
}

TEST(ViewportCameraTests, ProjectsWorldPointsToViewportPixels) {
  ViewportCameraController camera;
  camera.set_viewport_size(800.0f, 600.0f);

  const ViewportScreenPoint center =
      camera.project_world_point(camera.pose().target);
  ASSERT_TRUE(center.visible);
  EXPECT_NEAR(center.position.x, 400.0f, 0.001f);
  EXPECT_NEAR(center.position.y, 300.0f, 0.001f);

  const ViewportCameraPose pose = camera.pose();
  const QVec3 forward = normalize(pose.target - pose.eye);
  const QVec3 right = normalize(cross(forward, pose.up));
  const QVec3 up = normalize(cross(right, forward));
  const ViewportScreenPoint right_up =
      camera.project_world_point(pose.target + right * 0.5f + up * 0.5f);

  ASSERT_TRUE(right_up.visible);
  EXPECT_GT(right_up.position.x, center.position.x);
  EXPECT_LT(right_up.position.y, center.position.y);
  EXPECT_GT(right_up.depth, 0.0f);
}

TEST(ViewportCameraTests, ClipPlanesSanitizeAndControlProjectedVisibility) {
  ViewportCameraController camera;
  camera.set_viewport_size(800.0f, 600.0f);
  camera.set_clip_planes(0.01f, 0.2f);

  EXPECT_FLOAT_EQ(camera.state().near_plane, 0.01f);
  EXPECT_FLOAT_EQ(camera.state().far_plane, 0.2f);

  const ViewportCameraPose pose = camera.pose();
  const QVec3 forward = normalize(pose.target - pose.eye);

  EXPECT_FALSE(camera.project_world_point(pose.eye + forward * 0.005f).visible);
  EXPECT_TRUE(camera.project_world_point(pose.eye + forward * 0.02f).visible);
  EXPECT_FALSE(camera.project_world_point(pose.eye + forward * 0.25f).visible);

  camera.set_clip_planes(-1.0f, -2.0f);
  EXPECT_FLOAT_EQ(camera.state().near_plane, 0.001f);
  EXPECT_FLOAT_EQ(camera.state().far_plane, 0.002f);
}

TEST(ViewportCameraTests, FocusBoundsFramesDocumentCenter) {
  ViewportCameraController camera;

  camera.focus_bounds({10.0f, -2.0f, -4.0f}, {14.0f, 2.0f, 4.0f});

  EXPECT_NEAR(camera.state().target.x, 12.0f, 0.001f);
  EXPECT_NEAR(camera.state().target.y, 0.0f, 0.001f);
  EXPECT_NEAR(camera.state().target.z, 0.0f, 0.001f);
  EXPECT_GT(camera.state().distance, 4.0f);
}

TEST(ViewportCameraTests, SetViewDirectionKeepsTargetDistanceAndSnapsAxis) {
  ViewportCameraController camera;
  camera.focus_bounds({10.0f, -2.0f, -4.0f}, {14.0f, 2.0f, 4.0f});

  const QVec3 target_before = camera.state().target;
  const float distance_before = camera.state().distance;
  camera.set_view_direction({-1.0f, 0.0f, 0.0f});

  const ViewportCameraPose pose = camera.pose();
  const QVec3 forward = normalize(pose.target - pose.eye);
  EXPECT_NEAR(camera.state().target.x, target_before.x, 0.001f);
  EXPECT_NEAR(camera.state().target.y, target_before.y, 0.001f);
  EXPECT_NEAR(camera.state().target.z, target_before.z, 0.001f);
  EXPECT_FLOAT_EQ(camera.state().distance, distance_before);
  EXPECT_NEAR(forward.x, -1.0f, 0.001f);
  EXPECT_NEAR(forward.y, 0.0f, 0.001f);
  EXPECT_NEAR(forward.z, 0.0f, 0.001f);
}

TEST(ViewportCameraTests, ViewAxisUsesRuntimeNavigationPolicy) {
  ViewportCameraController camera;
  camera.focus_bounds({10.0f, -2.0f, -4.0f}, {14.0f, 2.0f, 4.0f});

  const QVec3 target_before = camera.state().target;
  const float distance_before = camera.state().distance;
  camera.view_axis(ViewportViewAxis::Y, 1);

  const ViewportCameraPose pose = camera.pose();
  const QVec3 forward = normalize(pose.target - pose.eye);
  EXPECT_NEAR(camera.state().target.x, target_before.x, 0.001f);
  EXPECT_NEAR(camera.state().target.y, target_before.y, 0.001f);
  EXPECT_NEAR(camera.state().target.z, target_before.z, 0.001f);
  EXPECT_NEAR(camera.state().distance, distance_before, 0.001f);
  EXPECT_NEAR(forward.x, 0.0f, 0.001f);
  EXPECT_NEAR(forward.y, -1.0f, 0.001f);
  EXPECT_NEAR(forward.z, 0.0f, 0.001f);
  EXPECT_EQ(camera.state().projection, ViewportProjection::Orthographic);
  EXPECT_TRUE(camera.orthographic_view_locked());
  EXPECT_GT(camera.state().orthographic_size, 0.0f);
  EXPECT_GT(length(pose.up), 0.99f);
}

TEST(ViewportCameraTests, ViewCubeAxisSnapDoesNotLockOrbit) {
  ViewportCameraController camera;
  camera.set_viewport_size(800.0f, 600.0f);
  camera.view_axis(ViewportViewAxis::Z, 1, ViewportAxisLock::Unlocked);

  ASSERT_EQ(camera.state().projection, ViewportProjection::Orthographic);
  ASSERT_FALSE(camera.orthographic_view_locked());

  camera.begin_drag({100.0f, 100.0f}, ViewportDragMode::Orbit);
  camera.update_drag({160.0f, 120.0f});
  camera.end_drag();

  EXPECT_EQ(camera.state().projection, ViewportProjection::Perspective);
  EXPECT_FALSE(camera.orthographic_view_locked());
}

TEST(ViewportCameraTests, ViewCubePrimitivesExposeVisibleAxisEdges) {
  quader_editor_core::ViewCubePrimitiveInput input;
  input.viewport_size = {200.0f, 200.0f};
  input.widget_size = 70.0;
  input.camera_transform.x_axis = {0.70710678f, 0.0f, -0.70710678f};
  input.camera_transform.y_axis = {-0.40824829f, 0.81649658f, -0.40824829f};
  input.camera_transform.z_axis = {0.57735027f, 0.57735027f, 0.57735027f};

  const quader_editor_core::ViewCubePrimitives primitives =
      quader_editor_core::build_view_cube_primitives(input);

  EXPECT_TRUE(primitives.ok);
  EXPECT_EQ(primitives.faces.size(), 3u);
  EXPECT_EQ(primitives.edges.size(), 9u);

  std::array<bool, 3> has_axis = {false, false, false};
  for (const quader_editor_core::ViewCubeEdgePrimitive &edge :
       primitives.edges) {
    const float dx = edge.screen_end.x - edge.screen_start.x;
    const float dy = edge.screen_end.y - edge.screen_start.y;
    EXPECT_GT(dx * dx + dy * dy, 1.0f);
    switch (edge.axis) {
    case quader_editor_core::ViewAxis::X:
      has_axis[0] = true;
      break;
    case quader_editor_core::ViewAxis::Y:
      has_axis[1] = true;
      break;
    case quader_editor_core::ViewAxis::Z:
      has_axis[2] = true;
      break;
    }
  }
  EXPECT_TRUE(has_axis[0]);
  EXPECT_TRUE(has_axis[1]);
  EXPECT_TRUE(has_axis[2]);

  EXPECT_EQ(primitives.axis_edges.size(), 3u);
  for (const quader_editor_core::ViewCubeEdgePrimitive &edge :
       primitives.axis_edges) {
    EXPECT_FLOAT_EQ(edge.local_start.x, 1.0f);
    EXPECT_FLOAT_EQ(edge.local_start.y, -1.0f);
    EXPECT_FLOAT_EQ(edge.local_start.z, 1.0f);
  }
}

TEST(ViewportCameraTests, OrthographicAxisViewBuildsParallelPickingRays) {
  ViewportCameraController camera;
  camera.set_viewport_size(800.0f, 600.0f);
  camera.view_axis(ViewportViewAxis::Z, 1);

  ASSERT_EQ(camera.state().projection, ViewportProjection::Orthographic);
  const ViewportRay center_ray =
      camera.ray_from_viewport_point({400.0f, 300.0f});
  const ViewportRay corner_ray = camera.ray_from_viewport_point({800.0f, 0.0f});
  const ViewportCameraPose pose = camera.pose();
  const QVec3 forward = normalize(pose.target - pose.eye);

  EXPECT_NEAR(dot(center_ray.direction, forward), 1.0f, 0.001f);
  EXPECT_NEAR(dot(corner_ray.direction, center_ray.direction), 1.0f, 0.001f);
  EXPECT_NE(center_ray.origin.x, corner_ray.origin.x);
  EXPECT_NE(center_ray.origin.y, corner_ray.origin.y);
}

TEST(ViewportCameraTests, FocusOriginResetsOrthographicSize) {
  ViewportCameraController camera;
  camera.set_viewport_size(800.0f, 600.0f);
  camera.view_axis(ViewportViewAxis::Y, 1);
  ASSERT_EQ(camera.state().projection, ViewportProjection::Orthographic);
  const float initial_orthographic_size = camera.state().orthographic_size;

  camera.scroll(3.0f);
  ASSERT_NE(camera.state().orthographic_size, initial_orthographic_size);

  camera.focus_origin();

  EXPECT_NEAR(camera.state().target.x, 0.0f, 0.001f);
  EXPECT_NEAR(camera.state().target.y, 0.0f, 0.001f);
  EXPECT_NEAR(camera.state().target.z, 0.0f, 0.001f);
  EXPECT_NEAR(camera.state().distance, 64.0f, 0.001f);
  EXPECT_NEAR(camera.state().orthographic_size, 64.0f, 0.001f);
}

TEST(ViewportCameraTests,
     SetViewDirectionClampsVerticalViewWithoutDegenerateBasis) {
  ViewportCameraController camera;
  camera.set_view_direction({0.0f, -1.0f, 0.0f});

  const ViewportCameraPose pose = camera.pose();
  const QVec3 forward = normalize(pose.target - pose.eye);
  EXPECT_LT(forward.y, -0.99f);
  EXPECT_GT(length(pose.up), 0.99f);
}

TEST(ViewportCameraTests, OrbitSnapUsesRuntimeOrthogonalSnap) {
  ViewportCameraController camera;
  camera.focus_origin(8.0f);
  camera.set_view_direction({0.02f, 0.0f, -1.0f});

  camera.begin_drag({100.0f, 100.0f}, ViewportDragMode::OrbitSnap);
  camera.update_drag({100.0f, 100.0f});
  camera.end_drag();

  const ViewportCameraPose pose = camera.pose();
  const QVec3 forward = normalize(pose.target - pose.eye);
  EXPECT_NEAR(forward.x, 0.0f, 0.001f);
  EXPECT_NEAR(forward.y, 0.0f, 0.001f);
  EXPECT_NEAR(forward.z, -1.0f, 0.001f);
  EXPECT_EQ(camera.state().projection, ViewportProjection::Orthographic);
  EXPECT_FALSE(camera.orthographic_view_locked());
  EXPECT_GT(camera.state().orthographic_size, 0.0f);

  camera.begin_drag({100.0f, 100.0f}, ViewportDragMode::Orbit);
  camera.update_drag({118.0f, 100.0f});
  camera.end_drag();
  EXPECT_EQ(camera.state().projection, ViewportProjection::Perspective);
}

TEST(ViewportCameraTests, OrbitSnapPreservesPerspectiveZoomLevel) {
  ViewportCameraController camera;
  camera.set_viewport_size(800.0f, 600.0f);
  camera.focus_origin(30.0f);
  camera.scroll(3.0f);
  camera.set_view_direction({0.02f, 0.0f, -1.0f});

  const float expected_visible_height =
      2.0f * camera.state().distance *
      std::tan(camera.state().fov_degrees *
               static_cast<float>(std::acos(-1.0)) / 360.0f);

  camera.begin_drag({100.0f, 100.0f}, ViewportDragMode::Orbit);
  camera.update_drag({100.0f, 100.0f}, ViewportDragMode::OrbitSnap);
  camera.end_drag();

  EXPECT_EQ(camera.state().projection, ViewportProjection::Orthographic);
  EXPECT_FALSE(camera.orthographic_view_locked());
  EXPECT_NEAR(camera.state().orthographic_size, expected_visible_height,
              0.001f);
}

TEST(ViewportCameraTests,
     OrbitSnapTransitionSamplesCurrentViewBeforeMouseDelta) {
  ViewportCameraController camera;
  camera.set_viewport_size(800.0f, 600.0f);
  camera.focus_origin(8.0f);
  camera.set_view_direction({0.02f, 0.0f, -1.0f});

  camera.begin_drag({100.0f, 100.0f}, ViewportDragMode::Orbit);
  camera.update_drag({220.0f, 100.0f}, ViewportDragMode::OrbitSnap);
  camera.end_drag();

  const QVec3 forward = normalize(camera.pose().target - camera.pose().eye);
  EXPECT_EQ(camera.state().projection, ViewportProjection::Orthographic);
  EXPECT_FALSE(camera.orthographic_view_locked());
  EXPECT_NEAR(forward.x, 0.0f, 0.001f);
  EXPECT_NEAR(forward.y, 0.0f, 0.001f);
  EXPECT_NEAR(forward.z, -1.0f, 0.001f);
}

TEST(ViewportCameraTests, OrbitSnapUsesViewAxisWhenNavigationPivotIsOffCenter) {
  ViewportCameraController camera;
  camera.set_viewport_size(800.0f, 600.0f);
  camera.focus_origin(8.0f);
  camera.set_view_direction({0.02f, 0.0f, -1.0f});
  camera.scroll_to_cursor(1.0f, {700.0f, 100.0f});
  ASSERT_GT(length(camera.state().target), 0.001f);

  camera.begin_drag({100.0f, 100.0f}, ViewportDragMode::Orbit);
  camera.update_drag({100.0f, 100.0f}, ViewportDragMode::OrbitSnap);
  camera.end_drag();

  const QVec3 forward = normalize(camera.pose().target - camera.pose().eye);
  EXPECT_EQ(camera.state().projection, ViewportProjection::Orthographic);
  EXPECT_FALSE(camera.orthographic_view_locked());
  EXPECT_NEAR(forward.x, 0.0f, 0.001f);
  EXPECT_NEAR(forward.y, 0.0f, 0.001f);
  EXPECT_NEAR(forward.z, -1.0f, 0.001f);
}

TEST(ViewportCameraTests, OrbitFromUnlockedOrthographicPreservesZoomLevel) {
  ViewportCameraController camera;
  camera.set_viewport_size(800.0f, 600.0f);
  camera.focus_origin(30.0f);
  camera.view_axis(ViewportViewAxis::Z, 1, ViewportAxisLock::Unlocked);
  camera.scroll(3.0f);

  const float orthographic_size = camera.state().orthographic_size;

  camera.begin_drag({100.0f, 100.0f}, ViewportDragMode::Orbit);
  camera.update_drag({118.0f, 100.0f});
  camera.end_drag();

  const float visible_height =
      2.0f * camera.state().distance *
      std::tan(camera.state().fov_degrees *
               static_cast<float>(std::acos(-1.0)) / 360.0f);
  EXPECT_EQ(camera.state().projection, ViewportProjection::Perspective);
  EXPECT_FALSE(camera.orthographic_view_locked());
  EXPECT_NEAR(visible_height, orthographic_size, 0.001f);
}

TEST(ViewportCameraTests, OrbitSnapUsesBlenderDiagonalAxisTargets) {
  ViewportCameraController camera;
  camera.focus_origin(8.0f);
  camera.set_view_direction({-0.63f, 0.02f, -0.78f});

  camera.begin_drag({100.0f, 100.0f}, ViewportDragMode::OrbitSnap);
  camera.update_drag({100.0f, 100.0f});
  camera.end_drag();

  const ViewportCameraPose pose = camera.pose();
  const QVec3 forward = normalize(pose.target - pose.eye);
  constexpr float kDiagonalComponent = 0.70710678f;
  EXPECT_NEAR(forward.x, -kDiagonalComponent, 0.001f);
  EXPECT_NEAR(forward.y, 0.0f, 0.001f);
  EXPECT_NEAR(forward.z, -kDiagonalComponent, 0.001f);
  EXPECT_EQ(camera.state().projection, ViewportProjection::Perspective);
  EXPECT_FALSE(camera.orthographic_view_locked());
}

TEST(ViewportCameraTests, FlyLookKeepsEyeFixedAndFlyMoveAdvancesCamera) {
  ViewportCameraController camera;
  camera.set_viewport_size(800.0f, 600.0f);

  const ViewportCameraPose start_pose = camera.pose();
  camera.begin_drag({100.0f, 100.0f}, ViewportDragMode::Fly);
  camera.update_drag({160.0f, 80.0f});
  const ViewportCameraPose look_pose = camera.pose();
  const QVec3 start_forward = normalize(start_pose.target - start_pose.eye);
  const QVec3 look_forward = normalize(look_pose.target - look_pose.eye);

  EXPECT_NEAR(length(look_pose.eye - start_pose.eye), 0.0f, 0.001f);
  EXPECT_LT(dot(look_forward, start_forward), 0.999f);

  const float default_fly_speed =
      static_cast<float>(viewport_host_defaults().fly_base_speed);
  EXPECT_NEAR(camera.fly_speed(), default_fly_speed, 0.001f);
  camera.adjust_fly_speed(1.0f);
  EXPECT_NEAR(camera.fly_speed(), default_fly_speed + 1.0f, 0.001f);

  camera.fly_move({0.0f, 0.0f, 1.0f}, 0.25f);
  const ViewportCameraPose moved_pose = camera.pose();
  const QVec3 moved_delta = moved_pose.eye - look_pose.eye;

  EXPECT_GT(dot(moved_delta, look_forward), (default_fly_speed + 1.0f) * 0.24f);
  EXPECT_NEAR(length((moved_pose.target - look_pose.target) - moved_delta),
              0.0f, 0.001f);
}

TEST(ViewportCameraTests, FlyLookDoesNotSnapToTopOrBottomView) {
  ViewportCameraController camera;
  camera.set_viewport_size(800.0f, 600.0f);

  camera.begin_drag({100.0f, 100.0f}, ViewportDragMode::Fly);
  camera.update_drag({100.0f, -1200.0f});
  camera.end_drag();

  ViewportCameraPose pose = camera.pose();
  QVec3 forward = normalize(pose.target - pose.eye);
  EXPECT_GT(std::sqrt(forward.x * forward.x + forward.z * forward.z), 0.001f);
  EXPECT_GT(length(pose.up), 0.99f);
  EXPECT_NEAR(dot(forward, pose.up), 0.0f, 0.001f);
  EXPECT_EQ(camera.state().projection, ViewportProjection::Perspective);
  EXPECT_FALSE(camera.orthographic_view_locked());

  camera.begin_drag({100.0f, 100.0f}, ViewportDragMode::Fly);
  camera.update_drag({100.0f, 1400.0f});
  camera.end_drag();

  pose = camera.pose();
  forward = normalize(pose.target - pose.eye);
  EXPECT_GT(std::sqrt(forward.x * forward.x + forward.z * forward.z), 0.001f);
  EXPECT_GT(length(pose.up), 0.99f);
  EXPECT_NEAR(dot(forward, pose.up), 0.0f, 0.001f);
  EXPECT_EQ(camera.state().projection, ViewportProjection::Perspective);
  EXPECT_FALSE(camera.orthographic_view_locked());
}

TEST(ViewportCameraTests,
     PlainMiddleOrbitNearOrthogonalAxisDoesNotSnapToOrthographic) {
  ViewportCameraController camera;
  camera.set_viewport_size(800.0f, 600.0f);
  camera.focus_origin(8.0f);
  camera.set_view_direction({0.02f, 0.0f, -1.0f});

  camera.begin_drag({100.0f, 100.0f}, ViewportDragMode::Orbit);
  camera.update_drag({100.0f, 100.0f});
  camera.end_drag();

  const ViewportCameraPose pose = camera.pose();
  const QVec3 forward = normalize(pose.target - pose.eye);
  EXPECT_EQ(camera.state().projection, ViewportProjection::Perspective);
  EXPECT_FALSE(camera.orthographic_view_locked());
  EXPECT_GT(std::abs(forward.x), 0.001f);
  EXPECT_NEAR(forward.y, 0.0f, 0.001f);
}

TEST(ViewportInputRouterTests, ImplementedToolHotkeysMatchViewportContract) {
  ViewportInputRouter router;
  ViewportCameraController camera;
  ViewportInputFrame input;
  input.viewport_focused = true;
  input.viewport_size = {800.0f, 600.0f};

  set_resolved_shortcut(input, test_tool_shortcut(ViewportTool::kBox));
  auto result = router.handle_frame(input, camera);
  EXPECT_EQ(result.shortcut_handled,
            viewport_tool_has_native_controller(ViewportTool::kBox));
  EXPECT_EQ(router.state().active_tool,
            viewport_tool_has_native_controller(ViewportTool::kBox)
                ? ViewportTool::kBox
                : ViewportTool::kSelect);

  set_resolved_shortcut(input, test_tool_shortcut(ViewportTool::kSelect));
  result = router.handle_frame(input, camera);
  EXPECT_EQ(router.state().active_tool, ViewportTool::kSelect);

  set_resolved_shortcut(input, test_tool_shortcut(ViewportTool::kMove));
  result = router.handle_frame(input, camera);
  EXPECT_EQ(router.state().active_tool, ViewportTool::kMove);

  set_resolved_shortcut(input, test_tool_shortcut(ViewportTool::kRotate));
  result = router.handle_frame(input, camera);
  EXPECT_EQ(router.state().active_tool, ViewportTool::kRotate);

  set_resolved_shortcut(input, test_tool_shortcut(ViewportTool::kScale));
  result = router.handle_frame(input, camera);
  EXPECT_EQ(router.state().active_tool, ViewportTool::kScale);
}

TEST(ViewportInputRouterTests, ResolvedNavigationBindingsDriveMouseNavigation) {
  ViewportInputRouter router;
  ViewportCameraController camera;
  ViewportInputFrame input;
  input.viewport_hovered = true;
  input.viewport_focused = true;
  input.viewport_size = {800.0f, 600.0f};
  input.mouse_screen = {200.0f, 150.0f};

  ViewportInputResult result = router.handle_frame(input, camera);
  EXPECT_FALSE(result.navigation_active);
  EXPECT_FALSE(result.consumed);

  set_navigation_pressed(input, ViewportInputActionKind::Orbit);
  result = router.handle_frame(input, camera);
  EXPECT_TRUE(result.navigation_active);
  EXPECT_TRUE(result.consumed);

  set_navigation_down(input, ViewportInputActionKind::Orbit);
  input.mouse_screen = {220.0f, 165.0f};
  result = router.handle_frame(input, camera);
  EXPECT_TRUE(result.camera_changed);
  EXPECT_TRUE(result.navigation_active);

  set_navigation_released(input, ViewportInputActionKind::Orbit);
  result = router.handle_frame(input, camera);
  EXPECT_FALSE(result.navigation_active);
  EXPECT_TRUE(result.consumed);
}

TEST(ViewportInputRouterTests,
     ResolvedMiddleOrbitSamplesAltDuringDragForAxisSnap) {
  ViewportInputRouter router;
  ViewportCameraController camera;
  camera.set_viewport_size(800.0f, 600.0f);
  camera.focus_origin(8.0f);
  camera.set_view_direction({0.02f, 0.0f, -1.0f});

  ViewportInputFrame input;
  input.viewport_hovered = true;
  input.viewport_focused = true;
  input.viewport_size = {800.0f, 600.0f};
  input.mouse_screen = {100.0f, 100.0f};
  set_navigation_pressed(input, ViewportInputActionKind::Orbit);

  ViewportInputResult result = router.handle_frame(input, camera);
  ASSERT_TRUE(result.consumed);
  ASSERT_TRUE(router.navigating());
  EXPECT_EQ(camera.state().projection, ViewportProjection::Perspective);

  set_navigation_down(input, ViewportInputActionKind::Orbit);
  input.modifiers.alt = true;
  input.mouse_screen = {220.0f, 100.0f};
  result = router.handle_frame(input, camera);

  EXPECT_TRUE(result.camera_changed);
  EXPECT_EQ(camera.state().projection, ViewportProjection::Orthographic);
  EXPECT_FALSE(camera.orthographic_view_locked());

  input.modifiers.alt = false;
  input.mouse_screen = {116.0f, 100.0f};
  result = router.handle_frame(input, camera);

  EXPECT_TRUE(result.camera_changed);
  EXPECT_EQ(camera.state().projection, ViewportProjection::Perspective);
}

TEST(ViewportInputRouterTests, ResolvedWheelBindingsDriveViewportZoom) {
  ViewportInputRouter router;
  ViewportCameraController camera;
  ViewportInputFrame input;
  input.viewport_hovered = true;
  input.viewport_focused = true;
  input.viewport_size = {800.0f, 600.0f};
  input.mouse_local = {400.0f, 300.0f};
  input.mouse_wheel = 1.0f;

  ViewportInputResult result = router.handle_frame(input, camera);
  EXPECT_FALSE(result.camera_changed);
  EXPECT_FALSE(result.consumed);

  input.navigation_bindings.zoom_wheel = true;
  result = router.handle_frame(input, camera);
  EXPECT_TRUE(result.camera_changed);
  EXPECT_TRUE(result.consumed);
  EXPECT_FALSE(result.zoomed_to_cursor);

  input.navigation_bindings.zoom_wheel = false;
  input.navigation_bindings.zoom_to_cursor_wheel = true;
  result = router.handle_frame(input, camera);
  EXPECT_TRUE(result.camera_changed);
  EXPECT_TRUE(result.zoomed_to_cursor);
}

TEST(ViewportInputRouterTests,
     ResolvedResetFlySpeedDoesNotPreemptActiveFlyNavigation) {
  ViewportInputRouter router;
  ViewportCameraController camera;
  camera.adjust_fly_speed(5.0f);
  ASSERT_FALSE(camera.fly_speed_is_default());

  ViewportInputFrame input;
  input.viewport_hovered = true;
  input.viewport_focused = true;
  input.viewport_size = {800.0f, 600.0f};
  input.mouse_screen = {300.0f, 240.0f};
  set_navigation_pressed(input, ViewportInputActionKind::Fly);

  ViewportInputResult result = router.handle_frame(input, camera);
  ASSERT_TRUE(result.fly_navigation_active);

  set_navigation_down(input, ViewportInputActionKind::Fly);
  input.navigation_bindings.orbit_pressed = true;
  input.navigation_bindings.orbit_down = true;
  input.navigation_bindings.reset_fly_speed_pressed = true;
  result = router.handle_frame(input, camera);

  EXPECT_TRUE(result.consumed);
  EXPECT_TRUE(result.fly_navigation_active);
  EXPECT_TRUE(result.fly_speed_changed);
  EXPECT_TRUE(result.fly_speed_reset);
  EXPECT_NEAR(camera.fly_speed(),
              static_cast<float>(viewport_host_defaults().fly_base_speed),
              0.001f);
  EXPECT_TRUE(camera.fly_speed_is_default());
}

TEST(ViewportInputRouterTests,
     ResolvedFlyNavigationKeepsWasdMovementAheadOfToolShortcuts) {
  ViewportInputRouter router;
  ViewportCameraController camera;

  ViewportInputFrame input;
  input.viewport_hovered = true;
  input.viewport_focused = true;
  input.viewport_size = {800.0f, 600.0f};
  input.mouse_screen = {300.0f, 240.0f};
  set_navigation_pressed(input, ViewportInputActionKind::Fly);

  ViewportInputResult result = router.handle_frame(input, camera);
  ASSERT_TRUE(result.fly_navigation_active);

  set_navigation_down(input, ViewportInputActionKind::Fly);
  input.fly_local_direction = {0.0f, 0.0f, 1.0f};
  input.resolved_shortcuts = {{
      .kind = ViewportShortcutKind::ActivateTool,
      .context = ViewportShortcutContext::Viewport,
      .tool = ViewportToolId::Move,
  }};
  result = router.handle_frame(input, camera);

  EXPECT_TRUE(result.consumed);
  EXPECT_TRUE(result.camera_changed);
  EXPECT_TRUE(result.fly_navigation_active);
  EXPECT_FALSE(result.shortcut_handled);
  EXPECT_EQ(router.state().active_tool, ViewportTool::kSelect);
}

TEST(ViewportInputRouterTests, ResolvedFlyNavigationContinuesWhileShiftIsHeld) {
  ViewportInputRouter router;
  ViewportCameraController camera;

  ViewportInputFrame input;
  input.viewport_hovered = true;
  input.viewport_focused = true;
  input.viewport_size = {800.0f, 600.0f};
  input.mouse_screen = {300.0f, 240.0f};
  set_navigation_pressed(input, ViewportInputActionKind::Fly);

  ViewportInputResult result = router.handle_frame(input, camera);
  ASSERT_TRUE(result.fly_navigation_active);

  set_navigation_down(input, ViewportInputActionKind::Fly);
  input.modifiers.shift = true;
  input.fly_local_direction = {0.0f, 0.0f, 1.0f};
  result = router.handle_frame(input, camera);

  EXPECT_TRUE(result.consumed);
  EXPECT_TRUE(result.camera_changed);
  EXPECT_TRUE(result.fly_navigation_active);
}

TEST(ViewportInputRouterTests, ModalToolHotkeysActivateNativeControllers) {
  ViewportInputRouter router;
  ViewportCameraController camera;
  ViewportInputFrame input;
  input.viewport_focused = true;
  input.viewport_size = {800.0f, 600.0f};

  set_resolved_shortcut(input,
                        test_tool_shortcut(ViewportTool::kInsertEdgeLoop));
  ViewportInputResult result = router.handle_frame(input, camera);
  EXPECT_TRUE(result.shortcut_handled);
  EXPECT_EQ(router.state().active_tool, ViewportTool::kInsertEdgeLoop);

  set_resolved_shortcut(input, test_tool_shortcut(ViewportTool::kCut));
  result = router.handle_frame(input, camera);
  EXPECT_TRUE(result.shortcut_handled);
  EXPECT_EQ(router.state().active_tool, ViewportTool::kCut);

  set_resolved_shortcut(input, test_tool_shortcut(ViewportTool::kKnife));
  result = router.handle_frame(input, camera);
  EXPECT_TRUE(result.shortcut_handled);
  EXPECT_EQ(router.state().active_tool, ViewportTool::kKnife);

  set_resolved_shortcut(input, test_tool_shortcut(ViewportTool::kPoly));
  result = router.handle_frame(input, camera);
  EXPECT_TRUE(result.shortcut_handled);
  EXPECT_EQ(router.state().active_tool, ViewportTool::kPoly);
}

TEST(ViewportInputRouterTests, OperationShortcutsMatchViewportContract) {
  /**
   * Stores the Shortcut Case data contract used by the automated test suite.
   */
  struct ShortcutCase {
    const char *label;
    ViewportShortcut shortcut;
  };

  const std::array<ShortcutCase, 21> shortcuts{{
      {"Ctrl+A select all",
       test_router_shortcut(ViewportShortcutKind::SelectAll,
                            ViewportShortcutContext::Global)},
      {"Ctrl+I invert selection",
       test_router_shortcut(ViewportShortcutKind::InvertSelection,
                            ViewportShortcutContext::Global)},
      {"Delete delete selection",
       test_router_shortcut(ViewportShortcutKind::DeleteSelection,
                            ViewportShortcutContext::Global)},
      {"Backspace delete selection",
       test_router_shortcut(ViewportShortcutKind::DeleteSelection,
                            ViewportShortcutContext::Global)},
      {"Ctrl+D duplicate",
       test_router_shortcut(ViewportShortcutKind::Duplicate,
                            ViewportShortcutContext::Global)},
      {"Ctrl+C copy", test_router_shortcut(ViewportShortcutKind::Copy,
                                           ViewportShortcutContext::Global)},
      {"Ctrl+V paste", test_router_shortcut(ViewportShortcutKind::Paste,
                                            ViewportShortcutContext::Global)},
      {"Ctrl+Z undo", test_router_shortcut(ViewportShortcutKind::Undo,
                                           ViewportShortcutContext::Global)},
      {"Ctrl+Y redo", test_router_shortcut(ViewportShortcutKind::Redo,
                                           ViewportShortcutContext::Global)},
      {"Ctrl+Shift+Z redo",
       test_router_shortcut(ViewportShortcutKind::Redo,
                            ViewportShortcutContext::Global)},
      {"Ctrl+r repeat",
       test_router_shortcut(ViewportShortcutKind::RepeatLastAction,
                            ViewportShortcutContext::Global)},
      {"M merge active",
       test_router_shortcut(ViewportShortcutKind::MergeActive)},
      {"Shift+M merge center",
       test_router_shortcut(ViewportShortcutKind::MergeCenter)},
      {"X delete popup",
       test_router_shortcut(ViewportShortcutKind::DeletePopup)},
      {"Shift+X mirror X", test_mirror_shortcut(ViewportMirrorAxis::X)},
      {"Shift+Y mirror Y", test_mirror_shortcut(ViewportMirrorAxis::Y)},
      {"Shift+Z mirror Z", test_mirror_shortcut(ViewportMirrorAxis::Z)},
      {"F flip mesh normals",
       test_router_shortcut(ViewportShortcutKind::FlipMeshNormals)},
      {"Shift+H flip horizontal",
       test_router_shortcut(ViewportShortcutKind::FlipHorizontal)},
      {"Shift+V flip vertical",
       test_router_shortcut(ViewportShortcutKind::FlipVertical)},
      {"Shift+T apply material",
       test_router_shortcut(ViewportShortcutKind::ApplyMaterial)},
  }};

  for (const ShortcutCase &shortcut_case : shortcuts) {
    SCOPED_TRACE(shortcut_case.label);
    ViewportInputRouter router;
    ViewportCameraController camera;
    ViewportInputFrame input;
    input.viewport_focused = true;
    input.viewport_size = {800.0f, 600.0f};
    set_resolved_shortcut(input, shortcut_case.shortcut);

    const auto result = router.handle_frame(input, camera);

    EXPECT_TRUE(result.shortcut_handled);
    EXPECT_TRUE(result.consumed);
    EXPECT_EQ(result.shortcut.kind, shortcut_case.shortcut.kind);
    EXPECT_EQ(result.shortcut.context, shortcut_case.shortcut.context);
    if (shortcut_case.shortcut.kind == ViewportShortcutKind::MirrorAxis) {
      EXPECT_EQ(result.shortcut.mirror_axis,
                shortcut_case.shortcut.mirror_axis);
    }
  }
}

TEST(ViewportInputRouterTests, LegacyShortcutAdapterSourceIsNotBuilt) {
  const std::string cmake = read_source_file("CMakeLists.txt");

  EXPECT_EQ(cmake.find("src/viewport/viewport_legacy_shortcut_adapter.cpp"),
            std::string::npos);
  EXPECT_EQ(cmake.find("src/viewport/viewport_legacy_shortcut_adapter.hpp"),
            std::string::npos);
}

TEST(ViewportInputRouterTests, FlipShortcutsOnlyApplyInObjectSelectionMode) {
  ViewportInputRouter router;
  ViewportCameraController camera;
  ViewportInputFrame input;
  input.viewport_focused = true;
  input.viewport_size = {800.0f, 600.0f};
  input.modifiers.shift = true;

  router.set_selection_mode(ViewportSelectionMode::Face);
  set_resolved_shortcut(
      input, test_router_shortcut(ViewportShortcutKind::FlipMeshNormals));
  ViewportInputResult result = router.handle_frame(input, camera);
  EXPECT_FALSE(result.shortcut_handled);
  EXPECT_FALSE(result.consumed);

  set_resolved_shortcut(
      input, test_router_shortcut(ViewportShortcutKind::FlipHorizontal));
  result = router.handle_frame(input, camera);
  EXPECT_FALSE(result.shortcut_handled);
  EXPECT_FALSE(result.consumed);

  router.set_selection_mode(ViewportSelectionMode::Edge);
  set_resolved_shortcut(
      input, test_router_shortcut(ViewportShortcutKind::FlipVertical));
  result = router.handle_frame(input, camera);
  EXPECT_FALSE(result.shortcut_handled);
  EXPECT_FALSE(result.consumed);

  router.set_selection_mode(ViewportSelectionMode::Object);
  set_resolved_shortcut(
      input, test_router_shortcut(ViewportShortcutKind::FlipMeshNormals));
  result = router.handle_frame(input, camera);
  EXPECT_TRUE(result.shortcut_handled);
  EXPECT_EQ(result.shortcut.kind, ViewportShortcutKind::FlipMeshNormals);

  set_resolved_shortcut(
      input, test_router_shortcut(ViewportShortcutKind::FlipHorizontal));
  result = router.handle_frame(input, camera);
  EXPECT_TRUE(result.shortcut_handled);
  EXPECT_EQ(result.shortcut.kind, ViewportShortcutKind::FlipHorizontal);
}

TEST(ViewportInputRouterTests, SelectionModeHotkeysMatchViewportShortcuts) {
  ViewportInputRouter router;
  ViewportCameraController camera;
  ViewportInputFrame input;
  input.viewport_focused = true;
  input.viewport_size = {800.0f, 600.0f};

  set_resolved_shortcut(input,
                        test_selection_shortcut(ViewportSelectionMode::Vertex));
  router.handle_frame(input, camera);
  EXPECT_EQ(router.state().selection_mode, ViewportSelectionMode::Vertex);

  set_resolved_shortcut(input,
                        test_selection_shortcut(ViewportSelectionMode::Edge));
  router.handle_frame(input, camera);
  EXPECT_EQ(router.state().selection_mode, ViewportSelectionMode::Edge);

  set_resolved_shortcut(input,
                        test_selection_shortcut(ViewportSelectionMode::Face));
  router.handle_frame(input, camera);
  EXPECT_EQ(router.state().selection_mode, ViewportSelectionMode::Face);

  set_resolved_shortcut(input,
                        test_selection_shortcut(ViewportSelectionMode::Object));
  router.handle_frame(input, camera);
  EXPECT_EQ(router.state().selection_mode, ViewportSelectionMode::Object);
}

TEST(ViewportInputRouterTests, GridPresetWorldSizesMatchMapUnitContract) {
  ViewportInputRouter router;
  EXPECT_EQ(router.state().grid_preset, viewport_default_grid_preset());
  EXPECT_EQ(viewport_default_grid_preset(), 6);
  EXPECT_NEAR(viewport_grid_world_size_for_preset(1), 1.0f / 32.0f, 0.0001f);
  EXPECT_NEAR(viewport_grid_world_size_for_preset(6), 1.0f, 0.0001f);
  EXPECT_NEAR(viewport_grid_world_size_for_preset(10), 16.0f, 0.0001f);
  EXPECT_NEAR(viewport_grid_world_size_for_preset(-4), 1.0f / 32.0f, 0.0001f);
  EXPECT_NEAR(viewport_grid_world_size_for_preset(99), 16.0f, 0.0001f);
}

TEST(ViewportInputRouterTests, GridPresetShortcutReportsOnlyActualChanges) {
  ViewportInputRouter router;
  ViewportCameraController camera;
  ViewportInputFrame input;
  input.viewport_focused = true;
  input.viewport_size = {800.0f, 600.0f};

  set_resolved_shortcut(input, test_grid_shortcut(7));
  ViewportInputResult result = router.handle_frame(input, camera);
  EXPECT_TRUE(result.shortcut_handled);
  EXPECT_TRUE(result.grid_preset_changed);
  EXPECT_EQ(result.requested_grid_preset, 7);
  EXPECT_EQ(router.state().grid_preset, viewport_default_grid_preset());

  router.sync_grid_preset_snapshot(result.requested_grid_preset);
  result = router.handle_frame(input, camera);
  EXPECT_TRUE(result.shortcut_handled);
  EXPECT_FALSE(result.grid_preset_changed);
  EXPECT_EQ(result.requested_grid_preset, 7);
  EXPECT_EQ(router.state().grid_preset, 7);
}

TEST(ViewportInputRouterTests, ViewShortcutReportsFocusedPaneRequest) {
  ViewportInputRouter router;
  ViewportCameraController camera;
  ViewportInputFrame input;
  input.viewport_focused = true;
  input.viewport_size = {800.0f, 600.0f};

  set_resolved_shortcut(input, test_view_shortcut(ViewportView::Front));
  const ViewportInputResult result = router.handle_frame(input, camera);

  EXPECT_TRUE(result.shortcut_handled);
  EXPECT_TRUE(result.view_changed);
  EXPECT_EQ(result.requested_view, ViewportView::Front);
  EXPECT_EQ(result.shortcut.kind, ViewportShortcutKind::SetView);
}

TEST(ViewportInputRouterTests, ToolActivationSupportTracksNativeControllers) {
  EXPECT_TRUE(viewport_tool_has_native_controller(ViewportTool::kSelect));
  EXPECT_TRUE(viewport_tool_has_native_controller(ViewportTool::kMove));
  EXPECT_TRUE(viewport_tool_has_native_controller(ViewportTool::kRotate));
  EXPECT_TRUE(viewport_tool_has_native_controller(ViewportTool::kScale));
  EXPECT_EQ(viewport_tool_has_native_controller(ViewportTool::kBox),
            viewport_tool_has_host_contract(ViewportTool::kBox));
  EXPECT_EQ(viewport_tool_has_native_controller(ViewportTool::kInsertEdgeLoop),
            viewport_tool_has_host_contract(ViewportTool::kInsertEdgeLoop));
  EXPECT_EQ(viewport_tool_has_native_controller(ViewportTool::kKnife),
            viewport_tool_has_host_contract(ViewportTool::kKnife));
  EXPECT_EQ(viewport_tool_has_native_controller(ViewportTool::kCut),
            viewport_tool_has_host_contract(ViewportTool::kCut));
  EXPECT_EQ(viewport_tool_has_native_controller(ViewportTool::kPoly),
            viewport_tool_has_host_contract(ViewportTool::kPoly));
  EXPECT_TRUE(viewport_tool_has_native_controller(ViewportTool::kMirror));
}

TEST(ViewportInputRouterTests,
     ModalToolGestureKeysAreNotConsumedByViewShortcuts) {
  ViewportInputRouter router;
  ViewportCameraController camera;
  ViewportInputFrame input;
  input.viewport_focused = true;
  input.viewport_size = {800.0f, 600.0f};

  router.set_active_tool(ViewportTool::kCut);
  set_resolved_shortcut(
      input, test_router_shortcut(ViewportShortcutKind::FocusDocument));
  ViewportInputResult result = router.handle_frame(input, camera);
  EXPECT_FALSE(result.shortcut_handled);
  EXPECT_FALSE(result.focus_document_requested);

  set_resolved_shortcut(
      input, test_router_shortcut(ViewportShortcutKind::ToggleMaximized));
  result = router.handle_frame(input, camera);
  EXPECT_FALSE(result.shortcut_handled);
  EXPECT_FALSE(result.maximized_toggle_requested);

  router.set_active_tool(ViewportTool::kMirror);
  set_resolved_shortcut(
      input, test_router_shortcut(ViewportShortcutKind::ToggleMaximized));
  result = router.handle_frame(input, camera);
  EXPECT_FALSE(result.shortcut_handled);
  EXPECT_FALSE(result.maximized_toggle_requested);
}

TEST(ViewportInputRouterTests, ToggleMaximizedShortcutReportsWorkspaceRequest) {
  ViewportInputRouter router;
  ViewportCameraController camera;
  ViewportInputFrame input;
  input.viewport_focused = true;
  input.viewport_size = {800.0f, 600.0f};
  set_resolved_shortcut(
      input, test_router_shortcut(ViewportShortcutKind::ToggleMaximized));

  const ViewportInputResult result = router.handle_frame(input, camera);

  EXPECT_TRUE(result.shortcut_handled);
  EXPECT_TRUE(result.maximized_toggle_requested);
  EXPECT_EQ(result.shortcut.kind, ViewportShortcutKind::ToggleMaximized);
  EXPECT_EQ(router.state().active_tool, ViewportTool::kSelect);
  EXPECT_EQ(router.state().selection_mode, ViewportSelectionMode::Object);
  EXPECT_EQ(router.state().grid_preset, viewport_default_grid_preset());
}

TEST(ViewportInputRouterTests, FocusGateBlocksViewportToolHotkeys) {
  ViewportInputRouter router;
  ViewportCameraController camera;
  ViewportInputFrame input;
  input.viewport_focused = false;
  input.viewport_size = {800.0f, 600.0f};
  set_resolved_shortcut(input, test_tool_shortcut(ViewportTool::kBox));

  const auto result = router.handle_frame(input, camera);

  EXPECT_FALSE(result.shortcut_handled);
  EXPECT_EQ(router.state().active_tool, ViewportTool::kSelect);
}

TEST(ViewportInputRouterTests, EscapeRequestsActiveToolCancel) {
  ViewportInputRouter router;
  ViewportCameraController camera;
  ViewportInputFrame input;
  input.viewport_focused = true;
  input.viewport_size = {800.0f, 600.0f};
  set_resolved_shortcut(
      input, test_router_shortcut(ViewportShortcutKind::CancelActiveTool));

  const auto result = router.handle_frame(input, camera);

  EXPECT_TRUE(result.shortcut_handled);
  EXPECT_TRUE(result.cancel_requested);
  EXPECT_EQ(result.shortcut.kind, ViewportShortcutKind::CancelActiveTool);
}

TEST(ViewportInputRouterTests, ExternalSelectionModeSyncUpdatesToolState) {
  ViewportInputRouter router;

  router.set_selection_mode(ViewportSelectionMode::Face);

  EXPECT_EQ(router.state().selection_mode, ViewportSelectionMode::Face);
}

TEST(ViewportInputRouterTests,
     ConsumedCommandKeybindingDoesNotRouteViewportShortcut) {
  ViewportInputRouter router;
  ViewportCameraController camera;
  ViewportInputFrame input;
  input.viewport_focused = true;
  input.viewport_size = {800.0f, 600.0f};
  input.keybinding_input_consumed = true;
  set_resolved_shortcut(
      input, test_router_shortcut(ViewportShortcutKind::FocusDocument));

  const auto result = router.handle_frame(input, camera);

  EXPECT_TRUE(result.consumed);
  EXPECT_FALSE(result.shortcut_handled);
  EXPECT_FALSE(result.focus_document_requested);
  EXPECT_EQ(result.shortcut.kind, ViewportShortcutKind::None);
}

TEST(ViewportInputRouterTests,
     EmptyShortcutSentinelDoesNotRouteViewportAction) {
  ViewportInputRouter router;
  ViewportCameraController camera;
  ViewportInputFrame input;
  input.viewport_focused = true;
  input.viewport_size = {800.0f, 600.0f};
  input.resolved_shortcuts = {ViewportShortcut{}};

  const auto result = router.handle_frame(input, camera);

  EXPECT_FALSE(result.shortcut_handled);
  EXPECT_FALSE(result.focus_document_requested);
  EXPECT_EQ(result.shortcut.kind, ViewportShortcutKind::None);
}

TEST(ViewportHostContractAdapterTests, EmptyShortcutDoesNotMapToCommandTarget) {
  EXPECT_FALSE(
      viewport_command_target_id_for_router_shortcut(ViewportShortcut{})
          .has_value());
}

TEST(ViewportInputRouterTests, FocusShortcutsMatchViewportContract) {
  ViewportInputRouter router;
  ViewportCameraController camera;
  ViewportInputFrame input;
  input.viewport_focused = true;
  input.viewport_size = {800.0f, 600.0f};
  set_resolved_shortcut(
      input, test_router_shortcut(ViewportShortcutKind::FocusDocument));

  auto result = router.handle_frame(input, camera);
  EXPECT_TRUE(result.shortcut_handled);
  EXPECT_TRUE(result.focus_document_requested);
  EXPECT_EQ(result.shortcut.kind, ViewportShortcutKind::FocusDocument);

  set_resolved_shortcut(
      input, test_router_shortcut(ViewportShortcutKind::FocusOrigin));
  result = router.handle_frame(input, camera);
  EXPECT_TRUE(result.shortcut_handled);
  EXPECT_FALSE(result.focus_document_requested);
  EXPECT_EQ(result.shortcut.kind, ViewportShortcutKind::FocusOrigin);
  EXPECT_FLOAT_EQ(camera.state().distance, 64.0f);
}

TEST(ViewportInputRouterTests, MiddleMouseNavigationAndWheelAreViewportGated) {
  ViewportInputRouter router;
  ViewportCameraController camera;
  ViewportInputFrame input;
  input.viewport_hovered = true;
  input.viewport_focused = true;
  input.viewport_size = {800.0f, 600.0f};
  input.mouse_screen = {100.0f, 100.0f};
  set_navigation_pressed(input, ViewportInputActionKind::Orbit);

  auto result = router.handle_frame(input, camera);
  EXPECT_TRUE(result.consumed);
  EXPECT_TRUE(router.navigating());

  input.mouse_screen = {160.0f, 120.0f};
  set_navigation_down(input, ViewportInputActionKind::Orbit);
  result = router.handle_frame(input, camera);
  EXPECT_TRUE(result.camera_changed);

  set_navigation_released(input, ViewportInputActionKind::Orbit);
  result = router.handle_frame(input, camera);
  EXPECT_FALSE(router.navigating());

  const float distance_before_ignored_wheel = camera.state().distance;
  input = {};
  input.viewport_hovered = false;
  input.viewport_size = {800.0f, 600.0f};
  input.mouse_wheel = 1.0f;
  result = router.handle_frame(input, camera);
  EXPECT_FALSE(result.consumed);
  EXPECT_FLOAT_EQ(camera.state().distance, distance_before_ignored_wheel);
}

TEST(ViewportInputRouterTests, AltMiddleMouseStartsOrbitSnapNavigation) {
  ViewportInputRouter router;
  ViewportCameraController camera;
  camera.set_viewport_size(800.0f, 600.0f);
  camera.focus_origin(8.0f);
  camera.set_view_direction({0.02f, 0.0f, -1.0f});

  ViewportInputFrame input;
  input.viewport_hovered = true;
  input.viewport_focused = true;
  input.viewport_size = {800.0f, 600.0f};
  input.modifiers.alt = true;
  input.mouse_screen = {100.0f, 100.0f};
  set_navigation_pressed(input, ViewportInputActionKind::OrbitSnap);

  auto result = router.handle_frame(input, camera);
  EXPECT_TRUE(result.consumed);
  EXPECT_TRUE(router.navigating());

  input.mouse_screen = {100.0f, 100.0f};
  set_navigation_down(input, ViewportInputActionKind::OrbitSnap);
  result = router.handle_frame(input, camera);
  EXPECT_TRUE(result.camera_changed);
  EXPECT_EQ(camera.state().projection, ViewportProjection::Orthographic);
  EXPECT_FALSE(camera.orthographic_view_locked());
  const QVec3 forward = normalize(camera.pose().target - camera.pose().eye);
  EXPECT_NEAR(forward.x, 0.0f, 0.001f);
  EXPECT_NEAR(forward.y, 0.0f, 0.001f);
  EXPECT_NEAR(forward.z, -1.0f, 0.001f);

  set_navigation_released(input, ViewportInputActionKind::OrbitSnap);
  result = router.handle_frame(input, camera);
  EXPECT_TRUE(result.consumed);
  EXPECT_FALSE(router.navigating());
}

TEST(ViewportInputRouterTests,
     MiddleOrbitSamplesAltDuringDragForOrthographicSnap) {
  ViewportInputRouter router;
  ViewportCameraController camera;
  camera.set_viewport_size(800.0f, 600.0f);
  camera.focus_origin(8.0f);
  camera.set_view_direction({0.02f, 0.0f, -1.0f});

  ViewportInputFrame input;
  input.viewport_hovered = true;
  input.viewport_focused = true;
  input.viewport_size = {800.0f, 600.0f};
  input.mouse_screen = {100.0f, 100.0f};
  set_navigation_pressed(input, ViewportInputActionKind::Orbit);

  auto result = router.handle_frame(input, camera);
  ASSERT_TRUE(result.consumed);
  ASSERT_TRUE(router.navigating());
  EXPECT_EQ(camera.state().projection, ViewportProjection::Perspective);

  input.modifiers.alt = true;
  input.mouse_screen = {220.0f, 100.0f};
  set_navigation_down(input, ViewportInputActionKind::Orbit);
  result = router.handle_frame(input, camera);

  EXPECT_TRUE(result.camera_changed);
  EXPECT_EQ(camera.state().projection, ViewportProjection::Orthographic);
  EXPECT_FALSE(camera.orthographic_view_locked());

  input.modifiers.alt = false;
  input.mouse_screen = {116.0f, 100.0f};
  set_navigation_down(input, ViewportInputActionKind::Orbit);
  result = router.handle_frame(input, camera);

  EXPECT_TRUE(result.camera_changed);
  EXPECT_EQ(camera.state().projection, ViewportProjection::Perspective);
}

TEST(ViewportInputRouterTests, PlainMiddleOrbitAfterAltSnapDoesNotSnap) {
  ViewportInputRouter router;
  ViewportCameraController camera;
  camera.set_viewport_size(800.0f, 600.0f);
  camera.focus_origin(8.0f);
  camera.set_view_direction({0.02f, 0.0f, -1.0f});

  ViewportInputFrame input;
  input.viewport_hovered = true;
  input.viewport_focused = true;
  input.viewport_size = {800.0f, 600.0f};
  input.modifiers.alt = true;
  input.mouse_screen = {100.0f, 100.0f};
  set_navigation_pressed(input, ViewportInputActionKind::OrbitSnap);

  auto result = router.handle_frame(input, camera);
  ASSERT_TRUE(result.consumed);
  input.mouse_screen = {100.0f, 100.0f};
  set_navigation_down(input, ViewportInputActionKind::OrbitSnap);
  result = router.handle_frame(input, camera);
  ASSERT_TRUE(result.camera_changed);
  ASSERT_EQ(camera.state().projection, ViewportProjection::Orthographic);
  ASSERT_FALSE(camera.orthographic_view_locked());

  set_navigation_released(input, ViewportInputActionKind::OrbitSnap);
  result = router.handle_frame(input, camera);
  ASSERT_FALSE(router.navigating());

  input = {};
  input.viewport_hovered = true;
  input.viewport_focused = true;
  input.viewport_size = {800.0f, 600.0f};
  input.mouse_screen = {100.0f, 100.0f};
  set_navigation_pressed(input, ViewportInputActionKind::Orbit);
  result = router.handle_frame(input, camera);

  EXPECT_TRUE(result.consumed);
  EXPECT_TRUE(router.navigating());
  EXPECT_EQ(camera.state().projection, ViewportProjection::Perspective);
  EXPECT_FALSE(camera.orthographic_view_locked());
}

TEST(ViewportInputRouterTests, RightMouseAfterAltSnapPansOrthographicView) {
  ViewportInputRouter router;
  ViewportCameraController camera;
  camera.set_viewport_size(800.0f, 600.0f);
  camera.focus_origin(8.0f);
  camera.set_view_direction({0.02f, 0.0f, -1.0f});

  ViewportInputFrame input;
  input.viewport_hovered = true;
  input.viewport_focused = true;
  input.viewport_size = {800.0f, 600.0f};
  input.modifiers.alt = true;
  input.mouse_screen = {100.0f, 100.0f};
  set_navigation_pressed(input, ViewportInputActionKind::OrbitSnap);

  auto result = router.handle_frame(input, camera);
  ASSERT_TRUE(result.consumed);
  input.mouse_screen = {100.0f, 100.0f};
  set_navigation_down(input, ViewportInputActionKind::OrbitSnap);
  result = router.handle_frame(input, camera);
  ASSERT_TRUE(result.camera_changed);
  ASSERT_EQ(camera.state().projection, ViewportProjection::Orthographic);
  ASSERT_FALSE(camera.orthographic_view_locked());

  set_navigation_released(input, ViewportInputActionKind::OrbitSnap);
  result = router.handle_frame(input, camera);
  ASSERT_FALSE(router.navigating());

  input = {};
  input.viewport_hovered = true;
  input.viewport_focused = true;
  input.viewport_size = {800.0f, 600.0f};
  input.mouse_screen = {300.0f, 240.0f};
  set_navigation_pressed(input, ViewportInputActionKind::Fly);
  result = router.handle_frame(input, camera);

  EXPECT_TRUE(result.consumed);
  EXPECT_TRUE(router.navigating());
  EXPECT_FALSE(result.fly_navigation_active);
  EXPECT_EQ(camera.state().projection, ViewportProjection::Orthographic);
  EXPECT_FALSE(camera.orthographic_view_locked());
}

TEST(ViewportInputRouterTests,
     LockedOrthographicViewConsumesMiddleOrbitWithoutMovingCamera) {
  ViewportInputRouter router;
  ViewportCameraController camera;
  camera.set_viewport_size(800.0f, 600.0f);
  camera.view_axis(ViewportViewAxis::Z, 1);
  ASSERT_TRUE(camera.orthographic_view_locked());

  const ViewportCameraState before = camera.state();

  ViewportInputFrame input;
  input.viewport_hovered = true;
  input.viewport_focused = true;
  input.viewport_size = {800.0f, 600.0f};
  input.mouse_screen = {100.0f, 100.0f};
  set_navigation_pressed(input, ViewportInputActionKind::Orbit);

  auto result = router.handle_frame(input, camera);
  EXPECT_TRUE(result.consumed);
  EXPECT_TRUE(router.navigating());

  input.mouse_screen = {180.0f, 140.0f};
  set_navigation_down(input, ViewportInputActionKind::Orbit);
  result = router.handle_frame(input, camera);

  EXPECT_TRUE(result.consumed);
  EXPECT_FALSE(result.camera_changed);
  EXPECT_NEAR(camera.state().target.x, before.target.x, 0.001f);
  EXPECT_NEAR(camera.state().target.y, before.target.y, 0.001f);
  EXPECT_NEAR(camera.state().target.z, before.target.z, 0.001f);
  EXPECT_NEAR(camera.state().yaw_radians, before.yaw_radians, 0.001f);
  EXPECT_NEAR(camera.state().pitch_radians, before.pitch_radians, 0.001f);
  EXPECT_EQ(camera.state().projection, ViewportProjection::Orthographic);

  set_navigation_released(input, ViewportInputActionKind::Orbit);
  result = router.handle_frame(input, camera);
  EXPECT_TRUE(result.consumed);
  EXPECT_FALSE(router.navigating());
}

TEST(ViewportInputRouterTests, ShiftMiddleMousePansLockedOrthographicView) {
  ViewportInputRouter router;
  ViewportCameraController camera;
  camera.set_viewport_size(800.0f, 600.0f);
  camera.view_axis(ViewportViewAxis::Z, 1);
  ASSERT_TRUE(camera.orthographic_view_locked());

  const QVec3 target_before = camera.state().target;

  ViewportInputFrame input;
  input.viewport_hovered = true;
  input.viewport_focused = true;
  input.viewport_size = {800.0f, 600.0f};
  input.modifiers.shift = true;
  input.mouse_screen = {100.0f, 100.0f};
  set_navigation_pressed(input, ViewportInputActionKind::Pan);

  auto result = router.handle_frame(input, camera);
  EXPECT_TRUE(result.consumed);
  EXPECT_TRUE(router.navigating());

  input.mouse_screen = {180.0f, 140.0f};
  set_navigation_down(input, ViewportInputActionKind::Pan);
  result = router.handle_frame(input, camera);

  EXPECT_TRUE(result.consumed);
  EXPECT_TRUE(result.camera_changed);
  EXPECT_GT(length(camera.state().target - target_before), 0.001f);
  EXPECT_EQ(camera.state().projection, ViewportProjection::Orthographic);
}

TEST(ViewportInputRouterTests, RightMousePansOrthographicViewInsteadOfFlying) {
  ViewportInputRouter router;
  ViewportCameraController camera;
  camera.set_viewport_size(800.0f, 600.0f);
  camera.view_axis(ViewportViewAxis::Z, 1);
  ASSERT_TRUE(camera.orthographic_view_locked());

  const ViewportCameraState before = camera.state();

  ViewportInputFrame input;
  input.viewport_hovered = true;
  input.viewport_focused = true;
  input.viewport_size = {800.0f, 600.0f};
  input.mouse_screen = {240.0f, 180.0f};
  set_navigation_pressed(input, ViewportInputActionKind::Fly);

  auto result = router.handle_frame(input, camera);
  EXPECT_TRUE(result.consumed);
  EXPECT_TRUE(router.navigating());

  input.mouse_screen = {310.0f, 210.0f};
  input.fly_local_direction = {0.0f, 0.0f, 1.0f};
  input.delta_seconds = 0.25f;
  set_navigation_down(input, ViewportInputActionKind::Fly);
  result = router.handle_frame(input, camera);

  EXPECT_TRUE(result.consumed);
  EXPECT_TRUE(result.camera_changed);
  EXPECT_GT(length(camera.state().target - before.target), 0.001f);
  EXPECT_NEAR(camera.state().yaw_radians, before.yaw_radians, 0.001f);
  EXPECT_NEAR(camera.state().pitch_radians, before.pitch_radians, 0.001f);
  EXPECT_EQ(camera.state().projection, ViewportProjection::Orthographic);

  input.fly_local_direction = {};
  set_navigation_released(input, ViewportInputActionKind::Fly);
  result = router.handle_frame(input, camera);
  EXPECT_TRUE(result.consumed);
  EXPECT_FALSE(router.navigating());
}

TEST(ViewportInputRouterTests, RightMouseFlyNavigationConsumesMovementHotkeys) {
  ViewportInputRouter router;
  ViewportCameraController camera;
  ViewportInputFrame input;
  input.viewport_hovered = true;
  input.viewport_focused = true;
  input.viewport_size = {800.0f, 600.0f};
  input.mouse_screen = {300.0f, 240.0f};
  set_navigation_pressed(input, ViewportInputActionKind::Fly);

  auto result = router.handle_frame(input, camera);
  EXPECT_TRUE(result.consumed);
  EXPECT_TRUE(router.navigating());
  EXPECT_TRUE(result.fly_navigation_active);
  EXPECT_TRUE(result.fly_cursor_capture_active);
  EXPECT_FALSE(result.fly_hud_active);

  const ViewportCameraPose before_move = camera.pose();
  input.mouse_delta = {40.0f, 0.0f};
  input.fly_local_direction = {0.0f, 0.0f, 1.0f};
  input.delta_seconds = 0.25f;
  set_navigation_down(input, ViewportInputActionKind::Fly);
  result = router.handle_frame(input, camera);

  EXPECT_TRUE(result.camera_changed);
  EXPECT_TRUE(result.fly_navigation_active);
  EXPECT_TRUE(result.fly_cursor_capture_active);
  EXPECT_TRUE(result.fly_hud_active);
  EXPECT_EQ(router.state().active_tool, ViewportTool::kSelect);
  EXPECT_GT(length(camera.pose().eye - before_move.eye), 1.0f);

  input.fly_local_direction = {};
  input.mouse_delta = {};
  set_navigation_down(input, ViewportInputActionKind::Fly);
  result = router.handle_frame(input, camera);
  EXPECT_TRUE(result.fly_navigation_active);
  EXPECT_TRUE(result.fly_cursor_capture_active);
  EXPECT_TRUE(result.fly_hud_active);

  set_navigation_released(input, ViewportInputActionKind::Fly);
  result = router.handle_frame(input, camera);
  EXPECT_TRUE(result.consumed);
  EXPECT_FALSE(router.navigating());
  EXPECT_FALSE(result.fly_hud_active);
}

TEST(ViewportInputRouterTests,
     RightMouseFlyNavigationIgnoresPressFrameMouseDelta) {
  ViewportInputRouter router;
  ViewportCameraController camera;
  ViewportInputFrame input;
  input.viewport_hovered = true;
  input.viewport_focused = true;
  input.viewport_size = {800.0f, 600.0f};
  input.mouse_screen = {300.0f, 240.0f};
  input.mouse_delta = {250.0f, -180.0f};
  input.fly_local_direction = {0.0f, 0.0f, 1.0f};
  input.delta_seconds = 0.25f;
  set_navigation_pressed(input, ViewportInputActionKind::Fly);

  const ViewportCameraPose before = camera.pose();
  const auto result = router.handle_frame(input, camera);

  EXPECT_TRUE(result.consumed);
  EXPECT_TRUE(result.fly_navigation_active);
  EXPECT_FALSE(result.fly_hud_active);
  EXPECT_FALSE(result.camera_changed);
  EXPECT_NEAR(length(camera.pose().eye - before.eye), 0.0f, 0.001f);
  EXPECT_NEAR(dot(normalize(camera.pose().target - camera.pose().eye),
                  normalize(before.target - before.eye)),
              1.0f, 0.001f);
}

TEST(ViewportInputRouterTests,
     RightMouseFlyLookNearOrthogonalAxisStaysPerspective) {
  ViewportInputRouter router;
  ViewportCameraController camera;
  camera.set_viewport_size(800.0f, 600.0f);
  camera.focus_origin(8.0f);
  camera.set_view_direction({0.02f, 0.0f, -1.0f});

  ViewportInputFrame input;
  input.viewport_hovered = true;
  input.viewport_focused = true;
  input.viewport_size = {800.0f, 600.0f};
  input.mouse_screen = {300.0f, 240.0f};
  set_navigation_pressed(input, ViewportInputActionKind::Fly);

  auto result = router.handle_frame(input, camera);
  ASSERT_TRUE(result.consumed);
  ASSERT_TRUE(result.fly_navigation_active);

  const ViewportCameraPose before = camera.pose();
  input.mouse_delta = {0.0f, -2.0f};
  input.delta_seconds = 0.016f;
  set_navigation_down(input, ViewportInputActionKind::Fly);
  result = router.handle_frame(input, camera);

  const ViewportCameraPose after = camera.pose();
  const QVec3 after_forward = normalize(after.target - after.eye);
  EXPECT_TRUE(result.consumed);
  EXPECT_TRUE(result.fly_navigation_active);
  EXPECT_TRUE(result.camera_changed);
  EXPECT_EQ(camera.state().projection, ViewportProjection::Perspective);
  EXPECT_FALSE(camera.orthographic_view_locked());
  EXPECT_NEAR(length(after.eye - before.eye), 0.0f, 0.001f);
  EXPECT_GT(std::abs(after_forward.x), 0.001f);
}

TEST(ViewportInputRouterTests, FlyWheelAdjustsNavigationSpeedInsteadOfZooming) {
  ViewportInputRouter router;
  ViewportCameraController camera;
  ViewportInputFrame input;
  input.viewport_hovered = true;
  input.viewport_focused = true;
  input.viewport_size = {800.0f, 600.0f};
  input.mouse_screen = {300.0f, 240.0f};
  set_navigation_pressed(input, ViewportInputActionKind::Fly);

  auto result = router.handle_frame(input, camera);
  ASSERT_TRUE(result.fly_navigation_active);

  const float distance_before_wheel = camera.state().distance;
  const float speed_before_wheel = camera.fly_speed();

  input.mouse_wheel = 1.0f;
  input.mouse_delta = {};
  set_navigation_down(input, ViewportInputActionKind::Fly);
  input.navigation_bindings.zoom_wheel = true;
  result = router.handle_frame(input, camera);

  EXPECT_TRUE(result.consumed);
  EXPECT_TRUE(result.fly_navigation_active);
  EXPECT_TRUE(result.fly_speed_changed);
  EXPECT_NEAR(result.fly_speed, speed_before_wheel + 1.0f, 0.001f);
  EXPECT_NEAR(camera.fly_speed(), speed_before_wheel + 1.0f, 0.001f);
  EXPECT_NEAR(camera.state().distance, distance_before_wheel, 0.001f);

  input.mouse_wheel = -2.0f;
  set_navigation_down(input, ViewportInputActionKind::Fly);
  input.navigation_bindings.zoom_wheel = true;
  result = router.handle_frame(input, camera);
  EXPECT_TRUE(result.fly_speed_changed);
  EXPECT_NEAR(camera.fly_speed(), speed_before_wheel - 1.0f, 0.001f);
}

TEST(ViewportInputRouterTests, MiddleMouseResetsFlyNavigationSpeed) {
  ViewportInputRouter router;
  ViewportCameraController camera;
  camera.adjust_fly_speed(5.0f);
  ASSERT_NEAR(camera.fly_speed(),
              static_cast<float>(viewport_host_defaults().fly_base_speed) +
                  5.0f,
              0.001f);
  ASSERT_FALSE(camera.fly_speed_is_default());

  ViewportInputFrame input;
  input.viewport_hovered = true;
  input.viewport_focused = true;
  input.viewport_size = {800.0f, 600.0f};
  input.mouse_screen = {300.0f, 240.0f};
  set_navigation_pressed(input, ViewportInputActionKind::Fly);

  auto result = router.handle_frame(input, camera);
  ASSERT_TRUE(result.fly_navigation_active);

  input.mouse_delta = {};
  set_navigation_down(input, ViewportInputActionKind::Fly);
  input.navigation_bindings.reset_fly_speed_pressed = true;
  result = router.handle_frame(input, camera);

  EXPECT_TRUE(result.consumed);
  EXPECT_TRUE(result.fly_navigation_active);
  EXPECT_TRUE(result.fly_speed_changed);
  EXPECT_TRUE(result.fly_speed_reset);
  EXPECT_NEAR(result.fly_speed,
              static_cast<float>(viewport_host_defaults().fly_base_speed),
              0.001f);
  EXPECT_NEAR(camera.fly_speed(),
              static_cast<float>(viewport_host_defaults().fly_base_speed),
              0.001f);
  EXPECT_TRUE(camera.fly_speed_is_default());
}

TEST(ViewportInputRouterTests, ShiftRightMouseDoesNotStartFlyNavigation) {
  ViewportInputRouter router;
  ViewportCameraController camera;
  ViewportInputFrame input;
  input.viewport_hovered = true;
  input.viewport_focused = true;
  input.viewport_size = {800.0f, 600.0f};
  input.modifiers.shift = true;
  input.mouse_screen = {300.0f, 240.0f};

  const auto result = router.handle_frame(input, camera);

  EXPECT_FALSE(result.consumed);
  EXPECT_FALSE(router.navigating());
}

TEST(AssetRegistryTests, StoresMaterialMeshAndTextureMetadata) {
  AssetRegistry registry;
  const AssetId mesh = registry.add_mesh(make_test_triangle_mesh());
  const AssetId material =
      registry.add_material(make_default_quader_material());
  TextureAsset texture_asset;
  texture_asset.name = "Curvature";
  texture_asset.usage = TextureUsage::Curvature;
  texture_asset.color_space = TextureColorSpace::Linear;
  texture_asset.width = 512;
  texture_asset.height = 512;
  texture_asset.generated = true;
  const AssetId texture = registry.add_texture(std::move(texture_asset));

  ASSERT_NE(registry.mesh(mesh), nullptr);
  ASSERT_NE(registry.material(material), nullptr);
  ASSERT_NE(registry.texture(texture), nullptr);
  EXPECT_EQ(registry.texture(texture)->usage, TextureUsage::Curvature);
  EXPECT_EQ(registry.texture(texture)->color_space, TextureColorSpace::Linear);
}

TEST(AssetRegistryTests, TracksTypedRevisionsAndScopedUpdates) {
  AssetRegistry registry;
  MeshAsset mesh_asset = make_test_triangle_mesh("Source");
  const AssetId mesh = registry.add_mesh(std::move(mesh_asset));
  const AssetId material =
      registry.add_material(make_default_quader_material());
  TextureAsset texture_asset;
  texture_asset.name = "Mask";
  texture_asset.usage = TextureUsage::Mask;
  texture_asset.width = 16;
  texture_asset.height = 16;
  texture_asset.generated = true;
  const AssetId texture = registry.add_texture(std::move(texture_asset));

  EXPECT_EQ(registry.mesh_revision(mesh).value, 1u);
  EXPECT_EQ(registry.material_revision(material).value, 1u);
  EXPECT_EQ(registry.texture_revision(texture).value, 1u);
  EXPECT_EQ(registry.revision(), 3u);

  MeshAsset replacement = make_test_triangle_mesh();
  replacement.id = 999;
  replacement.revision = MeshAssetRevision{99};
  replacement.name = "Replacement";
  ASSERT_TRUE(registry.replace_mesh(mesh, std::move(replacement)));

  const MeshAsset *replaced_mesh = registry.mesh(mesh);
  ASSERT_NE(replaced_mesh, nullptr);
  EXPECT_EQ(replaced_mesh->id, mesh);
  EXPECT_EQ(replaced_mesh->revision.value, 2u);
  EXPECT_EQ(replaced_mesh->name, "Replacement");
  EXPECT_EQ(registry.revision(), 4u);

  ASSERT_TRUE(registry.update_mesh(mesh, [](MeshAsset &asset) {
    asset.id = 999;
    asset.revision = MeshAssetRevision{99};
    asset.name = "Scoped Mesh";
  }));
  ASSERT_TRUE(registry.update_material(material, [](MaterialDocument &asset) {
    asset.id = 999;
    asset.revision = MaterialAssetRevision{99};
    asset.name = "Scoped Material";
  }));
  ASSERT_TRUE(registry.update_texture(texture, [](TextureAsset &asset) {
    asset.id = 999;
    asset.revision = TextureAssetRevision{99};
    asset.width = 32;
  }));

  ASSERT_NE(registry.mesh(mesh), nullptr);
  ASSERT_NE(registry.material(material), nullptr);
  ASSERT_NE(registry.texture(texture), nullptr);
  EXPECT_EQ(registry.mesh(mesh)->id, mesh);
  EXPECT_EQ(registry.material(material)->id, material);
  EXPECT_EQ(registry.texture(texture)->id, texture);
  EXPECT_EQ(registry.mesh(mesh)->revision.value, 3u);
  EXPECT_EQ(registry.material(material)->revision.value, 2u);
  EXPECT_EQ(registry.texture(texture)->revision.value, 2u);
  EXPECT_EQ(registry.texture(texture)->width, 32u);
  EXPECT_EQ(registry.revision(), 7u);
  EXPECT_FALSE(registry.update_mesh(kInvalidAssetId, [](MeshAsset &) {}));
  EXPECT_EQ(registry.mesh_revision(kInvalidAssetId).value, 0u);
}

TEST(AssetRegistryTests,
     RejectsDuplicateExplicitIdsAndEnumeratesAssetsDeterministically) {
  AssetRegistry registry;
  EXPECT_EQ(registry.add_mesh(20u, make_test_triangle_mesh("Mesh 20")), 20u);
  EXPECT_EQ(registry.add_mesh(10u, make_test_triangle_mesh("Mesh 10")), 10u);
  EXPECT_EQ(registry.add_mesh(20u, make_test_triangle_mesh("Duplicate")),
            kInvalidAssetId);
  EXPECT_EQ(registry.mesh_ids(), std::vector<AssetId>({10u, 20u}));

  TextureAsset texture_a;
  texture_a.name = "Texture 30";
  TextureAsset texture_b;
  texture_b.name = "Texture 25";
  EXPECT_EQ(registry.add_texture(30u, std::move(texture_a)), 30u);
  EXPECT_EQ(registry.add_texture(25u, std::move(texture_b)), 25u);
  TextureAsset duplicate_texture;
  duplicate_texture.name = "Duplicate Texture";
  EXPECT_EQ(registry.add_texture(25u, std::move(duplicate_texture)),
            kInvalidAssetId);
  EXPECT_EQ(registry.texture_ids(), std::vector<AssetId>({25u, 30u}));

  MaterialDocument material_a = make_default_quader_material("Material 40");
  MaterialDocument material_b = make_default_quader_material("Material 35");
  EXPECT_EQ(registry.add_material(40u, std::move(material_a)), 40u);
  EXPECT_EQ(registry.add_material(35u, std::move(material_b)), 35u);
  MaterialDocument duplicate_material =
      make_default_quader_material("Duplicate Material");
  EXPECT_EQ(registry.add_material(35u, std::move(duplicate_material)),
            kInvalidAssetId);
  EXPECT_EQ(registry.material_ids(), std::vector<AssetId>({35u, 40u}));
}

TEST(AssetRegistryTests, NormalizesMaterialTextureSlotsAtRegistryBoundary) {
  AssetRegistry registry;
  TextureAsset base_color;
  base_color.name = "Base Color";
  const AssetId base_color_texture =
      registry.add_texture(std::move(base_color));
  TextureAsset roughness;
  roughness.name = "Roughness";
  const AssetId roughness_texture = registry.add_texture(std::move(roughness));

  MaterialDocument material = make_default_quader_material("Aliased Material");
  material.texture_slots.clear();
  material.texture_slots.emplace("baseColor", base_color_texture);

  const AssetId material_id = registry.add_material(std::move(material));
  ASSERT_NE(material_id, kInvalidAssetId);

  const MaterialDocument *stored = registry.material(material_id);
  ASSERT_NE(stored, nullptr);
  ASSERT_TRUE(
      stored->texture_slots.contains(material_texture_slot::kBaseColor));
  EXPECT_EQ(stored->texture_slots.at(material_texture_slot::kBaseColor),
            base_color_texture);
  EXPECT_FALSE(stored->texture_slots.contains("baseColor"));

  ASSERT_TRUE(registry.update_material(
      material_id, [roughness_texture](MaterialDocument &asset) {
        asset.texture_slots.emplace("roughness", roughness_texture);
      }));

  stored = registry.material(material_id);
  ASSERT_NE(stored, nullptr);
  ASSERT_TRUE(
      stored->texture_slots.contains(material_texture_slot::kRoughness));
  EXPECT_EQ(stored->texture_slots.at(material_texture_slot::kRoughness),
            roughness_texture);
  EXPECT_FALSE(stored->texture_slots.contains("roughness"));
}

TEST(AssetRegistryTests,
     RejectsInvalidMaterialTextureReferencesAtRegistryBoundary) {
  AssetRegistry registry;
  MaterialDocument unsupported = make_default_quader_material("Unsupported");
  unsupported.texture_slots.emplace("normalTexture", 99u);
  EXPECT_EQ(registry.add_material(std::move(unsupported)), kInvalidAssetId);

  MaterialDocument missing = make_default_quader_material("Missing Texture");
  set_material_texture_slot(missing, material_texture_slot::kBaseColor, 99u);
  EXPECT_EQ(registry.add_material(std::move(missing)), kInvalidAssetId);

  TextureAsset texture;
  texture.name = "Valid";
  const AssetId texture_id = registry.add_texture(std::move(texture));
  MaterialDocument valid =
      make_default_quader_material("Valid", texture_id, texture_id);
  const AssetId material_id = registry.add_material(std::move(valid));
  ASSERT_NE(material_id, kInvalidAssetId);

  EXPECT_FALSE(
      registry.update_material(material_id, [](MaterialDocument &asset) {
        asset.texture_slots.emplace("unsupported", 5u);
      }));
  ASSERT_NE(registry.material(material_id), nullptr);
  EXPECT_FALSE(
      registry.material(material_id)->texture_slots.contains("unsupported"));
}

TEST(AssetRegistryTests,
     MaterialTextureMutationOwnsSlotTextureAndReplacementValidation) {
  AssetRegistry registry;
  const AssetId material =
      registry.add_material(make_default_quader_material("Mutable Material"));
  TextureAsset texture_asset;
  texture_asset.name = "Base Color";
  const AssetId texture = registry.add_texture(std::move(texture_asset));
  ASSERT_NE(material, kInvalidAssetId);
  ASSERT_NE(texture, kInvalidAssetId);

  AssetRegistryMutationResult result =
      registry.set_material_texture(material, "baseColor", texture);
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_TRUE(result.changed);
  EXPECT_EQ(result.message, "Material updated");
  ASSERT_NE(registry.material(material), nullptr);
  ASSERT_TRUE(registry.material(material)->texture_slots.contains(
      material_texture_slot::kBaseColor));
  EXPECT_EQ(registry.material(material)->texture_slots.at(
                material_texture_slot::kBaseColor),
            texture);
  EXPECT_FALSE(
      registry.material(material)->texture_slots.contains("baseColor"));

  result = registry.set_material_texture(
      material, material_texture_slot::kBaseColor, texture);
  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_FALSE(result.changed);
  EXPECT_EQ(result.message, "Material unchanged");

  result = registry.set_material_texture(material, "", texture);
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.message, "Material texture slot is empty");

  result = registry.set_material_texture(material, "normalTexture", texture);
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.message, "Unsupported material texture slot");

  result = registry.set_material_texture(
      material, material_texture_slot::kBaseColor, texture + 1000u);
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.message, "Texture asset does not exist");

  result = registry.set_material_texture(
      material + 1000u, material_texture_slot::kBaseColor, texture);
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.message, "Material asset does not exist");
}

TEST(AssetCommandServiceTests,
     AddCommandsUndoRedoMaterialTextureAndMeshWithStableIds) {
  AssetRegistry assets;
  EditorHistoryService history;
  AssetCommandService commands(assets, history);

  TextureAsset texture;
  texture.name = "Undoable Texture";
  texture.width = 16;
  texture.height = 8;
  AssetCreateResult texture_result = commands.add_texture(std::move(texture));
  ASSERT_TRUE(texture_result.ok) << texture_result.message;
  const AssetId texture_id = texture_result.asset;
  ASSERT_NE(texture_id, kInvalidAssetId);

  MaterialDocument material = make_default_quader_material("Undoable Material");
  AssetCreateResult material_result =
      commands.add_material(std::move(material));
  ASSERT_TRUE(material_result.ok) << material_result.message;
  const AssetId material_id = material_result.asset;
  ASSERT_NE(material_id, kInvalidAssetId);

  AssetCreateResult mesh_result =
      commands.add_mesh(make_test_triangle_mesh("Undoable Mesh"));
  ASSERT_TRUE(mesh_result.ok) << mesh_result.message;
  const AssetId mesh_id = mesh_result.asset;
  ASSERT_NE(mesh_id, kInvalidAssetId);

  ASSERT_NE(assets.texture(texture_id), nullptr);
  ASSERT_NE(assets.material(material_id), nullptr);
  ASSERT_NE(assets.mesh(mesh_id), nullptr);
  ASSERT_NE(history.undo_label(), nullptr);
  EXPECT_EQ(*history.undo_label(), "Add Mesh");

  ASSERT_TRUE(history.undo().ok);
  EXPECT_EQ(assets.mesh(mesh_id), nullptr);
  ASSERT_TRUE(history.undo().ok);
  EXPECT_EQ(assets.material(material_id), nullptr);
  ASSERT_TRUE(history.undo().ok);
  EXPECT_EQ(assets.texture(texture_id), nullptr);

  ASSERT_TRUE(history.redo().ok);
  ASSERT_NE(assets.texture(texture_id), nullptr);
  EXPECT_EQ(assets.texture(texture_id)->name, "Undoable Texture");
  ASSERT_TRUE(history.redo().ok);
  ASSERT_NE(assets.material(material_id), nullptr);
  EXPECT_EQ(assets.material(material_id)->name, "Undoable Material");
  ASSERT_TRUE(history.redo().ok);
  ASSERT_NE(assets.mesh(mesh_id), nullptr);
  EXPECT_EQ(assets.mesh(mesh_id)->name, "Undoable Mesh");
}

TEST(AssetCommandServiceTests,
     ReplaceAndUpdateCommandsUndoRedoMaterialTextureAndMeshPayloads) {
  AssetRegistry assets;
  SceneDocument scene;
  EditorHistoryService history;
  AssetCommandService commands(assets, history, &scene);

  const AssetId material =
      assets.add_material(make_default_quader_material("Source Material"));
  ASSERT_NE(material, kInvalidAssetId);
  AssetCommandResult material_update =
      commands.update_material(material, [](MaterialDocument &asset) {
        asset.base_color = {0.25f, 0.5f, 0.75f};
        asset.roughness = 0.35f;
      });
  ASSERT_TRUE(material_update.ok) << material_update.message;
  ASSERT_NE(assets.material(material), nullptr);
  EXPECT_FLOAT_EQ(assets.material(material)->roughness, 0.35f);
  ASSERT_TRUE(history.undo().ok);
  ASSERT_NE(assets.material(material), nullptr);
  EXPECT_FLOAT_EQ(assets.material(material)->roughness, 1.0f);
  ASSERT_TRUE(history.redo().ok);
  EXPECT_FLOAT_EQ(assets.material(material)->base_color.z, 0.75f);

  TextureAsset source_texture;
  source_texture.name = "Source Texture";
  source_texture.width = 4;
  const AssetId texture = assets.add_texture(std::move(source_texture));
  ASSERT_NE(texture, kInvalidAssetId);
  AssetCommandResult texture_update =
      commands.update_texture(texture, [](TextureAsset &asset) {
        asset.name = "Updated Texture";
        asset.width = 64;
      });
  ASSERT_TRUE(texture_update.ok) << texture_update.message;
  EXPECT_EQ(assets.texture(texture)->width, 64u);
  ASSERT_TRUE(history.undo().ok);
  EXPECT_EQ(assets.texture(texture)->name, "Source Texture");
  ASSERT_TRUE(history.redo().ok);
  EXPECT_EQ(assets.texture(texture)->name, "Updated Texture");

  const AssetId mesh = assets.add_mesh(make_test_triangle_mesh("Source Mesh"));
  ASSERT_NE(mesh, kInvalidAssetId);
  MeshAsset replacement = make_test_triangle_mesh("Replacement Mesh");
  replacement.vertices.push_back(MeshVertex{
      .position = {2.0f, 0.0f, 0.0f},
      .normal = {0.0f, 1.0f, 0.0f},
      .uv = {0.0f, 0.0f},
  });
  AssetCommandResult mesh_replace =
      commands.replace_mesh(mesh, std::move(replacement));
  ASSERT_TRUE(mesh_replace.ok) << mesh_replace.message;
  EXPECT_EQ(assets.mesh(mesh)->vertices.size(), 4u);
  ASSERT_TRUE(history.undo().ok);
  EXPECT_EQ(assets.mesh(mesh)->vertices.size(), 3u);
  EXPECT_EQ(assets.mesh(mesh)->name, "Source Mesh");
  ASSERT_TRUE(history.redo().ok);
  EXPECT_EQ(assets.mesh(mesh)->name, "Replacement Mesh");
}

TEST(AssetCommandServiceTests,
     RejectsModelingOwnedMeshReplacementWithoutRecordingHistory) {
  AssetRegistry assets;
  SceneDocument scene;
  EditorHistoryService history;
  AssetCommandService commands(assets, history, &scene);

  const AssetId mesh = assets.add_mesh(make_test_triangle_mesh("Owned Mesh"));
  const EntityId entity = scene.create_entity("Modeled");
  scene.set_mesh(entity, MeshComponent{.mesh = mesh});
  scene.set_modeling_object(entity, ModelingObjectComponent{.object_id = 7});

  AssetCommandResult result =
      commands.replace_mesh(mesh, make_test_triangle_mesh("Rejected"));
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.message, "Mesh asset is owned by a modeling object");
  EXPECT_FALSE(history.can_undo());
  ASSERT_NE(assets.mesh(mesh), nullptr);
  EXPECT_EQ(assets.mesh(mesh)->name, "Owned Mesh");
}

TEST(AssetProjectArchitectureTests,
     TypedStoreAliasesStampDocumentsWithoutAssetRegistryCeremony) {
  static_assert(std::is_same_v<MeshAssetStore,
                               TypedAssetStore<MeshAsset, MeshAssetRevision>>);
  static_assert(
      std::is_same_v<MaterialAssetStore,
                     TypedAssetStore<MaterialDocument, MaterialAssetRevision>>);
  static_assert(
      std::is_same_v<TextureAssetStore,
                     TypedAssetStore<TextureAsset, TextureAssetRevision>>);

  MeshAssetStore meshes;
  MeshAsset mesh;
  mesh.name = "Mesh Source";
  const AssetId mesh_id = meshes.add(41, std::move(mesh));
  ASSERT_EQ(mesh_id, 41u);
  ASSERT_NE(meshes.get(mesh_id), nullptr);
  EXPECT_EQ(meshes.get(mesh_id)->id, mesh_id);
  EXPECT_EQ(meshes.get(mesh_id)->revision.value, 1u);
  EXPECT_EQ(meshes.get(mesh_id)->name, "Mesh Source");

  ASSERT_TRUE(meshes.update(
      mesh_id, [](MeshAsset &asset) { asset.name = "Mesh Updated"; }));
  ASSERT_NE(meshes.get(mesh_id), nullptr);
  EXPECT_EQ(meshes.get(mesh_id)->revision.value, 2u);
  EXPECT_EQ(meshes.get(mesh_id)->name, "Mesh Updated");

  MaterialAssetStore materials;
  const AssetId material_id =
      materials.add(7, make_default_quader_material("Material Source"));
  ASSERT_NE(materials.get(material_id), nullptr);
  EXPECT_EQ(materials.get(material_id)->revision.value, 1u);
  EXPECT_EQ(materials.get(material_id)->name, "Material Source");

  TextureAssetStore textures;
  TextureAsset texture;
  texture.name = "Texture Source";
  texture.usage = TextureUsage::Mask;
  const AssetId texture_id = textures.add(9, std::move(texture));
  ASSERT_NE(textures.get(texture_id), nullptr);
  EXPECT_EQ(textures.get(texture_id)->revision.value, 1u);
  EXPECT_EQ(textures.get(texture_id)->usage, TextureUsage::Mask);
}

TEST(ProjectDocumentTests, OwnsSceneAssetsAndStableObjectRecords) {
  ProjectDocument document(ProjectDocumentRecord{
      .id = 17,
      .name = "Authored Project",
  });

  MeshAsset mesh_asset = make_test_triangle_mesh("Authored Mesh");
  const AssetId mesh = document.assets().add_mesh(std::move(mesh_asset));
  const EntityId entity = document.scene().create_entity("Authored Mesh");
  document.scene().set_mesh(entity, MeshComponent{.mesh = mesh});

  const EditorObjectId object =
      document.create_object_record(ProjectObjectRecord{
          .scene_entity = entity,
          .mesh = mesh,
          .authored_name = "Authored Mesh",
      });

  ASSERT_NE(object, kInvalidEditorObjectId);
  EXPECT_EQ(document.id(), 17u);
  EXPECT_EQ(document.name(), "Authored Project");
  EXPECT_EQ(document.revision(), 1u);
  const std::optional<ProjectObjectRecord> record =
      document.object_record(object);
  ASSERT_TRUE(record.has_value());
  EXPECT_EQ(record->id, object);
  EXPECT_EQ(record->scene_entity, entity);
  EXPECT_EQ(record->mesh, mesh);
  EXPECT_EQ(record->authored_name, "Authored Mesh");
  ASSERT_NE(document.scene().mesh(entity), nullptr);
  EXPECT_EQ(document.scene().mesh(entity)->mesh, mesh);

  EXPECT_EQ(document.upsert_object_record(ProjectObjectRecord{
                .id = 42,
                .scene_entity = entity,
                .mesh = mesh,
                .authored_name = "Imported Stable Record",
            }),
            42u);
  EXPECT_EQ(document.object_id_for_entity(entity), 42u);
  EXPECT_EQ(document.create_object_record(ProjectObjectRecord{}), 43u);
  const EntityId scene_root = document.scene().scene_root();
  const std::vector<ProjectObjectRecord> records = document.object_records();
  ASSERT_EQ(records.size(), 3u);
  EXPECT_EQ(records[0].scene_entity, scene_root);
  EXPECT_EQ(records[0].authored_name, "scene");
  EXPECT_EQ(records[1].id, 42u);
  EXPECT_EQ(records[2].id, 43u);

  document.rename("Renamed Project");
  EXPECT_EQ(document.name(), "Renamed Project");
  EXPECT_EQ(document.revision(), 4u);
}

TEST(ProjectDocumentTests,
     BulkUpsertsImportedObjectRecordsAfterOneSceneRecordSync) {
  ProjectDocument document(ProjectDocumentRecord{
      .id = 23,
      .name = "Imported Batch Project",
  });

  MeshAsset mesh_asset = make_test_triangle_mesh("Imported Mesh");
  const AssetId mesh = document.assets().add_mesh(std::move(mesh_asset));
  const EntityId first =
      document.scene().create_entity("First Imported Mesh", EntityKind::Mesh);
  const EntityId second =
      document.scene().create_entity("Second Imported Mesh", EntityKind::Mesh);
  document.scene().set_mesh(first, MeshComponent{.mesh = mesh});
  document.scene().set_mesh(second, MeshComponent{.mesh = mesh});

  const EditorObjectId first_object = document.scene().editor_object_id(first);
  const EditorObjectId second_object =
      document.scene().editor_object_id(second);
  ASSERT_NE(first_object, kInvalidEditorObjectId);
  ASSERT_NE(second_object, kInvalidEditorObjectId);

  const std::uint64_t revision_before_import = document.revision();
  const std::vector<EditorObjectId> imported = document.upsert_object_records({
      ProjectObjectRecord{
          .id = first_object,
          .scene_entity = first,
          .mesh = mesh,
          .authored_name = "Authored First",
      },
      ProjectObjectRecord{
          .id = second_object,
          .scene_entity = second,
          .mesh = mesh,
          .authored_name = "Authored Second",
      },
  });

  ASSERT_EQ(imported.size(), 2u);
  EXPECT_EQ(imported[0], first_object);
  EXPECT_EQ(imported[1], second_object);
  EXPECT_EQ(document.revision(), revision_before_import + 1u);
  EXPECT_EQ(document.object_id_for_entity(first), first_object);
  EXPECT_EQ(document.object_id_for_entity(second), second_object);

  const std::optional<ProjectObjectRecord> first_record =
      document.object_record(first_object);
  const std::optional<ProjectObjectRecord> second_record =
      document.object_record(second_object);
  ASSERT_TRUE(first_record.has_value());
  ASSERT_TRUE(second_record.has_value());
  EXPECT_EQ(first_record->authored_name, "Authored First");
  EXPECT_EQ(second_record->authored_name, "Authored Second");
}

TEST(ProjectDocumentTests, ValidatesAssetReferencesAtDocumentOwner) {
  ProjectDocument document(ProjectDocumentRecord{
      .id = 21,
      .name = "Reference Validation Project",
  });
  const EntityId entity =
      document.scene().create_entity("Missing Mesh", EntityKind::Mesh);
  document.scene().set_mesh(entity, MeshComponent{.mesh = 404u});
  document.scene().set_material(entity, MaterialComponent{
                                            .material = 405u,
                                            .material_slots = {{2u, 406u}},
                                        });

  const ProjectDocumentAssetReferenceReport scene_report =
      document.validate_asset_references();
  ASSERT_FALSE(scene_report.ok());
  ASSERT_GE(scene_report.issues.size(), 3u);
  EXPECT_NE(scene_report.issues[0].message.find("missing mesh asset"),
            std::string::npos);
  EXPECT_NE(scene_report.issues[1].message.find("missing material asset"),
            std::string::npos);

  const ProjectDocumentAssetReferenceReport record_report =
      document.validate_asset_references(ProjectObjectRecord{
          .id = 99u,
          .scene_entity = kInvalidEntityId,
          .mesh = 404u,
          .material = 405u,
      });
  ASSERT_FALSE(record_report.ok());
  EXPECT_NE(record_report.issues.front().message.find(
                "object record points at a missing scene entity"),
            std::string::npos);

  const std::string project_document_source =
      read_source_file("src/scene/document/project_document.cpp");
  const std::string qdr_reader_source =
      read_source_file("src/io/qdr/qdr_document_reader.cpp");
  EXPECT_NE(project_document_source.find(
                "ProjectDocument::validate_asset_references"),
            std::string::npos);
  EXPECT_NE(qdr_reader_source.find("document.validate_asset_references()"),
            std::string::npos);
  EXPECT_NE(
      qdr_reader_source.find("document.validate_asset_references(record)"),
      std::string::npos);
  EXPECT_EQ(qdr_reader_source.find("document.assets().mesh(mesh->mesh)"),
            std::string::npos);
  EXPECT_EQ(
      qdr_reader_source.find("document.assets().material(material->material)"),
      std::string::npos);
}

TEST(ProjectDocumentTests,
     RejectsEditorObjectIdCollisionsBetweenSceneEntities) {
  ProjectDocument document(ProjectDocumentRecord{
      .id = 19,
      .name = "Identity Collision Project",
  });

  const EntityId first =
      document.scene().create_entity("First", EntityKind::Mesh);
  const EntityId second =
      document.scene().create_entity("Second", EntityKind::Mesh);
  const EditorObjectId first_object = document.scene().editor_object_id(first);
  const EditorObjectId second_object =
      document.scene().editor_object_id(second);
  ASSERT_NE(first_object, kInvalidEditorObjectId);
  ASSERT_NE(second_object, kInvalidEditorObjectId);
  ASSERT_NE(first_object, second_object);

  EXPECT_EQ(document.upsert_object_record(ProjectObjectRecord{
                .id = first_object,
                .scene_entity = second,
                .authored_name = "Bad Collision",
            }),
            kInvalidEditorObjectId);
  EXPECT_EQ(document.scene().editor_object_id(first), first_object);
  EXPECT_EQ(document.scene().editor_object_id(second), second_object);
  EXPECT_EQ(document.entity_for_object_id(first_object), first);
  EXPECT_EQ(document.entity_for_object_id(second_object), second);

  const ProjectDocumentIdentityReport report =
      document.validate_object_identity_invariants();
  EXPECT_TRUE(report.ok()) << (report.issues.empty()
                                   ? std::string{}
                                   : report.issues.front().message);
}

TEST(ProjectDocumentTests,
     MirrorsEverySceneEntityIntoStableEditorObjectRecords) {
  ProjectDocument document(ProjectDocumentRecord{
      .id = 18,
      .name = "Identity Project",
  });

  const EntityId mesh =
      document.scene().create_entity("Mesh", EntityKind::Mesh);
  const EntityId group = document.scene().create_empty("Group");
  ASSERT_TRUE(document.scene().set_parent(mesh, group, false));

  const EditorObjectId mesh_object = document.scene().editor_object_id(mesh);
  const EditorObjectId group_object = document.scene().editor_object_id(group);
  const EditorObjectId root_object =
      document.scene().editor_object_id(document.scene().scene_root());
  ASSERT_NE(root_object, kInvalidEditorObjectId);
  ASSERT_NE(mesh_object, kInvalidEditorObjectId);
  ASSERT_NE(group_object, kInvalidEditorObjectId);
  EXPECT_NE(root_object, mesh_object);
  EXPECT_NE(root_object, group_object);
  EXPECT_NE(mesh_object, group_object);

  const std::vector<ProjectObjectRecord> records = document.object_records();
  ASSERT_EQ(records.size(), 3u);
  EXPECT_EQ(document.object_id_for_entity(document.scene().scene_root()),
            root_object);
  EXPECT_EQ(document.object_id_for_entity(mesh), mesh_object);
  EXPECT_EQ(document.object_id_for_entity(group), group_object);
  EXPECT_EQ(document.entity_for_object_id(root_object),
            document.scene().scene_root());
  EXPECT_EQ(document.entity_for_object_id(mesh_object), mesh);
  EXPECT_EQ(document.entity_for_object_id(group_object), group);
  EXPECT_TRUE(document.object_record(mesh_object).has_value());
  const std::optional<ProjectObjectRecord> group_record =
      document.object_record(group_object);
  ASSERT_TRUE(group_record.has_value());
  EXPECT_EQ(group_record->modeling_object, kInvalidModelingObjectId);

  const ProjectDocumentIdentityReport report =
      document.validate_object_identity_invariants();
  EXPECT_TRUE(report.ok()) << (report.issues.empty()
                                   ? std::string{}
                                   : report.issues.front().message);
}

TEST(ProjectDocumentTests,
     SceneEntitiesReserveStableObjectIdsFromProjectDocumentAuthority) {
  ProjectDocument document(ProjectDocumentRecord{
      .id = 20,
      .name = "Identity Authority Project",
  });

  const EntityId first =
      document.scene().create_entity("First", EntityKind::Mesh);
  ASSERT_NE(first, kInvalidEntityId);
  EXPECT_EQ(document.scene().editor_object_id(first), 2u);

  const EditorObjectId detached =
      document.create_object_record(ProjectObjectRecord{
          .authored_name = "Detached Object Record",
      });
  EXPECT_EQ(detached, 3u);

  const EntityId second =
      document.scene().create_entity("Second", EntityKind::Mesh);
  ASSERT_NE(second, kInvalidEntityId);
  EXPECT_EQ(document.scene().editor_object_id(second), 4u);

  const EntityId imported = document.scene().create_entity_with_id(
      50u, "Imported", EntityKind::Mesh, 99u);
  ASSERT_NE(imported, kInvalidEntityId);
  EXPECT_EQ(document.scene().editor_object_id(imported), 99u);

  const EditorObjectId after_imported =
      document.create_object_record(ProjectObjectRecord{
          .authored_name = "After Imported",
      });
  EXPECT_EQ(after_imported, 100u);

  ProjectDocument moved(std::move(document));
  const EntityId after_move =
      moved.scene().create_entity("After Move", EntityKind::Mesh);
  ASSERT_NE(after_move, kInvalidEntityId);
  EXPECT_EQ(moved.scene().editor_object_id(after_move), 101u);

  const ProjectDocumentIdentityReport report =
      moved.validate_object_identity_invariants();
  EXPECT_TRUE(report.ok()) << (report.issues.empty()
                                   ? std::string{}
                                   : report.issues.front().message);
}

TEST(ProjectDocumentTests, ConstObjectRecordQueriesDoNotMutateProjectRevision) {
  ProjectDocument document(ProjectDocumentRecord{
      .id = 21,
      .name = "Const Projection Project",
  });
  const EntityId entity =
      document.scene().create_entity("Projected Mesh", EntityKind::Mesh);
  const EditorObjectId object = document.scene().editor_object_id(entity);
  ASSERT_NE(object, kInvalidEditorObjectId);
  const std::uint64_t revision_before_queries = document.revision();

  const std::vector<ProjectObjectRecord> records =
      std::as_const(document).object_records();
  ASSERT_EQ(records.size(), 2u);
  EXPECT_EQ(records.front().scene_entity, document.scene().scene_root());
  EXPECT_EQ(records.back().id, object);
  const std::optional<ProjectObjectRecord> record =
      std::as_const(document).object_record(object);
  ASSERT_TRUE(record.has_value());
  EXPECT_EQ(record->scene_entity, entity);
  EXPECT_EQ(std::as_const(document).object_id_for_entity(entity), object);
  EXPECT_EQ(std::as_const(document).entity_for_object_id(object), entity);
  EXPECT_TRUE(
      std::as_const(document).validate_object_identity_invariants().ok());
  EXPECT_EQ(document.revision(), revision_before_queries);

  EXPECT_TRUE(document.synchronize_object_records_with_scene());
  EXPECT_EQ(document.revision(), revision_before_queries + 1u);
  EXPECT_FALSE(document.synchronize_object_records_with_scene());
  EXPECT_EQ(document.revision(), revision_before_queries + 1u);
}

TEST(ProjectDocumentTests,
     ExplicitObjectRecordSyncParticipatesInProjectSessionDirtyState) {
  ProjectSession session;
  const EntityId entity =
      session.scene().create_entity("Session Projected Mesh", EntityKind::Mesh);
  ASSERT_NE(entity, kInvalidEntityId);
  session.mark_clean();
  EXPECT_FALSE(session.dirty());

  EXPECT_EQ(std::as_const(session.document()).object_records().size(), 2u);
  EXPECT_FALSE(session.dirty());

  EXPECT_TRUE(session.document().synchronize_object_records_with_scene());
  EXPECT_TRUE(session.dirty());
  session.mark_clean();
  EXPECT_FALSE(session.dirty());
  EXPECT_FALSE(session.document().synchronize_object_records_with_scene());
  EXPECT_FALSE(session.dirty());
}

TEST(ProjectSessionTests,
     DirtyStateAggregatesProjectSceneAssetAndModelingContentRevisions) {
  ProjectSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_project_session(session));

  EXPECT_FALSE(editor.project().dirty());

  session.document().rename("Dirty Project");
  EXPECT_TRUE(editor.project().dirty());
  session.mark_clean();
  EXPECT_FALSE(editor.project().dirty());

  QuaderCreateEntityArgs entity_args;
  entity_args.name = "Dirty Entity";
  const QuaderApiResult<QuaderEntityRef> entity =
      editor.scene().create_entity(entity_args);
  ASSERT_TRUE(entity.ok()) << entity.error_message();
  EXPECT_TRUE(editor.project().dirty());
  session.mark_clean();
  EXPECT_FALSE(editor.project().dirty());

  QuaderTransform moved;
  moved.position = {1.0f, 2.0f, 3.0f};
  const QuaderApiVoidResult transformed =
      editor.scene().set_transform(entity.value().id, moved);
  ASSERT_TRUE(transformed.ok()) << transformed.error_message();
  EXPECT_TRUE(transformed.changed());
  EXPECT_TRUE(editor.project().dirty());
  session.mark_clean();
  EXPECT_FALSE(editor.project().dirty());

  const QuaderApiVoidResult hidden =
      editor.scene().set_visible(entity.value().id, false);
  ASSERT_TRUE(hidden.ok()) << hidden.error_message();
  EXPECT_TRUE(hidden.changed());
  EXPECT_TRUE(editor.project().dirty());
  session.mark_clean();
  EXPECT_FALSE(editor.project().dirty());

  const QuaderApiVoidResult locked =
      editor.scene().set_locked(entity.value().id, true);
  ASSERT_TRUE(locked.ok()) << locked.error_message();
  EXPECT_TRUE(locked.changed());
  EXPECT_TRUE(editor.project().dirty());
  session.mark_clean();
  EXPECT_FALSE(editor.project().dirty());

  QuaderMaterialArgs material_args;
  material_args.name = "Dirty Material";
  material_args.base_color = {0.8f, 0.7f, 0.6f};
  const QuaderApiResult<QuaderAssetRef> material =
      editor.assets().add_material(material_args);
  ASSERT_TRUE(material.ok()) << material.error_message();
  EXPECT_TRUE(editor.project().dirty());
  session.mark_clean();
  EXPECT_FALSE(editor.project().dirty());

  const QuaderApiVoidResult assigned =
      editor.assets().assign_material_to_entity(entity.value().id,
                                                material.value().id);
  ASSERT_TRUE(assigned.ok()) << assigned.error_message();
  EXPECT_TRUE(assigned.changed());
  EXPECT_TRUE(editor.project().dirty());
  session.mark_clean();
  EXPECT_FALSE(editor.project().dirty());

  const QuaderApiVoidResult recolored = editor.assets().set_material_base_color(
      material.value().id, {0.1f, 0.2f, 0.3f});
  ASSERT_TRUE(recolored.ok()) << recolored.error_message();
  EXPECT_TRUE(recolored.changed());
  EXPECT_TRUE(editor.project().dirty());
  session.mark_clean();
  EXPECT_FALSE(editor.project().dirty());

  const QuaderApiResult<QuaderModelingObjectRef> box =
      editor.modeling().create_box(
          QuaderBounds{.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
          "Dirty Modeling Box");
  ASSERT_TRUE(box.ok()) << box.error_message();
  ASSERT_TRUE(editor.selection().select_object(box.value()).ok());
  session.mark_clean();
  EXPECT_FALSE(editor.project().dirty());

  const QuaderApiVoidResult moved_selection =
      editor.modeling().move_selection({0.25f, 0.0f, 0.0f});
  ASSERT_TRUE(moved_selection.ok()) << moved_selection.error_message();
  EXPECT_TRUE(moved_selection.changed());
  EXPECT_TRUE(editor.project().dirty());
}

TEST(ProjectSessionTests,
     ReplaceDocumentAdvancesSceneRevisionPastPreviousDocument) {
  ProjectSession session;
  for (int i = 0; i < 8; ++i) {
    const EntityId entity =
        session.scene().create_entity("Previous Entity " + std::to_string(i));
    ASSERT_NE(entity, kInvalidEntityId);
  }
  const std::uint64_t previous_scene_revision = session.scene().revision();
  session.mark_clean();
  EXPECT_FALSE(session.dirty());

  ProjectDocument replacement;
  const EntityId light =
      replacement.scene().create_entity("Loaded Light", EntityKind::Light);
  ASSERT_NE(light, kInvalidEntityId);
  replacement.scene().set_light(light,
                                LightComponent{.type = LightType::Point});
  ASSERT_LT(replacement.scene().revision(), previous_scene_revision);

  session.replace_document(std::move(replacement));

  EXPECT_GT(session.scene().revision(), previous_scene_revision);
  EXPECT_FALSE(session.dirty());
  ASSERT_NE(session.scene().light(light), nullptr);
  EXPECT_EQ(session.scene().light(light)->type, LightType::Point);
}

TEST(ProjectSessionTests, PublicHistoryIgnoresUntrackedModelingSessionHistory) {
  ProjectSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_project_session(session));
  const auto entity_named = [&](std::string_view name) {
    const std::vector<QuaderEntitySnapshot> entities =
        editor.query().entities();
    return std::find_if(entities.begin(), entities.end(),
                        [&](const QuaderEntitySnapshot &snapshot) {
                          return snapshot.name == name;
                        }) != entities.end();
  };

  const auto hidden = session.modeling().create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Hidden Modeling Box",
  });
  ASSERT_TRUE(hidden.ok) << hidden.message;
  ASSERT_TRUE(entity_named("Hidden Modeling Box"));

  EXPECT_FALSE(editor.history().can_undo());
  EXPECT_FALSE(editor.history().can_redo());

  const QuaderApiVoidResult direct_undo = editor.history().undo();
  EXPECT_FALSE(direct_undo.ok());
  EXPECT_FALSE(direct_undo.changed());
  EXPECT_EQ(direct_undo.error_message(), "No command to undo");
  EXPECT_TRUE(entity_named("Hidden Modeling Box"));

  const QuaderApiVoidResult command_undo =
      editor.commands().execute(quader_command::kEditUndo);
  EXPECT_FALSE(command_undo.ok());
  EXPECT_FALSE(command_undo.changed());
  EXPECT_EQ(command_undo.error_message(), "Command is disabled.");
  EXPECT_EQ(command_undo.revisions.scene_revision,
            editor.revisions().scene_revision);
  EXPECT_EQ(command_undo.revisions.modeling_content_revision,
            editor.revisions().modeling_content_revision);
  EXPECT_TRUE(entity_named("Hidden Modeling Box"));

  const QuaderApiVoidResult unknown_tool =
      editor.tools().activate("__missing_tool__");
  EXPECT_FALSE(unknown_tool.ok());
  EXPECT_EQ(unknown_tool.error_message(), "Unknown viewport tool.");
  EXPECT_EQ(unknown_tool.revisions.scene_revision,
            editor.revisions().scene_revision);
  EXPECT_EQ(unknown_tool.revisions.modeling_content_revision,
            editor.revisions().modeling_content_revision);
}

TEST(ProjectSessionTests,
     QuickPivotOverrideFeedsTransformPivotAndClearsOnSelectionChange) {
  ProjectSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_project_session(session));
  const QuaderApiResult<QuaderModelingObjectRef> first =
      editor.modeling().create_box(
          QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                       .max = {1.0f, 1.0f, 1.0f}},
          "Quick Pivot A");
  ASSERT_TRUE(first.ok()) << first.error_message();
  const QuaderApiResult<QuaderModelingObjectRef> second =
      editor.modeling().create_box(
          QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                       .max = {1.0f, 1.0f, 1.0f}},
          "Quick Pivot B");
  ASSERT_TRUE(second.ok()) << second.error_message();
  ASSERT_TRUE(editor.selection()
                  .select_objects({first.value().entity, second.value().entity},
                                  second.value().entity)
                  .ok());

  session.pivot().begin_quick({4.0f, 5.0f, 6.0f});
  EXPECT_TRUE(session.pivot().quick_edit_active());
  std::optional<QVec3> pivot = editor.query().selection_transform_pivot();
  ASSERT_TRUE(pivot.has_value());
  expect_vec3_near(*pivot, {4.0f, 5.0f, 6.0f});

  session.pivot().set_quick_preview_position({7.0f, 8.0f, 9.0f});
  session.pivot().end_quick_edit();
  EXPECT_FALSE(session.pivot().quick_edit_active());
  pivot = editor.query().selection_transform_pivot();
  ASSERT_TRUE(pivot.has_value());
  expect_vec3_near(*pivot, {7.0f, 8.0f, 9.0f});

  ASSERT_TRUE(editor.selection().select_object(first.value()).ok());
  pivot = editor.query().selection_transform_pivot();
  ASSERT_TRUE(pivot.has_value());
  EXPECT_TRUE(length(*pivot - QVec3{7.0f, 8.0f, 9.0f}) > 0.0001f);

  session.pivot().begin_quick({1.0f, 2.0f, 3.0f});
  session.pivot().end_quick_edit();
  pivot = editor.query().selection_transform_pivot();
  ASSERT_TRUE(pivot.has_value());
  expect_vec3_near(*pivot, {1.0f, 2.0f, 3.0f});

  ASSERT_TRUE(editor.selection().clear().ok());
  EXPECT_FALSE(editor.query().selection_transform_pivot().has_value());
}

TEST(ProjectSessionTests,
     ServiceCreatesSessionsWithDocumentAssetsAndCommandsWiredTogether) {
  ProjectSessionService service;
  std::unique_ptr<ProjectSession> session =
      service.create_session(ProjectDocumentRecord{
          .id = 42,
          .name = "Architecture Session",
      });
  ASSERT_NE(session, nullptr);
  EXPECT_EQ(session->document().id(), 42u);
  EXPECT_EQ(session->document().name(), "Architecture Session");
  EXPECT_EQ(&session->scene(), &session->document().scene());
  EXPECT_EQ(&session->assets(), &session->document().assets());

  const AssetId material = session->assets().add_material(
      make_default_quader_material("Session Material"));
  ASSERT_NE(session->document().assets().material(material), nullptr);
  EXPECT_EQ(session->document().assets().material(material)->name,
            "Session Material");

  const DocumentCreateEntityResult result =
      session->document_commands().create_entity(
          "Session Entity", EntityKind::Mesh, kInvalidEntityId, {});
  ASSERT_TRUE(result.ok) << result.message;
  const EntityId entity = result.entity;
  ASSERT_NE(entity, kInvalidEntityId);
  EXPECT_TRUE(session->scene().exists(entity));
  EXPECT_TRUE(session->history().can_undo());
  ASSERT_TRUE(session->history().undo().ok);
  EXPECT_FALSE(session->scene().exists(entity));

  ProjectDocument existing(ProjectDocumentRecord{
      .id = 99,
      .name = "Opened Session",
  });
  const EntityId authored_entity =
      existing.scene().create_entity("Authored Mesh");
  const EditorObjectId object =
      existing.create_object_record(ProjectObjectRecord{
          .scene_entity = authored_entity,
          .authored_name = "Authored Mesh",
      });
  std::unique_ptr<ProjectSession> opened =
      service.open_session(std::move(existing));
  ASSERT_NE(opened, nullptr);
  EXPECT_EQ(opened->document().id(), 99u);
  EXPECT_EQ(opened->document().name(), "Opened Session");
  EXPECT_TRUE(opened->document().object_record(object).has_value());
  EXPECT_EQ(opened->document().entity_for_object_id(object), authored_entity);
}

TEST(ProjectSessionTests, ReadViewExposesConstProjectSceneAndAssetState) {
  static_assert(
      std::is_same_v<decltype(std::declval<ProjectSessionReadView>().document),
                     const ProjectDocument &>);
  static_assert(
      std::is_same_v<decltype(std::declval<ProjectSessionReadView>().scene),
                     const SceneDocument &>);
  static_assert(
      std::is_same_v<decltype(std::declval<ProjectSessionReadView>().assets),
                     const AssetRegistry &>);

  ProjectSession session;
  const ProjectContentRevisionStamp before = session.content_revision_stamp();
  const ProjectSessionReadView view = session.read_view();

  EXPECT_EQ(&view.document, &std::as_const(session).document());
  EXPECT_EQ(&view.scene, &std::as_const(session).scene());
  EXPECT_EQ(&view.assets, &std::as_const(session).assets());
  EXPECT_EQ(&view.modeling, &std::as_const(session).modeling());
  EXPECT_EQ(&view.selection, &std::as_const(session).selection());
  EXPECT_EQ(view.document.revision(), before.project_revision);
  EXPECT_EQ(view.scene.revision(), before.scene_revision);
  EXPECT_EQ(view.assets.revision(), before.asset_revision);
  EXPECT_EQ(session.content_revision_stamp(), before);
}

TEST(ProjectSessionTests,
     DefaultAssetBootstrapServiceCreatesRuntimeDefaultMaterial) {
  ProjectSession session;
  const ProjectDefaultAssets defaults =
      ProjectAssetBootstrapService{}.bootstrap_default_assets(session);

  ASSERT_NE(defaults.albedo_texture, kInvalidAssetId);
  ASSERT_NE(defaults.roughness_texture, kInvalidAssetId);
  ASSERT_NE(defaults.default_material, kInvalidAssetId);
  const ProjectSessionReadView view = session.read_view();
  EXPECT_EQ(view.assets.texture_count(), 2u);
  EXPECT_EQ(view.assets.material_count(), 1u);

  const TextureAsset *albedo = view.assets.texture(defaults.albedo_texture);
  ASSERT_NE(albedo, nullptr);
  EXPECT_EQ(std::string_view(albedo->name), kDefaultMaterialAlbedoTextureName);
  EXPECT_EQ(albedo->usage, TextureUsage::BaseColor);
  EXPECT_EQ(albedo->color_space, TextureColorSpace::Srgb);
  EXPECT_EQ(albedo->source_path.generic_string(),
            std::string(kDefaultMaterialAlbedoTexturePackagePath));

  const TextureAsset *roughness =
      view.assets.texture(defaults.roughness_texture);
  ASSERT_NE(roughness, nullptr);
  EXPECT_EQ(std::string_view(roughness->name),
            kDefaultMaterialRoughnessTextureName);
  EXPECT_EQ(roughness->usage, TextureUsage::Roughness);
  EXPECT_EQ(roughness->color_space, TextureColorSpace::Linear);
  EXPECT_EQ(roughness->source_path.generic_string(),
            std::string(kDefaultMaterialRoughnessTexturePackagePath));

  const MaterialDocument *material =
      view.assets.material(defaults.default_material);
  ASSERT_NE(material, nullptr);
  EXPECT_EQ(material->name, "Default Quader");
  EXPECT_EQ(
      material_texture_slot_asset(*material, material_texture_slot::kBaseColor),
      std::optional<AssetId>{defaults.albedo_texture});
  EXPECT_FALSE(
      material_texture_slot_asset(*material, material_texture_slot::kRoughness)
          .has_value());
}

TEST(AssetProjectArchitectureTests,
     SourceExposesNamedAssetAndProjectArchitectureTypes) {
  const std::string asset_document_source =
      read_source_file("src/assets/asset_document.hpp");
  const std::string typed_store_source =
      read_source_file("src/assets/typed_asset_store.hpp");
  const std::string registry_source =
      read_source_file("src/assets/asset_registry.hpp");
  const std::string project_document_source =
      read_source_file("src/scene/document/project_document.hpp");
  const std::string project_session_source =
      read_source_file("src/editor/session/project_session.hpp");
  const std::string project_session_compat_source =
      read_source_file("src/scene/document/project_session.hpp");

  EXPECT_NE(asset_document_source.find("struct AssetDocumentBase"),
            std::string::npos);
  EXPECT_NE(typed_store_source.find("class TypedAssetStore"),
            std::string::npos);
  EXPECT_EQ(typed_store_source.find("    Document* get(AssetId id) {"),
            std::string::npos);
  EXPECT_NE(typed_store_source.find("const Document* get(AssetId id) const"),
            std::string::npos);
  EXPECT_NE(
      registry_source.find("using MeshAssetStore = TypedAssetStore<MeshAsset, "
                           "MeshAssetRevision>;"),
      std::string::npos);
  EXPECT_NE(registry_source.find(
                "using MaterialAssetStore = TypedAssetStore<MaterialDocument, "
                "MaterialAssetRevision>;"),
            std::string::npos);
  EXPECT_NE(registry_source.find(
                "using TextureAssetStore = TypedAssetStore<TextureAsset, "
                "TextureAssetRevision>;"),
            std::string::npos);
  EXPECT_EQ(registry_source.find("class MeshAssetStore"), std::string::npos);
  EXPECT_EQ(registry_source.find("class MaterialAssetStore"),
            std::string::npos);
  EXPECT_EQ(registry_source.find("class TextureAssetStore"), std::string::npos);
  EXPECT_NE(registry_source.find("MeshAssetStore meshes_"), std::string::npos);
  EXPECT_NE(registry_source.find("MaterialAssetStore materials_"),
            std::string::npos);
  EXPECT_NE(registry_source.find("TextureAssetStore textures_"),
            std::string::npos);
  EXPECT_EQ(registry_source.find("    MeshAsset* mesh(AssetId id);"),
            std::string::npos);
  EXPECT_EQ(registry_source.find("    MaterialDocument* material(AssetId id);"),
            std::string::npos);
  EXPECT_EQ(registry_source.find("    TextureAsset* texture(AssetId id);"),
            std::string::npos);
  EXPECT_NE(registry_source.find("const MeshAsset *mesh(AssetId id) const"),
            std::string::npos);
  EXPECT_NE(registry_source.find(
                "const MaterialDocument *material(AssetId id) const"),
            std::string::npos);
  EXPECT_NE(
      registry_source.find("const TextureAsset *texture(AssetId id) const"),
      std::string::npos);
  EXPECT_NE(project_document_source.find(
                "bool synchronize_object_records_with_scene()"),
            std::string::npos);
  EXPECT_NE(
      project_document_source.find("bool sync_object_records_with_scene()"),
      std::string::npos);
  EXPECT_EQ(
      project_document_source.find("sync_object_records_with_scene() const"),
      std::string::npos);
  EXPECT_EQ(project_document_source.find("mutable std::unordered_map"),
            std::string::npos);
  EXPECT_NE(project_session_source.find("class ProjectSession"),
            std::string::npos);
  EXPECT_NE(project_session_source.find("class ProjectSessionService final"),
            std::string::npos);
  EXPECT_NE(
      project_session_source.find("open_session(ProjectDocument document)"),
      std::string::npos);
  EXPECT_NE(project_session_compat_source.find(
                "#include \"editor/session/project_session.hpp\""),
            std::string::npos);
  EXPECT_EQ(project_session_compat_source.find("class ProjectSession"),
            std::string::npos);
}

TEST(DocumentCommandServiceTests, RecordsUndoRedoForNativeSceneEdits) {
  SceneDocument scene;
  const EntityId entity = scene.create_entity("Brush");
  EditorHistoryService history;
  DocumentCommandService commands(scene, history);

  DocumentCommandResult result = commands.rename_entity(entity, "Brush_1");
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_TRUE(result.changed);
  ASSERT_NE(scene.name(entity), nullptr);
  EXPECT_EQ(scene.name(entity)->name, "Brush_1");
  EXPECT_TRUE(history.can_undo());
  EXPECT_FALSE(history.can_redo());
  ASSERT_NE(history.undo_label(), nullptr);
  EXPECT_EQ(*history.undo_label(), "Rename Entity");

  result = history.undo();
  ASSERT_TRUE(result.ok) << result.message;
  ASSERT_NE(scene.name(entity), nullptr);
  EXPECT_EQ(scene.name(entity)->name, "Brush");
  EXPECT_FALSE(history.can_undo());
  EXPECT_TRUE(history.can_redo());

  result = history.redo();
  ASSERT_TRUE(result.ok) << result.message;
  ASSERT_NE(scene.name(entity), nullptr);
  EXPECT_EQ(scene.name(entity)->name, "Brush_1");

  TransformComponent transform;
  transform.position = {3.0f, 2.0f, 1.0f};
  ASSERT_TRUE(commands.set_transform(entity, transform).ok);
  ASSERT_NE(scene.transform(entity), nullptr);
  EXPECT_FLOAT_EQ(scene.transform(entity)->position.x, 3.0f);
  EXPECT_TRUE(history.undo().ok);
  ASSERT_NE(scene.transform(entity), nullptr);
  EXPECT_FLOAT_EQ(scene.transform(entity)->position.x, 0.0f);
}

TEST(EditorCommandServiceTests, ExecutesCommandsThroughSharedUndoStack) {
  SceneDocument scene;
  const EntityId entity = scene.create_entity("Brush");
  EditorHistoryService history;
  DocumentCommandService document_commands(scene, history);
  EditorCommandService editor_commands(history, document_commands);

  CommandResult result = editor_commands.execute(
      std::make_unique<RenameEntityCommand>(scene, entity, "Brush", "Brush_1"));
  ASSERT_TRUE(result.ok) << result.message;
  ASSERT_NE(scene.name(entity), nullptr);
  EXPECT_EQ(scene.name(entity)->name, "Brush_1");
  EXPECT_TRUE(editor_commands.can_undo());

  result = editor_commands.undo();
  ASSERT_TRUE(result.ok) << result.message;
  ASSERT_NE(scene.name(entity), nullptr);
  EXPECT_EQ(scene.name(entity)->name, "Brush");
  EXPECT_TRUE(editor_commands.can_redo());

  result = editor_commands.redo();
  ASSERT_TRUE(result.ok) << result.message;
  ASSERT_NE(scene.name(entity), nullptr);
  EXPECT_EQ(scene.name(entity)->name, "Brush_1");
}

TEST(EditorHistoryServiceTests,
     TransactionsRecordAlreadyExecutedCommandsAsOneUndoItem) {
  EditorHistoryService history;
  int first = 0;
  int second = 0;

  EditorTransaction transaction = history.begin_transaction("Two Edits");
  ASSERT_TRUE(
      transaction
          .execute(std::make_unique<TestValueCommand>("First", first, 0, 1))
          .ok);
  ASSERT_TRUE(
      transaction
          .execute(std::make_unique<TestValueCommand>("Second", second, 0, 2))
          .ok);
  CommandResult result = transaction.commit();
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_TRUE(history.can_undo());
  ASSERT_NE(history.undo_label(), nullptr);
  EXPECT_EQ(*history.undo_label(), "Two Edits");

  ASSERT_TRUE(history.undo().ok);
  EXPECT_EQ(first, 0);
  EXPECT_EQ(second, 0);
  EXPECT_TRUE(history.can_redo());

  ASSERT_TRUE(history.redo().ok);
  EXPECT_EQ(first, 1);
  EXPECT_EQ(second, 2);
}

TEST(DocumentCommandTests, TypedCommandsExecuteDirectlyThroughUndoRedoManager) {
  SceneDocument scene;
  const EntityId entity = scene.create_entity("Brush");
  UndoRedoManager undo_redo;

  UndoRedoResult result = undo_redo.execute(
      std::make_unique<RenameEntityCommand>(scene, entity, "Brush", "Brush_1"));
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_TRUE(result.changed);
  ASSERT_NE(scene.name(entity), nullptr);
  EXPECT_EQ(scene.name(entity)->name, "Brush_1");
  ASSERT_NE(undo_redo.undo_label(), nullptr);
  EXPECT_EQ(*undo_redo.undo_label(), "Rename Entity");

  ASSERT_TRUE(undo_redo.undo().ok);
  EXPECT_EQ(scene.name(entity)->name, "Brush");
  ASSERT_TRUE(undo_redo.redo().ok);
  EXPECT_EQ(scene.name(entity)->name, "Brush_1");

  TransformComponent before_transform = *scene.transform(entity);
  TransformComponent after_transform = before_transform;
  after_transform.position = {4.0f, 5.0f, 6.0f};

  std::vector<std::unique_ptr<IUndoableCommand>> batch_commands;
  batch_commands.push_back(
      std::make_unique<SetVisibilityCommand>(scene, entity, true, false));
  batch_commands.push_back(
      std::make_unique<SetLockCommand>(scene, entity, false, true));
  batch_commands.push_back(std::make_unique<ApplyEntityTransformCommand>(
      scene, entity, before_transform, after_transform, "Set Transform"));
  result = undo_redo.execute(std::make_unique<BatchDocumentCommand>(
      "Batch Edit", std::move(batch_commands)));
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_FALSE(scene.visibility(entity)->visible);
  EXPECT_TRUE(scene.lock(entity)->locked);
  EXPECT_FLOAT_EQ(scene.transform(entity)->position.x, 4.0f);
  ASSERT_NE(undo_redo.undo_label(), nullptr);
  EXPECT_EQ(*undo_redo.undo_label(), "Batch Edit");

  ASSERT_TRUE(undo_redo.undo().ok);
  EXPECT_TRUE(scene.visibility(entity)->visible);
  EXPECT_FALSE(scene.lock(entity)->locked);
  EXPECT_FLOAT_EQ(scene.transform(entity)->position.x, 0.0f);

  int value = 0;
  result = undo_redo.execute(
      std::make_unique<TestValueCommand>("Value Edit", value, 0, 1));
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_EQ(value, 1);
  ASSERT_TRUE(undo_redo.undo().ok);
  EXPECT_EQ(value, 0);
}

TEST(DocumentCommandTests, UndoRedoManagerCoalescesOnlyExplicitMergeKeys) {
  int value = 0;
  UndoRedoManager undo_redo;

  UndoRedoResult result = undo_redo.execute(
      std::make_unique<TestValueCommand>("Set Value", value, 0, 1, nullptr,
                                         nullptr, true, true, "value.drag"),
      UndoRedoExecuteOptions{.merge_key = "value.drag"});
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_EQ(value, 1);

  result = undo_redo.execute(
      std::make_unique<TestValueCommand>("Set Value", value, 1, 2, nullptr,
                                         nullptr, true, true, "value.drag"),
      UndoRedoExecuteOptions{.merge_key = "value.drag"});
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_EQ(value, 2);

  ASSERT_TRUE(undo_redo.undo().ok);
  EXPECT_EQ(value, 0);
  EXPECT_FALSE(undo_redo.can_undo());
  ASSERT_TRUE(undo_redo.redo().ok);
  EXPECT_EQ(value, 2);

  result = undo_redo.execute(
      std::make_unique<TestValueCommand>("Set Value", value, 2, 3));
  ASSERT_TRUE(result.ok) << result.message;
  result = undo_redo.execute(
      std::make_unique<TestValueCommand>("Set Value", value, 3, 4));
  ASSERT_TRUE(result.ok) << result.message;
  ASSERT_TRUE(undo_redo.undo().ok);
  EXPECT_EQ(value, 3);
}

TEST(DocumentCommandTests,
     EditorTransactionsCoalesceExplicitMergeKeysBeforeCommit) {
  int value = 0;
  EditorHistoryService history;

  EditorTransaction transaction = history.begin_transaction("Value Drag");
  ASSERT_TRUE(transaction
                  .execute(std::make_unique<TestValueCommand>(
                               "Set Value", value, 0, 1, nullptr, nullptr, true,
                               true, "value.drag"),
                           UndoRedoExecuteOptions{.merge_key = "value.drag"})
                  .ok);
  ASSERT_TRUE(transaction
                  .execute(std::make_unique<TestValueCommand>(
                               "Set Value", value, 1, 2, nullptr, nullptr, true,
                               true, "value.drag"),
                           UndoRedoExecuteOptions{.merge_key = "value.drag"})
                  .ok);
  ASSERT_TRUE(transaction.commit().ok);

  EXPECT_EQ(value, 2);
  ASSERT_TRUE(history.undo().ok);
  EXPECT_EQ(value, 0);
  EXPECT_FALSE(history.can_undo());
  ASSERT_TRUE(history.redo().ok);
  EXPECT_EQ(value, 2);
}

TEST(DocumentCommandTests, SceneBeforeAfterCommandsSupportExplicitMergeKeys) {
  SceneDocument scene;
  const EntityId entity = scene.create_entity("Brush");
  UndoRedoManager undo_redo;

  TransformComponent before = *scene.transform(entity);
  TransformComponent middle = before;
  middle.position.x = 1.0f;
  TransformComponent after = before;
  after.position.x = 2.0f;

  UndoRedoResult result =
      undo_redo.execute(std::make_unique<ApplyEntityTransformCommand>(
                            scene, entity, before, middle, "Set Transform"),
                        UndoRedoExecuteOptions{.merge_key = "transform.drag"});
  ASSERT_TRUE(result.ok) << result.message;
  result =
      undo_redo.execute(std::make_unique<ApplyEntityTransformCommand>(
                            scene, entity, middle, after, "Set Transform"),
                        UndoRedoExecuteOptions{.merge_key = "transform.drag"});
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_FLOAT_EQ(scene.transform(entity)->position.x, 2.0f);

  ASSERT_TRUE(undo_redo.undo().ok);
  EXPECT_FLOAT_EQ(scene.transform(entity)->position.x, 0.0f);
  EXPECT_FALSE(undo_redo.can_undo());
  ASSERT_TRUE(undo_redo.redo().ok);
  EXPECT_FLOAT_EQ(scene.transform(entity)->position.x, 2.0f);

  TransformComponent third = after;
  third.position.x = 3.0f;
  TransformComponent fourth = after;
  fourth.position.x = 4.0f;

  result = undo_redo.execute(
      std::make_unique<ApplyEntityTransformCommand>(scene, entity, after, third,
                                                    "Set Transform"),
      UndoRedoExecuteOptions{.merge_key = "transform.drag.second"});
  ASSERT_TRUE(result.ok) << result.message;
  result = undo_redo.execute(
      std::make_unique<ApplyEntityTransformCommand>(scene, entity, third,
                                                    fourth, "Set Transform"),
      UndoRedoExecuteOptions{.merge_key = "transform.drag.second"});
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_FLOAT_EQ(scene.transform(entity)->position.x, 4.0f);

  ASSERT_TRUE(undo_redo.undo().ok);
  EXPECT_FLOAT_EQ(scene.transform(entity)->position.x, 2.0f);
  ASSERT_TRUE(undo_redo.undo().ok);
  EXPECT_FLOAT_EQ(scene.transform(entity)->position.x, 0.0f);
}

TEST(DocumentCommandTests, BatchDocumentCommandRollsBackFailedRedoAndUndo) {
  UndoRedoManager undo_redo;

  int first = 0;
  int second = 0;
  bool fail_second_redo = true;
  std::vector<std::unique_ptr<IUndoableCommand>> redo_commands;
  redo_commands.push_back(
      std::make_unique<TestValueCommand>("First", first, 0, 1));
  redo_commands.push_back(std::make_unique<TestValueCommand>(
      "Second", second, 0, 2, nullptr, &fail_second_redo));

  UndoRedoResult result =
      undo_redo.execute(std::make_unique<BatchDocumentCommand>(
          "Failing Redo Batch", std::move(redo_commands)));
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(first, 0);
  EXPECT_EQ(second, 0);
  EXPECT_FALSE(undo_redo.can_undo());

  bool fail_first_undo = false;
  std::vector<std::unique_ptr<IUndoableCommand>> undo_commands;
  undo_commands.push_back(std::make_unique<TestValueCommand>(
      "First", first, 0, 1, &fail_first_undo));
  undo_commands.push_back(
      std::make_unique<TestValueCommand>("Second", second, 0, 2));

  result = undo_redo.execute(std::make_unique<BatchDocumentCommand>(
      "Failing Undo Batch", std::move(undo_commands)));
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_EQ(first, 1);
  EXPECT_EQ(second, 2);

  fail_first_undo = true;
  result = undo_redo.undo();
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(first, 1);
  EXPECT_EQ(second, 2);
  EXPECT_TRUE(undo_redo.can_undo());
  EXPECT_FALSE(undo_redo.can_redo());
}

TEST(DocumentCommandTests,
     SceneTransformPreviewTransactionCommitsAndCancelsThroughEditorHistory) {
  SceneDocument scene;
  const EntityId entity = scene.create_entity("Brush");
  EditorHistoryService history;

  TransformComponent preview_transform = *scene.transform(entity);
  preview_transform.position = {2.0f, 0.0f, 0.0f};
  {
    SceneTransformPreviewTransaction transaction(scene, {entity},
                                                 "Move Preview");
    DocumentCommandResult result =
        transaction.preview_transform(entity, preview_transform);
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_TRUE(result.changed);
    ASSERT_NE(scene.transform(entity), nullptr);
    EXPECT_FLOAT_EQ(scene.transform(entity)->position.x, 2.0f);

    result = transaction.cancel();
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_TRUE(result.changed);
    ASSERT_NE(scene.transform(entity), nullptr);
    EXPECT_FLOAT_EQ(scene.transform(entity)->position.x, 0.0f);
  }

  preview_transform.position = {4.0f, 0.0f, 0.0f};
  SceneTransformPreviewTransaction transaction(scene, {entity}, "Move Preview");
  ASSERT_TRUE(transaction.preview_transform(entity, preview_transform).ok);
  DocumentCommandResult result = transaction.commit(history);
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_TRUE(result.changed);
  ASSERT_NE(scene.transform(entity), nullptr);
  EXPECT_FLOAT_EQ(scene.transform(entity)->position.x, 4.0f);
  ASSERT_NE(history.undo_label(), nullptr);
  EXPECT_EQ(*history.undo_label(), "Move Preview");

  ASSERT_TRUE(history.undo().ok);
  ASSERT_NE(scene.transform(entity), nullptr);
  EXPECT_FLOAT_EQ(scene.transform(entity)->position.x, 0.0f);
  ASSERT_TRUE(history.redo().ok);
  ASSERT_NE(scene.transform(entity), nullptr);
  EXPECT_FLOAT_EQ(scene.transform(entity)->position.x, 4.0f);
}

TEST(DocumentCommandTests,
     SceneTransformPreviewTransactionIndexesPreviewEditsByEntity) {
  const std::string source = read_source_file(
      "src/editor/history/scene_transform_preview_transaction.hpp");

  EXPECT_NE(
      source.find(
          "std::unordered_map<EntityId, std::size_t> edit_index_by_entity_"),
      std::string::npos);
  EXPECT_NE(source.find("edit_index_by_entity_.emplace(entity, edit_index)"),
            std::string::npos);
  EXPECT_NE(source.find("edit_index_by_entity_.find(entity)"),
            std::string::npos);
  EXPECT_NE(source.find("scene_.set_transforms(assignments)"),
            std::string::npos);
  EXPECT_EQ(source.find("std::find_if(edits_.begin()"), std::string::npos);
}

TEST(DocumentCommandTests,
     SceneTransformDragServiceFiltersTargetsAndRecordsScenePreviewThroughApi) {
  ProjectSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_project_session(session));
  SceneDocument &scene = session.scene();

  const EntityId parent = scene.create_entity("Parent");
  const EntityId child = scene.create_entity("Child");
  ASSERT_TRUE(scene.set_parent(child, parent));
  ASSERT_TRUE(scene.set_selected(parent, true));
  ASSERT_TRUE(scene.set_selected(child, true));
  ASSERT_TRUE(scene.set_active_selection(parent));

  SceneTransformDragService service;
  ASSERT_TRUE(service.begin(scene, QVec3{}, true, "Move Preview"));
  ASSERT_TRUE(service.preview_active());
  ASSERT_EQ(service.targets().size(), 1u);
  EXPECT_EQ(service.targets().front().entity, parent);

  TransformComponent preview = service.targets().front().transform;
  preview.position = {2.0f, 0.0f, 0.0f};
  DocumentCommandResult preview_result =
      service.preview_transform(parent, preview);
  ASSERT_TRUE(preview_result.ok) << preview_result.message;
  EXPECT_TRUE(preview_result.changed);
  ASSERT_NE(scene.transform(parent), nullptr);
  EXPECT_FLOAT_EQ(scene.transform(parent)->position.x, 2.0f);

  DocumentCommandResult cancel_result = service.cancel();
  ASSERT_TRUE(cancel_result.ok) << cancel_result.message;
  EXPECT_TRUE(cancel_result.changed);
  EXPECT_FALSE(service.preview_active());
  EXPECT_TRUE(service.targets().empty());
  ASSERT_NE(scene.transform(parent), nullptr);
  EXPECT_FLOAT_EQ(scene.transform(parent)->position.x, 0.0f);

  ASSERT_TRUE(service.begin(scene, QVec3{}, true, "Move Preview"));
  preview = service.targets().front().transform;
  preview.position = {4.0f, 0.0f, 0.0f};
  ASSERT_TRUE(service.preview_transform(parent, preview).ok);
  QuaderApiVoidResult commit = service.commit(editor);
  ASSERT_TRUE(commit.ok) << commit.message;
  EXPECT_TRUE(commit.changed);
  EXPECT_FALSE(service.preview_active());
  EXPECT_TRUE(service.targets().empty());
  ASSERT_NE(scene.transform(parent), nullptr);
  EXPECT_FLOAT_EQ(scene.transform(parent)->position.x, 4.0f);
  ASSERT_NE(session.history().undo_label(), nullptr);
  EXPECT_EQ(*session.history().undo_label(), "Move Preview");

  ASSERT_TRUE(session.history().undo().ok);
  ASSERT_NE(scene.transform(parent), nullptr);
  EXPECT_FLOAT_EQ(scene.transform(parent)->position.x, 0.0f);
  ASSERT_TRUE(session.history().redo().ok);
  ASSERT_NE(scene.transform(parent), nullptr);
  EXPECT_FLOAT_EQ(scene.transform(parent)->position.x, 4.0f);
}

TEST(DocumentCommandTests,
     SceneTransformDragServicePreviewsInspectorPrimaryTransformsBeforeCommit) {
  constexpr float kHalfPi = 1.57079632679f;

  ProjectSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_project_session(session));
  SceneDocument &scene = session.scene();
  AssetRegistry &assets = session.assets();

  const EntityId entity = scene.create_entity("Entity");
  ASSERT_TRUE(scene.set_selected(entity, true));
  ASSERT_TRUE(scene.set_active_selection(entity));

  SceneTransformDragService service;
  ASSERT_TRUE(service.begin_move_selected_to_primary_position_preview(
      scene, entity, "Set Transform"));
  DocumentCommandResult preview = service.preview_selected_to_primary_position(
      scene, entity, {2.0f, 0.0f, 0.0f});
  ASSERT_TRUE(preview.ok) << preview.message;
  EXPECT_TRUE(preview.changed);
  ASSERT_NE(scene.transform(entity), nullptr);
  EXPECT_FLOAT_EQ(scene.transform(entity)->position.x, 2.0f);

  QuaderApiVoidResult commit = service.commit(editor);
  ASSERT_TRUE(commit.ok) << commit.message;
  EXPECT_TRUE(commit.changed);
  ASSERT_NE(session.history().undo_label(), nullptr);
  EXPECT_EQ(*session.history().undo_label(), "Set Transform");
  ASSERT_TRUE(session.history().undo().ok);
  ASSERT_NE(scene.transform(entity), nullptr);
  EXPECT_FLOAT_EQ(scene.transform(entity)->position.x, 0.0f);

  ASSERT_TRUE(service.begin_rotate_selected_to_primary_euler_preview(
      scene, assets, entity, "Set Transform"));
  preview = service.preview_selected_to_primary_euler(scene, entity,
                                                      {0.0f, 0.0f, kHalfPi});
  ASSERT_TRUE(preview.ok) << preview.message;
  EXPECT_TRUE(preview.changed);
  ASSERT_NE(scene.transform(entity), nullptr);
  EXPECT_NEAR(scene.transform(entity)->rotation_euler.z, kHalfPi, 0.001f);
  DocumentCommandResult cancel = service.cancel();
  ASSERT_TRUE(cancel.ok) << cancel.message;
  EXPECT_TRUE(cancel.changed);
  ASSERT_NE(scene.transform(entity), nullptr);
  EXPECT_NEAR(scene.transform(entity)->rotation_euler.z, 0.0f, 0.001f);

  ASSERT_TRUE(service.begin_scale_selected_to_primary_scale_preview(
      scene, assets, entity, "Set Transform"));
  preview = service.preview_selected_to_primary_scale(scene, entity,
                                                      {2.0f, 3.0f, 4.0f});
  ASSERT_TRUE(preview.ok) << preview.message;
  EXPECT_TRUE(preview.changed);
  ASSERT_NE(scene.transform(entity), nullptr);
  EXPECT_FLOAT_EQ(scene.transform(entity)->scale.x, 2.0f);
  EXPECT_FLOAT_EQ(scene.transform(entity)->scale.y, 3.0f);
  EXPECT_FLOAT_EQ(scene.transform(entity)->scale.z, 4.0f);
  commit = service.commit(editor);
  ASSERT_TRUE(commit.ok) << commit.message;
  EXPECT_TRUE(commit.changed);
  ASSERT_TRUE(session.history().undo().ok);
  ASSERT_NE(scene.transform(entity), nullptr);
  EXPECT_FLOAT_EQ(scene.transform(entity)->scale.x, 1.0f);
  EXPECT_FLOAT_EQ(scene.transform(entity)->scale.y, 1.0f);
  EXPECT_FLOAT_EQ(scene.transform(entity)->scale.z, 1.0f);
}

TEST(DocumentCommandTests,
     SceneTransformDragServiceInspectorMoveUsesSharedTargetPolicy) {
  ProjectSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_project_session(session));
  SceneDocument &scene = session.scene();

  const EntityId parent = scene.create_entity("Parent");
  const EntityId child = scene.create_entity("Child");
  const EntityId locked = scene.create_entity("Locked");
  ASSERT_TRUE(scene.set_parent(child, parent, false));

  TransformComponent child_transform;
  child_transform.position = {1.0f, 0.0f, 0.0f};
  scene.set_transform(child, child_transform);
  TransformComponent locked_transform;
  locked_transform.position = {7.0f, 0.0f, 0.0f};
  scene.set_transform(locked, locked_transform);
  ASSERT_TRUE(scene.set_locked(locked, true));

  ASSERT_TRUE(scene.set_selected(parent, true));
  ASSERT_TRUE(scene.set_selected(child, true));
  ASSERT_TRUE(scene.set_selected(locked, true));
  ASSERT_TRUE(scene.set_active_selection(parent));

  SceneTransformDragService service;
  const QuaderApiVoidResult result = service.move_selected_to_primary_position(
      editor, scene, parent, {3.0f, 0.0f, 0.0f}, "Set Transform");
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_TRUE(result.changed);

  ASSERT_NE(scene.transform(parent), nullptr);
  ASSERT_NE(scene.transform(child), nullptr);
  ASSERT_NE(scene.transform(locked), nullptr);
  EXPECT_FLOAT_EQ(scene.transform(parent)->position.x, 3.0f);
  EXPECT_FLOAT_EQ(scene.transform(child)->position.x, 1.0f);
  EXPECT_FLOAT_EQ(scene.transform(locked)->position.x, 7.0f);
  ASSERT_NE(session.history().undo_label(), nullptr);
  EXPECT_EQ(*session.history().undo_label(), "Set Transform");

  ASSERT_TRUE(session.history().undo().ok);
  EXPECT_FLOAT_EQ(scene.transform(parent)->position.x, 0.0f);
  EXPECT_FLOAT_EQ(scene.transform(child)->position.x, 1.0f);
  EXPECT_FLOAT_EQ(scene.transform(locked)->position.x, 7.0f);
  ASSERT_TRUE(session.history().redo().ok);
  EXPECT_FLOAT_EQ(scene.transform(parent)->position.x, 3.0f);
  EXPECT_FLOAT_EQ(scene.transform(child)->position.x, 1.0f);
  EXPECT_FLOAT_EQ(scene.transform(locked)->position.x, 7.0f);
}

TEST(
    DocumentCommandTests,
    SceneTransformDragServiceInspectorRotateAndScalePinParentedSelectionPivot) {
  constexpr float kHalfPi = 1.57079632679f;

  ProjectSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_project_session(session));
  SceneDocument &scene = session.scene();
  AssetRegistry &assets = session.assets();

  MeshAsset mesh;
  mesh.vertices = {
      {.position = {0.0f, 1.0f, 0.0f}},
      {.position = {0.0f, 3.0f, 0.0f}},
  };
  const AssetId mesh_id = assets.add_mesh(std::move(mesh));

  const EntityId parent = scene.create_entity("Parent");
  TransformComponent parent_transform;
  parent_transform.position = {10.0f, 0.0f, 0.0f};
  scene.set_transform(parent, parent_transform);

  const EntityId child = scene.create_entity("Child");
  TransformComponent child_transform;
  child_transform.position = {1.0f, 0.0f, 0.0f};
  scene.set_transform(child, child_transform);
  scene.set_mesh(child, MeshComponent{.mesh = mesh_id});
  ASSERT_TRUE(scene.set_parent(child, parent, false));
  ASSERT_TRUE(scene.set_selected(child, true));
  ASSERT_TRUE(scene.set_active_selection(child));

  const auto selected_pivot = [&]() {
    const std::optional<SceneBounds> bounds =
        selected_world_bounds(scene, assets);
    EXPECT_TRUE(bounds.has_value());
    return bounds ? bounds_center(*bounds) : QVec3{};
  };
  const QVec3 pivot_before_rotate = selected_pivot();

  SceneTransformDragService service;
  QuaderApiVoidResult rotate =
      service.rotate_selected_to_primary_euler_around_selection_pivot(
          editor, scene, assets, child, {0.0f, 0.0f, kHalfPi}, "Set Transform");
  ASSERT_TRUE(rotate.ok) << rotate.message;
  EXPECT_TRUE(rotate.changed);
  const QVec3 pivot_after_rotate = selected_pivot();
  EXPECT_NEAR(pivot_after_rotate.x, pivot_before_rotate.x, 0.001f);
  EXPECT_NEAR(pivot_after_rotate.y, pivot_before_rotate.y, 0.001f);
  EXPECT_NEAR(pivot_after_rotate.z, pivot_before_rotate.z, 0.001f);
  ASSERT_NE(scene.transform(child), nullptr);
  EXPECT_NEAR(scene.transform(child)->position.x, 3.0f, 0.001f);
  EXPECT_NEAR(scene.transform(child)->position.y, 2.0f, 0.001f);

  ASSERT_TRUE(session.history().undo().ok);
  ASSERT_NE(scene.transform(child), nullptr);
  EXPECT_NEAR(scene.transform(child)->position.x, 1.0f, 0.001f);
  EXPECT_NEAR(scene.transform(child)->position.y, 0.0f, 0.001f);

  const QVec3 pivot_before_scale = selected_pivot();
  QuaderApiVoidResult scale =
      service.scale_selected_to_primary_scale_around_selection_pivot(
          editor, scene, assets, child, {2.0f, 3.0f, 4.0f}, "Set Transform");
  ASSERT_TRUE(scale.ok) << scale.message;
  EXPECT_TRUE(scale.changed);
  const QVec3 pivot_after_scale = selected_pivot();
  EXPECT_NEAR(pivot_after_scale.x, pivot_before_scale.x, 0.001f);
  EXPECT_NEAR(pivot_after_scale.y, pivot_before_scale.y, 0.001f);
  EXPECT_NEAR(pivot_after_scale.z, pivot_before_scale.z, 0.001f);
  ASSERT_NE(scene.transform(child), nullptr);
  EXPECT_NEAR(scene.transform(child)->scale.x, 2.0f, 0.001f);
  EXPECT_NEAR(scene.transform(child)->scale.y, 3.0f, 0.001f);
  EXPECT_NEAR(scene.transform(child)->scale.z, 4.0f, 0.001f);
  EXPECT_NEAR(scene.transform(child)->position.x, 1.0f, 0.001f);
  EXPECT_NEAR(scene.transform(child)->position.y, -4.0f, 0.001f);
}

TEST(DocumentCommandTests,
     TypedEntityCommandsRoundTripHierarchySelectionAndTransforms) {
  SceneDocument scene;
  const EntityId parent = scene.create_entity("Parent");
  TransformComponent parent_transform;
  parent_transform.position = {10.0f, 0.0f, 0.0f};
  scene.set_transform(parent, parent_transform);

  const EntityId child = scene.create_entity("Child");
  TransformComponent child_transform;
  child_transform.position = {1.0f, 0.0f, 0.0f};
  scene.set_transform(child, child_transform);
  ASSERT_TRUE(scene.set_parent(child, parent, false));

  UndoRedoManager undo_redo;
  UndoRedoResult result =
      undo_redo.execute(std::make_unique<ReparentEntityCommand>(
          scene, child, kInvalidEntityId, true));
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_EQ(scene.parent(child), kInvalidEntityId);
  ASSERT_NE(scene.transform(child), nullptr);
  EXPECT_FLOAT_EQ(scene.transform(child)->position.x, 11.0f);

  ASSERT_TRUE(undo_redo.undo().ok);
  EXPECT_EQ(scene.parent(child), parent);
  ASSERT_NE(scene.transform(child), nullptr);
  EXPECT_FLOAT_EQ(scene.transform(child)->position.x, 1.0f);
  ASSERT_TRUE(undo_redo.redo().ok);
  EXPECT_EQ(scene.parent(child), kInvalidEntityId);
  EXPECT_FLOAT_EQ(scene.transform(child)->position.x, 11.0f);

  TransformComponent moved = *scene.transform(child);
  moved.position.y = 4.0f;
  result = undo_redo.execute(std::make_unique<ApplyEntityTransformCommand>(
      scene, child, *scene.transform(child), moved));
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_FLOAT_EQ(scene.transform(child)->position.y, 4.0f);
  ASSERT_TRUE(undo_redo.undo().ok);
  EXPECT_FLOAT_EQ(scene.transform(child)->position.y, 0.0f);

  result = undo_redo.execute(std::make_unique<SelectEntitiesCommand>(
      scene, std::vector<EntityId>{parent, child}, parent));
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_EQ(scene.selected_entities().size(), 2u);
  EXPECT_EQ(scene.primary_selection(), parent);
  ASSERT_TRUE(undo_redo.undo().ok);
  EXPECT_TRUE(scene.selected_entities().empty());
}

TEST(DocumentCommandServiceTests,
     SceneBatchReparentUsesSceneOwnedReparentCommand) {
  const std::string source =
      read_source_file("src/editor/commands/document_command_service.cpp");
  const std::size_t batch_start =
      source.find("DocumentCommandService::apply_scene_batch");
  ASSERT_NE(batch_start, std::string::npos);
  const std::string batch_source = source.substr(batch_start);

  EXPECT_NE(batch_source.find("std::make_unique<ReparentEntityCommand>("),
            std::string::npos);
  EXPECT_NE(batch_source.find("operation.preserve_world_transform"),
            std::string::npos);
  EXPECT_EQ(batch_source.find("simulatedWorldMatrix"), std::string::npos);
  EXPECT_EQ(batch_source.find("simulatedIsDescendantOf"), std::string::npos);
  EXPECT_EQ(batch_source.find("scene_local_matrix_from_world_matrix"),
            std::string::npos);
  EXPECT_EQ(batch_source.find("world_transform_matrix"), std::string::npos);
}

TEST(DocumentCommandTests, TypedLifecycleCommandsRestoreSceneSnapshots) {
  SceneDocument scene;
  UndoRedoManager undo_redo;

  auto create = std::make_unique<CreateEntityCommand>(
      scene, "Created", EntityKind::Light, kInvalidEntityId,
      TransformComponent{.position = {2.0f, 0.0f, 0.0f}}, true);
  CreateEntityCommand *create_command = create.get();
  UndoRedoResult result = undo_redo.execute(std::move(create));
  ASSERT_TRUE(result.ok) << result.message;
  EntityId created = create_command->entity();
  ASSERT_NE(created, kInvalidEntityId);
  const std::string created_uuid = scene.uuid(created);
  EXPECT_FALSE(created_uuid.empty());
  EXPECT_TRUE(scene.exists(created));
  EXPECT_EQ(scene.kind(created), EntityKind::Light);
  ASSERT_NE(scene.transform(created), nullptr);
  EXPECT_FLOAT_EQ(scene.transform(created)->position.x, 2.0f);
  EXPECT_EQ(scene.primary_selection(), created);
  ASSERT_TRUE(undo_redo.undo().ok);
  EXPECT_FALSE(scene.exists(created));
  ASSERT_TRUE(undo_redo.redo().ok);
  created = create_command->entity();
  EXPECT_TRUE(scene.exists(created));
  EXPECT_EQ(scene.uuid(created), created_uuid);
  EXPECT_EQ(scene.entity_for_uuid(created_uuid), created);

  const EntityId source = scene.create_entity("Source", EntityKind::Mesh);
  scene.set_mesh(source, MeshComponent{.mesh = 42});
  scene.set_material(source, MaterialComponent{.material = 7});
  TransformComponent source_transform;
  source_transform.position = {3.0f, 0.0f, 0.0f};
  scene.set_transform(source, source_transform);
  const EntityId leaf = scene.create_entity("Leaf");
  TransformComponent leaf_transform;
  leaf_transform.position = {0.0f, 1.0f, 0.0f};
  scene.set_transform(leaf, leaf_transform);
  ASSERT_TRUE(scene.set_parent(leaf, source, false));

  auto duplicate = std::make_unique<DuplicateEntityCommand>(scene, source);
  DuplicateEntityCommand *duplicate_command = duplicate.get();
  result = undo_redo.execute(std::move(duplicate));
  ASSERT_TRUE(result.ok) << result.message;
  const EntityId duplicate_root = duplicate_command->duplicate_entity();
  ASSERT_TRUE(scene.exists(duplicate_root));
  const std::string duplicate_uuid = scene.uuid(duplicate_root);
  EXPECT_FALSE(duplicate_uuid.empty());
  EXPECT_NE(duplicate_uuid, scene.uuid(source));
  ASSERT_NE(scene.name(duplicate_root), nullptr);
  EXPECT_EQ(scene.name(duplicate_root)->name, "Source_1");
  ASSERT_NE(scene.mesh(duplicate_root), nullptr);
  EXPECT_EQ(scene.mesh(duplicate_root)->mesh, 42u);
  ASSERT_EQ(scene.children(duplicate_root).size(), 1u);
  EXPECT_EQ(scene.primary_selection(), duplicate_root);
  ASSERT_TRUE(undo_redo.undo().ok);
  EXPECT_FALSE(scene.exists(duplicate_root));

  scene.clear_selection();
  scene.set_selected(source, true);
  auto destroy = std::make_unique<DestroyEntityCommand>(scene, source);
  DestroyEntityCommand *destroy_command = destroy.get();
  result = undo_redo.execute(std::move(destroy));
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_FALSE(scene.exists(source));
  EXPECT_FALSE(scene.exists(leaf));
  ASSERT_TRUE(undo_redo.undo().ok);
  const EntityId restored = destroy_command->entity();
  ASSERT_TRUE(scene.exists(restored));
  ASSERT_NE(scene.name(restored), nullptr);
  EXPECT_EQ(scene.name(restored)->name, "Source");
  ASSERT_NE(scene.mesh(restored), nullptr);
  EXPECT_EQ(scene.mesh(restored)->mesh, 42u);
  ASSERT_EQ(scene.children(restored).size(), 1u);
  EXPECT_EQ(scene.primary_selection(), restored);
  ASSERT_TRUE(undo_redo.redo().ok);
  EXPECT_FALSE(scene.exists(restored));
}

TEST(DocumentCommandTests, DuplicateMintsEditorObjectIdsOnceAndRedoReusesThem) {
  SceneDocument scene;
  UndoRedoManager undo_redo;

  const EntityId source = scene.create_entity("Source", EntityKind::Mesh);
  const EntityId leaf = scene.create_entity("Leaf");
  ASSERT_TRUE(scene.set_parent(leaf, source, false));
  const EditorObjectId source_object = scene.editor_object_id(source);
  const EditorObjectId leaf_object = scene.editor_object_id(leaf);
  const std::string source_uuid = scene.uuid(source);
  const std::string leaf_uuid = scene.uuid(leaf);

  auto duplicate = std::make_unique<DuplicateEntityCommand>(scene, source);
  DuplicateEntityCommand *duplicate_command = duplicate.get();
  UndoRedoResult result = undo_redo.execute(std::move(duplicate));
  ASSERT_TRUE(result.ok) << result.message;

  const EntityId duplicate_root = duplicate_command->duplicate_entity();
  ASSERT_TRUE(scene.exists(duplicate_root));
  ASSERT_EQ(scene.children(duplicate_root).size(), 1u);
  const EntityId duplicate_leaf = scene.children(duplicate_root).front();
  const EditorObjectId duplicate_object =
      scene.editor_object_id(duplicate_root);
  const EditorObjectId duplicate_leaf_object =
      scene.editor_object_id(duplicate_leaf);
  const std::string duplicate_uuid = scene.uuid(duplicate_root);
  const std::string duplicate_leaf_uuid = scene.uuid(duplicate_leaf);
  EXPECT_NE(duplicate_object, kInvalidEditorObjectId);
  EXPECT_NE(duplicate_leaf_object, kInvalidEditorObjectId);
  EXPECT_NE(duplicate_object, source_object);
  EXPECT_NE(duplicate_leaf_object, leaf_object);
  EXPECT_NE(duplicate_uuid, source_uuid);
  EXPECT_NE(duplicate_leaf_uuid, leaf_uuid);

  ASSERT_TRUE(undo_redo.undo().ok);
  EXPECT_FALSE(scene.exists(duplicate_root));
  ASSERT_TRUE(undo_redo.redo().ok);

  const EntityId redone_root = duplicate_command->duplicate_entity();
  ASSERT_TRUE(scene.exists(redone_root));
  ASSERT_EQ(scene.children(redone_root).size(), 1u);
  const EntityId redone_leaf = scene.children(redone_root).front();
  EXPECT_EQ(scene.editor_object_id(redone_root), duplicate_object);
  EXPECT_EQ(scene.editor_object_id(redone_leaf), duplicate_leaf_object);
  EXPECT_EQ(scene.uuid(redone_root), duplicate_uuid);
  EXPECT_EQ(scene.uuid(redone_leaf), duplicate_leaf_uuid);
  EXPECT_EQ(scene.entity_for_uuid(duplicate_uuid), redone_root);
  EXPECT_EQ(scene.entity_for_uuid(duplicate_leaf_uuid), redone_leaf);
}

TEST(DocumentCommandTests, DestroyUndoRestoresEditorObjectIdsForSubtrees) {
  SceneDocument scene;
  UndoRedoManager undo_redo;

  const EntityId source = scene.create_entity("Source", EntityKind::Mesh);
  const EntityId leaf = scene.create_entity("Leaf");
  ASSERT_TRUE(scene.set_parent(leaf, source, false));
  const EditorObjectId source_object = scene.editor_object_id(source);
  const EditorObjectId leaf_object = scene.editor_object_id(leaf);
  const std::string source_uuid = scene.uuid(source);
  const std::string leaf_uuid = scene.uuid(leaf);

  auto destroy = std::make_unique<DestroyEntityCommand>(scene, source);
  DestroyEntityCommand *destroy_command = destroy.get();
  UndoRedoResult result = undo_redo.execute(std::move(destroy));
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_FALSE(scene.exists(source));
  EXPECT_FALSE(scene.exists(leaf));

  ASSERT_TRUE(undo_redo.undo().ok);
  const EntityId restored_root = destroy_command->entity();
  ASSERT_TRUE(scene.exists(restored_root));
  ASSERT_EQ(scene.children(restored_root).size(), 1u);
  const EntityId restored_leaf = scene.children(restored_root).front();
  EXPECT_EQ(scene.editor_object_id(restored_root), source_object);
  EXPECT_EQ(scene.editor_object_id(restored_leaf), leaf_object);
  EXPECT_EQ(scene.uuid(restored_root), source_uuid);
  EXPECT_EQ(scene.uuid(restored_leaf), leaf_uuid);
  EXPECT_EQ(scene.entity_for_uuid(source_uuid), restored_root);
  EXPECT_EQ(scene.entity_for_uuid(leaf_uuid), restored_leaf);
}

TEST(DocumentCommandServiceTests, BatchesVisibilityAndTransforms) {
  SceneDocument scene;
  const EntityId first = scene.create_entity("First");
  const EntityId second = scene.create_entity("Second");
  const EntityId locked = scene.create_entity("Locked");
  EditorHistoryService history;
  DocumentCommandService commands(scene, history);

  DocumentCommandResult result = commands.set_visibility({
      {.entity = first, .before = true, .after = false},
      {.entity = second, .before = true, .after = false},
  });
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_TRUE(result.changed);
  ASSERT_NE(scene.visibility(first), nullptr);
  ASSERT_NE(scene.visibility(second), nullptr);
  EXPECT_FALSE(scene.visibility(first)->visible);
  EXPECT_FALSE(scene.visibility(second)->visible);

  ASSERT_TRUE(history.undo().ok);
  EXPECT_TRUE(scene.visibility(first)->visible);
  EXPECT_TRUE(scene.visibility(second)->visible);
  ASSERT_TRUE(history.redo().ok);
  EXPECT_FALSE(scene.visibility(first)->visible);
  EXPECT_FALSE(scene.visibility(second)->visible);

  TransformComponent first_after;
  first_after.position = {1.0f, 0.0f, 0.0f};
  TransformComponent second_after;
  second_after.position = {0.0f, 2.0f, 0.0f};
  result = commands.set_transforms({
      {.entity = first,
       .before = *scene.transform(first),
       .after = first_after},
      {.entity = second,
       .before = *scene.transform(second),
       .after = second_after},
  });
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_FLOAT_EQ(scene.transform(first)->position.x, 1.0f);
  EXPECT_FLOAT_EQ(scene.transform(second)->position.y, 2.0f);

  ASSERT_TRUE(history.undo().ok);
  EXPECT_FLOAT_EQ(scene.transform(first)->position.x, 0.0f);
  EXPECT_FLOAT_EQ(scene.transform(second)->position.y, 0.0f);

  result = commands.set_locked(locked, true);
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_TRUE(result.changed);
  ASSERT_NE(scene.lock(locked), nullptr);
  EXPECT_TRUE(scene.lock(locked)->locked);

  TransformComponent locked_after;
  locked_after.position = {5.0f, 0.0f, 0.0f};
  result = commands.set_transform(locked, locked_after);
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_FALSE(result.changed);
  EXPECT_FLOAT_EQ(scene.transform(locked)->position.x, 0.0f);

  ASSERT_TRUE(history.undo().ok);
  EXPECT_FALSE(scene.lock(locked)->locked);
}

TEST(DocumentCommandServiceTests,
     LockCommandsClearSelectionInsideUndoableCommands) {
  SceneDocument scene;
  const EntityId parent = scene.create_entity("Lock Parent");
  const EntityId child = scene.create_entity("Lock Child");
  ASSERT_TRUE(scene.set_parent(child, parent, false));
  EditorHistoryService history;
  DocumentCommandService commands(scene, history);

  ASSERT_TRUE(commands.select_entities({parent, child}, child).ok);
  DocumentCommandResult result = commands.set_locked(parent, true);
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_TRUE(result.changed);
  ASSERT_NE(scene.lock(parent), nullptr);
  EXPECT_TRUE(scene.lock(parent)->locked);
  EXPECT_TRUE(scene.selected_entities().empty());
  EXPECT_EQ(scene.primary_selection(), kInvalidEntityId);

  ASSERT_TRUE(history.undo().ok);
  EXPECT_FALSE(scene.lock(parent)->locked);
  EXPECT_EQ(scene.selected_entities().size(), 2u);
  EXPECT_EQ(scene.primary_selection(), child);

  ASSERT_TRUE(history.redo().ok);
  EXPECT_TRUE(scene.lock(parent)->locked);
  EXPECT_TRUE(scene.selected_entities().empty());
  EXPECT_EQ(scene.primary_selection(), kInvalidEntityId);

  ASSERT_TRUE(history.undo().ok);
  ASSERT_TRUE(commands.select_entities({child}, child).ok);

  result = commands.apply_scene_batch(
      "Batch Lock Parent",
      {
          DocumentSceneBatchOperation{
              .kind = DocumentSceneBatchOperationKind::SetLocked,
              .entity = parent,
              .flag = true,
          },
      });
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_TRUE(result.changed);
  ASSERT_NE(history.undo_label(), nullptr);
  EXPECT_EQ(*history.undo_label(), "Batch Lock Parent");
  EXPECT_TRUE(scene.lock(parent)->locked);
  EXPECT_TRUE(scene.selected_entities().empty());
  EXPECT_EQ(scene.primary_selection(), kInvalidEntityId);

  ASSERT_TRUE(history.undo().ok);
  EXPECT_FALSE(scene.lock(parent)->locked);
  EXPECT_EQ(scene.selected_entities().size(), 1u);
  EXPECT_EQ(scene.primary_selection(), child);

  ASSERT_TRUE(history.redo().ok);
  EXPECT_TRUE(scene.lock(parent)->locked);
  EXPECT_TRUE(scene.selected_entities().empty());
}

TEST(DocumentCommandServiceTests,
     AssetAwareServiceRejectsDanglingAssetReferences) {
  SceneDocument scene;
  AssetRegistry assets;
  EditorHistoryService history;
  DocumentCommandService commands(scene, history, &assets);

  MeshAsset mesh;
  const AssetId mesh_id = assets.add_mesh(mesh);
  const EntityId entity =
      scene.create_entity("Asset Checked", EntityKind::Mesh);
  scene.set_mesh(entity, MeshComponent{.mesh = mesh_id});

  MeshComponent invalid_mesh = *scene.mesh(entity);
  invalid_mesh.mesh = mesh_id + 1000u;
  DocumentCommandResult result =
      commands.set_mesh_component(entity, invalid_mesh);
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.message, "Mesh asset does not exist");

  result = commands.set_material(entity, mesh_id + 2000u);
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.message, "Material asset does not exist");
}

TEST(DocumentCommandServiceTests,
     ModelingOwnedMeshComponentAllowsMetadataButRejectsMeshAssetReplacement) {
  SceneDocument scene;
  AssetRegistry assets;
  EditorHistoryService history;
  DocumentCommandService commands(scene, history, &assets);

  const AssetId source_mesh = assets.add_mesh(MeshAsset{});
  const AssetId replacement_mesh = assets.add_mesh(MeshAsset{});
  ASSERT_NE(source_mesh, kInvalidAssetId);
  ASSERT_NE(replacement_mesh, kInvalidAssetId);

  const EntityId entity =
      scene.create_entity("Modeling Owned Mesh", EntityKind::Mesh);
  scene.set_mesh(entity, MeshComponent{.mesh = source_mesh});
  scene.set_modeling_object(entity, ModelingObjectComponent{.object_id = 42});

  MeshComponent metadata = *scene.mesh(entity);
  metadata.double_sided = true;
  metadata.generate_collisions = false;
  metadata.collision_type = MeshCollisionType::ConvexHull;
  metadata.display_collision = true;
  metadata.body_type = MeshPhysicsBodyType::RigidBody;
  DocumentCommandResult result = commands.set_mesh_component(entity, metadata);
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_TRUE(result.changed);
  ASSERT_NE(scene.mesh(entity), nullptr);
  EXPECT_EQ(scene.mesh(entity)->mesh, source_mesh);
  EXPECT_TRUE(scene.mesh(entity)->double_sided);
  EXPECT_FALSE(scene.mesh(entity)->generate_collisions);
  EXPECT_EQ(scene.mesh(entity)->collision_type, MeshCollisionType::ConvexHull);
  EXPECT_TRUE(scene.mesh(entity)->display_collision);
  EXPECT_EQ(scene.mesh(entity)->body_type, MeshPhysicsBodyType::RigidBody);

  ASSERT_TRUE(history.undo().ok);
  ASSERT_NE(scene.mesh(entity), nullptr);
  EXPECT_EQ(scene.mesh(entity)->mesh, source_mesh);
  EXPECT_FALSE(scene.mesh(entity)->double_sided);
  EXPECT_TRUE(scene.mesh(entity)->generate_collisions);

  MeshComponent replacement = *scene.mesh(entity);
  replacement.mesh = replacement_mesh;
  result = commands.set_mesh_component(entity, replacement);
  EXPECT_FALSE(result.ok);
  EXPECT_NE(result.message.find("modeling object"), std::string::npos);
  EXPECT_EQ(scene.mesh(entity)->mesh, source_mesh);
}

TEST(DocumentCommandServiceTests,
     SetMaterialComparesFullMaterialComponentState) {
  SceneDocument scene;
  AssetRegistry assets;
  EditorHistoryService history;
  DocumentCommandService commands(scene, history, &assets);

  const AssetId base_material =
      assets.add_material(make_default_quader_material("Base Material"));
  const AssetId slot_material =
      assets.add_material(make_default_quader_material("Slot Material"));
  ASSERT_NE(base_material, kInvalidAssetId);
  ASSERT_NE(slot_material, kInvalidAssetId);

  const EntityId entity =
      scene.create_entity("Slotted Material", EntityKind::Mesh);
  scene.set_material(entity, MaterialComponent{
                                 .material = base_material,
                                 .material_slots = {{2u, slot_material}},
                             });

  const DocumentCommandResult result =
      commands.set_material(entity, base_material);
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_TRUE(result.changed);
  ASSERT_NE(scene.material(entity), nullptr);
  EXPECT_EQ(scene.material(entity)->material, base_material);
  EXPECT_TRUE(scene.material(entity)->material_slots.empty());

  ASSERT_TRUE(history.undo().ok);
  ASSERT_NE(scene.material(entity), nullptr);
  EXPECT_EQ(scene.material(entity)->material, base_material);
  ASSERT_TRUE(scene.material(entity)->material_slots.contains(2u));
  EXPECT_EQ(scene.material(entity)->material_slots.at(2u), slot_material);

  ASSERT_TRUE(history.redo().ok);
  ASSERT_NE(scene.material(entity), nullptr);
  EXPECT_EQ(scene.material(entity)->material, base_material);
  EXPECT_TRUE(scene.material(entity)->material_slots.empty());
}

TEST(DocumentCommandServiceTests,
     CreatePointLightAndSetLightComponentAreUndoable) {
  SceneDocument scene;
  EditorHistoryService history;
  DocumentCommandService commands(scene, history);

  TransformComponent transform;
  transform.position = {1.0f, 2.0f, 3.0f};

  LightComponent light;
  light.type = LightType::Point;
  light.color = {1.0f, 0.8f, 0.6f};
  light.intensity = 900.0f;
  light.range = 12.0f;
  light.casts_shadows = true;

  const DocumentCreateEntityResult created = commands.create_point_light(
      "Key Light", light, kInvalidEntityId, transform, true);
  ASSERT_TRUE(created.ok) << created.message;
  EXPECT_TRUE(created.changed);
  ASSERT_NE(created.entity, kInvalidEntityId);
  EXPECT_TRUE(scene.exists(created.entity));
  EXPECT_EQ(scene.kind(created.entity), EntityKind::Light);
  ASSERT_NE(scene.transform(created.entity), nullptr);
  EXPECT_FLOAT_EQ(scene.transform(created.entity)->position.z, 3.0f);
  ASSERT_NE(scene.light(created.entity), nullptr);
  EXPECT_EQ(scene.light(created.entity)->type, LightType::Point);
  EXPECT_FLOAT_EQ(scene.light(created.entity)->intensity, 900.0f);
  EXPECT_FLOAT_EQ(scene.light(created.entity)->range, 12.0f);
  EXPECT_TRUE(scene.light(created.entity)->casts_shadows);
  EXPECT_EQ(scene.primary_selection(), created.entity);

  LightComponent edited = *scene.light(created.entity);
  edited.color = {0.25f, 0.5f, 1.0f};
  edited.intensity = 1250.0f;
  edited.range = 6.5f;
  edited.casts_shadows = false;
  DocumentCommandResult result =
      commands.set_light_component(created.entity, edited);
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_TRUE(result.changed);
  EXPECT_FLOAT_EQ(scene.light(created.entity)->color.z, 1.0f);
  EXPECT_FLOAT_EQ(scene.light(created.entity)->range, 6.5f);
  EXPECT_FALSE(scene.light(created.entity)->casts_shadows);

  result = commands.set_light_component(created.entity, edited);
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_FALSE(result.changed);

  ASSERT_TRUE(history.undo().ok);
  ASSERT_NE(scene.light(created.entity), nullptr);
  EXPECT_FLOAT_EQ(scene.light(created.entity)->color.z, 0.6f);
  EXPECT_FLOAT_EQ(scene.light(created.entity)->range, 12.0f);
  EXPECT_TRUE(scene.light(created.entity)->casts_shadows);

  ASSERT_TRUE(history.undo().ok);
  EXPECT_FALSE(scene.exists(created.entity));

  ASSERT_TRUE(history.redo().ok);
  EXPECT_TRUE(scene.exists(created.entity));
  ASSERT_NE(scene.light(created.entity), nullptr);
  EXPECT_EQ(scene.light(created.entity)->type, LightType::Point);
  EXPECT_EQ(scene.primary_selection(), created.entity);

  ASSERT_TRUE(history.redo().ok);
  ASSERT_NE(scene.light(created.entity), nullptr);
  EXPECT_FLOAT_EQ(scene.light(created.entity)->range, 6.5f);
  EXPECT_FALSE(scene.light(created.entity)->casts_shadows);
}

TEST(DocumentCommandServiceTests, DefaultGroupNameStaysLiteralGroup) {
  SceneDocument scene;
  EditorHistoryService history;
  DocumentCommandService commands(scene, history);

  const EntityId first = scene.create_entity("First");
  const EntityId second = scene.create_entity("Second");
  ASSERT_NE(first, kInvalidEntityId);
  ASSERT_NE(second, kInvalidEntityId);

  ASSERT_TRUE(commands.select_entities({first}, first).ok);
  const DocumentGroupSelectionResult first_group = commands.group_selection();
  ASSERT_TRUE(first_group.ok) << first_group.message;
  ASSERT_TRUE(scene.exists(first_group.group_entity));
  ASSERT_NE(scene.name(first_group.group_entity), nullptr);
  EXPECT_EQ(scene.name(first_group.group_entity)->name, "Group");

  ASSERT_TRUE(commands.select_entities({second}, second).ok);
  const DocumentGroupSelectionResult second_group = commands.group_selection();
  ASSERT_TRUE(second_group.ok) << second_group.message;
  ASSERT_TRUE(scene.exists(second_group.group_entity));
  ASSERT_NE(scene.name(second_group.group_entity), nullptr);
  EXPECT_EQ(scene.name(second_group.group_entity)->name, "Group");
  EXPECT_NE(first_group.group_entity, second_group.group_entity);
}

TEST(DocumentCommandServiceTests,
     CoversEntityHierarchySelectionAndLifecycleCommands) {
  SceneDocument scene;
  EditorHistoryService history;
  DocumentCommandService commands(scene, history);

  const DocumentCreateEntityResult first_created = commands.create_entity(
      "First", EntityKind::Generic, kInvalidEntityId, {});
  ASSERT_TRUE(first_created.ok) << first_created.message;
  const EntityId first = first_created.entity;
  ASSERT_NE(first, kInvalidEntityId);
  const DocumentCreateEntityResult second_created = commands.create_entity(
      "Second", EntityKind::Generic, kInvalidEntityId, {});
  ASSERT_TRUE(second_created.ok) << second_created.message;
  const EntityId second = second_created.entity;
  ASSERT_NE(second, kInvalidEntityId);

  TransformComponent moved;
  moved.position = {5.0f, 0.0f, 0.0f};
  DocumentCommandResult result = commands.apply_entity_transform(first, moved);
  ASSERT_TRUE(result.ok) << result.message;
  ASSERT_NE(scene.transform(first), nullptr);
  EXPECT_FLOAT_EQ(scene.transform(first)->position.x, 5.0f);
  ASSERT_TRUE(history.undo().ok);
  EXPECT_FLOAT_EQ(scene.transform(first)->position.x, 0.0f);
  ASSERT_TRUE(history.redo().ok);

  result = commands.select_entities({first, second}, second);
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_EQ(scene.selected_entities().size(), 2u);
  EXPECT_EQ(scene.primary_selection(), second);

  TransformComponent group_transform;
  group_transform.position = {2.0f, 0.0f, 0.0f};
  const DocumentGroupSelectionResult grouped =
      commands.group_selection("Group", group_transform);
  ASSERT_TRUE(grouped.ok) << grouped.message;
  const EntityId group = grouped.group_entity;
  ASSERT_TRUE(scene.exists(group));
  const std::string group_uuid = scene.uuid(group);
  EXPECT_FALSE(group_uuid.empty());
  EXPECT_EQ(scene.parent(first), group);
  EXPECT_EQ(scene.parent(second), group);
  EXPECT_EQ(scene.primary_selection(), group);

  ASSERT_TRUE(history.undo().ok);
  EXPECT_FALSE(scene.exists(group));
  EXPECT_EQ(scene.parent(first), kInvalidEntityId);
  EXPECT_EQ(scene.parent(second), kInvalidEntityId);
  EXPECT_EQ(scene.selected_entities().size(), 2u);
  EXPECT_EQ(scene.primary_selection(), second);

  ASSERT_TRUE(history.redo().ok);
  const EntityId regrouped = scene.primary_selection();
  ASSERT_TRUE(scene.exists(regrouped));
  EXPECT_EQ(scene.uuid(regrouped), group_uuid);
  EXPECT_EQ(scene.entity_for_uuid(group_uuid), regrouped);
  EXPECT_EQ(scene.parent(first), regrouped);
  EXPECT_EQ(scene.parent(second), regrouped);

  result = commands.ungroup_selection();
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_FALSE(scene.exists(regrouped));
  EXPECT_EQ(scene.parent(first), kInvalidEntityId);
  EXPECT_EQ(scene.parent(second), kInvalidEntityId);
  EXPECT_EQ(scene.selected_entities().size(), 2u);

  ASSERT_TRUE(history.undo().ok);
  const EntityId restored_group = scene.primary_selection();
  ASSERT_TRUE(scene.exists(restored_group));
  EXPECT_EQ(scene.parent(first), restored_group);
  EXPECT_EQ(scene.parent(second), restored_group);

  result = commands.reparent_entity(first, kInvalidEntityId, true);
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_EQ(scene.parent(first), kInvalidEntityId);
  ASSERT_TRUE(history.undo().ok);
  EXPECT_EQ(scene.parent(first), restored_group);

  const DocumentDuplicateEntityResult duplicated =
      commands.duplicate_entity(first);
  ASSERT_TRUE(duplicated.ok) << duplicated.message;
  const EntityId duplicate = duplicated.duplicate_entity;
  ASSERT_TRUE(scene.exists(duplicate));
  EXPECT_EQ(scene.parent(duplicate), restored_group);
  EXPECT_EQ(scene.primary_selection(), duplicate);
  ASSERT_TRUE(history.undo().ok);
  EXPECT_FALSE(scene.exists(duplicate));

  result = commands.destroy_entity(second);
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_FALSE(scene.exists(second));
  ASSERT_TRUE(history.undo().ok);
  EXPECT_TRUE(scene.exists(scene.primary_selection()));
}

TEST(DocumentCommandServiceTests,
     EntityPayloadResultsKeepInvalidIdsWhenCommandsFailOrNoop) {
  SceneDocument scene;
  EditorHistoryService history;
  DocumentCommandService commands(scene, history);

  const DocumentCreateEntityResult invalid_create =
      commands.create_entity("Child", EntityKind::Generic, 999u, {});
  EXPECT_FALSE(invalid_create.ok);
  EXPECT_FALSE(invalid_create.changed);
  EXPECT_EQ(invalid_create.entity, kInvalidEntityId);
  EXPECT_FALSE(history.can_undo());

  const DocumentGroupSelectionResult empty_group =
      commands.group_selection("Empty Group", {});
  EXPECT_TRUE(empty_group.ok) << empty_group.message;
  EXPECT_FALSE(empty_group.changed);
  EXPECT_EQ(empty_group.group_entity, kInvalidEntityId);
  EXPECT_FALSE(history.can_undo());

  const DocumentDuplicateEntityResult missing_duplicate =
      commands.duplicate_entity(999u);
  EXPECT_FALSE(missing_duplicate.ok);
  EXPECT_FALSE(missing_duplicate.changed);
  EXPECT_EQ(missing_duplicate.duplicate_entity, kInvalidEntityId);
  EXPECT_FALSE(history.can_undo());
}

TEST(DocumentCommandServiceTests,
     GroupsAreSceneOwnedEditorObjectsNotModelingObjects) {
  ProjectSession session;

  const DocumentCreateEntityResult first_created =
      session.document_commands().create_entity("First", EntityKind::Mesh,
                                                kInvalidEntityId, {});
  ASSERT_TRUE(first_created.ok) << first_created.message;
  const EntityId first = first_created.entity;
  const DocumentCreateEntityResult second_created =
      session.document_commands().create_entity("Second", EntityKind::Mesh,
                                                kInvalidEntityId, {});
  ASSERT_TRUE(second_created.ok) << second_created.message;
  const EntityId second = second_created.entity;
  ASSERT_TRUE(
      session.document_commands().select_entities({first, second}, second).ok);

  const DocumentGroupSelectionResult grouped =
      session.document_commands().group_selection("Group", {});
  ASSERT_TRUE(grouped.ok) << grouped.message;
  const EntityId group = grouped.group_entity;
  ASSERT_TRUE(session.scene().exists(group));
  EXPECT_EQ(session.scene().kind(group), EntityKind::Empty);
  EXPECT_EQ(session.scene().modeling_object(group), nullptr);

  const EditorObjectId group_object = session.scene().editor_object_id(group);
  ASSERT_NE(group_object, kInvalidEditorObjectId);
  const std::optional<ProjectObjectRecord> record =
      session.document().object_record(group_object);
  ASSERT_TRUE(record.has_value());
  EXPECT_EQ(record->scene_entity, group);
  EXPECT_EQ(record->modeling_object, kInvalidModelingObjectId);

  const ProjectDocumentIdentityReport report =
      session.document().validate_object_identity_invariants();
  EXPECT_TRUE(report.ok()) << (report.issues.empty()
                                   ? std::string{}
                                   : report.issues.front().message);
}

TEST(EditorHistoryServiceTests, NoopCommandsDoNotClearRedoButNewCommandsDo) {
  SceneDocument scene;
  const EntityId entity = scene.create_entity("Brush");
  EditorHistoryService history;
  DocumentCommandService commands(scene, history);

  ASSERT_TRUE(commands.rename_entity(entity, "Brush_1").ok);
  ASSERT_TRUE(history.undo().ok);
  EXPECT_TRUE(history.can_redo());

  DocumentCommandResult result = commands.rename_entity(entity, "Brush");
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_FALSE(result.changed);
  EXPECT_TRUE(history.can_redo());

  result = commands.set_visible(entity, true);
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_FALSE(result.changed);
  EXPECT_TRUE(history.can_redo());

  result = commands.rename_entity(entity, "Brush_2");
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_TRUE(result.changed);
  EXPECT_FALSE(history.can_redo());
}

TEST(EditorHistoryServiceTests,
     ExternalCommandsShareUndoOrderWithDocumentCommands) {
  SceneDocument scene;
  const EntityId entity = scene.create_entity("Brush");
  EditorHistoryService history;
  DocumentCommandService commands(scene, history);
  int external_value = 0;

  CommandResult result = history.execute(std::make_unique<TestValueCommand>(
      "External Edit", external_value, 0, 1));
  ASSERT_TRUE(result.ok) << result.message;
  ASSERT_TRUE(commands.rename_entity(entity, "Brush_1").ok);

  ASSERT_TRUE(history.undo().ok);
  EXPECT_EQ(scene.name(entity)->name, "Brush");
  EXPECT_EQ(external_value, 1);

  ASSERT_TRUE(history.undo().ok);
  EXPECT_EQ(external_value, 0);

  ASSERT_TRUE(history.redo().ok);
  EXPECT_EQ(external_value, 1);

  ASSERT_TRUE(history.redo().ok);
  EXPECT_EQ(scene.name(entity)->name, "Brush_1");
}

TEST(UndoRedoManagerTests, FailedExecuteAndFailedRestoreDoNotCorruptStacks) {
  UndoRedoManager undo_redo;
  int value = 0;
  bool fail_undo = false;
  bool fail_redo = false;

  UndoRedoResult result = undo_redo.execute(
      std::make_unique<TestValueCommand>("Set Value", value, 0, 1));
  ASSERT_TRUE(result.ok) << result.message;
  ASSERT_TRUE(undo_redo.undo().ok);
  EXPECT_TRUE(undo_redo.can_redo());

  result = undo_redo.execute(std::make_unique<TestValueCommand>(
      "Noop", value, value, value, nullptr, nullptr, true, false));
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_FALSE(result.changed);
  EXPECT_TRUE(undo_redo.can_redo());

  fail_redo = true;
  result = undo_redo.execute(std::make_unique<TestValueCommand>(
      "Failed", value, 0, 2, nullptr, &fail_redo));
  EXPECT_FALSE(result.ok);
  EXPECT_TRUE(undo_redo.can_redo());
  fail_redo = false;

  result = undo_redo.redo();
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(undo_redo.can_undo());
  ASSERT_TRUE(undo_redo.undo().ok);
  EXPECT_TRUE(undo_redo.can_redo());

  fail_undo = true;
  result = undo_redo.execute(std::make_unique<TestValueCommand>(
      "Fragile", value, 1, 2, &fail_undo, &fail_redo));
  ASSERT_TRUE(result.ok) << result.message;
  result = undo_redo.undo();
  EXPECT_FALSE(result.ok);
  EXPECT_TRUE(undo_redo.can_undo());
  EXPECT_FALSE(undo_redo.can_redo());

  fail_undo = false;
  ASSERT_TRUE(undo_redo.undo().ok);
  EXPECT_TRUE(undo_redo.can_redo());

  fail_redo = true;
  result = undo_redo.redo();
  EXPECT_FALSE(result.ok);
  EXPECT_TRUE(undo_redo.can_redo());

  fail_redo = false;
  ASSERT_TRUE(undo_redo.redo().ok);
  EXPECT_TRUE(undo_redo.can_undo());
}

TEST(ModelingSessionServiceTests, ExposesBoxCommandContract) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCommandDescriptor box = service.box_tool_command();
  const ToolDescriptor *viewport_box =
      find_viewport_tool_descriptor(ModelingViewportTool::Box);
  ASSERT_NE(viewport_box, nullptr);

  EXPECT_EQ(box.command, ModelingCommandId::ActivateBoxTool);
  EXPECT_EQ(box.id, std::string{viewport_box->id});
  EXPECT_EQ(box.label, std::string{viewport_box->label});
  EXPECT_EQ(box.shortcut, std::string{viewport_box->shortcut});
  EXPECT_EQ(box.viewport_scoped, viewport_box->owns_viewport);
  EXPECT_TRUE(box.rebuilds_mesh_on_success);
}

const ModelingActionPanel *find_action_panel(const ModelingActionSet &actions,
                                             std::string_view panel_id) {
  const auto found =
      std::find_if(actions.panels.begin(), actions.panels.end(),
                   [&](const ModelingActionPanel &panel) {
                     return std::string_view(panel.panel_id) == panel_id;
                   });
  return found == actions.panels.end() ? nullptr : &(*found);
}

const ModelingActionItem *find_action_item(const ModelingActionPanel &panel,
                                           std::string_view action_id) {
  const auto found =
      std::find_if(panel.actions.begin(), panel.actions.end(),
                   [&](const ModelingActionItem &action) {
                     return std::string_view(action.action_id) == action_id;
                   });
  return found == panel.actions.end() ? nullptr : &(*found);
}

const ModelingActionItem *
find_action_item(const std::vector<ModelingActionItem> &actions,
                 std::string_view action_id) {
  const auto found = std::find_if(
      actions.begin(), actions.end(), [&](const ModelingActionItem &action) {
        return std::string_view(action.action_id) == action_id;
      });
  return found == actions.end() ? nullptr : &(*found);
}

TEST(ModelingSessionServiceTests,
     ContextActionsExposeObjectPanelAndExecuteHostActionIds) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Action Box",
  });
  ASSERT_TRUE(box.ok) << box.message;

  const ModelingActionSet actions = service.context_action_set({
      .surface = ModelingActionSurface::OperationsPanel,
      .include_disabled = true,
  });
  EXPECT_EQ(actions.surface, ModelingActionSurface::OperationsPanel);
  EXPECT_EQ(actions.surface_id, "operations_top_bar");
  ASSERT_EQ(actions.panels.size(), 1u);

  const ModelingActionPanel *object_panel =
      find_action_panel(actions, "object_operations");
  ASSERT_NE(object_panel, nullptr);
  EXPECT_EQ(object_panel->label, "Object Operations");
  EXPECT_EQ(object_panel->selection_mode_id, "object");
  EXPECT_EQ(object_panel->visibility, ModelingActionVisibility::Enabled);
  EXPECT_EQ(find_action_panel(actions, "edge_operations"), nullptr);
  EXPECT_EQ(find_action_panel(actions, "face_operations"), nullptr);

  const ModelingActionItem *invert =
      find_action_item(*object_panel, "invert_mesh");
  ASSERT_NE(invert, nullptr);
  EXPECT_EQ(invert->label, "Flip Mesh Normals");
  EXPECT_EQ(invert->operation_id, "flip_face_normals");
  EXPECT_EQ(invert->visibility, ModelingActionVisibility::Enabled);

  const ModelingActionItem *combine =
      find_action_item(*object_panel, "combine_mesh");
  ASSERT_NE(combine, nullptr);
  EXPECT_EQ(combine->operation_id, "combine_meshes");
  EXPECT_EQ(combine->visibility, ModelingActionVisibility::Disabled);
  EXPECT_EQ(combine->disabled_reason_key,
            "modeling.action.disabled.minimum_objects");
  EXPECT_EQ(combine->disabled_reason_count, 2u);

  const ModelingCommandResult disabled =
      service.execute_context_action("combine_mesh");
  EXPECT_FALSE(disabled.ok);

  const ModelingCommandResult executed =
      service.execute_context_action("invert_mesh");
  EXPECT_TRUE(executed.ok) << executed.message;
}

TEST(ModelingSessionServiceTests,
     ContextActionsExposeEdgePanelAndDisableInsufficientCounts) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Edge Action Box",
  });
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Edge).ok);

  const ModelingActionSet actions = service.context_action_set({
      .surface = ModelingActionSurface::OperationsPanel,
      .include_disabled = true,
  });
  ASSERT_EQ(actions.panels.size(), 1u);
  EXPECT_EQ(find_action_panel(actions, "object_operations"), nullptr);

  const ModelingActionPanel *edge_panel =
      find_action_panel(actions, "edge_operations");
  ASSERT_NE(edge_panel, nullptr);
  EXPECT_EQ(edge_panel->selection_mode_id, "edge");
  EXPECT_EQ(edge_panel->visibility, ModelingActionVisibility::Enabled);

  const ModelingActionItem *bevel =
      find_action_item(*edge_panel, "bevel_edges");
  ASSERT_NE(bevel, nullptr);
  EXPECT_EQ(bevel->operation_id, "edge_bevel");
  EXPECT_EQ(bevel->preview_kind, ModelingActionPreviewKind::EdgeBevel);

  const ModelingActionItem *interpolated_bridge =
      find_action_item(*edge_panel, "interpolated_bridge_edges");
  ASSERT_NE(interpolated_bridge, nullptr);
  EXPECT_EQ(interpolated_bridge->operation_id, "bridge");
  EXPECT_EQ(interpolated_bridge->preview_kind,
            ModelingActionPreviewKind::Bridge);

  const ModelingActionItem *split =
      find_action_item(*edge_panel, "split_edges");
  ASSERT_NE(split, nullptr);
  EXPECT_EQ(split->operation_id, "split_edges");
  EXPECT_EQ(split->visibility, ModelingActionVisibility::Disabled);

  const ModelingActionItem *fill_hole =
      find_action_item(*edge_panel, "fill_hole");
  ASSERT_NE(fill_hole, nullptr);
  EXPECT_EQ(fill_hole->visibility, ModelingActionVisibility::Disabled);
  EXPECT_EQ(fill_hole->disabled_reason_key,
            "modeling.action.disabled.minimum_edges");
  EXPECT_EQ(fill_hole->disabled_reason_count, 3u);

  ModelingActionQuery hidden_query;
  hidden_query.surface = ModelingActionSurface::OperationsPanel;
  hidden_query.include_disabled = true;
  hidden_query.include_hidden = true;
  const ModelingActionSet all_panels = service.context_action_set(hidden_query);
  const ModelingActionPanel *hidden_object_panel =
      find_action_panel(all_panels, "object_operations");
  ASSERT_NE(hidden_object_panel, nullptr);
  EXPECT_EQ(hidden_object_panel->visibility, ModelingActionVisibility::Hidden);
}

TEST(ModelingSessionServiceTests,
     PreviewActionsExposeOnlyActiveToolSettingsAndLiveUpdate) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Bevel Preview Box",
  });
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Edge).ok);
  ASSERT_TRUE(service.select_all().ok);

  const ModelingCommandResult begin =
      service.execute_context_action("bevel_edges");
  ASSERT_TRUE(begin.ok) << begin.message;

  ModelingToolSettingsTarget target = service.tool_settings_target();
  EXPECT_TRUE(target.active);
  EXPECT_TRUE(target.operation_preview_active);
  EXPECT_EQ(target.kind, ModelingToolSettingsTargetKind::EdgeBevel);
  EXPECT_EQ(target.action_id, "bevel_edges");
  EXPECT_EQ(target.operation_id, "edge_bevel");
  EXPECT_EQ(target.label, "Bevel Edges");

  const std::optional<MeshAsset> preview_before =
      service.compile_object_mesh(box.object.modeling_object_id);
  ASSERT_TRUE(preview_before.has_value());

  ModelingInteractionSettings settings = service.interaction_settings();
  settings.edge_bevel_width = 0.25;
  const ModelingCommandResult update =
      service.set_interaction_settings(settings);
  EXPECT_TRUE(update.ok) << update.message;
  EXPECT_TRUE(update.changed);

  const std::optional<MeshAsset> preview_after =
      service.compile_object_mesh(box.object.modeling_object_id);
  ASSERT_TRUE(preview_after.has_value());
  EXPECT_NE(mesh_position_fingerprint(*preview_after),
            mesh_position_fingerprint(*preview_before));

  const ModelingCommandResult cancel =
      service.cancel_active_operation_preview();
  EXPECT_TRUE(cancel.ok) << cancel.message;
  EXPECT_FALSE(service.tool_settings_target().active);
}

TEST(ModelingSessionServiceTests,
     EdgeBevelPreviewRebuildsAfterDirectAddEdgeSelectionWhileActive) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);
  PreviewClosedEdgeFixture fixture = make_preview_closed_edge_fixture();

  const ModelingCreateBoxResult mesh = service.create_mesh({
      .document = fixture.document,
      .name = "Edge Bevel Preview Add Edge Mesh",
  });
  ASSERT_TRUE(mesh.ok) << mesh.message;

  ModelingInteractionSettings settings = service.interaction_settings();
  settings.edge_bevel_width = 0.1;
  ASSERT_TRUE(service.set_interaction_settings(settings).ok);

  ModelingCommandResult selected = service.set_component_selection({
      .modeling_object_id = mesh.object.modeling_object_id,
      .mode = ModelingSelectionMode::Edge,
      .edges = {fixture.first_edge},
      .active_kind = ModelingActiveSelectionKind::Edge,
      .active_edge = fixture.first_edge,
  });
  ASSERT_TRUE(selected.ok) << selected.message;
  ASSERT_TRUE(selected.changed);

  const ModelingCommandResult begin =
      service.execute_context_action("bevel_edges");
  ASSERT_TRUE(begin.ok) << begin.message;
  const std::optional<MeshAsset> first_preview =
      service.compile_object_mesh(mesh.object.modeling_object_id);
  ASSERT_TRUE(first_preview.has_value());

  selected = service.set_component_selection({
      .modeling_object_id = mesh.object.modeling_object_id,
      .mode = ModelingSelectionMode::Edge,
      .edges = {fixture.second_edge},
      .edit = ModelingSelectionEdit::Add,
      .active_kind = ModelingActiveSelectionKind::Edge,
      .active_edge = fixture.second_edge,
  });
  ASSERT_TRUE(selected.ok) << selected.message;
  EXPECT_TRUE(selected.changed);
  EXPECT_GT(service.selection_summary().selected_edge_count, 1u);
  EXPECT_TRUE(service.tool_settings_target().operation_preview_active);

  const std::optional<MeshAsset> added_preview =
      service.compile_object_mesh(mesh.object.modeling_object_id);
  ASSERT_TRUE(added_preview.has_value());
  EXPECT_NE(mesh_position_fingerprint(*first_preview),
            mesh_position_fingerprint(*added_preview));
}

TEST(ModelingSessionServiceTests,
     ThickenPreviewRebuildsAfterDirectAddFaceSelectionWhileActive) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);
  PreviewParallelQuadFixture fixture = make_preview_parallel_quad_fixture();

  const ModelingCreateBoxResult mesh = service.create_mesh({
      .document = fixture.document,
      .name = "Thicken Preview Add Face Mesh",
  });
  ASSERT_TRUE(mesh.ok) << mesh.message;

  ModelingCommandResult selected = service.set_component_selection({
      .modeling_object_id = mesh.object.modeling_object_id,
      .mode = ModelingSelectionMode::Face,
      .faces = {fixture.front_face},
      .active_kind = ModelingActiveSelectionKind::Face,
      .active_face = fixture.front_face,
  });
  ASSERT_TRUE(selected.ok) << selected.message;
  ASSERT_TRUE(selected.changed);

  const ModelingCommandResult begin =
      service.execute_context_action("thicken_faces");
  ASSERT_TRUE(begin.ok) << begin.message;
  const std::optional<MeshAsset> first_preview =
      service.compile_object_mesh(mesh.object.modeling_object_id);
  ASSERT_TRUE(first_preview.has_value());

  selected = service.set_component_selection({
      .modeling_object_id = mesh.object.modeling_object_id,
      .mode = ModelingSelectionMode::Face,
      .faces = {fixture.back_face},
      .edit = ModelingSelectionEdit::Add,
      .active_kind = ModelingActiveSelectionKind::Face,
      .active_face = fixture.back_face,
  });
  ASSERT_TRUE(selected.ok) << selected.message;
  EXPECT_TRUE(selected.changed);
  EXPECT_GT(service.selection_summary().selected_face_count, 1u);
  EXPECT_TRUE(service.tool_settings_target().operation_preview_active);

  const std::optional<MeshAsset> added_preview =
      service.compile_object_mesh(mesh.object.modeling_object_id);
  ASSERT_TRUE(added_preview.has_value());
  EXPECT_NE(mesh_position_fingerprint(*first_preview),
            mesh_position_fingerprint(*added_preview));
}

TEST(ModelingSessionServiceTests,
     BridgePreviewRebuildsAfterDirectReplaceFaceSelectionWhileActive) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);
  PreviewBridgeFixture fixture = make_preview_bridge_fixture();

  const ModelingCreateBoxResult mesh = service.create_mesh({
      .document = fixture.document,
      .name = "Bridge Preview Replace Face Mesh",
  });
  ASSERT_TRUE(mesh.ok) << mesh.message;

  ModelingCommandResult selected = service.set_component_selection({
      .modeling_object_id = mesh.object.modeling_object_id,
      .mode = ModelingSelectionMode::Face,
      .faces = {fixture.first_front_face, fixture.first_back_face},
      .active_kind = ModelingActiveSelectionKind::Face,
      .active_face = fixture.first_back_face,
  });
  ASSERT_TRUE(selected.ok) << selected.message;
  ASSERT_TRUE(selected.changed);

  const ModelingCommandResult begin =
      service.execute_context_action("interpolated_bridge_faces");
  ASSERT_TRUE(begin.ok) << begin.message;
  const std::optional<MeshAsset> first_preview =
      service.compile_object_mesh(mesh.object.modeling_object_id);
  ASSERT_TRUE(first_preview.has_value());

  selected = service.set_component_selection({
      .modeling_object_id = mesh.object.modeling_object_id,
      .mode = ModelingSelectionMode::Face,
      .faces = {fixture.second_front_face, fixture.second_back_face},
      .active_kind = ModelingActiveSelectionKind::Face,
      .active_face = fixture.second_back_face,
  });
  ASSERT_TRUE(selected.ok) << selected.message;
  EXPECT_TRUE(selected.changed);
  EXPECT_GE(service.selection_summary().selected_face_count, 2u);
  EXPECT_TRUE(service.tool_settings_target().operation_preview_active);

  const std::optional<MeshAsset> replaced_preview =
      service.compile_object_mesh(mesh.object.modeling_object_id);
  ASSERT_TRUE(replaced_preview.has_value());
  EXPECT_NE(mesh_position_fingerprint(*first_preview),
            mesh_position_fingerprint(*replaced_preview));
}

TEST(ModelingSessionServiceTests,
     NormalBridgeFacesIgnoresInterpolatedBridgeSteps) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);
  PreviewBridgeFixture fixture = make_preview_bridge_fixture();

  const ModelingCreateBoxResult mesh = service.create_mesh({
      .document = fixture.document,
      .name = "Normal Bridge Fixed Step Mesh",
  });
  ASSERT_TRUE(mesh.ok) << mesh.message;

  ModelingInteractionSettings settings = service.interaction_settings();
  settings.bridge_steps = 4;
  const ModelingCommandResult settings_update =
      service.set_interaction_settings(settings);
  ASSERT_TRUE(settings_update.ok) << settings_update.message;

  const ModelingCommandResult selected = service.set_component_selection({
      .modeling_object_id = mesh.object.modeling_object_id,
      .mode = ModelingSelectionMode::Face,
      .faces = {fixture.first_front_face, fixture.first_back_face},
      .active_kind = ModelingActiveSelectionKind::Face,
      .active_face = fixture.first_back_face,
  });
  ASSERT_TRUE(selected.ok) << selected.message;
  ASSERT_TRUE(selected.changed);

  const ModelingCommandResult bridge =
      service.execute_context_action("bridge_faces");

  ASSERT_TRUE(bridge.ok) << bridge.message;
  ASSERT_TRUE(bridge.changed) << bridge.message;
  EXPECT_EQ(service.selection_summary().selected_face_count, 4u);
  EXPECT_FALSE(service.tool_settings_target().operation_preview_active);
}

TEST(ModelingSessionServiceTests,
     MergeByDistancePreviewRebuildsAfterDirectAddVertexSelectionWhileActive) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);
  PreviewMergeDistanceFixture fixture = make_preview_merge_distance_fixture();

  const ModelingCreateBoxResult mesh = service.create_mesh({
      .document = fixture.document,
      .name = "Merge Distance Preview Add Vertex Mesh",
  });
  ASSERT_TRUE(mesh.ok) << mesh.message;

  ModelingCommandResult selected = service.set_component_selection({
      .modeling_object_id = mesh.object.modeling_object_id,
      .mode = ModelingSelectionMode::Vertex,
      .vertices = {fixture.first_near_a, fixture.first_near_b},
      .active_kind = ModelingActiveSelectionKind::Vertex,
      .active_vertex = fixture.first_near_b,
  });
  ASSERT_TRUE(selected.ok) << selected.message;
  ASSERT_TRUE(selected.changed);

  const ModelingCommandResult begin =
      service.execute_context_action("merge_by_distance");
  ASSERT_TRUE(begin.ok) << begin.message;
  const std::optional<MeshAsset> first_preview =
      service.compile_object_mesh(mesh.object.modeling_object_id);
  ASSERT_TRUE(first_preview.has_value());

  selected = service.set_component_selection({
      .modeling_object_id = mesh.object.modeling_object_id,
      .mode = ModelingSelectionMode::Vertex,
      .vertices = {fixture.second_near_a, fixture.second_near_b},
      .edit = ModelingSelectionEdit::Add,
      .active_kind = ModelingActiveSelectionKind::Vertex,
      .active_vertex = fixture.second_near_b,
  });
  ASSERT_TRUE(selected.ok) << selected.message;
  EXPECT_TRUE(selected.changed);
  EXPECT_GT(service.selection_summary().selected_vertex_count, 0u);
  EXPECT_TRUE(service.tool_settings_target().operation_preview_active);

  const std::optional<MeshAsset> added_preview =
      service.compile_object_mesh(mesh.object.modeling_object_id);
  ASSERT_TRUE(added_preview.has_value());
  EXPECT_NE(mesh_position_fingerprint(*first_preview),
            mesh_position_fingerprint(*added_preview));
}

TEST(ModelingSessionServiceTests,
     SliceQuadPreviewRebuildsAfterFacePickWhileActive) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Slice Preview Rebuild Box",
  });
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Face).ok);

  const ModelingSelectionSummary front_picked =
      service.pick_select(ModelingPickSelectRequest{
          .ray = {.origin = {0.0f, 0.0f, 5.0f},
                  .direction = {0.0f, 0.0f, -1.0f}},
          .click_count = 1,
      });
  ASSERT_EQ(front_picked.selected_face_count, 1u);

  const ModelingCommandResult begin =
      service.execute_context_action("slice_quad");
  ASSERT_TRUE(begin.ok) << begin.message;
  const std::optional<MeshAsset> front_preview =
      service.compile_object_mesh(box.object.modeling_object_id);
  ASSERT_TRUE(front_preview.has_value());

  const ModelingSelectionSummary side_picked =
      service.pick_select(ModelingPickSelectRequest{
          .ray = {.origin = {5.0f, 0.0f, 0.0f},
                  .direction = {-1.0f, 0.0f, 0.0f}},
          .click_count = 1,
      });
  ASSERT_EQ(side_picked.selected_face_count, 4u);
  EXPECT_TRUE(service.tool_settings_target().operation_preview_active);
  const std::optional<QVec3> side_center = service.selection_center();
  ASSERT_TRUE(side_center.has_value());
  EXPECT_NEAR(side_center->x, 1.0f, 0.001f);

  const std::optional<MeshAsset> side_preview =
      service.compile_object_mesh(box.object.modeling_object_id);
  ASSERT_TRUE(side_preview.has_value());
  EXPECT_NE(mesh_position_fingerprint(*front_preview),
            mesh_position_fingerprint(*side_preview));
}

TEST(ModelingSessionServiceTests,
     SliceQuadPreviewRebuildsAfterAddFacePickWhileActive) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Slice Preview Add Face Box",
  });
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Face).ok);

  const ModelingSelectionSummary front_picked =
      service.pick_select(ModelingPickSelectRequest{
          .ray = {.origin = {0.0f, 0.0f, 5.0f},
                  .direction = {0.0f, 0.0f, -1.0f}},
          .click_count = 1,
      });
  ASSERT_EQ(front_picked.selected_face_count, 1u);

  const ModelingCommandResult begin =
      service.execute_context_action("slice_quad");
  ASSERT_TRUE(begin.ok) << begin.message;
  const std::optional<MeshAsset> front_preview =
      service.compile_object_mesh(box.object.modeling_object_id);
  ASSERT_TRUE(front_preview.has_value());
  const MeshComponent *front_scene_mesh_component =
      scene.mesh(box.object.entity);
  ASSERT_NE(front_scene_mesh_component, nullptr);
  const MeshAsset *front_scene_preview =
      assets.mesh(front_scene_mesh_component->mesh);
  ASSERT_NE(front_scene_preview, nullptr);
  const double front_scene_preview_fingerprint =
      mesh_position_fingerprint(*front_scene_preview);

  const ModelingSelectionSummary added =
      service.pick_select(ModelingPickSelectRequest{
          .ray = {.origin = {5.0f, 0.0f, 0.0f},
                  .direction = {-1.0f, 0.0f, 0.0f}},
          .click_count = 1,
          .shift_pressed = true,
      });
  EXPECT_GT(added.selected_face_count, 4u);
  EXPECT_TRUE(service.tool_settings_target().operation_preview_active);

  const std::optional<MeshAsset> add_preview =
      service.compile_object_mesh(box.object.modeling_object_id);
  ASSERT_TRUE(add_preview.has_value());
  EXPECT_NE(mesh_position_fingerprint(*front_preview),
            mesh_position_fingerprint(*add_preview));
  const MeshComponent *add_scene_mesh_component = scene.mesh(box.object.entity);
  ASSERT_NE(add_scene_mesh_component, nullptr);
  const MeshAsset *add_scene_preview =
      assets.mesh(add_scene_mesh_component->mesh);
  ASSERT_NE(add_scene_preview, nullptr);
  EXPECT_NE(front_scene_preview_fingerprint,
            mesh_position_fingerprint(*add_scene_preview));
}

TEST(ModelingSessionServiceTests,
     SliceQuadPreviewRebuildsAfterDirectAddFaceSelectionWhileActive) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Slice Preview Direct Add Face Box",
  });
  ASSERT_TRUE(box.ok) << box.message;

  ModelingCommandResult selected = service.set_component_selection({
      .modeling_object_id = box.object.modeling_object_id,
      .mode = ModelingSelectionMode::Face,
      .faces = {1},
      .active_kind = ModelingActiveSelectionKind::Face,
      .active_face = 1,
  });
  ASSERT_TRUE(selected.ok) << selected.message;
  ASSERT_TRUE(selected.changed);

  const ModelingCommandResult begin =
      service.execute_context_action("slice_quad");
  ASSERT_TRUE(begin.ok) << begin.message;
  const std::optional<MeshAsset> first_preview =
      service.compile_object_mesh(box.object.modeling_object_id);
  ASSERT_TRUE(first_preview.has_value());

  selected = service.set_component_selection({
      .modeling_object_id = box.object.modeling_object_id,
      .mode = ModelingSelectionMode::Face,
      .faces = {2},
      .edit = ModelingSelectionEdit::Add,
      .active_kind = ModelingActiveSelectionKind::Face,
      .active_face = 2,
  });
  ASSERT_TRUE(selected.ok) << selected.message;
  EXPECT_TRUE(selected.changed);
  const ModelingSelectionSummary summary = service.selection_summary();
  EXPECT_EQ(summary.mode, ModelingSelectionMode::Face);
  EXPECT_GT(summary.selected_face_count, 1u);
  EXPECT_TRUE(service.tool_settings_target().operation_preview_active);

  const std::optional<MeshAsset> added_preview =
      service.compile_object_mesh(box.object.modeling_object_id);
  ASSERT_TRUE(added_preview.has_value());
  EXPECT_NE(mesh_position_fingerprint(*first_preview),
            mesh_position_fingerprint(*added_preview));
}

TEST(ModelingSessionServiceTests,
     SliceQuadPreviewRebuildsAfterFaceBoxSelectWhileActive) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Slice Preview Box Select Box",
  });
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Face).ok);

  const ModelingSelectionSummary front_picked =
      service.pick_select(ModelingPickSelectRequest{
          .ray = {.origin = {0.0f, 0.0f, 5.0f},
                  .direction = {0.0f, 0.0f, -1.0f}},
          .click_count = 1,
      });
  ASSERT_EQ(front_picked.selected_face_count, 1u);

  const ModelingCommandResult begin =
      service.execute_context_action("slice_quad");
  ASSERT_TRUE(begin.ok) << begin.message;
  const std::optional<MeshAsset> front_preview =
      service.compile_object_mesh(box.object.modeling_object_id);
  ASSERT_TRUE(front_preview.has_value());

  ModelingViewportCamera side_camera = test_orthographic_viewport_camera();
  side_camera.origin = {10.0f, 0.0f, 0.0f};
  side_camera.x_axis = {0.0f, 1.0f, 0.0f};
  side_camera.y_axis = {0.0f, 0.0f, 1.0f};
  side_camera.z_axis = {1.0f, 0.0f, 0.0f};
  side_camera.orthographic_size = 4.0;

  const ModelingSelectionSummary box_selected =
      service.box_select(ModelingBoxSelectRequest{
          .camera = side_camera,
          .start = {20.0f, 20.0f},
          .end = {80.0f, 80.0f},
          .edit = ModelingSelectionEdit::Replace,
          .rule = ModelingBoxSelectionRule::Contain,
      });
  EXPECT_GT(box_selected.selected_face_count, 4u);
  EXPECT_TRUE(service.tool_settings_target().operation_preview_active);

  const std::optional<MeshAsset> box_preview =
      service.compile_object_mesh(box.object.modeling_object_id);
  ASSERT_TRUE(box_preview.has_value());
  EXPECT_NE(mesh_position_fingerprint(*front_preview),
            mesh_position_fingerprint(*box_preview));
}

TEST(ModelingSessionServiceTests,
     ContextActionsExposeFacePanelAndKeepBridgeDisabledForOneFace) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Face Action Box",
  });
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Face).ok);

  const ModelingSelectionSummary picked =
      service.pick_select(ModelingPickSelectRequest{
          .ray = {.origin = {0.0f, 0.0f, 5.0f},
                  .direction = {0.0f, 0.0f, -1.0f}},
          .click_count = 1,
      });
  ASSERT_EQ(picked.selected_face_count, 1u);

  const ModelingActionSet actions = service.context_action_set({
      .surface = ModelingActionSurface::OperationsPanel,
      .include_disabled = true,
  });
  ASSERT_EQ(actions.panels.size(), 1u);
  EXPECT_EQ(find_action_panel(actions, "object_operations"), nullptr);
  EXPECT_EQ(find_action_panel(actions, "edge_operations"), nullptr);

  const ModelingActionPanel *face_panel =
      find_action_panel(actions, "face_operations");
  ASSERT_NE(face_panel, nullptr);
  EXPECT_EQ(face_panel->selection_mode_id, "face");
  EXPECT_EQ(face_panel->visibility, ModelingActionVisibility::Enabled);

  const ModelingActionItem *invert =
      find_action_item(*face_panel, "invert_faces");
  ASSERT_NE(invert, nullptr);
  EXPECT_EQ(invert->operation_id, "flip_face_normals");
  EXPECT_EQ(invert->visibility, ModelingActionVisibility::Enabled);

  const ModelingActionItem *bridge =
      find_action_item(*face_panel, "bridge_faces");
  ASSERT_NE(bridge, nullptr);
  EXPECT_EQ(bridge->operation_id, "bridge");
  EXPECT_EQ(bridge->visibility, ModelingActionVisibility::Disabled);

  const ModelingActionItem *interpolated_bridge =
      find_action_item(*face_panel, "interpolated_bridge_faces");
  ASSERT_NE(interpolated_bridge, nullptr);
  EXPECT_EQ(interpolated_bridge->operation_id, "bridge");
  EXPECT_EQ(interpolated_bridge->preview_kind,
            ModelingActionPreviewKind::Bridge);

  const ModelingCommandResult executed =
      service.execute_context_action("invert_faces");
  EXPECT_TRUE(executed.ok) << executed.message;
}

TEST(
    ModelingSessionServiceTests,
    RmbMeshPickWithExistingFaceSelectionPreservesSelectionAndAllowsContextMenu) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "RMB Face Context Box",
  });
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Face).ok);

  const ModelingSelectionSummary picked =
      service.pick_select(ModelingPickSelectRequest{
          .ray = {.origin = {0.0f, 0.0f, 5.0f},
                  .direction = {0.0f, 0.0f, -1.0f}},
          .click_count = 1,
      });
  ASSERT_TRUE(picked.has_selection);
  ASSERT_EQ(picked.selected_face_count, 1u);

  const ModelingSelectionSummary multi_picked =
      service.pick_select(ModelingPickSelectRequest{
          .ray = {.origin = {0.0f, 0.0f, -5.0f},
                  .direction = {0.0f, 0.0f, 1.0f}},
          .click_count = 1,
          .shift_pressed = true,
      });
  ASSERT_TRUE(multi_picked.has_selection);
  ASSERT_EQ(multi_picked.selected_face_count, 2u);
  const int active_object_before_context = multi_picked.active_object_id;
  const int active_face_before_context = multi_picked.active_face_id;

  const ModelingSelectionSummary rmb_on_unselected_with_existing_selection =
      service.pick_select(ModelingPickSelectRequest{
          .ray = {.origin = {5.0f, 0.0f, 0.0f},
                  .direction = {-1.0f, 0.0f, 0.0f}},
          .click_count = 1,
          .preserve_selection_on_miss = true,
          .preserve_selection_on_selected_hit = true,
          .preserve_existing_component_selection_on_hit = true,
      });
  ASSERT_TRUE(rmb_on_unselected_with_existing_selection.has_selection);
  EXPECT_EQ(rmb_on_unselected_with_existing_selection.active_object_id,
            active_object_before_context);
  EXPECT_EQ(rmb_on_unselected_with_existing_selection.active_face_id,
            active_face_before_context);
  EXPECT_EQ(rmb_on_unselected_with_existing_selection.selected_face_count, 2u);

  const ModelingSelectionSummary rmb_on_already_selected =
      service.pick_select(ModelingPickSelectRequest{
          .ray = {.origin = {0.0f, 0.0f, 5.0f},
                  .direction = {0.0f, 0.0f, -1.0f}},
          .click_count = 1,
          .preserve_selection_on_miss = true,
          .preserve_selection_on_selected_hit = true,
          .preserve_existing_component_selection_on_hit = true,
      });
  ASSERT_TRUE(rmb_on_already_selected.has_selection);
  EXPECT_EQ(rmb_on_already_selected.selected_face_count, 2u);

  ModelingActionQuery query;
  query.surface = ModelingActionSurface::ViewportContextMenu;
  query.include_disabled = true;
  const std::vector<ModelingActionItem> actions =
      service.context_actions(query);
  ASSERT_FALSE(actions.empty());

  EXPECT_TRUE(should_open_viewport_context_menu({
      .right_press_tracked = true,
      .right_released = true,
      .release_inside_viewport = true,
      .target_hit = rmb_on_already_selected.has_selection,
      .has_actions = !actions.empty(),
      .press_screen = {320.0f, 240.0f},
      .release_screen = {320.0f, 240.0f},
  }));

  const ModelingSelectionSummary missed_with_preserved_selection =
      service.pick_select(ModelingPickSelectRequest{
          .ray = {.origin = {8.0f, 8.0f, 5.0f},
                  .direction = {0.0f, 0.0f, -1.0f}},
          .click_count = 1,
          .preserve_selection_on_miss = true,
      });
  ASSERT_TRUE(missed_with_preserved_selection.has_selection);
  EXPECT_EQ(missed_with_preserved_selection.selected_face_count, 2u);
  EXPECT_TRUE(should_open_viewport_context_menu({
      .right_press_tracked = true,
      .right_released = true,
      .release_inside_viewport = true,
      .target_hit = missed_with_preserved_selection.has_selection,
      .has_actions = true,
      .press_screen = {100.0f, 100.0f},
      .release_screen = {100.0f, 100.0f},
  }));

  const ModelingSelectionSummary missed_without_preserving_selection =
      service.pick_select(ModelingPickSelectRequest{
          .ray = {.origin = {8.0f, 8.0f, 5.0f},
                  .direction = {0.0f, 0.0f, -1.0f}},
          .click_count = 1,
      });
  EXPECT_FALSE(missed_without_preserving_selection.has_selection);
}

TEST(ModelingSessionServiceTests,
     RmbObjectPickOnSelectedMultiSelectionPreservesSelection) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult left = service.create_box_from_bounds({
      .bounds = {.min = {-4.0f, -1.0f, -1.0f}, .max = {-2.0f, 1.0f, 1.0f}},
      .name = "Left RMB Context Box",
  });
  ASSERT_TRUE(left.ok) << left.message;
  const ModelingCreateBoxResult right = service.create_box_from_bounds({
      .bounds = {.min = {2.0f, -1.0f, -1.0f}, .max = {4.0f, 1.0f, 1.0f}},
      .name = "Right RMB Context Box",
  });
  ASSERT_TRUE(right.ok) << right.message;
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Object).ok);
  ASSERT_TRUE(service.clear_selection().ok);

  const ModelingRay left_ray{
      .origin = {-3.0f, 0.0f, 5.0f},
      .direction = {0.0f, 0.0f, -1.0f},
  };
  const ModelingRay right_ray{
      .origin = {3.0f, 0.0f, 5.0f},
      .direction = {0.0f, 0.0f, -1.0f},
  };

  ModelingSelectionSummary summary =
      service.pick_select(ModelingPickSelectRequest{
          .ray = left_ray,
          .click_count = 1,
      });
  ASSERT_EQ(summary.selected_object_count, 1u);

  summary = service.pick_select(ModelingPickSelectRequest{
      .ray = right_ray,
      .click_count = 1,
      .shift_pressed = true,
  });
  ASSERT_EQ(summary.selected_object_count, 2u);

  summary = service.pick_select(ModelingPickSelectRequest{
      .ray = left_ray,
      .click_count = 1,
      .preserve_selection_on_miss = true,
      .preserve_selection_on_selected_hit = true,
  });
  EXPECT_EQ(summary.selected_object_count, 2u);
}

TEST(ModelingSessionServiceTests,
     ContextActionsHideWhileBoxOrModalOwnsViewport) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  ModelingActionQuery context_query;
  context_query.surface = ModelingActionSurface::ViewportContextMenu;
  context_query.include_disabled = true;

  ModelingActionQuery explicit_modal_query = context_query;
  explicit_modal_query.modal_tool_owns_viewport = true;
  EXPECT_TRUE(service.context_action_set(explicit_modal_query).panels.empty());
  EXPECT_TRUE(service.context_actions(explicit_modal_query).empty());

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Modal Action Box",
  });
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_FALSE(service.context_actions(context_query).empty());

  ASSERT_TRUE(service.activate_box_tool().ok);
  EXPECT_TRUE(service.context_action_set().panels.empty());
  EXPECT_TRUE(service.context_actions(context_query).empty());
  EXPECT_FALSE(service.execute_context_action("invert_mesh").ok);

  ModelingActionQuery shelf_query;
  shelf_query.surface = ModelingActionSurface::OperationsPanel;
  shelf_query.include_disabled = true;
  shelf_query.include_hidden = false;
  shelf_query.suppress_when_modal_tool_owns_viewport = false;
  EXPECT_FALSE(service.context_action_set(shelf_query).panels.empty());

  const ModelingCommandResult cancel = service.cancel_box();
  ASSERT_TRUE(cancel.ok) << cancel.message;
  EXPECT_FALSE(service.context_actions(context_query).empty());
}

TEST(ModelingSessionServiceTests, CreatesBoxThroughRuntimeWhenAvailable) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult result = service.create_box_from_bounds({
      .bounds =
          {
              .min = {-2.0f, -1.0f, -3.0f},
              .max = {2.0f, 1.0f, 3.0f},
          },
      .name = "Test Box",
  });

  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_TRUE(result.changed);
  EXPECT_GT(result.object.modeling_object_id, 0);
  EXPECT_NE(result.object.entity, kInvalidEntityId);
  EXPECT_NE(result.object.mesh, kInvalidAssetId);
  EXPECT_EQ(
      service.entity_for_modeling_object(result.object.modeling_object_id),
      result.object.entity);
  EXPECT_EQ(service.modeling_object_for_entity(result.object.entity),
            result.object.modeling_object_id);

  const MeshAsset *stored_mesh = assets.mesh(result.object.mesh);
  ASSERT_NE(stored_mesh, nullptr);
  EXPECT_EQ(stored_mesh->name, "Test Box");
  EXPECT_FALSE(stored_mesh->vertices.empty());
  EXPECT_FALSE(stored_mesh->indices.empty());
  EXPECT_TRUE(stored_mesh->has_stable_cpu_topology);
  const std::optional<SceneBounds> selected_bounds =
      selected_world_bounds(scene, assets);
  ASSERT_TRUE(selected_bounds.has_value());
  const QVec3 selected_center = bounds_center(*selected_bounds);
  EXPECT_NEAR(selected_center.x, 0.0f, 0.001f);
  EXPECT_NEAR(selected_center.y, 0.0f, 0.001f);
  EXPECT_NEAR(selected_center.z, 0.0f, 0.001f);
  ASSERT_NE(scene.mesh(result.object.entity), nullptr);
  EXPECT_EQ(scene.mesh(result.object.entity)->mesh, result.object.mesh);
  ASSERT_NE(scene.modeling_object(result.object.entity), nullptr);
  EXPECT_EQ(scene.modeling_object(result.object.entity)->object_id,
            result.object.modeling_object_id);
}

TEST(ModelingSessionServiceTests, InsertEdgeLoopDragCommitsMesh) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Edge Loop Source",
  });
  ASSERT_TRUE(box.ok) << box.message;
  const std::optional<MeshAsset> before =
      service.compile_object_mesh(box.object.modeling_object_id);
  ASSERT_TRUE(before.has_value());

  ModelingCommandResult command =
      service.activate_viewport_tool(ModelingViewportTool::InsertEdgeLoop);
  ASSERT_TRUE(command.ok) << command.message;
  EXPECT_EQ(service.selection_summary().mode, ModelingSelectionMode::Edge);
  EXPECT_TRUE(service.tool_status().insert_edge_loop_active);

  command = service.begin_insert_edge_loop_drag(test_insert_edge_loop_event());
  ASSERT_TRUE(command.ok) << command.message;
  EXPECT_TRUE(service.tool_status().insert_edge_loop_dragging);
  EXPECT_TRUE(service.tool_status().insert_edge_loop_has_preview);

  command = service.commit_insert_edge_loop_drag();
  ASSERT_TRUE(command.ok) << command.message;
  EXPECT_TRUE(command.changed);
  EXPECT_FALSE(service.tool_status().insert_edge_loop_active);
  EXPECT_FALSE(service.tool_status().insert_edge_loop_dragging);
  EXPECT_FALSE(service.tool_status().insert_edge_loop_has_preview);
  EXPECT_EQ(service.selection_summary().mode, ModelingSelectionMode::Edge);

  const std::optional<MeshAsset> after =
      service.compile_object_mesh(box.object.modeling_object_id);
  ASSERT_TRUE(after.has_value());
  EXPECT_GT(after->vertices.size(), before->vertices.size());
  EXPECT_GT(after->indices.size(), before->indices.size());
}

TEST(ModelingSessionServiceTests,
     InsertEdgeLoopCommitSurvivesReleaseFrameHover) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Release Hover Edge Loop Source",
  });
  ASSERT_TRUE(box.ok) << box.message;

  std::optional<MeshAsset> before =
      service.compile_object_mesh(box.object.modeling_object_id);
  ASSERT_TRUE(before.has_value());

  ModelingCommandResult command =
      service.activate_viewport_tool(ModelingViewportTool::InsertEdgeLoop);
  ASSERT_TRUE(command.ok) << command.message;
  command = service.begin_insert_edge_loop_drag(test_insert_edge_loop_event());
  ASSERT_TRUE(command.ok) << command.message;
  ASSERT_TRUE(service.tool_status().insert_edge_loop_dragging);

  command =
      service.update_insert_edge_loop_hover(test_insert_edge_loop_event());
  ASSERT_TRUE(command.ok) << command.message;
  EXPECT_TRUE(service.tool_status().insert_edge_loop_dragging);

  command = service.commit_insert_edge_loop_drag();
  ASSERT_TRUE(command.ok) << command.message;
  EXPECT_TRUE(command.changed);
  EXPECT_FALSE(service.tool_status().insert_edge_loop_active);

  std::optional<MeshAsset> after =
      service.compile_object_mesh(box.object.modeling_object_id);
  ASSERT_TRUE(after.has_value());
  EXPECT_GT(after->vertices.size(), before->vertices.size());
}

TEST(ModelingSessionServiceTests, InsertEdgeLoopUsesViewportPickRadius) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Near Edge Loop Source",
  });
  ASSERT_TRUE(box.ok) << box.message;

  ModelingCommandResult command =
      service.activate_viewport_tool(ModelingViewportTool::InsertEdgeLoop);
  ASSERT_TRUE(command.ok) << command.message;

  ModelingToolRayEvent event = test_insert_edge_loop_event(0.0f, 0.86f);
  event.edge_radius = 0.2;
  command = service.begin_insert_edge_loop_drag(event);
  ASSERT_TRUE(command.ok) << command.message;
  EXPECT_TRUE(service.tool_status().insert_edge_loop_dragging);
  EXPECT_TRUE(service.tool_status().insert_edge_loop_has_preview);

  command = service.commit_insert_edge_loop_drag();
  ASSERT_TRUE(command.ok) << command.message;
  EXPECT_TRUE(command.changed);
  EXPECT_FALSE(service.tool_status().insert_edge_loop_active);
}

TEST(ModelingSessionServiceTests,
     PersistentObjectSelectionSuppressionKeepsTransientWire) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Object Selection Wire Source",
  });
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_EQ(service.selection_mode(), ModelingSelectionMode::Object);

  const auto selected_face_fill_count = [](const ViewportOverlayFrame &frame) {
    return static_cast<std::size_t>(std::count_if(
        frame.triangles.begin(), frame.triangles.end(),
        [](const ViewportOverlayTriangle &triangle) {
          return triangle.layer == ViewportOverlayLayer::SelectedVisibleFace ||
                 triangle.layer == ViewportOverlayLayer::SelectedXrayFace ||
                 triangle.layer == ViewportOverlayLayer::SelectedFaceMask;
        }));
  };
  const auto selected_wire_count = [](const ViewportOverlayFrame &frame) {
    return static_cast<std::size_t>(std::count_if(
        frame.lines.begin(), frame.lines.end(),
        [](const ViewportOverlayLine &line) {
          return line.layer == ViewportOverlayLayer::SelectedFaceEdge ||
                 line.layer == ViewportOverlayLayer::SelectedEdge;
        }));
  };

  const ViewportOverlayFrame full_frame = service.selection_overlay_frame();
  EXPECT_GT(selected_face_fill_count(full_frame), 0u);
  EXPECT_GT(selected_wire_count(full_frame), 0u);

  const ModelingSelectionOverlayOptions overlay_options{
      .suppress_persistent_object_selection = true,
  };
  const ViewportOverlayFrame transient_wire_frame =
      service.selection_overlay_frame(overlay_options);
  EXPECT_EQ(selected_face_fill_count(transient_wire_frame), 0u);
  EXPECT_GT(selected_wire_count(transient_wire_frame), 0u);
}

TEST(ModelingSessionServiceTests, CutAndMirrorEmitDrawnPlaneOverlay) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Plane Overlay Source",
  });
  ASSERT_TRUE(box.ok) << box.message;
  const AssetId preview_material = assets.add_material(
      make_default_quader_material("Mirror Preview Material"));
  scene.set_material(box.object.entity,
                     MaterialComponent{.material = preview_material});

  auto expect_drawn_plane_overlay = [&](ModelingViewportTool tool) {
    ModelingCommandResult command = service.activate_viewport_tool(tool);
    ASSERT_TRUE(command.ok) << command.message;
    command = tool == ModelingViewportTool::Cut
                  ? service.click_cut(test_drawn_plane_event(0.0f))
                  : service.click_mirror(test_drawn_plane_event(0.0f));
    ASSERT_TRUE(command.ok) << command.message;
    command =
        tool == ModelingViewportTool::Cut
            ? service.update_cut_hover(test_drawn_plane_event(1.0f, false))
            : service.update_mirror_hover(test_drawn_plane_event(1.0f, false));
    ASSERT_TRUE(command.ok) << command.message;

    const ViewportOverlayFrame hover_only_frame =
        service.selection_overlay_frame();
    const auto hover_only_handle_lines = static_cast<std::size_t>(std::count_if(
        hover_only_frame.lines.begin(), hover_only_frame.lines.end(),
        [](const ViewportOverlayLine &line) {
          return line.layer == ViewportOverlayLayer::ToolHandle &&
                 line.color == kOverlayToolHandleColor &&
                 line.width_pixels == 2.0f;
        }));
    const auto hover_only_handle_points =
        static_cast<std::size_t>(std::count_if(
            hover_only_frame.points.begin(), hover_only_frame.points.end(),
            [](const ViewportOverlayPoint &point) {
              return point.layer == ViewportOverlayLayer::ToolHandle &&
                     point.color == kOverlayToolHandleColor &&
                     point.size_pixels == 8.0f;
            }));
    const auto hover_only_preview_fills = static_cast<std::size_t>(
        std::count_if(hover_only_frame.triangles.begin(),
                      hover_only_frame.triangles.end(),
                      [](const ViewportOverlayTriangle &triangle) {
                        return triangle.layer ==
                               ViewportOverlayLayer::ToolPreviewFill;
                      }));
    const auto hover_only_preview_lines =
        static_cast<std::size_t>(std::count_if(
            hover_only_frame.lines.begin(), hover_only_frame.lines.end(),
            [](const ViewportOverlayLine &line) {
              return line.layer == ViewportOverlayLayer::ToolPreviewLine ||
                     line.layer == ViewportOverlayLayer::ToolPreviewSeam;
            }));
    EXPECT_EQ(hover_only_handle_lines, 1u);
    EXPECT_EQ(hover_only_handle_points, 2u);
    EXPECT_EQ(hover_only_preview_fills, 0u);
    EXPECT_EQ(hover_only_preview_lines, 0u);

    command =
        tool == ModelingViewportTool::Cut
            ? service.update_cut_hover(test_drawn_plane_event(1.0f, true))
            : service.update_mirror_hover(test_drawn_plane_event(1.0f, true));
    ASSERT_TRUE(command.ok) << command.message;

    const ViewportOverlayFrame frame = service.selection_overlay_frame();
    const auto tool_planes = static_cast<std::size_t>(std::count_if(
        frame.triangles.begin(), frame.triangles.end(),
        [](const ViewportOverlayTriangle &triangle) {
          return triangle.layer == ViewportOverlayLayer::ToolPlane &&
                 triangle.color == kOverlayToolPlaneColor;
        }));
    const auto plane_lines = static_cast<std::size_t>(std::count_if(
        frame.lines.begin(), frame.lines.end(),
        [](const ViewportOverlayLine &line) {
          return line.layer == ViewportOverlayLayer::ToolPlaneLine &&
                 line.color == kOverlayToolPlaneLineColor &&
                 line.width_pixels == 1.0f;
        }));
    const auto handle_lines = static_cast<std::size_t>(
        std::count_if(frame.lines.begin(), frame.lines.end(),
                      [](const ViewportOverlayLine &line) {
                        return line.layer == ViewportOverlayLayer::ToolHandle &&
                               line.color == kOverlayToolHandleColor &&
                               line.width_pixels == 2.0f;
                      }));
    const auto handle_points = static_cast<std::size_t>(std::count_if(
        frame.points.begin(), frame.points.end(),
        [](const ViewportOverlayPoint &point) {
          return point.layer == ViewportOverlayLayer::ToolHandle &&
                 point.color == kOverlayToolHandleColor &&
                 point.size_pixels == 8.0f;
        }));
    EXPECT_EQ(tool_planes, 2u);
    EXPECT_EQ(plane_lines, 4u);
    EXPECT_EQ(handle_lines, 1u);
    EXPECT_EQ(handle_points, 2u);

    command = tool == ModelingViewportTool::Cut ? service.end_cut_drag()
                                                : service.end_mirror_drag();
    ASSERT_TRUE(command.ok) << command.message;

    const ViewportOverlayFrame preview_frame =
        service.selection_overlay_frame();
    const auto preview_fills = static_cast<std::size_t>(std::count_if(
        preview_frame.triangles.begin(), preview_frame.triangles.end(),
        [](const ViewportOverlayTriangle &triangle) {
          return triangle.layer == ViewportOverlayLayer::ToolPreviewFill;
        }));
    const auto preview_lines = static_cast<std::size_t>(std::count_if(
        preview_frame.lines.begin(), preview_frame.lines.end(),
        [](const ViewportOverlayLine &line) {
          return line.layer == ViewportOverlayLayer::ToolPreviewLine ||
                 line.layer == ViewportOverlayLayer::ToolPreviewSeam;
        }));
    if (tool == ModelingViewportTool::Cut) {
      EXPECT_GT(preview_fills, 0u);
      EXPECT_GT(preview_lines, 0u);

      const ViewportOverlayFrame keep_both_frame = preview_frame;
      ModelingCommandResult mode_result =
          service.set_cut_mode(ModelingCutMode::DiscardLeft);
      ASSERT_TRUE(mode_result.ok) << mode_result.message;
      const ViewportOverlayFrame discard_left_frame =
          service.selection_overlay_frame();
      const auto keep_both_orange_fills = static_cast<std::size_t>(
          std::count_if(keep_both_frame.triangles.begin(),
                        keep_both_frame.triangles.end(),
                        [](const ViewportOverlayTriangle &triangle) {
                          return triangle.layer ==
                                     ViewportOverlayLayer::ToolPreviewFill &&
                                 triangle.color ==
                                     kOverlayCutPreviewKeepBothDiscardFillColor;
                        }));
      const auto discard_left_red_fills = static_cast<std::size_t>(
          std::count_if(discard_left_frame.triangles.begin(),
                        discard_left_frame.triangles.end(),
                        [](const ViewportOverlayTriangle &triangle) {
                          return triangle.layer ==
                                     ViewportOverlayLayer::ToolPreviewFill &&
                                 triangle.color ==
                                     kOverlayCutPreviewDiscardFillColor;
                        }));
      EXPECT_GT(keep_both_orange_fills, 0u);
      EXPECT_GT(discard_left_red_fills, 0u);
    } else {
      EXPECT_EQ(preview_fills, 0u);
      EXPECT_EQ(preview_lines, 0u);
      const std::vector<ModelingToolPreviewMesh> preview_meshes =
          service.tool_preview_meshes();
      ASSERT_FALSE(preview_meshes.empty());
      EXPECT_EQ(preview_meshes.front().source_entity, box.object.entity);
      EXPECT_EQ(preview_meshes.front().material, preview_material);
      EXPECT_GT(preview_meshes.front().mesh.vertices.size(), 0u);
      EXPECT_GT(preview_meshes.front().mesh.indices.size(), 0u);
    }
    (void)service.cancel_active_modal_tool();
  };

  expect_drawn_plane_overlay(ModelingViewportTool::Cut);
  expect_drawn_plane_overlay(ModelingViewportTool::Mirror);
}

TEST(ModelingSessionServiceTests, CutToolDragEndAndFinalizeSplitMesh) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Cut Source",
  });
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_EQ(scene.entities().size(), 1u);

  ModelingCommandResult command =
      service.activate_viewport_tool(ModelingViewportTool::Cut);
  ASSERT_TRUE(command.ok) << command.message;
  EXPECT_TRUE(service.tool_status().cut_active);

  command = service.click_cut(test_drawn_plane_event(0.0f));
  ASSERT_TRUE(command.ok) << command.message;
  EXPECT_TRUE(service.tool_status().cut_dragging);
  EXPECT_TRUE(service.tool_status().cut_dragging_new_point);
  EXPECT_EQ(service.tool_status().cut_point_count, 2);
  EXPECT_EQ(service.tool_status().cut_captured_point_count, 1);
  EXPECT_TRUE(service.tool_status().cut_has_line);
  EXPECT_FALSE(service.tool_status().cut_can_preview);

  command = service.update_cut_hover(test_drawn_plane_event(1.0f, false));
  ASSERT_TRUE(command.ok) << command.message;
  EXPECT_TRUE(service.tool_status().cut_dragging);
  EXPECT_TRUE(service.tool_status().cut_dragging_new_point);
  EXPECT_EQ(service.tool_status().cut_captured_point_count, 1);
  EXPECT_EQ(service.tool_status().cut_point_count, 2);
  EXPECT_TRUE(service.tool_status().cut_has_line);

  command = service.update_cut_hover(test_drawn_plane_event(1.0f, true));
  ASSERT_TRUE(command.ok) << command.message;
  EXPECT_TRUE(service.tool_status().cut_dragging);
  EXPECT_TRUE(service.tool_status().cut_dragging_new_point);
  EXPECT_EQ(service.tool_status().cut_captured_point_count, 2);
  EXPECT_EQ(service.tool_status().cut_point_count, 2);
  EXPECT_TRUE(service.tool_status().cut_has_line);
  command = service.end_cut_drag();
  ASSERT_TRUE(command.ok) << command.message;
  EXPECT_FALSE(service.tool_status().cut_dragging);
  EXPECT_EQ(service.tool_status().cut_captured_point_count, 2);
  EXPECT_EQ(service.tool_status().cut_point_count, 2);
  EXPECT_TRUE(service.tool_status().cut_can_preview);
  EXPECT_TRUE(service.tool_status().cut_preview_valid);

  command = service.finalize_cut_tool();
  ASSERT_TRUE(command.ok) << command.message;
  EXPECT_TRUE(command.changed);
  EXPECT_EQ(service.tool_status().active_tool, ModelingViewportTool::Select);
  EXPECT_FALSE(service.tool_status().cut_active);
  EXPECT_FALSE(service.tool_settings_target().active);
  EXPECT_GT(scene.entities().size(), 1u);
}

TEST(ModelingSessionServiceTests,
     CutToolActivationPromotesSelectedSceneMeshToObjectMode) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Cut Activation Source",
  });
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_EQ(scene.selected_entities().size(), 1u);

  ModelingCommandResult command =
      service.set_selection_mode(ModelingSelectionMode::Vertex);
  ASSERT_TRUE(command.ok) << command.message;
  ASSERT_EQ(service.selection_summary().mode, ModelingSelectionMode::Vertex);
  ASSERT_EQ(scene.selected_entities().size(), 1u);

  command = service.activate_viewport_tool(ModelingViewportTool::Cut);
  ASSERT_TRUE(command.ok) << command.message;
  EXPECT_EQ(service.selection_summary().mode, ModelingSelectionMode::Object);
  EXPECT_EQ(service.selection_summary().selected_object_count, 1u);
  EXPECT_EQ(service.tool_status().active_tool, ModelingViewportTool::Cut);
  EXPECT_TRUE(service.tool_status().cut_active);
  EXPECT_TRUE(service.tool_settings_target().active);
  EXPECT_EQ(service.tool_settings_target().kind,
            ModelingToolSettingsTargetKind::Cut);
}

TEST(ModelingSessionServiceTests,
     CutAndMirrorActivationPromotesRuntimeSelectionWhenSceneSelectionIsStale) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Drawn Plane Activation Source",
  });
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_EQ(service.selection_summary().selected_object_count, 1u);

  scene.clear_selection();
  ASSERT_TRUE(scene.selected_entities().empty());

  ModelingCommandResult command =
      service.activate_viewport_tool(ModelingViewportTool::Mirror);
  ASSERT_TRUE(command.ok) << command.message;
  EXPECT_EQ(service.selection_summary().mode, ModelingSelectionMode::Object);
  EXPECT_EQ(service.selection_summary().selected_object_count, 1u);
  EXPECT_EQ(service.tool_status().active_tool, ModelingViewportTool::Mirror);
  EXPECT_TRUE(service.tool_status().mirror_active);
  EXPECT_FALSE(service.tool_settings_target().active);
  EXPECT_EQ(scene.selected_entities().size(), 1u);

  command = service.activate_viewport_tool(ModelingViewportTool::Cut);
  ASSERT_TRUE(command.ok) << command.message;
  EXPECT_EQ(service.selection_summary().mode, ModelingSelectionMode::Object);
  EXPECT_EQ(service.selection_summary().selected_object_count, 1u);
  EXPECT_EQ(service.tool_status().active_tool, ModelingViewportTool::Cut);
  EXPECT_TRUE(service.tool_status().cut_active);
}

TEST(ModelingSessionServiceTests,
     MirrorToolDrawnPlaneFinalizeDuplicatesSelection) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {1.0f, -1.0f, -1.0f}, .max = {3.0f, 1.0f, 1.0f}},
      .name = "Mirror Source",
  });
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_EQ(scene.entities().size(), 1u);

  ModelingCommandResult command =
      service.activate_viewport_tool(ModelingViewportTool::Mirror);
  ASSERT_TRUE(command.ok) << command.message;
  EXPECT_TRUE(service.tool_status().mirror_active);

  command = service.click_mirror(test_drawn_plane_event(0.0f));
  ASSERT_TRUE(command.ok) << command.message;
  EXPECT_TRUE(service.tool_status().mirror_dragging);
  EXPECT_TRUE(service.tool_status().mirror_dragging_new_point);
  EXPECT_EQ(service.tool_status().mirror_point_count, 2);
  EXPECT_EQ(service.tool_status().mirror_captured_point_count, 1);
  EXPECT_TRUE(service.tool_status().mirror_has_line);
  EXPECT_FALSE(service.tool_status().mirror_can_preview);

  command = service.update_mirror_hover(test_drawn_plane_event(1.0f, false));
  ASSERT_TRUE(command.ok) << command.message;
  EXPECT_TRUE(service.tool_status().mirror_dragging);
  EXPECT_TRUE(service.tool_status().mirror_dragging_new_point);
  EXPECT_EQ(service.tool_status().mirror_captured_point_count, 1);
  EXPECT_EQ(service.tool_status().mirror_point_count, 2);
  EXPECT_TRUE(service.tool_status().mirror_has_line);

  command = service.update_mirror_hover(test_drawn_plane_event(1.0f, true));
  ASSERT_TRUE(command.ok) << command.message;
  EXPECT_TRUE(service.tool_status().mirror_dragging);
  EXPECT_TRUE(service.tool_status().mirror_dragging_new_point);
  EXPECT_EQ(service.tool_status().mirror_captured_point_count, 2);
  EXPECT_EQ(service.tool_status().mirror_point_count, 2);
  EXPECT_TRUE(service.tool_status().mirror_has_line);
  command = service.end_mirror_drag();
  ASSERT_TRUE(command.ok) << command.message;
  EXPECT_FALSE(service.tool_status().mirror_dragging);
  EXPECT_EQ(service.tool_status().mirror_captured_point_count, 2);
  EXPECT_EQ(service.tool_status().mirror_point_count, 2);
  EXPECT_TRUE(service.tool_status().mirror_can_preview);
  EXPECT_TRUE(service.tool_status().mirror_preview_valid);

  command = service.finalize_mirror_tool();
  ASSERT_TRUE(command.ok) << command.message;
  EXPECT_TRUE(command.changed);
  EXPECT_EQ(service.tool_status().active_tool, ModelingViewportTool::Select);
  EXPECT_FALSE(service.tool_status().mirror_active);
  EXPECT_EQ(scene.entities().size(), 2u);
}

TEST(ModelingSessionServiceTests,
     MirrorToolActivationPromotesSelectedSceneMeshToObjectMode) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {1.0f, -1.0f, -1.0f}, .max = {3.0f, 1.0f, 1.0f}},
      .name = "Mirror Activation Source",
  });
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_EQ(scene.selected_entities().size(), 1u);

  ModelingCommandResult command =
      service.set_selection_mode(ModelingSelectionMode::Face);
  ASSERT_TRUE(command.ok) << command.message;
  ASSERT_EQ(service.selection_summary().mode, ModelingSelectionMode::Face);
  ASSERT_EQ(scene.selected_entities().size(), 1u);

  command = service.activate_viewport_tool(ModelingViewportTool::Mirror);
  ASSERT_TRUE(command.ok) << command.message;
  EXPECT_EQ(service.selection_summary().mode, ModelingSelectionMode::Object);
  EXPECT_EQ(service.selection_summary().selected_object_count, 1u);
  EXPECT_EQ(service.tool_status().active_tool, ModelingViewportTool::Mirror);
  EXPECT_TRUE(service.tool_status().mirror_active);
}

TEST(ModelingSessionServiceTests,
     EditorSelectionProjectsSceneSelectionToRuntimeSelectionCenter) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);
  EditorSelectionTestHarness editor_selection(scene, assets, service);

  const ModelingCreateBoxResult first = service.create_box_from_bounds({
      .bounds =
          {
              .min = {-2.0f, -1.0f, -3.0f},
              .max = {2.0f, 1.0f, 3.0f},
          },
      .name = "First Box",
  });
  ASSERT_TRUE(first.ok) << first.message;

  const ModelingCreateBoxResult second = service.create_box_from_bounds({
      .bounds =
          {
              .min = {9.0f, 2.0f, -4.0f},
              .max = {13.0f, 6.0f, 2.0f},
          },
      .name = "Second Box",
  });
  ASSERT_TRUE(second.ok) << second.message;

  ASSERT_TRUE(
      select_through_editor(editor_selection.selection, {first.object.entity})
          .ok);
  std::optional<QVec3> center = service.selection_center();
  ASSERT_TRUE(center.has_value());
  EXPECT_FLOAT_EQ(center->x, 0.0f);
  EXPECT_FLOAT_EQ(center->y, 0.0f);
  EXPECT_FLOAT_EQ(center->z, 0.0f);
  const std::uint64_t first_selection_revision =
      service.selection_summary().selection_revision;
  ASSERT_TRUE(
      select_through_editor(editor_selection.selection, {first.object.entity})
          .ok);
  EXPECT_EQ(service.selection_summary().selection_revision,
            first_selection_revision);

  ASSERT_TRUE(
      select_through_editor(editor_selection.selection, {second.object.entity})
          .ok);
  center = service.selection_center();
  ASSERT_TRUE(center.has_value());
  EXPECT_FLOAT_EQ(center->x, 11.0f);
  EXPECT_FLOAT_EQ(center->y, 4.0f);
  EXPECT_FLOAT_EQ(center->z, -1.0f);
  const std::uint64_t second_selection_revision =
      service.selection_summary().selection_revision;
  ASSERT_TRUE(
      select_through_editor(editor_selection.selection, {second.object.entity})
          .ok);
  EXPECT_EQ(service.selection_summary().selection_revision,
            second_selection_revision);
}

TEST(ModelingSessionServiceTests, SelectionCommandsStaySynchronizedWithScene) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);
  EditorSelectionTestHarness editor_selection(scene, assets, service);

  const ModelingCreateBoxResult first = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "First Box",
  });
  ASSERT_TRUE(first.ok) << first.message;
  const ModelingCreateBoxResult second = service.create_box_from_bounds({
      .bounds = {.min = {4.0f, -1.0f, -1.0f}, .max = {6.0f, 1.0f, 1.0f}},
      .name = "Second Box",
  });
  ASSERT_TRUE(second.ok) << second.message;

  ASSERT_TRUE(service.clear_selection().ok);
  EXPECT_TRUE(scene.selected_entities().empty());

  const ModelingCommandResult select_all = service.select_all();
  ASSERT_TRUE(select_all.ok) << select_all.message;
  EXPECT_TRUE(select_all.changed);
  EXPECT_EQ(service.selection_summary().selected_object_count, 2u);
  EXPECT_EQ(scene.selected_entities().size(), 2u);

  const ModelingCommandResult invert_all = service.invert_selection();
  ASSERT_TRUE(invert_all.ok) << invert_all.message;
  EXPECT_TRUE(scene.selected_entities().empty());

  ASSERT_TRUE(
      select_through_editor(editor_selection.selection, {first.object.entity})
          .ok);
  const ModelingCommandResult invert_one = service.invert_selection();
  ASSERT_TRUE(invert_one.ok) << invert_one.message;
  EXPECT_EQ(service.selection_summary().selected_object_count, 1u);
  ASSERT_EQ(scene.selected_entities().size(), 1u);
  EXPECT_EQ(scene.selected_entities().front(), second.object.entity);
}

TEST(ModelingSessionServiceTests,
     SceneSelectionEditOverloadsShareActiveFallbackPolicy) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);
  EditorSelectionTestHarness editor_selection(scene, assets, service);

  const ModelingCreateBoxResult first = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "First Selection Edit Box",
  });
  ASSERT_TRUE(first.ok) << first.message;
  const ModelingCreateBoxResult second = service.create_box_from_bounds({
      .bounds = {.min = {3.0f, -1.0f, -1.0f}, .max = {5.0f, 1.0f, 1.0f}},
      .name = "Second Selection Edit Box",
  });
  ASSERT_TRUE(second.ok) << second.message;

  ASSERT_TRUE(select_through_editor(editor_selection.selection,
                                    {first.object.entity, second.object.entity})
                  .ok);
  ASSERT_TRUE(editor_selection.selection
                  .edit_scene_selection(first.object.entity,
                                        EditorSelectionEdit::Remove)
                  .ok);
  ASSERT_EQ(scene.selected_entities().size(), 1u);
  EXPECT_EQ(scene.selected_entities().front(), second.object.entity);
  EXPECT_EQ(scene.primary_selection(), second.object.entity);

  ASSERT_TRUE(select_through_editor(editor_selection.selection,
                                    {first.object.entity, second.object.entity})
                  .ok);
  const std::array<EntityId, 1> remove_first{{first.object.entity}};
  ASSERT_TRUE(
      editor_selection.selection
          .edit_scene_selection(remove_first, EditorSelectionEdit::Remove)
          .ok);
  ASSERT_EQ(scene.selected_entities().size(), 1u);
  EXPECT_EQ(scene.selected_entities().front(), second.object.entity);
  EXPECT_EQ(scene.primary_selection(), second.object.entity);

  ASSERT_TRUE(select_through_editor(editor_selection.selection,
                                    {first.object.entity, second.object.entity})
                  .ok);
  ASSERT_TRUE(editor_selection.selection
                  .edit_scene_selection(second.object.entity,
                                        EditorSelectionEdit::Toggle)
                  .ok);
  ASSERT_EQ(scene.selected_entities().size(), 1u);
  EXPECT_EQ(scene.selected_entities().front(), first.object.entity);
  EXPECT_EQ(scene.primary_selection(), first.object.entity);

  ASSERT_TRUE(select_through_editor(editor_selection.selection,
                                    {first.object.entity, second.object.entity})
                  .ok);
  const std::array<EntityId, 1> toggle_second{{second.object.entity}};
  ASSERT_TRUE(
      editor_selection.selection
          .edit_scene_selection(toggle_second, EditorSelectionEdit::Toggle)
          .ok);
  ASSERT_EQ(scene.selected_entities().size(), 1u);
  EXPECT_EQ(scene.selected_entities().front(), first.object.entity);
  EXPECT_EQ(scene.primary_selection(), first.object.entity);
}

TEST(ModelingSessionServiceTests,
     ElementSelectionCommandsWorkAcrossDifferentMeshes) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult first = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "First Component Box",
  });
  ASSERT_TRUE(first.ok) << first.message;
  const ModelingCreateBoxResult second = service.create_box_from_bounds({
      .bounds = {.min = {4.0f, -1.0f, -1.0f}, .max = {6.0f, 1.0f, 1.0f}},
      .name = "Second Component Box",
  });
  ASSERT_TRUE(second.ok) << second.message;

  ASSERT_TRUE(service.clear_selection().ok);
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Face).ok);

  ModelingSelectionSummary summary =
      service.pick_select(ModelingPickSelectRequest{
          .ray = {.origin = {0.0f, 0.0f, 5.0f},
                  .direction = {0.0f, 0.0f, -1.0f}},
          .click_count = 1,
      });
  ASSERT_EQ(summary.mode, ModelingSelectionMode::Face);
  ASSERT_EQ(summary.selected_face_count, 1u);
  ASSERT_EQ(scene.selected_entities().size(), 1u);
  EXPECT_EQ(scene.selected_entities().front(), first.object.entity);

  summary = service.pick_select(ModelingPickSelectRequest{
      .ray = {.origin = {5.0f, 0.0f, 5.0f}, .direction = {0.0f, 0.0f, -1.0f}},
      .click_count = 1,
      .shift_pressed = true,
  });
  EXPECT_EQ(summary.selected_face_count, 2u);
  EXPECT_EQ(scene.selected_entities().size(), 2u);

  const std::optional<QVec3> center = service.selection_center();
  ASSERT_TRUE(center.has_value());
  EXPECT_NEAR(center->x, 2.5f, 0.001f);
  EXPECT_NEAR(center->z, 1.0f, 0.001f);

  ASSERT_TRUE(service.clear_selection().ok);
  ASSERT_TRUE(service.select_all().ok);
  summary = service.selection_summary();
  EXPECT_EQ(summary.mode, ModelingSelectionMode::Face);
  EXPECT_EQ(summary.selected_face_count, 12u);
  EXPECT_EQ(scene.selected_entities().size(), 2u);

  ASSERT_TRUE(service.invert_selection().ok);
  summary = service.selection_summary();
  EXPECT_EQ(summary.selected_face_count, 0u);
  EXPECT_TRUE(scene.selected_entities().empty());
}

TEST(ModelingSessionServiceTests,
     ComponentSelectionRejectsInvalidPolygonIdsWithoutMutation) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Validated Component Box",
  });
  ASSERT_TRUE(box.ok) << box.message;

  ModelingCommandResult command = service.set_component_selection({
      .modeling_object_id = box.object.modeling_object_id,
      .mode = ModelingSelectionMode::Face,
      .faces = {1},
      .active_kind = ModelingActiveSelectionKind::Face,
      .active_face = 1,
  });
  ASSERT_TRUE(command.ok) << command.message;
  ASSERT_TRUE(command.changed);
  ModelingSelectionSummary summary = service.selection_summary();
  EXPECT_EQ(summary.mode, ModelingSelectionMode::Face);
  EXPECT_EQ(summary.active_kind, ModelingActiveSelectionKind::Face);
  EXPECT_EQ(summary.active_face_id, 1);
  EXPECT_EQ(summary.selected_face_count, 1u);

  const std::uint64_t selection_revision = summary.selection_revision;
  command = service.set_component_selection({
      .modeling_object_id = box.object.modeling_object_id,
      .mode = ModelingSelectionMode::Face,
      .faces = {999999},
  });
  EXPECT_FALSE(command.ok);
  summary = service.selection_summary();
  EXPECT_EQ(summary.selection_revision, selection_revision);
  EXPECT_EQ(summary.selected_face_count, 1u);
  EXPECT_EQ(summary.active_face_id, 1);
}

TEST(ModelingSessionServiceTests,
     SelectionCenterAndOverlayRespectSceneTransforms) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);
  EditorSelectionTestHarness editor_selection(scene, assets, service);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Moved Box",
  });
  ASSERT_TRUE(box.ok) << box.message;

  TransformComponent moved;
  moved.position = {7.0f, 2.0f, -3.0f};
  scene.set_transform(box.object.entity, moved);
  ASSERT_TRUE(
      select_through_editor(editor_selection.selection, {box.object.entity})
          .ok);

  const std::optional<QVec3> center = service.selection_center();
  ASSERT_TRUE(center.has_value());
  EXPECT_NEAR(center->x, 7.0f, 0.001f);
  EXPECT_NEAR(center->y, 2.0f, 0.001f);
  EXPECT_NEAR(center->z, -3.0f, 0.001f);

  const ViewportOverlayFrame frame = service.selection_overlay_frame();
  ASSERT_FALSE(frame.lines.empty());
  std::optional<SceneBounds> overlay_bounds;
  for (const ViewportOverlayLine &line : frame.lines) {
    if (!overlay_bounds) {
      overlay_bounds = SceneBounds{.min = line.start, .max = line.start};
    } else {
      include_point(*overlay_bounds, line.start);
    }
    include_point(*overlay_bounds, line.end);
  }
  ASSERT_TRUE(overlay_bounds.has_value());
  const QVec3 overlay_center = bounds_center(*overlay_bounds);
  EXPECT_NEAR(overlay_center.x, 7.0f, 0.001f);
  EXPECT_NEAR(overlay_center.y, 2.0f, 0.001f);
  EXPECT_NEAR(overlay_center.z, -3.0f, 0.001f);
}

TEST(ModelingSessionServiceTests,
     PickAndHoverRespectSceneTransformsForLocalMeshes) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult left = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Left Local Box",
  });
  ASSERT_TRUE(left.ok) << left.message;
  const ModelingCreateBoxResult right = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Right Local Box",
  });
  ASSERT_TRUE(right.ok) << right.message;

  TransformComponent left_transform;
  left_transform.position = {-4.0f, 0.0f, 0.0f};
  scene.set_transform(left.object.entity, left_transform);
  TransformComponent right_transform;
  right_transform.position = {4.0f, 0.0f, 0.0f};
  scene.set_transform(right.object.entity, right_transform);

  ASSERT_TRUE(service.clear_selection().ok);
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Object).ok);

  const ModelingCommandResult hover =
      service.update_selection_hover(ModelingHoverRequest{
          .ray =
              {
                  .origin = {4.0f, 0.0f, 5.0f},
                  .direction = {0.0f, 0.0f, -1.0f},
              },
      });
  ASSERT_TRUE(hover.ok) << hover.message;
  const ViewportOverlayFrame hover_frame = service.selection_overlay_frame();
  const bool hover_has_source_wire =
      std::any_of(hover_frame.lines.begin(), hover_frame.lines.end(),
                  [](const ViewportOverlayLine &line) {
                    return line.layer == ViewportOverlayLayer::SourceWire;
                  });
  EXPECT_FALSE(hover_has_source_wire);

  const ModelingSelectionSummary picked =
      service.pick_select(ModelingPickSelectRequest{
          .ray =
              {
                  .origin = {4.0f, 0.0f, 5.0f},
                  .direction = {0.0f, 0.0f, -1.0f},
              },
          .click_count = 1,
      });
  ASSERT_EQ(picked.selected_object_count, 1u);
  ASSERT_EQ(scene.selected_entities().size(), 1u);
  EXPECT_EQ(scene.selected_entities().front(), right.object.entity);
  EXPECT_EQ(picked.active_object_id, right.object.modeling_object_id);

  const ViewportOverlayFrame selected_frame = service.selection_overlay_frame();
  std::optional<SceneBounds> selected_wire_bounds;
  for (const ViewportOverlayLine &line : selected_frame.lines) {
    if (line.layer != ViewportOverlayLayer::SelectedEdge) {
      continue;
    }
    if (!selected_wire_bounds) {
      selected_wire_bounds = SceneBounds{.min = line.start, .max = line.start};
    } else {
      include_point(*selected_wire_bounds, line.start);
    }
    include_point(*selected_wire_bounds, line.end);
  }
  ASSERT_TRUE(selected_wire_bounds.has_value());
  EXPECT_NEAR(bounds_center(*selected_wire_bounds).x, 4.0f, 0.001f);
}

TEST(ModelingSessionServiceTests,
     VertexHoverCandidatePointsRespectSceneTransforms) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Hover Candidate Box",
  });
  ASSERT_TRUE(box.ok) << box.message;

  TransformComponent transform;
  transform.position = {4.0f, 0.0f, 0.0f};
  scene.set_transform(box.object.entity, transform);

  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Vertex).ok);
  ASSERT_TRUE(service.clear_selection().ok);

  const ModelingCommandResult hover =
      service.update_selection_hover(ModelingHoverRequest{
          .ray =
              {
                  .origin = {4.0f, 0.0f, 5.0f},
                  .direction = {0.0f, 0.0f, -1.0f},
              },
      });
  ASSERT_TRUE(hover.ok) << hover.message;

  const ViewportOverlayFrame frame = service.selection_overlay_frame();
  std::optional<SceneBounds> point_bounds;
  for (const ViewportOverlayPoint &point : frame.points) {
    if (point.layer != ViewportOverlayLayer::UnselectedVertex) {
      continue;
    }
    if (!point_bounds) {
      point_bounds = SceneBounds{.min = point.position, .max = point.position};
    } else {
      include_point(*point_bounds, point.position);
    }
  }

  ASSERT_TRUE(point_bounds.has_value());
  EXPECT_NEAR(bounds_center(*point_bounds).x, 4.0f, 0.001f);
}

TEST(ModelingSessionServiceTests,
     SelectedHoverAndModifierRemovePreviewStayDistinct) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Selected Hover Box",
  });
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Face).ok);

  const ModelingRay front_face_ray{
      .origin = {0.0f, 0.0f, 5.0f},
      .direction = {0.0f, 0.0f, -1.0f},
  };
  const ModelingSelectionSummary picked =
      service.pick_select(ModelingPickSelectRequest{
          .ray = front_face_ray,
          .click_count = 1,
      });
  ASSERT_EQ(picked.selected_face_count, 1u);
  const ModelingSelectionSummary selected_before_hover =
      service.selection_summary();
  const std::uint64_t scene_revision_before_hover = scene.revision();

  const ModelingCommandResult hover =
      service.update_selection_hover(ModelingHoverRequest{
          .ray = front_face_ray,
          .selected_modifier_pressed = false,
      });
  ASSERT_TRUE(hover.ok) << hover.message;

  const ViewportOverlayFrame frame = service.selection_overlay_frame();
  const auto hover_face_count =
      std::count_if(frame.triangles.begin(), frame.triangles.end(),
                    [](const ViewportOverlayTriangle &triangle) {
                      return triangle.layer == ViewportOverlayLayer::HoverFace;
                    });
  const auto hover_face_edge_count =
      std::count_if(frame.lines.begin(), frame.lines.end(),
                    [](const ViewportOverlayLine &line) {
                      return line.layer == ViewportOverlayLayer::HoverFaceEdge;
                    });
  const auto selected_face_edge_count = std::count_if(
      frame.lines.begin(), frame.lines.end(),
      [](const ViewportOverlayLine &line) {
        return line.layer == ViewportOverlayLayer::SelectedFaceEdge;
      });
  const bool normal_hover_uses_hover_face_color =
      std::any_of(frame.triangles.begin(), frame.triangles.end(),
                  [](const ViewportOverlayTriangle &triangle) {
                    return triangle.layer == ViewportOverlayLayer::HoverFace &&
                           triangle.color == kOverlayHoverFaceColor;
                  });
  const bool normal_hover_uses_hover_wire_color =
      std::any_of(frame.lines.begin(), frame.lines.end(),
                  [](const ViewportOverlayLine &line) {
                    return line.layer == ViewportOverlayLayer::HoverFaceEdge &&
                           line.color == kOverlayHoverWireColor;
                  });

  EXPECT_FALSE(frame.hover_target_selected);
  EXPECT_GT(hover_face_count, 0);
  EXPECT_GT(hover_face_edge_count, 0);
  EXPECT_GT(selected_face_edge_count, 0);
  EXPECT_TRUE(normal_hover_uses_hover_face_color);
  EXPECT_TRUE(normal_hover_uses_hover_wire_color);
  EXPECT_EQ(scene.revision(), scene_revision_before_hover);
  EXPECT_EQ(service.selection_summary().selected_face_count,
            selected_before_hover.selected_face_count);
  EXPECT_EQ(service.selection_summary().content_revision,
            selected_before_hover.content_revision);
  EXPECT_EQ(service.selection_summary().selection_revision,
            selected_before_hover.selection_revision);

  const ModelingCommandResult modifier_hover =
      service.update_selection_hover(ModelingHoverRequest{
          .ray = front_face_ray,
          .selected_modifier_pressed = true,
      });
  ASSERT_TRUE(modifier_hover.ok) << modifier_hover.message;

  const ViewportOverlayFrame modifier_frame = service.selection_overlay_frame();
  EXPECT_TRUE(modifier_frame.hover_target_selected);
  const auto modifier_hover_face_count = std::count_if(
      modifier_frame.triangles.begin(), modifier_frame.triangles.end(),
      [](const ViewportOverlayTriangle &triangle) {
        return triangle.layer == ViewportOverlayLayer::HoverFace;
      });
  const auto modifier_hover_face_edge_count =
      std::count_if(modifier_frame.lines.begin(), modifier_frame.lines.end(),
                    [](const ViewportOverlayLine &line) {
                      return line.layer == ViewportOverlayLayer::HoverFaceEdge;
                    });
  const bool modifier_uses_remove_face_color = std::any_of(
      modifier_frame.triangles.begin(), modifier_frame.triangles.end(),
      [](const ViewportOverlayTriangle &triangle) {
        return triangle.layer == ViewportOverlayLayer::HoverFace &&
               triangle.color == kOverlayRemoveFaceColor;
      });
  const bool modifier_uses_remove_wire_color =
      std::any_of(modifier_frame.lines.begin(), modifier_frame.lines.end(),
                  [](const ViewportOverlayLine &line) {
                    return line.layer == ViewportOverlayLayer::HoverFaceEdge &&
                           line.color == kOverlayRemoveWireColor;
                  });

  EXPECT_GT(modifier_hover_face_count, 0);
  EXPECT_GT(modifier_hover_face_edge_count, 0);
  EXPECT_TRUE(modifier_uses_remove_face_color);
  EXPECT_TRUE(modifier_uses_remove_wire_color);
  EXPECT_EQ(scene.revision(), scene_revision_before_hover);
  EXPECT_EQ(service.selection_summary().selected_face_count,
            selected_before_hover.selected_face_count);
  EXPECT_EQ(service.selection_summary().content_revision,
            selected_before_hover.content_revision);
  EXPECT_EQ(service.selection_summary().selection_revision,
            selected_before_hover.selection_revision);
}

TEST(
    ModelingSessionServiceTests,
    ClearingHoverAfterSelectionLetsSelectionColorWinAndCtrlShowsRemovePreview) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Hover Preserved Selection Box",
  });
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Face).ok);

  const ModelingRay front_face_ray{
      .origin = {0.0f, 0.0f, 5.0f},
      .direction = {0.0f, 0.0f, -1.0f},
  };
  const ModelingCommandResult initial_hover =
      service.update_selection_hover(ModelingHoverRequest{
          .ray = front_face_ray,
          .selected_modifier_pressed = false,
      });
  ASSERT_TRUE(initial_hover.ok) << initial_hover.message;
  ASSERT_TRUE(initial_hover.changed);

  const ModelingSelectionSummary picked =
      service.pick_select(ModelingPickSelectRequest{
          .ray = front_face_ray,
          .click_count = 1,
      });
  ASSERT_EQ(picked.selected_face_count, 1u);
  const ModelingHoverSummary picked_hover =
      service.preview_selection_hover(ModelingHoverRequest{
          .ray = front_face_ray,
          .selected_modifier_pressed = false,
      });
  EXPECT_EQ(picked_hover.target.kind, ModelingActiveSelectionKind::Face);
  EXPECT_TRUE(picked_hover.target_selected);

  const ModelingCommandResult clear_after_pick =
      service.clear_selection_hover();
  ASSERT_TRUE(clear_after_pick.ok) << clear_after_pick.message;

  const ViewportOverlayFrame after_click_frame =
      service.selection_overlay_frame();
  const bool hover_face_still_visible = std::any_of(
      after_click_frame.triangles.begin(), after_click_frame.triangles.end(),
      [](const ViewportOverlayTriangle &triangle) {
        return triangle.layer == ViewportOverlayLayer::HoverFace &&
               triangle.color == kOverlayHoverFaceColor;
      });
  const bool hover_face_edge_still_visible = std::any_of(
      after_click_frame.lines.begin(), after_click_frame.lines.end(),
      [](const ViewportOverlayLine &line) {
        return line.layer == ViewportOverlayLayer::HoverFaceEdge &&
               line.color == kOverlayHoverWireColor;
      });
  const bool selected_face_wire_visible_under_hover = std::any_of(
      after_click_frame.lines.begin(), after_click_frame.lines.end(),
      [](const ViewportOverlayLine &line) {
        return line.layer == ViewportOverlayLayer::SelectedFaceEdge;
      });
  EXPECT_FALSE(after_click_frame.hover_target_selected);
  EXPECT_FALSE(hover_face_still_visible);
  EXPECT_FALSE(hover_face_edge_still_visible);
  EXPECT_TRUE(selected_face_wire_visible_under_hover);

  const ModelingCommandResult modifier_hover =
      service.update_selection_hover(ModelingHoverRequest{
          .ray = front_face_ray,
          .selected_modifier_pressed = true,
      });
  ASSERT_TRUE(modifier_hover.ok) << modifier_hover.message;

  const ViewportOverlayFrame modifier_hover_frame =
      service.selection_overlay_frame();
  const bool remove_face_visible =
      std::any_of(modifier_hover_frame.triangles.begin(),
                  modifier_hover_frame.triangles.end(),
                  [](const ViewportOverlayTriangle &triangle) {
                    return triangle.layer == ViewportOverlayLayer::HoverFace &&
                           triangle.color == kOverlayRemoveFaceColor;
                  });
  const bool remove_face_edge_visible = std::any_of(
      modifier_hover_frame.lines.begin(), modifier_hover_frame.lines.end(),
      [](const ViewportOverlayLine &line) {
        return line.layer == ViewportOverlayLayer::HoverFaceEdge &&
               line.color == kOverlayRemoveWireColor;
      });
  EXPECT_TRUE(modifier_hover_frame.hover_target_selected);
  EXPECT_TRUE(remove_face_visible);
  EXPECT_TRUE(remove_face_edge_visible);

  const ModelingCommandResult clear_hover = service.clear_selection_hover();
  ASSERT_TRUE(clear_hover.ok) << clear_hover.message;

  const ViewportOverlayFrame after_clear_frame =
      service.selection_overlay_frame();
  const bool hover_face_cleared = std::none_of(
      after_clear_frame.triangles.begin(), after_clear_frame.triangles.end(),
      [](const ViewportOverlayTriangle &triangle) {
        return triangle.layer == ViewportOverlayLayer::HoverFace;
      });
  const bool selected_face_wire_after_clear = std::any_of(
      after_clear_frame.lines.begin(), after_clear_frame.lines.end(),
      [](const ViewportOverlayLine &line) {
        return line.layer == ViewportOverlayLayer::SelectedFaceEdge;
      });
  EXPECT_TRUE(hover_face_cleared);
  EXPECT_TRUE(selected_face_wire_after_clear);
}

TEST(ModelingSessionServiceTests,
     ClearingHoverAfterEdgeSelectionShowsSelectionBeforeHoverReturns) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Hover Suppressed Edge Box",
  });
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Edge).ok);

  const ModelingRay front_right_edge_ray{
      .origin = {1.0f, 0.0f, 5.0f},
      .direction = {0.0f, 0.0f, -1.0f},
  };
  const ModelingCommandResult initial_hover =
      service.update_selection_hover(ModelingHoverRequest{
          .ray = front_right_edge_ray,
          .selected_modifier_pressed = false,
          .edge_radius = 0.4f,
      });
  ASSERT_TRUE(initial_hover.ok) << initial_hover.message;
  ASSERT_TRUE(initial_hover.changed);

  const ModelingSelectionSummary picked =
      service.pick_select(ModelingPickSelectRequest{
          .ray = front_right_edge_ray,
          .click_count = 1,
          .edge_radius = 0.4f,
      });
  ASSERT_GT(picked.selected_edge_count, 0u);
  const ModelingCommandResult clear_after_pick =
      service.clear_selection_hover();
  ASSERT_TRUE(clear_after_pick.ok) << clear_after_pick.message;

  const auto has_hover_edge = [](const ViewportOverlayFrame &frame) {
    return std::any_of(frame.lines.begin(), frame.lines.end(),
                       [](const ViewportOverlayLine &line) {
                         return line.layer == ViewportOverlayLayer::HoverEdge &&
                                line.color == kOverlayHoverWireColor;
                       });
  };
  const auto has_selected_edge = [](const ViewportOverlayFrame &frame) {
    return std::any_of(frame.lines.begin(), frame.lines.end(),
                       [](const ViewportOverlayLine &line) {
                         return line.layer ==
                                ViewportOverlayLayer::SelectedEdge;
                       });
  };

  const ViewportOverlayFrame after_click_frame =
      service.selection_overlay_frame();
  EXPECT_FALSE(has_hover_edge(after_click_frame));
  EXPECT_TRUE(has_selected_edge(after_click_frame));

  const ModelingCommandResult same_target_hover =
      service.update_selection_hover(ModelingHoverRequest{
          .ray = front_right_edge_ray,
          .selected_modifier_pressed = false,
          .edge_radius = 0.4f,
      });
  ASSERT_TRUE(same_target_hover.ok) << same_target_hover.message;
  const ViewportOverlayFrame same_target_frame =
      service.selection_overlay_frame();
  EXPECT_TRUE(has_hover_edge(same_target_frame));
  EXPECT_TRUE(has_selected_edge(same_target_frame));

  const ModelingCommandResult clear_hover = service.clear_selection_hover();
  ASSERT_TRUE(clear_hover.ok) << clear_hover.message;
  ASSERT_TRUE(clear_hover.changed);
  const ModelingCommandResult rehover =
      service.update_selection_hover(ModelingHoverRequest{
          .ray = front_right_edge_ray,
          .selected_modifier_pressed = false,
          .edge_radius = 0.4f,
      });
  ASSERT_TRUE(rehover.ok) << rehover.message;
  const ViewportOverlayFrame rehover_frame = service.selection_overlay_frame();
  EXPECT_TRUE(has_hover_edge(rehover_frame));
  EXPECT_TRUE(has_selected_edge(rehover_frame));
}

TEST(ModelingSessionServiceTests,
     SelectionBoundsUsePickedComponentInsteadOfWholeObject) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Component Focus Box",
  });
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Edge).ok);

  const ModelingRay front_right_edge_ray{
      .origin = {1.0f, 0.0f, 5.0f},
      .direction = {0.0f, 0.0f, -1.0f},
  };
  const ModelingSelectionSummary picked =
      service.pick_select(ModelingPickSelectRequest{
          .ray = front_right_edge_ray,
          .click_count = 1,
          .edge_radius = 0.4f,
      });
  ASSERT_EQ(picked.selected_edge_count, 1u);

  const std::optional<SceneBounds> component_bounds =
      service.selection_bounds();
  ASSERT_TRUE(component_bounds.has_value());
  EXPECT_NEAR(component_bounds->min.x, 1.0f, 0.001f);
  EXPECT_NEAR(component_bounds->max.x, 1.0f, 0.001f);
  EXPECT_NEAR(component_bounds->min.z, 1.0f, 0.001f);
  EXPECT_NEAR(component_bounds->max.z, 1.0f, 0.001f);

  const std::optional<SceneBounds> object_bounds =
      selected_world_bounds(scene, assets);
  ASSERT_TRUE(object_bounds.has_value());
  EXPECT_NEAR(object_bounds->min.x, -1.0f, 0.001f);
  EXPECT_NEAR(object_bounds->max.x, 1.0f, 0.001f);
}

TEST(ModelingSessionServiceTests,
     ClearingHoverAfterVertexSelectionShowsSelectionBeforeHoverReturns) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Hover Suppressed Vertex Box",
  });
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Vertex).ok);

  const ModelingRay front_corner_ray{
      .origin = {1.0f, 1.0f, 5.0f},
      .direction = {0.0f, 0.0f, -1.0f},
  };
  const ModelingCommandResult initial_hover =
      service.update_selection_hover(ModelingHoverRequest{
          .ray = front_corner_ray,
          .selected_modifier_pressed = false,
          .vertex_radius = 0.4f,
      });
  ASSERT_TRUE(initial_hover.ok) << initial_hover.message;
  ASSERT_TRUE(initial_hover.changed);

  const ModelingSelectionSummary picked =
      service.pick_select(ModelingPickSelectRequest{
          .ray = front_corner_ray,
          .click_count = 1,
          .vertex_radius = 0.4f,
      });
  ASSERT_GT(picked.selected_vertex_count, 0u);
  const ModelingCommandResult clear_after_pick =
      service.clear_selection_hover();
  ASSERT_TRUE(clear_after_pick.ok) << clear_after_pick.message;

  const auto has_hover_vertex = [](const ViewportOverlayFrame &frame) {
    return std::any_of(frame.points.begin(), frame.points.end(),
                       [](const ViewportOverlayPoint &point) {
                         return point.layer ==
                                    ViewportOverlayLayer::HoverVertex &&
                                point.color == kOverlayHoverVertexColor;
                       });
  };
  const auto has_selected_vertex = [](const ViewportOverlayFrame &frame) {
    return std::any_of(frame.points.begin(), frame.points.end(),
                       [](const ViewportOverlayPoint &point) {
                         return point.layer ==
                                ViewportOverlayLayer::SelectedVertex;
                       });
  };

  const ViewportOverlayFrame after_click_frame =
      service.selection_overlay_frame();
  EXPECT_FALSE(has_hover_vertex(after_click_frame));
  EXPECT_TRUE(has_selected_vertex(after_click_frame));

  const ModelingCommandResult same_target_hover =
      service.update_selection_hover(ModelingHoverRequest{
          .ray = front_corner_ray,
          .selected_modifier_pressed = false,
          .vertex_radius = 0.4f,
      });
  ASSERT_TRUE(same_target_hover.ok) << same_target_hover.message;
  const ViewportOverlayFrame same_target_frame =
      service.selection_overlay_frame();
  EXPECT_TRUE(has_hover_vertex(same_target_frame));
  EXPECT_TRUE(has_selected_vertex(same_target_frame));

  const ModelingCommandResult clear_hover = service.clear_selection_hover();
  ASSERT_TRUE(clear_hover.ok) << clear_hover.message;
  ASSERT_TRUE(clear_hover.changed);
  const ModelingCommandResult rehover =
      service.update_selection_hover(ModelingHoverRequest{
          .ray = front_corner_ray,
          .selected_modifier_pressed = false,
          .vertex_radius = 0.4f,
      });
  ASSERT_TRUE(rehover.ok) << rehover.message;
  const ViewportOverlayFrame rehover_frame = service.selection_overlay_frame();
  EXPECT_TRUE(has_hover_vertex(rehover_frame));
  EXPECT_TRUE(has_selected_vertex(rehover_frame));
}

TEST(ModelingSessionServiceTests,
     SelectedEdgeHoverAndModifierRemovePreviewStayDistinct) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Selected Edge Remove Hover Box",
  });
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Edge).ok);

  const ModelingRay front_right_edge_ray{
      .origin = {1.0f, 0.0f, 5.0f},
      .direction = {0.0f, 0.0f, -1.0f},
  };
  const ModelingSelectionSummary picked =
      service.pick_select(ModelingPickSelectRequest{
          .ray = front_right_edge_ray,
          .click_count = 1,
          .edge_radius = 0.4f,
      });
  ASSERT_GT(picked.selected_edge_count, 0u);
  const ModelingSelectionSummary selected_before_hover =
      service.selection_summary();

  const ModelingCommandResult hover =
      service.update_selection_hover(ModelingHoverRequest{
          .ray = front_right_edge_ray,
          .selected_modifier_pressed = false,
          .edge_radius = 0.4f,
      });
  ASSERT_TRUE(hover.ok) << hover.message;

  const ViewportOverlayFrame frame = service.selection_overlay_frame();
  EXPECT_FALSE(frame.hover_target_selected);
  const bool has_selected_edge =
      std::any_of(frame.lines.begin(), frame.lines.end(),
                  [](const ViewportOverlayLine &line) {
                    return line.layer == ViewportOverlayLayer::SelectedEdge ||
                           line.layer == ViewportOverlayLayer::SelectedFaceEdge;
                  });
  const bool has_normal_hover_edge =
      std::any_of(frame.lines.begin(), frame.lines.end(),
                  [](const ViewportOverlayLine &line) {
                    return line.layer == ViewportOverlayLayer::HoverEdge &&
                           line.color == kOverlayHoverWireColor;
                  });
  EXPECT_TRUE(has_selected_edge);
  EXPECT_TRUE(has_normal_hover_edge);
  EXPECT_EQ(service.selection_summary().selected_edge_count,
            selected_before_hover.selected_edge_count);
  EXPECT_EQ(service.selection_summary().selection_revision,
            selected_before_hover.selection_revision);

  const ModelingCommandResult modifier_hover =
      service.update_selection_hover(ModelingHoverRequest{
          .ray = front_right_edge_ray,
          .selected_modifier_pressed = true,
          .edge_radius = 0.4f,
      });
  ASSERT_TRUE(modifier_hover.ok) << modifier_hover.message;

  const ViewportOverlayFrame modifier_frame = service.selection_overlay_frame();
  EXPECT_TRUE(modifier_frame.hover_target_selected);
  const bool has_remove_hover_edge =
      std::any_of(modifier_frame.lines.begin(), modifier_frame.lines.end(),
                  [](const ViewportOverlayLine &line) {
                    return line.layer == ViewportOverlayLayer::HoverEdge &&
                           line.color == kOverlayRemoveWireColor;
                  });
  EXPECT_TRUE(has_remove_hover_edge);
  EXPECT_EQ(service.selection_summary().selected_edge_count,
            selected_before_hover.selected_edge_count);
  EXPECT_EQ(service.selection_summary().selection_revision,
            selected_before_hover.selection_revision);
}

TEST(ModelingSessionServiceTests,
     SelectedVertexHoverAndModifierRemovePreviewStayDistinct) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Selected Vertex Remove Hover Box",
  });
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Vertex).ok);

  const ModelingCommandResult select_all = service.select_all();
  ASSERT_TRUE(select_all.ok) << select_all.message;
  ASSERT_GT(service.selection_summary().selected_vertex_count, 0u);
  const ModelingSelectionSummary selected_before_hover =
      service.selection_summary();

  const ModelingRay front_corner_ray{
      .origin = {1.0f, 1.0f, 5.0f},
      .direction = {0.0f, 0.0f, -1.0f},
  };
  const ModelingCommandResult normal_hover =
      service.update_selection_hover(ModelingHoverRequest{
          .ray = front_corner_ray,
          .selected_modifier_pressed = false,
          .vertex_radius = 0.4f,
      });
  ASSERT_TRUE(normal_hover.ok) << normal_hover.message;

  const ViewportOverlayFrame normal_frame = service.selection_overlay_frame();
  EXPECT_FALSE(normal_frame.hover_target_selected);
  const bool has_selected_handle =
      std::any_of(normal_frame.points.begin(), normal_frame.points.end(),
                  [](const ViewportOverlayPoint &point) {
                    return point.layer == ViewportOverlayLayer::SelectedVertex;
                  });
  const bool has_normal_hover_handle =
      std::any_of(normal_frame.points.begin(), normal_frame.points.end(),
                  [](const ViewportOverlayPoint &point) {
                    return point.layer == ViewportOverlayLayer::HoverVertex &&
                           point.color == kOverlayHoverVertexColor;
                  });
  EXPECT_TRUE(has_selected_handle);
  EXPECT_TRUE(has_normal_hover_handle);
  EXPECT_EQ(service.selection_summary().selected_vertex_count,
            selected_before_hover.selected_vertex_count);
  EXPECT_EQ(service.selection_summary().selection_revision,
            selected_before_hover.selection_revision);

  const ModelingCommandResult hover =
      service.update_selection_hover(ModelingHoverRequest{
          .ray = front_corner_ray,
          .selected_modifier_pressed = true,
          .vertex_radius = 0.4f,
      });
  ASSERT_TRUE(hover.ok) << hover.message;

  const ViewportOverlayFrame frame = service.selection_overlay_frame();
  EXPECT_TRUE(frame.hover_target_selected);
  const bool has_remove_hover_handle =
      std::any_of(frame.points.begin(), frame.points.end(),
                  [](const ViewportOverlayPoint &point) {
                    return point.layer == ViewportOverlayLayer::HoverVertex &&
                           point.color == kOverlayRemoveWireColor;
                  });
  EXPECT_TRUE(has_remove_hover_handle);
  EXPECT_EQ(service.selection_summary().selected_vertex_count,
            selected_before_hover.selected_vertex_count);
  EXPECT_EQ(service.selection_summary().selection_revision,
            selected_before_hover.selection_revision);
}

TEST(ModelingSessionServiceTests,
     InstantOperationsRefreshSceneObjectsAndSelection) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Source Box",
  });
  ASSERT_TRUE(box.ok) << box.message;
  const AssetId default_material =
      assets.add_material(make_default_quader_material());
  scene.set_material(box.object.entity,
                     MaterialComponent{.material = default_material});
  TransformComponent source_transform;
  source_transform.position = {2.0f, 0.0f, -3.0f};
  source_transform.rotation_euler = {0.25f, 0.75f, 0.0f};
  scene.set_transform(box.object.entity, source_transform);
  const MeshComponent *source_mesh_component = scene.mesh(box.object.entity);
  ASSERT_NE(source_mesh_component, nullptr);
  MeshComponent source_mesh_settings = *source_mesh_component;
  source_mesh_settings.double_sided = true;
  scene.set_mesh(box.object.entity, source_mesh_settings);
  const MeshAsset *source_mesh_asset = assets.mesh(source_mesh_settings.mesh);
  ASSERT_NE(source_mesh_asset, nullptr);
  EXPECT_EQ(scene.entities().size(), 1u);

  const std::vector<ModelingInstantOperationDescriptor> object_ops =
      service.instant_operations();
  const auto duplicate =
      std::find_if(object_ops.begin(), object_ops.end(),
                   [](const ModelingInstantOperationDescriptor &operation) {
                     return operation.operation ==
                            ModelingInstantOperationId::DuplicateMeshes;
                   });
  ASSERT_NE(duplicate, object_ops.end());
  EXPECT_TRUE(duplicate->enabled);

  const ModelingCommandResult duplicated = service.execute_instant_operation(
      ModelingInstantOperationId::DuplicateMeshes);
  ASSERT_TRUE(duplicated.ok) << duplicated.message;
  EXPECT_TRUE(duplicated.changed);
  EXPECT_EQ(scene.entities().size(), 2u);
  EXPECT_EQ(service.objects().size(), 2u);
  EXPECT_EQ(service.selection_summary().selected_object_count, 1u);
  ASSERT_EQ(scene.selected_entities().size(), 1u);
  const EntityId duplicate_entity = scene.selected_entities().front();
  EXPECT_NE(duplicate_entity, box.object.entity);
  ASSERT_NE(scene.name(duplicate_entity), nullptr);
  EXPECT_EQ(scene.name(duplicate_entity)->name, "Mesh");
  EXPECT_EQ(scene.name(duplicate_entity)->name.find("Runtime Object"),
            std::string::npos);
  const MeshComponent *duplicate_mesh = scene.mesh(duplicate_entity);
  ASSERT_NE(duplicate_mesh, nullptr);
  EXPECT_TRUE(duplicate_mesh->double_sided);
  const MeshAsset *duplicate_mesh_asset = assets.mesh(duplicate_mesh->mesh);
  ASSERT_NE(duplicate_mesh_asset, nullptr);
  EXPECT_EQ(duplicate_mesh_asset->name, "Mesh");
  ASSERT_NE(scene.transform(duplicate_entity), nullptr);
  expect_vec3_near(scene.transform(duplicate_entity)->position,
                   source_transform.position);
  expect_vec3_near(scene.transform(duplicate_entity)->rotation_euler,
                   source_transform.rotation_euler);
  ASSERT_EQ(duplicate_mesh_asset->vertices.size(),
            source_mesh_asset->vertices.size());
  for (std::size_t index = 0; index < source_mesh_asset->vertices.size();
       ++index) {
    expect_vec3_near(duplicate_mesh_asset->vertices[index].position,
                     source_mesh_asset->vertices[index].position);
  }
  const MaterialComponent *duplicate_material =
      scene.material(duplicate_entity);
  ASSERT_NE(duplicate_material, nullptr);
  EXPECT_EQ(duplicate_material->material, default_material);

  const ModelingCommandResult legacy_delete = service.execute_instant_operation(
      ModelingInstantOperationId::DeleteSelection);
  EXPECT_FALSE(legacy_delete.ok);
  EXPECT_FALSE(legacy_delete.changed);
  EXPECT_EQ(scene.entities().size(), 2u);

  EditorHistoryService history;
  const CommandResult deleted =
      history.execute(service.create_delete_selection_command());
  ASSERT_TRUE(deleted.ok) << deleted.message;
  EXPECT_TRUE(deleted.changed);
  EXPECT_EQ(scene.entities().size(), 1u);
  EXPECT_EQ(service.objects().size(), 1u);
  EXPECT_TRUE(scene.selected_entities().empty());
}

TEST(ModelingSessionServiceTests,
     RepeatedObjectSelectionProjectionDoesNotDirtySceneOrResyncMeshes) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);
  EditorSelectionTestHarness editor_selection(scene, assets, service);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds =
          {
              .min = {-1.0f, -1.0f, -1.0f},
              .max = {1.0f, 1.0f, 1.0f},
          },
      .name = "Selection Sync Box",
  });
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_TRUE(
      select_through_editor(editor_selection.selection, {box.object.entity})
          .ok);
  ASSERT_TRUE(editor_selection.selection
                  .set_object_selection(
                      std::span<const EntityId>(&box.object.entity, 1),
                      box.object.entity)
                  .ok);

  const std::uint64_t scene_revision = scene.revision();
  const ModelingSelectionSummary summary = service.selection_summary();
  ASSERT_TRUE(editor_selection.selection
                  .set_object_selection(
                      std::span<const EntityId>(&box.object.entity, 1),
                      box.object.entity)
                  .ok);

  EXPECT_EQ(scene.revision(), scene_revision);
  EXPECT_EQ(service.selection_summary().content_revision,
            summary.content_revision);
  EXPECT_EQ(service.selection_summary().selection_revision,
            summary.selection_revision);
}

TEST(ModelingSessionServiceTests, TransformPreviewZeroDeltaRestoresSceneMesh) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Zero Delta Preview Box",
  });
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Face).ok);
  const ModelingSelectionSummary picked =
      service.pick_select(ModelingPickSelectRequest{
          .ray =
              {
                  .origin = {0.0f, 0.0f, 5.0f},
                  .direction = {0.0f, 0.0f, -1.0f},
              },
          .click_count = 1,
      });
  ASSERT_EQ(picked.selected_face_count, 1u);

  const MeshComponent *mesh_component = scene.mesh(box.object.entity);
  ASSERT_NE(mesh_component, nullptr);
  const MeshAsset *source_mesh = assets.mesh(mesh_component->mesh);
  ASSERT_NE(source_mesh, nullptr);
  const std::size_t source_index_count = source_mesh->indices.size();

  ASSERT_TRUE(service.begin_transform_preview(false).ok);
  ASSERT_TRUE(
      service.update_transform_extrude_preview({0.0f, 0.0f, 1.0f}, 1.0f).ok);
  const MeshAsset *extruded_mesh = assets.mesh(mesh_component->mesh);
  ASSERT_NE(extruded_mesh, nullptr);
  ASSERT_GT(extruded_mesh->indices.size(), source_index_count);

  ASSERT_TRUE(
      service.update_transform_extrude_preview({0.0f, 0.0f, 0.0f}, 1.0f).ok);
  const MeshAsset *restored_mesh = assets.mesh(mesh_component->mesh);
  ASSERT_NE(restored_mesh, nullptr);
  EXPECT_EQ(restored_mesh->indices.size(), source_index_count);

  ASSERT_TRUE(service.cancel_transform_preview().ok);
}

TEST(ModelingSessionServiceTests,
     ComponentTransformPreviewRefreshesSelectionOverlayEveryUpdate) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Preview Overlay Box",
  });
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Face).ok);
  const ModelingSelectionSummary picked =
      service.pick_select(ModelingPickSelectRequest{
          .ray =
              {
                  .origin = {0.0f, 0.0f, 5.0f},
                  .direction = {0.0f, 0.0f, -1.0f},
              },
          .click_count = 1,
      });
  ASSERT_EQ(picked.selected_face_count, 1u);

  ASSERT_TRUE(service.begin_transform_preview(false).ok);
  ASSERT_TRUE(
      service.update_transform_translate_preview({1.0f, 0.0f, 0.0f}).ok);
  const ViewportOverlayFrame first_frame = service.selection_overlay_frame();

  ASSERT_TRUE(
      service.update_transform_translate_preview({3.0f, 0.0f, 0.0f}).ok);
  const ViewportOverlayFrame second_frame = service.selection_overlay_frame();

  auto selected_face_edge_bounds = [](const ViewportOverlayFrame &frame) {
    std::optional<SceneBounds> bounds;
    for (const ViewportOverlayLine &line : frame.lines) {
      if (line.layer != ViewportOverlayLayer::SelectedFaceEdge &&
          line.layer != ViewportOverlayLayer::SelectedEdge) {
        continue;
      }
      if (!bounds) {
        bounds = SceneBounds{.min = line.start, .max = line.start};
      } else {
        include_point(*bounds, line.start);
      }
      include_point(*bounds, line.end);
    }
    return bounds;
  };

  const std::optional<SceneBounds> first_bounds =
      selected_face_edge_bounds(first_frame);
  const std::optional<SceneBounds> second_bounds =
      selected_face_edge_bounds(second_frame);
  ASSERT_TRUE(first_bounds.has_value());
  ASSERT_TRUE(second_bounds.has_value());
  EXPECT_NEAR(bounds_center(*first_bounds).x, 1.0f, 0.001f);
  EXPECT_NEAR(bounds_center(*second_bounds).x, 3.0f, 0.001f);

  ASSERT_TRUE(service.cancel_transform_preview().ok);
}

TEST(ModelingSessionServiceTests,
     ComponentTransformPreviewMovesVertexHandlesEveryUpdate) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Preview Vertex Overlay Box",
  });
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Vertex).ok);
  const ModelingCommandResult select_all = service.select_all();
  ASSERT_TRUE(select_all.ok) << select_all.message;
  ASSERT_GT(service.selection_summary().selected_vertex_count, 0u);

  ASSERT_TRUE(service.begin_transform_preview(false).ok);
  ASSERT_TRUE(
      service.update_transform_translate_preview({1.0f, 0.0f, 0.0f}).ok);
  const ViewportOverlayFrame first_frame = service.selection_overlay_frame();

  ASSERT_TRUE(
      service.update_transform_translate_preview({3.0f, 0.0f, 0.0f}).ok);
  const ViewportOverlayFrame second_frame = service.selection_overlay_frame();

  auto selected_vertex_bounds = [](const ViewportOverlayFrame &frame) {
    std::optional<SceneBounds> bounds;
    for (const ViewportOverlayPoint &point : frame.points) {
      if (point.layer != ViewportOverlayLayer::SelectedVertex) {
        continue;
      }
      if (!bounds) {
        bounds = SceneBounds{.min = point.position, .max = point.position};
      } else {
        include_point(*bounds, point.position);
      }
    }
    return bounds;
  };

  const std::optional<SceneBounds> first_bounds =
      selected_vertex_bounds(first_frame);
  const std::optional<SceneBounds> second_bounds =
      selected_vertex_bounds(second_frame);
  ASSERT_TRUE(first_bounds.has_value());
  ASSERT_TRUE(second_bounds.has_value());
  EXPECT_NEAR(bounds_center(*first_bounds).x, 1.0f, 0.001f);
  EXPECT_NEAR(bounds_center(*second_bounds).x, 3.0f, 0.001f);

  ASSERT_TRUE(service.cancel_transform_preview().ok);
}

TEST(ModelingSessionServiceTests,
     ComponentTransformPreviewMovesEdgeHandlesEveryUpdate) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Preview Edge Overlay Box",
  });
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Edge).ok);
  const ModelingCommandResult select_all = service.select_all();
  ASSERT_TRUE(select_all.ok) << select_all.message;
  ASSERT_GT(service.selection_summary().selected_edge_count, 0u);

  ASSERT_TRUE(service.begin_transform_preview(false).ok);
  ASSERT_TRUE(
      service.update_transform_translate_preview({1.0f, 0.0f, 0.0f}).ok);
  const ViewportOverlayFrame first_frame = service.selection_overlay_frame();

  ASSERT_TRUE(
      service.update_transform_translate_preview({3.0f, 0.0f, 0.0f}).ok);
  const ViewportOverlayFrame second_frame = service.selection_overlay_frame();

  auto selected_edge_bounds = [](const ViewportOverlayFrame &frame) {
    std::optional<SceneBounds> bounds;
    for (const ViewportOverlayLine &line : frame.lines) {
      if (line.layer != ViewportOverlayLayer::SelectedEdge &&
          line.layer != ViewportOverlayLayer::SelectedFaceEdge) {
        continue;
      }
      if (!bounds) {
        bounds = SceneBounds{.min = line.start, .max = line.start};
      } else {
        include_point(*bounds, line.start);
      }
      include_point(*bounds, line.end);
    }
    return bounds;
  };

  const std::optional<SceneBounds> first_bounds =
      selected_edge_bounds(first_frame);
  const std::optional<SceneBounds> second_bounds =
      selected_edge_bounds(second_frame);
  ASSERT_TRUE(first_bounds.has_value());
  ASSERT_TRUE(second_bounds.has_value());
  EXPECT_NEAR(bounds_center(*first_bounds).x, 1.0f, 0.001f);
  EXPECT_NEAR(bounds_center(*second_bounds).x, 3.0f, 0.001f);

  ASSERT_TRUE(service.cancel_transform_preview().ok);
}

TEST(ModelingSessionServiceTests,
     ComponentTransformCommitKeepsHoverHandlesOnCommittedGeometry) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Commit Hover Overlay Box",
  });
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Vertex).ok);
  ASSERT_TRUE(service.select_all().ok);

  ASSERT_TRUE(service.begin_transform_preview(false).ok);
  ASSERT_TRUE(
      service.update_transform_translate_preview({2.0f, 0.0f, 0.0f}).ok);
  const ModelingCommandResult commit = service.commit_transform_preview();
  ASSERT_TRUE(commit.ok) << commit.message;

  ASSERT_TRUE(service.clear_selection().ok);
  const ModelingCommandResult hover =
      service.update_selection_hover(ModelingHoverRequest{
          .ray =
              {
                  .origin = {2.0f, 0.0f, 5.0f},
                  .direction = {0.0f, 0.0f, -1.0f},
              },
      });
  ASSERT_TRUE(hover.ok) << hover.message;

  const ViewportOverlayFrame frame = service.selection_overlay_frame();
  std::optional<SceneBounds> point_bounds;
  for (const ViewportOverlayPoint &point : frame.points) {
    if (point.layer != ViewportOverlayLayer::UnselectedVertex) {
      continue;
    }
    if (!point_bounds) {
      point_bounds = SceneBounds{.min = point.position, .max = point.position};
    } else {
      include_point(*point_bounds, point.position);
    }
  }

  ASSERT_TRUE(point_bounds.has_value());
  EXPECT_NEAR(bounds_center(*point_bounds).x, 2.0f, 0.001f);
}

TEST(ModelingSessionServiceTests,
     ComponentTransformCancelRestoresMeshAndOverlayTogether) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Cancel Overlay Box",
  });
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Vertex).ok);
  ASSERT_TRUE(service.select_all().ok);

  ASSERT_TRUE(service.begin_transform_preview(false).ok);
  ASSERT_TRUE(
      service.update_transform_translate_preview({2.0f, 0.0f, 0.0f}).ok);
  std::optional<SceneBounds> selected_bounds =
      selected_world_bounds(scene, assets);
  ASSERT_TRUE(selected_bounds.has_value());
  EXPECT_NEAR(bounds_center(*selected_bounds).x, 2.0f, 0.001f);

  ModelingCommandResult cancel = service.cancel_transform_preview();
  ASSERT_TRUE(cancel.ok) << cancel.message;
  selected_bounds = selected_world_bounds(scene, assets);
  ASSERT_TRUE(selected_bounds.has_value());
  EXPECT_NEAR(bounds_center(*selected_bounds).x, 0.0f, 0.001f);

  const ViewportOverlayFrame frame = service.selection_overlay_frame();
  std::optional<SceneBounds> point_bounds;
  for (const ViewportOverlayPoint &point : frame.points) {
    if (point.layer != ViewportOverlayLayer::SelectedVertex) {
      continue;
    }
    if (!point_bounds) {
      point_bounds = SceneBounds{.min = point.position, .max = point.position};
    } else {
      include_point(*point_bounds, point.position);
    }
  }

  ASSERT_TRUE(point_bounds.has_value());
  EXPECT_NEAR(bounds_center(*point_bounds).x, 0.0f, 0.001f);
}

TEST(ModelingSessionServiceTests,
     TransformDragComponentSelectionCommitsOverlayOnCommittedGeometry) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Drag Commit Overlay Box",
  });
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Vertex).ok);
  ASSERT_TRUE(service.select_all().ok);

  ModelingTransformDragPreview begin =
      service.begin_transform_drag(test_x_move_drag_begin_request());
  ASSERT_TRUE(begin.ok) << begin.message;
  EXPECT_TRUE(begin.active);
  EXPECT_TRUE(begin.preview_driven);

  ModelingTransformDragPreview update =
      service.update_transform_drag(test_x_move_drag_update_request(2.0f));
  ASSERT_TRUE(update.ok) << update.message;
  EXPECT_TRUE(update.preview_driven);
  EXPECT_NEAR(update.move_delta.x, 2.0f, 0.001f);

  ModelingTransformDragPreview commit = service.commit_transform_drag();
  ASSERT_TRUE(commit.ok) << commit.message;
  EXPECT_TRUE(commit.preview_driven);
  EXPECT_TRUE(commit.changed);

  ASSERT_TRUE(service.clear_selection().ok);
  const ModelingCommandResult hover =
      service.update_selection_hover(ModelingHoverRequest{
          .ray =
              {
                  .origin = {2.0f, 0.0f, 5.0f},
                  .direction = {0.0f, 0.0f, -1.0f},
              },
      });
  ASSERT_TRUE(hover.ok) << hover.message;

  const ViewportOverlayFrame frame = service.selection_overlay_frame();
  std::optional<SceneBounds> point_bounds;
  for (const ViewportOverlayPoint &point : frame.points) {
    if (point.layer != ViewportOverlayLayer::UnselectedVertex) {
      continue;
    }
    if (!point_bounds) {
      point_bounds = SceneBounds{.min = point.position, .max = point.position};
    } else {
      include_point(*point_bounds, point.position);
    }
  }

  ASSERT_TRUE(point_bounds.has_value());
  EXPECT_NEAR(bounds_center(*point_bounds).x, 2.0f, 0.001f);
}

TEST(ModelingSessionServiceTests,
     FaceTransformDragKeepsComponentSelectionAndMovesOnlyFaceGeometry) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Face Drag Regression Box",
  });
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Face).ok);

  const ModelingSelectionSummary picked =
      service.pick_select(ModelingPickSelectRequest{
          .ray =
              {
                  .origin = {0.0f, 0.0f, 5.0f},
                  .direction = {0.0f, 0.0f, -1.0f},
              },
          .click_count = 1,
      });
  ASSERT_EQ(picked.mode, ModelingSelectionMode::Face);
  ASSERT_EQ(picked.active_kind, ModelingActiveSelectionKind::Face);
  ASSERT_EQ(picked.selected_face_count, 1u);
  ASSERT_EQ(scene.primary_selection(), box.object.entity);

  const std::optional<MeshAsset> before_mesh =
      service.compile_object_mesh(box.object.modeling_object_id);
  ASSERT_TRUE(before_mesh.has_value());
  const std::optional<SceneBounds> before_bounds =
      local_mesh_bounds(*before_mesh);
  ASSERT_TRUE(before_bounds.has_value());
  EXPECT_NEAR(before_bounds->min.x, -1.0f, 0.001f);
  EXPECT_NEAR(before_bounds->max.x, 1.0f, 0.001f);

  const std::optional<QVec3> selection_center = service.selection_center();
  ASSERT_TRUE(selection_center.has_value());
  ModelingTransformDragPreview begin = service.begin_transform_drag(
      test_x_move_drag_begin_request(*selection_center));
  ASSERT_TRUE(begin.ok) << begin.message;
  EXPECT_TRUE(begin.active);
  EXPECT_TRUE(begin.preview_driven);

  ModelingTransformDragPreview update =
      service.update_transform_drag(test_x_move_drag_update_request(2.0f));
  ASSERT_TRUE(update.ok) << update.message;
  EXPECT_TRUE(update.preview_driven);
  EXPECT_NEAR(update.move_delta.x, 2.0f, 0.001f);

  ModelingTransformDragPreview commit = service.commit_transform_drag();
  ASSERT_TRUE(commit.ok) << commit.message;
  EXPECT_TRUE(commit.preview_driven);
  EXPECT_TRUE(commit.changed);

  const ModelingSelectionSummary after_selection = service.selection_summary();
  EXPECT_EQ(after_selection.mode, ModelingSelectionMode::Face);
  EXPECT_EQ(after_selection.active_kind, ModelingActiveSelectionKind::Face);
  EXPECT_EQ(after_selection.selected_face_count, 1u);
  EXPECT_EQ(scene.primary_selection(), box.object.entity);

  const std::optional<MeshAsset> after_mesh =
      service.compile_object_mesh(box.object.modeling_object_id);
  ASSERT_TRUE(after_mesh.has_value());
  const std::optional<SceneBounds> after_bounds =
      local_mesh_bounds(*after_mesh);
  ASSERT_TRUE(after_bounds.has_value());
  EXPECT_NEAR(after_bounds->min.x, -1.0f, 0.001f);
  EXPECT_NEAR(after_bounds->max.x, 3.0f, 0.001f);
  EXPECT_NEAR(after_bounds->min.y, -1.0f, 0.001f);
  EXPECT_NEAR(after_bounds->max.y, 1.0f, 0.001f);
  EXPECT_NEAR(after_bounds->min.z, -1.0f, 0.001f);
  EXPECT_NEAR(after_bounds->max.z, 1.0f, 0.001f);

  const MeshComponent *scene_mesh = scene.mesh(box.object.entity);
  ASSERT_NE(scene_mesh, nullptr);
  const MeshAsset *stored_mesh = assets.mesh(scene_mesh->mesh);
  ASSERT_NE(stored_mesh, nullptr);
  const std::optional<SceneBounds> stored_bounds =
      local_mesh_bounds(*stored_mesh);
  ASSERT_TRUE(stored_bounds.has_value());
  EXPECT_NEAR(stored_bounds->min.x, -1.0f, 0.001f);
  EXPECT_NEAR(stored_bounds->max.x, 3.0f, 0.001f);
}

TEST(ModelingSessionServiceTests,
     ComponentTransformSyncKeepsUntouchedMeshAssetRevision) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult edited = service.create_box_from_bounds({
      .bounds = {.min = {-3.0f, -1.0f, -1.0f}, .max = {-1.0f, 1.0f, 1.0f}},
      .name = "Edited Component Sync Box",
  });
  ASSERT_TRUE(edited.ok) << edited.message;
  const ModelingCreateBoxResult untouched = service.create_box_from_bounds({
      .bounds = {.min = {1.0f, -1.0f, -1.0f}, .max = {3.0f, 1.0f, 1.0f}},
      .name = "Untouched Component Sync Box",
  });
  ASSERT_TRUE(untouched.ok) << untouched.message;

  const MeshComponent *edited_mesh = scene.mesh(edited.object.entity);
  ASSERT_NE(edited_mesh, nullptr);
  const MeshComponent *untouched_mesh = scene.mesh(untouched.object.entity);
  ASSERT_NE(untouched_mesh, nullptr);
  const AssetId edited_mesh_id = edited_mesh->mesh;
  const AssetId untouched_mesh_id = untouched_mesh->mesh;
  const std::uint64_t edited_revision_before =
      assets.mesh_revision(edited_mesh_id).value;
  const std::uint64_t untouched_revision_before =
      assets.mesh_revision(untouched_mesh_id).value;

  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Face).ok);
  const ModelingSelectionSummary picked =
      service.pick_select(ModelingPickSelectRequest{
          .ray = {.origin = {-2.0f, 0.0f, 5.0f},
                  .direction = {0.0f, 0.0f, -1.0f}},
          .click_count = 1,
      });
  ASSERT_EQ(picked.active_object_id, edited.object.modeling_object_id);
  ASSERT_EQ(picked.selected_face_count, 1u);

  const std::optional<QVec3> selection_center = service.selection_center();
  ASSERT_TRUE(selection_center.has_value());
  ModelingTransformDragPreview begin = service.begin_transform_drag(
      test_x_move_drag_begin_request(*selection_center));
  ASSERT_TRUE(begin.ok) << begin.message;
  ASSERT_TRUE(begin.preview_driven);

  ModelingTransformDragPreview update =
      service.update_transform_drag(test_x_move_drag_update_request(2.0f));
  ASSERT_TRUE(update.ok) << update.message;
  EXPECT_TRUE(update.preview_driven);

  EXPECT_GT(assets.mesh_revision(edited_mesh_id).value, edited_revision_before);
  EXPECT_EQ(assets.mesh_revision(untouched_mesh_id).value,
            untouched_revision_before);

  ModelingTransformDragPreview commit = service.commit_transform_drag();
  ASSERT_TRUE(commit.ok) << commit.message;
  EXPECT_EQ(assets.mesh_revision(untouched_mesh_id).value,
            untouched_revision_before);
}

TEST(ModelingSessionServiceTests,
     TransformDragObjectSelectionUsesSceneTransformPreview) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Object Drag Box",
  });
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_EQ(service.selection_mode(), ModelingSelectionMode::Object);

  ModelingTransformDragPreview begin =
      service.begin_transform_drag(test_x_move_drag_begin_request());
  ASSERT_TRUE(begin.ok) << begin.message;
  EXPECT_TRUE(begin.active);
  EXPECT_FALSE(begin.preview_driven);
  EXPECT_TRUE(begin.scene_transform);
  EXPECT_TRUE(begin.has_scene_preview_transform);

  ModelingTransformDragPreview update =
      service.update_transform_drag(test_x_move_drag_update_request(2.0f));
  ASSERT_TRUE(update.ok) << update.message;
  EXPECT_FALSE(update.preview_driven);
  EXPECT_TRUE(update.scene_transform);
  EXPECT_TRUE(update.has_scene_preview_transform);
  EXPECT_NEAR(update.move_delta.x, 2.0f, 0.001f);
  EXPECT_NEAR(update.scene_preview_transform.origin.x, 2.0f, 0.001f);
  ASSERT_NE(scene.transform(box.object.entity), nullptr);
  EXPECT_NEAR(scene.transform(box.object.entity)->position.x, 2.0f, 0.001f);

  ModelingTransformDragPreview commit = service.commit_transform_drag();
  ASSERT_TRUE(commit.ok) << commit.message;
  EXPECT_FALSE(commit.preview_driven);
  EXPECT_TRUE(commit.changed);
  ASSERT_NE(scene.transform(box.object.entity), nullptr);
  EXPECT_NEAR(scene.transform(box.object.entity)->position.x, 2.0f, 0.001f);
  const ModelingTransformDragPreview after_commit_update =
      service.update_transform_drag(test_x_move_drag_update_request(3.0f));
  EXPECT_TRUE(after_commit_update.ok);
  EXPECT_FALSE(after_commit_update.active);
}

TEST(ModelingSessionServiceTests,
     ObjectTransformDragRefreshesViewportOverlayCacheKeyDuringScenePreview) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Object Drag Overlay Cache Box",
  });
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_EQ(service.selection_mode(), ModelingSelectionMode::Object);

  const ModelingSelectionOverlayOptions overlay_options{
      .suppress_persistent_object_selection = true,
  };
  auto make_viewport_overlay_key = [&]() {
    const ModelingSelectionSummary summary = service.selection_summary();
    return ViewportPaneOverlayCacheKey{
        .content_revision = summary.content_revision,
        .selection_revision = summary.selection_revision,
        .scene_revision = scene.revision(),
        .asset_revision = assets.revision(),
        .overlay_signature =
            service.selection_overlay_signature(overlay_options),
        .selection_mode = summary.mode,
    };
  };

  ViewportPaneOverlayCache cache;
  const ViewportPaneOverlayCacheKey before_key = make_viewport_overlay_key();
  cache.update(service.selection_overlay_frame(overlay_options), before_key);

  ModelingTransformDragPreview begin =
      service.begin_transform_drag(test_x_move_drag_begin_request());
  ASSERT_TRUE(begin.ok) << begin.message;
  ASSERT_TRUE(begin.scene_transform);

  ModelingTransformDragPreview update =
      service.update_transform_drag(test_x_move_drag_update_request(2.0f));
  ASSERT_TRUE(update.ok) << update.message;
  ASSERT_TRUE(update.scene_transform);
  ASSERT_NE(scene.transform(box.object.entity), nullptr);
  EXPECT_NEAR(scene.transform(box.object.entity)->position.x, 2.0f, 0.001f);

  const ViewportPaneOverlayCacheKey preview_key = make_viewport_overlay_key();
  EXPECT_EQ(cache.refresh_reason(preview_key),
            ViewportPaneOverlayCacheRefreshReason::SceneRevision);

  const ModelingCommandResult cancel = service.cancel_transform_drag();
  ASSERT_TRUE(cancel.ok) << cancel.message;
}

TEST(ModelingSessionServiceTests,
     PublicTransformDragObjectSelectionRecordsSceneTransformHistory) {
  ProjectSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_project_session(session));

  const QuaderApiResult<QuaderModelingObjectRef> box =
      editor.modeling().create_box({
          .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
          .name = "History Object Drag Box",
      });
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_EQ(session.modeling().selection_mode(), ModelingSelectionMode::Object);

  QuaderApiResult<QuaderTransformDragPreview> begin =
      editor.modeling().begin_transform_drag(
          test_api_x_move_drag_begin_request());
  ASSERT_TRUE(begin.ok) << begin.message;
  EXPECT_TRUE(begin.value.active);
  EXPECT_TRUE(begin.value.scene_transform);

  QuaderApiResult<QuaderTransformDragPreview> update =
      editor.modeling().update_transform_drag(
          test_api_x_move_drag_update_request(2.0f));
  ASSERT_TRUE(update.ok) << update.message;
  EXPECT_TRUE(update.value.has_scene_preview_transform);
  ASSERT_NE(session.scene().transform(box.value.entity), nullptr);
  EXPECT_NEAR(session.scene().transform(box.value.entity)->position.x, 2.0f,
              0.001f);

  QuaderApiResult<QuaderTransformDragPreview> commit =
      editor.modeling().commit_transform_drag();
  ASSERT_TRUE(commit.ok) << commit.message;
  EXPECT_TRUE(commit.changed);
  ASSERT_NE(session.scene().transform(box.value.entity), nullptr);
  EXPECT_NEAR(session.scene().transform(box.value.entity)->position.x, 2.0f,
              0.001f);
  EXPECT_TRUE(editor.history().can_undo());

  const QuaderApiVoidResult undo = editor.history().undo();
  ASSERT_TRUE(undo.ok) << undo.message;
  ASSERT_NE(session.scene().transform(box.value.entity), nullptr);
  EXPECT_NEAR(session.scene().transform(box.value.entity)->position.x, 0.0f,
              0.001f);
}

TEST(ModelingSessionServiceTests,
     PublicTransformDragFaceSelectionRecordsModelingHistory) {
  ProjectSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_project_session(session));

  const QuaderApiResult<QuaderModelingObjectRef> box =
      editor.modeling().create_box({
          .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
          .name = "History Face Drag Box",
      });
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Face).ok());

  QuaderPickSelectRequest pick_face;
  pick_face.ray = {
      .origin = {0.0f, 0.0f, 5.0f},
      .direction = {0.0f, 0.0f, -1.0f},
  };
  const QuaderApiResult<QuaderSelectionSummary> picked =
      editor.selection().pick_select(pick_face);
  ASSERT_TRUE(picked.ok) << picked.message;
  ASSERT_EQ(picked.value.selected_face_count, 1u);

  auto mesh_bounds = [&]() {
    const std::optional<MeshAsset> mesh =
        session.modeling().compile_object_mesh(box.value.modeling_object_id);
    EXPECT_TRUE(mesh.has_value());
    return mesh.has_value() ? local_mesh_bounds(*mesh)
                            : std::optional<SceneBounds>{};
  };
  std::optional<SceneBounds> bounds = mesh_bounds();
  ASSERT_TRUE(bounds.has_value());
  EXPECT_NEAR(bounds->max.x, 1.0f, 0.001f);

  auto drag_face_by = [&](float screen_x) {
    QuaderTransformDragBeginRequest begin_request =
        test_api_x_move_drag_begin_request();
    if (const std::optional<QVec3> center = editor.query().selection_center()) {
      begin_request.pivot = *center;
    }
    QuaderApiResult<QuaderTransformDragPreview> begin =
        editor.modeling().begin_transform_drag(begin_request);
    EXPECT_TRUE(begin.ok) << begin.message;
    EXPECT_TRUE(begin.value.active);
    EXPECT_TRUE(begin.value.preview_driven);

    QuaderApiResult<QuaderTransformDragPreview> update =
        editor.modeling().update_transform_drag(
            test_api_x_move_drag_update_request(screen_x));
    EXPECT_TRUE(update.ok) << update.message;
    EXPECT_TRUE(update.value.preview_driven);

    QuaderApiResult<QuaderTransformDragPreview> commit =
        editor.modeling().commit_transform_drag();
    EXPECT_TRUE(commit.ok) << commit.message;
    EXPECT_TRUE(commit.changed);
  };

  drag_face_by(2.0f);
  bounds = mesh_bounds();
  ASSERT_TRUE(bounds.has_value());
  EXPECT_NEAR(bounds->max.x, 3.0f, 0.001f);

  drag_face_by(2.0f);
  bounds = mesh_bounds();
  ASSERT_TRUE(bounds.has_value());
  EXPECT_NEAR(bounds->max.x, 5.0f, 0.001f);

  QuaderApiVoidResult undo = editor.history().undo();
  ASSERT_TRUE(undo.ok) << undo.message;
  EXPECT_TRUE(session.scene().exists(box.value.entity));
  bounds = mesh_bounds();
  ASSERT_TRUE(bounds.has_value());
  EXPECT_NEAR(bounds->max.x, 3.0f, 0.001f);

  undo = editor.history().undo();
  ASSERT_TRUE(undo.ok) << undo.message;
  EXPECT_TRUE(session.scene().exists(box.value.entity));
  bounds = mesh_bounds();
  ASSERT_TRUE(bounds.has_value());
  EXPECT_NEAR(bounds->max.x, 1.0f, 0.001f);
}

TEST(ModelingSessionServiceTests,
     TransformDragShiftDuplicateUsesScenePreviewForRotatedObject) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Rotated Shift Duplicate Source",
  });
  ASSERT_TRUE(box.ok) << box.message;
  TransformComponent source_transform;
  source_transform.rotation_euler = {0.0f, 0.5f, 0.25f};
  scene.set_transform(box.object.entity, source_transform);
  const MeshComponent *source_mesh = scene.mesh(box.object.entity);
  ASSERT_NE(source_mesh, nullptr);
  MeshComponent source_mesh_settings = *source_mesh;
  source_mesh_settings.double_sided = true;
  scene.set_mesh(box.object.entity, source_mesh_settings);

  ModelingTransformDragPreview begin =
      service.begin_transform_drag(test_x_move_drag_begin_request(
          {}, ModelingTransformOperation::Transform, true));
  ASSERT_TRUE(begin.ok) << begin.message;
  EXPECT_TRUE(begin.active);
  EXPECT_FALSE(begin.preview_driven);
  EXPECT_TRUE(begin.scene_transform);
  EXPECT_TRUE(begin.has_scene_preview_transform);
  ASSERT_EQ(scene.entities().size(), 2u);
  ASSERT_EQ(scene.selected_entities().size(), 1u);
  const EntityId duplicate_entity = scene.selected_entities().front();
  EXPECT_NE(duplicate_entity, box.object.entity);
  ASSERT_NE(scene.transform(duplicate_entity), nullptr);
  expect_vec3_near(scene.transform(duplicate_entity)->rotation_euler,
                   source_transform.rotation_euler);
  ASSERT_NE(scene.mesh(duplicate_entity), nullptr);
  EXPECT_TRUE(scene.mesh(duplicate_entity)->double_sided);

  ModelingTransformDragPreview update =
      service.update_transform_drag(test_x_move_drag_update_request(2.0f));
  ASSERT_TRUE(update.ok) << update.message;
  EXPECT_FALSE(update.preview_driven);
  EXPECT_TRUE(update.scene_transform);
  EXPECT_TRUE(update.has_scene_preview_transform);
  EXPECT_NEAR(update.move_delta.x, 2.0f, 0.001f);
  EXPECT_NEAR(update.scene_preview_transform.origin.x, 2.0f, 0.001f);

  const ModelingCommandResult cancel = service.cancel_transform_drag();
  ASSERT_TRUE(cancel.ok) << cancel.message;
  EXPECT_EQ(scene.entities().size(), 1u);
}

TEST(ModelingSessionServiceTests,
     TransformDragShiftDuplicateKeepsGroupedMeshInsideParent) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);
  EditorSelectionTestHarness editor_selection(scene, assets, service);

  const ModelingCreateBoxResult first = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Grouped Shift Source",
  });
  ASSERT_TRUE(first.ok) << first.message;
  const ModelingCreateBoxResult second = service.create_box_from_bounds({
      .bounds = {.min = {2.0f, -1.0f, -1.0f}, .max = {4.0f, 1.0f, 1.0f}},
      .name = "Grouped Sibling",
  });
  ASSERT_TRUE(second.ok) << second.message;

  const EntityId group = scene.create_empty("Parent Group");
  ASSERT_TRUE(scene.set_parent(first.object.entity, group, true));
  ASSERT_TRUE(scene.set_parent(second.object.entity, group, true));
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Object).ok);
  ASSERT_TRUE(select_through_editor(editor_selection.selection,
                                    {first.object.entity}, first.object.entity)
                  .ok);

  ModelingTransformDragPreview begin =
      service.begin_transform_drag(test_x_move_drag_begin_request(
          {}, ModelingTransformOperation::Transform, true));
  ASSERT_TRUE(begin.ok) << begin.message;
  EXPECT_TRUE(begin.active);
  EXPECT_FALSE(begin.preview_driven);
  EXPECT_TRUE(begin.scene_transform);
  ASSERT_EQ(scene.entities().size(), 4u);
  ASSERT_EQ(service.objects().size(), 3u);
  ASSERT_EQ(scene.selected_entities().size(), 1u);
  const EntityId duplicate_entity = scene.selected_entities().front();
  EXPECT_NE(duplicate_entity, first.object.entity);
  EXPECT_NE(duplicate_entity, second.object.entity);
  EXPECT_EQ(scene.parent(duplicate_entity), group);
  EXPECT_EQ(scene.children(group).size(), 3u);

  const ModelingCommandResult cancel = service.cancel_transform_drag();
  ASSERT_TRUE(cancel.ok) << cancel.message;
  EXPECT_EQ(scene.entities().size(), 3u);
  EXPECT_EQ(scene.children(group).size(), 2u);
}

TEST(ModelingSessionServiceTests,
     TransformDragComponentCancelRestoresMeshOverlayAndRuntime) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Drag Cancel Overlay Box",
  });
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Vertex).ok);
  ASSERT_TRUE(service.select_all().ok);

  ModelingTransformDragPreview begin =
      service.begin_transform_drag(test_x_move_drag_begin_request());
  ASSERT_TRUE(begin.ok) << begin.message;
  ASSERT_TRUE(begin.preview_driven);

  ModelingTransformDragPreview update =
      service.update_transform_drag(test_x_move_drag_update_request(2.0f));
  ASSERT_TRUE(update.ok) << update.message;
  std::optional<SceneBounds> selected_bounds =
      selected_world_bounds(scene, assets);
  ASSERT_TRUE(selected_bounds.has_value());
  EXPECT_NEAR(bounds_center(*selected_bounds).x, 2.0f, 0.001f);

  const ModelingCommandResult cancel = service.cancel_transform_drag();
  ASSERT_TRUE(cancel.ok) << cancel.message;
  selected_bounds = selected_world_bounds(scene, assets);
  ASSERT_TRUE(selected_bounds.has_value());
  EXPECT_NEAR(bounds_center(*selected_bounds).x, 0.0f, 0.001f);

  const ViewportOverlayFrame frame = service.selection_overlay_frame();
  std::optional<SceneBounds> point_bounds;
  for (const ViewportOverlayPoint &point : frame.points) {
    if (point.layer != ViewportOverlayLayer::SelectedVertex) {
      continue;
    }
    if (!point_bounds) {
      point_bounds = SceneBounds{.min = point.position, .max = point.position};
    } else {
      include_point(*point_bounds, point.position);
    }
  }

  ASSERT_TRUE(point_bounds.has_value());
  EXPECT_NEAR(bounds_center(*point_bounds).x, 0.0f, 0.001f);

  ModelingTransformDragPreview after_cancel_update =
      service.update_transform_drag(test_x_move_drag_update_request(3.0f));
  EXPECT_TRUE(after_cancel_update.ok);
  EXPECT_FALSE(after_cancel_update.active);
}

TEST(
    ModelingSessionServiceTests,
    TransformPreviewDuplicateTranslateRefreshesMeshAndKeepsDuplicateSelectable) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Shift Duplicate Source",
  });
  ASSERT_TRUE(box.ok) << box.message;
  TransformComponent source_transform;
  scene.set_transform(box.object.entity, source_transform);
  const MeshComponent *source_mesh = scene.mesh(box.object.entity);
  ASSERT_NE(source_mesh, nullptr);
  MeshComponent source_mesh_settings = *source_mesh;
  source_mesh_settings.double_sided = true;
  scene.set_mesh(box.object.entity, source_mesh_settings);
  ASSERT_EQ(scene.entities().size(), 1u);

  const ModelingCommandResult begin = service.begin_transform_preview(true);
  ASSERT_TRUE(begin.ok) << begin.message;
  ASSERT_TRUE(begin.changed);
  ASSERT_EQ(scene.entities().size(), 2u);
  ASSERT_EQ(service.objects().size(), 2u);
  ASSERT_EQ(scene.selected_entities().size(), 1u);

  const EntityId duplicate_entity = scene.selected_entities().front();
  EXPECT_NE(duplicate_entity, box.object.entity);
  ASSERT_NE(scene.transform(duplicate_entity), nullptr);
  expect_vec3_near(scene.transform(duplicate_entity)->position,
                   source_transform.position);
  expect_vec3_near(scene.transform(duplicate_entity)->rotation_euler,
                   source_transform.rotation_euler);
  ASSERT_NE(scene.mesh(duplicate_entity), nullptr);
  EXPECT_TRUE(scene.mesh(duplicate_entity)->double_sided);
  const std::optional<int> duplicate_modeling_object_id =
      service.modeling_object_for_entity(duplicate_entity);
  ASSERT_TRUE(duplicate_modeling_object_id.has_value());
  EXPECT_NE(*duplicate_modeling_object_id, box.object.modeling_object_id);

  const ModelingCommandResult first_move =
      service.update_transform_translate_preview({1.0f, 0.0f, 0.0f});
  ASSERT_TRUE(first_move.ok) << first_move.message;
  ASSERT_TRUE(first_move.changed);
  std::optional<SceneBounds> selected_bounds =
      selected_world_bounds(scene, assets);
  ASSERT_TRUE(selected_bounds.has_value());
  EXPECT_NEAR(bounds_center(*selected_bounds).x, 1.0f, 0.001f);

  const std::uint64_t revision_after_first_move = scene.revision();
  const ModelingCommandResult second_move =
      service.update_transform_translate_preview({3.0f, 0.0f, 0.0f});
  ASSERT_TRUE(second_move.ok) << second_move.message;
  ASSERT_TRUE(second_move.changed);
  EXPECT_GT(scene.revision(), revision_after_first_move);
  selected_bounds = selected_world_bounds(scene, assets);
  ASSERT_TRUE(selected_bounds.has_value());
  EXPECT_NEAR(bounds_center(*selected_bounds).x, 3.0f, 0.001f);
  ASSERT_NE(scene.transform(duplicate_entity), nullptr);
  expect_vec3_near(scene.transform(duplicate_entity)->rotation_euler,
                   source_transform.rotation_euler);
  ASSERT_NE(scene.mesh(duplicate_entity), nullptr);
  EXPECT_TRUE(scene.mesh(duplicate_entity)->double_sided);

  const ModelingCommandResult commit = service.commit_transform_preview();
  ASSERT_TRUE(commit.ok) << commit.message;
  ASSERT_TRUE(commit.changed);
  ASSERT_EQ(scene.selected_entities().size(), 1u);
  EXPECT_EQ(scene.selected_entities().front(), duplicate_entity);
  selected_bounds = selected_world_bounds(scene, assets);
  ASSERT_TRUE(selected_bounds.has_value());
  EXPECT_NEAR(bounds_center(*selected_bounds).x, 3.0f, 0.001f);

  ASSERT_TRUE(service.clear_selection().ok);
  const ModelingSelectionSummary picked =
      service.pick_select(ModelingPickSelectRequest{
          .ray =
              {
                  .origin = {3.0f, 0.0f, 5.0f},
                  .direction = {0.0f, 0.0f, -1.0f},
              },
          .click_count = 1,
      });
  EXPECT_EQ(picked.selected_object_count, 1u);
  EXPECT_EQ(picked.active_object_id, *duplicate_modeling_object_id);
  ASSERT_EQ(scene.selected_entities().size(), 1u);
  EXPECT_EQ(
      service.modeling_object_for_entity(scene.selected_entities().front()),
      duplicate_modeling_object_id);
}

TEST(ModelingSessionServiceTests,
     ShortcutCommandWrappersRefreshSceneAndHistory) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Shortcut Source",
  });
  ASSERT_TRUE(box.ok) << box.message;
  EXPECT_EQ(scene.entities().size(), 1u);

  ModelingCommandResult command = service.execute_instant_operation(
      ModelingInstantOperationId::DuplicateMeshes);
  ASSERT_TRUE(command.ok) << command.message;
  EXPECT_TRUE(command.changed);
  EXPECT_EQ(scene.entities().size(), 2u);

  command = service.repeat_last_action();
  ASSERT_TRUE(command.ok) << command.message;
  EXPECT_TRUE(command.changed);
  EXPECT_EQ(scene.entities().size(), 3u);

  command = service.copy_selection();
  ASSERT_TRUE(command.ok) << command.message;

  const ModelingSessionMemento before_paste = service.capture_history_memento();
  command = service.paste_selection();
  ASSERT_TRUE(command.ok) << command.message;
  EXPECT_TRUE(command.changed);
  EXPECT_EQ(scene.entities().size(), 4u);
  const ModelingSessionMemento after_paste = service.capture_history_memento();

  command = service.restore_history_memento(before_paste);
  ASSERT_TRUE(command.ok) << command.message;
  EXPECT_TRUE(command.changed);
  EXPECT_EQ(scene.entities().size(), 3u);

  command = service.restore_history_memento(after_paste);
  ASSERT_TRUE(command.ok) << command.message;
  EXPECT_TRUE(command.changed);
  EXPECT_EQ(scene.entities().size(), 4u);

  command = service.mirror_selection(ModelingMirrorAxis::X);
  ASSERT_TRUE(command.ok) << command.message;
  EXPECT_TRUE(command.changed);
  EXPECT_EQ(scene.entities().size(), 5u);

  const std::size_t entity_count_before_flip = scene.entities().size();
  command = service.flip_selection(ModelingFlipOrientation::Horizontal);
  ASSERT_TRUE(command.ok) << command.message;
  EXPECT_TRUE(command.changed);
  EXPECT_EQ(scene.entities().size(), entity_count_before_flip);

  command = service.apply_active_material(7u);
  ASSERT_TRUE(command.ok) << command.message;
  EXPECT_TRUE(command.changed);
}

TEST(ModelingSessionServiceTests,
     RepeatLastActionReplaysRecordedTransformPreview) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Repeat Move Source",
  });
  ASSERT_TRUE(box.ok) << box.message;

  ASSERT_TRUE(service.begin_transform_preview(false).ok);
  ASSERT_TRUE(
      service.update_transform_translate_preview({2.0f, 0.0f, 0.0f}).ok);
  ModelingCommandResult command = service.commit_transform_preview();
  ASSERT_TRUE(command.ok) << command.message;
  ASSERT_TRUE(command.changed);

  command = service.record_repeatable_transform({
      .active = true,
      .tool = ModelingTransformTool::Move,
      .operation = ModelingTransformOperation::Transform,
      .axis = ModelingTransformAxis::X,
      .move_delta = {2.0f, 0.0f, 0.0f},
  });
  ASSERT_TRUE(command.ok) << command.message;

  std::optional<QVec3> center = service.selection_center();
  ASSERT_TRUE(center.has_value());
  EXPECT_NEAR(center->x, 2.0f, 0.001f);

  command = service.repeat_last_action();
  ASSERT_TRUE(command.ok) << command.message;
  EXPECT_TRUE(command.changed);
  center = service.selection_center();
  ASSERT_TRUE(center.has_value());
  EXPECT_NEAR(center->x, 4.0f, 0.001f);
}

TEST(ModelingSessionServiceTests,
     RepeatLastActionReplaysRecordedExtrudePreview) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Repeat Extrude Source",
  });
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Face).ok);
  const ModelingSelectionSummary picked =
      service.pick_select(ModelingPickSelectRequest{
          .ray =
              {
                  .origin = {0.0f, 0.0f, 5.0f},
                  .direction = {0.0f, 0.0f, -1.0f},
              },
          .click_count = 1,
      });
  ASSERT_EQ(picked.selected_face_count, 1u);

  const std::optional<MeshAsset> before =
      service.compile_object_mesh(box.object.modeling_object_id);
  ASSERT_TRUE(before.has_value());

  ASSERT_TRUE(service.begin_transform_preview(false).ok);
  ASSERT_TRUE(
      service.update_transform_extrude_preview({0.0f, 0.0f, 1.0f}, 1.0f).ok);
  ModelingCommandResult command = service.commit_transform_preview();
  ASSERT_TRUE(command.ok) << command.message;
  ASSERT_TRUE(command.changed);

  command = service.record_repeatable_transform({
      .active = true,
      .tool = ModelingTransformTool::Move,
      .operation = ModelingTransformOperation::Extrude,
      .axis = ModelingTransformAxis::Z,
      .move_delta = {0.0f, 0.0f, 1.0f},
  });
  ASSERT_TRUE(command.ok) << command.message;

  const std::optional<MeshAsset> after_first =
      service.compile_object_mesh(box.object.modeling_object_id);
  ASSERT_TRUE(after_first.has_value());
  ASSERT_GT(after_first->indices.size(), before->indices.size());

  command = service.repeat_last_action();
  ASSERT_TRUE(command.ok) << command.message;
  EXPECT_TRUE(command.changed);

  const std::optional<MeshAsset> after_repeat =
      service.compile_object_mesh(box.object.modeling_object_id);
  ASSERT_TRUE(after_repeat.has_value());
  EXPECT_GT(after_repeat->indices.size(), after_first->indices.size());
}

TEST(ModelingSessionServiceTests,
     EditorSceneSelectionForcesEntityObjectModeFromComponentMode) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);
  EditorSelectionTestHarness editor_selection(scene, assets, service);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds =
          {
              .min = {-1.0f, -1.0f, -1.0f},
              .max = {1.0f, 1.0f, 1.0f},
          },
      .name = "Component Box",
  });
  ASSERT_TRUE(box.ok) << box.message;

  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Vertex).ok);
  EXPECT_EQ(service.selection_mode(), ModelingSelectionMode::Vertex);

  ASSERT_TRUE(
      select_through_editor(editor_selection.selection, {box.object.entity})
          .ok);

  EXPECT_EQ(service.selection_mode(), ModelingSelectionMode::Object);
  EXPECT_EQ(service.selection_summary().mode, ModelingSelectionMode::Object);
  EXPECT_EQ(service.selection_summary().active_object_id,
            box.object.modeling_object_id);
}

TEST(ModelingSessionServiceTests,
     SelectionModeSwitchPreservesSceneOnlySelection) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds =
          {
              .min = {-1.0f, -1.0f, -1.0f},
              .max = {1.0f, 1.0f, 1.0f},
          },
      .name = "Stale Box",
  });
  ASSERT_TRUE(box.ok) << box.message;

  const EntityId empty = scene.create_empty("Empty");
  scene.clear_selection();
  scene.set_selected(empty, true);

  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Object).ok);
  ASSERT_EQ(scene.selected_entities().size(), 1u);
  EXPECT_EQ(scene.selected_entities().front(), empty);
  EXPECT_EQ(scene.primary_selection(), empty);
}

TEST(ModelingSessionServiceTests,
     GroupSelectionSurvivesTransformToolActivation) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds =
          {
              .min = {-1.0f, -1.0f, -1.0f},
              .max = {1.0f, 1.0f, 1.0f},
          },
      .name = "Grouped Box",
  });
  ASSERT_TRUE(box.ok) << box.message;

  const EntityId group = scene.create_empty("Group");
  ASSERT_TRUE(scene.set_parent(box.object.entity, group, true));
  scene.clear_selection();
  scene.set_selected(group, true);
  scene.set_active_selection(group);
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Group).ok);

  ASSERT_TRUE(service.activate_viewport_tool(ModelingViewportTool::Move).ok);
  EXPECT_EQ(service.selection_mode(), ModelingSelectionMode::Group);
  ASSERT_EQ(scene.selected_entities().size(), 1u);
  EXPECT_EQ(scene.primary_selection(), group);
  EXPECT_EQ(scene.selected_entities().front(), group);

  ASSERT_TRUE(service.activate_viewport_tool(ModelingViewportTool::Rotate).ok);
  EXPECT_EQ(service.selection_mode(), ModelingSelectionMode::Group);
  ASSERT_EQ(scene.selected_entities().size(), 1u);
  EXPECT_EQ(scene.primary_selection(), group);
}

TEST(ModelingSessionServiceTests,
     GroupShiftDragDuplicateClonesHierarchyWithoutRootMeshStrays) {
  SceneDocument scene;
  AssetRegistry assets;
  EditorHistoryService history;
  DocumentCommandService commands(scene, history);
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult left = service.create_box_from_bounds({
      .bounds =
          {
              .min = {-1.0f, -1.0f, -1.0f},
              .max = {1.0f, 1.0f, 1.0f},
          },
      .name = "Left Grouped Box",
  });
  ASSERT_TRUE(left.ok) << left.message;
  const ModelingCreateBoxResult right = service.create_box_from_bounds({
      .bounds =
          {
              .min = {-0.5f, -0.5f, -0.5f},
              .max = {0.5f, 0.5f, 0.5f},
          },
      .name = "Right Grouped Box",
  });
  ASSERT_TRUE(right.ok) << right.message;

  TransformComponent left_transform;
  left_transform.position = {4.0f, 0.0f, 0.0f};
  left_transform.rotation_euler = {0.0f, 0.5f, 0.0f};
  scene.set_transform(left.object.entity, left_transform);
  TransformComponent right_transform;
  right_transform.position = {6.0f, 0.0f, 2.0f};
  right_transform.rotation_euler = {0.25f, 0.0f, 0.5f};
  scene.set_transform(right.object.entity, right_transform);

  ASSERT_TRUE(commands
                  .select_entities({left.object.entity, right.object.entity},
                                   left.object.entity)
                  .ok);
  const DocumentGroupSelectionResult grouped =
      commands.group_selection("Box Pair", {});
  ASSERT_TRUE(grouped.ok) << grouped.message;
  const EntityId group = grouped.group_entity;
  ASSERT_NE(group, kInvalidEntityId);
  ASSERT_EQ(scene.parent(left.object.entity), group);
  ASSERT_EQ(scene.parent(right.object.entity), group);
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Group).ok);

  const TransformComponent source_left_transform =
      *scene.transform(left.object.entity);
  const TransformComponent source_right_transform =
      *scene.transform(right.object.entity);
  const ModelingCommandResult duplicate =
      service.duplicate_selected_hierarchy_for_group_drag(commands);
  ASSERT_TRUE(duplicate.ok) << duplicate.message;
  ASSERT_TRUE(duplicate.changed);
  EXPECT_EQ(service.selection_mode(), ModelingSelectionMode::Group);
  ASSERT_EQ(scene.entities().size(), 6u);
  ASSERT_EQ(service.objects().size(), 4u);
  ASSERT_EQ(scene.selected_entities().size(), 1u);
  const EntityId duplicate_group = scene.selected_entities().front();
  ASSERT_NE(duplicate_group, group);
  EXPECT_EQ(scene.parent(duplicate_group), kInvalidEntityId);
  EXPECT_EQ(scene.modeling_object(duplicate_group), nullptr);

  const std::vector<EntityId> duplicate_children =
      scene.children(duplicate_group);
  ASSERT_EQ(duplicate_children.size(), 2u);
  ASSERT_NE(scene.modeling_object(duplicate_children[0]), nullptr);
  ASSERT_NE(scene.modeling_object(duplicate_children[1]), nullptr);
  EXPECT_EQ(scene.parent(duplicate_children[0]), duplicate_group);
  EXPECT_EQ(scene.parent(duplicate_children[1]), duplicate_group);
  ASSERT_NE(scene.transform(duplicate_children[0]), nullptr);
  ASSERT_NE(scene.transform(duplicate_children[1]), nullptr);
  expect_vec3_near(scene.transform(duplicate_children[0])->position,
                   source_left_transform.position);
  expect_vec3_near(scene.transform(duplicate_children[0])->rotation_euler,
                   source_left_transform.rotation_euler);
  expect_vec3_near(scene.transform(duplicate_children[1])->position,
                   source_right_transform.position);
  expect_vec3_near(scene.transform(duplicate_children[1])->rotation_euler,
                   source_right_transform.rotation_euler);

  std::size_t root_modeling_meshes = 0;
  for (const EntityId entity : scene.entities()) {
    if (scene.modeling_object(entity) != nullptr &&
        scene.parent(entity) == kInvalidEntityId) {
      ++root_modeling_meshes;
    }
  }
  EXPECT_EQ(root_modeling_meshes, 0u);
}

TEST(ModelingSessionServiceTests, VertexSelectionModeExposesOverlayPoints) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds =
          {
              .min = {-1.0f, -1.0f, -1.0f},
              .max = {1.0f, 1.0f, 1.0f},
          },
      .name = "Vertex Overlay Box",
  });
  ASSERT_TRUE(box.ok) << box.message;

  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Vertex).ok);
  const ViewportOverlayFrame frame = service.selection_overlay_frame();

  EXPECT_FALSE(frame.points.empty());
  const auto vertex_point = std::find_if(
      frame.points.begin(), frame.points.end(),
      [](const ViewportOverlayPoint &point) {
        return point.layer == ViewportOverlayLayer::UnselectedVertex ||
               point.layer == ViewportOverlayLayer::SelectedVertex;
      });
  ASSERT_NE(vertex_point, frame.points.end());
  EXPECT_EQ(vertex_point->color, rgba8(107, 230, 255, 255));
  EXPECT_GT(vertex_point->size_pixels, 0.0f);
}

TEST(ModelingSessionServiceTests,
     SwitchingSelectedObjectToVertexModeShowsHandlesImmediately) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);
  EditorSelectionTestHarness editor_selection(scene, assets, service);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds =
          {
              .min = {-1.0f, -1.0f, -1.0f},
              .max = {1.0f, 1.0f, 1.0f},
          },
      .name = "Mode Switch Overlay Box",
  });
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_TRUE(
      select_through_editor(editor_selection.selection, {box.object.entity})
          .ok);

  const ViewportOverlayFrame object_frame = service.selection_overlay_frame();
  ASSERT_FALSE(object_frame.lines.empty());

  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Vertex).ok);
  const ModelingSelectionSummary vertex_summary = service.selection_summary();
  EXPECT_EQ(vertex_summary.mode, ModelingSelectionMode::Vertex);
  EXPECT_EQ(vertex_summary.selected_object_count, 1u);
  const ViewportOverlayFrame vertex_frame = service.selection_overlay_frame();
  const auto vertex_point = std::find_if(
      vertex_frame.points.begin(), vertex_frame.points.end(),
      [](const ViewportOverlayPoint &point) {
        return point.layer == ViewportOverlayLayer::UnselectedVertex ||
               point.layer == ViewportOverlayLayer::SelectedVertex;
      });
  ASSERT_NE(vertex_point, vertex_frame.points.end());
  EXPECT_GT(vertex_point->size_pixels, 0.0f);
}

TEST(ModelingSessionServiceTests,
     SwitchingSelectedObjectToEdgeModeShowsHandlesImmediately) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);
  EditorSelectionTestHarness editor_selection(scene, assets, service);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds =
          {
              .min = {-1.0f, -1.0f, -1.0f},
              .max = {1.0f, 1.0f, 1.0f},
          },
      .name = "Edge Mode Switch Overlay Box",
  });
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_TRUE(
      select_through_editor(editor_selection.selection, {box.object.entity})
          .ok);

  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Edge).ok);
  const ModelingSelectionSummary edge_summary = service.selection_summary();
  EXPECT_EQ(edge_summary.mode, ModelingSelectionMode::Edge);
  EXPECT_EQ(edge_summary.selected_object_count, 1u);
  const ViewportOverlayFrame edge_frame = service.selection_overlay_frame();
  const auto edge_line = std::find_if(
      edge_frame.lines.begin(), edge_frame.lines.end(),
      [](const ViewportOverlayLine &line) {
        return line.layer == ViewportOverlayLayer::SourceWire ||
               line.layer == ViewportOverlayLayer::SelectedEdge ||
               line.layer == ViewportOverlayLayer::SelectedFaceEdge;
      });
  ASSERT_NE(edge_line, edge_frame.lines.end());
  EXPECT_GT(edge_line->width_pixels, 0.0f);
}

TEST(ModelingSessionServiceTests,
     ComponentModeSourceWireUsesActiveEditMeshOnly) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);
  EditorSelectionTestHarness editor_selection(scene, assets, service);

  const ModelingCreateBoxResult first = service.create_box_from_bounds({
      .bounds =
          {
              .min = {-1.0f, -1.0f, -1.0f},
              .max = {1.0f, 1.0f, 1.0f},
          },
      .name = "Inactive Component Source Box",
  });
  ASSERT_TRUE(first.ok) << first.message;
  const ModelingCreateBoxResult second = service.create_box_from_bounds({
      .bounds =
          {
              .min = {4.0f, -1.0f, -1.0f},
              .max = {6.0f, 1.0f, 1.0f},
          },
      .name = "Active Component Source Box",
  });
  ASSERT_TRUE(second.ok) << second.message;

  ASSERT_TRUE(select_through_editor(editor_selection.selection,
                                    {first.object.entity,
                                     second.object.entity},
                                    second.object.entity)
                  .ok);
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Edge).ok);
  const ModelingSelectionSummary summary = service.selection_summary();
  ASSERT_EQ(summary.active_object_id, second.object.modeling_object_id);

  const ViewportOverlayFrame frame = service.selection_overlay_frame();
  const auto source_wire_count_for_object = [&](int object_id) {
    return static_cast<std::size_t>(std::count_if(
        frame.lines.begin(), frame.lines.end(),
        [object_id](const ViewportOverlayLine &line) {
          return line.layer == ViewportOverlayLayer::SourceWire &&
                 line.element.object_id == object_id;
        }));
  };
  const auto source_stamp_count_for_object = [&](int object_id) {
    return static_cast<std::size_t>(std::count_if(
        frame.triangles.begin(), frame.triangles.end(),
        [object_id](const ViewportOverlayTriangle &triangle) {
          return triangle.layer == ViewportOverlayLayer::SourceWireDepthStamp &&
                 triangle.element.object_id == object_id;
        }));
  };

  EXPECT_EQ(source_wire_count_for_object(first.object.modeling_object_id), 0u);
  EXPECT_EQ(source_wire_count_for_object(second.object.modeling_object_id),
            12u);
  EXPECT_EQ(source_stamp_count_for_object(first.object.modeling_object_id), 0u);
  EXPECT_GT(source_stamp_count_for_object(second.object.modeling_object_id),
            0u);
}

TEST(ModelingSessionServiceTests,
     SwitchingSelectedObjectToFaceModeShowsHandlesImmediately) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);
  EditorSelectionTestHarness editor_selection(scene, assets, service);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds =
          {
              .min = {-1.0f, -1.0f, -1.0f},
              .max = {1.0f, 1.0f, 1.0f},
          },
      .name = "Face Mode Switch Overlay Box",
  });
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_TRUE(
      select_through_editor(editor_selection.selection, {box.object.entity})
          .ok);

  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Face).ok);
  const ModelingSelectionSummary face_summary = service.selection_summary();
  EXPECT_EQ(face_summary.mode, ModelingSelectionMode::Face);
  EXPECT_EQ(face_summary.selected_object_count, 1u);
  const ViewportOverlayFrame face_frame = service.selection_overlay_frame();
  const auto visible_primitive =
      !face_frame.triangles.empty() ||
      std::any_of(face_frame.lines.begin(), face_frame.lines.end(),
                  [](const ViewportOverlayLine &line) {
                    return line.layer == ViewportOverlayLayer::SourceWire ||
                           line.layer ==
                               ViewportOverlayLayer::SelectedFaceEdge ||
                           line.layer == ViewportOverlayLayer::SelectedEdge;
                  });
  EXPECT_TRUE(visible_primitive);
}

TEST(ModelingSessionServiceTests, ObjectSelectionOverlayUsesSelectionColors) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);
  EditorSelectionTestHarness editor_selection(scene, assets, service);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds =
          {
              .min = {-1.0f, -1.0f, -1.0f},
              .max = {1.0f, 1.0f, 1.0f},
          },
      .name = "Object Overlay Box",
  });
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_TRUE(
      select_through_editor(editor_selection.selection, {box.object.entity})
          .ok);

  const ViewportOverlayFrame frame = service.selection_overlay_frame();
  ASSERT_FALSE(frame.triangles.empty());
  ASSERT_FALSE(frame.lines.empty());

  const auto triangle = std::find_if(
      frame.triangles.begin(), frame.triangles.end(),
      [](const ViewportOverlayTriangle &value) {
        return value.layer == ViewportOverlayLayer::SelectedXrayFace ||
               value.layer == ViewportOverlayLayer::SelectedFaceMask ||
               value.layer == ViewportOverlayLayer::SelectedVisibleFace;
      });
  ASSERT_NE(triangle, frame.triangles.end());
  for (const ViewportOverlayTriangle &value : frame.triangles) {
    if (value.layer == ViewportOverlayLayer::SelectedXrayFace ||
        value.layer == ViewportOverlayLayer::SelectedFaceMask ||
        value.layer == ViewportOverlayLayer::SelectedVisibleFace) {
      EXPECT_EQ(value.color, rgba8(255, 172, 31, 8));
    }
  }

  const auto wire =
      std::find_if(frame.lines.begin(), frame.lines.end(),
                   [](const ViewportOverlayLine &value) {
                     return value.layer == ViewportOverlayLayer::SelectedEdge;
                   });
  ASSERT_NE(wire, frame.lines.end());
  EXPECT_EQ(wire->color, rgba8(255, 211, 31, 255));
}

TEST(ModelingSessionServiceTests,
     FacePickKeepsSceneEntitySelectedForInspectorAndGizmo) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds =
          {
              .min = {-1.0f, -1.0f, -1.0f},
              .max = {1.0f, 1.0f, 1.0f},
          },
      .name = "Face Pick Box",
  });
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Face).ok);

  const ModelingSelectionSummary summary =
      service.pick_select(ModelingPickSelectRequest{
          .ray =
              {
                  .origin = {0.0f, 0.0f, 5.0f},
                  .direction = {0.0f, 0.0f, -1.0f},
              },
          .click_count = 1,
      });

  EXPECT_EQ(summary.mode, ModelingSelectionMode::Face);
  EXPECT_TRUE(summary.has_selection);
  EXPECT_EQ(summary.active_kind, ModelingActiveSelectionKind::Face);
  EXPECT_EQ(summary.selected_face_count, 1u);
  EXPECT_EQ(scene.primary_selection(), box.object.entity);
  ASSERT_NE(scene.selection(box.object.entity), nullptr);
  EXPECT_TRUE(scene.selection(box.object.entity)->selected);
}

TEST(ModelingSessionServiceTests, SupportsRuntimeBoxPreviewLifecycle) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);
  const EntityId previous_selection = scene.create_entity("Previous Selection");
  scene.set_selected(previous_selection, true);
  EXPECT_EQ(scene.primary_selection(), previous_selection);
  const std::size_t scene_entity_count_before = scene.entities().size();
  const std::size_t mesh_count_before = assets.mesh_count();
  auto expect_preview_not_document_truth = [&]() {
    EXPECT_EQ(scene.entities().size(), scene_entity_count_before);
    EXPECT_EQ(assets.mesh_count(), mesh_count_before);
  };

  const ModelingBoxToolEvent start{
      .screen_position = {400.0f, 300.0f},
      .ray =
          {
              .origin = {4.0f, 4.0f, 4.0f},
              .direction = {0.0f, -1.0f, 0.0f},
          },
      .grid_size = 1.0,
  };
  const ModelingBoxToolEvent footprint_end{
      .screen_position = {400.0f, 300.0f},
      .ray =
          {
              .origin = {6.0f, 4.0f, 7.0f},
              .direction = {0.0f, -1.0f, 0.0f},
          },
      .grid_size = 1.0,
  };
  const ModelingBoxToolEvent height_end{
      .screen_position = {400.0f, 360.0f},
      .ray = start.ray,
      .grid_size = 1.0,
  };

  ASSERT_TRUE(service.activate_box_tool().ok);

  const ModelingBoxPreview hover = service.update_box_hover(start);
  EXPECT_TRUE(hover.ok);
  EXPECT_TRUE(hover.valid);
  EXPECT_EQ(hover.drag_state, ModelingBoxDragState::Idle);
  EXPECT_TRUE(service.box_preview().valid);
  EXPECT_EQ(service.box_preview().drag_state, ModelingBoxDragState::Idle);
  EXPECT_EQ(scene.primary_selection(), previous_selection);
  expect_preview_not_document_truth();

  const ModelingBoxPreview footprint_begin = service.begin_box_footprint(start);
  EXPECT_TRUE(footprint_begin.ok);
  EXPECT_EQ(footprint_begin.drag_state, ModelingBoxDragState::Footprint);
  EXPECT_EQ(service.box_preview().drag_state, ModelingBoxDragState::Footprint);
  EXPECT_TRUE(scene.selected_entities().empty());
  EXPECT_EQ(scene.primary_selection(), kInvalidEntityId);
  expect_preview_not_document_truth();

  const ModelingBoxPreview footprint_update =
      service.update_box_footprint(footprint_end);
  EXPECT_TRUE(footprint_update.ok);
  EXPECT_TRUE(footprint_update.valid);
  EXPECT_EQ(footprint_update.drag_state, ModelingBoxDragState::Footprint);
  EXPECT_TRUE(service.box_preview().valid);
  EXPECT_EQ(service.box_preview().drag_state, ModelingBoxDragState::Footprint);
  expect_preview_not_document_truth();

  const ModelingBoxPreview height_begin = service.begin_box_height(start);
  EXPECT_TRUE(height_begin.ok);
  EXPECT_EQ(height_begin.drag_state, ModelingBoxDragState::Height);
  EXPECT_EQ(service.box_preview().drag_state, ModelingBoxDragState::Height);
  expect_preview_not_document_truth();

  const ModelingBoxPreview height_update =
      service.update_box_height(height_end);
  EXPECT_TRUE(height_update.ok);
  EXPECT_TRUE(height_update.valid);
  EXPECT_EQ(height_update.drag_state, ModelingBoxDragState::Height);
  EXPECT_GT(height_update.distance, 0.0);
  EXPECT_GE(height_update.corner_count, 8u);
  EXPECT_EQ(service.box_preview().drag_state, ModelingBoxDragState::Height);
  EXPECT_GE(service.box_preview().corner_count, 8u);
  expect_preview_not_document_truth();

  const ModelingCreateBoxResult commit = service.commit_box("Preview Box");
  ASSERT_TRUE(commit.ok) << commit.message;
  EXPECT_TRUE(commit.changed);
  EXPECT_NE(commit.object.entity, kInvalidEntityId);
  EXPECT_EQ(service.box_preview().drag_state, ModelingBoxDragState::Idle);
  EXPECT_FALSE(service.box_preview().valid);
  EXPECT_EQ(scene.primary_selection(), commit.object.entity);
  EXPECT_EQ(scene.entities().size(), scene_entity_count_before + 1U);
  EXPECT_EQ(assets.mesh_count(), mesh_count_before + 1U);
}

TEST(MaterialDocumentTests,
     DefaultQuaderMaterialUsesDefaultMaterialTextureSlots) {
  const MaterialDocument material =
      make_default_quader_material("Default", 41, 42);
  EXPECT_TRUE(material.node_graph_reserved);
  EXPECT_FLOAT_EQ(material.base_color.x, 1.0f);
  EXPECT_FLOAT_EQ(material.base_color.y, 1.0f);
  EXPECT_FLOAT_EQ(material.base_color.z, 1.0f);
  EXPECT_FLOAT_EQ(material.metallic, 0.0f);
  EXPECT_FLOAT_EQ(material.roughness, 1.0f);
  ASSERT_TRUE(
      material.texture_slots.contains(material_texture_slot::kBaseColor));
  ASSERT_TRUE(
      material.texture_slots.contains(material_texture_slot::kRoughness));
  EXPECT_EQ(material.texture_slots.at(material_texture_slot::kBaseColor), 41u);
  EXPECT_EQ(material.texture_slots.at(material_texture_slot::kRoughness), 42u);
}

TEST(MaterialDocumentTests,
     MaterialTextureSlotPolicyOwnsAliasesAndSupportedSlots) {
  ASSERT_EQ(material_texture_slot_descriptors().size(), 2u);
  EXPECT_EQ(std::string_view(material_texture_slot_descriptors()[0].id),
            std::string_view(material_texture_slot::kBaseColor));
  EXPECT_EQ(
      std::string_view(material_texture_slot_descriptors()[0].legacy_alias),
      std::string_view("baseColor"));
  EXPECT_EQ(std::string_view(material_texture_slot_descriptors()[1].id),
            std::string_view(material_texture_slot::kRoughness));
  EXPECT_EQ(
      std::string_view(material_texture_slot_descriptors()[1].legacy_alias),
      std::string_view("roughness"));

  ASSERT_TRUE(canonical_material_texture_slot("baseColor").has_value());
  EXPECT_EQ(*canonical_material_texture_slot("baseColor"),
            material_texture_slot::kBaseColor);
  ASSERT_TRUE(canonical_material_texture_slot(material_texture_slot::kRoughness)
                  .has_value());
  EXPECT_EQ(*canonical_material_texture_slot(material_texture_slot::kRoughness),
            material_texture_slot::kRoughness);
  EXPECT_FALSE(canonical_material_texture_slot("normalTexture").has_value());

  MaterialDocument material;
  material.texture_slots["baseColor"] = 41u;
  material.texture_slots["roughness"] = 42u;
  material.texture_slots["normalTexture"] = 43u;
  normalize_material_texture_slots(material);

  ASSERT_TRUE(
      material.texture_slots.contains(material_texture_slot::kBaseColor));
  ASSERT_TRUE(
      material.texture_slots.contains(material_texture_slot::kRoughness));
  EXPECT_EQ(material.texture_slots.at(material_texture_slot::kBaseColor), 41u);
  EXPECT_EQ(material.texture_slots.at(material_texture_slot::kRoughness), 42u);
  EXPECT_FALSE(material.texture_slots.contains("baseColor"));
  EXPECT_FALSE(material.texture_slots.contains("roughness"));
  EXPECT_FALSE(material.texture_slots.contains("normalTexture"));
}

TEST(MaterialDocumentTests,
     MaterialTextureSlotPolicyOwnsApiQdrAndRenderSlotNormalization) {
  const std::string material_header =
      read_source_file("src/assets/material_document.hpp");
  const std::string material_source =
      read_source_file("src/assets/material_document.cpp");
  const std::string registry_header =
      read_source_file("src/assets/asset_registry.hpp");
  const std::string registry_source =
      read_source_file("src/assets/asset_registry.cpp");
  const std::string api_source =
      read_source_file("src/api/quader_asset_api.cpp");
  const std::string asset_command_source =
      read_source_file("src/editor/commands/replace_asset_command.hpp");
  const std::string document_command_source =
      read_source_file("src/editor/commands/document_command_service.cpp");
  const std::string qdr_reader_source =
      read_source_file("src/io/qdr/qdr_document_reader.cpp");
  const std::string qdr_writer_source =
      read_source_file("src/io/qdr/qdr_document_writer.cpp");
  const std::string texture_binding_source =
      read_source_file("src/render/texture_binding_service.cpp");
  const std::string render_signature_source =
      read_source_file("src/render/render_signature_builder.cpp");
  const std::size_t set_texture_start = api_source.find(
      "QuaderApiVoidResult QuaderAssetApi::set_material_texture");
  const std::size_t assign_material_start =
      api_source.find("QuaderAssetApi::assign_material_to_entity");
  const std::size_t assign_object_start =
      api_source.find("QuaderAssetApi::assign_material_to_object");
  ASSERT_NE(set_texture_start, std::string::npos);
  ASSERT_NE(assign_material_start, std::string::npos);
  ASSERT_NE(assign_object_start, std::string::npos);
  const std::string set_texture_source = api_source.substr(
      set_texture_start, assign_material_start - set_texture_start);
  const std::string assign_material_source = api_source.substr(
      assign_material_start, assign_object_start - assign_material_start);

  EXPECT_NE(material_header.find("struct MaterialTextureSlotDescriptor"),
            std::string::npos);
  EXPECT_NE(material_source.find("kMaterialTextureSlotDescriptors"),
            std::string::npos);
  EXPECT_NE(material_source.find("normalize_material_texture_slots"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      registry_header, "AssetRegistryMutationResult set_material_texture"));
  EXPECT_NE(registry_source.find("AssetRegistry::set_material_texture"),
            std::string::npos);
  EXPECT_NE(registry_source.find("canonical_material_texture_slot(slot)"),
            std::string::npos);
  EXPECT_NE(registry_source.find("validate_material(updated)"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(set_texture_source,
                                      ".asset_commands().set_material_texture("
                                      "material, std::move(slot), texture)"));
  EXPECT_NE(asset_command_source.find("assets().set_material_texture"),
            std::string::npos);
  EXPECT_EQ(set_texture_source.find(".assets().set_material_texture"),
            std::string::npos);
  EXPECT_NE(
      document_command_source.find("DocumentCommandService::set_material"),
      std::string::npos);
  EXPECT_NE(document_command_source.find("assets_->material(material)"),
            std::string::npos);
  EXPECT_EQ(set_texture_source.find("canonical_material_texture_slot"),
            std::string::npos);
  EXPECT_EQ(set_texture_source.find("set_material_texture_slot("),
            std::string::npos);
  EXPECT_EQ(set_texture_source.find(".texture(texture)"), std::string::npos);
  EXPECT_EQ(assign_material_source.find(".assets().material(material)"),
            std::string::npos);
  EXPECT_NE(qdr_reader_source.find("set_material_texture_slot(material, slot"),
            std::string::npos);
  EXPECT_NE(qdr_writer_source.find("material_texture_slot_descriptors()"),
            std::string::npos);
  EXPECT_NE(texture_binding_source.find(
                "material_texture_slot_asset(*material, slot_name)"),
            std::string::npos);
  EXPECT_NE(render_signature_source.find("material_texture_slot_descriptors()"),
            std::string::npos);
  EXPECT_EQ(texture_binding_source.find("alternateSlot"), std::string::npos);
  EXPECT_EQ(texture_binding_source.find("ends_with(\"Texture\")"),
            std::string::npos);
}

TEST(BakeServiceTests, ValidatesDescriptors) {
  const auto invalid = BakeService::validate({});
  EXPECT_FALSE(invalid.valid);

  const auto valid = BakeService::validate({
      .mesh = 42,
      .maps = {BakeMapType::Curvature, BakeMapType::Thickness},
      .width = 1024,
      .height = 1024,
  });
  EXPECT_TRUE(valid.valid);
}
