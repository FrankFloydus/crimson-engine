////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "support/guardrail_file_scan.hpp"

#include <gtest/gtest.h>

#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>

namespace quader {
namespace {

using test::guardrail::read_source_file;
using test::guardrail::source_slice;

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
  return compact_source_text(haystack).find(compact_source_text(needle)) !=
         std::string::npos;
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

TEST(FatalErrorHandlerTests,
     MainWiresPlatformBoundaryAndNativeTestsOwnFatalBehavior) {
  const std::string main_source = read_source_file("src/main.cpp");
  const std::string cmake_source = read_source_file("CMakeLists.txt");

  EXPECT_NE(main_source.find("install_fatal_error_handler(log_path)"),
            std::string::npos);
  EXPECT_NE(main_source.find("report_fatal_error_and_exit"), std::string::npos);
  EXPECT_NE(main_source.find("FatalErrorCode::UnhandledCppException"),
            std::string::npos);
  EXPECT_NE(cmake_source.find("tests/fatal_error_handler_behavior_tests.cpp"),
            std::string::npos);
  EXPECT_NE(cmake_source.find("Dbghelp"), std::string::npos);
}

TEST(FatalErrorHandlerTests, SentryLifecycleRemainsPlatformAndMainOwned) {
  const std::string main_source = read_source_file("src/main.cpp");
  const std::string sentry_header =
      read_source_file("src/platform/sentry_client.hpp");

  EXPECT_NE(sentry_header.find("kQuaderSentryDsn"), std::string::npos);
  EXPECT_NE(sentry_header.find("enum class SentryEventLevel"),
            std::string::npos);
  EXPECT_NE(main_source.find("initialize_sentry_client"), std::string::npos);
  EXPECT_NE(main_source.find("--sentry-verify"), std::string::npos);
  EXPECT_NE(main_source.find("shutdown_sentry_client()"), std::string::npos);
}

TEST(ViewportSettingsTests, EveryDescriptorHasARuntimeConsumer) {
  const std::string consumers =
      read_source_file("src/render/viewport_grid_renderer.cpp") +
      read_source_file("src/render/material_binding_service.cpp") +
      read_source_file("src/viewport/viewport_grid_placement_policy.hpp") +
      read_source_file(
          "src/render/overlay/viewport_overlay_style_resolver.cpp") +
      read_source_file("src/ui/ui_system.cpp") +
      read_source_file("src/ui/qt_viewport_panel.cpp") +
      read_source_file("src/viewport/viewport_workspace_service.cpp") +
      read_source_file(
          "src/editor/viewport/viewport_selection_interaction.cpp") +
      read_source_file("src/editor/viewport/transform_gizmo_interaction.cpp") +
      quader::test::guardrail::source_slice(
          read_source_file("src/viewport/viewport_settings.cpp"),
          "bool viewport_snapping_active(",
          "ViewportSettings::ViewportSettings()");

  constexpr std::string_view kColorConsumers[]{
          "ViewportColorSettingId::GridMinor",
          "ViewportColorSettingId::GridMajor",
          "ViewportColorSettingId::MeshGridMinor",
          "ViewportColorSettingId::MeshGridMajor",
          "ViewportColorSettingId::GridXAxis",
          "ViewportColorSettingId::GridYAxis",
          "ViewportColorSettingId::GridZAxis",
          "ViewportColorSettingId::SelectionFace",
          "ViewportColorSettingId::SelectionWire",
          "ViewportColorSettingId::BoundingBoxWire",
          "ViewportColorSettingId::SourceWire",
          "ViewportColorSettingId::OpenEdge",
          "ViewportColorSettingId::DiagnosticEdge",
          "ViewportColorSettingId::HoverFace",
          "ViewportColorSettingId::HoverWire",
          "ViewportColorSettingId::RemoveSelectionFace",
          "ViewportColorSettingId::RemoveSelectionWire",
          "ViewportColorSettingId::Vertex",
          "ViewportColorSettingId::SelectedVertex",
          "ViewportColorSettingId::HoverVertex",
          "ViewportColorSettingId::RemoveSelectionVertex",
      };
  constexpr std::string_view kFloatConsumers[]{
          "ViewportFloatSettingId::GridMinorLineSize",
          "ViewportFloatSettingId::GridMajorLineSize",
          "ViewportFloatSettingId::GridAxisLineSize",
          "ViewportFloatSettingId::DefaultFlyNavigationSpeed",
          "ViewportFloatSettingId::ShadedKeyDirectionX",
          "ViewportFloatSettingId::ShadedKeyDirectionY",
          "ViewportFloatSettingId::ShadedKeyDirectionZ",
          "ViewportFloatSettingId::ShadedKeyIntensity",
          "ViewportFloatSettingId::ShadedDiffuseWrap",
          "ViewportFloatSettingId::ShadedGroundFill",
          "ViewportFloatSettingId::ShadedSkyFill",
          "ViewportFloatSettingId::SourceWireLineSize",
          "ViewportFloatSettingId::SelectionFaceWireLineSize",
          "ViewportFloatSettingId::SelectionEdgeLineSize",
          "ViewportFloatSettingId::BoundingBoxWireThickness",
          "ViewportFloatSettingId::HoverWireLineSize",
          "ViewportFloatSettingId::OpenEdgeLineSize",
          "ViewportFloatSettingId::DiagnosticEdgeLineSize",
          "ViewportFloatSettingId::DimensionFontSize",
          "ViewportFloatSettingId::VertexSize",
          "ViewportFloatSettingId::SelectedVertexGrowth",
          "ViewportFloatSettingId::HoverVertexGrowth",
          "ViewportFloatSettingId::VertexOutlineSize",
          "ViewportFloatSettingId::PickVertexRadius",
          "ViewportFloatSettingId::PickEdgeRadius",
          "ViewportFloatSettingId::GizmoSize",
          "ViewportFloatSettingId::RotationSnapDegrees",
      };
  constexpr std::string_view kBoolConsumers[]{
          "ViewportBoolSettingId::ShowGrid",
          "ViewportBoolSettingId::ShowMeshGrid",
          "ViewportBoolSettingId::ShowGizmos",
          "ViewportBoolSettingId::ShowOverlays",
          "ViewportBoolSettingId::ShowHoverDimensions",
          "ViewportBoolSettingId::ShowInfo",
          "ViewportBoolSettingId::SnappingEnabled",
          "ViewportBoolSettingId::GridSnap",
          "ViewportBoolSettingId::VertexSnap",
          "ViewportBoolSettingId::RotationSnap",
          "ViewportBoolSettingId::BackfaceSelection",
      };

  const auto expect_consumed = [&](const auto &tokens) {
    for (const std::string_view token : tokens) {
      EXPECT_NE(consumers.find(token), std::string::npos) << token;
    }
  };
  expect_consumed(kColorConsumers);
  expect_consumed(kFloatConsumers);
  expect_consumed(kBoolConsumers);
}

TEST(ViewportSettingsTests,
     BackfaceSelectionPreferenceUsesThePersistedRuntimeHook) {
  const std::string ui_source = read_source_file("src/ui/ui_system.cpp");
  const std::string hook = source_slice(
      ui_source, ".backface_selection_enabled =",
      ".grid_visible =");

  EXPECT_NE(hook.find("ViewportBoolSettingId::BackfaceSelection"),
            std::string::npos);
  EXPECT_NE(hook.find(".set_backface_selection_enabled ="),
            std::string::npos);
  EXPECT_NE(hook.find("save_viewport_settings()"), std::string::npos);
  EXPECT_NE(hook.find("synchronize_settings_surfaces(true)"),
            std::string::npos);
}

TEST(PolygonOperationRegistryTests, MetadataComesFromDescriptorOwner) {
  const std::string descriptor =
      read_source_file("src/sdk/core/modeling/neutral/include/quader/modeling/"
                       "neutral/polygon/poly_operation_descriptor.hpp");
  const std::string registry = read_source_file(
      "src/sdk/core/modeling/internal/mesh/polygon/operations/"
      "poly_operation_registry.cpp");

  EXPECT_NE(descriptor.find("kPolyOperationDescriptors"), std::string::npos);
  EXPECT_NE(registry.find("poly_operation_descriptors()"), std::string::npos);
  EXPECT_EQ(registry.find("std::array<PolyOperationMetadata"),
            std::string::npos);
  EXPECT_EQ(registry.find("k_poly_operations"), std::string::npos);

  const std::filesystem::path source_root =
      std::filesystem::path(QUADER_SOURCE_DIR);
  const std::filesystem::path operations_dir =
      source_root / "src/sdk/core/modeling/internal/mesh/polygon/operations";
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
  EXPECT_NE(source.find("append_projected_overlay_points"),
            std::string::npos);
  EXPECT_EQ(modeling_service.find("runtime_.knife_state().preview_lines"),
            std::string::npos);
  EXPECT_NE(overlay_service.find("runtime.knife_state().preview_lines"),
            std::string::npos);
  EXPECT_NE(overlay_service.find("ViewportOverlayLayer::ToolPreviewLine"),
            std::string::npos);
  EXPECT_NE(overlay_service.find("ViewportOverlayLayer::ToolHandle"),
            std::string::npos);
  EXPECT_NE(overlay_service.find("ViewportOverlayStyleRole::ExplicitColor"),
            std::string::npos);
  EXPECT_EQ(overlay_service.find("knife.preview_lines.begin()"),
            std::string::npos);
  EXPECT_EQ(overlay_service.find("knife.preview_points.begin()"),
            std::string::npos);
}

TEST(StandardMaterialEditorPresentationTests,
     PendingTexturePathUsesOnlyTheNativeFilename) {
  const std::string ui_system_source =
      read_source_file("src/ui/ui_system.cpp");

  EXPECT_NE(ui_system_source.find("pending.filename().wstring()"),
            std::string::npos);
  EXPECT_EQ(ui_system_source.find(
                "ui.standard_material_editor.texture.pending"),
            std::string::npos);
  EXPECT_EQ(ui_system_source.find("Pending: {name}"), std::string::npos);
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
  const std::string sdk_adapter = read_source_file(
      "src/editor/modeling/services/modeling_sdk_projection_adapter.cpp");

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
  EXPECT_EQ(
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
  EXPECT_NE(source.find("cached_pick_face_geometry(options)"),
            std::string::npos);
  EXPECT_NE(
      split_source.find("ModelingSelectionService::apply_pick_selection("),
      std::string::npos);
  EXPECT_NE(split_source.find("quader_editor_core::apply_pick_selection("),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      source, "sdk_projection_adapter_.pick_select(request, selection_service_)"));
  EXPECT_NE(sdk_adapter.find("selection.apply_pick_selection("),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      sdk_adapter,
      "selection.apply_pick_selection(projected, scene_, object_map_, "
      "effective_request)"));
  EXPECT_NE(split_source.find(
                "cached_pick_object_bounds(session, scene, object_registry)"),
            std::string::npos);
  EXPECT_NE(
      split_source.find("cached_local_pick_face_geometry(session, scene,"),
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
  EXPECT_NE(
      source.find(
          "quader_editor_core::move_axis_supports_vertex_snap(drag.axis)"),
      std::string::npos);
  EXPECT_EQ(source.find("bool transform_drag_axis_supports_vertex_snap("),
            std::string::npos);
  EXPECT_NE(source.find("request.vertex_snap_radius > 0.0 &&"),
            std::string::npos);
  EXPECT_NE(
      source.find("transform_drag_can_use_vertex_snap(drag_before_update)"),
      std::string::npos);
  EXPECT_NE(
      source.find(
          "transform_drag_has_stationary_vertex_snap_target(context.session)"),
      std::string::npos);
  EXPECT_NE(source.find("context.port.cached_pick_object_bounds()"),
            std::string::npos);
  EXPECT_NE(source.find("context.port.cached_local_pick_face_geometry()"),
            std::string::npos);
  EXPECT_NE(source.find("context.port.cached_local_picking_transforms()"),
            std::string::npos);
  EXPECT_NE(source.find("context.port.cached_local_objects_for_picking()"),
            std::string::npos);
  EXPECT_EQ(source.find("context.port.cached_pick_face_geometry()"),
            std::string::npos);
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

  EXPECT_NE(app_header.find("void bind_active_scene(SceneSession &scene)"),
            std::string::npos);
  EXPECT_NE(app_header.find("ProjectSession project_session_"),
            std::string::npos);
  EXPECT_EQ(app_header.find("SceneSession scene_session_"), std::string::npos);
  EXPECT_NE(app_source.find("ProjectAssetBootstrapService{}."
                            "bootstrap_default_assets(scene)"),
            std::string::npos);
  EXPECT_NE(app_source.find("const SceneSessionReadView session_view = "
                            "active_scene->session->read_view();"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      app_source,
      "QuaderEditorApiSessionAccess::set_scene_binding_changed_callback("
      "*editor_, [this](SceneSession &scene) { bind_active_scene(scene); })"));
  EXPECT_TRUE(source_contains_compact(
      app_source,
      "ui_.install_runtime_hooks(*editor_, scene.scene(), scene.assets(), "
      "scene.modeling(), scene.selection(), scene.native_tools(), "
      "scene.point_placement(), default_material_)"));
  EXPECT_NE(app_source.find("ui_.prepare_viewports(render_bridge_, "
                            "render_overlay_snapshot_cache_)"),
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

TEST(AppLoopTests, QtEventLoopUsesEffectivePresentationCadenceAndGpuReadiness) {
  const std::string app_header = read_source_file("src/app/app.hpp");
  const std::string app_source = read_source_file("src/app/app.cpp");
  const std::string ui_header = read_source_file("src/ui/ui_system.hpp");
  const std::string schedule = read_source_file("src/app/app_frame_schedule.hpp");

  EXPECT_NE(app_header.find("QApplication *application_"), std::string::npos);
  EXPECT_NE(app_header.find("std::unique_ptr<QTimer> frame_timer_"),
            std::string::npos);
  EXPECT_NE(app_source.find("return application_->exec()"), std::string::npos);
  EXPECT_NE(app_source.find("&QTimer::timeout"), std::string::npos);
  EXPECT_NE(schedule.find("kIdleFrameIntervalMs = 16"), std::string::npos);
  EXPECT_NE(app_source.find("presentation_frame_interval_ms"),
            std::string::npos);
  EXPECT_NE(app_source.find("screen->refreshRate()"), std::string::npos);
  EXPECT_NE(app_source.find("kMaximumDisplayRefreshRateHz = 240.0"),
            std::string::npos);
  EXPECT_NE(app_source.find("frame_timer_->setInterval(interval)"),
            std::string::npos);
  EXPECT_EQ(app_source.find("kContinuousFrameIntervalMs = 0"),
            std::string::npos);
  EXPECT_EQ(app_source.find("kActiveFrameIntervalMs = 1"), std::string::npos);
  EXPECT_NE(app_header.find("install_display_refresh_rate_sync()"),
            std::string::npos);
  EXPECT_NE(app_header.find("screen_refresh_rate_changed_connection_"),
            std::string::npos);
  EXPECT_NE(app_source.find("&QWindow::screenChanged"), std::string::npos);
  EXPECT_NE(app_source.find("&QScreen::refreshRateChanged"),
            std::string::npos);
  EXPECT_NE(app_source.find("render_system_.set_display_refresh_rate_hz("),
            std::string::npos);
  EXPECT_NE(app_source.find("ui_.needs_continuous_rendering()"),
            std::string::npos);
  const std::string timeout = source_slice(
      app_source, "QObject::connect(frame_timer_.get(), &QTimer::timeout",
      "QObject::connect(application_, &QApplication::aboutToQuit");
  const auto ready = timeout.find("ready_for_next_frame()");
  const auto work = timeout.find("render_frame(frame_clock_.tick())");
  ASSERT_NE(ready, std::string::npos);
  ASSERT_NE(work, std::string::npos);
  EXPECT_LT(ready, work);
  EXPECT_NE(timeout.find("presentation_is_synchronized()"), std::string::npos);
  EXPECT_NE(timeout.find("schedule_next_frame(false)"), std::string::npos);
  EXPECT_NE(app_source.find("frame_schedule_.next_interval_ms("), std::string::npos);
  EXPECT_EQ(timeout.find("commands().available()"), std::string::npos);
  EXPECT_EQ(timeout.find("settings_."), std::string::npos);
  EXPECT_EQ(app_source.find("SDL_"), std::string::npos);
  EXPECT_EQ(app_source.find("platform_wait_for_event_timeout"),
            std::string::npos);
  EXPECT_NE(
      ui_header.find("[[nodiscard]] bool needs_continuous_rendering() const"),
      std::string::npos);
}

TEST(SceneAwareDeleteSourcePolicyTests,
     SceneOwnedRestoreDefersSdkHierarchyPlacement) {
  const std::string adapter_header = read_source_file(
      "src/editor/modeling/services/modeling_sdk_projection_adapter.hpp");
  const std::string adapter_source = read_source_file(
      "src/editor/modeling/services/modeling_sdk_projection_adapter.cpp");
  const std::string service_source = read_source_file(
      "src/editor/modeling/services/modeling_session_service.cpp");

  EXPECT_NE(adapter_header.find("restore_snapshot_deferred_scene_hierarchy"),
            std::string::npos);
  EXPECT_NE(
      adapter_header.find("restore_snapshot_exact_deferred_scene_hierarchy"),
      std::string::npos);
  EXPECT_NE(adapter_source.find("return restore_snapshot(snapshot, true, "
                                "false, false);"),
            std::string::npos);
  EXPECT_NE(adapter_source.find("return restore_snapshot(snapshot, false, "
                                "false, false);"),
            std::string::npos);
  EXPECT_NE(service_source.find(
                "restore_history_memento_deferred_scene_hierarchy("),
            std::string::npos);
  EXPECT_NE(service_source.find(
                "return restore_history_memento_exact(memento, false, true);"),
            std::string::npos);
}

TEST(AppLoopTests,
     TransientFirstPaintPrecedesNativeViewportFrameSubmission) {
  const std::string app_source = read_source_file("src/app/app.cpp");
  const std::string gate_header =
      read_source_file("src/app/app_frame_presentation.hpp");

  EXPECT_NE(gate_header.find("class AppFramePresentationGate"),
            std::string::npos);
  EXPECT_NE(app_source.find("case QEvent::Show:"), std::string::npos);
  EXPECT_NE(app_source.find("case QEvent::Paint:"), std::string::npos);
  EXPECT_NE(app_source.find("QTimer::singleShot(0, this"),
            std::string::npos);
  const std::size_t event_filter = app_source.find(
      "bool AppFramePresentationGate::eventFilter(");
  const std::size_t event_type_gate =
      app_source.find("switch (event->type())", event_filter);
  const std::size_t widget_cast =
      app_source.find("qobject_cast<QWidget *>(watched)", event_filter);
  ASSERT_NE(event_filter, std::string::npos);
  ASSERT_NE(event_type_gate, std::string::npos);
  ASSERT_NE(widget_cast, std::string::npos);
  EXPECT_LT(event_type_gate, widget_cast)
      << "the application-wide filter must reject irrelevant event types "
         "before QObject meta-casts";
  const std::string frame_timer_source = source_slice(
      app_source,
      "QObject::connect(frame_timer_.get(), &QTimer::timeout",
      "QObject::connect(application_, &QApplication::aboutToQuit");
  EXPECT_LT(frame_timer_source.find("blocks_frame_submission()"),
            frame_timer_source.find("render_frame(frame_clock_.tick())"));
  // Modal interaction defers autosave, but must not suppress rendering for the
  // entire lifetime of a dialog or popup after its first paint.
  EXPECT_EQ(frame_timer_source.find("activeModalWidget"), std::string::npos);
  EXPECT_EQ(frame_timer_source.find("activePopupWidget"), std::string::npos);
}

TEST(AppLoopTests, UISystemRetainsAndPrewarmsEveryMessageBoxOwner) {
  const std::string ui_header = read_source_file("src/ui/ui_system.hpp");
  const std::string ui_source = read_source_file("src/ui/ui_system.cpp");
  const std::string message_box_source =
      read_source_file("src/ui/qt_retained_message_box.cpp");

  EXPECT_NE(ui_header.find(
                "QtUnsavedConfirmationDialog *unsaved_confirmation_"),
            std::string::npos);
  EXPECT_NE(ui_header.find(
                "QtUnsavedConfirmationDialog "
                "*standard_material_unsaved_confirmation_"),
            std::string::npos);
  EXPECT_NE(ui_header.find("QtRetainedMessageBox *lifecycle_message_box_"),
            std::string::npos);
  EXPECT_NE(ui_header.find(
                "QtRetainedMessageBox *standard_material_error_message_box_"),
            std::string::npos);
  EXPECT_NE(ui_source.find("new QtUnsavedConfirmationDialog("),
            std::string::npos);
  EXPECT_NE(ui_source.find(
                "new QtUnsavedConfirmationDialog("
                "standard_material_editor_window_)"),
            std::string::npos);
  EXPECT_NE(ui_source.find("new QtRetainedMessageBox(main_window_.get())"),
            std::string::npos);
  EXPECT_NE(ui_source.find(
                "new QtRetainedMessageBox("
                "standard_material_editor_window_)"),
            std::string::npos);
  EXPECT_NE(ui_source.find("schedule_retained_message_box_prewarm()"),
            std::string::npos);
  EXPECT_NE(ui_source.find("QTimer::singleShot(0, main_window_.get()"),
            std::string::npos);
  EXPECT_NE(ui_source.find(
                "QPointer<QtRetainedMessageBox>(unsaved_confirmation_)"),
            std::string::npos);
  EXPECT_NE(ui_source.find("QPointer<QtRetainedMessageBox>("
                           "standard_material_unsaved_confirmation_)"),
            std::string::npos);
  EXPECT_NE(ui_source.find(
                "QPointer<QtRetainedMessageBox>(lifecycle_message_box_)"),
            std::string::npos);
  EXPECT_NE(ui_source.find("QPointer<QtRetainedMessageBox>("
                           "standard_material_error_message_box_)"),
            std::string::npos);
  EXPECT_NE(ui_source.find("viewport_panel_->prewarm_retained_dialogs()"),
            std::string::npos);
  EXPECT_EQ(ui_source.find("QMessageBox::warning("), std::string::npos);
  EXPECT_EQ(ui_source.find("QMessageBox confirmation("), std::string::npos);

  EXPECT_NE(message_box_source.find("QStyle::SP_MessageBoxCritical"),
            std::string::npos);
  EXPECT_NE(message_box_source.find("QStyle::SP_MessageBoxWarning"),
            std::string::npos);
  EXPECT_NE(message_box_source.find("QStyle::SP_MessageBoxQuestion"),
            std::string::npos);
  EXPECT_NE(message_box_source.find("QStyle::SP_MessageBoxInformation"),
            std::string::npos);
  EXPECT_NE(message_box_source.find("(void)winId()"), std::string::npos);
  EXPECT_NE(message_box_source.find("if (prewarmed_ || isVisible())"),
            std::string::npos);
  EXPECT_EQ(message_box_source.find("show()"), std::string::npos);
}

TEST(QtViewportDeletePopupTests,
     ObjectConfirmationStaysRetainedWhileComponentMenuStaysTransient) {
  const std::string popup_header =
      read_source_file("src/ui/qt_viewport_delete_popup.hpp");
  const std::string popup_source =
      read_source_file("src/ui/qt_viewport_delete_popup.cpp");
  const std::string panel_source =
      read_source_file("src/ui/qt_viewport_panel.cpp");

  EXPECT_NE(popup_header.find(
                "std::unique_ptr<QtRetainedMessageBox> "
                "object_confirmation_"),
            std::string::npos);
  EXPECT_NE(popup_source.find(
                "std::make_unique<QtRetainedMessageBox>(&owner)"),
            std::string::npos);
  EXPECT_NE(popup_source.find("object_confirmation_->prewarm()"),
            std::string::npos);
  EXPECT_EQ(popup_source.find("QMessageBox confirmation("),
            std::string::npos);
  EXPECT_NE(popup_source.find("QMenu menu(&parent)"), std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      panel_source,
      ": QWidget(parent), delete_popup_presenter_(*this)"));
  EXPECT_NE(panel_source.find("delete_popup_presenter_.prewarm()"),
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

TEST(ViewportWorkspaceServiceTests, PaneStateDoesNotStoreDuplicateView) {
  const std::string header =
      read_source_file("src/viewport/viewport_workspace_service.hpp");
  const std::string source =
      read_source_file("src/viewport/viewport_workspace_service.cpp");

  EXPECT_EQ(header.find("ViewportView view ="), std::string::npos);
  EXPECT_EQ(source.find("pane_state.view"), std::string::npos);
}

TEST(ViewportInputRouterTests, LegacyShortcutAdapterSourceIsNotBuilt) {
  const std::string cmake = read_source_file("CMakeLists.txt");

  EXPECT_EQ(cmake.find("src/viewport/viewport_legacy_shortcut_adapter.cpp"),
            std::string::npos);
  EXPECT_EQ(cmake.find("src/viewport/viewport_legacy_shortcut_adapter.hpp"),
            std::string::npos);
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
      read_source_file("src/scene/document/scene_content_document.hpp");
  const std::string scene_session_source =
      read_source_file("src/editor/session/scene_session.hpp");
  const std::string project_session_source =
      read_source_file("src/editor/session/project_session.hpp");
  const std::string app_header = read_source_file("src/app/app.hpp");

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
  EXPECT_EQ(project_document_source.find("ProjectObjectRecord"),
            std::string::npos);
  EXPECT_EQ(project_document_source.find("object_records_"),
            std::string::npos);
  EXPECT_EQ(project_document_source.find("object_by_entity_"),
            std::string::npos);
  EXPECT_EQ(project_document_source.find("mutable std::unordered_map"),
            std::string::npos);
  EXPECT_NE(project_document_source.find("class SceneContentDocument"),
            std::string::npos);
  EXPECT_NE(scene_session_source.find("class SceneSession"),
            std::string::npos);
  EXPECT_NE(scene_session_source.find("struct SceneSessionReadView final"),
            std::string::npos);
  EXPECT_NE(scene_session_source.find("class SceneSessionService final"),
            std::string::npos);
  EXPECT_NE(
      scene_session_source.find("open_session(SceneContentDocument document)"),
      std::string::npos);
  EXPECT_NE(project_session_source.find("class ProjectSession final"),
            std::string::npos);
  EXPECT_NE(project_session_source.find(
                "std::vector<std::unique_ptr<OpenSceneSession>> open_scenes_"),
            std::string::npos);
  EXPECT_NE(app_header.find("void bind_active_scene(SceneSession &scene)"),
            std::string::npos);
  EXPECT_NE(app_header.find("ProjectSession project_session_"),
            std::string::npos);
  EXPECT_EQ(app_header.find("SceneSession scene_session_"), std::string::npos);
}

TEST(DocumentCommandTests,
     SceneTransformPreviewTransactionIndexesPreviewEditsByEntity) {
  const std::string source = read_source_file(
      "src/editor/history/scene_transform_preview_transaction.hpp");

  EXPECT_NE(
      source.find(
          "std::unordered_map<EntityId, std::size_t> edit_index_by_entity_"),
      std::string::npos);
  EXPECT_NE(source.find("edit_index_by_entity_.emplace(entity, edits_.size())"),
            std::string::npos);
  EXPECT_NE(source.find("edit_index_by_entity_.find(edit.entity)"),
            std::string::npos);
  EXPECT_NE(source.find("scene_.set_transforms_and_texture_mappings(assignments"),
            std::string::npos);
  EXPECT_NE(source.find("mapping_plan_->evaluate(staged, kind)"), std::string::npos);
  const std::size_t update_begin = source.find("DocumentCommandResult preview_transforms(");
  ASSERT_NE(update_begin, std::string::npos);
  const std::string update = source.substr(update_begin,
      source.find("DocumentCommandResult cancel()", update_begin) - update_begin);
  EXPECT_EQ(update.find("hierarchy_projection_snapshot"), std::string::npos);
  EXPECT_EQ(update.find("SceneTextureMappingPlan::capture"), std::string::npos);
  EXPECT_EQ(source.find("std::find_if(edits_.begin()"), std::string::npos);
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
  const std::string qdr_codec_source =
      read_source_file("src/io/qdr/qdr_scene_binary_codec.cpp");
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
  EXPECT_NE(qdr_codec_source.find("set_material_texture_slot(material, slot"),
            std::string::npos);
  EXPECT_NE(qdr_codec_source.find("material_texture_slot_descriptors()"),
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

TEST(SceneDocumentTests,
     ComponentAccessorSourceExposesOnlyNamedMutableEntryPoints) {
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
}

TEST(ViewportWorkspaceServiceTests,
     GridPresetSourceRoutesMutationThroughWorkspaceOwner) {
  const std::string router_source =
      read_source_file("src/viewport/viewport_input_router.cpp");
  const std::string runtime_source =
      read_source_file("src/editor/tools/viewport_tool_runtime.cpp");
  const std::string viewport_panel_source =
      read_source_file("src/ui/qt_viewport_panel.cpp");

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
          "workspace_->set_grid_preset(result.requested_grid_preset)"),
      std::string::npos);
  EXPECT_NE(
      viewport_panel_source.find("input_router_.sync_grid_preset_snapshot("
                                 "workspace_->grid_preset())"),
      std::string::npos);
}

TEST(ViewportCameraTests, FlySpeedSourceUsesHostViewportDefaults) {
  const std::string camera_source =
      read_source_file("src/viewport/viewport_camera_controller.cpp");
  const std::string workspace_source =
      read_source_file("src/viewport/viewport_workspace_service.cpp");
  const std::string ui_source = read_source_file("src/ui/ui_system.cpp");

  EXPECT_EQ(camera_source.find("kDefaultFlySpeed"), std::string::npos);
  EXPECT_NE(camera_source.find(
                "const ViewportHostDefaults defaults = viewport_host_defaults()"),
            std::string::npos);
  EXPECT_NE(camera_source.find("defaults.fly_base_speed"), std::string::npos);
  EXPECT_NE(camera_source.find("default_fly_speed_"), std::string::npos);
  EXPECT_NE(workspace_source.find("set_default_fly_speed"),
            std::string::npos);
  EXPECT_NE(ui_source.find(
                "ViewportFloatSettingId::DefaultFlyNavigationSpeed"),
            std::string::npos);
  EXPECT_LT(ui_source.find("apply_viewport_settings_to_workspace();"),
            ui_source.find("viewport_panel_->attach_workspace(workspace)"));
  const std::string commit_source = source_slice(
      ui_source, "void UISystem::handle_settings_dialog_change(",
      "void UISystem::apply_viewport_settings_to_workspace()");
  EXPECT_TRUE(source_contains_compact(
      commit_source,
      "case QtSettingsChangeKind::ViewportCommit: "
      "apply_viewport_settings_to_workspace();"));
}

TEST(SceneContentDocumentTests,
     AssetReferenceSourceRoutesValidationThroughDocumentOwner) {
  const std::string project_document_source =
      read_source_file("src/scene/document/scene_content_document.cpp");
  const std::string qdr_codec_source =
      read_source_file("src/io/qdr/qdr_scene_binary_codec.cpp");
  EXPECT_NE(project_document_source.find(
                "SceneContentDocument::validate_asset_references"),
            std::string::npos);
  EXPECT_NE(qdr_codec_source.find("document.validate_asset_references()"),
            std::string::npos);
  EXPECT_EQ(
      qdr_codec_source.find("document.validate_asset_references(record)"),
      std::string::npos);
  EXPECT_EQ(qdr_codec_source.find("document.assets().mesh(mesh->mesh)"),
            std::string::npos);
  EXPECT_EQ(
      qdr_codec_source.find("document.assets().material(material->material)"),
      std::string::npos);
}

TEST(TestOwnershipGuardrailTests,
     PureSourcePolicyRunsOnlyInGuardrailExecutable) {
  const std::string core_tests = read_source_file("tests/core_tests.cpp");
  const std::string c_make = read_source_file("CMakeLists.txt");
  const std::string guardrail_target = source_slice(
      c_make, "add_executable(quader_guardrail_tests",
      "add_test(NAME quader_guardrail_tests");
  const std::string native_target = source_slice(
      c_make, "add_executable(quader_tests", "add_test(NAME quader_tests");

  EXPECT_EQ(core_tests.find("read_source_file"), std::string::npos);
  for (const std::string_view source : {
           "tests/editor_source_policy_guardrail_tests.cpp",
           "tests/native_source_policy_guardrail_tests.cpp",
           "tests/render_source_policy_guardrail_tests.cpp",
       }) {
    EXPECT_NE(guardrail_target.find(source), std::string::npos) << source;
    EXPECT_EQ(native_target.find(source), std::string::npos) << source;
  }
}

} // namespace
} // namespace quader
