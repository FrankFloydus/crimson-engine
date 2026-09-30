////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/selection/editor_selection_service.hpp"
#include "editor/session/project_session.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/session/viewport_local_visibility_service.hpp"
#include "editor/viewport/viewport_scene_hit_policy.hpp"
#include "editor/viewport/viewport_scene_projection.hpp"
#include "render/overlay/render_overlay_snapshot.hpp"
#include "viewport/viewport_camera_adapter.hpp"

#include <QTemporaryDir>
#include <gtest/gtest.h>
#include <quader/modeling/neutral/polygon/document_topology.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace {

TEST(ModelingSelectionPerformanceTests,
     MarqueeBatchOrderingMatchesPointComparatorIncludingEquivalentKeys) {
  using namespace quader;
  constexpr std::array kKinds{
      ViewportSceneHitKind::CurveControl, ViewportSceneHitKind::ModelingComponent,
      ViewportSceneHitKind::Billboard, ViewportSceneHitKind::CurveEntity,
      ViewportSceneHitKind::ModelingObject, ViewportSceneHitKind::MeshSurface,
      ViewportSceneHitKind::FormalInstance};
  for (const std::size_t count : {1u, 31u, 32u, 33u, 64u, 129u, 257u}) {
    SCOPED_TRACE(count);
    std::vector<ViewportSceneHitCandidate> candidates;
    for (std::size_t index = 0u; index < count; ++index) {
      const auto kind = kKinds[index % kKinds.size()];
      const auto entity = static_cast<EntityId>(index + 1u);
      ViewportSceneHitCandidate candidate{
          .kind = kind, .entity = entity,
          .world_position = {static_cast<float>(index), 0.0f, 0.0f},
          .ray_distance = 2.0 + static_cast<double>(index % 4u) * 0.4e-6,
          .pick_distance_pixels = 7.0 + static_cast<double>(index % 3u) * 0.4e-6};
      if (kind == ViewportSceneHitKind::CurveControl) {
        candidate.source = ViewportSceneCurveControlHit{
            .control = {.point = modeling::make_id<modeling::CurvePointTag>(
                            static_cast<std::uint32_t>(index + 1u), 1u),
                        .kind = CurveControlKind::Anchor}};
      } else if (kind == ViewportSceneHitKind::ModelingComponent ||
                 kind == ViewportSceneHitKind::ModelingObject) {
        const bool component = kind == ViewportSceneHitKind::ModelingComponent;
        candidate.entity = component ? 5001u : entity;
        candidate.source = ViewportResolvedSelectionHit{
            .target = {.domain = component ? ResolvedSelectionDomain::ModelingComponent
                                          : ResolvedSelectionDomain::SceneEntity,
                       .entity = candidate.entity, .modeling_object_id = 8,
                       .component_kind = component ? ModelingActiveSelectionKind::Vertex
                                                  : ModelingActiveSelectionKind::Object,
                       .vertex = static_cast<int>(index)},
            .stable_key = {0u, candidate.entity, 8u, index % 2u}};
      } else if (kind == ViewportSceneHitKind::Billboard) {
        candidate.source = ViewportSceneBillboardHit{};
      } else if (kind == ViewportSceneHitKind::CurveEntity) {
        candidate.source = ViewportSceneCurveEntityHit{};
      } else {
        candidate.source = ViewportSceneMeshHit{
            .participant = {.display_entity = entity,
                            .geometry_entity = entity,
                            .definition_entity = kind == ViewportSceneHitKind::FormalInstance ? 9000u : 0u},
            .primitive_index = index};
      }
      candidates.push_back(std::move(candidate));
    }
    for (int permutation = 0; permutation < 3; ++permutation) {
      SCOPED_TRACE(permutation);
      auto expected = candidates;
      std::ranges::sort(expected, viewport_scene_hit_precedes);
      auto with_invalid = candidates;
      auto invalid = candidates.front();
      invalid.ray_distance = std::numeric_limits<double>::quiet_NaN();
      with_invalid.push_back(invalid);
      invalid = candidates.front();
      invalid.world_position.x = std::numeric_limits<float>::infinity();
      with_invalid.push_back(invalid);
      invalid = candidates.front();
      invalid.pick_distance_pixels = -1.0;
      with_invalid.push_back(invalid);
      const auto actual = resolve_viewport_scene_marquee_hits(with_invalid);
      ASSERT_EQ(actual.size(), expected.size());
      for (std::size_t index = 0u; index < expected.size(); ++index) {
        EXPECT_EQ(actual[index].kind, expected[index].kind);
        EXPECT_EQ(actual[index].entity, expected[index].entity);
        EXPECT_EQ(resolved_selection_target(actual[index]), resolved_selection_target(expected[index]));
        EXPECT_FLOAT_EQ(actual[index].world_position.x, expected[index].world_position.x);
      }
      std::ranges::reverse(candidates);
      std::rotate(candidates.begin(), candidates.begin() + count / 3u, candidates.end());
    }

    const auto duplicate = viewport_scene_hit_candidate({
        .target = {.domain = ResolvedSelectionDomain::ModelingComponent,
                   .entity = 10u, .modeling_object_id = 8,
                   .component_kind = ModelingActiveSelectionKind::Vertex, .vertex = 4},
        .ray_distance = 2.0, .stable_key = {0u, 10u, 8u, 4u}});
    candidates.assign(count, duplicate);
    for (std::size_t index = 0u; index < count; ++index) {
      candidates[index].world_position.x = static_cast<float>(index);
    }
    auto expected_duplicates = candidates;
    std::ranges::sort(expected_duplicates, viewport_scene_hit_precedes);
    const auto unique = resolve_viewport_scene_marquee_hits(candidates);
    ASSERT_EQ(unique.size(), 1u);
    EXPECT_FLOAT_EQ(unique.front().world_position.x, expected_duplicates.front().world_position.x);
    auto other_display = duplicate;
    other_display.entity = 20u;
    std::get<ViewportResolvedSelectionHit>(other_display.source).target.entity = 20u;
    const std::array display_duplicates{other_display, duplicate};
    const auto shared_component = resolve_viewport_scene_marquee_hits(display_duplicates);
    ASSERT_EQ(shared_component.size(), 1u);
    EXPECT_EQ(shared_component.front().entity, 10u);
  }
}

TEST(ModelingSelectionPerformanceTests,
     MarqueeSurfaceDepthRemainsIndependentOfWitnessDepthAndQueryLifetime) {
  using namespace quader;
  SceneSession session;
  QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto occluder = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -0.5f}, .max = {1.0f, 1.0f, 0.5f}}, "Marquee Occluder");
  ASSERT_TRUE(occluder.ok()) << occluder.error_message();
  std::array<ResolvedSelectionHit, 4> candidates;
  constexpr std::array kWorldPoints{
      QVec3{0.0f, 0.0f, -1.0f}, QVec3{0.0f, 0.0f, 1.0f},
      QVec3{3.0f, 0.0f, -1.0f}, QVec3{3.0f, 0.0f, -2.0f}};
  for (std::size_t index = 0u; index < candidates.size(); ++index) {
    candidates[index] = {
        .target = {.domain = ResolvedSelectionDomain::ModelingComponent,
                   .entity = occluder.value().entity,
                   .modeling_object_id = 1,
                   .component_kind = ModelingActiveSelectionKind::Vertex,
                   .vertex = static_cast<int>(index)},
        .visibility_witnesses = {{
            .screen_position = {index < 2u ? 50.0f : 80.0f, 50.0f},
            .world_position = kWorldPoints[index]}},
        .stable_key = {0u, occluder.value().entity, 1u, index}};
  }
  const auto collect = [&](const SceneSurfaceQuerySnapshot &surfaces,
                           bool include_occluded = false) {
    return collect_viewport_scene_marquee_hits({
        .camera = ModelingViewportCamera{
            .orthographic = true, .origin = {0.0f, 0.0f, 10.0f},
            .viewport_size = {100.0f, 100.0f}, .orthographic_size = 10.0},
        .start = {}, .end = {100.0f, 100.0f},
        .rule = ModelingBoxSelectionRule::Crossing,
        .scene_surfaces = &surfaces, .resolved_selection_hits = candidates,
        .include_occluded_resolved_modeling_hits = include_occluded});
  };
  const auto original_surfaces = session.scene_surface_query().snapshot();
  const auto visible = collect(original_surfaces);
  ASSERT_EQ(visible.size(), 3u);
  ASSERT_TRUE(resolved_selection_target(visible.front()).has_value());
  EXPECT_EQ(resolved_selection_target(visible.front())->vertex, 1);
  EXPECT_DOUBLE_EQ(visible.front().ray_distance, 9.0);
  EXPECT_DOUBLE_EQ(visible[1].ray_distance, 11.0);
  EXPECT_DOUBLE_EQ(visible[2].ray_distance, 12.0);

  std::ranges::reverse(candidates);
  const auto reversed = collect(original_surfaces);
  ASSERT_EQ(reversed.size(), visible.size());
  for (std::size_t index = 0u; index < visible.size(); ++index) {
    EXPECT_EQ(resolved_selection_target(reversed[index]),
              resolved_selection_target(visible[index]));
    EXPECT_DOUBLE_EQ(reversed[index].ray_distance, visible[index].ray_distance);
  }
  EXPECT_EQ(collect(original_surfaces, true).size(), candidates.size());

  ASSERT_TRUE(editor.selection().select_object(occluder.value()).ok());
  ASSERT_TRUE(editor.modeling().move_selection({0.0f, 0.0f, -3.0f}).ok());
  const auto moved_surfaces = session.scene_surface_query().snapshot();
  EXPECT_EQ(collect(moved_surfaces).size(), candidates.size());
  EXPECT_EQ(collect(original_surfaces).size(), visible.size());
}

TEST(ModelingSelectionPerformanceTests,
     ParallelMarqueeVisibilityMatchesSerialChunksAndOriginalWitnessTies) {
  using namespace quader;
  SceneSession session;
  QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto occluder = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -0.5f}, .max = {1.0f, 1.0f, 0.5f}}, "Parallel Marquee Occluder");
  ASSERT_TRUE(occluder.ok()) << occluder.error_message();
  const auto surfaces = session.scene_surface_query().snapshot();
  const ViewportSceneProjectionCamera camera(ModelingViewportCamera{
      .orthographic = false, .origin = {0.0f, 0.0f, 10.0f},
      .viewport_size = {100.0f, 100.0f}, .fov_degrees = 60.0});
  const auto witness_at = [&](QVec2 screen, float distance) {
    auto ray = camera.ray_from_viewport_point(screen);
    ray.direction = ray.direction / length(ray.direction);
    return ModelingSelectionVisibilityWitness{
        .screen_position = screen, .world_position = ray.origin + ray.direction * distance};
  };
  constexpr std::size_t kCandidateCount = 16384u;
  std::vector<ResolvedSelectionHit> candidates(kCandidateCount);
  std::size_t visible_count = 0u;
  std::size_t through_count = 0u;
  for (std::size_t index = 0u; index < candidates.size(); ++index) {
    auto &candidate = candidates[index];
    candidate.target = {.domain = ResolvedSelectionDomain::ModelingComponent,
                        .entity = occluder.value().entity, .modeling_object_id = 1,
                        .component_kind = ModelingActiveSelectionKind::Vertex,
                        .vertex = static_cast<int>(index)};
    candidate.stable_key = {0u, occluder.value().entity, 1u, index};
    switch (index % 5u) {
    case 0u:
      candidate.visibility_witnesses = {
          witness_at({50.0f, 50.0f}, 12.0f), witness_at({50.0f, 50.0f}, 8.0f)};
      break;
    case 1u:
      candidate.visibility_witnesses = {
          witness_at({20.0f, 50.0f}, 8.0f), witness_at({80.0f, 50.0f}, 8.0f)};
      if (index % 2u == 0u) {
        std::ranges::reverse(candidate.visibility_witnesses);
      }
      break;
    case 2u:
      candidate.visibility_witnesses = {
          witness_at({50.0f, 50.0f}, 12.0f), witness_at({50.0f, 50.0f}, 13.0f)};
      break;
    case 3u:
      if (index % 2u == 0u) {
        candidate.visibility_witnesses = {{
            .screen_position = {50.0f, 50.0f},
            .world_position = {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f}}};
      }
      break;
    default:
      for (std::size_t witness_index = 0u; witness_index < 24u; ++witness_index) {
        candidate.visibility_witnesses.push_back(witness_at(
            {80.0f, 50.0f}, 15.0f - 0.25f * static_cast<float>(witness_index)));
      }
      break;
    }
    visible_count += index % 5u != 2u && index % 5u != 3u ? 1u : 0u;
    through_count += index % 5u != 3u ? 1u : 0u;
  }
  const auto collect = [&](std::span<const ResolvedSelectionHit> input,
                           const SceneSurfaceQuerySnapshot &snapshot, bool through) {
    return collect_viewport_scene_marquee_hits({
        .camera = camera, .start = {}, .end = {100.0f, 100.0f},
        .rule = ModelingBoxSelectionRule::Crossing, .scene_surfaces = &snapshot,
        .resolved_selection_hits = input,
        .include_occluded_resolved_modeling_hits = through});
  };
  std::vector<ViewportSceneHitCandidate> original_visible;
  for (const bool through : {false, true}) {
    SCOPED_TRACE(through);
    std::vector<ViewportSceneHitCandidate> serial;
    constexpr std::size_t kSerialChunkSize = 256u;
    for (std::size_t begin = 0u; begin < candidates.size(); begin += kSerialChunkSize) {
      const auto chunk = collect(std::span(candidates).subspan(begin,
          std::min(kSerialChunkSize, candidates.size() - begin)), surfaces, through);
      serial.insert(serial.end(), chunk.begin(), chunk.end());
    }
    const auto expected = resolve_viewport_scene_marquee_hits(serial);
    ASSERT_EQ(expected.size(), through ? through_count : visible_count);
    for (int repetition = 0; repetition < 3; ++repetition) {
      SCOPED_TRACE(repetition);
      const auto actual = collect(candidates, surfaces, through);
      ASSERT_EQ(actual.size(), expected.size());
      for (std::size_t index = 0u; index < expected.size(); ++index) {
        EXPECT_EQ(resolved_selection_target(actual[index]), resolved_selection_target(expected[index]));
        EXPECT_EQ(actual[index].ray_distance, expected[index].ray_distance);
        EXPECT_EQ(actual[index].world_position.x, expected[index].world_position.x);
        EXPECT_EQ(actual[index].world_position.y, expected[index].world_position.y);
        EXPECT_EQ(actual[index].world_position.z, expected[index].world_position.z);
        const auto target = resolved_selection_target(actual[index]);
        ASSERT_TRUE(target.has_value());
        if (target->vertex % 5 == 1) {
          const auto &first_witness = candidates[static_cast<std::size_t>(target->vertex)].visibility_witnesses.front();
          EXPECT_EQ(actual[index].world_position.x, first_witness.world_position.x);
        }
      }
    }
    if (!through) {
      original_visible = expected;
    }
  }
  ASSERT_TRUE(editor.selection().select_object(occluder.value()).ok());
  ASSERT_TRUE(editor.modeling().move_selection({0.0f, 0.0f, -3.0f}).ok());
  EXPECT_EQ(collect(candidates, surfaces, false).size(), original_visible.size());
  EXPECT_EQ(collect(candidates, session.scene_surface_query().snapshot(), false).size(), through_count);
}

TEST(ModelingSelectionPerformanceTests,
     TypedMarqueeRetainsWorldGeometryAcrossSelectionChanges) {
  using namespace quader;
  for (const auto mode : {QuaderSelectionMode::Object, QuaderSelectionMode::Vertex,
                          QuaderSelectionMode::Edge, QuaderSelectionMode::Face}) {
    SCOPED_TRACE(mode == QuaderSelectionMode::Object ? "Object"
        : mode == QuaderSelectionMode::Vertex ? "Vertex"
        : mode == QuaderSelectionMode::Edge ? "Edge" : "Face");
    SceneSession session;
    QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
    const auto created = editor.modeling().create_box(
        {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}}, "Retained Picking Box");
    ASSERT_TRUE(created.ok()) << created.error_message();
    ASSERT_TRUE(editor.selection().select_object(created.value()).ok());
    ASSERT_TRUE(editor.selection().set_mode(mode).ok());
    ASSERT_TRUE(editor.selection().clear().ok());
    ModelingBoxSelectRequest query{
        .camera = {.orthographic = true, .origin = {0.0f, 0.0f, 10.0f},
                   .viewport_size = {100.0f, 100.0f}, .orthographic_size = 100.0},
        .start = {45.0f, 45.0f}, .end = {55.0f, 55.0f},
        .edit = ModelingSelectionEdit::Replace,
        .rule = ModelingBoxSelectionRule::Contain, .select_backfaces = false};
    const auto initial = session.selection().query_modeling_marquee_hits(query);
    ASSERT_FALSE(initial.empty());
    EXPECT_EQ(initial.front().target.entity, created.value().entity);
    const auto world_builds = session.modeling().world_picking_geometry_build_count();
    ASSERT_GT(world_builds, 0u);
    const QuaderBoxSelectRequest request{
        .camera = {.orthographic = true, .origin = query.camera.origin,
                   .viewport_size = query.camera.viewport_size,
                   .orthographic_size = query.camera.orthographic_size},
        .start = query.start, .end = query.end, .edit = QuaderSelectionEdit::Replace,
        .rule = QuaderBoxSelectionRule::Contain, .select_backfaces = false};
    std::optional<std::uint64_t> resolution_builds;
    for (int repetition = 0; repetition < 3; ++repetition) {
      SCOPED_TRACE(repetition);
      const auto selected = editor.selection().box_select(request);
      ASSERT_TRUE(selected.ok()) << selected.error_message();
      EXPECT_TRUE(selected.value().has_selection)
          << "scene_selected=" << session.scene().selected_entities().size()
          << " scene_primary=" << session.scene().primary_selection()
          << " returned_objects=" << selected.value().selected_object_count
          << " queried_objects=" << editor.selection().summary().selected_object_count
          << " changed=" << selected.changed();
      const auto selected_count = mode == QuaderSelectionMode::Object
          ? selected.value().selected_object_count
          : mode == QuaderSelectionMode::Vertex ? selected.value().selected_vertex_count
          : mode == QuaderSelectionMode::Edge ? selected.value().selected_edge_count
                                              : selected.value().selected_face_count;
      EXPECT_EQ(selected_count, initial.size());
      if (!resolution_builds.has_value()) {
        resolution_builds = session.modeling().rectangle_selection_resolution_build_count();
      }
      EXPECT_EQ(session.modeling().rectangle_selection_resolution_build_count(),
                *resolution_builds);
      ASSERT_TRUE(editor.selection().clear().ok());
      const auto repeated = session.selection().query_modeling_marquee_hits(query);
      ASSERT_EQ(repeated.size(), initial.size());
      for (std::size_t index = 0; index < initial.size(); ++index) {
        EXPECT_EQ(repeated[index].target, initial[index].target);
        EXPECT_EQ(repeated[index].visibility_witnesses, initial[index].visibility_witnesses);
      }
      EXPECT_EQ(session.modeling().world_picking_geometry_build_count(), world_builds);
    }

    // Move the entire mesh so the visible-only query retains its facing set.
    ASSERT_TRUE(editor.selection().box_select(request).ok());
    ASSERT_TRUE(editor.selection().select_all().ok());
    const auto first_move = editor.modeling().move_selection({1.0f, 0.0f, 0.0f});
    ASSERT_TRUE(first_move.ok()) << first_move.error_message();
    const auto moved = session.selection().query_modeling_marquee_hits(query);
    ASSERT_EQ(moved.size(), initial.size());
    EXPECT_EQ(session.modeling().world_picking_geometry_build_count(), world_builds + 1u);
    EXPECT_NE(moved.front().visibility_witnesses, initial.front().visibility_witnesses);
    (void)session.selection().query_modeling_marquee_hits(query);
    EXPECT_EQ(session.modeling().world_picking_geometry_build_count(), world_builds + 1u);

    query.start = {0.0f, 0.0f};
    query.end = {1.0f, 1.0f};
    ASSERT_TRUE(editor.modeling().move_selection({1.0f, 0.0f, 0.0f}).ok());
    EXPECT_TRUE(session.selection().query_modeling_marquee_hits(query).empty());
    EXPECT_EQ(session.modeling().world_picking_geometry_build_count(), world_builds + 1u);
  }
}

TEST(ModelingSelectionPerformanceTests,
     ThroughComponentMarqueeRetainsCameraPlaneAndLegacyWitnessRejections) {
  using namespace quader;
  for (const auto mode : {QuaderSelectionMode::Vertex, QuaderSelectionMode::Edge,
                          QuaderSelectionMode::Face}) {
    SCOPED_TRACE(static_cast<int>(mode));
    SceneSession session;
    QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
    const auto front = editor.modeling().create_box(
        {.min = {-6, -1, -4}, .max = {-4, 1, -2}}, "Front Targets");
    const auto crossing = editor.modeling().create_box(
        {.min = {-1, -1, -1}, .max = {1, 1, 1}}, "Camera Plane Targets");
    const auto rear = editor.modeling().create_box(
        {.min = {4, -1, 2}, .max = {6, 1, 4}}, "Rear Targets");
    ASSERT_TRUE(front.ok());
    ASSERT_TRUE(crossing.ok());
    ASSERT_TRUE(rear.ok());
    ASSERT_TRUE(editor.selection().set_mode(mode).ok());
    ASSERT_TRUE(editor.selection().clear().ok());
    for (const auto rule : {ModelingBoxSelectionRule::Contain,
                           ModelingBoxSelectionRule::Crossing}) {
      const ModelingBoxSelectRequest request{
          .camera = {.orthographic = true, .viewport_size = {200, 200},
                     .orthographic_size = 20},
          .start = {0, 0}, .end = {200, 200},
          .rule = rule, .select_backfaces = true};
      auto targets = session.selection().query_modeling_marquee_hits(request);
      ASSERT_FALSE(targets.empty());
      const bool front_can_skip = mode != QuaderSelectionMode::Face ||
                                  rule == ModelingBoxSelectionRule::Contain;
      for (const auto &target : targets) {
        EXPECT_EQ(target.rectangle_target_only,
                  front_can_skip && target.target.entity == front.value().entity);
      }
      // An unmarked empty witness remains a rejection, even in a mixed batch.
      ResolvedSelectionHit empty = targets.front();
      empty.target.entity = rear.value().entity;
      empty.rectangle_target_only = false;
      empty.visibility_witnesses.clear();
      empty.world_position.reset();
      targets.push_back(std::move(empty));
      const auto surfaces = session.scene_surface_query().snapshot();
      const auto hits = collect_viewport_scene_marquee_hits({
          .camera = request.camera, .start = request.start, .end = request.end,
          .rule = rule, .scene_surfaces = &surfaces,
          .resolved_selection_hits = targets,
          .include_occluded_resolved_modeling_hits = true});
      const std::size_t front_count = mode == QuaderSelectionMode::Vertex ? 8u
          : mode == QuaderSelectionMode::Edge ? 12u : 6u;
      const std::size_t crossing_count = mode == QuaderSelectionMode::Vertex ? 4u
          : mode == QuaderSelectionMode::Edge ? 8u : 5u;
      EXPECT_EQ(std::ranges::count(hits, front.value().entity,
                                  &ViewportSceneHitCandidate::entity), front_count);
      EXPECT_EQ(std::ranges::count(hits, crossing.value().entity,
                                  &ViewportSceneHitCandidate::entity), crossing_count);
      EXPECT_EQ(std::ranges::count(hits, rear.value().entity,
                                  &ViewportSceneHitCandidate::entity), 0);
      EXPECT_EQ(hits.size(), front_count + crossing_count);
    }
  }
}

TEST(ModelingSelectionPerformanceTests,
     ThroughComponentMarqueeSkipsWitnessesAndPreservesCanonicalEdits) {
  using namespace quader;
  for (const auto mode : {QuaderSelectionMode::Vertex, QuaderSelectionMode::Edge,
                          QuaderSelectionMode::Face}) {
    SCOPED_TRACE(static_cast<int>(mode));
    SceneSession session;
    QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
    ASSERT_TRUE(editor.modeling().create_box(
        {.min = {-3, -2, -1}, .max = {0, 2, 1}}, "Through Selection Left").ok());
    ASSERT_TRUE(editor.modeling().create_box(
        {.min = {1, -2, -4}, .max = {4, 2, -2}}, "Through Selection Right").ok());
    ASSERT_TRUE(editor.selection().set_mode(mode).ok());
    for (const bool orthographic : {true, false}) {
      SCOPED_TRACE(orthographic);
      const ModelingBoxSelectRequest seed{
          .camera = {.orthographic = orthographic, .origin = {0, 0, 20},
                     .viewport_size = {200, 200}, .orthographic_size = 20},
          .start = {65, 75}, .end = {85, 125},
          .edit = ModelingSelectionEdit::Replace,
          .rule = ModelingBoxSelectionRule::Crossing, .select_backfaces = true};
      for (const auto rule : {ModelingBoxSelectionRule::Contain,
                             ModelingBoxSelectionRule::Crossing}) {
        SCOPED_TRACE(static_cast<int>(rule));
        const bool targets_only = mode != QuaderSelectionMode::Face ||
                                  rule == ModelingBoxSelectionRule::Contain;
        for (const auto edit : {ModelingSelectionEdit::Replace, ModelingSelectionEdit::Add,
                               ModelingSelectionEdit::Remove, ModelingSelectionEdit::Toggle}) {
          SCOPED_TRACE(static_cast<int>(edit));
          for (const auto end : {QVec2{145, 130}, QVec2{95, 130}, QVec2{10, 10}}) {
            const ModelingBoxSelectRequest request{
                .camera = seed.camera,
                .start = end.x == 10 ? QVec2{5, 5} : QVec2{75, 70}, .end = end,
                .edit = edit, .rule = rule, .select_backfaces = true};
            ASSERT_TRUE(editor.selection().clear().ok());
            (void)session.selection().box_select(seed);
            const auto world_builds = session.modeling().world_picking_geometry_build_count();
            const auto targets = session.selection().query_modeling_marquee_hits(request);
            if (targets_only) {
              EXPECT_EQ(session.modeling().world_picking_geometry_build_count(), world_builds);
            }
            for (const auto &target : targets) {
              EXPECT_EQ(target.target.domain, ResolvedSelectionDomain::ModelingComponent);
              EXPECT_EQ(target.rectangle_target_only, targets_only);
              if (targets_only) {
                EXPECT_TRUE(target.visibility_witnesses.empty());
                EXPECT_FALSE(target.world_position.has_value());
              }
            }
            // The native rectangle owner is the independent publication oracle:
            // it applies exact projected hits without the typed depth arbitration.
            (void)session.selection().box_select(request);
            const auto expected = session.modeling().export_selection();
            ASSERT_TRUE(editor.selection().clear().ok());
            (void)session.selection().box_select(seed);
            const QuaderBoxSelectRequest api_request{
                .camera = {.orthographic = orthographic, .origin = request.camera.origin,
                           .viewport_size = request.camera.viewport_size,
                           .orthographic_size = request.camera.orthographic_size},
                .start = request.start, .end = request.end,
                .edit = static_cast<QuaderSelectionEdit>(edit),
                .rule = rule == ModelingBoxSelectionRule::Contain
                            ? QuaderBoxSelectionRule::Contain : QuaderBoxSelectionRule::Crossing,
                .select_backfaces = true};
            const auto applied = editor.selection().box_select(api_request);
            ASSERT_TRUE(applied.ok()) << applied.error_message();
            const auto actual = session.modeling().export_selection();
            EXPECT_EQ(actual.summary.active_object_id, expected.summary.active_object_id);
            EXPECT_EQ(actual.summary.active_kind, expected.summary.active_kind);
            EXPECT_EQ(actual.summary.active_vertex_id, expected.summary.active_vertex_id);
            EXPECT_EQ(actual.summary.active_edge_a, expected.summary.active_edge_a);
            EXPECT_EQ(actual.summary.active_edge_b, expected.summary.active_edge_b);
            EXPECT_EQ(actual.summary.active_face_id, expected.summary.active_face_id);
            ASSERT_EQ(actual.objects.size(), expected.objects.size());
            for (std::size_t index = 0; index < actual.objects.size(); ++index) {
              EXPECT_EQ(actual.objects[index].entity, expected.objects[index].entity);
              EXPECT_EQ(actual.objects[index].selected, expected.objects[index].selected);
              EXPECT_TRUE(quader_editor_core::polygon_selection_matches(
                  actual.objects[index].selection, expected.objects[index].selection));
            }
            if (targets_only) {
              EXPECT_EQ(session.modeling().world_picking_geometry_build_count(), world_builds);
            }
          }
        }
      }
    }
  }
}

TEST(ModelingSelectionPerformanceTests, CopiedLargeSceneMarqueeStageBenchmark) {
  using namespace quader;
  const char *source = std::getenv("QUADER_TEST_LARGE_SELECTION_PROJECT");
  if (source == nullptr || *source == '\0') {
    GTEST_SKIP() << "Set QUADER_TEST_LARGE_SELECTION_PROJECT to measure a cloned project.";
  }
  const std::filesystem::path source_project = std::filesystem::u8path(source);
  ASSERT_TRUE(std::filesystem::is_regular_file(source_project));
  const char *scene_source = std::getenv("QUADER_TEST_LARGE_SELECTION_SCENE");
  const auto original_scene = scene_source != nullptr && *scene_source != '\0'
      ? std::filesystem::u8path(scene_source)
      : source_project.parent_path() / "Scenes" / "one.qdr";
  const auto relative_scene = original_scene.lexically_relative(source_project.parent_path());
  ASSERT_FALSE(relative_scene.empty());
  ASSERT_FALSE(relative_scene.is_absolute());
  ASSERT_EQ(std::ranges::find(relative_scene, std::filesystem::path("..")), relative_scene.end());
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const std::filesystem::path root(temporary.path().toStdWString());
  const auto project_root = root / source_project.parent_path().filename();
  std::error_code error;
  std::filesystem::copy(source_project.parent_path(), project_root,
      std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_symlinks, error);
  ASSERT_FALSE(error) << error.message();
  ProjectSession project(root / "recent-scenes.json");
  project.set_resource_root(std::filesystem::path(QUADER_SOURCE_DIR) / "resources/modeling_assets");
  QuaderEditorApi editor(project);
  const auto opened_project = editor.project().open(project_root / source_project.filename());
  ASSERT_TRUE(opened_project.ok()) << opened_project.error_message();
  const auto opened_scene = editor.scene_documents().open_native_path(project_root / relative_scene);
  ASSERT_TRUE(opened_scene.ok()) << opened_scene.error_message();
  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(editor.selection().select_all().ok());
  auto &session = *project.active_scene()->session;
  ASSERT_GE(session.scene().selected_entities().size(), 6000u);
  const auto entire_scene_selection = session.scene().selected_entities();
  const auto bounds = session.modeling().selection_bounds();
  ASSERT_TRUE(bounds.has_value());
  const char *mode_source = std::getenv("QUADER_TEST_LARGE_MARQUEE_MODE");
  const std::string_view mode_name = mode_source != nullptr ? mode_source : "object";
  QuaderSelectionMode mode = QuaderSelectionMode::Object;
  if (mode_name == "vertex") {
    mode = QuaderSelectionMode::Vertex;
  } else if (mode_name == "edge") {
    mode = QuaderSelectionMode::Edge;
  } else if (mode_name == "face") {
    mode = QuaderSelectionMode::Face;
  } else {
    ASSERT_EQ(mode_name, "object");
  }
  const bool scene_mode = mode == QuaderSelectionMode::Object;
  const char *extent_source = std::getenv("QUADER_TEST_LARGE_MARQUEE_EXTENT");
  const std::string_view extent_name = extent_source != nullptr ? extent_source : "full";
  ASSERT_TRUE(extent_name == "full" || extent_name == "central");
  const bool full_extent = extent_name == "full";
  ASSERT_TRUE(editor.selection().set_mode(mode).ok());
  ASSERT_TRUE(editor.selection().select_all().ok());
  ASSERT_TRUE(editor.selection().clear().ok());
  constexpr QVec2 kViewportSize{1280.0f, 720.0f};
  const auto saved_camera = ViewportCameraAdapter::modeling_camera(
      session.viewport_workspace().camera(ViewportPaneId::Perspective), kViewportSize);
  const QVec3 extent = bounds->max - bounds->min;
  const QVec3 center = (bounds->min + bounds->max) * 0.5f;
  const float framing_size = std::max({extent.x, extent.y, extent.z, 1.0f});
  const ModelingViewportCamera framed_camera{
      .orthographic = true,
      .origin = {center.x, center.y, bounds->max.z + framing_size},
      .viewport_size = kViewportSize,
      .orthographic_size = std::max(extent.y, extent.x * kViewportSize.y / kViewportSize.x) * 1.1 + 1.0};
  const std::array cameras{saved_camera, framed_camera};
  const std::array camera_names{"saved", "framed"};
  const auto elapsed = [](auto begin) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
  };
  RenderOverlaySnapshotCache overlay_snapshots;
  const std::size_t camera_count = scene_mode || full_extent ? cameras.size() : 1u;
  for (std::size_t camera_index = 0u; camera_index < camera_count; ++camera_index) {
    for (const bool select_backfaces : {false, true}) {
      ASSERT_TRUE(editor.selection().clear().ok());
      const ModelingBoxSelectRequest request{
          .camera = cameras[camera_index],
          .start = scene_mode || full_extent ? kViewportSize : kViewportSize * 0.7f,
          .end = scene_mode || full_extent ? QVec2{} : kViewportSize * 0.3f,
          .edit = ModelingSelectionEdit::Replace,
          .rule = ModelingBoxSelectionRule::Crossing,
          .select_backfaces = select_backfaces};
      const auto report = [&](std::string_view phase, double milliseconds) {
        std::cout << "LARGE_MARQUEE mode=" << mode_name << " extent=" << extent_name
                  << " camera=" << camera_names[camera_index]
                  << " backfaces=" << select_backfaces << " phase=" << phase
                  << " ms=" << milliseconds << std::endl;
      };
      const QuaderBoxSelectRequest api_request{
          .camera = {.orthographic = request.camera.orthographic,
                     .origin = request.camera.origin, .x_axis = request.camera.x_axis,
                     .y_axis = request.camera.y_axis, .z_axis = request.camera.z_axis,
                     .viewport_size = request.camera.viewport_size,
                     .orthographic_size = request.camera.orthographic_size,
                     .fov_degrees = request.camera.fov_degrees},
          .start = request.start, .end = request.end,
          .edit = QuaderSelectionEdit::Replace, .rule = QuaderBoxSelectionRule::Crossing,
          .select_backfaces = select_backfaces};
      auto begin = std::chrono::steady_clock::now();
      const auto first_selected = editor.selection().box_select(api_request);
      report("public_api_from_empty", elapsed(begin));
      ASSERT_TRUE(first_selected.ok()) << first_selected.error_message();
      ASSERT_TRUE(first_selected.value().has_selection);
      EXPECT_TRUE(first_selected.changed());
      begin = std::chrono::steady_clock::now();
      const auto overlay_frame = session.modeling().selection_overlay_frame();
      report("next_frame_semantic_overlay", elapsed(begin));
      begin = std::chrono::steady_clock::now();
      const auto &render_selection = overlay_snapshots.selection_snapshot(
          session.scene(), session.assets());
      report("next_frame_render_selection_snapshot", elapsed(begin));
      begin = std::chrono::steady_clock::now();
      (void)session.scene_surface_query().snapshot();
      report("next_frame_surface_snapshot", elapsed(begin));
      std::cout << "LARGE_MARQUEE_FRAME mode=" << mode_name << " extent=" << extent_name
                << " camera=" << camera_names[camera_index] << " backfaces=" << select_backfaces
                << " selected_meshes=" << session.scene().selected_entities().size()
                << " vertices=" << first_selected.value().selected_vertex_count
                << " edges=" << first_selected.value().selected_edge_count
                << " faces=" << first_selected.value().selected_face_count
                << " overlay_points=" << overlay_frame.points.size()
                << " overlay_lines=" << overlay_frame.lines.size()
                << " overlay_triangles=" << overlay_frame.triangles.size()
                << " render_bounds=" << render_selection.selected_mesh_bounds.size() << std::endl;
      if (full_extent && camera_index == 1u && select_backfaces) {
        EXPECT_GE(session.scene().selected_entities().size(), 6000u);
      }
      ASSERT_TRUE(editor.selection().clear().ok());
      begin = std::chrono::steady_clock::now();
      auto modeling_hits = session.selection().query_modeling_marquee_hits(request);
      report("query_after_clear", elapsed(begin));
      const auto world_builds = session.modeling().world_picking_geometry_build_count();
      begin = std::chrono::steady_clock::now();
      const auto repeated_hits = session.selection().query_modeling_marquee_hits(request);
      report("query_warm", elapsed(begin));
      ASSERT_EQ(repeated_hits.size(), modeling_hits.size());
      std::size_t witnesses = 0u;
      for (std::size_t index = 0u; index < modeling_hits.size(); ++index) {
        witnesses += modeling_hits[index].visibility_witnesses.size();
        EXPECT_EQ(repeated_hits[index].target, modeling_hits[index].target);
        EXPECT_EQ(repeated_hits[index].visibility_witnesses, modeling_hits[index].visibility_witnesses);
      }
      begin = std::chrono::steady_clock::now();
      const auto surfaces = session.scene_surface_query().snapshot();
      report("surface_snapshot", elapsed(begin));
      const auto visibility = session.viewport_local_visibility().snapshot();
      const auto billboards = scene_mode
          ? ViewportSceneProjection::point_light_pick_candidates(session.scene(), visibility)
          : std::vector<ViewportSceneBillboardPickCandidate>{};
      const auto curves = scene_mode ? session.modeling().scene_curve_pick_candidates()
          : std::span<const ViewportSceneCurvePickCandidate>{};
      SceneSurfaceQueryScratch scratch;
      begin = std::chrono::steady_clock::now();
      const auto hits = collect_viewport_scene_marquee_hits({
          .camera = request.camera, .start = request.start, .end = request.end,
          .rule = request.rule, .billboards = billboards, .curves = curves,
          .scene_surfaces = &surfaces, .resolved_selection_hits = modeling_hits,
          .include_occluded_resolved_modeling_hits = select_backfaces,
          .surface_scratch = &scratch});
      report("scene_hit_filter", elapsed(begin));
      std::vector<ResolvedSelectionTarget> targets;
      targets.reserve(hits.size());
      for (const auto &hit : hits) {
        if (auto target = resolved_selection_target(hit)) {
          targets.push_back(std::move(*target));
        }
      }
      const auto baseline = session.selection().capture_box_selection_baseline();
      begin = std::chrono::steady_clock::now();
      const auto applied = session.selection().apply_resolved_marquee({
          .targets = targets, .edit = request.edit, .mode = baseline.mode,
          .rule = request.rule, .baseline = baseline});
      report("publication", elapsed(begin));
      ASSERT_TRUE(applied.command.ok) << applied.command.message;
      const auto expected_selection = session.scene().selected_entities();
      const auto expected_summary = editor.selection().summary();
      begin = std::chrono::steady_clock::now();
      const auto after_selection = session.selection().query_modeling_marquee_hits(request);
      report("query_after_selection", elapsed(begin));
      EXPECT_EQ(after_selection.size(), modeling_hits.size());
      EXPECT_EQ(session.modeling().world_picking_geometry_build_count(), world_builds);
      begin = std::chrono::steady_clock::now();
      const auto repeated = editor.selection().box_select(api_request);
      report("public_api_repeat", elapsed(begin));
      ASSERT_TRUE(repeated.ok()) << repeated.error_message();
      EXPECT_EQ(session.scene().selected_entities(), expected_selection);
      EXPECT_EQ(repeated.value().selected_vertex_count, expected_summary.selected_vertex_count);
      EXPECT_EQ(repeated.value().selected_edge_count, expected_summary.selected_edge_count);
      EXPECT_EQ(repeated.value().selected_face_count, expected_summary.selected_face_count);
      EXPECT_EQ(repeated.value().active_kind, expected_summary.active_kind);
      EXPECT_EQ(repeated.value().active_vertex_id, expected_summary.active_vertex_id);
      EXPECT_EQ(repeated.value().active_edge_a, expected_summary.active_edge_a);
      EXPECT_EQ(repeated.value().active_edge_b, expected_summary.active_edge_b);
      EXPECT_EQ(repeated.value().active_face_id, expected_summary.active_face_id);
      ASSERT_TRUE(editor.selection().clear().ok());
      begin = std::chrono::steady_clock::now();
      const auto reselected = editor.selection().box_select(api_request);
      report("public_api_reselect_after_clear", elapsed(begin));
      ASSERT_TRUE(reselected.ok()) << reselected.error_message();
      EXPECT_TRUE(reselected.changed());
      EXPECT_EQ(reselected.value().selected_vertex_count, expected_summary.selected_vertex_count);
      EXPECT_EQ(reselected.value().selected_edge_count, expected_summary.selected_edge_count);
      EXPECT_EQ(reselected.value().selected_face_count, expected_summary.selected_face_count);
      std::cout << "LARGE_MARQUEE mode=" << mode_name << " extent=" << extent_name
                << " camera=" << camera_names[camera_index]
                << " backfaces=" << select_backfaces << " modeling_hits=" << modeling_hits.size()
                << " witnesses=" << witnesses << " visible_hits=" << hits.size()
                << " selected=" << expected_selection.size()
                << " vertices=" << repeated.value().selected_vertex_count
                << " edges=" << repeated.value().selected_edge_count
                << " faces=" << repeated.value().selected_face_count
                << " world_geometry_builds=" << world_builds << std::endl;
      if (full_extent && camera_index == 1u && select_backfaces && !scene_mode) {
        std::size_t expected_components = 0u;
        std::size_t expected_meshes = 0u;
        for (const EntityId entity : entire_scene_selection) {
          const auto *mesh_component = session.scene().mesh(entity);
          if (mesh_component == nullptr) {
            continue;
          }
          ++expected_meshes;
          const auto *mesh = session.assets().mesh(mesh_component->mesh);
          ASSERT_NE(mesh, nullptr);
          ASSERT_NE(mesh->authored_polygon_document, nullptr);
          const auto &document = *mesh->authored_polygon_document;
          expected_components += mode == QuaderSelectionMode::Vertex ? document.vertices.size()
              : mode == QuaderSelectionMode::Edge ? quader_poly::document_edges(document).size()
              : document.faces.size();
        }
        const auto selected_components = mode == QuaderSelectionMode::Vertex
            ? first_selected.value().selected_vertex_count
            : mode == QuaderSelectionMode::Edge ? first_selected.value().selected_edge_count
            : first_selected.value().selected_face_count;
        EXPECT_EQ(session.scene().selected_entities().size(), expected_meshes);
        EXPECT_EQ(selected_components, expected_components);
      }
    }
  }
}

} // namespace
