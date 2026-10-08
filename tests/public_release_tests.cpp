////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_command_ids.hpp"
#include "api/quader_editor_api.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_polygon_document_internal_access.hpp"
#include "core/localization_service.hpp"
#include "core/release_features.hpp"
#include "core/user_config_paths.hpp"
#include "editor/input/quader_input_binding.hpp"
#include "editor/modes/editor_mode_service.hpp"
#include "editor/session/project_session.hpp"
#include "editor/session/scene_session.hpp"
#include "io/project/project_manifest.hpp"
#include "io/qdr/qdr_document_reader.hpp"
#include "io/qdr/qdr_document_writer.hpp"
#include "scene/scene_transform.hpp"
#include "support/command_observable_state.hpp"
#include "ui/qt_main_menu.hpp"
#include "ui/qt_snapping_toolbar.hpp"
#include "ui/qt_tool_toolbar.hpp"
#include "viewport/viewport_settings.hpp"

#include <quader/modeling/neutral/polygon/document_topology.hpp>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <QAction>
#include <QApplication>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quader::test {
namespace {

/// Keeps public-profile fixtures isolated from each other and user data.
class PublicReleaseDirectory final {
public:
  PublicReleaseDirectory()
      : path_(std::filesystem::temp_directory_path() /
              ("quader-public-release-" + std::to_string(
                  std::chrono::steady_clock::now().time_since_epoch().count()))) {
    std::filesystem::create_directories(path_);
  }
  ~PublicReleaseDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }
  [[nodiscard]] const std::filesystem::path &path() const { return path_; }

private:
  std::filesystem::path path_;
};

[[nodiscard]] std::string read_text(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_text(const std::filesystem::path &path, std::string_view text) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  ASSERT_TRUE(output.is_open()) << path;
  output.write(text.data(), static_cast<std::streamsize>(text.size()));
  ASSERT_TRUE(output.good()) << path;
}

[[nodiscard]] QuaderInputChord single_chord(std::string_view text) {
  const auto parsed = quader_input_parse_shortcut(text);
  EXPECT_TRUE(parsed.has_value()) << text;
  if (!parsed || parsed->size() != 1u) {
    ADD_FAILURE() << "Expected one chord: " << text;
    return {};
  }
  return parsed->front();
}

void expect_unchanged(const QuaderEditorApi &editor,
                      const CommandObservableState &before) {
  const auto difference = diff_command_observable_state(
      before, capture_command_observable_state(editor));
  EXPECT_TRUE(difference.empty()) << difference.summary;
}

/// Independent expected public-release exclusions, including every command variant.
constexpr auto kExcludedCommands = std::to_array<QuaderCommandId>({
    quader_command::kEditCreateInstance,
    quader_command::kEditFlattenInstance,
    quader_command::kEditExitInstanceEdit,
    quader_command::kModelingBevelVertices,
    quader_command::kModelingBevelEdges,
    quader_command::kModelingSliceQuad,
    quader_command::kModelingThickenFaces,
    quader_command::kModelingCreateOuterHull,
    quader_command::kModelingHalfCut,
    quader_command::kModelingRadialAlign,
    quader_command::kModelingRadialAlignVertices,
    quader_command::kModelingRadialAlignEdges,
    quader_command::kModelingRadialAlignFaces,
    quader_command::kModelingSnapMeshesToFloor,
    quader_command::kModelingSnapVerticesToFloor,
    quader_command::kModelingBakeTransforms,
    quader_command::kModelingBakeLocation,
    quader_command::kModelingBakeRotation,
    quader_command::kModelingBakeScale,
    quader_command::kModelingSetOriginToCenter,
    quader_command::kModelingSetOriginToPivot,
    quader_command::kModelingMirrorSelectionX,
    quader_command::kModelingMirrorSelectionY,
    quader_command::kModelingMirrorSelectionZ,
    quader_command::kModelingFlipHorizontal,
    quader_command::kModelingFlipVertical,
    quader_command::kModelingUvPeel,
    quader_command::kToolsActivateMirror,
    quader_command::kToolsActivateCut,
    quader_command::kToolsActivatePoly,
    quader_command::kToolsActivateLinearArray,
    quader_command::kToolsActivateRadialArray,
    quader_command::kToolsActivatePathArray,
    quader_command::kToolsActivatePivot,
    quader_command::kToolsActivateCurve,
    quader_command::kToolsActivateCreateCurve,
    quader_command::kToolsActivateCreateTorus,
    quader_command::kToolsActivateCreateCone,
    quader_command::kToolsActivateExtrude,
    quader_command::kToolsActivateExtrudeIndividual,
    quader_command::kToolsActivateExtrudeAlongNormals,
});

[[nodiscard]] bool excluded_command(QuaderCommandId command) {
  const std::string_view id = command.string();
  return std::any_of(kExcludedCommands.begin(), kExcludedCommands.end(),
                     [id](QuaderCommandId excluded) { return excluded.string() == id; }) ||
         id.starts_with("modeling.texture.") ||
         id.starts_with("modeling.align.") ||
         id.starts_with("modeling.distribute.") ||
         id.starts_with("scene.layers.") ||
         id == "modeling.copy_pivot_from_active" ||
         id == "scene.create_point_light" ||
         id == "scene.create_directional_light" ||
         id == "viewport.toggle_mesh_grid" ||
         id == "viewport.shading.rendered" ||
         id == "viewport.toggle_texture_lock" ||
         id == "viewport.toggle_texture_lock_scale" ||
         id == "select.face.same_material";
}

TEST(PublicReleaseProfile, ExecutableIdentityCatalogAndSettingsArePublic01) {
  auto standalone = create_standalone_editor();
  const auto &editor = standalone.editor();
  const auto info = editor.release_info();
  EXPECT_EQ(info.version, kPublicRelease01);
  EXPECT_EQ(info.label, "0.1");
  EXPECT_FALSE(info.internal_full);
  for (const auto &feature : release_feature_catalog()) {
    SCOPED_TRACE(feature.id);
    EXPECT_EQ(editor.feature_available(feature.feature),
              feature.feature == ReleaseFeature::LassoSelection ||
              feature.feature == ReleaseFeature::PaintSelection);
    EXPECT_TRUE(product_release_has_feature(kInternalFullRelease, feature.feature));
  }
  ASSERT_NE(std::getenv("LOCALAPPDATA"), nullptr);
  EXPECT_EQ(quader_user_config_directory({}),
            std::filesystem::path(std::getenv("LOCALAPPDATA")) / "Quader" / "Public");
}

TEST(PublicReleaseProfile, ReleaseCompatibilityIsOrderedAndLegacyIsFullOnly) {
  const auto next = product_release(0u, 2u);
  EXPECT_TRUE(product_release_can_open(kPublicRelease01, kPublicRelease01));
  EXPECT_FALSE(product_release_can_open(kPublicRelease01, next));
  EXPECT_FALSE(product_release_can_open(kPublicRelease01, kInternalFullRelease));
  EXPECT_FALSE(product_release_can_open(kPublicRelease01, 0u));
  EXPECT_TRUE(product_release_can_open(next, kPublicRelease01));
  EXPECT_TRUE(product_release_can_open(next, next));
  EXPECT_FALSE(product_release_can_open(next, kInternalFullRelease));
  EXPECT_TRUE(product_release_can_open(kInternalFullRelease, kPublicRelease01));
  EXPECT_TRUE(product_release_can_open(kInternalFullRelease, next));
  EXPECT_TRUE(product_release_can_open(kInternalFullRelease, kInternalFullRelease));
}

TEST(PublicReleaseCommands, EveryExcludedCommandIsAbsentAndRejectsWithoutMutation) {
  auto standalone = create_standalone_editor();
  auto &editor = standalone.editor();
  const auto box = editor.modeling().create_box(
      {.min = {-1.f, -1.f, -1.f}, .max = {1.f, 1.f, 1.f}}, "Public Box");
  ASSERT_TRUE(box.ok()) << box.error_message();
  ASSERT_TRUE(editor.selection().select_object(box.value()).ok());
  const auto available = editor.commands().available();
  const auto catalog = editor.commands().keybinding_catalog();
  const auto bindings = editor.commands().keybindings();
  const auto before = capture_command_observable_state(editor);
  std::size_t count = 0u;
  for (const QuaderCommandId command : quader_command::kTypedCommandIds) {
    if (!excluded_command(command)) {
      continue;
    }
    ++count;
    SCOPED_TRACE(command.string());
    EXPECT_FALSE(editor.commands().descriptor(command).has_value());
    EXPECT_TRUE(editor.commands().shortcut_for(command).empty());
    EXPECT_FALSE(std::any_of(available.begin(), available.end(), [&](const auto &entry) {
      return entry.id == command.string();
    }));
    EXPECT_FALSE(std::any_of(catalog.begin(), catalog.end(), [&](const auto &entry) {
      return std::find(entry.target_ids.begin(), entry.target_ids.end(), command.string()) !=
             entry.target_ids.end();
    }));
    EXPECT_FALSE(std::any_of(bindings.begin(), bindings.end(), [&](const auto &entry) {
      return entry.target_id == command.string();
    }));
    const auto result = editor.commands().execute(command);
    EXPECT_FALSE(result.ok());
    EXPECT_FALSE(result.changed());
    EXPECT_FALSE(result.error_message().empty());
    expect_unchanged(editor, before);
  }
  EXPECT_GE(count, kExcludedCommands.size());
  for (const auto command : {quader_command::kToolsActivateKnife,
                             quader_command::kToolsActivateLassoSelect,
                             quader_command::kToolsActivatePaintSelect,
                             quader_command::kToolsActivateMove,
                             quader_command::kToolsActivateRotate,
                             quader_command::kToolsActivateScale,
                             quader_command::kModelingFlipFaceNormals}) {
    EXPECT_TRUE(editor.commands().descriptor(command).has_value()) << command.string();
  }
}

TEST(PublicReleaseCommands, OriginPlacementApisRejectWithoutMutation) {
  auto standalone = create_standalone_editor();
  auto &editor = standalone.editor();
  const auto box = editor.modeling().create_box(
      {.min = {0.f, 0.f, 0.f}, .max = {4.f, 2.f, 3.f}}, "Origin selection");
  ASSERT_TRUE(box.ok()) << box.error_message();
  ASSERT_TRUE(editor.selection().select_object(box.value()).ok());
  const auto before = capture_command_observable_state(editor);
  EXPECT_FALSE(editor.modeling().can_set_origin_to_center());
  EXPECT_FALSE(editor.modeling().can_set_origin_to_pivot());
  const auto centered = editor.modeling().set_origin_to_center();
  EXPECT_FALSE(centered.ok());
  EXPECT_FALSE(centered.changed());
  expect_unchanged(editor, before);
  const auto pivoted = editor.modeling().set_origin_to_pivot();
  EXPECT_FALSE(pivoted.ok());
  EXPECT_FALSE(pivoted.changed());
  expect_unchanged(editor, before);
}

TEST(PublicReleaseCommands, DisabledToolActivationPreservesActivePrimitivePreview) {
  auto standalone = create_standalone_editor();
  auto &editor = standalone.editor();
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::CreatePrimitive).ok());
  const auto before = capture_command_observable_state(editor);
  for (const auto tool : {"mirror", "cut", "poly", "linear_array",
                          "radial_array", "path_array", "curve", "create_curve",
                          "pivot", "uv_peel"}) {
    SCOPED_TRACE(tool);
    EXPECT_FALSE(editor.tools().can_activate(tool));
    const auto result = editor.tools().activate(tool);
    EXPECT_FALSE(result.ok());
    EXPECT_FALSE(result.changed());
    expect_unchanged(editor, before);
  }
}

TEST(PublicReleaseCommands, KeyboardNudgesHaveDefaultsAndExecuteThroughPublicProfile) {
  auto standalone = create_standalone_editor();
  auto &editor = standalone.editor();
  constexpr std::array<std::pair<QuaderCommandId, std::string_view>, 16> kCommands{{
      {quader_command::kViewportNudgeMoveUp, "Up"},
      {quader_command::kViewportNudgeMoveDown, "Down"},
      {quader_command::kViewportNudgeMoveLeft, "Left"},
      {quader_command::kViewportNudgeMoveRight, "Right"},
      {quader_command::kViewportNudgeRotateUp, "Alt+Up"},
      {quader_command::kViewportNudgeRotateDown, "Alt+Down"},
      {quader_command::kViewportNudgeRotateLeft, "Alt+Left"},
      {quader_command::kViewportNudgeRotateRight, "Alt+Right"},
      {quader_command::kViewportNudgeDuplicateOrExtrudeMoveUp, "Shift+Up"},
      {quader_command::kViewportNudgeDuplicateOrExtrudeMoveDown, "Shift+Down"},
      {quader_command::kViewportNudgeDuplicateOrExtrudeMoveLeft, "Shift+Left"},
      {quader_command::kViewportNudgeDuplicateOrExtrudeMoveRight, "Shift+Right"},
      {quader_command::kViewportNudgeDuplicateOrExtrudeRotateUp, "Shift+Alt+Up"},
      {quader_command::kViewportNudgeDuplicateOrExtrudeRotateDown, "Shift+Alt+Down"},
      {quader_command::kViewportNudgeDuplicateOrExtrudeRotateLeft, "Shift+Alt+Left"},
      {quader_command::kViewportNudgeDuplicateOrExtrudeRotateRight, "Shift+Alt+Right"}}};
  const auto box = editor.modeling().create_box(
      {.min = {-0.5F, -1.0F, -1.5F}, .max = {0.5F, 1.0F, 1.5F}}, "Nudge selection");
  ASSERT_TRUE(box.ok());
  ASSERT_TRUE(editor.selection().select_object(box.value()).ok());
  ASSERT_TRUE(editor.viewports().focus_pane(ViewportPaneId::Perspective).ok());
  ASSERT_TRUE(editor.viewports().set_view(ViewportPaneId::Perspective, ViewportView::Back).ok());
  ASSERT_TRUE(editor.viewports().set_control(QuaderViewportControl::SnappingEnabled, false).ok());
  const auto tool = editor.tools().active_tool();
  const auto catalog = editor.commands().keybinding_catalog();
  for (const auto &[command, shortcut] : kCommands) {
    SCOPED_TRACE(command.string());
    EXPECT_TRUE(editor.commands().descriptor(command).has_value());
    EXPECT_EQ(editor.commands().shortcut_for(command), shortcut);
    EXPECT_TRUE(std::ranges::any_of(catalog, [&](const auto &entry) {
      return std::ranges::find(entry.target_ids, command.string()) != entry.target_ids.end();
    }));
    const auto result = editor.commands().execute(command);
    ASSERT_TRUE(result.ok()) << result.error_message();
    EXPECT_TRUE(result.changed());
    EXPECT_EQ(editor.tools().active_tool(), tool);
  }
}

TEST(PublicReleaseCommands, ModifiedNudgeBindingExtrudesFacesAndUndoesThroughPublicProfile) {
  auto standalone = create_standalone_editor();
  auto &editor = standalone.editor();
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  quader_poly::Document document;
  const std::array vertices{
      quader_poly::add_vertex(document, {1, -1, -1}),
      quader_poly::add_vertex(document, {1, 1, -1}),
      quader_poly::add_vertex(document, {1, 1, 1}),
      quader_poly::add_vertex(document, {1, -1, 1})};
  const auto face = quader_poly::add_face(document, vertices);
  ASSERT_NE(face, quader_poly::kInvalidElementId);
  const auto object = editor.modeling().create_mesh(
      QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)),
      {.name = "Public Modified Nudge Face"});
  ASSERT_TRUE(object.ok()) << object.error_message();
  ASSERT_TRUE(editor.selection().select_object(object.value()).ok());
  ASSERT_TRUE(editor.mode().set(QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(object.value(), static_cast<QuaderElementId>(face)).ok());
  ASSERT_TRUE(editor.viewports().set_view(editor.viewports().focused_pane(), ViewportView::Back).ok());
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::Scale).ok());
  const auto before = capture_command_observable_state(editor);
  const auto mesh_before = editor.query().compiled_object_mesh(object.value());
  ASSERT_TRUE(mesh_before.has_value());
  const auto descriptor = editor.commands().descriptor(
      quader_command::kViewportNudgeDuplicateOrExtrudeMoveRight);
  ASSERT_TRUE(descriptor.has_value());
  ASSERT_TRUE(descriptor->enabled);
  const auto dispatch = editor.commands().dispatch_viewport_input_binding(
      single_chord("Shift+Right"), {.active_tool = ViewportToolId::Scale}, true, false);
  ASSERT_EQ(dispatch.kind, QuaderViewportKeybindingDispatch::Kind::Command);
  ASSERT_EQ(dispatch.command_id, quader_command::kViewportNudgeDuplicateOrExtrudeMoveRight.string());
  const auto extruded = editor.commands().execute(dispatch.command_id);
  ASSERT_TRUE(extruded.ok()) << extruded.error_message();
  ASSERT_TRUE(extruded.changed());
  const auto mesh_after = editor.query().compiled_object_mesh(object.value());
  ASSERT_TRUE(mesh_after.has_value());
  EXPECT_GT(mesh_after->face_textures.size(), mesh_before->face_textures.size());
  EXPECT_EQ(editor.selection().summary().mode, QuaderSelectionMode::Face);
  EXPECT_EQ(editor.selection().summary().selected_face_count, 1U);
  EXPECT_EQ(editor.tools().active_tool(), "scale");
  ASSERT_TRUE(editor.history().undo().ok());
  const auto restored = capture_command_observable_state(editor);
  for (const auto domain : {CommandObservableDomain::Scene, CommandObservableDomain::Geometry,
                            CommandObservableDomain::Selection, CommandObservableDomain::Overlay,
                            CommandObservableDomain::Tools}) {
    EXPECT_EQ(restored.domain(domain), before.domain(domain));
  }
}

TEST(PublicReleaseCommands, LassoSelectionIsAvailableThroughCommandsAndTools) {
  auto standalone = create_standalone_editor();
  auto &editor = standalone.editor();
  EXPECT_TRUE(editor.feature_available(ReleaseFeature::LassoSelection));
  EXPECT_TRUE(editor.tools().can_activate(ViewportToolId::LassoSelect));
  ASSERT_TRUE(editor.commands().execute(quader_command::kToolsActivateLassoSelect).ok());
  EXPECT_EQ(editor.tools().active_tool(), "lasso_select");
  EXPECT_EQ(editor.viewports().preferred_selection_tool(), ViewportToolId::LassoSelect);
  ASSERT_TRUE(editor.commands().execute(quader_command::kToolsActivateSelect).ok());
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::LassoSelect).ok());
  EXPECT_EQ(editor.tools().active_tool(), "lasso_select");
}

TEST(PublicReleaseCommands, PaintSelectionIsAvailableThroughCommandsAndTools) {
  auto standalone = create_standalone_editor();
  auto &editor = standalone.editor();
  EXPECT_TRUE(editor.feature_available(ReleaseFeature::PaintSelection));
  EXPECT_TRUE(editor.tools().can_activate(ViewportToolId::PaintSelect));
  ASSERT_TRUE(editor.commands().execute(quader_command::kToolsActivatePaintSelect).ok());
  EXPECT_EQ(editor.tools().active_tool(), "paint_select");
  EXPECT_EQ(editor.viewports().preferred_selection_tool(), ViewportToolId::PaintSelect);
  EXPECT_EQ(editor.commands().shortcut_for(quader_command::kToolsActivatePaintSelect), "Ctrl+Q");
  ASSERT_TRUE(editor.commands().execute(quader_command::kToolsActivateSelect).ok());
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::PaintSelect).ok());
  EXPECT_EQ(editor.tools().active_tool(), "paint_select");
}

TEST(PublicReleaseSelection, AltClickUsesTypedVertexAndFaceHits) {
  for (const auto mode : {QuaderSelectionMode::Vertex, QuaderSelectionMode::Face}) {
    for (const bool prior_selection : {false, true}) {
      SCOPED_TRACE(static_cast<int>(mode));
      SCOPED_TRACE(prior_selection);
      SceneSession session;
      QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
      quader_poly::Document document;
      std::array<std::array<QuaderElementId, 4>, 4> vertices{};
      std::array<std::array<QuaderElementId, 3>, 3> faces{};
      for (std::size_t row = 0; row < vertices.size(); ++row) {
        for (std::size_t column = 0; column < vertices[row].size(); ++column) {
          vertices[row][column] = quader_poly::add_vertex(document,
              {static_cast<float>(column) - 1.5f,
               static_cast<float>(row) - 1.5f, 0.0f});
        }
      }
      for (std::size_t row = 0; row < faces.size(); ++row) {
        for (std::size_t column = 0; column < faces[row].size(); ++column) {
          const std::array<quader_poly::ElementId, 4> boundary{
              static_cast<quader_poly::ElementId>(vertices[row][column]),
              static_cast<quader_poly::ElementId>(vertices[row][column + 1u]),
              static_cast<quader_poly::ElementId>(vertices[row + 1u][column + 1u]),
              static_cast<quader_poly::ElementId>(vertices[row + 1u][column])};
          faces[row][column] = quader_poly::add_face(document, boundary);
        }
      }
      const auto patch = editor.modeling().create_mesh(
          QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)),
          {.name = "Public directional component patch"});
      ASSERT_TRUE(patch.ok()) << patch.error_message();
      ASSERT_TRUE(editor.selection().select_object(patch.value()).ok());
      ASSERT_TRUE(session.editor_mode().set_mode(EditorMode::Edit).ok);
      ASSERT_TRUE(editor.selection().set_mode(mode).ok());
      ASSERT_TRUE(editor.selection().clear().ok());
      const auto unrelated = mode == QuaderSelectionMode::Vertex
          ? vertices[3][0] : faces[2][0];
      if (prior_selection) {
        const auto selected = mode == QuaderSelectionMode::Vertex
            ? editor.selection().select_vertex(patch.value(), unrelated)
            : editor.selection().select_face(patch.value(), unrelated);
        ASSERT_TRUE(selected.ok());
      }
      const auto selected_ids = [&] {
        std::vector<QuaderElementId> ids;
        const auto overlay = editor.query().selection_overlay();
        if (mode == QuaderSelectionMode::Vertex) {
          for (const auto &point : overlay.points) {
            if (point.layer == QuaderOverlayLayer::SelectedVertex &&
                point.element.object_id == patch.value().modeling_object_id) {
              ids.push_back(point.element.vertex_id);
            }
          }
        } else {
          for (const auto &triangle : overlay.triangles) {
            if ((triangle.layer == QuaderOverlayLayer::SelectedVisibleFace ||
                 triangle.layer == QuaderOverlayLayer::SelectedXrayFace) &&
                triangle.element.object_id == patch.value().modeling_object_id) {
              ids.push_back(triangle.element.face_id);
            }
          }
        }
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        return ids;
      };
      std::vector<QuaderElementId> expected;
      if (mode == QuaderSelectionMode::Vertex) {
        expected.assign(vertices[1].begin(), vertices[1].end());
      } else {
        for (const auto &row : faces) expected.push_back(row[1]);
      }
      std::sort(expected.begin(), expected.end());
      session.history().clear();
      const auto content_revision = editor.selection().summary().content_revision;
      QuaderPickSelectRequest request{
          .ray = {.origin = mode == QuaderSelectionMode::Vertex
                      ? QVec3{-0.49f, -0.5f, 5.0f} : QVec3{0.0f, -0.49f, 5.0f},
                  .direction = {0.0f, 0.0f, -1.0f}},
          .alt_pressed = true, .vertex_radius = 0.03, .edge_radius = 0.03};
      const auto picked = editor.selection().pick_select(request);
      ASSERT_TRUE(picked.ok()) << picked.error_message();
      EXPECT_TRUE(picked.changed());
      EXPECT_EQ(selected_ids(), expected);
      EXPECT_EQ(picked.value().mode, mode);
      EXPECT_EQ(picked.value().content_revision, content_revision);
      EXPECT_EQ(session.history().undo_count(), 0u);
      const auto repeated = editor.selection().pick_select(request);
      ASSERT_TRUE(repeated.ok());
      EXPECT_EQ(selected_ids(), expected);
      EXPECT_EQ(repeated.value().active_kind, picked.value().active_kind);
      EXPECT_EQ(repeated.value().active_vertex_id, picked.value().active_vertex_id);
      EXPECT_EQ(repeated.value().active_face_id, picked.value().active_face_id);
      EXPECT_FALSE(repeated.changed());
      EXPECT_EQ(repeated.value().selection_revision, picked.value().selection_revision);
      EXPECT_EQ(session.history().undo_count(), 0u);
      EXPECT_FALSE(editor.history().can_undo());
      request.shift_pressed = true;
      ASSERT_TRUE(editor.selection().pick_select(request).ok());
      EXPECT_TRUE(selected_ids().empty());

      // Ring/loop direction follows the selected neighbor while hits retain
      // their actual component kind, even at a face center far from an edge.
      const auto anchor = mode == QuaderSelectionMode::Vertex
          ? editor.selection().select_vertex(patch.value(), vertices[1][2])
          : editor.selection().select_face(patch.value(), faces[1][0]);
      ASSERT_TRUE(anchor.ok());
      request.ray.origin = mode == QuaderSelectionMode::Vertex
          ? QVec3{-0.5f, -0.5f, 5.0f} : QVec3{0.0f, 0.0f, 5.0f};
      request.shift_pressed = false;
      request.control_pressed = true;
      expected.clear();
      if (mode == QuaderSelectionMode::Vertex) {
        for (const auto &row : vertices) {
          expected.push_back(row[1]);
          expected.push_back(row[2]);
        }
      } else {
        expected.assign(faces[1].begin(), faces[1].end());
      }
      std::sort(expected.begin(), expected.end());
      const auto edit_context = session.editor_mode().snapshot();
      ASSERT_TRUE(editor.selection().pick_select(request).ok());
      EXPECT_EQ(selected_ids(), expected);
      EXPECT_EQ(session.editor_mode().snapshot().participants, edit_context.participants);
      EXPECT_EQ(session.editor_mode().snapshot().generation, edit_context.generation);
      EXPECT_EQ(session.history().undo_count(), 0u);
      EXPECT_EQ(editor.selection().summary().content_revision, content_revision);
    }
  }
}

TEST(PublicReleaseSelection, DoubleClicksUseTypedExpansionAndBoundedEdgeSelection) {
  for (const auto mode : {QuaderSelectionMode::Vertex, QuaderSelectionMode::Edge,
                          QuaderSelectionMode::Face}) {
    SCOPED_TRACE(static_cast<int>(mode));
    SceneSession session;
    QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
    quader_poly::Document document;
    std::array<std::array<quader_poly::ElementId, 6>, 4> vertices{};
    std::array<std::array<quader_poly::ElementId, 5>, 3> faces{};
    for (std::size_t row = 0; row < vertices.size(); ++row) {
      for (std::size_t column = 0; column < vertices[row].size(); ++column) {
        vertices[row][column] = quader_poly::add_vertex(document,
            {static_cast<float>(column), static_cast<float>(row), 0.0f});
      }
    }
    for (std::size_t row = 0; row < faces.size(); ++row) {
      for (std::size_t column = 0; column < faces[row].size(); ++column) {
        faces[row][column] = quader_poly::add_face(document, std::array{
            vertices[row][column], vertices[row][column + 1u],
            vertices[row + 1u][column + 1u], vertices[row + 1u][column]});
      }
    }
    const auto created = editor.modeling().create_mesh(
        QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)),
        {.name = "Public double-click selection"});
    ASSERT_TRUE(created.ok()) << created.error_message();
    ASSERT_TRUE(editor.mode().set(QuaderEditorMode::Edit).ok());
    ASSERT_TRUE(editor.selection().set_mode(mode).ok());
    ASSERT_TRUE(editor.selection().clear().ok());
    QuaderPickSelectRequest request{
        .ray = {.origin = {1.5f, 1.5f, 5.0f}, .direction = {0.0f, 0.0f, -1.0f}},
        .click_count = 2, .vertex_radius = 0.03, .edge_radius = 0.03};
    if (mode == QuaderSelectionMode::Edge) {
      ASSERT_TRUE(editor.selection().select_edge(created.value(), vertices[1][0], vertices[1][1]).ok());
      request.ray.origin = {3.5f, 1.0f, 5.0f};
      request.shift_pressed = true;
    } else if (mode == QuaderSelectionMode::Face) {
      ASSERT_TRUE(editor.selection().select_face(created.value(), faces[1][0]).ok());
    } else {
      request.ray.origin = {1.0f, 1.0f, 5.0f};
    }
    session.history().clear();
    const auto context = session.editor_mode().snapshot();
    const auto content_revision = editor.selection().summary().content_revision;
    const auto selected = editor.selection().pick_select(request);
    ASSERT_TRUE(selected.ok()) << selected.error_message();
    const auto overlay = editor.query().selection_overlay();
    std::vector<std::pair<QuaderElementId, QuaderElementId>> actual;
    std::vector<std::pair<QuaderElementId, QuaderElementId>> expected;
    if (mode == QuaderSelectionMode::Edge) {
      for (const auto &line : overlay.lines) {
        if (line.layer == QuaderOverlayLayer::SelectedEdge)
          actual.emplace_back(std::min(line.element.edge_a, line.element.edge_b),
                              std::max(line.element.edge_a, line.element.edge_b));
      }
      for (std::size_t column = 0; column < 4; ++column)
        expected.emplace_back(vertices[1][column], vertices[1][column + 1u]);
    } else if (mode == QuaderSelectionMode::Face) {
      for (const auto &triangle : overlay.triangles) {
        if (triangle.layer == QuaderOverlayLayer::SelectedVisibleFace ||
            triangle.layer == QuaderOverlayLayer::SelectedXrayFace)
          actual.emplace_back(triangle.element.face_id, triangle.element.face_id);
      }
      for (const auto face : faces[1]) expected.emplace_back(face, face);
    } else {
      for (const auto &point : overlay.points) {
        if (point.layer == QuaderOverlayLayer::SelectedVertex)
          actual.emplace_back(point.element.vertex_id, point.element.vertex_id);
      }
      for (const auto &row : vertices)
        for (const auto vertex : row) expected.emplace_back(vertex, vertex);
    }
    std::sort(actual.begin(), actual.end());
    actual.erase(std::unique(actual.begin(), actual.end()), actual.end());
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(actual, expected);
    EXPECT_EQ(session.editor_mode().snapshot().participants, context.participants);
    EXPECT_EQ(session.editor_mode().snapshot().generation, context.generation);
    EXPECT_EQ(editor.selection().summary().content_revision, content_revision);
    EXPECT_EQ(session.history().undo_count(), 0u);
  }
}

TEST(PublicReleaseSelection, ExplicitEditModeSupportsMultipleMeshesAndKeepsObjectActionsOut) {
  auto standalone = create_standalone_editor();
  auto &editor = standalone.editor();
  EXPECT_EQ(editor.mode().current(), QuaderEditorMode::Object);
  EXPECT_EQ(editor.commands().shortcut_for(quader_command::kEditorModeToggleEdit), "Tab");
  const auto first = editor.modeling().create_box(
      {.min = {-3, -1, -1}, .max = {-1, 1, 1}}, "First public edit participant");
  const auto second = editor.modeling().create_box(
      {.min = {1, -1, -1}, .max = {3, 1, 1}}, "Second public edit participant");
  ASSERT_TRUE(first.ok());
  ASSERT_TRUE(second.ok());
  ASSERT_TRUE(editor.selection().select_objects(
      {first.value().entity, second.value().entity}, first.value().entity).ok());
  ASSERT_TRUE(editor.commands().execute(quader_command::kEditorModeToggleEdit).ok());
  EXPECT_EQ(editor.mode().current(), QuaderEditorMode::Edit);
  ASSERT_EQ(editor.mode().state().participants.size(), 2U);
  for (const auto kind : {QuaderSelectionMode::Vertex, QuaderSelectionMode::Edge,
                          QuaderSelectionMode::Face}) {
    ASSERT_TRUE(editor.selection().set_mode(kind).ok());
    ASSERT_TRUE(editor.selection().clear().ok());
    EXPECT_EQ(editor.mode().state().participants.size(), 2U);
  }
  const auto before = capture_command_observable_state(editor);
  EXPECT_FALSE(editor.selection().set_mode(QuaderSelectionMode::Group).ok());
  EXPECT_FALSE(editor.modeling().create_box(
      {.min = {5, -1, -1}, .max = {7, 1, 1}}, "Rejected Edit creation").ok());
  expect_unchanged(editor, before);
  ASSERT_TRUE(editor.commands().execute(quader_command::kEditorModeToggleEdit).ok());
  EXPECT_EQ(editor.mode().current(), QuaderEditorMode::Object);
  EXPECT_EQ(editor.selection().summary().mode, QuaderSelectionMode::Object);
  EXPECT_EQ(editor.selection().summary().selected_object_count, 2U);
}

TEST(PublicReleaseSelection, ComponentModeConversionIgnoresDormantMeshPayload) {
  SceneSession session;
  QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
  const QuaderBounds bounds{.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}};
  const auto first = editor.modeling().create_box(bounds, "Public Conversion First");
  const auto second = editor.modeling().create_box(bounds, "Public Conversion Second");
  ASSERT_TRUE(first.ok());
  ASSERT_TRUE(second.ok());
  const auto ids = [&](QuaderModelingObjectRef target, bool vertices) {
    std::vector<int> selected;
    const auto overlay = editor.query().selection_overlay();
    for (const auto &point : overlay.points) {
      if (vertices && point.layer == QuaderOverlayLayer::SelectedVertex &&
          point.element.object_id == target.modeling_object_id) {
        selected.push_back(point.element.vertex_id);
      }
    }
    for (const auto &triangle : overlay.triangles) {
      if (!vertices && triangle.element.object_id == target.modeling_object_id &&
          (triangle.layer == QuaderOverlayLayer::SelectedVisibleFace ||
           triangle.layer == QuaderOverlayLayer::SelectedXrayFace)) {
        selected.push_back(triangle.element.face_id);
      }
    }
    std::sort(selected.begin(), selected.end());
    selected.erase(std::unique(selected.begin(), selected.end()), selected.end());
    return selected;
  };
  ASSERT_TRUE(editor.selection().select_objects(
      {first.value().entity, second.value().entity}, first.value().entity).ok());
  const auto undo_count_before_entry = session.history().undo_count();
  ASSERT_TRUE(editor.mode().set(QuaderEditorMode::Edit).ok());
  EXPECT_EQ(session.history().undo_count(), undo_count_before_entry);
  session.history().clear();
  ASSERT_TRUE(editor.selection().select_vertices(first.value(), {1, 2, 3, 4}).ok());
  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Face).ok());
  EXPECT_EQ(ids(first.value(), false), (std::vector<int>{1}));
  ASSERT_TRUE(editor.selection().select_face(second.value(), 1).ok());
  ASSERT_TRUE(editor.selection().select_face(first.value(), 1).ok());
  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Vertex).ok());
  ASSERT_TRUE(editor.selection().select_vertex(second.value(), 5, QuaderSelectionEdit::Add).ok());
  EXPECT_EQ(ids(first.value(), true), (std::vector<int>{1, 2, 3, 4}));
  EXPECT_EQ(ids(second.value(), true), (std::vector<int>{5}));
  EXPECT_EQ(editor.selection().summary().selected_vertex_count, 5u);
  EXPECT_EQ(session.history().undo_count(), 0u);
}

TEST(PublicReleaseCommands, ParentingCommandsArePublicAndSupportMeshesAndGroups) {
  SceneSession session;
  QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto catalog = editor.commands().keybinding_catalog();
  for (const auto command : {quader_command::kParentToActive,
                             quader_command::kUnparentSelection}) {
    EXPECT_TRUE(editor.commands().descriptor(command).has_value());
    EXPECT_TRUE(std::any_of(catalog.begin(), catalog.end(), [&](const auto &entry) {
      return std::find(entry.target_ids.begin(), entry.target_ids.end(), command.string()) !=
             entry.target_ids.end();
    }));
  }
  EXPECT_EQ(editor.commands().shortcut_for(quader_command::kParentToActive), "P");
  EXPECT_EQ(editor.commands().shortcut_for(quader_command::kUnparentSelection), "Ctrl+P");
  const auto child = editor.modeling().create_box(
      {.min = {-1.f, -1.f, -1.f}, .max = {1.f, 1.f, 1.f}}, "Child");
  const auto parent = editor.modeling().create_box(
      {.min = {3.f, 0.f, 0.f}, .max = {5.f, 2.f, 2.f}}, "Parent");
  const auto first_group = editor.scene().create_empty();
  const auto second_group = editor.scene().create_empty();
  ASSERT_TRUE(child.ok());
  ASSERT_TRUE(parent.ok());
  ASSERT_TRUE(first_group.ok());
  ASSERT_TRUE(second_group.ok());
  ASSERT_TRUE(editor.scene().set_transform(parent.value().entity, {
      .position = {3.f, -2.f, 1.f}, .rotation_euler = {0.3f, 0.5f, -0.4f},
      .scale = {2.f, 0.5f, -1.f}}).ok());
  ASSERT_TRUE(editor.selection().select_objects(
      {child.value().entity, first_group.value().id, parent.value().entity},
      parent.value().entity).ok());
  const auto child_world = session.scene().world_transform_matrix(child.value().entity);
  const auto parented = editor.commands().execute(quader_command::kParentToActive);
  ASSERT_TRUE(parented.ok()) << parented.error_message();
  EXPECT_TRUE(parented.changed());
  EXPECT_EQ(session.scene().parent(child.value().entity), parent.value().entity);
  EXPECT_EQ(session.scene().parent(first_group.value().id), parent.value().entity);
  EXPECT_EQ(session.scene().primary_selection(), parent.value().entity);
  EXPECT_TRUE(scene_matrix_near_equal(
      session.scene().world_transform_matrix(child.value().entity), child_world));
  ASSERT_TRUE(editor.commands().execute(quader_command::kUnparentSelection).ok());
  EXPECT_EQ(session.scene().parent(child.value().entity), kInvalidEntityId);
  EXPECT_EQ(session.scene().parent(first_group.value().id), kInvalidEntityId);
  EXPECT_TRUE(scene_matrix_near_equal(
      session.scene().world_transform_matrix(child.value().entity), child_world));

  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Group).ok());
  ASSERT_TRUE(editor.selection().select_objects(
      {first_group.value().id, second_group.value().id}, second_group.value().id).ok());
  ASSERT_TRUE(editor.commands().execute(quader_command::kParentToActive).ok());
  EXPECT_EQ(session.scene().parent(first_group.value().id), second_group.value().id);
  ASSERT_TRUE(editor.commands().execute(quader_command::kUnparentSelection).ok());
  EXPECT_EQ(session.scene().parent(first_group.value().id), kInvalidEntityId);
}

TEST(PublicReleaseCommands, ParentingRejectsRestrictedParticipantsAtomically) {
  for (const EntityKind restricted :
       {EntityKind::Curve, EntityKind::Light, EntityKind::Instance}) {
    SCOPED_TRACE(static_cast<int>(restricted));
    SceneSession session;
    QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
    const auto box = editor.modeling().create_box(
        {.min = {-1.f, -1.f, -1.f}, .max = {1.f, 1.f, 1.f}}, "Allowed Mesh");
    const auto parent = editor.scene().create_empty();
    ASSERT_TRUE(box.ok());
    ASSERT_TRUE(parent.ok());
    // Seed retained unsupported scene data below the release-facing creation API.
    const EntityId hidden = session.scene().create_entity("Unavailable", restricted);
    ASSERT_NE(hidden, kInvalidEntityId);
    ASSERT_TRUE(editor.selection().select_objects_for_inspection(
        {box.value().entity, hidden, parent.value().id}, parent.value().id).ok());
    const auto before_parent = capture_command_observable_state(editor);
    const auto rejected_parent = editor.scene().parent_selection_to_active();
    EXPECT_FALSE(rejected_parent.ok());
    EXPECT_FALSE(rejected_parent.changed());
    expect_unchanged(editor, before_parent);
    const std::array moved{box.value().entity, hidden};
    EXPECT_FALSE(editor.scene().move_entities(moved, parent.value().id).ok());
    expect_unchanged(editor, before_parent);

    ASSERT_TRUE(session.scene().set_parent(box.value().entity, parent.value().id, false));
    ASSERT_TRUE(session.scene().set_parent(hidden, parent.value().id, false));
    ASSERT_TRUE(editor.selection().select_objects_for_inspection(
        {box.value().entity, hidden}, box.value().entity).ok());
    const auto before_unparent = capture_command_observable_state(editor);
    const auto rejected_unparent = editor.scene().unparent_selection();
    EXPECT_FALSE(rejected_unparent.ok());
    EXPECT_FALSE(rejected_unparent.changed());
    expect_unchanged(editor, before_unparent);
    EXPECT_EQ(session.scene().parent(box.value().entity), parent.value().id);
    EXPECT_EQ(session.scene().parent(hidden), parent.value().id);
  }
}

TEST(PublicReleaseCommands, ParentingOverridesAndExplicitUnassignmentSurviveReload) {
  PublicReleaseDirectory directory;
  const auto path = directory.path() / "parenting-keybindings.json";
  write_text(path, R"([
    {"command":"edit.parent_to_active","key":"Ctrl+Alt+F24"},
    {"command":"-edit.unparent","key":""}
  ])");
  {
    auto standalone = create_standalone_editor();
    auto &commands = standalone.editor().commands();
    const auto loaded = commands.set_keybinding_storage_path(path);
    ASSERT_TRUE(loaded.ok()) << loaded.error_message();
    EXPECT_EQ(commands.shortcut_for(quader_command::kParentToActive), "Ctrl+Alt+F24");
    EXPECT_TRUE(commands.shortcut_for(quader_command::kUnparentSelection).empty());
    ASSERT_TRUE(commands.set_shortcut(quader_command::kToolsActivateMove,
                                      "Ctrl+Alt+F23").ok());
  }
  auto reloaded = create_standalone_editor();
  auto &commands = reloaded.editor().commands();
  const auto loaded = commands.set_keybinding_storage_path(path);
  ASSERT_TRUE(loaded.ok()) << loaded.error_message();
  EXPECT_EQ(commands.shortcut_for(quader_command::kParentToActive), "Ctrl+Alt+F24");
  EXPECT_TRUE(commands.shortcut_for(quader_command::kUnparentSelection).empty());
}

TEST(PublicReleaseCommands, DisabledHeldInputDoesNotConsumeQuickPivotOrMaterialTransfer) {
  auto standalone = create_standalone_editor();
  auto &editor = standalone.editor();
  const auto box = editor.modeling().create_box(
      {.min = {-1.f, -1.f, -1.f}, .max = {1.f, 1.f, 1.f}}, "Gesture Box");
  ASSERT_TRUE(box.ok());
  ASSERT_TRUE(editor.mode().set(QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(editor.selection().select_face(box.value(), 1u).ok());
  const auto before = capture_command_observable_state(editor);
  ViewportToolState state;
  state.active_tool = ViewportToolId::Move;
  state.selection_mode = ViewportSelectionMode::Face;
  for (const auto input : {"D", "Shift+RightMouseClick", "Shift+RightMouseDoubleClick",
                           "Ctrl+RightMouseClick", "Ctrl+RightMouseDoubleClick"}) {
    SCOPED_TRACE(input);
    ViewportInputActionKind action = ViewportInputActionKind::None;
    EXPECT_FALSE(editor.commands().resolve_viewport_input_action(
        single_chord(input), state, true, false, action, {.viewport_hovered = true}));
  }
  expect_unchanged(editor, before);
}

TEST(PublicReleaseCommands, DormantShortcutOverridesSurviveEditsReloadAndResetAll) {
  PublicReleaseDirectory directory;
  const auto path = directory.path() / "keybindings.json";
  write_text(path, R"([
    {"command":"tools.activate.cut","key":"Ctrl+Alt+F23"},
    {"command":"viewport.quick_pivot","key":"Ctrl+Alt+F22"}
  ])");
  const auto contains_dormant_bindings = [&] {
    const auto json = nlohmann::json::parse(read_text(path));
    for (const auto id : {"tools.activate.cut", "viewport.quick_pivot"}) {
      EXPECT_TRUE(std::any_of(json.begin(), json.end(), [id](const auto &entry) {
        return entry.value("command", std::string{}) == id;
      })) << id;
    }
  };
  {
    auto standalone = create_standalone_editor();
    auto &editor = standalone.editor();
    ASSERT_TRUE(editor.commands().set_keybinding_storage_path(path).ok());
    EXPECT_TRUE(editor.commands().shortcut_for("tools.activate.cut").empty());
    EXPECT_TRUE(editor.commands().conflicts_for_shortcut("Ctrl+Alt+F23").empty());
    ViewportToolState state;
    EXPECT_EQ(editor.commands().dispatch_viewport_input_binding(
                  single_chord("Ctrl+Alt+F23"), state, true, false).kind,
              QuaderViewportKeybindingDispatch::Kind::NoMatch);
    ASSERT_TRUE(editor.commands().set_shortcut(
        quader_command::kToolsActivateMove, "Ctrl+Alt+F23").ok());
    contains_dormant_bindings();
    const auto dispatched = editor.commands().dispatch_viewport_input_binding(
        single_chord("Ctrl+Alt+F23"), state, true, false);
    ASSERT_EQ(dispatched.kind,
              QuaderViewportKeybindingDispatch::Kind::ViewportShortcut);
    EXPECT_EQ(dispatched.shortcut.kind, ViewportShortcutKind::ActivateTool);
    EXPECT_EQ(dispatched.shortcut.tool, ViewportToolId::Move);
    ASSERT_TRUE(editor.commands().reset_all_shortcuts().ok());
    contains_dormant_bindings();
  }
  auto reloaded = create_standalone_editor();
  ASSERT_TRUE(reloaded.editor().commands().set_keybinding_storage_path(path).ok());
  contains_dormant_bindings();
  ViewportToolState state;
  for (const auto shortcut : {"Ctrl+Alt+F23", "Ctrl+Alt+F22"}) {
    EXPECT_EQ(reloaded.editor().commands().dispatch_viewport_input_binding(
                  single_chord(shortcut), state, true, false).kind,
              QuaderViewportKeybindingDispatch::Kind::NoMatch);
  }
}

[[nodiscard]] ProjectRelativePath scene_path(std::string_view text) {
  auto parsed = ProjectRelativePath::parse_portable(text);
  EXPECT_TRUE(parsed.ok());
  return parsed.take_value();
}

[[nodiscard]] std::map<std::filesystem::path, std::string>
directory_contents(const std::filesystem::path &root) {
  std::map<std::filesystem::path, std::string> result;
  for (const auto &entry : std::filesystem::recursive_directory_iterator(root)) {
    if (entry.is_regular_file()) {
      result.emplace(entry.path().lexically_relative(root), read_text(entry.path()));
    }
  }
  return result;
}

TEST(PublicReleasePersistence, PublicAuthoringSavesAndReopensTaggedSceneAndProject) {
  PublicReleaseDirectory directory;
  const auto project_root = directory.path() / "PublicProject";
  std::filesystem::create_directory(project_root);
  std::filesystem::path manifest_path;
  const auto relative = scene_path("Scenes/Model.qdr");
  {
    ProjectSession project;
    QuaderEditorApi editor(project);
    const auto created = editor.project().create(project_root);
    ASSERT_TRUE(created.ok()) << created.error_message();
    const auto box = editor.modeling().create_box(
        {.min = {-2.f, -2.f, -2.f}, .max = {2.f, 2.f, 2.f}}, "Saved Public Box");
    ASSERT_TRUE(box.ok()) << box.error_message();
    const auto id = editor.scene_documents().active_scene_id();
    ASSERT_TRUE(id.has_value());
    const auto saved = editor.scene_documents().save_as(*id, relative);
    ASSERT_TRUE(saved.ok()) << saved.error_message();
    manifest_path = editor.project().manifest_path();
    const auto manifest = ProjectManifestStore::deserialize(read_text(manifest_path));
    ASSERT_TRUE(manifest.ok()) << manifest.error().message;
    EXPECT_EQ(manifest.value().minimum_product_release(), kPublicRelease01);
    const auto read = QdrDocumentReader::read_project_document(
        project_root / relative.native_path(), kPublicRelease01);
    ASSERT_TRUE(read.ok) << read.message;
    EXPECT_EQ(read.minimum_product_release, kPublicRelease01);
    ASSERT_TRUE(editor.project().close().ok());
  }
  ProjectSession reopened_project;
  QuaderEditorApi reopened(reopened_project);
  const auto opened = reopened.project().open(manifest_path);
  ASSERT_TRUE(opened.ok()) << opened.error_message();
  const auto entities = reopened.query().entities();
  EXPECT_TRUE(std::any_of(entities.begin(), entities.end(), [](const auto &entity) {
    return entity.name == "Saved Public Box";
  }));
}

TEST(PublicReleasePersistence, RejectedSceneRequirementsPreserveLiveStateAndFiles) {
  PublicReleaseDirectory directory;
  const auto root = directory.path() / "WorkingProject";
  std::filesystem::create_directory(root);
  ProjectSession project;
  QuaderEditorApi editor(project);
  ASSERT_TRUE(editor.project().create(root).ok());
  const auto box = editor.modeling().create_box(
      {.min = {-1.f, -1.f, -1.f}, .max = {1.f, 1.f, 1.f}}, "Existing Box");
  ASSERT_TRUE(box.ok());
  const auto active = editor.scene_documents().active_scene_id();
  ASSERT_TRUE(active.has_value());
  ASSERT_TRUE(editor.scene_documents().save_as(*active, scene_path("Scenes/Existing.qdr")).ok());
  const auto public_read = QdrDocumentReader::read_project_document(root / "Scenes/Existing.qdr");
  ASSERT_TRUE(public_read.ok) << public_read.message;
  auto snapshot = qdr_project_write_snapshot_from_document(public_read.document);
  ASSERT_TRUE(snapshot.ok()) << snapshot.error().message;
  int dirty_prompts = 0;
  editor.scene_documents().set_runtime_hooks({
      .confirm_dirty_scene = [&](const QuaderSceneDocumentInfo &, std::string_view) {
        ++dirty_prompts;
        return QuaderDirtySceneDecision::Discard;
      },
  });
  ASSERT_TRUE(editor.modeling().create_box(
      {.min = {3.f, 3.f, 3.f}, .max = {4.f, 4.f, 4.f}}, "Unsaved Box").ok());
  const auto before = capture_command_observable_state(editor);
  for (const auto required : {product_release(0u, 2u), kInternalFullRelease}) {
    SCOPED_TRACE(product_release_label(required));
    snapshot.value().minimum_product_release = required;
    const auto path = root / "Scenes/Unavailable.qdr";
    ASSERT_TRUE(QdrDocumentWriter::write_project_document_atomic(path, snapshot.value()).ok);
    const auto files_before = directory_contents(directory.path());
    const auto result = editor.scene_documents().open_native_path(path);
    EXPECT_FALSE(result.ok());
    EXPECT_FALSE(result.changed());
    expect_unchanged(editor, before);
    EXPECT_EQ(directory_contents(directory.path()), files_before);
    const auto rejected_read = QdrDocumentReader::read_project_document(path, kPublicRelease01);
    EXPECT_FALSE(rejected_read.ok);
    EXPECT_EQ(rejected_read.stats.unique_geometry_decoded_count, 0u);
    EXPECT_TRUE(QdrDocumentReader::read_project_document(path, kInternalFullRelease).ok);
  }
  // Earlier 0.1-tagged scenes may contain instances. Reject them without
  // stripping roles, changing live state, or rewriting the authored file.
  auto instanced = QdrDocumentReader::read_project_document(root / "Scenes/Existing.qdr");
  ASSERT_TRUE(instanced.ok) << instanced.message;
  auto &instance_scene = instanced.document.scene();
  const EntityId definition = instance_scene.create_entity("Retained Definition");
  ASSERT_TRUE(instance_scene.set_parent(box.value().entity, definition, false));
  ASSERT_TRUE(instance_scene.set_instance_definition(definition));
  const EntityId occurrence = instance_scene.create_entity("Retained Instance", EntityKind::Instance);
  ASSERT_TRUE(instance_scene.set_instance_occurrence(occurrence, {.definition = definition}));
  auto instance_snapshot = qdr_project_write_snapshot_from_document(instanced.document);
  ASSERT_TRUE(instance_snapshot.ok()) << instance_snapshot.error().message;
  instance_snapshot.value().minimum_product_release = kPublicRelease01;
  const auto instance_path = root / "Scenes/ExistingInstances.qdr";
  ASSERT_TRUE(QdrDocumentWriter::write_project_document_atomic(
      instance_path, instance_snapshot.value()).ok);
  const auto instance_files_before = directory_contents(directory.path());
  const auto instance_open = editor.scene_documents().open_native_path(instance_path);
  EXPECT_FALSE(instance_open.ok());
  EXPECT_FALSE(instance_open.changed());
  expect_unchanged(editor, before);
  EXPECT_EQ(directory_contents(directory.path()), instance_files_before);
  const auto retained = QdrDocumentReader::read_project_document(instance_path, kInternalFullRelease);
  ASSERT_TRUE(retained.ok) << retained.message;
  ASSERT_NE(retained.document.scene().instance_occurrence(occurrence), nullptr);
  EXPECT_EQ(retained.document.scene().instance_occurrence(occurrence)->definition, definition);

  // A real historical file exercises the release-less compatibility path.
  const auto fixture = std::filesystem::path(__FILE__).parent_path() /
                       "fixtures/qdr/current_project_document_v4.qdr";
  ASSERT_TRUE(std::filesystem::exists(fixture));
  const auto legacy = root / "Scenes/Legacy.qdr";
  std::filesystem::copy_file(fixture, legacy);
  const auto files_before = directory_contents(directory.path());
  const auto rejected = editor.scene_documents().open_native_path(legacy);
  EXPECT_FALSE(rejected.ok());
  expect_unchanged(editor, before);
  EXPECT_EQ(directory_contents(directory.path()), files_before);
  const auto malformed = root / "Scenes/Malformed.qdr";
  std::string bytes = read_text(root / "Scenes/Existing.qdr");
  ASSERT_GE(bytes.size(), 64u);
  // Container 1.2 requires a nonzero release; the header has no payload checksum.
  std::fill(bytes.begin() + 56, bytes.begin() + 64, '\0');
  write_text(malformed, bytes);
  const auto malformed_files_before = directory_contents(directory.path());
  EXPECT_FALSE(editor.scene_documents().open_native_path(malformed).ok());
  expect_unchanged(editor, before);
  EXPECT_EQ(directory_contents(directory.path()), malformed_files_before);
  EXPECT_EQ(dirty_prompts, 0);
}

TEST(PublicReleasePersistence, ProjectRequirementPreflightPrecedesCloseOrProvisioning) {
  PublicReleaseDirectory directory;
  const auto working = directory.path() / "Working";
  const auto candidate = directory.path() / "Candidate";
  std::filesystem::create_directory(working);
  std::filesystem::create_directory(candidate);
  auto candidate_workspace = ProjectManifestStore::create_in_empty_directory(candidate);
  ASSERT_TRUE(candidate_workspace.ok()) << candidate_workspace.error().message;
  const auto manifest_path = candidate_workspace.value().manifest_path;
  const auto original_manifest = nlohmann::json::parse(read_text(manifest_path));
  ProjectSession project;
  QuaderEditorApi editor(project);
  ASSERT_TRUE(editor.project().create(working).ok());
  ASSERT_TRUE(editor.modeling().create_box(
      {.min = {-1.f, -1.f, -1.f}, .max = {1.f, 1.f, 1.f}}, "Unsaved Box").ok());
  int dirty_prompts = 0;
  editor.scene_documents().set_runtime_hooks({
      .confirm_dirty_scene = [&](const QuaderSceneDocumentInfo &, std::string_view) {
        ++dirty_prompts;
        return QuaderDirtySceneDecision::Discard;
      },
  });
  const auto before = capture_command_observable_state(editor);
  for (const auto kind : {"future", "full", "legacy", "missing", "zero"}) {
    SCOPED_TRACE(kind);
    auto json = original_manifest;
    if (std::string_view(kind) == "future") {
      json["minimumProductRelease"] = product_release(0u, 2u);
    } else if (std::string_view(kind) == "full") {
      json["minimumProductRelease"] = kInternalFullRelease;
    } else if (std::string_view(kind) == "zero") {
      json["minimumProductRelease"] = 0u;
    } else {
      json.erase("minimumProductRelease");
      if (std::string_view(kind) == "legacy") {
        json["version"] = 3u;
      }
    }
    write_text(manifest_path, json.dump(2));
    const auto files_before = directory_contents(directory.path());
    const auto result = editor.project().open(manifest_path);
    EXPECT_FALSE(result.ok());
    EXPECT_FALSE(result.changed());
    expect_unchanged(editor, before);
    EXPECT_EQ(directory_contents(directory.path()), files_before);
  }
  EXPECT_EQ(dirty_prompts, 0);
}

TEST(PublicReleasePersistence, CompatibleManifestRejectsIncompatibleRestoredSceneBeforeClosingProject) {
  PublicReleaseDirectory directory;
  const auto candidate_root = directory.path() / "FutureSceneProject";
  const auto working_root = directory.path() / "WorkingProject";
  std::filesystem::create_directory(candidate_root);
  std::filesystem::create_directory(working_root);
  std::filesystem::path candidate_manifest;
  const auto relative = scene_path("Scenes/Future.qdr");
  {
    ProjectSession candidate;
    QuaderEditorApi candidate_editor(candidate);
    ASSERT_TRUE(candidate_editor.project().create(candidate_root).ok());
    ASSERT_TRUE(candidate_editor.modeling().create_box(
        {.min = {-1.f, -1.f, -1.f}, .max = {1.f, 1.f, 1.f}}, "Future Box").ok());
    const auto id = candidate_editor.scene_documents().active_scene_id();
    ASSERT_TRUE(id.has_value());
    ASSERT_TRUE(candidate_editor.scene_documents().save_as(*id, relative).ok());
    candidate_manifest = candidate_editor.project().manifest_path();
  }
  const auto original_manifest = read_text(candidate_manifest);
  const auto decoded = ProjectManifestStore::deserialize(original_manifest);
  ASSERT_TRUE(decoded.ok());
  EXPECT_EQ(decoded.value().minimum_product_release(), kPublicRelease01);
  ASSERT_EQ(decoded.value().open_scene_paths().size(), 1u);
  const auto path = candidate_root / relative.native_path();
  const auto public_read = QdrDocumentReader::read_project_document(path, kPublicRelease01);
  ASSERT_TRUE(public_read.ok) << public_read.message;
  auto snapshot = qdr_project_write_snapshot_from_document(public_read.document);
  ASSERT_TRUE(snapshot.ok()) << snapshot.error().message;
  snapshot.value().minimum_product_release = kPublicRelease01;
  ASSERT_TRUE(QdrDocumentWriter::write_project_document_atomic(
      path, snapshot.value(), public_read.scene_uuid).ok);
  const auto public_bytes = read_text(path);
  ASSERT_GE(public_bytes.size(), 64u);
  // An accidental open would recreate this material, making provisioning writes
  // observable independently of the preserved manifest and dirty scene state.
  ASSERT_TRUE(std::filesystem::remove(candidate_root / "Materials/default_material.qmat"));

  ProjectSession working;
  QuaderEditorApi editor(working);
  ASSERT_TRUE(editor.project().create(working_root).ok());
  ASSERT_TRUE(editor.modeling().create_box(
      {.min = {2.f, 2.f, 2.f}, .max = {3.f, 3.f, 3.f}}, "Keep Unsaved Box").ok());
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::CreatePrimitive).ok());
  int dirty_prompts = 0;
  editor.scene_documents().set_runtime_hooks({
      .confirm_dirty_scene = [&](const QuaderSceneDocumentInfo &, std::string_view) {
        ++dirty_prompts;
        return QuaderDirtySceneDecision::Discard;
      },
  });
  const auto before = capture_command_observable_state(editor);
  for (const std::string_view kind : {"future_release", "zero_release", "future_major",
                                      "future_minor", "endian", "flags", "truncated"}) {
    SCOPED_TRACE(kind);
    auto bytes = public_bytes;
    if (kind == "future_release") {
      bytes[56] = '\x02';
    } else if (kind == "zero_release") {
      std::fill(bytes.begin() + 56, bytes.begin() + 64, '\0');
    } else if (kind == "future_major") {
      bytes[4] = '\x02';
    } else if (kind == "future_minor") {
      bytes[6] = '\x03';
    } else if (kind == "endian") {
      bytes[8] = '\0';
    } else if (kind == "flags") {
      bytes[12] = '\x01';
    } else {
      bytes.resize(63u);
    }
    write_text(path, bytes);
    const auto files_before = directory_contents(directory.path());
    const auto rejected = editor.project().open(candidate_manifest);
    EXPECT_FALSE(rejected.ok());
    EXPECT_FALSE(rejected.changed());
    expect_unchanged(editor, before);
    EXPECT_EQ(directory_contents(directory.path()), files_before);
    EXPECT_EQ(read_text(candidate_manifest), original_manifest);
    EXPECT_EQ(dirty_prompts, 0);
  }
}

TEST(PublicReleasePresentation, MenusAndToolbarsContainOnlyPublishedControls) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader_public_release_ui_test";
  char *argv[] = {name, nullptr};
  QApplication application(argc, argv);
  SceneSession session;
  QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
  auto settings = ViewportSettings::defaults();
  LocalizationService localization;
  QMainWindow window;
  QtMainMenu menu;
  menu.initialize(*window.menuBar(), QtMainMenuContext{
      .editor = editor,
      .modeling = session.modeling(),
      .viewport_settings = settings,
      .localization = localization,
  });
  menu.refresh();
  EXPECT_EQ(window.menuBar()->findChild<QMenu *>("menu_uv"), nullptr);
  EXPECT_EQ(window.menuBar()->findChild<QAction *>("toolbar_texture"), nullptr);
  for (const auto command : quader_command::kTypedCommandIds) {
    if (excluded_command(command)) {
      const auto object_name = QStringLiteral("command_%1").arg(
          QString::fromUtf8(command.string().data(),
                            static_cast<qsizetype>(command.string().size())));
      EXPECT_EQ(window.menuBar()->findChild<QAction *>(object_name), nullptr)
          << command.string();
    }
  }
  for (auto *submenu : window.menuBar()->findChildren<QMenu *>()) {
    const auto actions = submenu->actions();
    EXPECT_FALSE(actions.empty()) << submenu->objectName().toStdString();
    if (actions.empty()) {
      continue;
    }
    EXPECT_FALSE(actions.front()->isSeparator()) << submenu->objectName().toStdString();
    EXPECT_FALSE(actions.back()->isSeparator())
        << submenu->objectName().toStdString();
    for (qsizetype index = 1; index < actions.size(); ++index) {
      EXPECT_FALSE(actions[index]->isSeparator() && actions[index - 1]->isSeparator())
          << submenu->objectName().toStdString();
    }
  }
  QtToolToolbar toolbar;
  toolbar.synchronize(editor, editor.selection().summary());
  auto *primitive = toolbar.findChild<QAction *>(
      QStringLiteral("tool_create_primitive"));
  ASSERT_NE(primitive, nullptr);
  ASSERT_NE(primitive->menu(), nullptr);
  const auto primitive_choices = primitive->menu()->actions();
  ASSERT_EQ(primitive_choices.size(), 3);
  const std::array<const char *, 3> primitive_commands{
      "command_tools.activate.create_box",
      "command_tools.activate.create_cylinder",
      "command_tools.activate.create_sphere"};
  for (std::size_t index = 0; index < primitive_commands.size(); ++index) {
    EXPECT_EQ(primitive_choices[index]->objectName(),
              QString::fromUtf8(primitive_commands[index]));
    EXPECT_TRUE(primitive_choices[index]->shortcut().isEmpty());
  }
  EXPECT_EQ(primitive->text(), QStringLiteral("Create Box"));
  EXPECT_EQ(toolbar.findChild<QAction *>(
                QStringLiteral("command_tools.activate.create_cone")), nullptr);
  EXPECT_EQ(toolbar.findChild<QAction *>(
                QStringLiteral("command_tools.activate.create_torus")), nullptr);
  EXPECT_EQ(toolbar.findChild<QAction *>(QStringLiteral("toggle_transform_space")),
            nullptr);
  QtSnappingToolbar snapping_toolbar;
  snapping_toolbar.synchronize(editor);
  for (QAction *action : snapping_toolbar.viewport_actions()) {
    ASSERT_NE(action, nullptr);
    EXPECT_EQ(action->parent(), &snapping_toolbar);
    EXPECT_TRUE(action->isEnabled());
    EXPECT_TRUE(action->isVisible());
    EXPECT_TRUE(action->isCheckable());
    EXPECT_FALSE(snapping_toolbar.actions().contains(action));
    EXPECT_EQ(snapping_toolbar.widgetForAction(action), nullptr);
  }
  ASSERT_EQ(snapping_toolbar.actions().size(), 4);
  for (QAction *action : snapping_toolbar.actions()) {
    EXPECT_FALSE(action->isSeparator());
    EXPECT_FALSE(action->isCheckable());
  }
  for (const auto tool : {"mirror", "cut", "poly", "linear_array",
                          "radial_array", "path_array", "curve", "pivot"}) {
    EXPECT_EQ(toolbar.findChild<QAction *>(QStringLiteral("tool_%1").arg(tool)), nullptr)
        << tool;
  }
  for (const auto tool : {"select", "lasso_select", "paint_select", "move", "rotate", "scale", "knife"}) {
    EXPECT_NE(toolbar.findChild<QAction *>(QStringLiteral("tool_%1").arg(tool)), nullptr)
        << tool;
  }
}

} // namespace
} // namespace quader::test
