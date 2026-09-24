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
#include "editor/tools/extrude_tool_service.hpp"
#include "math/glm_bridge.hpp"
#include "render/overlay/instance_selection_overlay_cache.hpp"
#include "render/overlay/render_overlay_snapshot.hpp"
#include "render/overlay/scene_wireframe_overlay_cache.hpp"
#include "render/overlay/selected_mesh_outline_cache.hpp"
#include "render/scene_render_bridge.hpp"
#include "render/scene_render_extraction.hpp"
#include "scene/scene_document.hpp"
#include "ui/qt_command_event_filter.hpp"
#include "ui/qt_double_spin_box.hpp"
#include "ui/qt_spin_box.hpp"
#include "ui/qt_tool_settings_panel.hpp"
#include "ui/qt_viewport_panel.hpp"
#include "support/command_behavior_contract.hpp"
#include "support/command_behavior_fixture.hpp"
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

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace quader {
namespace {

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
    return session_.modeling().selection_overlay_frame();
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
      ViewportOverlayFrame, ViewportOverlayCamera) override {}
  ViewportOverlayFrame persistent;
  std::uint64_t submissions = 0u;
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
    ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Face).ok());
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
        EXPECT_EQ(line.color & 0x00ffffffu, rgba8(255, 255, 0) & 0x00ffffffu);
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
  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Edge).ok());
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
  QCoreApplication::sendEvent(surface, &enter);
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
     OptInSuppressesBothWireRoutesInWireframeWhileRetainingFillAndLegacyRecords) {
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
    ASSERT_EQ(outline.scene_wireframe->meshes.size(), 2u);
    for (const auto &mesh : outline.scene_wireframe->meshes) {
      EXPECT_TRUE(mesh.entity == harness.unselected_mesh ||
                  mesh.entity == harness.unselected_instance);
      EXPECT_FALSE(mesh.selected);
      EXPECT_TRUE(mesh.has_wire_geometry());
    }
    EXPECT_EQ(plan_scene_wireframe_overlay_batches(*outline.scene_wireframe, 64u).participant_count, 2u);

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
      TransformComponent{.position = {5.0f, 2.0f, -1.0f}}));
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
     NestedGroupsIncludeInstancesOnceAndSuppressInheritedNativeWires) {
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
  ASSERT_EQ(update.scene_wireframe->meshes.size(), 2u);
  for (const auto &mesh : update.scene_wireframe->meshes) {
    EXPECT_TRUE(mesh.entity == harness.unselected_mesh || mesh.entity == harness.unselected_instance);
  }
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

} // namespace
} // namespace quader
