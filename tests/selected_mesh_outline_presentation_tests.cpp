////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "api/quader_polygon_document_internal_access.hpp"
#include "assets/asset_registry.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/render/quader_render_api_runtime_access.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/session/project_session.hpp"
#include "editor/tools/extrude_tool_service.hpp"
#include "math/glm_bridge.hpp"
#include "render/overlay/instance_selection_overlay_cache.hpp"
#include "render/overlay/overlay_renderable_cache.hpp"
#include "render/overlay/render_overlay_snapshot.hpp"
#include "render/overlay/scene_wireframe_overlay_cache.hpp"
#include "render/overlay/selected_mesh_outline_cache.hpp"
#include "render/scene_render_bridge.hpp"
#include "render/scene_render_extraction.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "scene/scene_document.hpp"
#include "ui/qt_command_event_filter.hpp"
#include "ui/qt_double_spin_box.hpp"
#include "ui/qt_spin_box.hpp"
#include "ui/qt_tool_settings_panel.hpp"
#include "ui/qt_viewport_panel.hpp"
#include "support/qt_viewport_input_test_helpers.hpp"
#include "support/command_behavior_contract.hpp"
#include "support/command_behavior_fixture.hpp"
#include "support/modeling_test_events.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"

#include <quader/modeling/neutral/polygon/document.hpp>

#include <gtest/gtest.h>

#include <QAbstractSpinBox>
#include <QApplication>
#include <QEnterEvent>
#include <QEventLoop>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPointer>
#include <QTimer>
#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <cstdint>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <tuple>
#include <utility>
#include <vector>

namespace quader {
namespace {
/// Makes native fixture mode transitions explicit before changing selection kind.
quader::QuaderApiVoidResult enter_selection_context(
    quader::QuaderEditorApi &editor, quader::QuaderSelectionMode kind) {
  const bool component = kind == quader::QuaderSelectionMode::Vertex ||
      kind == quader::QuaderSelectionMode::Edge ||
      kind == quader::QuaderSelectionMode::Face;
  const auto transition = editor.mode().set(component
      ? quader::QuaderEditorMode::Edit : quader::QuaderEditorMode::Object);
  if (!transition.ok()) return transition;
  return editor.selection().set_mode(kind);
}


TEST(SelectedMeshOutlinePresentationTests,
     LargeSceneBevelMeasuresProjection) {
  const char *source = std::getenv("QUADER_TEST_LARGE_SELECTION_PROJECT");
  if (source == nullptr || *source == '\0') {
    GTEST_SKIP() << "Set QUADER_TEST_LARGE_SELECTION_PROJECT to measure a cloned project.";
  }
  const std::filesystem::path source_project = std::filesystem::u8path(source);
  ASSERT_TRUE(std::filesystem::is_regular_file(source_project));
  const char *source_scene = std::getenv("QUADER_TEST_LARGE_SELECTION_SCENE");
  const auto original_scene = source_scene != nullptr && *source_scene != '\0'
      ? std::filesystem::u8path(source_scene)
      : source_project.parent_path() / "Scenes" / "one.qdr";
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const std::filesystem::path root(temporary.path().toStdWString());
  const auto project_root = root / source_project.parent_path().filename();
  std::error_code error;
  std::filesystem::copy(source_project.parent_path(), project_root,
      std::filesystem::copy_options::recursive |
          std::filesystem::copy_options::skip_symlinks, error);
  ASSERT_FALSE(error) << error.message();
  const auto relative_scene = std::filesystem::relative(
      original_scene, source_project.parent_path(), error);
  ASSERT_FALSE(error) << error.message();
  ProjectSession project(root / "recent-scenes.json");
  project.set_resource_root(std::filesystem::path(QUADER_SOURCE_DIR) /
      "resources/modeling_assets");
  QuaderEditorApi editor(project);
  const auto opened_project = editor.project().open(project_root / source_project.filename());
  ASSERT_TRUE(opened_project.ok()) << opened_project.error_message();
  const auto opened = editor.scene_documents().open_native_path(project_root / relative_scene);
  ASSERT_TRUE(opened.ok()) << opened.error_message();
  ASSERT_TRUE(editor.selection().clear().ok());
  auto &session = *project.active_scene()->session;
  auto &modeling = session.modeling();
  ASSERT_GE(modeling.modeling_sdk_object_count_for_tests(), 6'000u);
  const auto created = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      "Bevel performance participant");
  ASSERT_TRUE(created.ok()) << created.error_message();
  const auto object = created.value();
  ASSERT_TRUE(editor.selection().select_object(object).ok());
  ASSERT_TRUE(enter_selection_context(editor, QuaderSelectionMode::Edge).ok());
  ASSERT_TRUE(editor.selection().select_edge(object, 1, 2).ok());
  ASSERT_TRUE(editor.commands().execute(
      editor.commands().command_id_for_modeling_action("bevel_edges")).ok());
  ASSERT_TRUE(editor.tools().set_setting(
      "edge_bevel", "operation.edge_bevel.segments", 1.0).ok());
  (void)modeling.selection_overlay_frame();
  auto render_snapshot = build_render_scene_snapshot(session.scene(), session.assets());
  const auto elapsed = [](std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
  };
  for (int sample = 0; sample < 10; ++sample) {
    auto start = std::chrono::steady_clock::now();
    ASSERT_TRUE(editor.tools().set_setting(
        "edge_bevel", "operation.edge_bevel.width", 0.04 + sample * 0.025).ok());
    const double update_ms = elapsed(start);
    start = std::chrono::steady_clock::now();
    const auto overlay = modeling.selection_overlay_frame();
    const double selection_projection_ms = elapsed(start);
    start = std::chrono::steady_clock::now();
    render_snapshot = build_render_scene_snapshot(
        session.scene(), session.assets(), {.previous = &render_snapshot});
    const double render_projection_ms = elapsed(start);
    const auto mesh = editor.query().compiled_object_mesh(object);
    ASSERT_TRUE(mesh.has_value());
    std::size_t source_wires = 0u;
    for (const auto &line : overlay.lines) {
      if (line.element.object_id != object.modeling_object_id ||
          (line.layer != ViewportOverlayLayer::SourceWire &&
           line.layer != ViewportOverlayLayer::SelectedEdge)) continue;
      ++source_wires;
      for (const auto endpoint : {line.start, line.end}) {
        EXPECT_TRUE(std::ranges::any_of(mesh->vertices, [&](const auto &vertex) {
          return length_squared(vertex.position - endpoint) < 1.0e-10F;
        }));
      }
    }
    EXPECT_GT(source_wires, 0u);
    const auto builds = modeling.modeling_sdk_projection_stats_for_tests()
                            .native_selection_projection_build_count;
    start = std::chrono::steady_clock::now();
    for (int repeat = 0; repeat < 4; ++repeat) {
      (void)modeling.selection_overlay_frame();
    }
    const double retained_ms = elapsed(start);
    EXPECT_EQ(modeling.modeling_sdk_projection_stats_for_tests()
                  .native_selection_projection_build_count, builds);
    std::cout << "LARGE_SCENE_BEVEL sample=" << sample
              << " objects=" << modeling.modeling_sdk_object_count_for_tests()
              << " update_ms=" << update_ms
              << " selection_projection_ms=" << selection_projection_ms
              << " render_projection_ms=" << render_projection_ms
              << " retained_4_ms=" << retained_ms << std::endl;
  }
  ASSERT_TRUE(editor.commands().execute_native_tool_settings_intent(
      QuaderNativeToolSettingsIntent::Cancel).ok());
}

/// Mixes selected/unselected direct meshes and formal occurrences sharing geometry.
struct OutlinePresentationHarness {
  SceneDocument document;
  AssetRegistry assets;
  RenderOverlaySnapshotCache cache;
  EntityId selected_mesh = kInvalidEntityId;
  EntityId unselected_mesh = kInvalidEntityId;
  EntityId selected_instance = kInvalidEntityId;
  EntityId unselected_instance = kInvalidEntityId;

  void seed() {
    MeshAsset mesh;
    mesh.name = "Outline Triangle";
    auto &geometry = mesh.edit_geometry();
    geometry.vertices = {
        MeshVertex{.position = {-1.0f, 0.0f, 0.0f}},
        MeshVertex{.position = {1.0f, 0.0f, 0.0f}},
        MeshVertex{.position = {0.0f, 1.0f, 0.0f}},
    };
    geometry.indices = {0u, 1u, 2u};
    geometry.primitives = {{.index_offset = 0u, .index_count = 3u, .material_slot = 0u}};
    const AssetId asset = assets.add_mesh(std::move(mesh));
    selected_mesh = document.create_entity("Selected Mesh", EntityKind::Mesh);
    unselected_mesh = document.create_entity("Unselected Mesh", EntityKind::Mesh);
    ASSERT_TRUE(document.set_mesh(selected_mesh, MeshComponent{.mesh = asset}));
    ASSERT_TRUE(document.set_mesh(unselected_mesh, MeshComponent{.mesh = asset}));
    const EntityId definition = document.create_entity("Definition", EntityKind::Empty);
    ASSERT_TRUE(document.set_instance_definition(definition));
    ASSERT_TRUE(document.set_visible(definition, false));
    const EntityId member = document.create_entity("Member", EntityKind::Mesh);
    ASSERT_TRUE(document.set_mesh(member, MeshComponent{.mesh = asset}));
    ASSERT_TRUE(document.set_parent(member, definition, false));
    selected_instance = document.create_entity("Selected Instance", EntityKind::Instance);
    unselected_instance = document.create_entity("Unselected Instance", EntityKind::Instance);
    ASSERT_TRUE(document.set_instance_occurrence(
        selected_instance, InstanceOccurrenceComponent{.definition = definition}));
    ASSERT_TRUE(document.set_instance_occurrence(
        unselected_instance, InstanceOccurrenceComponent{.definition = definition}));
    ASSERT_TRUE(document.set_selected(selected_mesh, true));
    ASSERT_TRUE(document.set_selected(selected_instance, true));
  }

  RenderOverlayPersistentSnapshotUpdate update(bool outline_only,
                                                std::uint8_t wireframe_mask = 1u,
                                                bool selected_wire_visible = true) {
    return cache.update_persistent(document, assets, true, wireframe_mask, {},
                                   {}, nullptr, selected_wire_visible, {}, {},
                                   outline_only);
  }
};

/// Uses the native selection owner only when the production coordinator asks
/// for a new frame; direct getter calls would hide a stale outer source gate.
class ExtrudeOutlineProvider final : public ViewportOverlaySelectionFrameProvider {
public:
  explicit ExtrudeOutlineProvider(SceneSession &session) : session_(session) {}
  ViewportOverlayFrame build_selection_overlay_frame() const override {
    ++builds;
    return session_.modeling().selection_overlay_render_frame();
  }
  mutable std::uint64_t builds = 0u;

private:
  SceneSession &session_;
};

/// Captures styled coordinates at the renderer-neutral submission boundary.
class ExtrudeOutlineTarget final : public ViewportOverlaySubmissionTarget {
public:
  ViewportOverlayCameraUpdate update_overlay_cameras(
      std::size_t, std::uint8_t, ViewportOverlayCamera) override {
    return {true, true};
  }
  void submit_persistent_overlay(std::size_t, std::uint8_t,
      ViewportOverlayFrame frame, ViewportOverlayCamera) override {
    persistent = std::move(frame);
    ++submissions;
  }
  void submit_dynamic_overlay(std::size_t, std::uint8_t,
      ViewportOverlayFrame frame, ViewportOverlayCamera) override {
    dynamic = std::move(frame);
    ++dynamic_submissions;
  }
  ViewportOverlayFrame persistent;
  ViewportOverlayFrame dynamic;
  std::uint64_t submissions = 0u;
  std::uint64_t dynamic_submissions = 0u;
};

/// Exercises Face-mode source wires and selected-cap edges with durable assets
/// unchanged during the same dynamic preview used by the viewport.
struct ExtrudeOutlineHarness {
  SceneSession session;
  QuaderEditorApi editor{QuaderApiSessionInternalAccess::from_scene_session(session)};
  ExtrudeOutlineProvider provider{session};
  ExtrudeOutlineTarget target;
  ViewportOverlaySubmissionCoordinator coordinator;
  ViewportSettings settings = ViewportSettings::defaults();
  QuaderModelingObjectRef object;

  void seed(modeling::FaceExtrudeMode mode, double amount = 1.0) {
    quader_poly::Document document;
    const std::array vertices{
        quader_poly::add_vertex(document, {0, 0, 0}),
        quader_poly::add_vertex(document, {1, 0, 0}),
        quader_poly::add_vertex(document, {2, 0, 0}),
        quader_poly::add_vertex(document, {0, 1, 0}),
        quader_poly::add_vertex(document, {1, 1, 0}),
        quader_poly::add_vertex(document, {2, 1, 0})};
    const auto first = quader_poly::add_face(
        document, std::array{vertices[0], vertices[1], vertices[4], vertices[3]});
    const auto second = quader_poly::add_face(
        document, std::array{vertices[1], vertices[2], vertices[5], vertices[4]});
    const auto created = editor.modeling().create_mesh(
        QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)),
        {.name = "Extrusion outline strip"});
    ASSERT_TRUE(created.ok()) << created.error_message();
    object = created.value();
    ASSERT_TRUE(enter_selection_context(editor, QuaderSelectionMode::Face).ok());
    ASSERT_TRUE(editor.selection().select_faces(object,
        {static_cast<QuaderElementId>(first),
         static_cast<QuaderElementId>(second)}).ok());
    ASSERT_TRUE(session.extrude().set_mode(mode).ok);
    ASSERT_TRUE(editor.tools().activate(ViewportToolId::Extrude).ok());
    ASSERT_TRUE(session.extrude().preview_active());
    ASSERT_DOUBLE_EQ(session.extrude().amount(), 0.0);
    ASSERT_TRUE(session.extrude().set_amount(amount).ok);
    (void)settings.set_color(ViewportColorSettingId::SourceWire, {0, 1, 1, 1});
    (void)settings.set_color(ViewportColorSettingId::SelectionWire, {1, 1, 0, 1});
  }

  ViewportOverlaySelectionSourceStamp source_stamp() const {
    auto &modeling = session.modeling();
    return {
        .source_identity = reinterpret_cast<std::uintptr_t>(&modeling),
        .content_revision = modeling.content_revision(),
        .selection_revision = modeling.selection_revision(),
        .scene_revision = session.scene().revision(),
        .asset_revision = session.assets().revision(),
        .overlay_signature = modeling.selection_overlay_signature(),
        .selection_context = ViewportOverlaySelectionContext::Face,
    };
  }

  ViewportOverlaySubmissionResult submit(std::size_t pane = 0u) {
    return coordinator.submit({
        .viewport_index = pane,
        .layer_mask = static_cast<std::uint8_t>(1u << pane),
        .selection_source = source_stamp(),
        .selection_frame_provider = &provider,
        .scene_selection_mode = RenderOverlaySelectionMode::Component,
        .style_selection_context = ViewportOverlaySelectionContext::Face,
        .overlays_visible = true,
    }, settings, target);
  }

  void expect_visible_coordinates(double displayed_amount) {
    const auto previews = session.modeling().tool_preview_meshes();
    std::vector<QVec3> positions;
    if (!previews.empty()) {
      ASSERT_EQ(previews.size(), 1u);
      const auto &preview = previews.front();
      EXPECT_EQ(preview.source_entity, object.entity);
      for (std::size_t index = 0; index < preview.mesh.geometry().vertices.size(); ++index) {
        const QVec3 local = preview.dynamic_position_payload
            ? (*preview.dynamic_position_payload)[index]
            : preview.mesh.geometry().vertices[index].position;
        positions.push_back(transform_point(preview.transform, local));
      }
    } else {
      const auto mesh = editor.query().compiled_object_mesh(object);
      ASSERT_TRUE(mesh.has_value());
      for (const auto &vertex : mesh->vertices) positions.push_back(vertex.position);
    }
    const float height = static_cast<float>(displayed_amount /
        viewport_host_defaults().map_units_per_world_unit);
    std::size_t source_wires = 0u;
    std::size_t cap_wires = 0u;
    for (const auto &line : target.persistent.lines) {
      if (line.element.object_id != object.modeling_object_id) continue;
      if (line.layer != ViewportOverlayLayer::SourceWire &&
          line.layer != ViewportOverlayLayer::SelectedFaceEdge) continue;
      for (const auto endpoint : {line.start, line.end}) {
        EXPECT_TRUE(std::ranges::any_of(positions, [&](QVec3 position) {
          return length_squared(position - endpoint) < 1.0e-14F;
        })) << "Stale submitted endpoint at " << endpoint.x << ", "
            << endpoint.y << ", " << endpoint.z;
      }
      if (line.layer == ViewportOverlayLayer::SelectedFaceEdge) {
        ++cap_wires;
        EXPECT_NEAR(line.start.z, height, 1.0e-7F);
        EXPECT_NEAR(line.end.z, height, 1.0e-7F);
        const auto expected = line.style_role == ViewportOverlayStyleRole::ActiveSelectionWire
            ? viewport_active_selection_color(rgba8(255, 255, 0)) : rgba8(255, 255, 0);
        EXPECT_EQ(line.color & 0x00ffffffu, expected & 0x00ffffffu);
      } else {
        ++source_wires;
        EXPECT_EQ(line.color & 0x00ffffffu, rgba8(0, 255, 255) & 0x00ffffffu);
      }
    }
    EXPECT_GT(cap_wires, 0u);
    if (displayed_amount != 0.0) EXPECT_GT(source_wires, 0u);
  }

  void expect_unchanged() {
    const auto builds = provider.builds;
    const auto submissions = target.submissions;
    const auto projection = session.modeling().modeling_sdk_projection_stats_for_tests();
    const auto preview_work = session.modeling().tool_preview_work_stats();
    const auto work = coordinator.work_counters();
    for (int frame = 0; frame < 20; ++frame) {
      const auto result = submit();
      EXPECT_FALSE(result.selection_source_refreshed);
      EXPECT_FALSE(result.persistent_submitted);
      EXPECT_FALSE(result.dynamic_submitted);
    }
    EXPECT_EQ(provider.builds, builds);
    EXPECT_EQ(target.submissions, submissions);
    EXPECT_EQ(session.modeling().modeling_sdk_projection_stats_for_tests()
                  .native_selection_projection_build_count,
              projection.native_selection_projection_build_count);
    EXPECT_EQ(session.modeling().tool_preview_work_stats().signature_enumeration_count,
              preview_work.signature_enumeration_count);
    EXPECT_EQ(session.modeling().tool_preview_work_stats().mesh_extraction_count,
              preview_work.mesh_extraction_count);
    EXPECT_EQ(coordinator.work_counters().dynamic_source_compose_count,
              work.dynamic_source_compose_count);
  }
};

TEST(SelectedMeshOutlinePresentationTests,
     SameDocumentBridgeStepsRefreshRetainedWiresAndCancelRestoresThem) {
  for (const auto command : {quader_command::kModelingInterpolatedBridgeEdges,
                             quader_command::kModelingInterpolatedBridgeFaces}) {
    SCOPED_TRACE(command.string());
    const auto *contract = test::find_command_behavior_contract(command.string());
    ASSERT_NE(contract, nullptr);
    auto fixture = test::CommandBehaviorFixture::create(*contract);
    auto &editor = fixture->editor();
    auto &session = fixture->native_scene();
    auto &modeling = session.modeling();
    ExtrudeOutlineProvider provider{session};
    ExtrudeOutlineTarget target;
    ViewportOverlaySubmissionCoordinator coordinator;
    const auto settings = ViewportSettings::defaults();
    const auto context = command.string() ==
                                 quader_command::kModelingInterpolatedBridgeEdges.string()
        ? ViewportOverlaySelectionContext::Edge
        : ViewportOverlaySelectionContext::Face;
    const auto submit = [&] {
      return coordinator.submit({
          .viewport_index = 0u,
          .layer_mask = 1u,
          .selection_source = {
              .source_identity = reinterpret_cast<std::uintptr_t>(&modeling),
              .content_revision = modeling.content_revision(),
              .selection_revision = modeling.selection_revision(),
              .scene_revision = session.scene().revision(),
              .asset_revision = session.assets().revision(),
              .overlay_signature = modeling.selection_overlay_signature(),
              .selection_context = context,
          },
          .selection_frame_provider = &provider,
          .scene_selection_mode = RenderOverlaySelectionMode::Component,
          .style_selection_context = context,
          .overlays_visible = true,
      }, settings, target);
    };
    ASSERT_TRUE(submit().persistent_submitted);
    const auto original_lines = target.persistent.lines.size();
    ASSERT_GT(original_lines, 0u);
    ASSERT_TRUE(editor.commands().execute(command).ok());
    ASSERT_TRUE(submit().persistent_submitted);
    const auto initial_lines = target.persistent.lines.size();
    const auto updated = editor.tools().set_setting(
        "bridge", "operation.bridge.steps", 3.0);
    ASSERT_TRUE(updated.ok()) << updated.error_message();
    ASSERT_TRUE(submit().persistent_submitted);
    EXPECT_GT(target.persistent.lines.size(), initial_lines);
    const auto builds = provider.builds;
    const auto submissions = target.submissions;
    for (int frame = 0; frame < 4; ++frame) {
      const auto stable = submit();
      EXPECT_FALSE(stable.selection_source_refreshed);
      EXPECT_FALSE(stable.persistent_submitted);
    }
    EXPECT_EQ(provider.builds, builds);
    EXPECT_EQ(target.submissions, submissions);
    ASSERT_TRUE(editor.commands().execute_native_tool_settings_intent(
        QuaderNativeToolSettingsIntent::Cancel).ok());
    ASSERT_TRUE(submit().persistent_submitted);
    EXPECT_EQ(target.persistent.lines.size(), original_lines);
  }
}

TEST(SelectedMeshOutlinePresentationTests,
     SequentialBevelCornerChainPublishesRetainedWiresThroughHistory) {
  SceneSession session;
  QuaderEditorApi editor{QuaderApiSessionInternalAccess::from_scene_session(session)};
  const auto created = editor.modeling().create_box(
      {.min = {-2.0f, -2.0f, -2.0f}, .max = {2.0f, 2.0f, 2.0f}},
      "Sequential bevel corner");
  ASSERT_TRUE(created.ok()) << created.error_message();
  const auto object = created.value();
  ASSERT_TRUE(editor.selection().select_object(object).ok());
  ASSERT_TRUE(enter_selection_context(editor, QuaderSelectionMode::Edge).ok());

  // Resolve authored IDs from visible edges, so the second selection follows
  // the first operation's real topology rather than assuming allocation order.
  const auto select_edges = [&](bool top_perimeter) {
    std::vector<std::pair<QuaderElementId, QuaderElementId>> edges;
    for (const auto &line : editor.query().selection_overlay().lines) {
      if (line.element.object_id != object.modeling_object_id ||
          (line.layer != QuaderOverlayLayer::SourceWire &&
           line.layer != QuaderOverlayLayer::SelectedEdge)) continue;
      const auto eligible = [&](const QVec3 &position) {
        return top_perimeter ? std::abs(position.y - 2.0f) < 1.0e-5f
            : position.x > 0.0f && position.z > 0.0f &&
                  std::abs(position.x - position.z) < 1.0e-5f;
      };
      if (eligible(line.start) && eligible(line.end)) {
        edges.emplace_back(std::min(line.element.edge_a, line.element.edge_b),
                           std::max(line.element.edge_a, line.element.edge_b));
      }
    }
    std::ranges::sort(edges);
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
    EXPECT_EQ(edges.size(), top_perimeter ? 4u : 3u);
    EXPECT_TRUE(editor.selection().clear().ok());
    for (const auto &[a, b] : edges) {
      EXPECT_TRUE(editor.selection().select_edge(
          object, a, b, QuaderSelectionEdit::Add).ok());
    }
  };
  const auto begin_bevel = [&](double width) {
    ASSERT_TRUE(editor.commands().execute(
        editor.commands().command_id_for_modeling_action("bevel_edges")).ok());
    ASSERT_TRUE(editor.tools().set_setting(
        "edge_bevel", "operation.edge_bevel.width", width).ok());
    ASSERT_TRUE(editor.tools().set_setting(
        "edge_bevel", "operation.edge_bevel.segments", 2.0).ok());
    ASSERT_TRUE(editor.tools().set_setting(
        "edge_bevel", "operation.edge_bevel.profile", 0.5).ok());
  };
  ASSERT_NO_FATAL_FAILURE(select_edges(true));
  ASSERT_NO_FATAL_FAILURE(begin_bevel(0.5));
  ASSERT_TRUE(editor.commands().execute(
      quader_command::kModelingApplyActivePreview).ok());
  ASSERT_NO_FATAL_FAILURE(select_edges(false));
  ASSERT_EQ(editor.selection().summary().selected_edge_count, 3u);

  ExtrudeOutlineProvider provider{session};
  ExtrudeOutlineTarget target;
  ViewportOverlaySubmissionCoordinator coordinator;
  auto &modeling = session.modeling();
  const auto submit = [&] {
    return coordinator.submit({
        .viewport_index = 0u,
        .layer_mask = 1u,
        .selection_source = {
            .source_identity = reinterpret_cast<std::uintptr_t>(&modeling),
            .content_revision = modeling.content_revision(),
            .selection_revision = modeling.selection_revision(),
            .scene_revision = session.scene().revision(),
            .asset_revision = session.assets().revision(),
            .overlay_signature = modeling.selection_overlay_signature(),
            .selection_context = ViewportOverlaySelectionContext::Edge},
        .selection_frame_provider = &provider,
        .scene_selection_mode = RenderOverlaySelectionMode::Component,
        .style_selection_context = ViewportOverlaySelectionContext::Edge,
        .overlays_visible = true}, ViewportSettings::defaults(), target);
  };
  struct SubmittedGeometry {
    std::vector<std::array<float, 3>> positions;
    std::vector<std::uint32_t> indices;
    std::vector<std::array<float, 6>> wires;
    bool operator==(const SubmittedGeometry &) const = default;
  };
  const auto capture = [&] {
    SubmittedGeometry result;
    const auto snapshot = build_render_scene_snapshot(session.scene(), session.assets());
    EXPECT_EQ(snapshot.meshes.size(), 1u);
    for (const auto &record : snapshot.meshes) {
      for (const auto &vertex : record.mesh.vertices) {
        const auto position = transform_point(record.world_transform, vertex.position);
        result.positions.push_back({position.x, position.y, position.z});
      }
      result.indices.insert(result.indices.end(), record.mesh.indices.begin(),
                            record.mesh.indices.end());
    }
    EXPECT_TRUE(submit().persistent_submitted);
    for (const auto &line : target.persistent.lines) {
      if (line.element.object_id != object.modeling_object_id ||
          (line.layer != ViewportOverlayLayer::SourceWire &&
           line.layer != ViewportOverlayLayer::SelectedEdge)) continue;
      for (const auto endpoint : {line.start, line.end}) {
        EXPECT_TRUE(std::ranges::any_of(result.positions, [&](const auto &position) {
          return length_squared(endpoint - QVec3{position[0], position[1], position[2]})
              < 1.0e-10f;
        })) << "A submitted corner-chain wire must belong to the displayed mesh.";
      }
      std::array<float, 3> start{line.start.x, line.start.y, line.start.z};
      std::array<float, 3> end{line.end.x, line.end.y, line.end.z};
      if (end < start) std::swap(start, end);
      result.wires.push_back({start[0], start[1], start[2], end[0], end[1], end[2]});
    }
    std::ranges::sort(result.wires);
    result.wires.erase(std::unique(result.wires.begin(), result.wires.end()),
                       result.wires.end());
    EXPECT_FALSE(result.wires.empty());
    const auto builds = provider.builds;
    const auto submissions = target.submissions;
    const auto work = modeling.tool_preview_work_stats();
    for (int frame = 0; frame < 3; ++frame) {
      const auto unchanged = submit();
      EXPECT_FALSE(unchanged.selection_source_refreshed);
      EXPECT_FALSE(unchanged.persistent_submitted);
    }
    EXPECT_EQ(provider.builds, builds);
    EXPECT_EQ(target.submissions, submissions);
    EXPECT_EQ(modeling.tool_preview_work_stats().mesh_extraction_count,
              work.mesh_extraction_count);
    EXPECT_EQ(modeling.tool_preview_work_stats().signature_enumeration_count,
              work.signature_enumeration_count);
    return result;
  };
  const auto source = capture();
  const auto initial_history = session.history().undo_count();
  ASSERT_NO_FATAL_FAILURE(begin_bevel(0.2));
  auto preview = capture();
  EXPECT_NE(preview, source);
  for (const auto &[setting, value] : std::array{
           std::pair{"operation.edge_bevel.width", 0.25},
           std::pair{"operation.edge_bevel.segments", 4.0},
           std::pair{"operation.edge_bevel.profile", 0.25}}) {
    SCOPED_TRACE(setting);
    ASSERT_TRUE(editor.tools().set_setting("edge_bevel", setting, value).ok());
    const auto updated = capture();
    EXPECT_NE(updated, preview);
    EXPECT_EQ(session.history().undo_count(), initial_history);
    preview = updated;
  }
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  EXPECT_EQ(capture(), source);
  EXPECT_EQ(session.history().undo_count(), initial_history);
  EXPECT_EQ(editor.selection().summary().selected_edge_count, 3u);

  ASSERT_NO_FATAL_FAILURE(begin_bevel(0.2));
  const auto accepted = capture();
  ASSERT_TRUE(editor.commands().execute(
      quader_command::kModelingApplyActivePreview).ok());
  EXPECT_EQ(capture(), accepted);
  EXPECT_EQ(session.history().undo_count(), initial_history + 1u);
  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_EQ(capture(), source);
  ASSERT_TRUE(editor.history().redo().ok());
  EXPECT_EQ(capture(), accepted);
  EXPECT_EQ(editor.mode().state().participants.size(), 1u);
}

TEST(SelectedMeshOutlinePresentationTests,
     BevelNumericEditsRefreshRetainedWiresAndRenderGeometry) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-bevel-submission-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  application.setQuitOnLastWindowClosed(false);
  SceneSession session;
  QuaderEditorApi editor{QuaderApiSessionInternalAccess::from_scene_session(session)};
  const auto created = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      "Bevel settings submission source");
  ASSERT_TRUE(created.ok()) << created.error_message();
  const auto object = created.value();
  ASSERT_TRUE(editor.selection().select_object(object).ok());
  ASSERT_TRUE(enter_selection_context(editor, QuaderSelectionMode::Edge).ok());
  ASSERT_TRUE(editor.selection().select_edge(object, 1, 2).ok());
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());

  QtViewportPanel viewport;
  viewport.setAttribute(Qt::WA_DontShowOnScreen, true);
  viewport.resize(1000, 750);
  viewport.attach_workspace(session.viewport_workspace());
  viewport.set_startup_presentation_blocked(false);
  QtToolSettingsPanel panel;
  viewport.set_settings_panel(&panel);
  panel.set_expanded(true);
  panel.bind(editor);
  QtCommandEventFilter command_filter(editor, [&] { return viewport.tool_state(); });
  command_filter.set_input_context_provider([&] {
    return viewport.command_input_context();
  });
  application.installEventFilter(&command_filter);
  viewport.show();
  viewport.activateWindow();
  QCoreApplication::processEvents();
  auto *surface = viewport.surface(ViewportPaneId::Perspective);
  ASSERT_NE(surface, nullptr);
  const QPointF pointer{400, 300};
  QEnterEvent enter(pointer, pointer, surface->mapToGlobal(pointer.toPoint()));
  QFocusEvent focus(QEvent::FocusIn);
  quader::test_support::send_visible_viewport_event(surface, &enter);
  QCoreApplication::sendEvent(surface, &focus);
  const auto process_frame = [&] {
    viewport.process_input(editor, session.scene(), session.assets(),
                           session.modeling(), {}, session.native_tools(),
                           &session.point_placement(), 1.0 / 60.0);
    // UISystem leaves the current numeric draft in place during interaction;
    // viewport ownership and overlay placement still advance on every frame.
    if (!panel.numeric_interaction_active()) panel.refresh();
    viewport.refresh_workspace_layout();
    viewport.refresh_settings_overlay(editor);
  };
  process_frame();

  auto &modeling = session.modeling();
  ExtrudeOutlineProvider provider{session};
  ExtrudeOutlineTarget target;
  ViewportOverlaySubmissionCoordinator coordinator;
  const auto settings = ViewportSettings::defaults();
  const auto submit_overlay = [&] {
    return coordinator.submit({
        .viewport_index = 0u,
        .layer_mask = 1u,
        .selection_source = {
            .source_identity = reinterpret_cast<std::uintptr_t>(&modeling),
            .content_revision = modeling.content_revision(),
            .selection_revision = modeling.selection_revision(),
            .scene_revision = session.scene().revision(),
            .asset_revision = session.assets().revision(),
            .overlay_signature = modeling.selection_overlay_signature(),
            .selection_context = ViewportOverlaySelectionContext::Edge,
        },
        .selection_frame_provider = &provider,
        .scene_selection_mode = RenderOverlaySelectionMode::Component,
        .style_selection_context = ViewportOverlaySelectionContext::Edge,
        .overlays_visible = true,
    }, settings, target);
  };
  const auto submitted_wires = [&] {
    std::vector<std::array<float, 6u>> wires;
    for (const auto &line : target.persistent.lines) {
      if (line.element.object_id == object.modeling_object_id &&
          (line.layer == ViewportOverlayLayer::SourceWire ||
           line.layer == ViewportOverlayLayer::SelectedEdge)) {
        wires.push_back({line.start.x, line.start.y, line.start.z,
                         line.end.x, line.end.y, line.end.z});
      }
    }
    return wires;
  };
  auto render_snapshot = build_render_scene_snapshot(session.scene(), session.assets());
  const auto rendered_positions = [&] {
    std::vector<std::array<float, 3u>> positions;
    for (const auto &record : render_snapshot.meshes) {
      for (const auto &vertex : record.mesh.vertices) {
        const auto world = transform_point(record.world_transform, vertex.position);
        positions.push_back({world.x, world.y, world.z});
      }
    }
    return positions;
  };
  const auto expect_unchanged = [&] {
    const auto builds = provider.builds;
    const auto submissions = target.submissions;
    const auto work = modeling.tool_preview_work_stats();
    for (int frame = 0; frame < 4; ++frame) {
      process_frame();
      EXPECT_TRUE(render_scene_snapshot_matches_sources(
          render_snapshot, session.scene(), session.assets()));
      const auto unchanged = build_render_scene_snapshot(
          session.scene(), session.assets(), {.previous = &render_snapshot});
      EXPECT_TRUE(unchanged.reused_previous_records);
      EXPECT_FALSE(unchanged.dirty.any_dirty());
      const auto submission = submit_overlay();
      EXPECT_FALSE(submission.selection_source_refreshed);
      EXPECT_FALSE(submission.persistent_submitted);
    }
    EXPECT_EQ(provider.builds, builds);
    EXPECT_EQ(target.submissions, submissions);
    EXPECT_EQ(modeling.tool_preview_work_stats().signature_enumeration_count,
              work.signature_enumeration_count);
    EXPECT_EQ(modeling.tool_preview_work_stats().mesh_extraction_count,
              work.mesh_extraction_count);
  };

  ASSERT_TRUE(submit_overlay().persistent_submitted);
  const auto original_positions = rendered_positions();
  const auto original_wires = submitted_wires();
  ASSERT_FALSE(original_wires.empty());
  ASSERT_TRUE(editor.commands().execute(
      editor.commands().command_id_for_modeling_action("bevel_edges")).ok());
  ASSERT_TRUE(editor.tools().set_setting(
      "edge_bevel", "operation.edge_bevel.segments", 1.0).ok());
  ASSERT_TRUE(editor.tools().set_setting(
      "edge_bevel", "operation.edge_bevel.width", 0.08).ok());
  process_frame();
  render_snapshot = build_render_scene_snapshot(
      session.scene(), session.assets(), {.previous = &render_snapshot});
  ASSERT_EQ(render_snapshot.meshes.size(), 1u);
  ASSERT_TRUE(submit_overlay().persistent_submitted);
  auto previous_positions = rendered_positions();
  auto previous_wires = submitted_wires();
  const auto history_count = session.history().undo_count();
  const auto initial_indices = render_snapshot.meshes.front().mesh.indices.size();

  QCoreApplication::processEvents();
  process_frame();
  ASSERT_TRUE(panel.isVisible());
  ASSERT_TRUE(panel.testAttribute(Qt::WA_NativeWindow));
  const auto send_mouse = [](QLineEdit &line, QEvent::Type type, QPoint position,
                             Qt::MouseButton button, Qt::MouseButtons buttons,
                             Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QMouseEvent event(type, QPointF(position), QPointF(line.mapToGlobal(position)),
                     button, buttons, modifiers);
    QCoreApplication::sendEvent(&line, &event);
  };
  const std::array changes{
      std::pair{"operation.edge_bevel.width", 0.12},
      std::pair{"operation.edge_bevel.width", 0.18},
      std::pair{"operation.edge_bevel.segments", 4.0},
      std::pair{"operation.edge_bevel.profile", 0.25}};
  for (const auto &[setting, value] : changes) {
    SCOPED_TRACE(setting);
    SCOPED_TRACE(value);
    auto *spin = panel.findChild<QAbstractSpinBox *>(
        QStringLiteral("tool_settings.control.") + QString::fromUtf8(setting));
    ASSERT_NE(spin, nullptr);
    const QPointer<QAbstractSpinBox> retained_spin(spin);
    ASSERT_TRUE(dynamic_cast<QtSpinBox *>(spin) != nullptr ||
                dynamic_cast<QtDoubleSpinBox *>(spin) != nullptr);
    auto *line = spin->findChild<QLineEdit *>();
    ASSERT_NE(line, nullptr);
    const QPoint press = line->rect().center();
    if (value == 0.18) {
      // The second width update follows the live custom-scrub path while the
      // same frame refresh that hosts the viewport runs between dispatches.
      send_mouse(*line, QEvent::MouseButtonPress, press, Qt::LeftButton,
                 Qt::LeftButton, Qt::ShiftModifier);
      const QPoint crossed = press + QPoint(QApplication::startDragDistance() + 1, 0);
      send_mouse(*line, QEvent::MouseMove, crossed, Qt::NoButton,
                 Qt::LeftButton, Qt::ShiftModifier);
      const QPoint moved = crossed + QPoint(6, 0);
      send_mouse(*line, QEvent::MouseMove, moved, Qt::NoButton,
                 Qt::LeftButton, Qt::ShiftModifier);
      process_frame();
      QEventLoop loop;
      QTimer frames;
      QObject::connect(&frames, &QTimer::timeout, &panel, process_frame);
      frames.start(4);
      QTimer::singleShot(40, &loop, &QEventLoop::quit);
      loop.exec();
      EXPECT_FALSE(render_scene_snapshot_matches_sources(
          render_snapshot, session.scene(), session.assets()));
      send_mouse(*line, QEvent::MouseButtonRelease, moved, Qt::LeftButton,
                 Qt::NoButton, Qt::ShiftModifier);
    } else {
      send_mouse(*line, QEvent::MouseButtonPress, press, Qt::LeftButton, Qt::LeftButton);
      send_mouse(*line, QEvent::MouseButtonRelease, press, Qt::LeftButton, Qt::NoButton);
      process_frame();
      ASSERT_TRUE(line->hasSelectedText());
      const auto text = dynamic_cast<QtSpinBox *>(spin) != nullptr
          ? spin->locale().toString(static_cast<int>(value))
          : spin->locale().toString(value, 'f', 4);
      for (const QChar character : text) {
        QKeyEvent key(QEvent::KeyPress, character.unicode(), Qt::NoModifier,
                      QString(character));
        QCoreApplication::sendEvent(line, &key);
      }
      process_frame();
      QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
      QCoreApplication::sendEvent(line, &enter);
    }
    QCoreApplication::processEvents();
    process_frame();
    ASSERT_FALSE(retained_spin.isNull());
    EXPECT_EQ(panel.findChild<QAbstractSpinBox *>(spin->objectName()), spin);
    EXPECT_FALSE(render_scene_snapshot_matches_sources(
        render_snapshot, session.scene(), session.assets()));
    render_snapshot = build_render_scene_snapshot(
        session.scene(), session.assets(), {.previous = &render_snapshot});
    ASSERT_EQ(render_snapshot.meshes.size(), 1u);
    EXPECT_TRUE(render_snapshot.meshes.front().mesh_dirty);
    EXPECT_EQ(render_snapshot.meshes.front().entity_id, object.entity);
    const auto submission = submit_overlay();
    EXPECT_TRUE(submission.selection_source_refreshed);
    EXPECT_TRUE(submission.persistent_submitted);
    const auto positions = rendered_positions();
    const auto wires = submitted_wires();
    EXPECT_NE(positions, previous_positions);
    EXPECT_NE(wires, previous_wires);
    ASSERT_FALSE(wires.empty());
    for (const auto &wire : wires) {
      for (const auto offset : {0u, 3u}) {
        const QVec3 endpoint{wire[offset], wire[offset + 1u], wire[offset + 2u]};
        EXPECT_TRUE(std::ranges::any_of(positions, [&](const auto &position) {
          return length_squared(endpoint - QVec3{position[0], position[1], position[2]})
              < 1.0e-12F;
        })) << "Submitted Bevel wire is stale relative to render geometry";
      }
    }
    EXPECT_EQ(session.history().undo_count(), history_count);
    previous_positions = positions;
    previous_wires = wires;
    ASSERT_NO_FATAL_FAILURE(expect_unchanged());
  }
  EXPECT_NE(render_snapshot.meshes.front().mesh.indices.size(), initial_indices);
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  process_frame();
  render_snapshot = build_render_scene_snapshot(
      session.scene(), session.assets(), {.previous = &render_snapshot});
  EXPECT_EQ(rendered_positions(), original_positions);
  ASSERT_TRUE(submit_overlay().persistent_submitted);
  EXPECT_EQ(submitted_wires(), original_wires);
  EXPECT_EQ(session.history().undo_count(), history_count);
  ASSERT_NO_FATAL_FAILURE(expect_unchanged());
}

TEST(SelectedMeshOutlinePresentationTests,
     RejectedFaceCollapseKeepsRetainedWiresAndAllowsDragRecovery) {
  for (const auto [cancel, grid_snap] : {
           std::pair{false, false}, std::pair{true, false},
           std::pair{false, true}, std::pair{true, true}}) {
    SCOPED_TRACE(cancel);
    SCOPED_TRACE(grid_snap);
    const float fixed_x = grid_snap ? -8.0F : -1.0F;
    const float initial_x = grid_snap ? 6.0F : 1.0F;
    const float recovered_x = grid_snap ? 0.0F : -0.5F;
    ExtrudeOutlineHarness harness;
    auto &modeling = harness.session.modeling();
    const auto created = harness.editor.modeling().create_box(
        {.min = {fixed_x, -1.0F, -1.0F}, .max = {initial_x, 1.0F, 1.0F}},
        "Retained face drag");
    ASSERT_TRUE(created.ok()) << created.error_message();
    harness.object = created.value();
    const auto *binding = harness.session.scene().mesh(harness.object.entity);
    ASSERT_NE(binding, nullptr);
    const auto *asset = harness.session.assets().mesh(binding->mesh);
    ASSERT_NE(asset, nullptr);
    ASSERT_NE(asset->authored_polygon_document, nullptr);
    const auto &document = *asset->authored_polygon_document;
    const auto face = std::ranges::find_if(document.faces, [&](const auto &item) {
      return std::ranges::all_of(item.vertices, [&](auto id) {
        const auto *vertex = quader_poly::find_vertex(document, id);
        return vertex != nullptr && vertex->position.x == initial_x;
      });
    });
    ASSERT_NE(face, document.faces.end());
    ASSERT_TRUE(enter_selection_context(harness.editor, QuaderSelectionMode::Face).ok());
    ASSERT_TRUE(harness.editor.selection().select_faces(
        harness.object, {static_cast<QuaderElementId>(face->id)}).ok());

    SceneRenderBridge bridge;
    OverlayRenderableCache overlay_cache;
    std::vector<RenderToolPreviewMeshRecord> published;
    std::vector<RenderToolPreviewMeshRecord> accepted_coordinates;
    std::size_t publications = 0u;
    bool position_stream_changed = false;
    const QuaderRenderApiRuntimeAccess::ToolPreviewScenePublisher publisher =
        [&](RenderToolPreviewSceneSnapshot records) {
          ++publications;
          published = std::move(records.meshes);
          accepted_coordinates = std::move(records.accepted_overlay_coordinates);
          position_stream_changed = overlay_cache.set_transform_position_streams(
              published.empty() ? accepted_coordinates : published,
              published.empty() && !accepted_coordinates.empty());
          return ToolPreviewSubmissionResult::Published;
        };
    const auto sync_preview = [&] {
      return QuaderRenderApiRuntimeAccess::sync_tool_previews(
          harness.editor.render(), bridge, {}, publisher);
    };
    const auto submit = [&] {
      EXPECT_TRUE(sync_preview().ok());
      const auto result = harness.submit();
      if (result.persistent_submitted) {
        (void)overlay_cache.set_overlay_frame(harness.target.persistent, {});
      }
      return result;
    };
    // Plain transforms deliberately retain semantic baseline coordinates. The
    // backend deforms their generated bindings using the independently retained
    // packed preview stream, so verify those two actual published inputs together.
    const auto wires = [&] {
      std::vector<std::array<float, 4u>> positions;
      for (const auto layer : {ViewportOverlayLayer::SourceWire,
                              ViewportOverlayLayer::SelectedFaceEdge}) {
        const auto plan = OverlayRenderableBuilder::build_layer(
            harness.target.persistent, {}, layer);
        EXPECT_FALSE(plan.edit_wire_batches.empty());
        for (const auto &batch : plan.edit_wire_batches) {
          EXPECT_FALSE(batch.mesh.position_stream_bindings.empty());
          for (const auto &binding : batch.mesh.position_stream_bindings) {
            EXPECT_EQ(binding.object_id, harness.object.modeling_object_id);
            EXPECT_GE(binding.vertex_id, 0);
            EXPECT_GT(binding.vertex_count, 0u);
            EXPECT_LE(binding.first_vertex + binding.vertex_count,
                      batch.mesh.vertices.size());
            QVec3 position{binding.base_position.x, binding.base_position.y,
                           binding.base_position.z};
            for (const auto &preview : published.empty() ? accepted_coordinates : published) {
              if (preview.source_object_id != binding.object_id) continue;
              EXPECT_TRUE(preview.dynamic_positions);
              EXPECT_NE(preview.layout_key, 0u);
              EXPECT_NE(preview.dynamic_position_payload, nullptr);
              if (!preview.dynamic_position_payload) continue;
              const auto &vertices = preview.mesh.geometry().vertices;
              const auto found = std::ranges::find(
                  vertices, static_cast<std::uint32_t>(binding.vertex_id),
                  &MeshVertex::source_vertex_id);
              EXPECT_NE(found, vertices.end());
              if (found == vertices.end()) continue;
              const auto index = static_cast<std::size_t>(found - vertices.begin());
              EXPECT_LT(index, preview.dynamic_position_payload->size());
              if (index >= preview.dynamic_position_payload->size()) continue;
              position = transform_point(
                  preview.transform, (*preview.dynamic_position_payload)[index]);
            }
            // Incident-face supports drive ribbon depth but are not endpoints.
            if (binding.stream == OverlayPositionStream::Primary ||
                binding.stream == OverlayPositionStream::Secondary) {
              positions.push_back({static_cast<float>(layer), position.x,
                                   position.y, position.z});
            }
          }
        }
      }
      return positions;
    };
    const auto expect_face_position = [&](float x) {
      std::size_t selected_edges = 0u;
      std::size_t source_edges = 0u;
      for (const auto &position : wires()) {
        if (position[0] == static_cast<float>(ViewportOverlayLayer::SelectedFaceEdge)) {
          ++selected_edges;
          EXPECT_FLOAT_EQ(position[1], x);
        } else {
          ++source_edges;
          EXPECT_TRUE(position[1] == fixed_x || position[1] == x)
              << "Stale source wire endpoint at " << position[1];
        }
      }
      EXPECT_GT(selected_edges, 0u);
      EXPECT_GT(source_edges, 0u);
    };
    const auto expect_unchanged = [&] {
      const auto count = publications;
      for (int frame = 0; frame < 5; ++frame) {
        ASSERT_TRUE(sync_preview().ok());
        EXPECT_FALSE(overlay_cache.set_transform_position_streams(
            published.empty() ? accepted_coordinates : published,
            published.empty() && !accepted_coordinates.empty()));
      }
      EXPECT_EQ(publications, count);
      ASSERT_NO_FATAL_FAILURE(harness.expect_unchanged());
    };
    ASSERT_TRUE(submit().persistent_submitted);
    expect_face_position(initial_x);
    ASSERT_NO_FATAL_FAILURE(expect_unchanged());
    auto begin = test::x_move_drag_begin({
        .pivot = {initial_x, 0.0F, 0.0F}, .grid_snap_enabled = grid_snap});
    begin.ray.origin.x = initial_x;
    begin.grid_size = 8.0;
    begin.vertex_snap_radius = 0.0;
    ASSERT_TRUE(modeling.begin_transform_drag(begin).ok);
    const auto drag_to = [&](float x) {
      auto update = test::x_move_drag_update((x - initial_x) * 60.0F);
      // The drag retains its axis even when the pointer leaves the box silhouette.
      update.screen_position.y = 180.0F;
      update.ray.origin = {x, 3.0F, 5.0F};
      update.grid_size = 8.0;
      update.grid_snap_enabled = grid_snap;
      update.vertex_snap_radius = 0.0;
      return modeling.update_transform_drag(update);
    };
    const auto durable = harness.source_stamp();
    bool first_sample = true;
    std::uint64_t retained_builds = 0U;
    std::uint64_t retained_submissions = 0U;
    const std::array samples = grid_snap
        ? std::array{std::pair{7.0F, 8.0F}, std::pair{13.0F, 16.0F}}
        : std::array{std::pair{0.5F, 0.5F}, std::pair{0.0F, 0.0F}};
    for (const auto [raw_x, snapped_x] : samples) {
      const auto moved = drag_to(raw_x);
      ASSERT_TRUE(moved.ok) << moved.message;
      EXPECT_FLOAT_EQ(moved.move_delta.x, snapped_x - initial_x);
      const auto submission = submit();
      EXPECT_EQ(submission.persistent_submitted, first_sample);
      EXPECT_EQ(submission.selection_source_refreshed, first_sample);
      if (first_sample) {
        retained_builds = harness.provider.builds;
        retained_submissions = harness.target.submissions;
        first_sample = false;
      } else {
        EXPECT_EQ(harness.provider.builds, retained_builds);
        EXPECT_EQ(harness.target.submissions, retained_submissions);
      }
      ASSERT_EQ(published.size(), 1u);
      expect_face_position(snapped_x);
      const auto stamp = harness.source_stamp();
      EXPECT_EQ(stamp.content_revision, durable.content_revision);
      EXPECT_EQ(stamp.scene_revision, durable.scene_revision);
      EXPECT_EQ(stamp.asset_revision, durable.asset_revision);
      ASSERT_NO_FATAL_FAILURE(expect_unchanged());
    }
    const auto accepted_wires = wires();
    for (int sample = 0; sample < 2; ++sample) {
      const auto rejected = drag_to(fixed_x);
      ASSERT_FALSE(rejected.ok);
      ASSERT_TRUE(modeling.transform_drag_active());
      (void)submit();
      ASSERT_EQ(published.size(), 1u);
      EXPECT_EQ(wires(), accepted_wires);
      expect_face_position(samples.back().second);
      ASSERT_NO_FATAL_FAILURE(expect_unchanged());
    }
    if (cancel) {
      if (grid_snap) {
        ASSERT_TRUE(drag_to(initial_x).ok);
        (void)submit();
        expect_face_position(initial_x);
        ASSERT_NO_FATAL_FAILURE(expect_unchanged());
        ASSERT_TRUE(drag_to(samples.front().first).ok);
        (void)submit();
        expect_face_position(samples.front().second);
      }
      ASSERT_TRUE(modeling.cancel_transform_drag().ok);
      ASSERT_TRUE(submit().persistent_submitted);
      EXPECT_TRUE(published.empty());
      EXPECT_TRUE(accepted_coordinates.empty());
      expect_face_position(initial_x);
    } else {
      ASSERT_TRUE(drag_to(recovered_x).ok);
      EXPECT_FALSE(submit().persistent_submitted);
      expect_face_position(recovered_x);
      EXPECT_NE(wires(), accepted_wires);
      ASSERT_NO_FATAL_FAILURE(expect_unchanged());
      const auto before_commit_work =
          modeling.selection_overlay_geometry_work_counters();
      ASSERT_TRUE(modeling.commit_transform_drag(harness.session.history()).ok);
      (void)submit();
      EXPECT_TRUE(published.empty());
      ASSERT_EQ(accepted_coordinates.size(), 1u);
      EXPECT_FALSE(position_stream_changed);
      EXPECT_FALSE(modeling.transient_transform_preview_active());
      EXPECT_TRUE(modeling.tool_preview_meshes().empty());
      expect_face_position(recovered_x);
      const auto committed_work = modeling.selection_overlay_geometry_work_counters();
      EXPECT_EQ(committed_work.geometry_build_count,
                before_commit_work.geometry_build_count);
      EXPECT_EQ(committed_work.geometry_position_refresh_count,
                before_commit_work.geometry_position_refresh_count);
      EXPECT_EQ(committed_work.semantic_aggregate_build_count,
                before_commit_work.semantic_aggregate_build_count);
      EXPECT_EQ(committed_work.world_projection_build_count,
                before_commit_work.world_projection_build_count);
      EXPECT_GT(committed_work.accepted_coordinate_handoff_count,
                before_commit_work.accepted_coordinate_handoff_count);
      ASSERT_TRUE(harness.editor.history().undo().ok());
      ASSERT_TRUE(submit().persistent_submitted);
      EXPECT_TRUE(accepted_coordinates.empty());
      expect_face_position(initial_x);
      ASSERT_TRUE(harness.editor.history().redo().ok());
      ASSERT_TRUE(submit().persistent_submitted);
      expect_face_position(recovered_x);
      EXPECT_EQ(modeling.selection_overlay_geometry_work_counters().geometry_build_count,
                before_commit_work.geometry_build_count);
    }
    ASSERT_FALSE(modeling.transform_drag_active());
    ASSERT_NO_FATAL_FAILURE(expect_unchanged());
  }
}

TEST(SelectedMeshOutlinePresentationTests,
     TangentialFaceExtrusionSubmitsCurrentDiagnosticEdgesThroughHistory) {
  for (const bool cancel : {false, true}) {
    SCOPED_TRACE(cancel);
    ExtrudeOutlineHarness harness;
    auto &modeling = harness.session.modeling();
    quader_poly::Document document;
    const std::array vertices{
        quader_poly::add_vertex(document, {0, 0, 0}),
        quader_poly::add_vertex(document, {1, 0, 0}),
        quader_poly::add_vertex(document, {1, 1, 0}),
        quader_poly::add_vertex(document, {0, 1, 0})};
    const auto face = quader_poly::add_face(document, vertices);
    const auto created = harness.editor.modeling().create_mesh(
        QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)),
        {.name = "Tangential extrusion diagnostics"});
    ASSERT_TRUE(created.ok()) << created.error_message();
    harness.object = created.value();
    ASSERT_TRUE(enter_selection_context(harness.editor, QuaderSelectionMode::Face).ok());
    ASSERT_TRUE(harness.editor.selection().select_faces(
        harness.object, {static_cast<QuaderElementId>(face)}).ok());
    ASSERT_TRUE(harness.editor.tools().activate(ViewportToolId::Move).ok());

    using Segment = std::array<float, 6>;
    const auto segment = [](QVec3 start, QVec3 end) {
      std::array<float, 3> first{start.x, start.y, start.z};
      std::array<float, 3> second{end.x, end.y, end.z};
      if (second < first) std::swap(first, second);
      return Segment{first[0], first[1], first[2], second[0], second[1], second[2]};
    };
    const auto diagnostic_segments = [&] {
      std::vector<Segment> result;
      // Shift extrusion publishes each candidate through the scene projection
      // and may assign new cap IDs. Every retained line must reference that
      // current candidate, including after commit and history replay.
      const auto *binding = harness.session.scene().mesh(harness.object.entity);
      EXPECT_NE(binding, nullptr);
      if (binding == nullptr) return result;
      const auto *candidate = harness.session.assets().mesh(binding->mesh);
      EXPECT_NE(candidate, nullptr);
      if (candidate == nullptr) return result;
      EXPECT_NE(candidate->authored_polygon_document, nullptr);
      if (candidate->authored_polygon_document == nullptr) return result;
      const auto &authored = *candidate->authored_polygon_document;
      for (const auto &line : harness.target.persistent.lines) {
        if (line.layer != ViewportOverlayLayer::DiagnosticEdge) continue;
        EXPECT_EQ(line.element.object_id, harness.object.modeling_object_id);
        EXPECT_GE(line.element.edge_a, 0);
        EXPECT_LT(line.element.edge_a, line.element.edge_b);
        EXPECT_GE(line.element.face_id, 0);
        EXPECT_EQ(line.style_role, ViewportOverlayStyleRole::DiagnosticEdge);
        EXPECT_EQ(line.color, rgba8(255, 0, 0));
        const auto *first = quader_poly::find_vertex(
            authored, static_cast<quader_poly::ElementId>(line.element.edge_a));
        const auto *second = quader_poly::find_vertex(
            authored, static_cast<quader_poly::ElementId>(line.element.edge_b));
        const auto *incident = quader_poly::find_face(
            authored, static_cast<quader_poly::ElementId>(line.element.face_id));
        EXPECT_NE(first, nullptr);
        EXPECT_NE(second, nullptr);
        EXPECT_NE(incident, nullptr);
        if (first != nullptr && second != nullptr) {
          EXPECT_EQ(segment(line.start, line.end),
                    segment(first->position, second->position));
        }
        result.push_back(segment(line.start, line.end));
      }
      std::ranges::sort(result);
      return result;
    };
    const auto expect_diagnostics = [&](float offset) {
      // A tangential extrusion collapses exactly two connector quads. Their
      // authored boundaries remain visible although their fills cannot triangulate.
      std::vector<Segment> expected;
      for (const float y : {0.0F, 1.0F}) {
        expected.push_back(segment({0, y, 0}, {1, y, 0}));
        expected.push_back(segment({1, y, 0}, {1 + offset, y, 0}));
        expected.push_back(segment({1 + offset, y, 0}, {offset, y, 0}));
        expected.push_back(segment({offset, y, 0}, {0, y, 0}));
      }
      std::ranges::sort(expected);
      EXPECT_EQ(diagnostic_segments(), expected);
    };
    const auto expect_unchanged = [&] {
      const auto work = modeling.selection_overlay_geometry_work_counters();
      ASSERT_NO_FATAL_FAILURE(harness.expect_unchanged());
      const auto after = modeling.selection_overlay_geometry_work_counters();
      EXPECT_EQ(after.geometry_build_count, work.geometry_build_count);
      EXPECT_EQ(after.face_triangulation_count, work.face_triangulation_count);
      EXPECT_EQ(after.diagnostic_face_triangulation_count,
                work.diagnostic_face_triangulation_count);
      EXPECT_EQ(after.geometry_position_refresh_count,
                work.geometry_position_refresh_count);
      EXPECT_EQ(after.semantic_aggregate_build_count, work.semantic_aggregate_build_count);
      EXPECT_EQ(after.world_projection_build_count, work.world_projection_build_count);
    };
    ASSERT_TRUE(harness.submit().persistent_submitted);
    EXPECT_TRUE(diagnostic_segments().empty());
    ASSERT_NO_FATAL_FAILURE(expect_unchanged());
    const auto undo_count = harness.session.history().undo_count();
    auto begin = test::x_move_drag_begin({
        .operation = ModelingTransformOperation::Extrude,
        .grid_snap_enabled = false});
    begin.vertex_snap_radius = 0.0;
    ASSERT_TRUE(modeling.begin_transform_drag(begin).ok);
    const auto drag_to = [&](float offset) {
      auto update = test::x_move_drag_update(offset * 60.0F);
      update.ray.origin.x = offset;
      update.grid_snap_enabled = false;
      update.vertex_snap_radius = 0.0;
      return modeling.update_transform_drag(update);
    };
    ViewportOverlaySelectionSourceStamp durable;
    bool first_preview = true;
    for (const float offset : {0.5F, 0.75F, 0.875F}) {
      SCOPED_TRACE(offset);
      const auto updated = drag_to(offset);
      ASSERT_TRUE(updated.ok) << updated.message;
      ASSERT_TRUE(updated.active);
      ASSERT_TRUE(updated.changed);
      ASSERT_EQ(updated.operation, ModelingTransformOperation::Extrude);
      ASSERT_FLOAT_EQ(updated.move_delta.x, offset);
      const auto stamp = harness.source_stamp();
      if (first_preview) {
        durable = stamp;
        first_preview = false;
      } else {
        EXPECT_EQ(stamp.content_revision, durable.content_revision);
        EXPECT_EQ(stamp.selection_revision, durable.selection_revision);
        EXPECT_GT(stamp.asset_revision, durable.asset_revision);
      }
      const auto submitted = harness.submit();
      EXPECT_TRUE(submitted.selection_source_refreshed);
      EXPECT_TRUE(submitted.persistent_submitted);
      ASSERT_NO_FATAL_FAILURE(expect_diagnostics(offset));
      EXPECT_EQ(harness.session.history().undo_count(), undo_count);
      ASSERT_NO_FATAL_FAILURE(expect_unchanged());
    }

    ASSERT_TRUE(drag_to(0.625F).ok);
    ASSERT_TRUE(harness.submit().persistent_submitted);
    ASSERT_NO_FATAL_FAILURE(expect_diagnostics(0.625F));
    const auto accepted = diagnostic_segments();
    if (cancel) {
      ASSERT_TRUE(modeling.cancel_transform_drag().ok);
      ASSERT_TRUE(harness.submit().persistent_submitted);
      EXPECT_TRUE(diagnostic_segments().empty());
      EXPECT_EQ(harness.session.history().undo_count(), undo_count);
    } else {
      const auto committed = modeling.commit_transform_drag(harness.session.history());
      ASSERT_TRUE(committed.ok) << committed.message;
      ASSERT_TRUE(committed.changed);
      (void)harness.submit();
      EXPECT_EQ(diagnostic_segments(), accepted);
      EXPECT_EQ(harness.session.history().undo_count(), undo_count + 1U);
      ASSERT_TRUE(harness.editor.history().undo().ok());
      ASSERT_TRUE(harness.submit().persistent_submitted);
      EXPECT_TRUE(diagnostic_segments().empty());
      ASSERT_TRUE(harness.editor.history().redo().ok());
      ASSERT_TRUE(harness.submit().persistent_submitted);
      EXPECT_EQ(diagnostic_segments(), accepted);
    }
    EXPECT_TRUE(modeling.tool_preview_meshes().empty());
    EXPECT_FALSE(modeling.transform_drag_active());
    ASSERT_NO_FATAL_FAILURE(expect_unchanged());
  }
}

TEST(SelectedMeshOutlinePresentationTests,
     ConnectingWallFromTangentialExtrusionCanMoveOnEveryAxis) {
  constexpr std::array kAxes{
      std::pair{ModelingTransformAxis::X, QVec3{1, 0, 0}},
      std::pair{ModelingTransformAxis::Y, QVec3{0, 1, 0}},
      std::pair{ModelingTransformAxis::Z, QVec3{0, 0, 1}}};
  for (const auto &[axis, direction] : kAxes) {
    SCOPED_TRACE(static_cast<int>(axis));
    for (const bool cancel : {false, true}) {
      SCOPED_TRACE(cancel);
      ExtrudeOutlineHarness harness;
      auto &modeling = harness.session.modeling();
      quader_poly::Document document;
      const std::array vertices{
          quader_poly::add_vertex(document, {0, 0, 0}),
          quader_poly::add_vertex(document, {0, 2, 0}),
          quader_poly::add_vertex(document, {0, 2, 2}),
          quader_poly::add_vertex(document, {0, 0, 2})};
      const auto cap_face = quader_poly::add_face(document, vertices);
      const auto created = harness.editor.modeling().create_mesh(
          QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)),
          {.name = "Move an extruded connecting wall"});
      ASSERT_TRUE(created.ok()) << created.error_message();
      harness.object = created.value();
      ASSERT_TRUE(enter_selection_context(harness.editor, QuaderSelectionMode::Face).ok());
      ASSERT_TRUE(harness.editor.selection().select_faces(
          harness.object, {static_cast<QuaderElementId>(cap_face)}).ok());
      ASSERT_TRUE(harness.editor.tools().activate(ViewportToolId::Move).ok());
      const auto authored_document = [&]() -> quader_poly::Document {
        const auto *binding = harness.session.scene().mesh(harness.object.entity);
        if (binding == nullptr) {
          ADD_FAILURE() << "Missing mesh binding";
          return {};
        }
        const auto *mesh = harness.session.assets().mesh(binding->mesh);
        if (mesh == nullptr || !mesh->authored_polygon_document) {
          ADD_FAILURE() << "Missing authored mesh";
          return {};
        }
        return *mesh->authored_polygon_document;
      };
      const auto begin_move = [&](ModelingTransformAxis move_axis,
                                  QVec3 move_direction,
                                  ModelingTransformOperation operation) {
        auto request = test::transform_drag_begin({
            .axis = move_axis,
            .operation = operation,
            .world_direction = move_direction,
            .grid_snap_enabled = false});
        request.vertex_snap_radius = 0.0;
        if (move_axis == ModelingTransformAxis::Z) {
          request.ray = {.origin = {5, 0, 0}, .direction = {-1, 0, 0}};
        }
        return modeling.begin_transform_drag(request);
      };
      const auto update_move = [&](ModelingTransformAxis move_axis,
                                   QVec3 move_direction, float distance) {
        auto request = test::transform_drag_update(distance * 60.0F);
        request.ray.origin = QVec3{0, 0, 5} + move_direction * distance;
        request.grid_snap_enabled = false;
        request.vertex_snap_radius = 0.0;
        if (move_axis == ModelingTransformAxis::Z) {
          request.ray = {.origin = {5, 0, distance}, .direction = {-1, 0, 0}};
        }
        return modeling.update_transform_drag(request);
      };
      ASSERT_TRUE(begin_move(ModelingTransformAxis::Z, {0, 0, 1},
                             ModelingTransformOperation::Extrude).ok);
      const auto extruded = update_move(ModelingTransformAxis::Z, {0, 0, 1}, 3.0F);
      ASSERT_TRUE(extruded.ok) << extruded.message;
      ASSERT_FLOAT_EQ(extruded.move_delta.z, 3.0F);
      ASSERT_TRUE(modeling.commit_transform_drag(harness.session.history()).ok);
      const auto baseline = authored_document();
      ASSERT_EQ(baseline.faces.size(), 5U);
      ASSERT_EQ(baseline.vertices.size(), 8U);
      // This is the broad connecting wall at the original side of the panel,
      // not its moved cap. It borders both collapsed horizontal connectors.
      const auto wall = std::ranges::find_if(baseline.faces, [&](const auto &face) {
        return face.id != cap_face &&
            std::ranges::find(face.vertices, vertices[0]) != face.vertices.end() &&
            std::ranges::find(face.vertices, vertices[1]) != face.vertices.end();
      });
      ASSERT_NE(wall, baseline.faces.end());
      const auto selected_vertices = wall->vertices;
      ASSERT_EQ(selected_vertices.size(), 4U);
      ASSERT_TRUE(harness.editor.selection().select_faces(
          harness.object, {static_cast<QuaderElementId>(wall->id)}).ok());
      SceneRenderBridge bridge;
      OverlayRenderableCache overlay_cache;
      std::vector<RenderToolPreviewMeshRecord> published;
      std::vector<RenderToolPreviewMeshRecord> accepted_coordinates;
      std::size_t publications = 0U;
      const QuaderRenderApiRuntimeAccess::ToolPreviewScenePublisher publisher =
          [&](RenderToolPreviewSceneSnapshot records) {
            ++publications;
            published = std::move(records.meshes);
            accepted_coordinates = std::move(records.accepted_overlay_coordinates);
            (void)overlay_cache.set_transform_position_streams(
                published.empty() ? accepted_coordinates : published,
                published.empty() && !accepted_coordinates.empty());
            return ToolPreviewSubmissionResult::Published;
          };
      const auto submit = [&] {
        EXPECT_TRUE(QuaderRenderApiRuntimeAccess::sync_tool_previews(
            harness.editor.render(), bridge, {}, publisher).ok());
        const auto result = harness.submit();
        if (result.persistent_submitted) {
          (void)overlay_cache.set_overlay_frame(harness.target.persistent, {});
        }
        return result;
      };
      ASSERT_TRUE(submit().persistent_submitted);
      EXPECT_EQ(std::ranges::count(harness.target.persistent.lines,
                                  ViewportOverlayLayer::DiagnosticEdge,
                                  &ViewportOverlayLine::layer), 8);
      const auto undo_count = harness.session.history().undo_count();
      const auto expected_position = [&](quader_poly::ElementId id, QVec3 delta) {
        const auto *source = quader_poly::find_vertex(baseline, id);
        EXPECT_NE(source, nullptr);
        if (source == nullptr) return QVec3{};
        return source->position +
            (std::ranges::find(selected_vertices, id) != selected_vertices.end()
                 ? delta : QVec3{});
      };
      const auto expect_preview = [&](QVec3 delta) {
        ASSERT_EQ(published.size(), 1U);
        const auto &preview = published.front();
        const auto &compiled = preview.mesh.geometry().vertices;
        ASSERT_FALSE(compiled.empty());
        if (preview.dynamic_position_payload) {
          ASSERT_EQ(preview.dynamic_position_payload->size(), compiled.size());
        }
        for (std::size_t index = 0; index < compiled.size(); ++index) {
          const auto expected = expected_position(compiled[index].source_vertex_id, delta);
          const auto actual = preview.dynamic_position_payload
              ? (*preview.dynamic_position_payload)[index] : compiled[index].position;
          EXPECT_FLOAT_EQ(actual.x, expected.x);
          EXPECT_FLOAT_EQ(actual.y, expected.y);
          EXPECT_FLOAT_EQ(actual.z, expected.z);
        }
      };
      const auto expect_submitted_wires = [&](QVec3 delta) {
        // Plain Move retains semantic coordinates and supplies current GPU
        // positions separately. Validate those actual published inputs together.
        const auto &coordinates = published.empty() ? accepted_coordinates : published;
        const auto unordered_endpoints = [](const std::array<QVec3, 2> &endpoints) {
          std::array points{
              std::array{endpoints[0].x, endpoints[0].y, endpoints[0].z},
              std::array{endpoints[1].x, endpoints[1].y, endpoints[1].z}};
          std::ranges::sort(points);
          return points;
        };
        for (const auto layer : {ViewportOverlayLayer::SourceWire,
                                ViewportOverlayLayer::SelectedFaceEdge}) {
          const auto plan = OverlayRenderableBuilder::build_layer(
              harness.target.persistent, {}, layer);
          std::size_t segments = 0U;
          for (const auto &batch : plan.edit_wire_batches) {
            const auto &bindings = batch.mesh.position_stream_bindings;
            for (const auto &primary : bindings) {
              if (primary.stream != OverlayPositionStream::Primary) continue;
              const auto secondary = std::ranges::find_if(bindings, [&](const auto &binding) {
                return binding.stream == OverlayPositionStream::Secondary &&
                    binding.first_vertex == primary.first_vertex &&
                    binding.vertex_count == primary.vertex_count;
              });
              ASSERT_NE(secondary, bindings.end());
              ++segments;
              const std::array endpoint_bindings{&primary, &*secondary};
              std::array<QVec3, 2> actual;
              std::array<QVec3, 2> expected;
              for (std::size_t endpoint = 0; endpoint < endpoint_bindings.size(); ++endpoint) {
                const auto &binding = *endpoint_bindings[endpoint];
                ASSERT_EQ(binding.object_id, harness.object.modeling_object_id);
                ASSERT_GE(binding.vertex_id, 0);
                actual[endpoint] = {binding.base_position.x, binding.base_position.y,
                                    binding.base_position.z};
                for (const auto &preview : coordinates) {
                  if (preview.source_object_id != binding.object_id) continue;
                  ASSERT_NE(preview.dynamic_position_payload, nullptr);
                  const auto &compiled = preview.mesh.geometry().vertices;
                  const auto vertex = std::ranges::find(
                      compiled, static_cast<std::uint32_t>(binding.vertex_id),
                      &MeshVertex::source_vertex_id);
                  ASSERT_NE(vertex, compiled.end());
                  const auto index = static_cast<std::size_t>(vertex - compiled.begin());
                  ASSERT_LT(index, preview.dynamic_position_payload->size());
                  actual[endpoint] = transform_point(
                      preview.transform, (*preview.dynamic_position_payload)[index]);
                }
                expected[endpoint] = expected_position(
                    static_cast<quader_poly::ElementId>(binding.vertex_id), delta);
              }
              // Authored edge identity is undirected; source-face winding may
              // reverse the ribbon's start/end pair when no stream is active.
              EXPECT_EQ(unordered_endpoints(actual), unordered_endpoints(expected));
            }
          }
          EXPECT_GT(segments, 0U);
        }
      };
      const auto expect_unchanged = [&] {
        const auto count = publications;
        const auto builds = harness.provider.builds;
        const auto submissions = harness.target.submissions;
        const auto work = modeling.selection_overlay_geometry_work_counters();
        for (int frame = 0; frame < 5; ++frame) {
          const auto unchanged = submit();
          EXPECT_FALSE(unchanged.selection_source_refreshed);
          EXPECT_FALSE(unchanged.persistent_submitted);
          EXPECT_FALSE(overlay_cache.set_transform_position_streams(
              published.empty() ? accepted_coordinates : published,
              published.empty() && !accepted_coordinates.empty()));
        }
        EXPECT_EQ(publications, count);
        EXPECT_EQ(harness.provider.builds, builds);
        EXPECT_EQ(harness.target.submissions, submissions);
        const auto after = modeling.selection_overlay_geometry_work_counters();
        EXPECT_EQ(after.geometry_build_count, work.geometry_build_count);
        EXPECT_EQ(after.face_triangulation_count, work.face_triangulation_count);
        EXPECT_EQ(after.diagnostic_face_triangulation_count,
                  work.diagnostic_face_triangulation_count);
        EXPECT_EQ(after.world_projection_build_count, work.world_projection_build_count);
      };
      const auto expect_document = [&](QVec3 delta) {
        const auto current = authored_document();
        ASSERT_EQ(current.vertices.size(), baseline.vertices.size());
        ASSERT_EQ(current.faces.size(), baseline.faces.size());
        for (const auto &vertex : current.vertices) {
          const auto expected = expected_position(vertex.id, delta);
          EXPECT_FLOAT_EQ(vertex.position.x, expected.x);
          EXPECT_FLOAT_EQ(vertex.position.y, expected.y);
          EXPECT_FLOAT_EQ(vertex.position.z, expected.z);
        }
        for (const auto &face : current.faces) {
          const auto *original = quader_poly::find_face(baseline, face.id);
          ASSERT_NE(original, nullptr);
          EXPECT_EQ(face.vertices, original->vertices);
        }
      };
      ASSERT_NO_FATAL_FAILURE(expect_submitted_wires({}));
      ASSERT_NO_FATAL_FAILURE(expect_unchanged());
      ASSERT_TRUE(begin_move(axis, direction, ModelingTransformOperation::Transform).ok);
      for (const float distance : {0.125F, 0.25F, 0.0F, 0.25F}) {
        SCOPED_TRACE(distance);
        const auto moved = update_move(axis, direction, distance);
        ASSERT_TRUE(moved.ok) << moved.message;
        ASSERT_TRUE(moved.active);
        EXPECT_EQ(moved.operation, ModelingTransformOperation::Transform);
        const auto delta = direction * distance;
        EXPECT_FLOAT_EQ(moved.move_delta.x, delta.x);
        EXPECT_FLOAT_EQ(moved.move_delta.y, delta.y);
        EXPECT_FLOAT_EQ(moved.move_delta.z, delta.z);
        (void)submit();
        ASSERT_NO_FATAL_FAILURE(expect_preview(delta));
        ASSERT_NO_FATAL_FAILURE(expect_submitted_wires(delta));
        ASSERT_NO_FATAL_FAILURE(expect_unchanged());
        EXPECT_EQ(harness.session.history().undo_count(), undo_count);
      }
      if (cancel) {
        ASSERT_TRUE(modeling.cancel_transform_drag().ok);
        ASSERT_NO_FATAL_FAILURE(expect_document({}));
        (void)submit();
        ASSERT_NO_FATAL_FAILURE(expect_submitted_wires({}));
        EXPECT_EQ(harness.session.history().undo_count(), undo_count);
      } else {
        const auto committed = modeling.commit_transform_drag(harness.session.history());
        ASSERT_TRUE(committed.ok) << committed.message;
        ASSERT_TRUE(committed.changed);
        ASSERT_NO_FATAL_FAILURE(expect_document(direction * 0.25F));
        (void)submit();
        ASSERT_NO_FATAL_FAILURE(expect_submitted_wires(direction * 0.25F));
        EXPECT_EQ(harness.session.history().undo_count(), undo_count + 1U);
        ASSERT_TRUE(harness.editor.history().undo().ok());
        ASSERT_NO_FATAL_FAILURE(expect_document({}));
        (void)submit();
        ASSERT_NO_FATAL_FAILURE(expect_submitted_wires({}));
        ASSERT_TRUE(harness.editor.history().redo().ok());
        ASSERT_NO_FATAL_FAILURE(expect_document(direction * 0.25F));
        (void)submit();
        ASSERT_NO_FATAL_FAILURE(expect_submitted_wires(direction * 0.25F));
      }
      EXPECT_FALSE(modeling.transform_drag_active());
      EXPECT_TRUE(modeling.tool_preview_meshes().empty());
      ASSERT_NO_FATAL_FAILURE(expect_unchanged());
    }
  }
}

TEST(SelectedMeshOutlinePresentationTests,
     ExtrusionPreviewSubmitsCurrentSourceAndSelectedWiresWithStableIds) {
  for (const double initial_amount : {0.0, 1.0}) {
    SCOPED_TRACE(initial_amount);
    for (const auto mode : {modeling::FaceExtrudeMode::Region,
                            modeling::FaceExtrudeMode::AlongNormals,
                            modeling::FaceExtrudeMode::Individual}) {
      SCOPED_TRACE(static_cast<int>(mode));
      ExtrudeOutlineHarness harness;
      ASSERT_NO_FATAL_FAILURE(harness.seed(mode, initial_amount));
      const auto undo_count = harness.session.history().undo_count();
      ASSERT_TRUE(harness.submit().persistent_submitted);
      ASSERT_NO_FATAL_FAILURE(harness.expect_visible_coordinates(initial_amount));
      const auto begun = initial_amount == 0.0
          ? harness.session.extrude().begin_drag()
          : harness.session.extrude().begin_adjust_drag();
      ASSERT_TRUE(begun.ok) << begun.message;
      std::vector<std::uint32_t> source_ids;
      std::vector<std::uint32_t> indices;
      const auto durable = harness.source_stamp();
      for (const double amount : {2.15, 2.1501, 3.25, 1.25}) {
        SCOPED_TRACE(amount);
        ASSERT_TRUE(harness.session.extrude().update_drag(amount).ok);
        const auto visible = harness.session.modeling().tool_preview_meshes();
        ASSERT_EQ(visible.size(), 1u);
        ASSERT_NE(visible.front().dynamic_position_payload, nullptr);
        const auto &geometry = visible.front().mesh.geometry();
        std::vector<std::uint32_t> current_ids;
        for (const auto &vertex : geometry.vertices) {
          current_ids.push_back(vertex.source_vertex_id);
        }
        if (source_ids.empty()) {
          source_ids = current_ids;
          indices = geometry.indices;
        } else {
          EXPECT_EQ(current_ids, source_ids);
          EXPECT_EQ(geometry.indices, indices);
        }
        const auto current = harness.source_stamp();
        EXPECT_EQ(current.content_revision, durable.content_revision);
        EXPECT_EQ(current.selection_revision, durable.selection_revision);
        EXPECT_EQ(current.scene_revision, durable.scene_revision);
        EXPECT_EQ(current.asset_revision, durable.asset_revision);
        const auto submitted = harness.submit();
        EXPECT_TRUE(submitted.selection_source_refreshed);
        EXPECT_TRUE(submitted.persistent_submitted);
        ASSERT_NO_FATAL_FAILURE(harness.expect_visible_coordinates(amount));
        const auto builds = harness.provider.builds;
        EXPECT_TRUE(harness.submit(1u).persistent_submitted);
        EXPECT_EQ(harness.provider.builds, builds);
        ASSERT_NO_FATAL_FAILURE(harness.expect_visible_coordinates(amount));
        ASSERT_TRUE(harness.session.extrude().update_drag(amount).ok);
        ASSERT_NO_FATAL_FAILURE(harness.expect_unchanged());
      }
      ASSERT_TRUE(harness.session.extrude().release_drag().ok);
      EXPECT_TRUE(harness.session.extrude().preview_active());
      EXPECT_EQ(harness.session.history().undo_count(), undo_count);
      (void)harness.submit();
      ASSERT_NO_FATAL_FAILURE(harness.expect_visible_coordinates(1.25));
      ASSERT_NO_FATAL_FAILURE(harness.expect_unchanged());
      ASSERT_TRUE(harness.editor.tools().commit_active_tool_preview().ok());
      EXPECT_FALSE(harness.session.extrude().preview_active());
      EXPECT_FALSE(harness.session.extrude().adjustable());
      EXPECT_DOUBLE_EQ(harness.session.extrude().amount(), 1.25);
      EXPECT_EQ(harness.session.history().undo_count(), undo_count + 1u);
      EXPECT_TRUE(harness.session.modeling().tool_preview_meshes().empty());
      ASSERT_TRUE(harness.submit().persistent_submitted);
      ASSERT_NO_FATAL_FAILURE(harness.expect_visible_coordinates(1.25));
      ASSERT_NO_FATAL_FAILURE(harness.expect_unchanged());
      ASSERT_TRUE(harness.editor.history().undo().ok());
      ASSERT_TRUE(harness.submit().persistent_submitted);
      ASSERT_NO_FATAL_FAILURE(harness.expect_visible_coordinates(0.0));
      ASSERT_TRUE(harness.editor.history().redo().ok());
      ASSERT_TRUE(harness.submit().persistent_submitted);
      ASSERT_NO_FATAL_FAILURE(harness.expect_visible_coordinates(1.25));
      ASSERT_NO_FATAL_FAILURE(harness.expect_unchanged());
    }
  }
}

TEST(SelectedMeshOutlinePresentationTests,
     ExtrusionWiresRetirePreviewCoordinatesOnZeroCancelAndFailure) {
  ExtrudeOutlineHarness harness;
  ASSERT_NO_FATAL_FAILURE(harness.seed(modeling::FaceExtrudeMode::Region, 0.0));
  (void)harness.submit();
  auto &tool = harness.session.extrude();
  for (const bool fail : {false, true}) {
    SCOPED_TRACE(fail);
    ASSERT_TRUE(tool.set_amount(1.0).ok);
    // Reusing the remembered value after Cancel needs an explicit activation.
    ASSERT_TRUE(tool.activate().ok);
    ASSERT_TRUE(tool.begin_adjust_drag().ok);
    ASSERT_TRUE(tool.update_drag(2.15).ok);
    (void)harness.submit();
    ASSERT_NO_FATAL_FAILURE(harness.expect_visible_coordinates(2.15));
    if (fail) {
      harness.session.modeling().set_modeling_sdk_projection_failure_for_tests(
          ModelingSdkProjectionFailurePoint::AfterSdkMutation);
      const auto rejected = tool.update_drag(3.25);
      harness.session.modeling().set_modeling_sdk_projection_failure_for_tests(
          ModelingSdkProjectionFailurePoint::None);
      ASSERT_FALSE(rejected.ok);
      ASSERT_TRUE(tool.refresh().ok);
      EXPECT_FALSE(tool.preview_active());
      EXPECT_TRUE(harness.submit().persistent_submitted);
      ASSERT_NO_FATAL_FAILURE(harness.expect_visible_coordinates(0.0));
      ASSERT_NO_FATAL_FAILURE(harness.expect_unchanged());
    } else {
      ASSERT_TRUE(tool.update_drag(0.0).ok);
      EXPECT_TRUE(harness.submit().persistent_submitted);
      ASSERT_NO_FATAL_FAILURE(harness.expect_visible_coordinates(0.0));
      ASSERT_NO_FATAL_FAILURE(harness.expect_unchanged());
      ASSERT_TRUE(tool.update_drag(-2.15).ok);
      EXPECT_TRUE(harness.submit().persistent_submitted);
      ASSERT_NO_FATAL_FAILURE(harness.expect_visible_coordinates(-2.15));
    }
    ASSERT_TRUE(tool.cancel_drag().ok);
    const auto retired = harness.submit();
    if (!fail) EXPECT_TRUE(retired.persistent_submitted);
    ASSERT_NO_FATAL_FAILURE(harness.expect_visible_coordinates(0.0));
    ASSERT_NO_FATAL_FAILURE(harness.expect_unchanged());
  }
}

TEST(SelectedMeshOutlinePresentationTests,
     PendingExtrusionSettingsAndCancelReachRetainedSubmission) {
  ExtrudeOutlineHarness harness;
  ASSERT_NO_FATAL_FAILURE(harness.seed(modeling::FaceExtrudeMode::Region, 0.0));
  auto &tool = harness.session.extrude();
  ASSERT_TRUE(harness.submit().persistent_submitted);
  ASSERT_NO_FATAL_FAILURE(harness.expect_visible_coordinates(0.0));
  ASSERT_TRUE(tool.set_amount(1.0).ok);
  const auto durable = harness.source_stamp();
  const auto undo_count = harness.session.history().undo_count();
  const auto redo_count = harness.session.history().redo_count();
  for (const double amount : {2.15, 2.1501, 3.25, 0.0, -1.25}) {
    SCOPED_TRACE(amount);
    ASSERT_TRUE(tool.set_amount(amount).ok);
    const auto current = harness.source_stamp();
    EXPECT_EQ(current.content_revision, durable.content_revision);
    EXPECT_EQ(current.asset_revision, durable.asset_revision);
    EXPECT_TRUE(harness.submit().persistent_submitted);
    ASSERT_NO_FATAL_FAILURE(harness.expect_visible_coordinates(amount));
    ASSERT_NO_FATAL_FAILURE(harness.expect_unchanged());
  }
  for (const double amount : {2.75, 4.25}) {
    SCOPED_TRACE(amount);
    ASSERT_TRUE(tool.begin_adjust_drag().ok);
    ASSERT_TRUE(tool.update_drag(amount).ok);
    ASSERT_TRUE(harness.submit().persistent_submitted);
    ASSERT_NO_FATAL_FAILURE(harness.expect_visible_coordinates(amount));
    const auto dragging_stamp = harness.source_stamp();

    ASSERT_TRUE(tool.release_drag().ok);
    EXPECT_FALSE(tool.dragging());
    EXPECT_TRUE(tool.preview_active());
    EXPECT_TRUE(tool.adjustable());
    const auto released_stamp = harness.source_stamp();
    EXPECT_EQ(released_stamp.content_revision, durable.content_revision);
    EXPECT_EQ(released_stamp.asset_revision, durable.asset_revision);
    EXPECT_EQ(released_stamp.selection_revision, dragging_stamp.selection_revision);
    EXPECT_EQ(released_stamp.scene_revision, dragging_stamp.scene_revision);
    EXPECT_EQ(released_stamp.overlay_signature, dragging_stamp.overlay_signature);
    EXPECT_EQ(harness.session.history().undo_count(), undo_count);
    EXPECT_EQ(harness.session.history().redo_count(), redo_count);
    ASSERT_NO_FATAL_FAILURE(harness.expect_visible_coordinates(amount));
    ASSERT_NO_FATAL_FAILURE(harness.expect_unchanged());
  }
  ASSERT_TRUE(harness.editor.tools().cancel_active_tool_preview().ok());
  EXPECT_FALSE(tool.preview_active());
  EXPECT_TRUE(harness.session.modeling().tool_preview_meshes().empty());
  EXPECT_EQ(harness.session.history().undo_count(), undo_count);
  EXPECT_EQ(harness.session.history().redo_count(), redo_count);
  EXPECT_TRUE(harness.submit().persistent_submitted);
  ASSERT_NO_FATAL_FAILURE(harness.expect_visible_coordinates(0.0));
  ASSERT_NO_FATAL_FAILURE(harness.expect_unchanged());
}

TEST(SelectedMeshOutlinePresentationTests,
     OutlinePreservesAllWireframeTopologyWithoutDuplicatingSelectionWires) {
  for (const bool selected_wire_visible : {true, false}) {
    SCOPED_TRACE(selected_wire_visible);
    OutlinePresentationHarness harness;
    ASSERT_NO_FATAL_FAILURE(harness.seed());
    const auto legacy = harness.update(false, 1u, selected_wire_visible);
    ASSERT_EQ(legacy.selected_object->meshes.size(), 1u);
    ASSERT_EQ(legacy.selected_object->instance_selection_fills.size(), 1u);
    const auto legacy_fills = plan_instance_selection_overlay_batches(*legacy.selected_object, 64u);
    EXPECT_EQ(legacy_fills.fills.occurrence_count, 1u);
    EXPECT_EQ(legacy_fills.wires.occurrence_count, 1u);
    EXPECT_EQ(plan_scene_wireframe_overlay_batches(*legacy.scene_wireframe, 64u).participant_count, 4u);
    const auto direct_payload = legacy.selected_object->meshes.front().geometry_payload;
    const auto instance_payload = legacy.selected_object->instance_selection_fills.front().geometry_payload;

    const auto outline = harness.update(true, 1u, selected_wire_visible);
    ASSERT_TRUE(outline.selected_object_changed);
    ASSERT_TRUE(outline.scene_wireframe_changed);
    ASSERT_EQ(outline.selected_object->meshes.size(), 1u);
    ASSERT_EQ(outline.selected_object->instance_selection_fills.size(), 1u);
    ASSERT_EQ(outline.selected_object->instance_selection_wires.size(), 1u);
    EXPECT_EQ(outline.selected_object->meshes.front().geometry_payload, direct_payload);
    EXPECT_EQ(outline.selected_object->instance_selection_fills.front().geometry_payload, instance_payload);
    const auto plans = plan_instance_selection_overlay_batches(*outline.selected_object, 64u);
    EXPECT_EQ(classify_instance_batch_update(legacy_fills.fills, plans.fills),
              RenderInstanceBatchUpdateKind::Unchanged);
    EXPECT_TRUE(plans.wires.chunks.empty());
    EXPECT_EQ(plans.work.wire_record_index_entry_count, 0u);
    ASSERT_EQ(outline.scene_wireframe->meshes.size(), 4u);
    for (const auto entity : {harness.selected_mesh, harness.unselected_mesh,
                              harness.selected_instance, harness.unselected_instance}) {
      EXPECT_EQ(std::ranges::count_if(outline.scene_wireframe->meshes,
          [entity](const auto &mesh) { return mesh.entity == entity; }), 1);
    }
    for (const auto &mesh : outline.scene_wireframe->meshes) {
      EXPECT_FALSE(mesh.selected);
      EXPECT_FALSE(mesh.active_selected);
      EXPECT_TRUE(mesh.has_wire_geometry());
      EXPECT_EQ(mesh.wire_segments.size(), 3u);
    }
    const auto wireframe_plan = plan_scene_wireframe_overlay_batches(*outline.scene_wireframe, 64u);
    EXPECT_EQ(wireframe_plan.participant_count, 4u);
    ASSERT_EQ(wireframe_plan.batches.chunks.size(), 1u);
    EXPECT_EQ(wireframe_plan.batches.chunks.front().key.layer_mask,
              static_cast<std::uint8_t>(ViewportOverlayLayer::WireframeXray));
    const auto builds = harness.cache.stats().scene_wireframe_build_count;
    EXPECT_FALSE(harness.update(true, 1u, selected_wire_visible).changed());
    EXPECT_EQ(harness.cache.stats().scene_wireframe_build_count, builds);

    const auto solid = harness.update(true, 0u, selected_wire_visible);
    EXPECT_EQ(plan_scene_wireframe_overlay_batches(*solid.scene_wireframe, 64u).participant_count, 0u);
    const auto wireframe_again = harness.update(true, 1u, selected_wire_visible);
    EXPECT_EQ(plan_scene_wireframe_overlay_batches(*wireframe_again.scene_wireframe, 64u).participant_count, 4u);

    const auto restored = harness.update(false, 1u, selected_wire_visible);
    EXPECT_EQ(plan_scene_wireframe_overlay_batches(*restored.scene_wireframe, 64u).participant_count, 4u);
    EXPECT_EQ(plan_instance_selection_overlay_batches(*restored.selected_object, 64u).wires.occurrence_count, 1u);
  }
}

TEST(SelectedMeshOutlinePresentationTests,
     PolicyTogglesAndStableFramesKeepOutlineGeometryCurrentWithoutReextractingFills) {
  OutlinePresentationHarness harness;
  ASSERT_NO_FATAL_FAILURE(harness.seed());
  EXPECT_EQ(harness.cache.selected_object_snapshot(), nullptr);
  const auto legacy = harness.update(false, 0u);
  const auto builds = harness.cache.stats().selected_object_build_count;
  EXPECT_EQ(plan_scene_wireframe_overlay_batches(*legacy.scene_wireframe, 64u).participant_count, 2u);
  const auto outline = harness.update(true, 0u);
  EXPECT_EQ(harness.cache.selected_object_snapshot(), outline.selected_object);
  EXPECT_EQ(harness.cache.stats().selected_object_build_count, builds);
  EXPECT_EQ(plan_scene_wireframe_overlay_batches(*outline.scene_wireframe, 64u).participant_count, 0u);
  const auto stats = harness.cache.stats();
  EXPECT_FALSE(harness.update(true, 0u).changed());
  EXPECT_EQ(harness.cache.stats().selected_object_build_count, stats.selected_object_build_count);
  EXPECT_EQ(harness.cache.stats().scene_wireframe_build_count, stats.scene_wireframe_build_count);

  ASSERT_TRUE(harness.document.set_transform(harness.selected_instance,
      TransformComponent{.location = {5.0f, 2.0f, -1.0f}}));
  ASSERT_TRUE(harness.update(true, 0u).selected_object_changed);
  const auto *moved = harness.cache.selected_object_snapshot();
  ASSERT_NE(moved, nullptr);
  ASSERT_EQ(moved->instance_selection_fills.size(), 1u);
  EXPECT_FLOAT_EQ(moved->instance_selection_fills.front().world_transform.values[12], 5.0f);
  ASSERT_TRUE(harness.document.set_selected(harness.selected_mesh, false));
  ASSERT_TRUE(harness.document.set_selected(harness.unselected_mesh, true));
  const auto changed_selection = harness.update(true);
  ASSERT_EQ(changed_selection.selected_object->meshes.size(), 1u);
  EXPECT_EQ(changed_selection.selected_object->meshes.front().entity, harness.unselected_mesh);
  EXPECT_TRUE(std::ranges::any_of(changed_selection.scene_wireframe->meshes,
      [&](const auto &mesh) { return mesh.entity == harness.selected_mesh; }));
  EXPECT_FALSE(harness.update(true).changed());
  const auto restored = harness.update(false, 0u);
  EXPECT_EQ(plan_scene_wireframe_overlay_batches(*restored.scene_wireframe, 64u).participant_count, 2u);
  harness.cache.reset();
  EXPECT_EQ(harness.cache.selected_object_snapshot(), nullptr);
}

TEST(SelectedMeshOutlinePresentationTests,
     RuntimeSyncRequestPropagatesOptInAndKeepsDefaultPresentationCompatible) {
  SceneSession session;
  QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.modeling().create_box(
      QuaderBounds{.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      "Outline API Box").ok());
  SceneRenderBridge bridge;
  RenderOverlaySnapshotCache cache;
  QuaderRenderSceneOverlaySyncRequest request{
      .selected_object_overlay_visible = true,
      .scene_wireframe_layer_mask = 1u,
  };
  EXPECT_FALSE(request.selected_mesh_outline_only);
  const auto sync = [&] {
    return QuaderRenderApiRuntimeAccess::sync_scene_asset_overlays(
        editor.render(), bridge, cache, request);
  };
  ASSERT_TRUE(sync().ok());
  ASSERT_NE(cache.selected_object_snapshot(), nullptr);
  ASSERT_EQ(cache.selected_object_snapshot()->meshes.size(), 1u);
  EXPECT_FALSE(cache.selected_object_snapshot()->selected_mesh_outline_only);
  const auto payload = cache.selected_object_snapshot()->meshes.front().geometry_payload;
  const auto builds = cache.stats().selected_object_build_count;
  request.selected_mesh_outline_only = true;
  ASSERT_TRUE(sync().ok());
  EXPECT_TRUE(cache.selected_object_snapshot()->selected_mesh_outline_only);
  EXPECT_EQ(cache.selected_object_snapshot()->meshes.front().geometry_payload, payload);
  EXPECT_EQ(cache.stats().selected_object_build_count, builds);
  const auto wire_builds = cache.stats().scene_wireframe_build_count;
  ASSERT_TRUE(sync().ok());
  EXPECT_EQ(cache.stats().scene_wireframe_build_count, wire_builds);
  request.selected_mesh_outline_only = false;
  ASSERT_TRUE(sync().ok());
  EXPECT_FALSE(cache.selected_object_snapshot()->selected_mesh_outline_only);
  EXPECT_EQ(cache.stats().selected_object_build_count, builds);
}

TEST(SelectedMeshOutlinePresentationTests,
     NestedGroupsKeepCompleteWireframeTopologyAndOneSelectionOutline) {
  OutlinePresentationHarness harness;
  ASSERT_NO_FATAL_FAILURE(harness.seed());
  auto &document = harness.document;
  const auto group = document.create_entity("Group", EntityKind::Empty);
  const auto nested = document.create_entity("Nested group", EntityKind::Empty);
  ASSERT_TRUE(document.set_parent(nested, group, false));
  ASSERT_TRUE(document.set_parent(harness.selected_mesh, nested, false));
  ASSERT_TRUE(document.set_parent(harness.selected_instance, nested, false));
  document.clear_selection();
  ASSERT_TRUE(document.set_selected(group, true));
  ASSERT_TRUE(document.set_selected(nested, true));
  const auto update = harness.update(true);
  auto hierarchy = build_render_overlay_selection_snapshot(document, harness.assets);
  ASSERT_EQ(hierarchy.hierarchy_selection_wires.size(), 2u);
  ASSERT_EQ(update.scene_wireframe->meshes.size(), 4u);
  for (const auto entity : {harness.selected_mesh, harness.unselected_mesh,
                            harness.selected_instance, harness.unselected_instance}) {
    EXPECT_EQ(std::ranges::count_if(update.scene_wireframe->meshes,
        [entity](const auto &mesh) { return mesh.entity == entity; }), 1);
  }
  for (const auto &mesh : update.scene_wireframe->meshes) {
    EXPECT_FALSE(mesh.selected);
    EXPECT_FALSE(mesh.active_selected);
    EXPECT_EQ(mesh.wire_segments.size(), 3u);
  }
  EXPECT_EQ(plan_scene_wireframe_overlay_batches(*update.scene_wireframe, 64u).participant_count, 4u);
  SelectedMeshOutlineCache cache;
  ViewportOverlayCamera camera;
  camera.eye = {0, 0, 10};
  // Coincident direct and formal-instance triangles have one projected boundary.
  EXPECT_EQ(cache.frame(update.selected_object, camera, 0, &hierarchy).lines.size(), 3u);
  EXPECT_EQ(cache.work_stats().topology_builds, 1u);
  EXPECT_FALSE(harness.update(true).changed());

  // Explicit selection moves an occurrence between sources without duplicating its outline.
  ASSERT_TRUE(document.set_selected(harness.selected_instance, true));
  const auto explicit_instance = harness.update(true);
  hierarchy = build_render_overlay_selection_snapshot(document, harness.assets);
  ASSERT_EQ(hierarchy.hierarchy_selection_wires.size(), 1u);
  ASSERT_EQ(explicit_instance.selected_object->instance_selection_fills.size(), 1u);
  EXPECT_EQ(cache.frame(explicit_instance.selected_object, camera, 0, &hierarchy).lines.size(), 3u);
  EXPECT_EQ(cache.work_stats().topology_builds, 1u);
  EXPECT_EQ(plan_instance_selection_overlay_batches(*explicit_instance.selected_object, 64u).wires.occurrence_count, 0u);

  ASSERT_TRUE(document.set_visible(harness.selected_mesh, false));
  const auto hidden = harness.update(true);
  hierarchy = build_render_overlay_selection_snapshot(document, harness.assets);
  EXPECT_TRUE(hierarchy.hierarchy_selection_wires.empty());
  EXPECT_EQ(cache.frame(hidden.selected_object, camera, 0, &hierarchy).lines.size(), 3u);
  document.clear_selection();
  const auto deselected = harness.update(true);
  hierarchy = build_render_overlay_selection_snapshot(document, harness.assets);
  EXPECT_TRUE(cache.frame(deselected.selected_object, camera, 0, &hierarchy).lines.empty());
  EXPECT_EQ(deselected.scene_wireframe->meshes.size(), 3u);
}

TEST(SelectedMeshOutlinePresentationTests,
     CurrentModeSelectionCountAndPrimaryChangesReachRetainedSubmission) {
  for (const auto mode : {QuaderSelectionMode::Vertex, QuaderSelectionMode::Edge,
                          QuaderSelectionMode::Face}) {
    SCOPED_TRACE(static_cast<int>(mode));
    SceneSession session;
    QuaderEditorApi editor{QuaderApiSessionInternalAccess::from_scene_session(session)};
    const auto first = editor.modeling().create_box(
        {.min = {-1, -1, -1}, .max = {1, 1, 1}}, "First active candidate");
    const auto second = editor.modeling().create_box(
        {.min = {2, -1, -1}, .max = {4, 1, 1}}, "Second active candidate");
    ASSERT_TRUE(first.ok());
    ASSERT_TRUE(second.ok());
    ASSERT_TRUE(editor.selection().select_objects(
        {first.value().entity, second.value().entity}, first.value().entity).ok());
    ASSERT_TRUE(enter_selection_context(editor, mode).ok());
    const auto select = [&](QuaderModelingObjectRef object) {
      switch (mode) {
      case QuaderSelectionMode::Vertex:
        return editor.selection().select_vertices(object, {1, 2}, QuaderSelectionEdit::Add);
      case QuaderSelectionMode::Edge:
        return editor.selection().select_edge(object, 1, 2, QuaderSelectionEdit::Add);
      default:
        return editor.selection().select_faces(object, {1, 2}, QuaderSelectionEdit::Add);
      }
    };
    const auto select_single = [&](QuaderModelingObjectRef object) {
      switch (mode) {
      case QuaderSelectionMode::Vertex:
        return editor.selection().select_vertex(object, 1);
      case QuaderSelectionMode::Edge:
        return editor.selection().select_edge(object, 1, 2);
      default:
        return editor.selection().select_face(object, 1);
      }
    };
    ASSERT_TRUE(select_single(first.value()).ok());
    auto &modeling = session.modeling();
    ExtrudeOutlineProvider provider{session};
    ExtrudeOutlineTarget target;
    ViewportOverlaySubmissionCoordinator coordinator;
    auto settings = ViewportSettings::defaults();
    (void)settings.set_color(ViewportColorSettingId::SelectionWire, {0.3f, 0.5f, 0.7f, 0.65f});
    ASSERT_TRUE(settings.set_float_value(ViewportFloatSettingId::SelectionEdgeLineSize, 3.0f));
    ASSERT_TRUE(settings.set_float_value(ViewportFloatSettingId::SelectionFaceWireLineSize, 4.0f));
    ASSERT_TRUE(settings.set_float_value(ViewportFloatSettingId::ActiveSelectionLineSize, 6.0f));
    const auto context = mode == QuaderSelectionMode::Vertex
        ? ViewportOverlaySelectionContext::Vertex
        : mode == QuaderSelectionMode::Edge ? ViewportOverlaySelectionContext::Edge
                                           : ViewportOverlaySelectionContext::Face;
    const auto submit = [&](bool wireframe_pane = false, std::size_t viewport_index = 0u) {
      return coordinator.submit({
          .viewport_index = viewport_index,
          .layer_mask = 1u,
          .selection_source = {
              .source_identity = reinterpret_cast<std::uintptr_t>(&modeling),
              .content_revision = modeling.content_revision(),
              .selection_revision = modeling.selection_revision(),
              .scene_revision = session.scene().revision(),
              .asset_revision = session.assets().revision(),
              .overlay_signature = modeling.selection_overlay_signature(),
              .selection_context = context,
          },
          .selection_frame_provider = &provider,
          .scene_selection_mode = editor.mode().current() == QuaderEditorMode::Edit
              ? RenderOverlaySelectionMode::Component : RenderOverlaySelectionMode::Object,
          .style_selection_context = context,
          .overlays_visible = true,
          .wireframe_pane = wireframe_pane,
      }, settings, target);
    };
    const auto expect_active = [&](int active_id) {
      std::size_t active_count = 0;
      for (const auto &point : target.persistent.points) {
        if (point.style_role == ViewportOverlayStyleRole::ActiveSelectedVertex) {
          ++active_count;
          EXPECT_EQ(point.element.object_id, active_id);
        }
      }
      for (const auto &line : target.persistent.lines) {
        if (line.style_role == ViewportOverlayStyleRole::ActiveSelectionWire) {
          ++active_count;
          EXPECT_EQ(line.element.object_id, active_id);
          EXPECT_FLOAT_EQ(line.width_pixels, 6.0f);
        }
      }
      if (mode == QuaderSelectionMode::Face) {
        EXPECT_TRUE(std::ranges::any_of(target.persistent.triangles, [active_id](const auto &triangle) {
          return triangle.style_role == ViewportOverlayStyleRole::ActiveSelectionFace &&
                 triangle.element.object_id == active_id;
        }));
        for (const auto &triangle : target.persistent.triangles) {
          if (triangle.style_role == ViewportOverlayStyleRole::ActiveSelectionFace)
            EXPECT_EQ(triangle.element.object_id, active_id);
        }
      }
      EXPECT_GT(active_count, 0u);
    };
    const auto expect_singleton = [&] {
      EXPECT_FALSE(std::ranges::any_of(target.persistent.points, [](const auto &point) {
        return point.style_role == ViewportOverlayStyleRole::ActiveSelectedVertex;
      }));
      EXPECT_FALSE(std::ranges::any_of(target.persistent.lines, [](const auto &line) {
        return line.style_role == ViewportOverlayStyleRole::ActiveSelectionWire;
      }));
      EXPECT_FALSE(std::ranges::any_of(target.persistent.triangles, [](const auto &triangle) {
        return triangle.style_role == ViewportOverlayStyleRole::ActiveSelectionFace;
      }));
      if (mode != QuaderSelectionMode::Vertex) {
        ASSERT_TRUE(std::ranges::any_of(target.persistent.lines, [](const auto &line) {
          return line.style_role == ViewportOverlayStyleRole::SelectionWire;
        }));
        for (const auto &line : target.persistent.lines) {
          if (line.style_role == ViewportOverlayStyleRole::SelectionWire) {
            EXPECT_FLOAT_EQ(line.width_pixels,
                            line.layer == ViewportOverlayLayer::SelectedFaceEdge
                                ? 4.0f : 3.0f);
          }
        }
      }
    };
    ASSERT_TRUE(submit().persistent_submitted);
    ASSERT_NO_FATAL_FAILURE(expect_singleton());
    const auto singleton_builds = provider.builds;
    const auto singleton_submissions = target.submissions;
    EXPECT_FALSE(submit().persistent_submitted);
    EXPECT_EQ(provider.builds, singleton_builds);
    EXPECT_EQ(target.submissions, singleton_submissions);
    if (mode == QuaderSelectionMode::Edge) {
      ASSERT_TRUE(editor.selection().select_edge(first.value(), 1, 4,
                                                QuaderSelectionEdit::Add).ok());
    } else {
      ASSERT_TRUE(select(first.value()).ok());
    }
    ASSERT_TRUE(submit().persistent_submitted);
    expect_active(first.value().modeling_object_id);
    ASSERT_TRUE(select_single(first.value()).ok());
    ASSERT_TRUE(submit().persistent_submitted);
    ASSERT_NO_FATAL_FAILURE(expect_singleton());
    ASSERT_TRUE(select(second.value()).ok());
    ASSERT_TRUE(submit().persistent_submitted);
    expect_active(second.value().modeling_object_id);
    const auto before = modeling.modeling_sdk_projection_stats_for_tests();
    const auto content_revision = modeling.content_revision();
    ASSERT_TRUE(select(first.value()).ok());
    EXPECT_EQ(modeling.content_revision(), content_revision);
    ASSERT_TRUE(submit().persistent_submitted);
    expect_active(first.value().modeling_object_id);
    const auto after = modeling.modeling_sdk_projection_stats_for_tests();
    EXPECT_EQ(after.selection_topology_build_count, before.selection_topology_build_count);
    EXPECT_EQ(after.mesh_compile_count, before.mesh_compile_count);
    const auto builds = provider.builds;
    const auto submissions = target.submissions;
    for (int frame = 0; frame < 4; ++frame) {
      const auto unchanged = submit();
      EXPECT_FALSE(unchanged.selection_source_refreshed);
      EXPECT_FALSE(unchanged.persistent_submitted);
    }
    EXPECT_EQ(provider.builds, builds);
    EXPECT_EQ(target.submissions, submissions);
    const auto source_count = [](const auto &frame) {
      return std::ranges::count_if(frame.lines, [](const auto &line) {
        return line.layer == ViewportOverlayLayer::SourceWire &&
               line.source == ViewportOverlaySource::ComponentSelection;
      });
    };
    const auto solid_frame = target.persistent;
    ASSERT_GT(source_count(solid_frame), 0);
    const auto semantic_frame = modeling.selection_overlay_render_frame();
    const auto semantic_source_count = source_count(semantic_frame);
    const auto wireframe = submit(true);
    EXPECT_TRUE(wireframe.persistent_submitted);
    EXPECT_FALSE(wireframe.selection_source_refreshed);
    EXPECT_EQ(source_count(target.persistent), 0);
    EXPECT_EQ(target.persistent.lines.size(),
              solid_frame.lines.size() - static_cast<std::size_t>(source_count(solid_frame)));
    EXPECT_EQ(target.persistent.points.size(), solid_frame.points.size());
    expect_active(first.value().modeling_object_id);
    EXPECT_EQ(provider.builds, builds);
    for (int frame = 0; frame < 4; ++frame) EXPECT_TRUE(submit(true).camera_only);
    EXPECT_EQ(source_count(modeling.selection_overlay_render_frame()), semantic_source_count);
    EXPECT_TRUE(submit(false, 1u).persistent_submitted);
    EXPECT_EQ(source_count(target.persistent), source_count(solid_frame));
    EXPECT_EQ(provider.builds, builds);
    EXPECT_TRUE(submit(false).persistent_submitted);
    EXPECT_EQ(source_count(target.persistent), source_count(solid_frame));
    ASSERT_TRUE(editor.mode().set(QuaderEditorMode::Object).ok());
    ASSERT_TRUE(submit(true).persistent_submitted);
    ASSERT_TRUE(enter_selection_context(editor, mode).ok());
    ASSERT_TRUE(submit(true).persistent_submitted);
    EXPECT_EQ(source_count(target.persistent), 0);
    expect_active(first.value().modeling_object_id);
    EXPECT_TRUE(submit(false).persistent_submitted);
    EXPECT_EQ(source_count(target.persistent), source_count(solid_frame));
  }
}

/// Exercises cutting-tool topology through the same retained source gate used
/// by the viewport, including face-interior hover before a cut can be seeded.
struct CuttingToolOutlineHarness {
  SceneSession session;
  QuaderEditorApi editor{QuaderApiSessionInternalAccess::from_scene_session(session)};
  ExtrudeOutlineProvider provider{session};
  ExtrudeOutlineTarget target;
  ViewportOverlaySubmissionCoordinator coordinator;
  ViewportSettings settings = ViewportSettings::defaults();

  QuaderModelingObjectRef add_strip(float offset) {
    quader_poly::Document document;
    const std::array vertices{
        quader_poly::add_vertex(document, {offset, 0, 0}),
        quader_poly::add_vertex(document, {offset + 1, 0, 0}),
        quader_poly::add_vertex(document, {offset + 2, 0, 0}),
        quader_poly::add_vertex(document, {offset, 1, 0}),
        quader_poly::add_vertex(document, {offset + 1, 1, 0}),
        quader_poly::add_vertex(document, {offset + 2, 1, 0})};
    (void)quader_poly::add_face(
        document, std::array{vertices[0], vertices[1], vertices[4], vertices[3]});
    (void)quader_poly::add_face(
        document, std::array{vertices[1], vertices[2], vertices[5], vertices[4]});
    const auto created = editor.modeling().create_mesh(
        QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)),
        {.name = "Cutting topology strip"});
    EXPECT_TRUE(created.ok()) << created.error_message();
    return created.value();
  }

  static ModelingToolRayEvent event_at(float x, float y = 0.5F) {
    ModelingToolRayEvent event;
    event.ray = {.origin = {x, y, 6}, .direction = {0, 0, -1}};
    event.camera = {.orthographic = true, .origin = {0, 0, 6},
                    .viewport_size = {800, 600}, .orthographic_size = 4.0};
    event.grid_size = 0.5;
    event.vertex_radius = 0.001;
    event.edge_radius = 0.01;
    event.snap_enabled = false;
    return event;
  }

  ModelingCommandResult hover(ViewportToolId tool, float x, float y = 0.5F) {
    return tool == ViewportToolId::Knife
        ? session.modeling().update_knife_hover(event_at(x, y))
        : session.modeling().update_insert_edge_loop_hover(event_at(x, y));
  }

  ViewportOverlaySubmissionResult submit() {
    auto &modeling = session.modeling();
    const auto volatile_snapshot = modeling.volatile_overlay_snapshot();
    return coordinator.submit({
        .viewport_index = 0u,
        .layer_mask = 1u,
        .selection_source = {
            .source_identity = reinterpret_cast<std::uintptr_t>(&modeling),
            .content_revision = modeling.content_revision(),
            .selection_revision = modeling.selection_revision(),
            .scene_revision = session.scene().revision(),
            .asset_revision = session.assets().revision(),
            .overlay_signature = modeling.selection_overlay_signature(),
            .selection_context = ViewportOverlaySelectionContext::Edge,
        },
        .selection_frame_provider = &provider,
        .volatile_modeling_overlay_frame = volatile_snapshot.frame.get(),
        .volatile_modeling_source = {
            .source_identity = volatile_snapshot.source.source_identity,
            .publication_revision = volatile_snapshot.source.publication_revision,
        },
        .scene_selection_mode = RenderOverlaySelectionMode::Component,
        .style_selection_context = ViewportOverlaySelectionContext::Edge,
        .overlays_visible = true,
        .selection_overlays_visible =
            modeling.active_viewport_tool() != ModelingViewportTool::Cut,
    }, settings, target);
  }

  using Wire = std::tuple<std::array<float, 6>, std::uint32_t, float,
                          ViewportOverlayStyleRole,
                          ViewportOverlayLineOcclusionStyle>;
  std::vector<Wire> wires(int object_id) const {
    std::vector<Wire> result;
    for (const auto &line : target.persistent.lines) {
      if (line.layer != ViewportOverlayLayer::SourceWire ||
          line.element.object_id != object_id) continue;
      std::array<float, 3> start{line.start.x, line.start.y, line.start.z};
      std::array<float, 3> end{line.end.x, line.end.y, line.end.z};
      if (end < start) std::swap(start, end);
      result.emplace_back(std::array{start[0], start[1], start[2],
                                     end[0], end[1], end[2]},
                          line.color, line.width_pixels, line.style_role,
                          line.occlusion_style);
    }
    std::ranges::sort(result);
    return result;
  }

  using Depth = std::tuple<std::array<float, 9>, std::uint32_t,
                           ViewportOverlayStyleRole>;
  std::vector<Depth> depths(int object_id) const {
    std::vector<Depth> result;
    for (const auto &triangle : target.persistent.triangles) {
      if (triangle.layer != ViewportOverlayLayer::SourceWireDepthStamp ||
          triangle.element.object_id != object_id) continue;
      result.emplace_back(std::array{triangle.a.x, triangle.a.y, triangle.a.z,
                                     triangle.b.x, triangle.b.y, triangle.b.z,
                                     triangle.c.x, triangle.c.y, triangle.c.z},
                          triangle.color, triangle.style_role);
    }
    std::ranges::sort(result);
    return result;
  }
};

TEST(SelectedMeshOutlinePresentationTests,
     CutFacePreviewHidesSelectionAndRefreshesToolContoursBeforeRestoringSelection) {
  CuttingToolOutlineHarness harness;
  const auto source = harness.add_strip(0.0f);
  auto &editor = harness.editor;
  auto &modeling = harness.session.modeling();
  ASSERT_TRUE(editor.selection().select_object(source).ok());
  ASSERT_TRUE(enter_selection_context(editor, QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(source, 1).ok());
  (void)harness.submit();
  const auto baseline_wires = harness.wires(source.modeling_object_id);
  const auto baseline_depths = harness.depths(source.modeling_object_id);
  const auto selected_faces = editor.selection().summary().selected_face_count;
  const auto content_revision = modeling.content_revision();
  const auto asset_revision = harness.session.assets().revision();
  const auto selected_bounds = modeling.selection_bounds();
  ASSERT_TRUE(selected_bounds.has_value());
  const auto cut_event = [](float x, float y) {
    auto event = CuttingToolOutlineHarness::event_at(x, y);
    event.grid_size = 0.125;
    event.snap_enabled = true;
    return event;
  };
  const auto face_fill_count = [&]() {
    return std::ranges::count_if(harness.target.persistent.triangles,
        [&](const ViewportOverlayTriangle &triangle) {
          return triangle.element.object_id == source.modeling_object_id &&
              (triangle.layer == ViewportOverlayLayer::SelectedVisibleFace ||
               triangle.layer == ViewportOverlayLayer::SelectedXrayFace ||
               triangle.layer == ViewportOverlayLayer::SelectedFaceMask);
        });
  };
  const auto baseline_fills = face_fill_count();
  ASSERT_GT(baseline_fills, 0);
  const auto provider_builds = harness.provider.builds;
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::Cut).ok());
  (void)harness.submit();
  EXPECT_EQ(face_fill_count(), 0);
  EXPECT_TRUE(harness.target.persistent.lines.empty());
  EXPECT_TRUE(harness.target.persistent.triangles.empty());
  EXPECT_TRUE(harness.target.persistent.points.empty());
  EXPECT_EQ(harness.provider.builds, provider_builds);
  EXPECT_EQ(editor.selection().summary().selected_face_count, selected_faces);

  ASSERT_TRUE(modeling.click_cut(cut_event(0.5f, 0.25f)).ok);
  auto second = cut_event(0.5f, 0.75f);
  second.dragging = true;
  ASSERT_TRUE(modeling.update_cut_hover(second).ok);
  ASSERT_TRUE(modeling.end_cut_drag().ok);
  float previous_x = -1.0f;
  for (const float plane_x : {0.5f, 0.625f, 0.375f}) {
    if (previous_x >= 0.0f) {
      ASSERT_TRUE(modeling.begin_cut_line_drag(
          cut_event(previous_x, 0.5f)).ok);
      auto moved = cut_event(plane_x, 0.5f);
      moved.dragging = true;
      ASSERT_TRUE(modeling.update_cut_hover(moved).ok);
      ASSERT_TRUE(modeling.end_cut_drag().ok);
    }
    (void)harness.submit();
    const auto has_seam = [&](float x) {
      return std::ranges::any_of(harness.target.dynamic.lines,
          [&](const ViewportOverlayLine &line) {
            return line.layer == ViewportOverlayLayer::ToolPreviewSeam &&
                line.style_role == ViewportOverlayStyleRole::CutPreviewSeam &&
                line.color == kOverlayCutPreviewSeamColor &&
                line.source == ViewportOverlaySource::Tool &&
                std::abs(line.start.x - x) < 0.0001f &&
                std::abs(line.end.x - x) < 0.0001f &&
                std::abs(line.start.y - line.end.y) > 0.9f;
          });
    };
    EXPECT_TRUE(has_seam(plane_x));
    if (previous_x >= 0.0f) EXPECT_FALSE(has_seam(previous_x));
    EXPECT_EQ(face_fill_count(), 0);
    EXPECT_EQ(modeling.content_revision(), content_revision);
    EXPECT_EQ(harness.session.assets().revision(), asset_revision);
    EXPECT_TRUE(harness.depths(source.modeling_object_id).empty());
    EXPECT_TRUE(harness.wires(source.modeling_object_id).empty());
    EXPECT_TRUE(std::ranges::any_of(harness.target.dynamic.lines,
        [](const auto &line) {
          return line.source == ViewportOverlaySource::Tool &&
              line.color == kOverlayCutPreviewKeptWireColor;
        }));
    EXPECT_FALSE(std::ranges::any_of(harness.target.dynamic.lines,
        [](const auto &line) {
          return line.style_role == ViewportOverlayStyleRole::CutPreviewDiscardOutline;
        }));
    ASSERT_TRUE(modeling.set_cut_mode(ModelingCutMode::DiscardLeft).ok);
    (void)harness.submit();
    EXPECT_TRUE(has_seam(plane_x));
    EXPECT_TRUE(std::ranges::any_of(harness.target.dynamic.lines,
        [](const auto &line) {
          return line.style_role == ViewportOverlayStyleRole::CutPreviewDiscardOutline &&
              line.color == kOverlayCutPreviewDiscardOutlineColor;
        }));
    const auto kept_bounds = modeling.selection_bounds();
    ASSERT_TRUE(kept_bounds.has_value());
    EXPECT_NEAR(kept_bounds->min.x, selected_bounds->min.x, 0.0001f);
    EXPECT_NEAR(kept_bounds->max.x, plane_x, 0.0001f);
    EXPECT_NEAR(kept_bounds->min.y, selected_bounds->min.y, 0.0001f);
    EXPECT_NEAR(kept_bounds->max.y, selected_bounds->max.y, 0.0001f);
    ASSERT_TRUE(modeling.set_cut_mode(ModelingCutMode::KeepBoth).ok);
    (void)harness.submit();
    const auto builds = harness.provider.builds;
    const auto submissions = harness.target.submissions;
    const auto dynamic_submissions = harness.target.dynamic_submissions;
    const auto compile_count =
        modeling.modeling_sdk_projection_stats_for_tests().mesh_compile_count;
    for (int frame = 0; frame < 8; ++frame) (void)harness.submit();
    EXPECT_EQ(harness.provider.builds, builds);
    EXPECT_EQ(harness.target.submissions, submissions);
    EXPECT_EQ(harness.target.dynamic_submissions, dynamic_submissions);
    EXPECT_EQ(modeling.modeling_sdk_projection_stats_for_tests().mesh_compile_count,
              compile_count);
    previous_x = plane_x;
  }
  ASSERT_TRUE(editor.tools().cancel_modal_tool().ok());
  (void)harness.submit();
  EXPECT_EQ(harness.wires(source.modeling_object_id), baseline_wires);
  EXPECT_EQ(harness.depths(source.modeling_object_id), baseline_depths);
  EXPECT_EQ(face_fill_count(), baseline_fills);
  EXPECT_EQ(editor.selection().summary().selected_face_count, selected_faces);
  const auto restored_bounds = modeling.selection_bounds();
  ASSERT_TRUE(restored_bounds.has_value());
  EXPECT_NEAR(restored_bounds->min.x, selected_bounds->min.x, 0.0001f);
  EXPECT_NEAR(restored_bounds->max.x, selected_bounds->max.x, 0.0001f);
  EXPECT_NEAR(restored_bounds->min.y, selected_bounds->min.y, 0.0001f);
  EXPECT_NEAR(restored_bounds->max.y, selected_bounds->max.y, 0.0001f);
}

TEST(SelectedMeshOutlinePresentationTests,
     CuttingToolSurfaceHoverKeepsFixedEditTopologyWithoutChangingSelection) {
  for (const auto tool : {ViewportToolId::Knife, ViewportToolId::InsertEdgeLoop}) {
    for (const bool selected : {false, true}) {
      SCOPED_TRACE(static_cast<int>(tool));
      SCOPED_TRACE(selected);
      CuttingToolOutlineHarness harness;
      const auto first = harness.add_strip(0);
      const auto second = harness.add_strip(4);
      ASSERT_TRUE(harness.editor.selection().select_objects(
          {first.entity, second.entity}, first.entity).ok());
      ASSERT_TRUE(enter_selection_context(harness.editor, QuaderSelectionMode::Edge).ok());
      ASSERT_TRUE(harness.editor.selection().clear().ok());
      (void)harness.settings.set_color(ViewportColorSettingId::SelectionWire, {1, 1, 0, 0.5F});
      (void)harness.settings.set_color(ViewportColorSettingId::ActiveSelection,
                                     {1.0F, 105.0F / 255.0F, 180.0F / 255.0F, 0.5F});
      if (selected) ASSERT_TRUE(harness.editor.selection().select_edge(second, 1, 2).ok());
      ASSERT_TRUE(harness.editor.tools().activate(tool).ok());
      ASSERT_TRUE(harness.submit().persistent_submitted);
      const auto first_wires = harness.wires(first.modeling_object_id);
      const auto second_wires = harness.wires(second.modeling_object_id);
      const auto first_depths = harness.depths(first.modeling_object_id);
      const auto second_depths = harness.depths(second.modeling_object_id);
      ASSERT_EQ(first_wires.size(), 7u);
      ASSERT_EQ(second_wires.size(), 7u);
      ASSERT_EQ(first_depths.size(), 4u);
      ASSERT_EQ(second_depths.size(), 4u);
      const auto selection = harness.editor.selection().summary();
      const auto history = harness.session.history().undo_count();
      auto &modeling = harness.session.modeling();
      const auto content = modeling.content_revision();
      const auto scene = harness.session.scene().revision();
      const auto assets = harness.session.assets().revision();
      for (const float x : {0.5F, 4.5F, 20.0F, 0.4F, 4.6F}) {
        ASSERT_TRUE(harness.hover(tool, x).ok);
        (void)harness.submit();
        EXPECT_EQ(harness.wires(first.modeling_object_id), first_wires);
        EXPECT_EQ(harness.wires(second.modeling_object_id), second_wires);
        EXPECT_EQ(harness.depths(first.modeling_object_id), first_depths);
        EXPECT_EQ(harness.depths(second.modeling_object_id), second_depths);
        const auto builds = harness.provider.builds;
        const auto submissions = harness.target.submissions;
        const auto projection = modeling.modeling_sdk_projection_stats_for_tests();
        for (int idle = 0; idle < 4; ++idle) {
          ASSERT_TRUE(harness.hover(tool, x).ok);
          const auto unchanged = harness.submit();
          EXPECT_FALSE(unchanged.selection_source_refreshed);
          EXPECT_FALSE(unchanged.persistent_submitted);
        }
        EXPECT_EQ(harness.provider.builds, builds);
        EXPECT_EQ(harness.target.submissions, submissions);
        EXPECT_EQ(modeling.modeling_sdk_projection_stats_for_tests().selection_topology_build_count,
                  projection.selection_topology_build_count);
        EXPECT_EQ(modeling.modeling_sdk_projection_stats_for_tests().mesh_compile_count,
                  projection.mesh_compile_count);
      }
      EXPECT_EQ(harness.editor.selection().summary().selection_revision, selection.selection_revision);
      EXPECT_EQ(harness.editor.selection().summary().selected_edge_count, selection.selected_edge_count);
      EXPECT_EQ(modeling.content_revision(), content);
      EXPECT_EQ(harness.session.scene().revision(), scene);
      EXPECT_EQ(harness.session.assets().revision(), assets);
      EXPECT_EQ(harness.session.history().undo_count(), history);
    }
  }
}

TEST(SelectedMeshOutlinePresentationTests,
     CuttingToolActiveDraftKeepsItsSourceTopologyWhenPointerLeaves) {
  for (const auto tool : {ViewportToolId::Knife, ViewportToolId::InsertEdgeLoop}) {
    SCOPED_TRACE(static_cast<int>(tool));
    CuttingToolOutlineHarness harness;
    const auto first = harness.add_strip(0);
    const auto second = harness.add_strip(4);
    ASSERT_TRUE(harness.editor.selection().clear().ok());
    ASSERT_TRUE(harness.editor.selection().select_object(first).ok());
    ASSERT_TRUE(enter_selection_context(harness.editor, QuaderSelectionMode::Edge).ok());
    ASSERT_TRUE(harness.editor.tools().activate(tool).ok());
    auto &modeling = harness.session.modeling();
    const auto history = harness.session.history().undo_count();
    const auto start = CuttingToolOutlineHarness::event_at(0.5F, 0);
    const auto begun = tool == ViewportToolId::Knife
        ? modeling.accept_knife_point(start)
        : modeling.begin_insert_edge_loop_drag(start);
    ASSERT_TRUE(begun.ok) << begun.message;
    ASSERT_TRUE(modeling.pending_operation_active());
    ASSERT_TRUE(harness.submit().persistent_submitted);
    const auto locked_wires = harness.wires(first.modeling_object_id);
    ASSERT_EQ(locked_wires.size(), 7u);
    for (const auto &wire : locked_wires) {
      EXPECT_EQ(std::get<4>(wire), ViewportOverlayLineOcclusionStyle::Default);
    }
    for (const float x : {4.5F, 20.0F}) {
      const auto moved = tool == ViewportToolId::InsertEdgeLoop
          ? modeling.update_insert_edge_loop_drag(
                CuttingToolOutlineHarness::event_at(x))
          : harness.hover(tool, x);
      ASSERT_TRUE(moved.ok) << moved.message;
      (void)harness.submit();
      EXPECT_EQ(harness.wires(first.modeling_object_id), locked_wires);
      EXPECT_TRUE(harness.wires(second.modeling_object_id).empty());
      EXPECT_EQ(harness.session.history().undo_count(), history);
    }
    ASSERT_TRUE(modeling.cancel_active_modal_tool().ok);
    (void)harness.submit();
    EXPECT_EQ(harness.wires(first.modeling_object_id), locked_wires);
    EXPECT_TRUE(harness.wires(second.modeling_object_id).empty());
    EXPECT_EQ(harness.session.history().undo_count(), history);
  }
}

} // namespace
} // namespace quader
