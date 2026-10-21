////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/services/modeling_lamina_overlay_service.hpp"

#include "assets/asset_registry.hpp"
#include "editor/modeling/services/modeling_mesh_sync_service.hpp"
#include "editor/modeling/services/modeling_sdk_projection_adapter.hpp"
#include "editor/modeling/services/modeling_selection_service.hpp"
#include "editor/selection/modeling_selection_projection_snapshot.hpp"
#include "render/viewport_overlay_frame.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "scene/scene_document.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <utility>
#include <vector>

namespace quader {
namespace {

struct LaminaOverlayFixture final {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingObjectRuntimeMap object_map;
  ModelingMeshSyncService mesh_sync;
  ModelingSdkProjectionAdapter projection{scene, assets, object_map, mesh_sync};
  ModelingSelectionService selection;
  ModelingLaminaOverlayService service;
  std::vector<quader_editor_core::PolygonObject> objects;
  std::uint64_t revision = 1u;

  void add_object(int id, std::vector<std::vector<QVec3>> loops,
                  std::vector<quader_poly::ElementId> face_ids = {}) {
    const auto entity = scene.create_entity_with_id(
        static_cast<EntityId>(id), "Lamina fixture", EntityKind::Mesh);
    EXPECT_TRUE(scene.set_transform(entity, {}));
    EXPECT_TRUE(object_map.bind(id, entity));
    quader_poly::Document document;
    for (std::size_t index = 0u; index < loops.size(); ++index) {
      quader_poly::Face face;
      face.id = index < face_ids.size() ? face_ids[index]
                                       : static_cast<quader_poly::ElementId>(index + 1u);
      for (const auto position : loops[index]) {
        const auto vertex_id = document.next_vertex_id++;
        face.vertices.push_back(vertex_id);
        document.vertices.push_back({vertex_id, position});
      }
      document.faces.push_back(std::move(face));
    }
    quader_editor_core::PolygonObject object;
    object.id = id;
    object.document = std::move(document);
    objects.push_back(std::move(object));
    ++revision;
  }

  [[nodiscard]] ModelingSelectionProjectionSnapshot snapshot() const {
    return {objects, quader_editor_core::SelectionMode::Face, -1, revision, 1u};
  }

  [[nodiscard]] const ViewportOverlayFrame &update() {
    return service.update(snapshot(), scene, object_map, selection, projection);
  }
};

std::set<std::pair<int, int>> diagnostic_faces(const ViewportOverlayFrame &frame) {
  std::set<std::pair<int, int>> result;
  for (const auto &triangle : frame.triangles) {
    EXPECT_EQ(triangle.layer, ViewportOverlayLayer::DiagnosticFace);
    EXPECT_EQ(triangle.style_role, ViewportOverlayStyleRole::LaminaFace);
    EXPECT_EQ(triangle.source, ViewportOverlaySource::Diagnostic);
    EXPECT_EQ(triangle.color, kOverlayLaminaFaceColor);
    EXPECT_EQ(triangle.element.vertex_id, -1);
    EXPECT_EQ(triangle.element.edge_a, -1);
    EXPECT_EQ(triangle.element.edge_b, -1);
    result.emplace(triangle.element.object_id, triangle.element.face_id);
  }
  for (const auto &line : frame.lines) {
    EXPECT_EQ(line.layer, ViewportOverlayLayer::DiagnosticEdge);
    EXPECT_EQ(line.style_role, ViewportOverlayStyleRole::LaminaWire);
    EXPECT_EQ(line.source, ViewportOverlaySource::Diagnostic);
    EXPECT_EQ(line.color, kOverlayLaminaWireColor);
    EXPECT_FLOAT_EQ(line.width_pixels, kOverlayLaminaWireLineWidthPixels);
    EXPECT_EQ(line.element.vertex_id, -1);
    EXPECT_EQ(line.element.edge_a, -1);
    EXPECT_EQ(line.element.edge_b, -1);
  }
  return result;
}

double diagnostic_area(const ViewportOverlayFrame &frame, int object, int face) {
  double area = 0.0;
  for (const auto &triangle : frame.triangles) {
    if (triangle.element.object_id != object || triangle.element.face_id != face) continue;
    const double ax = static_cast<double>(triangle.b.x) - triangle.a.x;
    const double ay = static_cast<double>(triangle.b.y) - triangle.a.y;
    const double az = static_cast<double>(triangle.b.z) - triangle.a.z;
    const double bx = static_cast<double>(triangle.c.x) - triangle.a.x;
    const double by = static_cast<double>(triangle.c.y) - triangle.a.y;
    const double bz = static_cast<double>(triangle.c.z) - triangle.a.z;
    area += 0.5 * std::hypot(ay * bz - az * by, az * bx - ax * bz, ax * by - ay * bx);
  }
  return area;
}

TEST(ModelingLaminaOverlayServiceTests,
     YFourFaceFiveMarksBothOppositeSplitFacesAndAuthoredBoundaries) {
  LaminaOverlayFixture fixture;
  fixture.add_object(3, {{{0, 4, 0}, {4, 4, 0}, {4, 4, 4}, {0, 4, 4}}}, {5});
  fixture.add_object(2, {
      {{0, 4, 0}, {0, 4, 4}, {2, 4, 4}, {2, 4, 0}},
      {{2, 4, 0}, {2, 4, 4}, {4, 4, 4}, {4, 4, 0}}});
  const auto &frame = fixture.update();
  const std::set<std::pair<int, int>> expected{{3, 5}, {2, 1}, {2, 2}};
  EXPECT_EQ(diagnostic_faces(frame), expected);
  EXPECT_GE(frame.triangles.size(), 6u);
  EXPECT_NEAR(diagnostic_area(frame, 3, 5), 16.0, 0.00001);
  EXPECT_NEAR(diagnostic_area(frame, 2, 1), 8.0, 0.00001);
  EXPECT_NEAR(diagnostic_area(frame, 2, 2), 8.0, 0.00001);
  EXPECT_EQ(frame.lines.size(), 12u);
  for (const auto &line : frame.lines) {
    EXPECT_FLOAT_EQ(line.start.y, 4.0f);
    EXPECT_FLOAT_EQ(line.end.y, 4.0f);
    EXPECT_TRUE(line.start.x == line.end.x || line.start.z == line.end.z)
        << "Authored boundaries must not include triangulation diagonals.";
  }
}

TEST(ModelingLaminaOverlayServiceTests, PartialContainmentAndSameObjectLaminaMarkOnlySharedRegions) {
  for (const bool same_object : {false, true}) {
    SCOPED_TRACE(same_object);
    LaminaOverlayFixture fixture;
    std::vector<std::vector<QVec3>> first{
        {{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 0}}};
    std::vector<std::vector<QVec3>> second{
        {{3, 1, 0}, {5, 1, 0}, {5, 3, 0}, {3, 3, 0}},
        {{1, 1, 0}, {2, 1, 0}, {2, 2, 0}, {1, 2, 0}}};
    if (same_object) {
      first.insert(first.end(), second.begin(), second.end());
      fixture.add_object(1, std::move(first));
    } else {
      fixture.add_object(1, std::move(first));
      fixture.add_object(2, std::move(second));
    }
    const auto &frame = fixture.update();
    EXPECT_EQ(diagnostic_faces(frame).size(), 3u);
    EXPECT_NEAR(diagnostic_area(frame, 1, 1), 3.0, 0.00001);
    EXPECT_NEAR(diagnostic_area(frame, same_object ? 1 : 2, same_object ? 2 : 1),
                2.0, 0.00001);
    EXPECT_NEAR(diagnostic_area(frame, same_object ? 1 : 2, same_object ? 3 : 2),
                1.0, 0.00001);
    const auto in_shared_region = [](QVec3 point) {
      constexpr float kTolerance = 0.00001f;
      const bool partial = point.x >= 3.0f - kTolerance && point.x <= 4.0f + kTolerance &&
                           point.y >= 1.0f - kTolerance && point.y <= 3.0f + kTolerance;
      const bool contained = point.x >= 1.0f - kTolerance && point.x <= 2.0f + kTolerance &&
                             point.y >= 1.0f - kTolerance && point.y <= 2.0f + kTolerance;
      return point.z == 0.0f && (partial || contained);
    };
    for (const auto &triangle : frame.triangles) {
      EXPECT_TRUE(in_shared_region(triangle.a));
      EXPECT_TRUE(in_shared_region(triangle.b));
      EXPECT_TRUE(in_shared_region(triangle.c));
    }
    for (const auto &line : frame.lines) {
      EXPECT_TRUE(in_shared_region(line.start));
      EXPECT_TRUE(in_shared_region(line.end));
      if (line.element.object_id == 1 && line.element.face_id == 1) {
        EXPECT_NEAR(line.start.x, 4.0f, 0.00001f);
        EXPECT_NEAR(line.end.x, 4.0f, 0.00001f);
      }
    }
    EXPECT_EQ(frame.lines.size(), 8u);
  }
}

TEST(ModelingLaminaOverlayServiceTests, SameObjectDuplicateFacesMarkBothWindingDirections) {
  for (const bool reversed : {false, true}) {
    SCOPED_TRACE(reversed);
    LaminaOverlayFixture fixture;
    std::vector<QVec3> duplicate{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}};
    if (reversed) std::ranges::reverse(duplicate);
    fixture.add_object(1, {
        {{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}}, duplicate,
        {{2, 0, 0}, {4, 0, 0}, {4, 2, 0}, {2, 2, 0}}});
    const auto &frame = fixture.update();
    const std::set<std::pair<int, int>> expected{{1, 1}, {1, 2}};
    EXPECT_EQ(diagnostic_faces(frame), expected);
    EXPECT_EQ(frame.lines.size(), 8u);
  }
}

TEST(ModelingLaminaOverlayServiceTests, WarpedAuthoredFaceDoesNotMarkItsCoincidentRenderTriangles) {
  LaminaOverlayFixture fixture;
  const std::array<QVec3, 4u> corners{
      QVec3{0, 0, 0}, QVec3{2, 0, 0}, QVec3{2, 2, 0}, QVec3{0, 2, 1}};
  fixture.add_object(1, {std::vector<QVec3>(corners.begin(), corners.end())});
  // Cover either triangulation diagonal without depending on the renderer's
  // triangle order. These four planar triangles bound a tetrahedron and meet
  // only at edges; two coincide with the warped quad's rendered triangles.
  fixture.add_object(2, {
      {corners[0], corners[1], corners[2]},
      {corners[0], corners[1], corners[3]},
      {corners[0], corners[2], corners[3]},
      {corners[1], corners[2], corners[3]}});
  EXPECT_TRUE(fixture.update().triangles.empty());
  EXPECT_TRUE(fixture.update().lines.empty());

  // Flattening the complete authored loop makes it genuinely coplanar with
  // the first peer face. Raising the corner again must retire both markers.
  fixture.objects.front().document.edit().vertices.back().position.z = 0.0f;
  ++fixture.revision;
  const std::set<std::pair<int, int>> expected{{1, 1}, {2, 1}};
  EXPECT_EQ(diagnostic_faces(fixture.update()), expected);
  fixture.objects.front().document.edit().vertices.back().position.z = 1.0f;
  ++fixture.revision;
  EXPECT_TRUE(fixture.update().triangles.empty());
  EXPECT_TRUE(fixture.update().lines.empty());
}

TEST(ModelingLaminaOverlayServiceTests, RaisedWallMarksOnlyTheSharedUpperStrip) {
  LaminaOverlayFixture fixture;
  fixture.add_object(1, {{{0, 0, 0}, {2, 0, 0}, {2, 5, 0}, {0, 5, 0}}});
  fixture.add_object(2, {{{0, 4, 0}, {0, 8, 0}, {2, 8, 0}, {2, 4, 0}}});
  const auto &frame = fixture.update();
  EXPECT_EQ(diagnostic_faces(frame).size(), 2u);
  EXPECT_NEAR(diagnostic_area(frame, 1, 1), 2.0, 0.00001);
  EXPECT_NEAR(diagnostic_area(frame, 2, 1), 2.0, 0.00001);
  const auto expect_shared_strip = [](QVec3 point) {
    EXPECT_GE(point.x, -0.00001f);
    EXPECT_LE(point.x, 2.00001f);
    EXPECT_GE(point.y, 3.99999f);
    EXPECT_LE(point.y, 5.00001f);
    EXPECT_FLOAT_EQ(point.z, 0.0f);
  };
  for (const auto &triangle : frame.triangles) {
    expect_shared_strip(triangle.a);
    expect_shared_strip(triangle.b);
    expect_shared_strip(triangle.c);
  }
  for (const auto &line : frame.lines) {
    expect_shared_strip(line.start);
    expect_shared_strip(line.end);
    EXPECT_TRUE(line.start.x == line.end.x || line.start.y == line.end.y)
        << "Only clipped authored edges may become red wire.";
  }
  EXPECT_EQ(frame.lines.size(), 6u);
}

TEST(ModelingLaminaOverlayServiceTests, ObliqueIntersectionFillsOnlyItsOctagonAndClipsAuthoredEdges) {
  LaminaOverlayFixture fixture;
  fixture.add_object(1, {{{0, -2, 0}, {2, 0, 0}, {0, 2, 0}, {-2, 0, 0}}});
  fixture.add_object(2, {
      {{-1.5f, -1.5f, 0}, {-1.5f, 1.5f, 0}, {1.5f, 1.5f, 0}, {1.5f, -1.5f, 0}}});
  const auto &frame = fixture.update();
  EXPECT_EQ(diagnostic_faces(frame).size(), 2u);
  EXPECT_NEAR(diagnostic_area(frame, 1, 1), 7.0, 0.00001);
  EXPECT_NEAR(diagnostic_area(frame, 2, 1), 7.0, 0.00001);
  const auto expect_octagon = [](QVec3 point) {
    EXPECT_LE(std::abs(point.x), 1.50001f);
    EXPECT_LE(std::abs(point.y), 1.50001f);
    EXPECT_LE(std::abs(point.x) + std::abs(point.y), 2.00001f);
    EXPECT_FLOAT_EQ(point.z, 0.0f);
  };
  for (const auto &triangle : frame.triangles) {
    expect_octagon(triangle.a);
    expect_octagon(triangle.b);
    expect_octagon(triangle.c);
  }
  for (const auto &line : frame.lines) {
    expect_octagon(line.start);
    expect_octagon(line.end);
    for (const auto endpoint : {line.start, line.end}) {
      if (line.element.object_id == 1) {
        EXPECT_NEAR(std::abs(endpoint.x) + std::abs(endpoint.y), 2.0f, 0.00001f);
      } else {
        EXPECT_TRUE(std::abs(std::abs(endpoint.x) - 1.5f) <= 0.00001f ||
                    std::abs(std::abs(endpoint.y) - 1.5f) <= 0.00001f);
      }
    }
  }
  EXPECT_EQ(frame.lines.size(), 8u);
}

TEST(ModelingLaminaOverlayServiceTests, ConcavePeerAreaOverlapDoesNotMarkItsSeparateEdgeOnlyContact) {
  LaminaOverlayFixture fixture;
  fixture.add_object(1, {{{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 0}}});
  fixture.add_object(2, {
      {{-1, -1, 0}, {3, -1, 0}, {3, 0, 0}, {1, 0, 0}, {1, 3, 0}, {-1, 3, 0}}});
  const auto &frame = fixture.update();
  EXPECT_EQ(diagnostic_faces(frame).size(), 2u);
  EXPECT_NEAR(diagnostic_area(frame, 1, 1), 3.0, 0.00001);
  EXPECT_NEAR(diagnostic_area(frame, 2, 1), 3.0, 0.00001);
  const auto expect_positive_patch = [](QVec3 point) {
    EXPECT_GE(point.x, -0.00001f);
    EXPECT_LE(point.x, 1.00001f)
        << "The separate contact at X=1..3, Y=0 has no area and must stay clear.";
    EXPECT_GE(point.y, -0.00001f);
    EXPECT_LE(point.y, 3.00001f);
    EXPECT_FLOAT_EQ(point.z, 0.0f);
  };
  for (const auto &triangle : frame.triangles) {
    expect_positive_patch(triangle.a);
    expect_positive_patch(triangle.b);
    expect_positive_patch(triangle.c);
  }
  for (const auto &line : frame.lines) {
    expect_positive_patch(line.start);
    expect_positive_patch(line.end);
  }
  EXPECT_EQ(frame.lines.size(), 4u);
}

TEST(ModelingLaminaOverlayServiceTests, ContactsCrossingsNearbyPlanesAndSeparatedFacesStayClear) {
  const std::vector<std::vector<QVec3>> candidates{
      {{2, 0, 0}, {4, 0, 0}, {4, 2, 0}, {2, 2, 0}},
      {{2, 2, 0}, {3, 2, 0}, {3, 3, 0}, {2, 3, 0}},
      {{0, 0, 0.0001f}, {2, 0, 0.0001f}, {2, 2, 0.0001f}, {0, 2, 0.0001f}},
      {{3, 0, 0}, {4, 0, 0}, {4, 1, 0}, {3, 1, 0}},
      {{1, 0, -1}, {1, 2, -1}, {1, 2, 1}, {1, 0, 1}}};
  for (std::size_t index = 0u; index < candidates.size(); ++index) {
    SCOPED_TRACE(index);
    LaminaOverlayFixture fixture;
    fixture.add_object(1, {{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}}});
    fixture.add_object(2, {candidates[index]});
    EXPECT_TRUE(fixture.update().triangles.empty());
    EXPECT_TRUE(fixture.update().lines.empty());
  }
}

TEST(ModelingLaminaOverlayServiceTests, PlacementVisibilityAndSelectionIndependentRetention) {
  LaminaOverlayFixture fixture;
  fixture.add_object(1, {{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}}});
  fixture.add_object(2, {{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}}});
  TransformComponent placement;
  placement.location = {4, 2, 3};
  placement.rotation_euler = {0.4f, 0.3f, 0.7f};
  placement.scale = {-1.5f, 0.75f, 1};
  ASSERT_TRUE(fixture.scene.set_transform(1u, placement));
  ASSERT_TRUE(fixture.scene.set_transform(2u, placement));
  EXPECT_EQ(diagnostic_faces(fixture.update()).size(), 2u);
  const auto initial_work = fixture.service.stats();
  for (int unchanged = 0; unchanged < 5; ++unchanged) {
    auto snapshot = fixture.snapshot();
    snapshot = snapshot.with_selection(fixture.objects,
        quader_editor_core::SelectionMode::Object, 2, 10u + unchanged);
    (void)fixture.service.update(snapshot, fixture.scene, fixture.object_map,
                                 fixture.selection, fixture.projection);
  }
  EXPECT_EQ(fixture.service.build_count(), initial_work.build_count);
  EXPECT_EQ(fixture.service.stats().local_geometry_build_count,
            initial_work.local_geometry_build_count);
  EXPECT_EQ(fixture.service.stats().preview_extraction_count,
            initial_work.preview_extraction_count);
  ASSERT_TRUE(fixture.scene.set_visible(2u, false));
  EXPECT_TRUE(fixture.update().triangles.empty());
  ASSERT_TRUE(fixture.scene.set_visible(2u, true));
  EXPECT_EQ(diagnostic_faces(fixture.update()).size(), 2u);
  placement.location.x += 10;
  ASSERT_TRUE(fixture.scene.set_transform(2u, placement));
  EXPECT_TRUE(fixture.update().triangles.empty());
  EXPECT_EQ(fixture.service.stats().local_geometry_build_count,
            initial_work.local_geometry_build_count + 1u)
      << "A hidden object is evicted, then its immutable local geometry is reacquired once.";
}

TEST(ModelingLaminaOverlayServiceTests, AcceptedComponentPreviewAppearsRetiresCancelsAndCommits) {
  LaminaOverlayFixture fixture;
  const std::array<QVec3, 4u> first_points{
      QVec3{0, 0, 0}, QVec3{2, 0, 0}, QVec3{2, 2, 0}, QVec3{0, 2, 0}};
  const std::array<QVec3, 4u> second_points{
      QVec3{5, 0, 0}, QVec3{7, 0, 0}, QVec3{7, 2, 0}, QVec3{5, 2, 0}};
  const auto first = fixture.projection.create_face(first_points, {0, 0, 1}, "First");
  const auto second = fixture.projection.create_face(second_points, {0, 0, 1}, "Second");
  ASSERT_TRUE(first.ok) << first.message;
  ASSERT_TRUE(second.ok) << second.message;
  const std::array selected{second.object.entity};
  ASSERT_TRUE(fixture.projection.set_object_selection(selected, second.object.entity).ok);
  ASSERT_TRUE(fixture.projection.set_selection_mode(ModelingSelectionMode::Face).ok);
  const auto &source = fixture.projection.selection_projection_snapshot();
  const auto object = std::ranges::find(source.objects(), second.object.modeling_object_id,
                                       &quader_editor_core::PolygonObject::id);
  ASSERT_NE(object, source.objects().end());
  ASSERT_FALSE(object->document.faces.empty());
  const int face_id = static_cast<int>(object->document.faces.front().id);
  ASSERT_TRUE(fixture.projection.set_component_selection({
      .modeling_object_id = second.object.modeling_object_id,
      .mode = ModelingSelectionMode::Face, .faces = {face_id},
      .active_kind = ModelingActiveSelectionKind::Face, .active_face = face_id}).ok);
  const auto update = [&]() -> const ViewportOverlayFrame & {
    return fixture.service.update(fixture.projection.selection_projection_snapshot(),
        fixture.scene, fixture.object_map, fixture.selection, fixture.projection);
  };
  EXPECT_TRUE(update().triangles.empty());
  ASSERT_TRUE(fixture.projection.begin_translate_preview(false, true).ok);
  ASSERT_TRUE(fixture.projection.update_translate_preview({-5, 0, 0}).ok);
  EXPECT_EQ(diagnostic_faces(update()).size(), 2u);
  const auto accepted_work = fixture.service.stats();
  for (int unchanged = 0; unchanged < 4; ++unchanged) (void)update();
  EXPECT_EQ(fixture.service.build_count(), accepted_work.build_count);
  EXPECT_EQ(fixture.service.stats().preview_extraction_count,
            accepted_work.preview_extraction_count);
  EXPECT_EQ(fixture.service.stats().world_face_build_count,
            accepted_work.world_face_build_count);
  ASSERT_TRUE(fixture.projection.update_translate_preview({0, 0, 0}).ok);
  EXPECT_TRUE(update().triangles.empty());
  ASSERT_TRUE(fixture.projection.update_translate_preview({-4, 0, 0}).ok);
  EXPECT_EQ(diagnostic_faces(update()).size(), 2u);
  ASSERT_TRUE(fixture.projection.cancel_translate_preview().ok);
  EXPECT_TRUE(update().triangles.empty());
  ASSERT_TRUE(fixture.projection.begin_translate_preview(false, true).ok);
  ASSERT_TRUE(fixture.projection.update_translate_preview({-5, 0, 0}).ok);
  EXPECT_EQ(diagnostic_faces(update()).size(), 2u);
  ASSERT_TRUE(fixture.projection.commit_translate_preview().ok);
  EXPECT_EQ(diagnostic_faces(update()).size(), 2u);
}

TEST(ModelingLaminaOverlayServiceTests, BroadphaseFollowsSpacedYAndZFacesAndRetainsUnchangedWork) {
  constexpr int kObjectCount = 48;
  for (const bool along_z : {false, true}) {
    SCOPED_TRACE(along_z);
    LaminaOverlayFixture fixture;
    for (int index = 0; index < kObjectCount; ++index) {
      const float offset = static_cast<float>(index) * 4.0f;
      const float y = along_z ? 0.0f : offset;
      const float z = along_z ? offset : 0.0f;
      fixture.add_object(index + 1, {
          {{0, y, z}, {1, y, z}, {1, y + 1, z}, {0, y + 1, z}}});
    }
    EXPECT_TRUE(fixture.update().triangles.empty());
    const auto work = fixture.service.stats();
    EXPECT_GT(work.broadphase_comparison_count, 0u);
    EXPECT_LT(work.broadphase_comparison_count,
              static_cast<std::uint64_t>(kObjectCount * 2));
    EXPECT_EQ(work.candidate_pair_count, 0u);
    for (int unchanged = 0; unchanged < 5; ++unchanged) (void)fixture.update();
    EXPECT_EQ(fixture.service.stats().broadphase_comparison_count,
              work.broadphase_comparison_count);
    EXPECT_EQ(fixture.service.stats().world_face_build_count, work.world_face_build_count);
    EXPECT_EQ(fixture.service.build_count(), work.build_count);
  }
}

} // namespace
} // namespace quader
