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
#include <optional>
#include <set>
#include <span>
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
  const auto compiled = mesh.document.compile_mesh().or_throw();
  double highest = std::numeric_limits<double>::lowest();
  const auto area = [](double ax, double az, double bx, double bz,
                       double cx, double cz) {
    return (bx - ax) * (cz - az) - (bz - az) * (cx - ax);
  };
  for (std::size_t triangle = 0U; triangle + 2U < compiled.indices.size(); triangle += 3U) {
    const Vec3 a = compiled.vertices[compiled.indices[triangle]].position;
    const Vec3 b = compiled.vertices[compiled.indices[triangle + 1U]].position;
    const Vec3 c = compiled.vertices[compiled.indices[triangle + 2U]].position;
    const double denominator = area(a.x, a.z, b.x, b.z, c.x, c.z);
    if (denominator >= -1.0e-12) continue;
    const double first = area(x, z, b.x, b.z, c.x, c.z) / denominator;
    const double second = area(a.x, a.z, x, z, c.x, c.z) / denominator;
    const double third = 1.0 - first - second;
    if (first >= -1.0e-7 && second >= -1.0e-7 && third >= -1.0e-7) {
      highest = std::max(highest, first * a.y + second * b.y + third * c.y);
    }
  }
  return highest;
}

[[nodiscard]] std::map<VertexId, Vec3>
authored_positions(const AuthoredTopology &topology) {
  std::map<VertexId, Vec3> positions;
  for (const auto &vertex : topology.vertices) positions.emplace(vertex.id, vertex.position);
  return positions;
}

[[nodiscard]] std::array<double, 3> face_area_vector(
    const AuthoredTopologyFace &face, const std::map<VertexId, Vec3> &positions) {
  const auto origin = positions.at(face.vertices.front());
  std::array<double, 3> area{};
  for (std::size_t corner = 1U; corner + 1U < face.vertices.size(); ++corner) {
    const auto a = positions.at(face.vertices[corner]);
    const auto b = positions.at(face.vertices[corner + 1U]);
    area[0] += (static_cast<double>(a.y) - origin.y) * (b.z - origin.z) -
               (static_cast<double>(a.z) - origin.z) * (b.y - origin.y);
    area[1] += (static_cast<double>(a.z) - origin.z) * (b.x - origin.x) -
               (static_cast<double>(a.x) - origin.x) * (b.z - origin.z);
    area[2] += (static_cast<double>(a.x) - origin.x) * (b.y - origin.y) -
               (static_cast<double>(a.y) - origin.y) * (b.x - origin.x);
  }
  return area;
}

[[nodiscard]] double face_upward_area(
    const AuthoredTopologyFace &face, const std::map<VertexId, Vec3> &positions) {
  return face_area_vector(face, positions)[1];
}

[[nodiscard]] bool vertical_face(
    const AuthoredTopologyFace &face, const std::map<VertexId, Vec3> &positions) {
  const auto area = face_area_vector(face, positions);
  return std::abs(area[1]) <= std::hypot(area[0], area[1], area[2]) * 1.0e-6;
}

[[nodiscard]] std::set<VertexId> walking_vertices(const AuthoredTopology &topology) {
  const auto positions = authored_positions(topology);
  std::set<VertexId> result;
  for (const auto &face : topology.faces) {
    const float height = positions.at(face.vertices.front()).y;
    if (face_upward_area(face, positions) > 0.0 &&
        std::ranges::all_of(face.vertices, [&](VertexId vertex) {
          return positions.at(vertex).y == height;
        })) result.insert(face.vertices.begin(), face.vertices.end());
  }
  return result;
}

[[nodiscard]] std::set<FaceId> wall_faces(const AuthoredTopology &topology,
                                        const StairsSpec &spec);

[[nodiscard]] std::map<EdgeKey, std::vector<const AuthoredTopologyFace *>>
authored_edge_faces(const AuthoredTopology &topology) {
  std::map<EdgeKey, std::vector<const AuthoredTopologyFace *>> adjacent;
  for (const auto &face : topology.faces) {
    for (std::size_t corner = 0U; corner < face.vertices.size(); ++corner) {
      adjacent[make_edge_key(face.vertices[corner],
          face.vertices[(corner + 1U) % face.vertices.size()])].push_back(&face);
    }
  }
  return adjacent;
}

[[nodiscard]] std::vector<EdgeKey>
landing_dividers(const AuthoredTopology &topology, bool top) {
  const auto positions = authored_positions(topology);
  const auto upper_guides = top ? std::vector<EdgeKey>{} : landing_dividers(topology, true);
  std::vector<EdgeKey> result;
  for (const auto &[edge, incident] : authored_edge_faces(topology)) {
    if (incident.size() != 2U) continue;
    const bool landing_pair = std::ranges::all_of(incident, [&](const auto *face) {
      if (face->vertices.size() <= 4U) return false;
      const double area = face_upward_area(*face, positions);
      if (top ? area <= 1.0e-10 : area >= -1.0e-10) return false;
      const double height = positions.at(face->vertices.front()).y;
      return std::ranges::all_of(face->vertices, [&](VertexId vertex) {
        return std::abs(positions.at(vertex).y - height) < 1.0e-6;
      });
    });
    if (!landing_pair) continue;
    if (!top) {
      const auto same_plan_point = [&](VertexId first, VertexId second) {
        const auto a = positions.at(first);
        const auto b = positions.at(second);
        return a.x == b.x && a.z == b.z;
      };
      const bool matches_upper = std::ranges::any_of(upper_guides, [&](EdgeKey upper) {
        return (same_plan_point(edge.a, upper.a) && same_plan_point(edge.b, upper.b)) ||
               (same_plan_point(edge.a, upper.b) && same_plan_point(edge.b, upper.a));
      });
      if (!matches_upper) continue;
    }
    result.push_back(edge);
  }
  return result;
}

void expect_quad_flight_sheets(const StairsMesh &mesh, const StairsSpec &spec) {
  const auto topology = mesh.document.authored_topology().or_throw();
  const auto positions = authored_positions(topology);
  const auto walls = wall_faces(topology, spec);
  std::size_t landing_count = 0U;
  if (spec.layout == StairsLayout::L || spec.layout == StairsLayout::U) landing_count = 1U;
  if (spec.layout == StairsLayout::Switchback) landing_count = spec.flight_count - 1U;
  for (const bool top : {true, false}) {
    std::size_t quads = 0U;
    std::size_t terminal_floors = 0U;
    const auto guides = landing_dividers(topology, top);
    const auto adjacent = authored_edge_faces(topology);
    std::set<FaceId> landing_patches;
    for (const auto guide : guides) {
      for (const auto *face : adjacent.at(guide)) landing_patches.insert(face->id);
    }
    std::set<FaceId> patches;
    for (const auto &face : topology.faces) {
      if (walls.contains(face.id) || vertical_face(face, positions)) continue;
      const double area = face_upward_area(face, positions);
      if (top ? area <= 1.0e-10 : area >= -1.0e-10) continue;
      if (face.vertices.size() == 4U) {
        ++quads;
      } else if (!landing_patches.contains(face.id) && !top &&
                 spec.surface == StairsSurface::Ramp &&
                 spec.construction == StairsConstruction::Solid) {
        // The collapsed free tip needs one floor cap so its top guide ends at
        // a degree-three vertex instead of continuing onto the underside.
        ++terminal_floors;
        EXPECT_EQ(face.vertices.size(), 6U);
        for (const auto vertex : face.vertices) EXPECT_FLOAT_EQ(positions.at(vertex).y, 0.0F);
        double start_x = 0.0;
        double start_z = -spec.footprint_depth * 0.5;
        if (spec.layout == StairsLayout::Arc || spec.layout == StairsLayout::Spiral) {
          start_z = -(spec.inner_radius + spec.flight_width * 0.5);
        } else if (spec.layout != StairsLayout::Straight) {
          const double lanes = spec.layout == StairsLayout::Switchback &&
              spec.switchback_arrangement == StairsSwitchbackArrangement::Spread
                  ? spec.flight_count : 2.0;
          const double width = spec.layout == StairsLayout::L ? spec.footprint_width
              : lanes * spec.flight_width + (lanes - 1.0) * spec.gap;
          start_x = -width * 0.5 + spec.flight_width * 0.5;
          if (!spec.clockwise) start_x = -start_x;
        }
        EXPECT_TRUE(std::ranges::any_of(face.vertices, [&](VertexId vertex) {
          const auto point = positions.at(vertex);
          return point.x == static_cast<float>(start_x) &&
                 point.z == static_cast<float>(start_z);
        }));
      } else {
        EXPECT_GT(face.vertices.size(), 4U) << "A flight sheet must retain authored quads.";
        patches.insert(face.id);
      }
    }
    EXPECT_GT(quads + terminal_floors, 0U);
    EXPECT_EQ(terminal_floors, !top && spec.surface == StairsSurface::Ramp &&
        spec.construction == StairsConstruction::Solid ? 1U : 0U);
    EXPECT_EQ(patches.size(), 2U * landing_count);
    EXPECT_EQ(guides.size(), landing_count);
    std::set<FaceId> divided;
    for (const auto guide : guides) {
      for (const auto *face : adjacent.at(guide)) {
        EXPECT_TRUE(divided.insert(face->id).second)
            << "Each landing must own an independent two-patch guide.";
      }
    }
    EXPECT_EQ(divided, patches);
  }
}

[[nodiscard]] double vertex_distance(Vec3 first, Vec3 second) {
  return std::hypot(static_cast<double>(first.x) - second.x,
                    static_cast<double>(first.y) - second.y,
                    static_cast<double>(first.z) - second.z);
}

[[nodiscard]] Vec2 effective_face_uv(const AuthoredTopologyFace &face, VertexId vertex) {
  const auto corner = std::ranges::find(face.vertices, vertex);
  EXPECT_NE(corner, face.vertices.end());
  EXPECT_EQ(face.uvs.size(), face.vertices.size());
  if (corner == face.vertices.end() || face.uvs.size() != face.vertices.size()) return {};
  const auto uv = face.uvs[static_cast<std::size_t>(corner - face.vertices.begin())];
  const double angle = face.texture_transform.rotation_degrees * std::numbers::pi / 180.0;
  return {static_cast<float>((uv.x * std::cos(angle) - uv.y * std::sin(angle)) /
                                face.texture_transform.scale.x + face.texture_transform.shift_uv.x),
          static_cast<float>((uv.x * std::sin(angle) + uv.y * std::cos(angle)) /
                                face.texture_transform.scale.y + face.texture_transform.shift_uv.y)};
}

[[nodiscard]] std::vector<EdgeKey>
flight_center_guides(const AuthoredTopology &topology, const StairsSpec &spec,
                     bool top = true) {
  const auto positions = authored_positions(topology);
  const bool circular = spec.layout == StairsLayout::Arc || spec.layout == StairsLayout::Spiral;
  const double center_radius = spec.inner_radius + spec.flight_width * 0.5;
  const double sweep = spec.layout == StairsLayout::Arc
      ? spec.sweep_degrees * std::numbers::pi / 180.0
      : spec.turns * 2.0 * std::numbers::pi;
  const auto target_panels = static_cast<std::uint32_t>(std::ceil(
      sweep / (2.0 * std::numbers::pi) * spec.curved_segments));
  const auto panels = spec.surface == StairsSurface::Steps
      ? spec.step_count * std::max(1U, (target_panels + spec.step_count - 1U) / spec.step_count)
      : std::max(1U, target_panels);
  const double minimum_radius = top ? center_radius
      : center_radius * std::cos(sweep / (2.0 * panels));
  const auto on_center = [&](VertexId vertex) {
    const auto point = positions.at(vertex);
    if (!circular) return std::abs(point.x) < 1.0e-6;
    const double radius = std::hypot(point.x, point.z);
    return radius >= minimum_radius - 2.0e-6 && radius <= center_radius + 2.0e-6;
  };
  std::vector<EdgeKey> guides;
  for (const auto &[edge, incident] : authored_edge_faces(topology)) {
    if (incident.size() != 2U || !on_center(edge.a) || !on_center(edge.b)) continue;
    if (std::ranges::all_of(incident, [&](const auto *face) {
          if (face->vertices.size() != 4U) return false;
          const double area = face_upward_area(*face, positions);
          return top ? area >= -1.0e-10 : area < 0.0 && !vertical_face(*face, positions);
        })) guides.push_back(edge);
  }
  return guides;
}

[[nodiscard]] std::set<FaceId> wall_faces(const AuthoredTopology &topology,
                                        const StairsSpec &spec) {
  const auto positions = authored_positions(topology);
  const bool circular = spec.layout == StairsLayout::Arc || spec.layout == StairsLayout::Spiral;
  std::set<FaceId> result;
  for (const auto &face : topology.faces) {
    const auto first = positions.at(face.vertices.front());
    bool side = false;
    if (!circular) {
      side = std::abs(first.x) > spec.footprint_width * 0.49 &&
          std::ranges::all_of(face.vertices, [&](VertexId vertex) {
            return std::abs(positions.at(vertex).x - first.x) < 1.0e-6;
          }) && std::ranges::any_of(face.vertices, [&](VertexId vertex) {
            return std::abs(positions.at(vertex).z - first.z) > 1.0e-5;
          });
    } else {
      Vec3 start = first;
      Vec3 end = first;
      double length = 0.0;
      for (const auto a : face.vertices) {
        for (const auto b : face.vertices) {
          const auto from = positions.at(a);
          const auto to = positions.at(b);
          const double span = std::hypot(static_cast<double>(to.x) - from.x,
                                         static_cast<double>(to.z) - from.z);
          if (span > length) {
            length = span;
            start = from;
            end = to;
          }
        }
      }
      const double radius = spec.inner_radius + spec.flight_width;
      const double tolerance = radius * std::numeric_limits<float>::epsilon() * 8.0;
      const double radial_cross = static_cast<double>(start.x) * end.z -
                                  static_cast<double>(start.z) * end.x;
      side = length > 0.0 && std::abs(radial_cross) > radius * radius * 1.0e-5 &&
          std::ranges::all_of(face.vertices, [&](VertexId vertex) {
            const auto point = positions.at(vertex);
            const double area = (static_cast<double>(end.x) - start.x) * (point.z - start.z) -
                                (static_cast<double>(end.z) - start.z) * (point.x - start.x);
            return std::abs(area) <= tolerance * length;
          });
    }
    if (side) result.insert(face.id);
  }
  return result;
}

void expect_minimal_wall_boundary(const StairsMesh &mesh, const StairsSpec &spec) {
  const auto topology = mesh.document.authored_topology().or_throw();
  const auto positions = authored_positions(topology);
  const auto walls = wall_faces(topology, spec);
  ASSERT_FALSE(walls.empty());
  std::set<VertexId> sheet_vertices;
  std::set<FaceId> walking;
  for (const auto &face : topology.faces) {
    if (walls.contains(face.id)) continue;
    sheet_vertices.insert(face.vertices.begin(), face.vertices.end());
    if (!vertical_face(face, positions) && face_upward_area(face, positions) > 0.0) {
      EXPECT_EQ(face.vertices.size(), 4U);
      walking.insert(face.id);
    }
  }
  ASSERT_EQ(sheet_vertices.size(), topology.vertices.size())
      << "Wall charts must not introduce any interior vertices or editable frames.";
  for (const auto &vertex : topology.vertices) EXPECT_TRUE(sheet_vertices.contains(vertex.id));
  std::vector<EdgeKey> chords;
  for (const auto &[edge, incident] : authored_edge_faces(topology)) {
    if (incident.size() != 2U) continue;
    if ((walking.contains(incident[0]->id) && walls.contains(incident[1]->id)) ||
        (walking.contains(incident[1]->id) && walls.contains(incident[0]->id))) {
      chords.push_back(edge);
    }
  }
  ASSERT_EQ(chords.size(), walking.size());
  std::map<EdgeKey, std::vector<const AuthoredTopologyFace *>> panels;
  for (const auto &face : topology.faces) {
    if (!walls.contains(face.id)) continue;
    EXPECT_LE(face.vertices.size(), 6U);
    std::vector<EdgeKey> matches;
    for (const auto chord : chords) {
      const auto first = positions.at(chord.a);
      const auto last = positions.at(chord.b);
      const double dx = static_cast<double>(last.x) - first.x;
      const double dz = static_cast<double>(last.z) - first.z;
      const double length = std::hypot(dx, dz);
      ASSERT_GT(length, 0.0);
      const double scale = std::max({1.0, std::abs(static_cast<double>(first.x)),
          std::abs(static_cast<double>(first.z)), std::abs(static_cast<double>(last.x)),
          std::abs(static_cast<double>(last.z))});
      const double tolerance = scale * std::numeric_limits<float>::epsilon() * 8.0;
      const bool same_panel = std::ranges::all_of(face.vertices, [&](VertexId vertex) {
        const auto point = positions.at(vertex);
        const double x = static_cast<double>(point.x) - first.x;
        const double z = static_cast<double>(point.z) - first.z;
        const double along = (x * dx + z * dz) / length;
        return std::abs(x * dz - z * dx) <= tolerance * length &&
            along >= -tolerance && along <= length + tolerance;
      });
      if (same_panel) matches.push_back(chord);
    }
    ASSERT_EQ(matches.size(), 1U);
    panels[matches.front()].push_back(&face);
    if (face.vertices.size() == 4U) {
      for (std::size_t corner = 0U; corner < 4U; ++corner) {
        const auto point = positions.at(face.vertices[corner]);
        const auto previous = positions.at(face.vertices[(corner + 3U) % 4U]);
        const auto next = positions.at(face.vertices[(corner + 1U) % 4U]);
        const double first_length = vertex_distance(point, previous);
        const double second_length = vertex_distance(point, next);
        ASSERT_GT(first_length, 0.0);
        ASSERT_GT(second_length, 0.0);
        const std::array<double, 3> a = {static_cast<double>(previous.x) - point.x,
            static_cast<double>(previous.y) - point.y, static_cast<double>(previous.z) - point.z};
        const std::array<double, 3> b = {static_cast<double>(next.x) - point.x,
            static_cast<double>(next.y) - point.y, static_cast<double>(next.z) - point.z};
        const double area = std::hypot(a[1] * b[2] - a[2] * b[1],
                                      a[2] * b[0] - a[0] * b[2],
                                      a[0] * b[1] - a[1] * b[0]);
        EXPECT_GT(area / (first_length * second_length), 1.0e-6)
            << "Four authored IDs must not disguise a triangular side patch.";
      }
    }
  }
  EXPECT_EQ(panels.size(), chords.size());
  for (const auto &[chord, faces] : panels) {
    static_cast<void>(chord);
    if (faces.size() == 1U) continue;
    std::set<VertexId> vertices;
    std::map<EdgeKey, std::pair<std::size_t, int>> edges;
    for (const auto *face : faces) {
      EXPECT_EQ(face->vertices.size(), 3U)
          << "Only a validated existing-boundary triangle fallback may split one wall panel.";
      vertices.insert(face->vertices.begin(), face->vertices.end());
      for (std::size_t corner = 0U; corner < face->vertices.size(); ++corner) {
        const auto first = face->vertices[corner];
        const auto last = face->vertices[(corner + 1U) % face->vertices.size()];
        auto &edge = edges[make_edge_key(first, last)];
        ++edge.first;
        edge.second += first < last ? 1 : -1;
      }
    }
    EXPECT_LE(vertices.size(), 6U);
    EXPECT_EQ(faces.size(), vertices.size() - 2U);
    std::size_t boundary_edges = 0U;
    for (const auto &[edge, uses] : edges) {
      static_cast<void>(edge);
      EXPECT_LE(uses.first, 2U);
      if (uses.first == 1U) ++boundary_edges;
      else EXPECT_EQ(uses.second, 0);
    }
    EXPECT_EQ(boundary_edges, vertices.size());
  }
}

void expect_unchanged_uvs(const AuthoredTopology &source,
                         const AuthoredTopology &replacement,
                         const std::set<FaceId> &changed) {
  std::map<FaceId, const AuthoredTopologyFace *> original;
  for (const auto &face : source.faces) original.emplace(face.id, &face);
  ASSERT_EQ(replacement.faces.size(), source.faces.size());
  for (const auto &face : replacement.faces) {
    if (changed.contains(face.id)) continue;
    const auto &before = *original.at(face.id);
    EXPECT_EQ(face.texture_transform, before.texture_transform);
    ASSERT_EQ(face.uvs.size(), before.uvs.size());
    for (std::size_t corner = 0U; corner < face.uvs.size(); ++corner) {
      EXPECT_FLOAT_EQ(face.uvs[corner].x, before.uvs[corner].x);
      EXPECT_FLOAT_EQ(face.uvs[corner].y, before.uvs[corner].y);
    }
  }
}

void expect_metric_wall_triangles(const PolygonDocument &document,
                                  const std::set<FaceId> &walls,
                                  bool positive_height) {
  const auto topology = document.authored_topology().or_throw();
  const auto compiled = document.compile_mesh().or_throw();
  std::map<VertexId, std::set<FaceId>> incident;
  for (const auto &face : topology.faces) {
    if (!walls.contains(face.id)) continue;
    for (const auto vertex : face.vertices) incident[vertex].insert(face.id);
  }
  std::map<FaceId, std::size_t> triangles;
  std::map<FaceId, double> uv_winding;
  for (std::size_t index = 0U; index + 2U < compiled.indices.size(); index += 3U) {
    const auto &a = compiled.vertices[compiled.indices[index]];
    const auto &b = compiled.vertices[compiled.indices[index + 1U]];
    const auto &c = compiled.vertices[compiled.indices[index + 2U]];
    if (!incident.contains(a.source_vertex) || !incident.contains(b.source_vertex) ||
        !incident.contains(c.source_vertex)) continue;
    for (const auto face : incident.at(a.source_vertex)) {
      if (!incident.at(b.source_vertex).contains(face) ||
          !incident.at(c.source_vertex).contains(face)) continue;
      ++triangles[face];
      const std::array<double, 3> first = {
          static_cast<double>(b.position.x) - a.position.x,
          static_cast<double>(b.position.y) - a.position.y,
          static_cast<double>(b.position.z) - a.position.z};
      const std::array<double, 3> second = {
          static_cast<double>(c.position.x) - a.position.x,
          static_cast<double>(c.position.y) - a.position.y,
          static_cast<double>(c.position.z) - a.position.z};
      const std::array<double, 3> area = {
          first[1] * second[2] - first[2] * second[1],
          first[2] * second[0] - first[0] * second[2],
          first[0] * second[1] - first[1] * second[0]};
      const double geometric_area = std::hypot(area[0], area[1], area[2]);
      ASSERT_GT(geometric_area, 1.0e-10);
      EXPECT_GT(area[0] * a.normal.x + area[1] * a.normal.y + area[2] * a.normal.z, 0.0);
      const double uv_area = (static_cast<double>(b.uv0.x) - a.uv0.x) *
                                (static_cast<double>(c.uv0.y) - a.uv0.y) -
                            (static_cast<double>(b.uv0.y) - a.uv0.y) *
                                (static_cast<double>(c.uv0.x) - a.uv0.x);
      ASSERT_NE(uv_area, 0.0);
      if (uv_winding.contains(face)) EXPECT_GT(uv_area * uv_winding.at(face), 0.0);
      else uv_winding.emplace(face, uv_area);
      EXPECT_NEAR(std::abs(uv_area), geometric_area * 0.25,
                  geometric_area * 2.0e-4 + 1.0e-9);
      const std::array corners = {&a, &b, &c};
      for (std::size_t corner = 0U; corner < corners.size(); ++corner) {
        const auto &from = *corners[corner];
        const auto &to = *corners[(corner + 1U) % corners.size()];
        const double horizontal = std::hypot(static_cast<double>(to.position.x) - from.position.x,
                                             static_cast<double>(to.position.z) - from.position.z);
        EXPECT_NEAR(std::abs(to.uv0.x - from.uv0.x), horizontal * 0.5, 2.0e-5);
        const double rise = static_cast<double>(to.position.y) - from.position.y;
        const double texture_rise = static_cast<double>(to.uv0.y) - from.uv0.y;
        if (positive_height) EXPECT_NEAR(texture_rise, rise * 0.5, 2.0e-5);
        else EXPECT_NEAR(std::abs(texture_rise), std::abs(rise) * 0.5, 2.0e-5);
      }
    }
  }
  for (const auto &face : topology.faces) {
    if (walls.contains(face.id)) EXPECT_EQ(triangles[face.id], face.vertices.size() - 2U);
  }
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
          expect_quad_flight_sheets(result.value(), spec);
          const auto [minimum, maximum] = vertical_bounds(result.value());
          EXPECT_NEAR(minimum, construction == StairsConstruction::Solid
                                   ? 0.0 : -spec.slab_thickness, 1.0e-6);
          EXPECT_NEAR(maximum, spec.height, 1.0e-6);
        }
      }
    }
  }
}

TEST(ModelingStairsTests, FlightCenterGuidesPeelAcrossAuthoredQuadLanes) {
  for (const auto layout : {StairsLayout::Straight, StairsLayout::Arc, StairsLayout::Spiral}) {
    for (const auto surface : {StairsSurface::Steps, StairsSurface::Ramp}) {
      for (const auto construction : {StairsConstruction::Solid, StairsConstruction::Slab}) {
        for (const bool clockwise : {false, true}) {
          SCOPED_TRACE(::testing::Message() << static_cast<int>(layout) << ", "
              << static_cast<int>(surface) << ", " << static_cast<int>(construction)
              << ", " << clockwise);
          StairsSpec spec;
          spec.layout = layout;
          spec.surface = surface;
          spec.construction = construction;
          spec.clockwise = clockwise;
          spec.automatic_step_count = false;
          spec.step_count = 7U;
          spec.curved_segments = 29U;
          spec.height = 4.0;
          spec.turns = construction == StairsConstruction::Solid ? 0.75 : 1.5;
          const auto generated = build_stairs_mesh(spec);
          ASSERT_TRUE(generated.ok()) << generated.error_message();
          const auto &document = generated.value().document;
          const auto topology = document.authored_topology().or_throw();
          const auto positions = authored_positions(topology);
          const auto guides = flight_center_guides(topology, spec);
          ASSERT_FALSE(guides.empty());
          std::map<VertexId, std::vector<VertexId>> neighbors;
          for (const auto edge : guides) {
            neighbors[edge.a].push_back(edge.b);
            neighbors[edge.b].push_back(edge.a);
          }
          std::size_t endpoints = 0U;
          for (const auto &[vertex, adjacent] : neighbors) {
            static_cast<void>(vertex);
            EXPECT_LE(adjacent.size(), 2U);
            endpoints += adjacent.size() == 1U ? 1U : 0U;
          }
          EXPECT_EQ(endpoints, 2U);
          std::set<VertexId> reached;
          std::vector<VertexId> pending{guides.front().a};
          while (!pending.empty()) {
            const auto vertex = pending.back();
            pending.pop_back();
            if (reached.insert(vertex).second) {
              const auto &adjacent = neighbors.at(vertex);
              pending.insert(pending.end(), adjacent.begin(), adjacent.end());
            }
          }
          EXPECT_EQ(reached.size(), neighbors.size());
          const auto identity = document.content_identity();
          const std::array targets{UvPeelTarget{.key = 1U, .document = &document,
              .edges = guides, .active_edge = guides.front()}};
          const auto prepared = prepare_uv_peel(targets, {});
          ASSERT_TRUE(prepared.ok()) << prepared.error_message();
          ASSERT_TRUE(prepared.value().changed);
          ASSERT_EQ(prepared.value().documents.size(), 1U);
          const auto &replacement = prepared.value().documents.front();
          EXPECT_TRUE(replacement.document.has_same_geometry(document));
          EXPECT_EQ(document.content_identity(), identity);
          const auto peeled = replacement.document.authored_topology().or_throw();
          const std::set<FaceId> affected(replacement.receipt.affected.faces.begin(),
                                         replacement.receipt.affected.faces.end());
          expect_unchanged_uvs(topology, peeled, affected);
          std::map<FaceId, const AuthoredTopologyFace *> peeled_faces;
          for (const auto &face : peeled.faces) {
            peeled_faces.emplace(face.id, &face);
            if (!affected.contains(face.id)) continue;
            ASSERT_EQ(face.uvs.size(), face.vertices.size());
            for (const auto uv : face.uvs) {
              EXPECT_TRUE(std::isfinite(uv.x));
              EXPECT_TRUE(std::isfinite(uv.y));
            }
          }
          const auto adjacent = authored_edge_faces(topology);
          for (const auto guide : guides) {
            for (const auto *face : adjacent.at(guide)) {
              EXPECT_NE(std::ranges::find(replacement.receipt.affected.faces, face->id),
                        replacement.receipt.affected.faces.end());
              const auto &mapped = *peeled_faces.at(face->id);
              ASSERT_EQ(mapped.uvs.size(), mapped.vertices.size());
              const auto first = effective_face_uv(mapped, guide.a);
              const auto second = effective_face_uv(mapped, guide.b);
              EXPECT_NEAR(std::abs(second.x - first.x),
                          0.5 * vertex_distance(positions.at(guide.a), positions.at(guide.b)),
                          2.0e-4);
              EXPECT_NEAR(second.y, first.y, 2.0e-4);
            }
          }
        }
      }
    }
  }
}

TEST(ModelingStairsTests, UndersideGuidesRetainDefaultPeelFlowAndMetricGuideDensity) {
  for (const auto layout : {StairsLayout::Straight, StairsLayout::Arc, StairsLayout::Spiral}) {
    for (const auto surface : {StairsSurface::Steps, StairsSurface::Ramp}) {
      for (const auto construction : {StairsConstruction::Solid, StairsConstruction::Slab}) {
        for (const bool clockwise : {false, true}) {
          SCOPED_TRACE(::testing::Message() << static_cast<int>(layout) << ", "
              << static_cast<int>(surface) << ", " << static_cast<int>(construction)
              << ", " << clockwise);
          StairsSpec spec;
          spec.layout = layout;
          spec.surface = surface;
          spec.construction = construction;
          spec.clockwise = clockwise;
          spec.automatic_step_count = false;
          spec.step_count = 7U;
          spec.curved_segments = 29U;
          spec.height = 4.0;
          spec.turns = construction == StairsConstruction::Solid ? 0.75 : 1.5;
          const auto generated = build_stairs_mesh(spec);
          ASSERT_TRUE(generated.ok()) << generated.error_message();
          const auto &document = generated.value().document;
          const auto topology = document.authored_topology().or_throw();
          const auto positions = authored_positions(topology);
          const auto walls = wall_faces(topology, spec);
          const auto guides = flight_center_guides(topology, spec, false);
          if (layout == StairsLayout::Straight && surface == StairsSurface::Ramp &&
              construction == StairsConstruction::Solid) {
            EXPECT_TRUE(guides.empty());
            std::size_t floors = 0U;
            for (const auto &face : topology.faces) {
              if (face_upward_area(face, positions) >= 0.0 || walls.contains(face.id)) continue;
              ++floors;
              EXPECT_EQ(face.vertices.size(), 6U);
              for (const auto vertex : face.vertices) EXPECT_FLOAT_EQ(positions.at(vertex).y, 0.0F);
            }
            EXPECT_EQ(floors, 1U);
            continue;
          }
          ASSERT_FALSE(guides.empty());
          const auto identity = document.content_identity();
          const std::array targets{UvPeelTarget{.key = 1U, .document = &document,
              .edges = guides, .active_edge = guides.front()}};
          const auto prepared = prepare_uv_peel(targets, {.u_offset = 0.43});
          ASSERT_TRUE(prepared.ok()) << prepared.error_message();
          ASSERT_TRUE(prepared.value().changed);
          ASSERT_EQ(prepared.value().documents.size(), 1U);
          const auto &replacement = prepared.value().documents.front();
          EXPECT_TRUE(replacement.document.has_same_geometry(document));
          EXPECT_EQ(document.content_identity(), identity);
          const auto peeled = replacement.document.authored_topology().or_throw();
          const std::set<FaceId> affected(replacement.receipt.affected.faces.begin(),
                                         replacement.receipt.affected.faces.end());
          ASSERT_FALSE(affected.empty());
          expect_unchanged_uvs(topology, peeled, affected);
          std::map<FaceId, const AuthoredTopologyFace *> mapped;
          for (const auto &face : peeled.faces) {
            mapped.emplace(face.id, &face);
            if (!affected.contains(face.id)) continue;
            ASSERT_EQ(face.uvs.size(), face.vertices.size());
            for (const auto uv : face.uvs) {
              EXPECT_TRUE(std::isfinite(uv.x));
              EXPECT_TRUE(std::isfinite(uv.y));
            }
          }
          const auto adjacent = authored_edge_faces(topology);
          for (const auto guide : guides) {
            for (const auto *face : adjacent.at(guide)) {
              const auto first = effective_face_uv(*mapped.at(face->id), guide.a);
              const auto second = effective_face_uv(*mapped.at(face->id), guide.b);
              EXPECT_NEAR(std::abs(second.x - first.x),
                          vertex_distance(positions.at(guide.a), positions.at(guide.b)) * 0.5,
                          2.0e-4);
              EXPECT_NEAR(second.y, first.y, 2.0e-4);
            }
          }
        }
      }
    }
  }
}

TEST(ModelingStairsTests, MinimalWallsUseOnlySheetBoundaryVerticesAndAffineMetricUvs) {
  for (const auto layout : {StairsLayout::Straight, StairsLayout::Arc, StairsLayout::Spiral}) {
    for (const auto surface : {StairsSurface::Steps, StairsSurface::Ramp}) {
      for (const auto construction : {StairsConstruction::Solid, StairsConstruction::Slab}) {
        for (const bool clockwise : {false, true}) {
          SCOPED_TRACE(::testing::Message() << static_cast<int>(layout) << ", "
              << static_cast<int>(surface) << ", " << static_cast<int>(construction)
              << ", " << clockwise);
          StairsSpec spec;
          spec.layout = layout;
          spec.surface = surface;
          spec.construction = construction;
          spec.clockwise = clockwise;
          spec.automatic_step_count = false;
          spec.step_count = 7U;
          spec.curved_segments = 29U;
          spec.height = 4.0;
          spec.turns = construction == StairsConstruction::Solid ? 0.75 : 1.5;
          const auto generated = build_stairs_mesh(spec);
          ASSERT_TRUE(generated.ok()) << generated.error_message();
          expect_minimal_wall_boundary(generated.value(), spec);
          const auto &document = generated.value().document;
          const auto topology = document.authored_topology().or_throw();
          const auto walls = wall_faces(topology, spec);
          expect_metric_wall_triangles(document, walls, true);
          for (const auto &[edge, incident] : authored_edge_faces(topology)) {
            if (incident.size() != 2U || !walls.contains(incident[0]->id) ||
                !walls.contains(incident[1]->id)) continue;
            for (const auto vertex : {edge.a, edge.b}) {
              const auto first = effective_face_uv(*incident[0], vertex);
              const auto second = effective_face_uv(*incident[1], vertex);
              EXPECT_NEAR(first.x, second.x, 2.0e-5);
              EXPECT_NEAR(first.y, second.y, 2.0e-5);
            }
          }
        }
      }
    }
  }
}

TEST(ModelingStairsTests, FloatThinWallsRetainBoundaryAndAffineMetricUvs) {
  StairsSpec spec;
  spec.surface = StairsSurface::Ramp;
  spec.construction = StairsConstruction::Slab;
  spec.height = 1000.0;
  spec.slab_thickness = kStairsMinimumDimension;
  const auto generated = build_stairs_mesh(spec);
  ASSERT_TRUE(generated.ok()) << generated.error_message();
  expect_closed_mesh(generated.value());
  expect_minimal_wall_boundary(generated.value(), spec);
  const auto topology = generated.value().document.authored_topology().or_throw();
  const auto walls = wall_faces(topology, spec);
  expect_metric_wall_triangles(generated.value().document, walls, true);
}

TEST(ModelingStairsTests, DenseMultiTurnStairsRetainWallChartsWithinTheMeshBudget) {
  StairsSpec spec;
  spec.layout = StairsLayout::Spiral;
  spec.construction = StairsConstruction::Slab;
  spec.turns = 8.0;
  spec.height = 4.0;
  spec.slab_thickness = 0.1;
  spec.automatic_step_count = false;
  spec.step_count = 1023U;
  spec.curved_segments = 256U;
  const auto generated = build_stairs_mesh(spec);
  ASSERT_TRUE(generated.ok()) << generated.error_message();
  EXPECT_EQ(generated.value().effective_step_count, spec.step_count);
  ASSERT_EQ(generated.value().flight_step_counts.size(), 1U);
  EXPECT_EQ(generated.value().flight_step_counts.front(), spec.step_count);
  const auto [minimum, maximum] = vertical_bounds(generated.value());
  EXPECT_NEAR(minimum, -spec.slab_thickness, 1.0e-6);
  EXPECT_NEAR(maximum, spec.height, 1.0e-6);
  const auto topology = generated.value().document.authored_topology().or_throw();
  const auto walls = wall_faces(topology, spec);
  EXPECT_FALSE(walls.empty());
  RecordProperty("generated_vertices", static_cast<int>(topology.vertices.size()));
  RecordProperty("generated_faces", static_cast<int>(topology.faces.size()));
  EXPECT_LT(topology.vertices.size(), 30'000U);
  EXPECT_LT(topology.faces.size(), 30'000U);
  std::set<VertexId> sheet_vertices;
  for (const auto &face : topology.faces) {
    if (!walls.contains(face.id)) sheet_vertices.insert(face.vertices.begin(), face.vertices.end());
  }
  EXPECT_EQ(sheet_vertices.size(), topology.vertices.size());
  expect_closed_mesh(generated.value());
}

TEST(ModelingStairsTests, FlightQuadStripSupportsTheSdkEdgeLoopOperation) {
  for (const auto construction : {StairsConstruction::Solid, StairsConstruction::Slab}) {
    StairsSpec spec;
    spec.construction = construction;
    spec.automatic_step_count = false;
    spec.step_count = 7U;
    auto generated = build_stairs_mesh(spec);
    ASSERT_TRUE(generated.ok()) << generated.error_message();
    auto mesh = generated.value();
    const auto topology = mesh.document.authored_topology().or_throw();
    const auto positions = authored_positions(topology);
    std::optional<EdgeKey> seed;
    for (const auto &[edge, incident] : authored_edge_faces(topology)) {
      const auto first = positions.at(edge.a);
      const auto second = positions.at(edge.b);
      if (incident.size() == 2U && first.y > 0.0F &&
          std::abs(first.y - second.y) < 1.0e-6 &&
          std::abs(first.z - second.z) < 1.0e-6 &&
          std::abs(first.x - second.x) > 0.1 &&
          std::ranges::all_of(incident, [](const auto *face) {
            return face->vertices.size() == 4U;
          })) {
        seed = edge;
        break;
      }
    }
    ASSERT_TRUE(seed.has_value());
    ASSERT_TRUE(mesh.document.select_edge(*seed).ok());
    const auto inserted = mesh.document.insert_edge_loop();
    ASSERT_TRUE(inserted.ok()) << inserted.error_message();
    ASSERT_TRUE(inserted.value().success) << inserted.value().message;
    ASSERT_TRUE(inserted.value().changed);
    const auto split = mesh.document.authored_topology().or_throw();
    const auto split_positions = authored_positions(split);
    std::vector<VertexId> created_vertices;
    for (const auto &vertex : split.vertices) {
      if (!positions.contains(vertex.id)) created_vertices.push_back(vertex.id);
    }
    std::set<FaceId> original_faces;
    for (const auto &face : topology.faces) original_faces.insert(face.id);
    std::size_t created_faces = 0U;
    for (const auto &face : split.faces) {
      created_faces += original_faces.contains(face.id) ? 0U : 1U;
    }
    EXPECT_GE(created_vertices.size(), spec.step_count + 1U);
    EXPECT_GE(created_faces, 2U * spec.step_count - 1U);
    double minimum_z = std::numeric_limits<double>::max();
    double maximum_z = std::numeric_limits<double>::lowest();
    for (const auto vertex : created_vertices) {
      const auto point = split_positions.at(vertex);
      EXPECT_NEAR(std::abs(point.x), spec.footprint_width * 0.25, 1.0e-6);
      minimum_z = std::min(minimum_z, static_cast<double>(point.z));
      maximum_z = std::max(maximum_z, static_cast<double>(point.z));
    }
    EXPECT_NEAR(minimum_z, -spec.footprint_depth * 0.5, 1.0e-6);
    EXPECT_NEAR(maximum_z, spec.footprint_depth * 0.5, 1.0e-6);
    expect_closed_mesh(mesh);
  }
}

TEST(ModelingStairsTests, LandingDividerPeelIsConfinedToItsTwoAuthoredPatches) {
  for (const auto layout : {StairsLayout::L, StairsLayout::U, StairsLayout::Switchback}) {
    for (const auto surface : {StairsSurface::Steps, StairsSurface::Ramp}) {
      for (const auto construction : {StairsConstruction::Solid, StairsConstruction::Slab}) {
        for (const bool clockwise : {false, true}) {
          SCOPED_TRACE(::testing::Message() << static_cast<int>(layout) << ", "
              << static_cast<int>(surface) << ", " << static_cast<int>(construction)
              << ", " << clockwise);
          StairsSpec spec;
          spec.layout = layout;
          spec.surface = surface;
          spec.construction = construction;
          spec.clockwise = clockwise;
          spec.footprint_width = 4.3;
          spec.footprint_depth = 7.1;
          spec.flight_width = 1.3;
          spec.landing_depth = 1.7;
          spec.gap = 0.37;
          spec.height = 6.0;
          spec.automatic_step_count = false;
          spec.step_count = 12U;
          const auto generated = build_stairs_mesh(spec);
          ASSERT_TRUE(generated.ok()) << generated.error_message();
          expect_closed_mesh(generated.value());
          expect_quad_flight_sheets(generated.value(), spec);
          const auto &document = generated.value().document;
          const auto topology = document.authored_topology().or_throw();
          const auto adjacent = authored_edge_faces(topology);
          for (const bool top : {true, false}) {
            const auto dividers = landing_dividers(topology, top);
            ASSERT_EQ(dividers.size(), layout == StairsLayout::Switchback ? 2U : 1U);
            for (const auto divider : dividers) {
              const std::array targets{UvPeelTarget{.key = 1U, .document = &document,
                  .edges = {divider}, .active_edge = divider}};
              const auto prepared = prepare_uv_peel(targets, {});
              ASSERT_TRUE(prepared.ok()) << prepared.error_message();
              ASSERT_TRUE(prepared.value().changed);
              ASSERT_EQ(prepared.value().documents.size(), 1U);
              const auto &replacement = prepared.value().documents.front();
              EXPECT_TRUE(replacement.document.has_same_geometry(document));
              std::set<FaceId> expected;
              for (const auto *face : adjacent.at(divider)) expected.insert(face->id);
              const std::set<FaceId> affected(replacement.receipt.affected.faces.begin(),
                                            replacement.receipt.affected.faces.end());
              EXPECT_EQ(affected, expected);
              const auto mapped = replacement.document.authored_topology().or_throw();
              for (const auto &face : mapped.faces) {
                if (!expected.contains(face.id)) continue;
                ASSERT_EQ(face.uvs.size(), face.vertices.size());
                for (const auto uv : face.uvs) {
                  EXPECT_TRUE(std::isfinite(uv.x));
                  EXPECT_TRUE(std::isfinite(uv.y));
                }
              }
            }
          }
        }
      }
    }
  }
}

TEST(ModelingStairsTests, OneStepFlightsAndTightLandingsRetainClosedQuadSheets) {
  for (const auto layout : {StairsLayout::Straight, StairsLayout::L, StairsLayout::U,
                            StairsLayout::Switchback, StairsLayout::Arc, StairsLayout::Spiral}) {
    for (const auto surface : {StairsSurface::Steps, StairsSurface::Ramp}) {
      for (const auto construction : {StairsConstruction::Solid, StairsConstruction::Slab}) {
        for (const bool clockwise : {false, true}) {
          SCOPED_TRACE(::testing::Message() << static_cast<int>(layout) << ", "
              << static_cast<int>(surface) << ", " << static_cast<int>(construction)
              << ", " << clockwise);
          StairsSpec spec;
          spec.layout = layout;
          spec.surface = surface;
          spec.construction = construction;
          spec.clockwise = clockwise;
          spec.height = 6.0;
          spec.footprint_width = 1.01;
          spec.footprint_depth = layout == StairsLayout::Switchback ? 2.01 : 1.01;
          spec.turns = 0.5;
          spec.curved_segments = 5U;
          spec.automatic_step_count = false;
          spec.step_count = layout == StairsLayout::Switchback ? 3U
              : layout == StairsLayout::L || layout == StairsLayout::U ? 2U : 1U;
          const auto generated = build_stairs_mesh(spec);
          ASSERT_TRUE(generated.ok()) << generated.error_message();
          expect_closed_mesh(generated.value());
          expect_quad_flight_sheets(generated.value(), spec);
          if (surface == StairsSurface::Steps) {
            for (const auto count : generated.value().flight_step_counts) EXPECT_EQ(count, 1U);
          }
        }
      }
    }
  }
}

TEST(ModelingStairsTests, CurvedStepsUseEqualQuadPanelCountsForEveryTread) {
  for (const auto layout : {StairsLayout::Arc, StairsLayout::Spiral}) {
    for (const auto construction : {StairsConstruction::Solid, StairsConstruction::Slab}) {
      for (const bool clockwise : {false, true}) {
        StairsSpec spec;
        spec.layout = layout;
        spec.construction = construction;
        spec.clockwise = clockwise;
        spec.height = 4.0;
        spec.turns = construction == StairsConstruction::Solid ? 0.75 : 1.5;
        spec.automatic_step_count = false;
        spec.step_count = 7U;
        spec.curved_segments = 29U;
        const auto generated = build_stairs_mesh(spec);
        ASSERT_TRUE(generated.ok()) << generated.error_message();
        const auto topology = generated.value().document.authored_topology().or_throw();
        const auto positions = authored_positions(topology);
        const auto walls = wall_faces(topology, spec);
        const auto panels = static_cast<std::uint32_t>(std::ceil(spec.curved_segments *
            (layout == StairsLayout::Arc ? spec.sweep_degrees / 360.0 : spec.turns)));
        const auto per_tread = (panels + spec.step_count - 1U) / spec.step_count;
        std::map<std::uint32_t, std::size_t> tread_quads;
        for (const auto &face : topology.faces) {
          if (walls.contains(face.id)) continue;
          const double area_y = face_upward_area(face, positions);
          const auto origin = positions.at(face.vertices.front());
          double area_x = 0.0;
          double area_z = 0.0;
          for (std::size_t corner = 1U; corner + 1U < face.vertices.size(); ++corner) {
            const auto a = positions.at(face.vertices[corner]);
            const auto b = positions.at(face.vertices[corner + 1U]);
            area_x += (static_cast<double>(a.y) - origin.y) * (b.z - origin.z) -
                      (static_cast<double>(a.z) - origin.z) * (b.y - origin.y);
            area_z += (static_cast<double>(a.x) - origin.x) * (b.y - origin.y) -
                      (static_cast<double>(a.y) - origin.y) * (b.x - origin.x);
          }
          if (area_y <= 0.0 ||
              area_y < std::hypot(area_x, area_y, area_z) * 0.999999) continue;
          ASSERT_EQ(face.vertices.size(), 4U);
          const double height = positions.at(face.vertices.front()).y;
          const auto step = static_cast<std::uint32_t>(std::lround(
              height / generated.value().riser_height));
          EXPECT_GE(step, 1U);
          EXPECT_LE(step, spec.step_count);
          for (const auto vertex : face.vertices) {
            EXPECT_NEAR(positions.at(vertex).y, height, 1.0e-6);
          }
          ++tread_quads[step];
        }
        ASSERT_EQ(tread_quads.size(), spec.step_count);
        for (const auto &[step, count] : tread_quads) {
          SCOPED_TRACE(step);
          EXPECT_EQ(count, 2U * per_tread);
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
  const auto positions = authored_positions(topology);
  const auto walking = walking_vertices(topology);
  ASSERT_FALSE(walking.empty());
  std::set<float> elevations;
  for (const auto vertex : walking) {
    const double level = positions.at(vertex).y / result.value().riser_height;
    EXPECT_NEAR(level, std::round(level), 2.0e-6);
    elevations.insert(positions.at(vertex).y);
  }
  EXPECT_EQ(elevations.size(), result.value().effective_step_count);
  for (const auto &face : topology.faces) {
    if (face.vertices.size() != 4U) continue;
    const auto first = positions.at(face.vertices.front());
    const bool riser = std::ranges::all_of(face.vertices, [&](VertexId vertex) {
      return positions.at(vertex).z == first.z;
    }) && std::ranges::any_of(face.vertices, [&](VertexId vertex) {
      return positions.at(vertex).x != first.x;
    });
    if (!riser) continue;
    for (const auto vertex : face.vertices) {
      const double level = positions.at(vertex).y / result.value().riser_height;
      EXPECT_NEAR(level, std::round(level), 2.0e-6);
    }
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
    const auto positions = authored_positions(topology);
    const auto walking = walking_vertices(topology);
    ASSERT_FALSE(walking.empty());
    std::set<float> elevations{0.0F};
    for (const auto vertex : walking) elevations.insert(positions.at(vertex).y);
    float largest_x = 0.0F;
    float largest_z = 0.0F;
    for (const auto &vertex : topology.vertices) {
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
    const double center_radius = spec.inner_radius + spec.flight_width * 0.5;
    const double x = (left_radius * std::cos(start_angle) +
                      left_radius * std::cos(end_angle) +
                      center_radius * std::cos(end_angle)) / 3.0;
    const double z = (left_radius * std::sin(start_angle) +
                      left_radius * std::sin(end_angle) +
                      center_radius * std::sin(end_angle)) / 3.0;
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
  // emitted two-lane float triangles overlap by approximately 1.05e-8 between vertices.
  spec.slab_thickness = 1.2451044433825746;
  EXPECT_FALSE(build_stairs_mesh(spec).ok());
  spec.slab_thickness = 1.2450944443825747;
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
