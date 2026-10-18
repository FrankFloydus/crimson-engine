////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/modeling.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <numbers>
#include <numeric>
#include <set>
#include <utility>
#include <vector>

namespace quader::modeling {
namespace {

[[nodiscard]] double determinant(Vec3 a, Vec3 b, Vec3 c) {
  return static_cast<double>(a.x) * (static_cast<double>(b.y) * c.z -
                                     static_cast<double>(b.z) * c.y) -
         static_cast<double>(a.y) * (static_cast<double>(b.x) * c.z -
                                     static_cast<double>(b.z) * c.x) +
         static_cast<double>(a.z) * (static_cast<double>(b.x) * c.y -
                                     static_cast<double>(b.y) * c.x);
}

[[nodiscard]] double signed_volume(const AuthoredTopology &topology) {
  std::map<std::uint32_t, Vec3> positions;
  for (const auto &vertex : topology.vertices) {
    positions.emplace(vertex.id.index, vertex.position);
  }
  double volume = 0.0;
  for (const auto &face : topology.faces) {
    const Vec3 first = positions.at(face.vertices.front().index);
    for (std::size_t corner = 1U; corner + 1U < face.vertices.size(); ++corner) {
      volume += determinant(first, positions.at(face.vertices[corner].index),
                            positions.at(face.vertices[corner + 1U].index)) / 6.0;
    }
  }
  return volume;
}

void expect_closed_mesh(const StairsMesh &mesh) {
  const auto validation = mesh.document.validate();
  ASSERT_TRUE(validation.ok()) << validation.error_message();
  ASSERT_TRUE(validation.value().success) << validation.value().message;
  const auto authored = mesh.document.authored_topology();
  ASSERT_TRUE(authored.ok()) << authored.error_message();
  const auto &topology = authored.value();
  ASSERT_FALSE(topology.vertices.empty());
  ASSERT_FALSE(topology.faces.empty());
  std::map<std::pair<std::uint32_t, std::uint32_t>, std::pair<int, int>> edges;
  std::map<std::uint32_t, std::vector<std::uint32_t>> neighbors;
  for (const auto &face : topology.faces) {
    ASSERT_GE(face.vertices.size(), 3U);
    for (std::size_t corner = 0U; corner < face.vertices.size(); ++corner) {
      const auto first = face.vertices[corner].index;
      const auto second = face.vertices[(corner + 1U) % face.vertices.size()].index;
      ASSERT_NE(first, second);
      auto &edge = edges[std::minmax(first, second)];
      ++edge.first;
      edge.second += first < second ? 1 : -1;
      neighbors[first].push_back(second);
      neighbors[second].push_back(first);
    }
  }
  for (const auto &[key, edge] : edges) {
    EXPECT_EQ(edge.first, 2) << key.first << ", " << key.second;
    EXPECT_EQ(edge.second, 0) << key.first << ", " << key.second;
  }
  std::set<std::uint32_t> reached;
  std::vector<std::uint32_t> pending{topology.vertices.front().id.index};
  while (!pending.empty()) {
    const auto vertex = pending.back();
    pending.pop_back();
    if (reached.insert(vertex).second) {
      const auto &adjacent = neighbors.at(vertex);
      pending.insert(pending.end(), adjacent.begin(), adjacent.end());
    }
  }
  EXPECT_EQ(reached.size(), topology.vertices.size());
  EXPECT_EQ(static_cast<std::int64_t>(topology.vertices.size()) -
                static_cast<std::int64_t>(edges.size()) +
                static_cast<std::int64_t>(topology.faces.size()), 2);
  EXPECT_GT(signed_volume(topology), 0.0);
  const auto compiled = mesh.document.compile_mesh();
  ASSERT_TRUE(compiled.ok()) << compiled.error_message();
  EXPECT_FALSE(compiled.value().indices.empty());
  for (const auto &vertex : compiled.value().vertices) {
    EXPECT_TRUE(std::isfinite(vertex.normal.x));
    EXPECT_TRUE(std::isfinite(vertex.normal.y));
    EXPECT_TRUE(std::isfinite(vertex.normal.z));
    EXPECT_TRUE(std::isfinite(vertex.uv0.x));
    EXPECT_TRUE(std::isfinite(vertex.uv0.y));
  }
}

[[nodiscard]] std::pair<double, double> vertical_bounds(const StairsMesh &mesh) {
  const auto topology = mesh.document.authored_topology().or_throw();
  double minimum = std::numeric_limits<double>::max();
  double maximum = std::numeric_limits<double>::lowest();
  for (const auto &vertex : topology.vertices) {
    minimum = std::min(minimum, static_cast<double>(vertex.position.y));
    maximum = std::max(maximum, static_cast<double>(vertex.position.y));
  }
  return {minimum, maximum};
}

[[nodiscard]] double upper_surface_at(const StairsMesh &mesh,
                                      double x, double z) {
  const auto topology = mesh.document.authored_topology().or_throw();
  std::map<std::uint32_t, Vec3> positions;
  for (const auto &vertex : topology.vertices) {
    positions.emplace(vertex.id.index, vertex.position);
  }
  double highest = std::numeric_limits<double>::lowest();
  const auto area = [](double ax, double az, double bx, double bz,
                       double cx, double cz) {
    return (bx - ax) * (cz - az) - (bz - az) * (cx - ax);
  };
  for (const auto &face : topology.faces) {
    const Vec3 a = positions.at(face.vertices.front().index);
    for (std::size_t corner = 1U; corner + 1U < face.vertices.size(); ++corner) {
      const Vec3 b = positions.at(face.vertices[corner].index);
      const Vec3 c = positions.at(face.vertices[corner + 1U].index);
      const double denominator = area(a.x, a.z, b.x, b.z, c.x, c.z);
      if (denominator >= -1.0e-12) {
        continue;
      }
      const double first = area(x, z, b.x, b.z, c.x, c.z) / denominator;
      const double second = area(a.x, a.z, x, z, c.x, c.z) / denominator;
      const double third = 1.0 - first - second;
      if (first >= -1.0e-7 && second >= -1.0e-7 && third >= -1.0e-7) {
        highest = std::max(highest, first * a.y + second * b.y + third * c.y);
      }
    }
  }
  return highest;
}

TEST(ModelingStairsTests, PresetsProduceOneClosedEditableBoundaryInBothDirections) {
  for (const auto layout : {StairsLayout::Straight, StairsLayout::L,
                            StairsLayout::U, StairsLayout::Switchback,
                            StairsLayout::Arc, StairsLayout::Spiral}) {
    for (const auto surface : {StairsSurface::Steps, StairsSurface::Ramp}) {
      for (const auto construction : {StairsConstruction::Solid,
                                      StairsConstruction::Slab}) {
        for (const bool clockwise : {false, true}) {
          SCOPED_TRACE(::testing::Message() << static_cast<int>(layout) << ", "
              << static_cast<int>(surface) << ", " << static_cast<int>(construction)
              << ", " << clockwise);
          StairsSpec spec;
          spec.layout = layout;
          spec.surface = surface;
          spec.construction = construction;
          spec.clockwise = clockwise;
          spec.turns = construction == StairsConstruction::Solid ? 0.75 : 1.5;
          spec.height = 4.0;
          const auto result = build_stairs_mesh(spec);
          ASSERT_TRUE(result.ok()) << result.error_message();
          expect_closed_mesh(result.value());
          const auto [minimum, maximum] = vertical_bounds(result.value());
          EXPECT_NEAR(minimum, construction == StairsConstruction::Solid
                                   ? 0.0 : -spec.slab_thickness, 1.0e-6);
          EXPECT_NEAR(maximum, spec.height, 1.0e-6);
        }
      }
    }
  }
}

TEST(ModelingStairsTests, StraightVolumeDistinguishesSolidAndSlabConstruction) {
  for (const auto surface : {StairsSurface::Steps, StairsSurface::Ramp}) {
    for (const auto construction : {StairsConstruction::Solid,
                                    StairsConstruction::Slab}) {
      StairsSpec spec;
      spec.surface = surface;
      spec.construction = construction;
      spec.automatic_step_count = false;
      spec.step_count = 7U;
      const auto result = build_stairs_mesh(spec);
      ASSERT_TRUE(result.ok()) << result.error_message();
      const double step_extra = surface == StairsSurface::Steps
                                   ? spec.height / (2.0 * spec.step_count) : 0.0;
      const double mean_depth = construction == StairsConstruction::Solid
                                    ? spec.height * 0.5 + step_extra
                                    : spec.slab_thickness + step_extra;
      const double expected = spec.footprint_width * spec.footprint_depth * mean_depth;
      EXPECT_NEAR(signed_volume(result.value().document.authored_topology().or_throw()),
                  expected, 1.0e-5);
    }
  }
}

TEST(ModelingStairsTests, CountsFitHeightGloballyAndLandingsUseWholeRisers) {
  StairsSpec decimal;
  decimal.height = 2.1;
  decimal.preferred_riser = 0.3;
  const auto decimal_result = build_stairs_mesh(decimal);
  ASSERT_TRUE(decimal_result.ok()) << decimal_result.error_message();
  EXPECT_EQ(decimal_result.value().effective_step_count, 7U);
  StairsSpec spec;
  spec.layout = StairsLayout::Switchback;
  spec.flight_count = 4U;
  spec.height = 2.3;
  spec.preferred_riser = 0.3;
  const auto result = build_stairs_mesh(spec);
  ASSERT_TRUE(result.ok()) << result.error_message();
  EXPECT_EQ(result.value().effective_step_count, 8U);
  EXPECT_NEAR(result.value().riser_height, 2.3 / 8.0, 1.0e-12);
  EXPECT_EQ(std::accumulate(result.value().flight_step_counts.begin(),
                           result.value().flight_step_counts.end(), 0U), 8U);
  EXPECT_EQ(result.value().flight_step_counts.size(), 4U);
  for (const auto count : result.value().flight_step_counts) {
    EXPECT_GE(count, 1U);
  }
  const auto topology = result.value().document.authored_topology().or_throw();
  for (const auto &vertex : topology.vertices) {
    // Solid underside and all authored stair/landing vertices lie on riser levels.
    const double level = vertex.position.y / result.value().riser_height;
    EXPECT_NEAR(level, std::round(level), 2.0e-6);
  }
  spec.automatic_step_count = false;
  spec.step_count = 3U;
  EXPECT_FALSE(build_stairs_mesh(spec).ok());
  spec.step_count = 11U;
  const auto manual = build_stairs_mesh(spec);
  ASSERT_TRUE(manual.ok()) << manual.error_message();
  EXPECT_EQ(manual.value().effective_step_count, 11U);
  EXPECT_NEAR(vertical_bounds(manual.value()).second, spec.height, 1.0e-6);
}

TEST(ModelingStairsTests, FlightCountsAreProportionalBeforeApplyingMinimums) {
  StairsSpec spec;
  spec.layout = StairsLayout::L;
  spec.footprint_width = 2.0;
  spec.footprint_depth = 10.0;
  spec.automatic_step_count = false;
  spec.step_count = 10U;
  const auto proportional = build_stairs_mesh(spec);
  ASSERT_TRUE(proportional.ok()) << proportional.error_message();
  EXPECT_EQ(proportional.value().flight_step_counts,
            (std::vector<std::uint32_t>{9U, 1U}));
  EXPECT_NEAR(proportional.value().flight_tread_depths[0], 1.0, 1.0e-12);
  EXPECT_NEAR(proportional.value().flight_tread_depths[1], 1.0, 1.0e-12);
  spec.step_count = 2U;
  const auto constrained = build_stairs_mesh(spec);
  ASSERT_TRUE(constrained.ok()) << constrained.error_message();
  EXPECT_EQ(constrained.value().flight_step_counts,
            (std::vector<std::uint32_t>{1U, 1U}));
}

TEST(ModelingStairsTests, IndependentCoordinateScalesPreserveRisersAndFootprints) {
  for (const int large_axis : {0, 1, 2}) {
    SCOPED_TRACE(large_axis);
    StairsSpec spec;
    spec.automatic_step_count = false;
    spec.step_count = 10U;
    if (large_axis == 0) {
      spec.footprint_width = 1.0e12;
    } else if (large_axis == 1) {
      spec.height = 1.0e15;
    } else {
      spec.footprint_depth = 1.0e12;
    }
    const auto result = build_stairs_mesh(spec);
    ASSERT_TRUE(result.ok()) << result.error_message();
    const auto topology = result.value().document.authored_topology().or_throw();
    std::set<float> elevations;
    float largest_x = 0.0F;
    float largest_z = 0.0F;
    for (const auto &vertex : topology.vertices) {
      elevations.insert(vertex.position.y);
      largest_x = std::max(largest_x, std::abs(vertex.position.x));
      largest_z = std::max(largest_z, std::abs(vertex.position.z));
    }
    EXPECT_EQ(elevations.size(), 11U);
    EXPECT_FLOAT_EQ(largest_x, static_cast<float>(spec.footprint_width * 0.5));
    EXPECT_FLOAT_EQ(largest_z, static_cast<float>(spec.footprint_depth * 0.5));
    EXPECT_FLOAT_EQ(*elevations.rbegin(), static_cast<float>(spec.height));
    expect_closed_mesh(result.value());
  }
}

TEST(ModelingStairsTests, CurvedRampInterpolatesItsExplicitTrianglesWithoutTwisting) {
  for (const bool clockwise : {false, true}) {
    StairsSpec spec;
    spec.layout = StairsLayout::Arc;
    spec.surface = StairsSurface::Ramp;
    spec.construction = StairsConstruction::Slab;
    spec.curved_segments = 8U;
    spec.clockwise = clockwise;
    const auto result = build_stairs_mesh(spec);
    ASSERT_TRUE(result.ok()) << result.error_message();
    const double start_angle = -std::numbers::pi * 0.5;
    const double end_angle = start_angle +
        (clockwise ? 1.0 : -1.0) * std::numbers::pi * 0.25;
    const double left_radius = clockwise ? spec.inner_radius
                                         : spec.inner_radius + spec.flight_width;
    const double right_radius = clockwise ? spec.inner_radius + spec.flight_width
                                          : spec.inner_radius;
    const double x = (left_radius * std::cos(start_angle) +
                      left_radius * std::cos(end_angle) +
                      right_radius * std::cos(end_angle)) / 3.0;
    const double z = (left_radius * std::sin(start_angle) +
                      left_radius * std::sin(end_angle) +
                      right_radius * std::sin(end_angle)) / 3.0;
    EXPECT_NEAR(upper_surface_at(result.value(), x, z), spec.height / 3.0, 1.0e-6);
    EXPECT_EQ(result.value().effective_step_count, 0U);
    EXPECT_EQ(result.value().riser_height, 0.0);
    EXPECT_NEAR(result.value().slope_degrees,
        std::atan(spec.height / ((spec.inner_radius + spec.flight_width * 0.5) *
                                 std::numbers::pi * 0.5)) *
            180.0 / std::numbers::pi, 1.0e-10);
    expect_closed_mesh(result.value());
  }
}

TEST(ModelingStairsTests, CurvedTreadBoundariesRemainHorizontalAcrossTessellation) {
  StairsSpec spec;
  spec.layout = StairsLayout::Arc;
  spec.automatic_step_count = false;
  spec.step_count = 7U;
  spec.curved_segments = 29U;
  const auto result = build_stairs_mesh(spec);
  ASSERT_TRUE(result.ok()) << result.error_message();
  const double radius = spec.inner_radius + spec.flight_width * 0.5;
  for (std::uint32_t step = 0U; step < spec.step_count; ++step) {
    const double fraction = (step + 0.5) / spec.step_count;
    const double angle = -std::numbers::pi * 0.5 -
                         fraction * std::numbers::pi * 0.5;
    EXPECT_NEAR(upper_surface_at(result.value(), radius * std::cos(angle),
                                 radius * std::sin(angle)),
                spec.height * (step + 1U) / spec.step_count, 2.0e-6);
  }
  expect_closed_mesh(result.value());
}

TEST(ModelingStairsTests, StackedSwitchbacksAndSpiralsRequireBodyClearance) {
  for (const auto layout : {StairsLayout::Switchback, StairsLayout::Spiral}) {
    StairsSpec spec;
    spec.layout = layout;
    spec.switchback_arrangement = StairsSwitchbackArrangement::Stacked;
    spec.turns = 2.0;
    spec.height = 6.0;
    spec.construction = StairsConstruction::Slab;
    const auto result = build_stairs_mesh(spec);
    ASSERT_TRUE(result.ok()) << result.error_message();
    expect_closed_mesh(result.value());
    spec.slab_thickness = 5.0;
    EXPECT_FALSE(build_stairs_mesh(spec).ok());
    spec.slab_thickness = 0.2;
    spec.construction = StairsConstruction::Solid;
    EXPECT_FALSE(build_stairs_mesh(spec).ok());
  }
  StairsSpec contact;
  contact.layout = StairsLayout::Spiral;
  contact.surface = StairsSurface::Ramp;
  contact.construction = StairsConstruction::Slab;
  contact.height = contact.slab_thickness;
  EXPECT_FALSE(build_stairs_mesh(contact).ok());
  contact.height = contact.slab_thickness + 0.1;
  const auto separated = build_stairs_mesh(contact);
  ASSERT_TRUE(separated.ok()) << separated.error_message();
  expect_closed_mesh(separated.value());
}

TEST(ModelingStairsTests, SpiralClearanceUsesTheEmittedFloatSurfaces) {
  StairsSpec spec;
  spec.layout = StairsLayout::Spiral;
  spec.surface = StairsSurface::Ramp;
  spec.construction = StairsConstruction::Slab;
  spec.turns = 1.6;
  spec.curved_segments = 8U;
  // These mismatched stations have 1e-9 clearance before narrowing, but the
  // emitted float triangles overlap by approximately 1.77e-8 between vertices.
  spec.slab_thickness = 1.2417770644287949;
  EXPECT_FALSE(build_stairs_mesh(spec).ok());
  spec.slab_thickness = 1.241767065428795;
  const auto separated = build_stairs_mesh(spec);
  ASSERT_TRUE(separated.ok()) << separated.error_message();
  expect_closed_mesh(separated.value());
}

TEST(ModelingStairsTests, FootprintRulesRetainCircularAndMultiFlightDimensions) {
  StairsSpec spec;
  spec.layout = StairsLayout::U;
  spec.flight_width = 0.7;
  spec.gap = 0.3;
  auto result = build_stairs_mesh(spec);
  ASSERT_TRUE(result.ok()) << result.error_message();
  EXPECT_NEAR(result.value().footprint_width, 1.7, 1.0e-12);
  spec.layout = StairsLayout::Switchback;
  spec.flight_count = 4U;
  result = build_stairs_mesh(spec);
  ASSERT_TRUE(result.ok()) << result.error_message();
  EXPECT_NEAR(result.value().footprint_width, 3.7, 1.0e-12);
  spec.layout = StairsLayout::Arc;
  spec.footprint_width = 100.0;
  spec.footprint_depth = 200.0;
  result = build_stairs_mesh(spec);
  ASSERT_TRUE(result.ok()) << result.error_message();
  EXPECT_NEAR(result.value().footprint_width, 3.4, 1.0e-12);
  EXPECT_EQ(result.value().footprint_width, result.value().footprint_depth);
}

TEST(ModelingStairsTests, InvalidInputsAndLimitsFailWithoutPartialDocuments) {
  for (const double invalid : {0.0, -1.0,
          std::numeric_limits<double>::infinity(),
          std::numeric_limits<double>::quiet_NaN()}) {
    StairsSpec spec;
    spec.height = invalid;
    EXPECT_FALSE(build_stairs_mesh(spec).ok());
    spec = {};
    spec.layout = StairsLayout::U;
    spec.gap = invalid;
    EXPECT_FALSE(build_stairs_mesh(spec).ok());
    spec = {};
    spec.layout = StairsLayout::Arc;
    spec.inner_radius = invalid;
    EXPECT_FALSE(build_stairs_mesh(spec).ok());
  }
  StairsSpec spec;
  spec.layout = static_cast<StairsLayout>(99);
  EXPECT_FALSE(build_stairs_mesh(spec).ok());
  spec = {};
  spec.preferred_riser = spec.height / (kStairsMaximumStepCount + 1U);
  EXPECT_FALSE(build_stairs_mesh(spec).ok());
  spec = {};
  spec.layout = StairsLayout::L;
  spec.landing_depth = 0.5;
  EXPECT_FALSE(build_stairs_mesh(spec).ok());
  spec = {};
  spec.layout = StairsLayout::Arc;
  spec.sweep_degrees = 360.0;
  EXPECT_FALSE(build_stairs_mesh(spec).ok());
  spec = {};
  spec.automatic_step_count = false;
  spec.step_count = kStairsMaximumStepCount;
  const auto maximum = build_stairs_mesh(spec);
  ASSERT_TRUE(maximum.ok()) << maximum.error_message();
  EXPECT_EQ(maximum.value().effective_step_count, kStairsMaximumStepCount);
  expect_closed_mesh(maximum.value());
}

} // namespace
} // namespace quader::modeling
