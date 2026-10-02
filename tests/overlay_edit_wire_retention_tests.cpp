////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/overlay_edit_wire_retention.hpp"
#include "render/overlay/overlay_proxy_factory.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <limits>
#include <vector>

namespace quader {
namespace {

ViewportOverlayLine source_edge(int object, int first, int second) {
  return {.layer = ViewportOverlayLayer::SourceWire,
          .start = {static_cast<float>(first), 0.0f, 0.0f},
          .end = {static_cast<float>(second), 1.0f, 0.0f},
          .color = 0xffaabbccu, .width_pixels = 2.0f,
          .source = ViewportOverlaySource::ComponentSelection,
          .element = {.object_id = object, .edge_a = first, .edge_b = second}};
}

TEST(OverlayEditWireRetentionTests, SubsetReorderAndReturnPreserveUnchangedEndpointSlots) {
  OverlayEditWireRetention retention;
  std::vector lines{source_edge(1, 0, 1), source_edge(1, 1, 2), source_edge(2, 0, 1)};
  ASSERT_TRUE(retention.prepare(lines));
  EXPECT_TRUE(retention.requires_recreation());
  ASSERT_EQ(retention.active_bindings().size(), 12u);
  EXPECT_EQ(retention.active_bindings()[8].object_id, 2);
  const auto second_slot = retention.active_bindings()[4].first_vertex;
  const auto third_slot = retention.active_bindings()[8].first_vertex;

  std::vector subset{lines[2], lines[1]};
  ASSERT_TRUE(retention.prepare(subset));
  EXPECT_FALSE(retention.requires_recreation());
  EXPECT_TRUE(retention.dirty_slots().empty());
  EXPECT_TRUE(retention.indices_changed());
  EXPECT_EQ(retention.edge_slots().size(), 3u);
  ASSERT_EQ(retention.active_indices().size(), 12u);
  EXPECT_EQ(retention.active_indices()[0], third_slot);
  EXPECT_EQ(retention.active_indices()[6], second_slot);
  EXPECT_EQ(retention.active_bindings()[0].object_id, 2);

  ASSERT_TRUE(retention.prepare(subset));
  EXPECT_FALSE(retention.indices_changed());
  EXPECT_TRUE(retention.dirty_slots().empty());
  ASSERT_TRUE(retention.prepare(lines));
  EXPECT_FALSE(retention.requires_recreation());
  ASSERT_EQ(retention.dirty_slots().size(), 1u);
  EXPECT_EQ(retention.active_bindings()[4].first_vertex, second_slot);
  EXPECT_EQ(retention.active_bindings()[8].first_vertex, third_slot);
}

TEST(OverlayEditWireRetentionTests, GeometryColorAndReversedOrientationRefreshExactBindings) {
  OverlayEditWireRetention retention;
  std::vector lines{source_edge(1, 0, 1), source_edge(1, 1, 2)};
  ASSERT_TRUE(retention.prepare(lines));
  lines[1].start.x += 7.0f;
  ASSERT_TRUE(retention.prepare(lines));
  EXPECT_FALSE(retention.requires_recreation());
  EXPECT_FALSE(retention.indices_changed());
  ASSERT_EQ(retention.dirty_slots().size(), 1u);
  EXPECT_EQ(retention.active_bindings()[4].base_position.x, lines[1].start.x);
  lines[0].color = 0xff123456u;
  ASSERT_TRUE(retention.prepare(lines));
  ASSERT_EQ(retention.dirty_slots().size(), 1u);
  EXPECT_EQ(retention.edge_slots()[retention.dirty_slots()[0]].color, lines[0].color);
  std::swap(lines[0].element.edge_a, lines[0].element.edge_b);
  std::swap(lines[0].start, lines[0].end);
  ASSERT_TRUE(retention.prepare(lines));
  ASSERT_EQ(retention.dirty_slots().size(), 1u);
  EXPECT_EQ(retention.active_bindings()[0].vertex_id, 1);
  EXPECT_EQ(retention.active_bindings()[1].vertex_id, 0);
  EXPECT_EQ(retention.active_bindings()[0].base_position.x, lines[0].start.x);
  for (auto &line : lines) line.width_pixels = 4.0f;
  ASSERT_TRUE(retention.prepare(lines));
  EXPECT_TRUE(retention.dirty_slots().empty());
  EXPECT_FALSE(retention.indices_changed());
  EXPECT_EQ(retention.width_pixels(), 4.0f);
}

TEST(OverlayEditWireRetentionTests, OrderedPositionUpdatesMatchFreshOutputAcrossSparseSlots) {
  OverlayEditWireRetention retention;
  const std::vector population{source_edge(1, 0, 1), source_edge(1, 1, 2),
      source_edge(2, 0, 1), source_edge(2, 1, 2), source_edge(3, 0, 1), source_edge(3, 1, 2)};
  ASSERT_TRUE(retention.prepare(population));
  std::vector lines{population[4], population[1], population[5]};
  auto ignored = source_edge(9, 0, 1);
  ignored.layer = ViewportOverlayLayer::SelectedEdge;
  ignored.source = ViewportOverlaySource::ComponentHover;
  lines.insert(lines.begin() + 1, ignored);
  ASSERT_TRUE(retention.prepare(lines));
  const auto *slots = retention.edge_slots().data();
  const auto *indices = retention.active_indices().data();
  const auto *bindings = retention.active_bindings().data();
  const std::vector previous_indices(retention.active_indices().begin(), retention.active_indices().end());

  lines[0].start = {11.0f, 12.0f, 13.0f};
  lines[2].end = {21.0f, 22.0f, 23.0f};
  ASSERT_TRUE(retention.prepare(lines));
  OverlayEditWireRetention fresh;
  ASSERT_TRUE(fresh.prepare(lines));
  EXPECT_FALSE(retention.requires_recreation());
  EXPECT_FALSE(retention.indices_changed());
  EXPECT_EQ(retention.edge_slots().data(), slots);
  EXPECT_EQ(retention.active_indices().data(), indices);
  EXPECT_EQ(retention.active_bindings().data(), bindings);
  EXPECT_EQ(std::vector(retention.active_indices().begin(), retention.active_indices().end()), previous_indices);
  EXPECT_EQ(std::vector(retention.dirty_slots().begin(), retention.dirty_slots().end()),
      (std::vector<std::uint32_t>{1u, 4u}));
  ASSERT_EQ(retention.active_bindings().size(), fresh.active_bindings().size());
  for (std::size_t index = 0u; index < retention.active_bindings().size(); ++index) {
    const auto &actual = retention.active_bindings()[index];
    const auto &expected = fresh.active_bindings()[index];
    EXPECT_EQ(actual.object_id, expected.object_id);
    EXPECT_EQ(actual.vertex_id, expected.vertex_id);
    EXPECT_EQ(actual.vertex_count, expected.vertex_count);
    EXPECT_EQ(actual.stream, expected.stream);
    EXPECT_EQ(actual.base_position.x, expected.base_position.x);
    EXPECT_EQ(actual.base_position.y, expected.base_position.y);
    EXPECT_EQ(actual.base_position.z, expected.base_position.z);
    EXPECT_EQ(actual.base_position.w, expected.base_position.w);
    EXPECT_EQ(retention.edge_slots()[actual.first_vertex / 4u].color,
        fresh.edge_slots()[expected.first_vertex / 4u].color);
  }
  ASSERT_TRUE(retention.prepare(lines));
  EXPECT_TRUE(retention.dirty_slots().empty());
  EXPECT_FALSE(retention.indices_changed());

  std::swap(lines[0], lines[3]);
  ASSERT_TRUE(retention.prepare(lines));
  EXPECT_TRUE(retention.indices_changed());
  EXPECT_TRUE(retention.dirty_slots().empty());
  EXPECT_EQ(retention.active_indices()[0], 20u);
  EXPECT_EQ(retention.active_bindings()[0].vertex_id, lines[0].element.edge_a);
  EXPECT_EQ(retention.active_bindings()[0].base_position.x, lines[0].start.x);
}

TEST(OverlayEditWireRetentionTests, WarmOrderedPopulationRejectsInvalidTailAndDuplicateIdentity) {
  const std::vector valid{source_edge(1, 0, 1), source_edge(1, 1, 2)};
  std::vector invalid_cases(7u, valid);
  invalid_cases[0].back().source = ViewportOverlaySource::ComponentHover;
  invalid_cases[1].back().width_pixels = 9.0f;
  invalid_cases[2].back().end.z = std::numeric_limits<float>::infinity();
  invalid_cases[3].back().element.object_id = -1;
  invalid_cases[4].back().element.edge_b = invalid_cases[4].back().element.edge_a;
  invalid_cases[5].back() = valid.front();
  invalid_cases[6].back().width_pixels = std::numeric_limits<float>::infinity();
  for (auto &invalid : invalid_cases) {
    OverlayEditWireRetention retention;
    ASSERT_TRUE(retention.prepare(valid));
    ASSERT_TRUE(retention.prepare(valid));
    invalid.front().start.x += 10.0f;
    EXPECT_FALSE(retention.prepare(invalid));
    EXPECT_TRUE(retention.edge_slots().empty());
    EXPECT_TRUE(retention.active_bindings().empty());
    EXPECT_TRUE(retention.active_indices().empty());
    EXPECT_EQ(retention.active_edge_count(), 0u);
    ASSERT_TRUE(retention.prepare(valid));
    EXPECT_TRUE(retention.requires_recreation());
  }
}

TEST(OverlayEditWireRetentionTests, AmbiguousOccurrencesAndIncompatibleBatchesFallBack) {
  OverlayEditWireRetention retention;
  const std::vector valid{source_edge(1, 0, 1), source_edge(1, 1, 2)};
  auto invalid = valid;
  invalid.push_back(valid[0]);
  invalid.back().start.x += 10.0f;
  ASSERT_TRUE(retention.prepare(valid));
  EXPECT_FALSE(retention.prepare(invalid));
  EXPECT_TRUE(retention.edge_slots().empty());
  invalid = valid;
  invalid.back().source = ViewportOverlaySource::ComponentHover;
  EXPECT_FALSE(retention.prepare(invalid));
  invalid = valid;
  invalid.back().width_pixels = 9.0f;
  EXPECT_FALSE(retention.prepare(invalid));
  invalid = valid;
  invalid.back().start.x = std::numeric_limits<float>::quiet_NaN();
  EXPECT_FALSE(retention.prepare(invalid));
  invalid = valid;
  invalid.back().element.object_id = -1;
  EXPECT_FALSE(retention.prepare(invalid));
  ASSERT_TRUE(retention.prepare(valid));
  EXPECT_TRUE(retention.requires_recreation());
}

TEST(OverlayEditWireRetentionTests, LargeSelectionShrinksBoundedCapacityAndReleasesEmptyPopulation) {
  OverlayEditWireRetention retention;
  std::vector<ViewportOverlayLine> lines;
  lines.reserve(6400u);
  for (int object = 0; object < 6400; ++object) lines.push_back(source_edge(object, 0, 1));
  ASSERT_TRUE(retention.prepare(lines));
  EXPECT_EQ(retention.active_edge_count(), 6400u);
  EXPECT_EQ(retention.edge_slots().size(), 6400u);
  lines.resize(1600u);
  ASSERT_TRUE(retention.prepare(lines));
  EXPECT_FALSE(retention.requires_recreation());
  EXPECT_TRUE(retention.dirty_slots().empty());
  lines.resize(1599u);
  ASSERT_TRUE(retention.prepare(lines));
  EXPECT_TRUE(retention.requires_recreation());
  EXPECT_EQ(retention.edge_slots().size(), 1599u);
  lines.push_back(source_edge(6401, 0, 1));
  ASSERT_TRUE(retention.prepare(lines));
  EXPECT_TRUE(retention.requires_recreation());
  EXPECT_EQ(retention.edge_slots().size(), 1600u);
  EXPECT_FALSE(retention.prepare({}));
  EXPECT_TRUE(retention.edge_slots().empty());
  EXPECT_TRUE(retention.active_bindings().empty());
  EXPECT_TRUE(retention.active_indices().empty());
  EXPECT_EQ(retention.active_edge_count(), 0u);
}

TEST(OverlayEditWireRetentionTests, FactoryRejectsMalformedIndicesAndBindingsBeforeGpuMutation) {
  OverlayEditWireRetention retention;
  const std::vector lines{source_edge(1, 0, 1), source_edge(1, 1, 2)};
  ASSERT_TRUE(retention.prepare(lines));
  ASSERT_TRUE(OverlayProxyFactory::prepare_retained_edit_wire_update(
      nullptr, retention.edge_slots(), retention.dirty_slots(), retention.active_indices(),
      retention.active_bindings(), true));
  auto indices = std::vector(retention.active_indices().begin(), retention.active_indices().end());
  indices.back() = std::numeric_limits<std::uint32_t>::max();
  EXPECT_FALSE(OverlayProxyFactory::prepare_retained_edit_wire_update(
      nullptr, retention.edge_slots(), retention.dirty_slots(), indices,
      retention.active_bindings(), true));
  auto bindings = std::vector(retention.active_bindings().begin(), retention.active_bindings().end());
  bindings.back().base_position.x += 1.0f;
  EXPECT_FALSE(OverlayProxyFactory::prepare_retained_edit_wire_update(
      nullptr, retention.edge_slots(), retention.dirty_slots(), retention.active_indices(), bindings, true));
  const std::vector<std::uint32_t> duplicate_dirty{0u, 0u};
  EXPECT_FALSE(OverlayProxyFactory::prepare_retained_edit_wire_update(
      nullptr, retention.edge_slots(), duplicate_dirty, retention.active_indices(),
      retention.active_bindings(), true));
}

TEST(OverlayEditWireRetentionTests, SupportOnlyUpdatesRetainSlotsAndRefreshPreviewBindings) {
  OverlayEditWireRetention retention;
  std::vector lines{source_edge(1, 0, 1), source_edge(1, 1, 2)};
  lines[1].surface_support = {ViewportOverlaySurfaceSupport{{3.0f, 4.0f, 5.0f}, 7},
                             ViewportOverlaySurfaceSupport{{6.0f, 7.0f, 8.0f}, 8}};
  ASSERT_TRUE(retention.prepare(lines));
  const auto *slots = retention.edge_slots().data();
  const auto *bindings = retention.active_bindings().data();
  EXPECT_EQ(bindings[2].vertex_id, lines[0].element.edge_a);
  EXPECT_EQ(bindings[2].base_position.x, lines[0].start.x);
  EXPECT_EQ(bindings[3].vertex_id, lines[0].element.edge_a);
  EXPECT_EQ(bindings[6].stream, OverlayPositionStream::Support0);
  EXPECT_EQ(bindings[6].vertex_id, 7);
  EXPECT_EQ(bindings[7].vertex_id, 8);
  lines[1].surface_support[0].position.z += 9.0f;
  lines[1].surface_support[1] = {};
  ASSERT_TRUE(retention.prepare(lines));
  EXPECT_EQ(retention.edge_slots().data(), slots);
  EXPECT_EQ(retention.active_bindings().data(), bindings);
  EXPECT_FALSE(retention.indices_changed());
  EXPECT_FALSE(retention.requires_recreation());
  ASSERT_EQ(retention.dirty_slots().size(), 1u);
  EXPECT_EQ(bindings[6].base_position.z, lines[1].surface_support[0].position.z);
  EXPECT_EQ(bindings[7].vertex_id, lines[1].element.edge_a);
  EXPECT_EQ(bindings[7].base_position.y, lines[1].start.y);
  ASSERT_TRUE(retention.prepare(lines));
  EXPECT_TRUE(retention.dirty_slots().empty());
}

ViewportOverlayTriangle source_surface(const ViewportOverlayLine &line,
                                       int face_id, int opposite_id, QVec3 opposite) {
  return {.layer = ViewportOverlayLayer::SourceWireDepthStamp,
          .a = line.start, .b = line.end, .c = opposite,
          .source = ViewportOverlaySource::ComponentSelection,
          .element = {.object_id = line.element.object_id, .vertex_id = line.element.edge_a,
                      .edge_a = line.element.edge_b, .edge_b = opposite_id, .face_id = face_id}};
}

TEST(OverlayEditWireRetentionTests, IncidentSurfacesFollowSourceStampWithoutCameraRebuilds) {
  ViewportOverlayFrame frame;
  frame.lines.push_back(source_edge(1, 0, 1));
  frame.triangles.push_back(source_surface(frame.lines[0], 10, 2, {0.0f, 2.0f, 3.0f}));
  frame.triangles.push_back(source_surface(frame.lines[0], 11, 3, {0.0f, -2.0f, 4.0f}));
  // A triangulated face contributes one plane per incident authored edge.
  frame.triangles.push_back(source_surface(frame.lines[0], 10, 4, {0.0f, 3.0f, 4.5f}));
  OverlayWireSurfaceSupportCache support;
  auto signatures = OverlayRenderableBuilder::frame_signatures(frame);
  support.prepare(frame, signatures);
  auto line = frame.lines[0];
  support.apply(line);
  EXPECT_EQ(line.surface_support[0].vertex_id, 2);
  EXPECT_EQ(line.surface_support[1].vertex_id, 3);
  EXPECT_EQ(line.surface_support[1].position.z, 4.0f);
  support.prepare(frame, signatures);
  EXPECT_EQ(support.build_count(), 1u);
  frame.lines[0].color = 0xff123456u;
  support.prepare(frame, OverlayRenderableBuilder::frame_signatures(frame));
  EXPECT_EQ(support.build_count(), 1u);

  ViewportOverlayCamera camera;
  const auto previous_build = OverlayRenderableBuilder::layer_build_signature(
      frame, camera, ViewportOverlayLayer::SourceWire);
  frame.triangles[1].c.z += 2.0f;
  signatures = OverlayRenderableBuilder::frame_signatures(frame);
  EXPECT_NE(previous_build, OverlayRenderableBuilder::layer_build_signature(
      signatures, camera, ViewportOverlayLayer::SourceWire));
  support.prepare(frame, signatures);
  EXPECT_EQ(support.build_count(), 2u);
  line = frame.lines[0];
  std::swap(line.start, line.end);
  std::swap(line.element.edge_a, line.element.edge_b);
  support.apply(line);
  EXPECT_EQ(line.surface_support[0].vertex_id, 2);
  EXPECT_EQ(line.surface_support[1].position.z, 6.0f);

  const auto plan = OverlayRenderableBuilder::build_layer(
      frame, camera, ViewportOverlayLayer::SourceWire);
  ASSERT_EQ(plan.edit_wire_batches.size(), 1u);
  ASSERT_EQ(plan.edit_wire_batches[0].mesh.vertices.size(), 4u);
  EXPECT_EQ(plan.edit_wire_batches[0].mesh.vertices[0].support0.z, 3.0f);
  EXPECT_EQ(plan.edit_wire_batches[0].mesh.vertices[0].support1.z, 6.0f);
}

TEST(OverlayEditWireRetentionTests, AmbiguousSurfaceOccurrencesAndNonmanifoldEdgesFallBack) {
  const auto line = source_edge(1, 0, 1);
  ViewportOverlayFrame frame;
  frame.lines.push_back(line);
  frame.triangles.push_back(source_surface(line, 10, 2, {0.0f, 2.0f, 3.0f}));
  OverlayWireSurfaceSupportCache support;
  support.prepare(frame, OverlayRenderableBuilder::frame_signatures(frame));
  auto displaced = line;
  displaced.start.x += 10.0f;
  displaced.end.x += 10.0f;
  support.apply(displaced);
  EXPECT_EQ(displaced.surface_support[0].vertex_id, -1);
  frame.triangles.push_back(source_surface(displaced, 11, 3, {10.0f, 2.0f, 3.0f}));
  support.prepare(frame, OverlayRenderableBuilder::frame_signatures(frame));
  auto ambiguous = line;
  support.apply(ambiguous);
  EXPECT_EQ(ambiguous.surface_support[0].vertex_id, -1);

  frame.triangles.resize(1u);
  frame.triangles.push_back(source_surface(line, 11, 3, {0.0f, -2.0f, 3.0f}));
  frame.triangles.push_back(source_surface(line, 12, 4, {0.0f, 3.0f, 4.0f}));
  support.prepare(frame, OverlayRenderableBuilder::frame_signatures(frame));
  ambiguous = line;
  support.apply(ambiguous);
  EXPECT_EQ(ambiguous.surface_support[0].vertex_id, -1);
  EXPECT_EQ(ambiguous.surface_support[1].vertex_id, -1);
}

} // namespace
} // namespace quader
