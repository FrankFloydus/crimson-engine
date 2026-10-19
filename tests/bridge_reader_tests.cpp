////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/default_material_catalog.hpp"
#include "assets/material_document.hpp"
#include "editor/material/standard_material_edit_session_service.hpp"
#include "editor/session/project_session.hpp"
#include "editor/projection/mesh_projection_service.hpp"
#include "io/bridge/quader_bridge_reader.hpp"
#include "io/project/project_manifest.hpp"
#include "io/project/qmat_codec.hpp"
#include "io/qdr/qdr_document_writer.hpp"
#include "io/qdr/qdr_binary_container.hpp"
#include "io/qdr/qdr_project_write_snapshot.hpp"
#include "scene/document/scene_content_document.hpp"
#include "scene/light_properties.hpp"
#include "scene/scene_transform.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <quader/modeling/neutral/polygon/document.hpp>
#include <quader/modeling/neutral/geometry/quad_uv_interpolation.hpp>
#include <quader/modeling/modeling.hpp>
#include <QImage>
#include <QColor>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

using namespace quader;

namespace {

constexpr char kFirstScene[] = "11111111-1111-4111-8111-111111111111";
constexpr char kSecondScene[] = "22222222-2222-4222-8222-222222222222";
constexpr char kLightingScene[] = "12121212-1212-4212-8212-121212121212";
constexpr char kCalibrationScene[] = "13131313-1313-4313-8313-131313131313";
constexpr std::array<LightType, 3> kLightShapes{
    LightType::Directional, LightType::Point, LightType::Spot};
constexpr std::array<std::string_view, 3> kLightShapeNames{
    "Directional", "Point", "Spot"};
constexpr std::array<std::string_view, 3> kLightShapeSlugs{
    "directional", "point", "spot"};
constexpr std::array<float, 3> kCalibrationIntensities{1.0f, 16.0f, 32.0f};
constexpr char kProjectMaterial[] = "33333333-3333-4333-8333-333333333333";
constexpr char kTexture[] = "44444444-4444-4444-8444-444444444444";
constexpr char kNormalTexture[] = "cccccccc-cccc-4ccc-8ccc-cccccccccccc";
constexpr char kMissingMaterial[] = "55555555-5555-4555-8555-555555555555";
constexpr char kNamingScene[] = "77777777-7777-4777-8777-777777777777";
constexpr char kNamingCollisionScene[] = "dddddddd-dddd-4ddd-8ddd-dddddddddddd";
constexpr char kNamingNormalCollisionScene[] = "eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee";
constexpr char kNamingNormalCollisionMaterial[] = "ffffffff-ffff-4fff-8fff-ffffffffffff";
constexpr std::array<const char *, 2> kNamingMaterials{
    "88888888-8888-4888-8888-888888888888",
    "99999999-9999-4999-8999-999999999999"};
constexpr std::array<const char *, 2> kNamingTextures{
    "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
    "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb"};

std::string utf8_path(const std::filesystem::path &path) {
  const auto text = path.u8string();
  return {reinterpret_cast<const char *>(text.data()), text.size()};
}

ProjectRelativePath relative_path(std::string_view text) {
  auto parsed = ProjectRelativePath::parse_portable(text);
  EXPECT_TRUE(parsed.ok()) << parsed.error().message;
  return parsed.take_value();
}

SceneId scene_id(std::string_view text) {
  auto parsed = SceneId::parse(text);
  EXPECT_TRUE(parsed.ok()) << parsed.error().message;
  return parsed.take_value();
}

void write_bytes(const std::filesystem::path &path, std::string_view bytes) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  ASSERT_TRUE(stream.is_open()) << path.string();
  stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  stream.close();
  ASSERT_TRUE(stream.good()) << path.string();
}

QuaderBridgeSourceFingerprint fingerprint_file(const std::filesystem::path &path) {
  std::ifstream stream(path, std::ios::binary);
  EXPECT_TRUE(stream.is_open()) << path.string();
  const std::string bytes((std::istreambuf_iterator<char>(stream)), {});
  return {.byte_count = bytes.size(),
          .content_hash = quader_io::qdr_binary_content_hash(std::as_bytes(
              std::span(bytes.data(), bytes.size())))};
}

MeshAsset make_two_material_mesh() {
  MeshAsset mesh;
  mesh.name = "Two material triangles";
  auto &geometry = mesh.edit_geometry();
  geometry.vertices = {
      {.position = {0.0f, 0.0f, 0.0f}, .normal = {0.0f, 0.0f, 1.0f},
       .uv = {0.0f, 0.0f}},
      {.position = {1.0f, 0.0f, 0.0f}, .normal = {0.0f, 0.0f, 1.0f},
       .uv = {1.0f, 0.0f}},
      {.position = {0.0f, 1.0f, 0.0f}, .normal = {0.0f, 0.0f, 1.0f},
       .uv = {0.0f, 1.0f}},
      {.position = {1.0f, 0.0f, 0.0f}, .normal = {0.0f, 0.0f, 1.0f},
       .uv = {0.25f, 0.0f}},
      {.position = {1.0f, 1.0f, 0.0f}, .normal = {0.0f, 0.0f, 1.0f},
       .uv = {1.0f, 1.0f}},
      {.position = {0.0f, 1.0f, 0.0f}, .normal = {0.0f, 0.0f, 1.0f},
       .uv = {0.0f, 0.75f}},
  };
  geometry.indices = {0u, 1u, 2u, 3u, 4u, 5u};
  geometry.primitives = {{.index_offset = 0u, .index_count = 3u,
                          .material_slot = 0u},
                         {.index_offset = 3u, .index_count = 3u,
                          .material_slot = 1u}};
  auto authored = std::make_shared<quader_poly::Document>();
  authored->next_vertex_id = 5u;
  authored->next_face_id = 3u;
  authored->vertices = {{.id = 1u, .position = {0.0f, 0.0f, 0.0f}},
                        {.id = 2u, .position = {1.0f, 0.0f, 0.0f}},
                        {.id = 3u, .position = {0.0f, 1.0f, 0.0f}},
                        {.id = 4u, .position = {1.0f, 1.0f, 0.0f}}};
  quader_poly::Face first;
  first.id = 1u;
  first.vertices = {1u, 2u, 3u};
  first.uvs = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}};
  first.normal_shading = quader_poly::SurfaceShading::Authored;
  quader_poly::Face second;
  second.id = 2u;
  second.vertices = {2u, 4u, 3u};
  second.uvs = {{0.25f, 0.0f}, {1.0f, 1.0f}, {0.0f, 0.75f}};
  second.material_slot = 1u;
  second.normal_shading = quader_poly::SurfaceShading::Authored;
  authored->faces = {std::move(first), std::move(second)};
  mesh.has_authored_polygon_document = true;
  mesh.authored_polygon_document = std::move(authored);
  return mesh;
}

SceneContentDocument make_document(bool mixed_materials = false,
                                   const char *material_id = kProjectMaterial,
                                   std::string_view embedded_name = "Embedded blue") {
  SceneContentDocument document({.id = 7u, .name = "Bridge fixture"});
  EXPECT_EQ(document.assets().add_mesh(10u, make_two_material_mesh()), 10u);
  auto material = make_default_quader_material(std::string(embedded_name));
  material.base_color = {0.1f, 0.2f, 0.8f};
  material.roughness = 0.35f;
  material.metallic = 0.6f;
  EXPECT_EQ(document.assets().add_material(20u, std::move(material)), 20u);
  const EntityId entity = document.scene().create_entity_with_id(
      100u, "Fixture mesh", EntityKind::Mesh);
  EXPECT_EQ(entity, 100u);
  EXPECT_TRUE(document.scene().set_mesh(
      entity, MeshComponent{.mesh = 10u, .collision_type = MeshCollisionType::Mesh}));
  MaterialComponent binding{.material = 20u};
  if (mixed_materials) {
    binding.material_slots.emplace(
        1u, MaterialReference::project(*ProjectAssetId::parse(material_id)));
  }
  EXPECT_TRUE(document.scene().set_material(entity, std::move(binding)));
  return document;
}

/// Couples the saved fixture with its independent signed radial UV expectation.
struct CircularBridgeFixture {
  SceneContentDocument document;
  double v_radius{};
};

CircularBridgeFixture make_circular_bridge_fixture() {
  namespace qm = quader::modeling;
  qm::AuthoredTopology topology;
  for (std::uint32_t station = 0u; station < 3u; ++station) {
    const float angle = .2f * static_cast<float>(station);
    for (std::uint32_t rail = 0u; rail < 3u; ++rail) {
      const float radius = static_cast<float>(rail + 1u);
      topology.vertices.push_back({qm::make_id<qm::VertexTag>(station * 3u + rail + 1u),
          {radius * std::cos(angle), 0.0f, radius * std::sin(angle)}});
    }
  }
  for (std::uint32_t station = 0u; station < 2u; ++station) {
    for (std::uint32_t rail = 0u; rail < 2u; ++rail) {
      const auto first = station * 3u + rail + 1u;
      topology.faces.push_back({.id = qm::make_id<qm::FaceTag>(station * 2u + rail + 1u),
          .vertices = {qm::make_id<qm::VertexTag>(first), qm::make_id<qm::VertexTag>(first + 3u),
                       qm::make_id<qm::VertexTag>(first + 4u), qm::make_id<qm::VertexTag>(first + 1u)}});
    }
  }
  topology.vertices.insert(topology.vertices.end(), {
      {qm::make_id<qm::VertexTag>(10u), {4.0f, 0.0f, 0.0f}},
      {qm::make_id<qm::VertexTag>(11u), {4.0f, 0.0f, 1.0f}},
      {qm::make_id<qm::VertexTag>(12u), {5.0f, 0.0f, 0.0f}}});
  topology.faces.push_back({.id = qm::make_id<qm::FaceTag>(5u),
      .vertices = {qm::make_id<qm::VertexTag>(10u), qm::make_id<qm::VertexTag>(11u),
                   qm::make_id<qm::VertexTag>(12u)}, .material_slot = 1u});
  topology.next_vertex_id = 13u;
  topology.next_face_id = 6u;
  auto source = qm::PolygonDocument::from_authored_topology(topology).or_throw();
  const qm::UvPeelTarget target{.key = 1u, .document = &source,
      .edges = {qm::make_edge_key(qm::make_id<qm::VertexTag>(2u), qm::make_id<qm::VertexTag>(5u)),
                qm::make_edge_key(qm::make_id<qm::VertexTag>(5u), qm::make_id<qm::VertexTag>(8u))}};
  qm::UvPeelOptions options;
  options.smooth_circular_flow = true;
  auto peeled = qm::prepare_uv_peel(std::span(&target, 1u), options).or_throw()
                    .documents.front().document;
  const double v_radius = -peeled.authored_topology().or_throw().faces.front().uvs.front().y;
  EXPECT_NEAR(std::abs(v_radius), .5, 1e-6);
  std::vector<qm::FaceTextureTransformUpdate> transforms;
  for (std::uint32_t id = 1u; id <= 4u; ++id) {
    transforms.push_back({.face = qm::make_id<qm::FaceTag>(id), .scale_x = 2.0f,
        .scale_y = .5f, .shift_x = .3f, .shift_y = -.2f, .rotation_degrees = 30.0f});
  }
  (void)peeled.set_face_texture_transforms(transforms).or_throw();
  auto projected = MeshProjectionService::compile_sdk_document_mesh(peeled);
  if (!projected.has_value()) throw std::runtime_error("Could not project circular bridge fixture.");
  auto document = make_document();
  EXPECT_TRUE(document.assets().replace_mesh(10u, std::move(*projected)));
  EXPECT_EQ(document.assets().add_material(30u, make_default_quader_material("Ordinary triangle")), 30u);
  MaterialComponent binding{.material = 20u};
  binding.material_slots.emplace(1u, MaterialReference{30u});
  EXPECT_TRUE(document.scene().set_material(100u, std::move(binding)));
  return {std::move(document), v_radius};
}

/// Computes the intended curved field from physical radius/angle, not bridge metadata.
std::array<double, 2> circular_bridge_expected_uv(double x, double z, double v_radius) {
  const double u = (std::sin(.1) / .1) * std::atan2(z, x);
  const double v = v_radius * (std::hypot(x, z) - 2.0);
  const double sine = .5, cosine = std::sqrt(3.0) * .5;
  return {.3 + (cosine * u - sine * v) / 2.0,
          -.2 + (sine * u + cosine * v) / .5};
}

void expect_circular_bridge_samples(const QuaderBridgeMeshView &mesh,
    const QuaderBridgeMeshUvInterpolationView &sampling, double v_radius,
    bool translated_sampling = false) {
  ASSERT_EQ(mesh.vertex_count, 19u);
  ASSERT_EQ(mesh.index_count, 27u);
  ASSERT_EQ(sampling.vertex_count, mesh.vertex_count);
  ASSERT_NE(sampling.vertices, nullptr);
  ASSERT_EQ(mesh.surface_count, 2u);
  EXPECT_EQ(mesh.surfaces[0].index_count, 24u);
  EXPECT_EQ(mesh.surfaces[1].index_count, 3u);
  EXPECT_NE(mesh.surfaces[0].material_index, mesh.surfaces[1].material_index);
  for (std::uint32_t vertex = 0u; vertex < mesh.vertex_count; ++vertex) {
    const auto &entry = sampling.vertices[vertex];
    if (vertex >= 16u) {
      EXPECT_EQ(entry.mode, QUADER_BRIDGE_UV_INTERPOLATION_NONE);
      for (std::size_t axis = 0u; axis < 4u; ++axis) {
        EXPECT_FLOAT_EQ(entry.coordinates[axis], 0.0f);
        EXPECT_FLOAT_EQ(entry.correction[axis], 0.0f);
      }
    } else {
      EXPECT_EQ(entry.mode, QUADER_BRIDGE_UV_INTERPOLATION_CIRCULAR);
      EXPECT_NEAR(entry.coordinates[2], std::hypot(mesh.vertices[vertex].position[0],
                                                 mesh.vertices[vertex].position[2]), 2e-5);
      EXPECT_NEAR(entry.coordinates[3], std::atan2(entry.coordinates[1], entry.coordinates[0]), 1e-6);
      EXPECT_NEAR(entry.correction[0], -.25 * v_radius, 2e-5);
      EXPECT_NEAR(entry.correction[2], std::sqrt(3.0) * v_radius, 2e-5);
    }
  }
  constexpr std::array<double, 3> kWeights{.2, .35, .45};
  double maximum_correction = 0.0;
  for (std::uint32_t offset = 0u; offset < mesh.index_count; offset += 3u) {
    std::array<double, 3> position{};
    std::array<double, 2> uv{};
    std::array<double, 4> coordinate{};
    for (std::size_t corner = 0u; corner < 3u; ++corner) {
      const auto index = mesh.indices[offset + corner];
      ASSERT_LT(index, mesh.vertex_count);
      for (std::size_t axis = 0u; axis < 3u; ++axis) {
        position[axis] += kWeights[corner] * mesh.vertices[index].position[axis];
      }
      for (std::size_t axis = 0u; axis < 2u; ++axis) {
        uv[axis] += kWeights[corner] * mesh.vertices[index].uv[axis];
      }
      for (std::size_t axis = 0u; axis < 4u; ++axis) {
        coordinate[axis] += kWeights[corner] * sampling.vertices[index].coordinates[axis];
      }
    }
    const auto &a = mesh.vertices[mesh.indices[offset]];
    const auto &b = mesh.vertices[mesh.indices[offset + 1u]];
    const auto &c = mesh.vertices[mesh.indices[offset + 2u]];
    const double winding_y = (b.position[2] - a.position[2]) * (c.position[0] - a.position[0]) -
                             (b.position[0] - a.position[0]) * (c.position[2] - a.position[2]);
    EXPECT_GT(winding_y, 0.0);
    if (offset >= 24u) continue;
    const auto &field = sampling.vertices[mesh.indices[offset]];
    const double radius_delta = std::hypot(coordinate[0], coordinate[1]) - coordinate[2];
    const double angle_delta = std::atan2(coordinate[1], coordinate[0]) - coordinate[3];
    const std::array<double, 2> correction{
        field.correction[0] * radius_delta + field.correction[1] * angle_delta,
        field.correction[2] * radius_delta + field.correction[3] * angle_delta};
    auto expected = circular_bridge_expected_uv(position[0], position[2], v_radius);
    const auto anchor = circular_bridge_expected_uv(a.position[0], a.position[2], v_radius);
    if (translated_sampling) {
      expected[0] += a.uv[0] - anchor[0];
      expected[1] += a.uv[1] - anchor[1];
      EXPECT_GT(std::hypot(a.uv[0] - anchor[0], a.uv[1] - anchor[1]), 1e-3);
    }
    EXPECT_NEAR(uv[0] + correction[0], expected[0], 3e-5);
    EXPECT_NEAR(uv[1] + correction[1], expected[1], 3e-5);
    maximum_correction = std::max(maximum_correction, std::hypot(correction[0], correction[1]));
  }
  EXPECT_GT(maximum_correction, 1e-4);
}

MeshAsset make_quad_bridge_mesh(bool affine_uv = false, bool nonplanar = false) {
  quader_poly::Document document;
  document.next_vertex_id = 5u;
  document.next_face_id = 2u;
  document.vertices = {
      {.id = 1u, .position = {0.0f, 0.0f, 0.0f}},
      {.id = 2u, .position = {2.0f, 0.0f, 0.0f}},
      {.id = 3u, .position = {1.5f, 1.0f, nonplanar ? .2f : 0.0f}},
      {.id = 4u, .position = {0.0f, 1.0f, 0.0f}}};
  document.faces = {{.id = 1u, .vertices = {1u, 2u, 3u, 4u},
      .uvs = {{0.0f, 0.0f}, {1.0f, 0.0f}, {affine_uv ? .75f : 1.25f, 1.0f},
              {0.0f, 1.0f}}}};
  auto mesh = MeshProjectionService::compile_polygon_document_mesh(document);
  mesh.replace_authored_polygon_document(std::move(document));
  return mesh;
}

void expect_quad_bridge_samples(const QuaderBridgeMeshView &mesh,
                                const QuaderBridgeMeshUvInterpolationView &sampling,
                                const MeshGeometryPayload &source) {
  ASSERT_EQ(mesh.vertex_count, 4u);
  ASSERT_EQ(mesh.index_count, 6u);
  ASSERT_EQ(mesh.surface_count, 1u);
  EXPECT_EQ(mesh.collision_point_count, 6u);
  ASSERT_EQ(sampling.vertex_count, mesh.vertex_count);
  ASSERT_NE(sampling.vertices, nullptr);
  ASSERT_EQ(source.vertices.size(), mesh.vertex_count);
  ASSERT_EQ(source.indices.size(), mesh.index_count);
  const std::array<double, 2> phase{
      mesh.vertices[0].uv[0] - source.vertices[0].uv.x,
      mesh.vertices[0].uv[1] - source.vertices[0].uv.y};
  EXPECT_GT(std::hypot(phase[0], phase[1]), 1e-3);
  for (std::uint32_t index = 0u; index < mesh.vertex_count; ++index) {
    const auto &vertex = mesh.vertices[index];
    EXPECT_EQ(sampling.vertices[index].mode, QUADER_BRIDGE_UV_INTERPOLATION_QUAD);
    EXPECT_FLOAT_EQ(sampling.vertices[index].coordinates[3], 1.0f);
    EXPECT_FLOAT_EQ(vertex.position[0], source.vertices[index].position.x);
    EXPECT_FLOAT_EQ(vertex.position[1], source.vertices[index].position.y);
    EXPECT_FLOAT_EQ(vertex.position[2], source.vertices[index].position.z);
    EXPECT_NEAR(vertex.uv[0] - source.vertices[index].uv.x, phase[0], 1e-6);
    EXPECT_NEAR(vertex.uv[1] - source.vertices[index].uv.y, phase[1], 1e-6);
  }
  for (std::uint32_t index = 0u; index < mesh.index_count; ++index) {
    EXPECT_EQ(mesh.indices[index], source.indices[index]);
  }
  double maximum_correction = 0.0;
  for (const double s : {.0, .17, .5, .83, 1.0}) {
    for (const double t : {.0, .23, .5, .79, 1.0}) {
      // Forward bilinear geometry is x=2s-.5st,y=t, and the authored UV
      // field is U=s+.25st,V=t. This expectation uses neither packed warp
      // coefficients nor the production inverse-coordinate preparation.
      const double x = 2.0 * s - .5 * s * t;
      const double y = t;
      const std::array<double, 2> expected{s + .25 * s * t + phase[0], t + phase[1]};
      bool sampled = false;
      for (std::uint32_t offset = 0u; offset < mesh.index_count; offset += 3u) {
        const auto &a = mesh.vertices[mesh.indices[offset]];
        const auto &b = mesh.vertices[mesh.indices[offset + 1u]];
        const auto &c = mesh.vertices[mesh.indices[offset + 2u]];
        const double ab_x = b.position[0] - a.position[0];
        const double ab_y = b.position[1] - a.position[1];
        const double ac_x = c.position[0] - a.position[0];
        const double ac_y = c.position[1] - a.position[1];
        const double determinant = ab_x * ac_y - ab_y * ac_x;
        ASSERT_GT(determinant, 0.0);
        const double wb = ((x - a.position[0]) * ac_y - (y - a.position[1]) * ac_x) / determinant;
        const double wc = (ab_x * (y - a.position[1]) - ab_y * (x - a.position[0])) / determinant;
        const std::array<double, 3> weights{1.0 - wb - wc, wb, wc};
        if (std::ranges::any_of(weights, [](double weight) { return weight < -1e-7; })) continue;
        std::array<double, 2> uv{};
        std::array<double, 3> coordinates{};
        for (std::size_t corner = 0u; corner < weights.size(); ++corner) {
          const auto index = mesh.indices[offset + corner];
          for (std::size_t axis = 0u; axis < uv.size(); ++axis) {
            uv[axis] += weights[corner] * mesh.vertices[index].uv[axis];
          }
          for (std::size_t axis = 0u; axis < coordinates.size(); ++axis) {
            coordinates[axis] += weights[corner] * sampling.vertices[index].coordinates[axis];
          }
        }
        const auto &field = sampling.vertices[mesh.indices[offset]];
        const auto actual = quader_geometry::evaluate_quad_uv_interpolation(
            {static_cast<float>(uv[0]), static_cast<float>(uv[1])},
            {static_cast<float>(coordinates[0]), static_cast<float>(coordinates[1]),
             static_cast<float>(coordinates[2])},
            {field.correction[0], field.correction[1]},
            {field.correction[2], field.correction[3]});
        EXPECT_NEAR(actual.x, expected[0], 3e-6);
        EXPECT_NEAR(actual.y, expected[1], 3e-6);
        maximum_correction = std::max(maximum_correction, std::hypot(actual.x - uv[0], actual.y - uv[1]));
        sampled = true;
      }
      EXPECT_TRUE(sampled);
    }
  }
  EXPECT_GT(maximum_correction, .01);
}

/// Creates deterministic authored lights independently of any renderer mapping.
SceneContentDocument make_lighting_document(std::span<const float> intensities,
                                            float falloff, bool calibration) {
  SceneContentDocument document({.id = 8u, .name = "Lighting calibration"});
  auto &scene = document.scene();
  SceneSettings settings;
  settings.environment.enabled = false;
  settings.environment.show_background = false;
  settings.environment.clear_color = {0.0f, 0.0f, 0.0f};
  EXPECT_TRUE(scene.set_scene_settings(settings));
  EntityId id = 300u;
  for (std::size_t shape = 0u; shape < kLightShapes.size(); ++shape) {
    for (const float intensity : intensities) {
      const std::string name = calibration
          ? "calibration-" + std::string(kLightShapeSlugs[shape]) + "-" +
                std::to_string(static_cast<int>(intensity))
          : std::string(kLightShapeNames[shape]) +
                std::to_string(static_cast<int>(intensity));
      EXPECT_EQ(scene.create_entity_with_id(id, name, EntityKind::Light), id);
      LightComponent light;
      light.type = kLightShapes[shape];
      light.color = {1.0f, 1.0f, 1.0f};
      light.intensity = intensity;
      light.range = 12.0f;
      light.distance_attenuation = falloff;
      light.casts_shadows = false;
      EXPECT_TRUE(scene.set_light(id, light));
      EXPECT_TRUE(scene.set_transform(id, TransformComponent{.location = {0, 3, 0}}));
      ++id;
    }
  }
  return document;
}

/// Publishes fixture metadata consumed by the maintained Godot import checks.
nlohmann::ordered_json lighting_expectations(const SceneContentDocument &document) {
  auto result = nlohmann::ordered_json::array();
  for (const EntityId entity : document.scene().entity_tree_order()) {
    const auto *light = document.scene().light(entity);
    if (light == nullptr) continue;
    const auto engine = light_engine_values(*light);
    result.push_back({
        {"name", document.scene().name(entity)->name},
        {"entity", entity}, {"type", static_cast<int>(light->type)},
        {"color", {light->color.x, light->color.y, light->color.z}},
        {"intensity", light->intensity}, {"range", light->range},
        {"falloff", light->distance_attenuation},
        {"casts_shadows", light->casts_shadows},
        {"light_unit", static_cast<int>(light->light_unit)},
        {"color_mode", static_cast<int>(light->color_mode)},
        {"temperature_kelvin", light->color_temperature_kelvin},
        {"source_preset", static_cast<int>(light->source_preset)},
        {"preview_energy", engine.preview_energy},
        {"illuminance_lux", engine.illuminance_lux},
        {"luminous_intensity_candela", engine.luminous_intensity_candela},
    });
  }
  return result;
}

/// Exercises the public reader ABI against disposable canonical native files.
class BridgeReaderTests : public testing::Test {
protected:
  void SetUp() override {
    api_ = quader_bridge_get_api(QUADER_BRIDGE_ABI_VERSION);
    ASSERT_NE(api_, nullptr);
    static std::atomic<std::uint64_t> sequence{0u};
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    root_ = std::filesystem::temp_directory_path() /
            ("quader-bridge-" + std::to_string(tick) + "-" +
             std::to_string(sequence.fetch_add(1u)));
    ASSERT_TRUE(std::filesystem::create_directory(root_));
    auto created = ProjectManifestStore::create_in_empty_directory(root_);
    ASSERT_TRUE(created.ok()) << created.error().message;
    workspace_ = created.take_value();
    auto manifest = ProjectManifest::create(
        ProjectId::parse("66666666-6666-4666-8666-666666666666").take_value(),
        root_.filename().string());
    ASSERT_TRUE(manifest.ok()) << manifest.error().message;
    workspace_.manifest = manifest.take_value();
    save_manifest();
  }

  void TearDown() override {
    if (api_ != nullptr) {
      api_->release_blob(blob_);
      api_->release_scene(scene_);
      api_->release_project(project_);
    }
    if (!root_.empty() && root_.parent_path() ==
                              std::filesystem::temp_directory_path() &&
        root_.filename().string().starts_with("quader-bridge-")) {
      std::error_code ignored;
      std::filesystem::remove_all(root_, ignored);
    }
  }

  void save_manifest() {
    const auto saved = ProjectManifestStore::save_atomic(workspace_);
    ASSERT_TRUE(saved.ok()) << saved.error().message;
  }

  void add_scene(const char *id, std::string_view path,
                const SceneContentDocument &document,
                const char *project_material = nullptr,
                std::string_view material_path = {}) {
    auto snapshot = qdr_project_write_snapshot_from_document(document);
    ASSERT_TRUE(snapshot.ok()) << snapshot.error().message;
    if (project_material != nullptr) {
      snapshot.value().project_asset_dependencies.push_back({
          .stable_id = *ProjectAssetId::parse(project_material),
          .kind = ProjectAssetKind::Material,
          .path = relative_path(!material_path.empty() ? material_path
                                : std::string_view(project_material) == kMissingMaterial
                                    ? "Materials/missing.qmat" : "Materials/surface.qmat"),
      });
    }
    const auto relative = relative_path(path);
    const auto resolved = workspace_.file_system.resolve_for_write(relative);
    ASSERT_TRUE(resolved.ok()) << resolved.error().message;
    const auto written = QdrDocumentWriter::write_project_document_atomic(
        resolved.value(), snapshot.value(), scene_id(id).bytes());
    ASSERT_TRUE(written.ok) << written.message;
    const auto registered = workspace_.manifest.register_scene(
        {.id = scene_id(id), .path = relative});
    ASSERT_TRUE(registered.ok()) << registered.error().message;
    save_manifest();
  }

  void open_project() {
    ASSERT_EQ(api_->open_project(utf8_path(workspace_.manifest_path).c_str(),
                                &project_, &error_), 1)
        << error_.message;
  }

  void open_scene(const char *id = kFirstScene) {
    ASSERT_NE(project_, nullptr);
    ASSERT_EQ(api_->open_scene(project_, id, &scene_, &error_), 1)
        << error_.message;
  }

  std::uint32_t material_index(std::uint32_t kind) {
    QuaderBridgeSceneView scene_view{};
    EXPECT_EQ(api_->scene_view(scene_, &scene_view, &error_), 1);
    for (std::uint32_t index = 0u; index < scene_view.material_count; ++index) {
      QuaderBridgeMaterialIdentity identity{};
      EXPECT_EQ(api_->material_identity(scene_, index, &identity, &error_), 1);
      if (identity.kind == kind) {
        return index;
      }
    }
    ADD_FAILURE() << "Expected material kind " << kind;
    return QUADER_BRIDGE_NO_MATERIAL;
  }

  void write_project_material(const char *id = kProjectMaterial,
                            std::string_view texture = "Textures/albedo.PNG",
                            bool all_textures = false,
                            std::string_view resource_prefix = {},
                            bool separate_normal = false) {
    std::filesystem::create_directories(root_ / "Materials");
    std::filesystem::create_directories(root_ / "Textures");
    ProjectMaterialDocument material;
    material.id = *ProjectAssetId::parse(id);
    material.name = std::string(resource_prefix) +
                    (std::string_view(id) == kMissingMaterial ? "Missing surface" : "Project surface");
    auto &standard = *material.standard_material();
    standard.base_color = {0.2f, 0.4f, 0.6f};
    standard.roughness = 0.45f;
    standard.metallic = 0.7f;
    standard.normal_strength = 0.8f;
    standard.normal_convention = NormalMapConvention::DirectX;
    standard.texture_transform = {.center = {0.25f, 0.75f},
                                  .scale = {2.0f, -1.0f},
                                  .offset = {0.2f, -0.3f},
                                  .rotation_degrees = 35.0f};
    standard.base_color_texture = ProjectMaterialTextureSlot{
        .id = *ProjectAssetId::parse(kTexture),
        .project_relative_path = std::string(texture),
        .color_space = ProjectTextureColorSpace::Srgb,
    };
    if (all_textures) {
      auto linear = *standard.base_color_texture;
      linear.color_space = ProjectTextureColorSpace::Linear;
      standard.roughness_texture = linear;
      standard.metallic_texture = linear;
      standard.normal_texture = linear;
      if (separate_normal) {
        standard.normal_texture->id = *ProjectAssetId::parse(kNormalTexture);
        standard.normal_texture->project_relative_path =
            "Textures/" + std::string(resource_prefix) + "normal.PNG";
      }
    }
    const auto written = QmatCodec::write_atomic(
        workspace_.file_system,
        relative_path("Materials/" + std::string(resource_prefix) +
                      (std::string_view(id) == kMissingMaterial
                           ? "missing.qmat" : "surface.qmat")),
        material);
    ASSERT_TRUE(written.ok()) << written.error().message;
  }

  const QuaderBridgeApi *api_ = nullptr;
  QuaderBridgeProject *project_ = nullptr;
  QuaderBridgeScene *scene_ = nullptr;
  QuaderBridgeBlob *blob_ = nullptr;
  QuaderBridgeError error_{};
  std::filesystem::path root_;
  ProjectWorkspaceStorage workspace_;
};

TEST_F(BridgeReaderTests, RejectsUnsupportedAbiAndInvalidArguments) {
  EXPECT_EQ(quader_bridge_get_api(0u), nullptr);
  EXPECT_EQ(quader_bridge_get_api(1u), nullptr);
  EXPECT_EQ(quader_bridge_get_api(2u), nullptr);
  EXPECT_EQ(quader_bridge_get_api(3u), nullptr);
  EXPECT_EQ(quader_bridge_get_api(4u), nullptr);
  EXPECT_EQ(quader_bridge_get_api(5u), nullptr);
  EXPECT_EQ(quader_bridge_get_api(QUADER_BRIDGE_ABI_VERSION + 1u), nullptr);
  EXPECT_EQ(api_->abi_version, QUADER_BRIDGE_ABI_VERSION);
  EXPECT_EQ(api_->struct_size, sizeof(QuaderBridgeApi));
  EXPECT_EQ(api_->open_project(nullptr, &project_, &error_), 0);
  EXPECT_EQ(project_, nullptr);
  EXPECT_NE(error_.message[0], '\0');
  EXPECT_EQ(api_->open_project(utf8_path(workspace_.manifest_path).c_str(),
                              nullptr, nullptr), 0);
  QuaderBridgeProjectView project_view{};
  EXPECT_EQ(api_->project_view(nullptr, &project_view, &error_), 0);
  EXPECT_EQ(api_->scene_view(nullptr, nullptr, nullptr), 0);
  EXPECT_EQ(api_->mesh_view(nullptr, 0u, nullptr, nullptr), 0);
  EXPECT_EQ(api_->material_identity(nullptr, 0u, nullptr, nullptr), 0);
  EXPECT_EQ(api_->material_view(nullptr, 0u, nullptr, nullptr), 0);
  EXPECT_EQ(api_->read_texture(nullptr, 0u, 0u, &blob_, nullptr), 0);
  EXPECT_EQ(api_->blob_view(nullptr, nullptr, nullptr), 0);
  EXPECT_EQ(api_->scene_source_fingerprint(nullptr, nullptr, nullptr), 0);
  EXPECT_EQ(api_->material_source_fingerprint(nullptr, 0u, nullptr, nullptr), 0);
  api_->release_project(nullptr);
  api_->release_scene(nullptr);
  api_->release_blob(nullptr);
}

TEST_F(BridgeReaderTests, OptionalUvInterpolationExtensionKeepsCoreAbiAndOrdinaryMeshesEmpty) {
  EXPECT_EQ(QUADER_BRIDGE_ABI_VERSION, 6u);
  EXPECT_EQ(quader_bridge_get_uv_interpolation_api(0u), nullptr);
  EXPECT_EQ(quader_bridge_get_uv_interpolation_api(
      QUADER_BRIDGE_UV_INTERPOLATION_ABI_VERSION + 1u), nullptr);
  const auto *extension = quader_bridge_get_uv_interpolation_api(
      QUADER_BRIDGE_UV_INTERPOLATION_ABI_VERSION);
  ASSERT_NE(extension, nullptr);
  EXPECT_EQ(extension->abi_version, 1u);
  EXPECT_EQ(extension->struct_size, sizeof(QuaderBridgeUvInterpolationApi));
  ASSERT_NE(extension->mesh_uv_interpolation_view, nullptr);
  QuaderBridgeMeshUvInterpolationView view{};
  EXPECT_EQ(extension->mesh_uv_interpolation_view(nullptr, 0u, &view, &error_), 0);
  EXPECT_NE(error_.message[0], '\0');
  add_scene(kFirstScene, "Scenes/first.qdr", make_document());
  open_project();
  open_scene();
  ASSERT_EQ(extension->mesh_uv_interpolation_view(scene_, 0u, &view, &error_), 1)
      << error_.message;
  EXPECT_EQ(view.vertex_count, 0u);
  EXPECT_EQ(view.vertices, nullptr);
  EXPECT_EQ(extension->mesh_uv_interpolation_view(scene_, 0u, nullptr, &error_), 0);
  EXPECT_EQ(extension->mesh_uv_interpolation_view(scene_, 1u, &view, &error_), 0);
  EXPECT_EQ(extension->mesh_uv_interpolation_view(nullptr, 0u, nullptr, nullptr), 0);
}

TEST_F(BridgeReaderTests, QuadUvExtensionPreservesBilinearInteriorsThroughMappedMirroredOccurrences) {
  auto document = make_document();
  auto mesh_asset = make_quad_bridge_mesh();
  ASSERT_TRUE(mesh_asset.has_authored_polygon_document);
  ASSERT_EQ(mesh_asset.authored_polygon_document->faces.front().vertices.size(), 4u);
  const MeshGeometryPayload expected = mesh_asset.geometry();
  ASSERT_TRUE(document.assets().replace_mesh(10u, std::move(mesh_asset)));
  auto &source = document.scene();
  ASSERT_EQ(source.create_entity_with_id(200u, "Quad definition", EntityKind::Generic), 200u);
  ASSERT_TRUE(source.set_parent(100u, 200u, false));
  ASSERT_TRUE(source.set_instance_definition(200u));
  ASSERT_EQ(source.create_entity_with_id(202u, "Quad occurrence", EntityKind::Instance), 202u);
  ASSERT_TRUE(source.set_instance_occurrence(202u, InstanceOccurrenceComponent{.definition = 200u}));
  ASSERT_TRUE(source.set_transform(202u, TransformComponent{
      .location = {8.0f, 3.0f, 4.0f}, .scale = {-1.0f, 2.0f, 1.0f}}));
  TextureMappingComponent member_mapping, occurrence_mapping;
  member_mapping.sampling_from_local.values[12] = 1.0f;
  occurrence_mapping.sampling_from_local.values[12] = 2.0f;
  ASSERT_TRUE(source.set_texture_mapping(100u, member_mapping));
  ASSERT_TRUE(source.set_texture_mapping(202u, occurrence_mapping));
  add_scene(kFirstScene, "Scenes/quad.qdr", document);
  open_project();
  open_scene();
  const auto *extension = quader_bridge_get_uv_interpolation_api(
      QUADER_BRIDGE_UV_INTERPOLATION_ABI_VERSION);
  ASSERT_NE(extension, nullptr);
  QuaderBridgeMeshView mesh{};
  QuaderBridgeMeshUvInterpolationView sampling{};
  ASSERT_EQ(api_->mesh_view(scene_, 0u, &mesh, &error_), 1) << error_.message;
  ASSERT_EQ(extension->mesh_uv_interpolation_view(scene_, 0u, &sampling, &error_), 1)
      << error_.message;
  EXPECT_EQ(mesh.display_entity, 202u);
  EXPECT_EQ(mesh.geometry_entity, 100u);
  EXPECT_FLOAT_EQ(mesh.transform[0], -1.0f);
  EXPECT_FLOAT_EQ(mesh.transform[5], 2.0f);
  EXPECT_FLOAT_EQ(mesh.transform[12], 8.0f);
  expect_quad_bridge_samples(mesh, sampling, expected);
}

TEST_F(BridgeReaderTests, QuadUvExtensionKeepsAffineAndNonplanarFacesOnTriangleSampling) {
  auto document = make_document();
  ASSERT_TRUE(document.assets().replace_mesh(10u, make_quad_bridge_mesh(true)));
  ASSERT_EQ(document.assets().add_mesh(11u, make_quad_bridge_mesh(false, true)), 11u);
  ASSERT_EQ(document.scene().create_entity_with_id(101u, "Nonplanar quad", EntityKind::Mesh), 101u);
  ASSERT_TRUE(document.scene().set_mesh(101u, MeshComponent{.mesh = 11u}));
  add_scene(kFirstScene, "Scenes/fallback.qdr", document);
  open_project();
  open_scene();
  QuaderBridgeSceneView scene{};
  ASSERT_EQ(api_->scene_view(scene_, &scene, &error_), 1) << error_.message;
  ASSERT_EQ(scene.mesh_count, 2u);
  const auto *extension = quader_bridge_get_uv_interpolation_api(
      QUADER_BRIDGE_UV_INTERPOLATION_ABI_VERSION);
  ASSERT_NE(extension, nullptr);
  for (std::uint32_t index = 0u; index < scene.mesh_count; ++index) {
    QuaderBridgeMeshView mesh{};
    QuaderBridgeMeshUvInterpolationView sampling{};
    ASSERT_EQ(api_->mesh_view(scene_, index, &mesh, &error_), 1) << error_.message;
    ASSERT_EQ(extension->mesh_uv_interpolation_view(scene_, index, &sampling, &error_), 1)
        << error_.message;
    EXPECT_EQ(mesh.vertex_count, 4u);
    EXPECT_EQ(mesh.index_count, 6u);
    EXPECT_EQ(sampling.vertex_count, 0u);
    EXPECT_EQ(sampling.vertices, nullptr);
  }
}

TEST_F(BridgeReaderTests, CircularUvExtensionPreservesAnalyticSamplingGeometryAndBorrowedLifetime) {
  auto fixture = make_circular_bridge_fixture();
  add_scene(kFirstScene, "Scenes/first.qdr", fixture.document);
  open_project();
  open_scene();
  const auto *extension = quader_bridge_get_uv_interpolation_api(
      QUADER_BRIDGE_UV_INTERPOLATION_ABI_VERSION);
  ASSERT_NE(extension, nullptr);
  QuaderBridgeMeshView mesh{};
  QuaderBridgeMeshUvInterpolationView sampling{};
  ASSERT_EQ(api_->mesh_view(scene_, 0u, &mesh, &error_), 1) << error_.message;
  ASSERT_EQ(extension->mesh_uv_interpolation_view(scene_, 0u, &sampling, &error_), 1)
      << error_.message;
  expect_circular_bridge_samples(mesh, sampling, fixture.v_radius);
  EXPECT_EQ(mesh.collision_point_count, 27u);
  const auto *borrowed = sampling.vertices;
  const auto first = sampling.vertices[0];
  api_->release_project(std::exchange(project_, nullptr));
  QuaderBridgeSceneView scene_view{};
  ASSERT_EQ(api_->scene_view(scene_, &scene_view, &error_), 1);
  QuaderBridgeMeshUvInterpolationView retained{};
  ASSERT_EQ(extension->mesh_uv_interpolation_view(scene_, 0u, &retained, &error_), 1);
  EXPECT_EQ(retained.vertices, borrowed);
  EXPECT_EQ(retained.vertex_count, mesh.vertex_count);
  for (std::size_t axis = 0u; axis < 4u; ++axis) {
    EXPECT_FLOAT_EQ(retained.vertices[0].coordinates[axis], first.coordinates[axis]);
    EXPECT_FLOAT_EQ(retained.vertices[0].correction[axis], first.correction[axis]);
  }
  expect_circular_bridge_samples(mesh, retained, fixture.v_radius);
}

TEST_F(BridgeReaderTests, CircularUvExtensionRetainsLocalSamplingThroughMirroredMappedOccurrences) {
  auto fixture = make_circular_bridge_fixture();
  auto &source = fixture.document.scene();
  ASSERT_EQ(source.create_entity_with_id(200u, "Circular definition", EntityKind::Generic), 200u);
  ASSERT_TRUE(source.set_parent(100u, 200u, false));
  ASSERT_TRUE(source.set_instance_definition(200u));
  ASSERT_EQ(source.create_entity_with_id(202u, "Circular occurrence", EntityKind::Instance), 202u);
  ASSERT_TRUE(source.set_instance_occurrence(202u, InstanceOccurrenceComponent{.definition = 200u}));
  ASSERT_TRUE(source.set_transform(202u, TransformComponent{
      .location = {8.0f, 3.0f, 4.0f}, .scale = {-1.0f, 2.0f, 1.0f}}));
  TextureMappingComponent member_mapping, occurrence_mapping;
  member_mapping.sampling_from_local.values[12] = 1.0f;
  occurrence_mapping.sampling_from_local.values[12] = 2.0f;
  ASSERT_TRUE(source.set_texture_mapping(100u, member_mapping));
  ASSERT_TRUE(source.set_texture_mapping(202u, occurrence_mapping));
  add_scene(kFirstScene, "Scenes/first.qdr", fixture.document);
  open_project();
  open_scene();
  QuaderBridgeSceneView scene_view{};
  ASSERT_EQ(api_->scene_view(scene_, &scene_view, &error_), 1);
  ASSERT_EQ(scene_view.mesh_count, 1u);
  QuaderBridgeMeshView mesh{};
  QuaderBridgeMeshUvInterpolationView sampling{};
  ASSERT_EQ(api_->mesh_view(scene_, 0u, &mesh, &error_), 1) << error_.message;
  const auto *extension = quader_bridge_get_uv_interpolation_api(
      QUADER_BRIDGE_UV_INTERPOLATION_ABI_VERSION);
  ASSERT_NE(extension, nullptr);
  ASSERT_EQ(extension->mesh_uv_interpolation_view(scene_, 0u, &sampling, &error_), 1)
      << error_.message;
  EXPECT_EQ(mesh.display_entity, 202u);
  EXPECT_EQ(mesh.geometry_entity, 100u);
  EXPECT_FLOAT_EQ(mesh.transform[0], -1.0f);
  EXPECT_FLOAT_EQ(mesh.transform[5], 2.0f);
  EXPECT_FLOAT_EQ(mesh.transform[12], 8.0f);
  expect_circular_bridge_samples(mesh, sampling, fixture.v_radius, true);
}

TEST_F(BridgeReaderTests, ParentingCompensationExportsCompleteAffineMeshAndLightTransforms) {
  auto document = make_document();
  auto &scene = document.scene();
  const auto parent = scene.create_entity_with_id(200u, "Mesh parent", EntityKind::Empty);
  const auto light = scene.create_entity_with_id(300u, "Compensated light", EntityKind::Light);
  ASSERT_EQ(parent, 200u);
  ASSERT_EQ(light, 300u);
  ASSERT_TRUE(scene.set_light(light, {.type = LightType::Directional}));
  TransformComponent parent_transform{.location = {5.0f, -2.0f, 7.0f},
      .rotation_euler = {0.2f, -0.6f, 0.4f}, .scale = {-2.0f, 3.0f, 0.5f}};
  parent_transform.parenting_offset.values[8] = 0.375f;
  TransformComponent child_transform{.location = {-1.0f, 2.0f, 3.0f},
      .rotation_euler = {0.1f, 0.7f, -0.2f}, .scale = {0.75f, -1.0f, 2.0f}};
  child_transform.parenting_offset.values[4] = -0.25f;
  child_transform.parenting_offset.values[12] = 4.0f;
  ASSERT_TRUE(scene.set_transform(parent, parent_transform));
  for (const EntityId entity : std::array<EntityId, 2>{100u, light}) {
    ASSERT_TRUE(scene.set_transform(entity, child_transform));
    ASSERT_TRUE(scene.set_parent(entity, parent, false));
  }
  const auto expected = scene_transform_multiply(
      scene_transform_multiply(parent_transform.parenting_offset,
                               compose_transform(parent_transform.spatial_transform())),
      scene_transform_multiply(child_transform.parenting_offset,
                               compose_transform(child_transform.spatial_transform())));
  add_scene(kFirstScene, "Scenes/compensated.qdr", document);
  open_project();
  open_scene();
  QuaderBridgeMeshView mesh_view{};
  ASSERT_EQ(api_->mesh_view(scene_, 0u, &mesh_view, &error_), 1) << error_.message;
  QuaderBridgeLightView light_view{};
  ASSERT_EQ(api_->light_view(scene_, 0u, &light_view, &error_), 1) << error_.message;
  for (std::size_t index = 0u; index < expected.values.size(); ++index) {
    EXPECT_NEAR(mesh_view.transform[index], expected.values[index], 0.00001f);
    EXPECT_NEAR(light_view.transform[index], expected.values[index], 0.00001f);
  }
  ASSERT_GT(mesh_view.vertex_count, 0u);
  // The affine placement travels in the ABI matrix, without baking mesh points.
  EXPECT_FLOAT_EQ(mesh_view.vertices[0].position[0], 0.0f);
  EXPECT_FLOAT_EQ(mesh_view.vertices[0].position[1], 0.0f);
  EXPECT_FLOAT_EQ(mesh_view.vertices[0].position[2], 0.0f);
}

TEST_F(BridgeReaderTests, FingerprintsIdentifyDecodedBytesAcrossSourceReplacement) {
  write_project_material();
  add_scene(kFirstScene, "Scenes/first.qdr", make_document(true), kProjectMaterial);
  const auto saved_scene = fingerprint_file(root_ / "Scenes" / "first.qdr");
  const auto saved_material = fingerprint_file(root_ / "Materials" / "surface.qmat");
  open_project();
  open_scene();
  QuaderBridgeSourceFingerprint scene_fingerprint{};
  ASSERT_EQ(api_->scene_source_fingerprint(scene_, &scene_fingerprint, &error_), 1)
      << error_.message;
  EXPECT_EQ(scene_fingerprint.byte_count, saved_scene.byte_count);
  EXPECT_EQ(scene_fingerprint.content_hash, saved_scene.content_hash);
  QuaderBridgeSceneView scene_view{};
  ASSERT_EQ(api_->scene_view(scene_, &scene_view, &error_), 1);
  EXPECT_STREQ(scene_view.relative_path, "Scenes/first.qdr");
  const auto project = material_index(QUADER_BRIDGE_MATERIAL_PROJECT);
  const auto embedded = material_index(QUADER_BRIDGE_MATERIAL_EMBEDDED);
  QuaderBridgeSourceFingerprint material_fingerprint{};
  ASSERT_EQ(api_->material_source_fingerprint(scene_, project,
      &material_fingerprint, &error_), 1) << error_.message;
  EXPECT_EQ(material_fingerprint.byte_count, saved_material.byte_count);
  EXPECT_EQ(material_fingerprint.content_hash, saved_material.content_hash);
  QuaderBridgeMaterialView material_view{};
  ASSERT_EQ(api_->material_view(scene_, project, &material_view, &error_), 1);
  EXPECT_STREQ(material_view.source_relative_path, "Materials/surface.qmat");

  // Exact source snapshots must not turn into whichever file is on disk later.
  write_bytes(root_ / "Scenes" / "first.qdr", "replaced scene bytes");
  write_bytes(root_ / "Materials" / "surface.qmat", "replaced material bytes");
  ASSERT_EQ(api_->scene_source_fingerprint(scene_, &scene_fingerprint, &error_), 1);
  EXPECT_EQ(scene_fingerprint.content_hash, saved_scene.content_hash);
  ASSERT_EQ(api_->material_source_fingerprint(scene_, project,
      &material_fingerprint, &error_), 1);
  EXPECT_EQ(material_fingerprint.content_hash, saved_material.content_hash);
  ASSERT_EQ(api_->material_source_fingerprint(scene_, embedded,
      &material_fingerprint, &error_), 1);
  EXPECT_EQ(material_fingerprint.byte_count, saved_scene.byte_count);
  EXPECT_EQ(material_fingerprint.content_hash, saved_scene.content_hash);
  ASSERT_EQ(api_->material_view(scene_, embedded, &material_view, &error_), 1);
  EXPECT_STREQ(material_view.source_relative_path, "Scenes/first.qdr");
  EXPECT_EQ(api_->scene_source_fingerprint(scene_, nullptr, &error_), 0);
  EXPECT_EQ(api_->material_source_fingerprint(scene_, UINT32_MAX,
      &material_fingerprint, &error_), 0);
  EXPECT_EQ(api_->material_source_fingerprint(scene_, project, nullptr, &error_), 0);
}

TEST_F(BridgeReaderTests, LegacyMaterialFingerprintsUseCatalogDecodedBytes) {
  write_project_material();
  add_scene(kFirstScene, "Scenes/first.qdr", make_document(true));
  const auto expected = fingerprint_file(root_ / "Materials" / "surface.qmat");
  open_project();
  open_scene();
  const auto index = material_index(QUADER_BRIDGE_MATERIAL_PROJECT);
  QuaderBridgeSourceFingerprint fingerprint{};
  ASSERT_EQ(api_->material_source_fingerprint(scene_, index, &fingerprint, &error_), 1)
      << error_.message;
  EXPECT_EQ(fingerprint.byte_count, expected.byte_count);
  EXPECT_EQ(fingerprint.content_hash, expected.content_hash);
  QuaderBridgeMaterialView material{};
  ASSERT_EQ(api_->material_view(scene_, index, &material, &error_), 1);
  EXPECT_STREQ(material.source_relative_path, "Materials/surface.qmat");
}

TEST_F(BridgeReaderTests, MovedMaterialUsesUuidAndReportsItsCurrentSourceFingerprint) {
  write_project_material();
  add_scene(kFirstScene, "Scenes/first.qdr", make_document(true), kProjectMaterial);
  const auto scene_fingerprint = fingerprint_file(root_ / "Scenes/first.qdr");
  const auto moved_path = root_ / "Materials/Dev/Wood/surface.qmat";
  ASSERT_TRUE(std::filesystem::create_directories(moved_path.parent_path()));
  std::filesystem::rename(root_ / "Materials/surface.qmat", moved_path);
  const auto expected = fingerprint_file(moved_path);

  open_project();
  open_scene();
  const auto index = material_index(QUADER_BRIDGE_MATERIAL_PROJECT);
  QuaderBridgeMaterialIdentity identity{};
  ASSERT_EQ(api_->material_identity(scene_, index, &identity, &error_), 1);
  EXPECT_STREQ(identity.id, kProjectMaterial);
  QuaderBridgeMaterialView material{};
  ASSERT_EQ(api_->material_view(scene_, index, &material, &error_), 1)
      << error_.message;
  EXPECT_STREQ(material.source_relative_path, "Materials/Dev/Wood/surface.qmat");
  QuaderBridgeSourceFingerprint actual{};
  ASSERT_EQ(api_->material_source_fingerprint(scene_, index, &actual, &error_), 1);
  EXPECT_EQ(actual.byte_count, expected.byte_count);
  EXPECT_EQ(actual.content_hash, expected.content_hash);
  EXPECT_EQ(fingerprint_file(root_ / "Scenes/first.qdr").content_hash,
            scene_fingerprint.content_hash);
}

TEST_F(BridgeReaderTests, TextureCatalogResolvesMovedImagesAndRetiredIds) {
  write_project_material();
  add_scene(kFirstScene, "Scenes/first.qdr", make_document(true), kProjectMaterial);
  const auto texture_id = *ProjectAssetId::parse(kTexture);
  const auto moved = relative_path("relocated.PNG");
  write_bytes(root_ / moved.native_path(), "encoded moved texture");
  ASSERT_TRUE(workspace_.manifest.register_texture({.id = texture_id, .path = moved}).ok());
  save_manifest();
  open_project();
  open_scene();
  const auto index = material_index(QUADER_BRIDGE_MATERIAL_PROJECT);
  QuaderBridgeMaterialView material{};
  ASSERT_EQ(api_->material_view(scene_, index, &material, &error_), 1) << error_.message;
  EXPECT_STREQ(material.textures[0].id, kTexture);
  EXPECT_STREQ(material.textures[0].source_path, "relocated.PNG");
  ASSERT_EQ(api_->read_texture(scene_, index, 0u, &blob_, &error_), 1) << error_.message;
  QuaderBridgeBlobView view{};
  ASSERT_EQ(api_->blob_view(blob_, &view, &error_), 1) << error_.message;
  EXPECT_EQ(view.byte_count, std::string_view("encoded moved texture").size());
  api_->release_blob(blob_);
  blob_ = nullptr;
  api_->release_scene(scene_);
  scene_ = nullptr;
  api_->release_project(project_);
  project_ = nullptr;
  const std::array retired{texture_id};
  ASSERT_TRUE(workspace_.manifest.mark_textures_deleted(retired).ok());
  save_manifest();
  open_project();
  open_scene();
  ASSERT_EQ(api_->material_view(scene_, material_index(QUADER_BRIDGE_MATERIAL_PROJECT),
                                &material, &error_), 1) << error_.message;
  EXPECT_EQ(material.textures[0].present, 0u);
}

TEST_F(BridgeReaderTests, ReusedDependencyPathResolvesOriginalUuidWithoutUsingNewOccupant) {
  write_project_material();
  add_scene(kFirstScene, "Scenes/first.qdr", make_document(true), kProjectMaterial);
  const auto path = relative_path("Materials/surface.qmat");
  auto material = QmatCodec::read(workspace_.file_system, path);
  ASSERT_TRUE(material.ok()) << material.error().message;
  ASSERT_TRUE(std::filesystem::create_directory(root_ / "Materials/Moved"));
  std::filesystem::rename(root_ / "Materials/surface.qmat",
                          root_ / "Materials/Moved/surface.qmat");
  const auto expected = fingerprint_file(root_ / "Materials/Moved/surface.qmat");
  material.value().material.id = *ProjectAssetId::parse(kMissingMaterial);
  material.value().material.name = "Different material at the old path";
  ASSERT_TRUE(QmatCodec::write_atomic(
      workspace_.file_system, path, material.value().material).ok());

  open_project();
  open_scene();
  const auto index = material_index(QUADER_BRIDGE_MATERIAL_PROJECT);
  QuaderBridgeMaterialView view{};
  ASSERT_EQ(api_->material_view(scene_, index, &view, &error_), 1) << error_.message;
  EXPECT_STREQ(view.name, "Project surface");
  EXPECT_STREQ(view.source_relative_path, "Materials/Moved/surface.qmat");
  QuaderBridgeSourceFingerprint actual{};
  ASSERT_EQ(api_->material_source_fingerprint(scene_, index, &actual, &error_), 1);
  EXPECT_EQ(actual.content_hash, expected.content_hash);
}

TEST_F(BridgeReaderTests, MalformedDependencyDoesNotFallBackToRelocatedUuid) {
  write_project_material();
  add_scene(kFirstScene, "Scenes/first.qdr", make_document(true), kProjectMaterial);
  ASSERT_TRUE(std::filesystem::create_directory(root_ / "Materials/Moved"));
  std::filesystem::rename(root_ / "Materials/surface.qmat",
                          root_ / "Materials/Moved/surface.qmat");
  write_bytes(root_ / "Materials/surface.qmat", "malformed material");
  open_project();
  open_scene();
  const auto index = material_index(QUADER_BRIDGE_MATERIAL_PROJECT);
  QuaderBridgeMaterialView material{};
  EXPECT_EQ(api_->material_view(scene_, index, &material, &error_), 0);
  EXPECT_NE(std::string(error_.message).find("surface.qmat"), std::string::npos);
}

TEST_F(BridgeReaderTests, UnsafeDependencyCasingDoesNotFallBackToCatalog) {
  write_project_material();
  add_scene(kFirstScene, "Scenes/first.qdr", make_document(true), kProjectMaterial,
            "Materials/SURFACE.qmat");
  open_project();
  open_scene();
  const auto index = material_index(QUADER_BRIDGE_MATERIAL_PROJECT);
  QuaderBridgeMaterialView material{};
  EXPECT_EQ(api_->material_view(scene_, index, &material, &error_), 0);
  EXPECT_NE(error_.message[0], '\0');
}

TEST_F(BridgeReaderTests, DeletedMaterialGroupsWithDefaultBeforeLazyDescriptorReads) {
  const ProjectAssetId deleted = *ProjectAssetId::parse(kProjectMaterial);
  const ProjectAssetId fallback = *ProjectAssetId::parse(kDefaultProjectMaterialId);
  auto document = make_document();
  MaterialComponent binding{.material = MaterialReference::project(deleted)};
  binding.material_slots.emplace(1u, MaterialReference::project(fallback));
  ASSERT_TRUE(document.scene().set_material(100u, std::move(binding)));
  add_scene(kFirstScene, "Scenes/first.qdr", document, kProjectMaterial);
  const std::array deleted_ids{deleted};
  const auto marked = workspace_.manifest.mark_materials_deleted(deleted_ids);
  ASSERT_TRUE(marked.ok()) << marked.error().message;
  save_manifest();

  // Neither QMAT exists yet: manifest tombstones alone determine effective
  // identity and surface grouping, so material reuse still needs no asset reads.
  open_project();
  open_scene();
  QuaderBridgeSceneView scene_view{};
  ASSERT_EQ(api_->scene_view(scene_, &scene_view, &error_), 1);
  ASSERT_EQ(scene_view.material_count, 1u);
  QuaderBridgeMeshView mesh{};
  ASSERT_EQ(api_->mesh_view(scene_, 0u, &mesh, &error_), 1);
  ASSERT_EQ(mesh.surface_count, 1u);
  EXPECT_EQ(mesh.surfaces[0].index_count, 6u);
  QuaderBridgeMaterialIdentity identity{};
  ASSERT_EQ(api_->material_identity(scene_, 0u, &identity, &error_), 1);
  EXPECT_EQ(std::string_view(identity.id), kDefaultProjectMaterialId);
  EXPECT_EQ(identity.kind, QUADER_BRIDGE_MATERIAL_PROJECT);

  ProjectMaterialDocument default_material;
  default_material.id = fallback;
  default_material.name = kDefaultProjectMaterialName;
  ASSERT_TRUE(std::filesystem::create_directory(root_ / "Materials"));
  ASSERT_TRUE(QmatCodec::write_atomic(workspace_.file_system,
      relative_path(kDefaultProjectMaterialPath), default_material).ok());
  const auto expected = fingerprint_file(root_ / kDefaultProjectMaterialPath);
  QuaderBridgeMaterialView material{};
  ASSERT_EQ(api_->material_view(scene_, 0u, &material, &error_), 1) << error_.message;
  EXPECT_EQ(std::string_view(material.name), kDefaultProjectMaterialName);
  EXPECT_EQ(std::string_view(material.source_relative_path), kDefaultProjectMaterialPath);
  QuaderBridgeSourceFingerprint actual{};
  ASSERT_EQ(api_->material_source_fingerprint(scene_, 0u, &actual, &error_), 1);
  EXPECT_EQ(actual.byte_count, expected.byte_count);
  EXPECT_EQ(actual.content_hash, expected.content_hash);
}

TEST_F(BridgeReaderTests, EmptyCatalogHasNoDefaultScene) {
  open_project();
  QuaderBridgeProjectView view{};
  ASSERT_EQ(api_->project_view(project_, &view, &error_), 1) << error_.message;
  EXPECT_EQ(view.scene_count, 0u);
  EXPECT_EQ(view.default_scene_index, QUADER_BRIDGE_NO_SCENE);
  EXPECT_EQ(std::string(view.name), root_.filename().string());
  QuaderBridgeSceneEntry entry{};
  EXPECT_EQ(api_->scene_entry(project_, 0u, &entry, &error_), 0);
  EXPECT_EQ(api_->open_scene(project_, kFirstScene, &scene_, &error_), 0);
  EXPECT_EQ(scene_, nullptr);
}

TEST_F(BridgeReaderTests, CatalogUsesSavedActiveSceneAndFallsBackToFirst) {
  const auto document = make_document();
  add_scene(kFirstScene, "Scenes/first.qdr", document);
  add_scene(kSecondScene, "Scenes/second.qdr", document);
  open_project();
  QuaderBridgeProjectView view{};
  ASSERT_EQ(api_->project_view(project_, &view, &error_), 1);
  EXPECT_EQ(view.scene_count, 2u);
  EXPECT_EQ(view.default_scene_index, 0u);
  QuaderBridgeSceneEntry entry{};
  ASSERT_EQ(api_->scene_entry(project_, 1u, &entry, &error_), 1);
  EXPECT_STREQ(entry.id, kSecondScene);
  EXPECT_STREQ(entry.relative_path, "Scenes/second.qdr");
  api_->release_project(std::exchange(project_, nullptr));
  const auto active = relative_path("Scenes/second.qdr");
  ASSERT_TRUE(workspace_.manifest.set_workspace({active}, active).ok());
  save_manifest();
  open_project();
  ASSERT_EQ(api_->project_view(project_, &view, &error_), 1);
  EXPECT_EQ(view.default_scene_index, 1u);
}

TEST_F(BridgeReaderTests, SceneViewUsesFixedExposureDespiteAuthoredCameraChanges) {
  auto document = make_lighting_document(std::array{16.0f}, 1.0f, false);
  SceneSettings settings = document.scene().scene_settings();
  settings.camera.aperture = 8.0f;
  settings.camera.shutter_speed = 0.02f;
  settings.camera.sensitivity = 400.0f;
  settings.camera.use_exposure = true;
  ASSERT_TRUE(document.scene().set_scene_settings(settings));
  add_scene(kFirstScene, "Scenes/exposed.qdr", document);
  settings.camera.aperture = 2.0f;
  settings.camera.shutter_speed = 0.5f;
  settings.camera.sensitivity = 1600.0f;
  settings.camera.use_exposure = false;
  ASSERT_TRUE(document.scene().set_scene_settings(settings));
  add_scene(kSecondScene, "Scenes/unexposed.qdr", document);
  open_project();
  std::array<QuaderBridgeLightView, 3> before{};
  for (const char *id : {kFirstScene, kSecondScene}) {
    open_scene(id);
    QuaderBridgeSceneView view{};
    ASSERT_EQ(api_->scene_view(scene_, &view, &error_), 1) << error_.message;
    EXPECT_FLOAT_EQ(view.camera_aperture, 16.0f);
    EXPECT_FLOAT_EQ(view.camera_shutter_seconds, 1.0f / 125.0f);
    EXPECT_FLOAT_EQ(view.camera_iso, 100.0f);
    ASSERT_EQ(view.light_count, 3u);
    for (std::uint32_t index = 0u; index < view.light_count; ++index) {
      QuaderBridgeLightView light{};
      ASSERT_EQ(api_->light_view(scene_, index, &light, &error_), 1) << error_.message;
      if (std::string_view(id) == kFirstScene) {
        before[index] = light;
      } else {
        EXPECT_FLOAT_EQ(light.intensity, before[index].intensity);
        EXPECT_FLOAT_EQ(light.preview_energy, before[index].preview_energy);
        EXPECT_FLOAT_EQ(light.illuminance_lux, before[index].illuminance_lux);
        EXPECT_FLOAT_EQ(light.luminous_intensity_candela,
                        before[index].luminous_intensity_candela);
      }
    }
    api_->release_scene(std::exchange(scene_, nullptr));
  }
}

TEST_F(BridgeReaderTests, LightViewsUseOneCalibrationForEveryShapeAndLegacyUnit) {
  SceneContentDocument document({.id = 8u, .name = "Light units"});
  const std::array<LightUnit, 3> units{LightUnit::Simple, LightUnit::Lumens, LightUnit::Candela};
  EntityId entity = 300u;
  for (const auto shape : kLightShapes) {
    for (const auto unit : units) {
      for (const float intensity : kCalibrationIntensities) {
        ASSERT_EQ(document.scene().create_entity_with_id(entity, "Legacy unit light", EntityKind::Light), entity);
        LightComponent light;
        light.type = shape;
        light.intensity = intensity;
        light.light_unit = unit;
        light.range = 12.0f;
        light.distance_attenuation = 2.0f;
        ASSERT_TRUE(document.scene().set_light(entity++, light));
      }
    }
  }
  add_scene(kFirstScene, "Scenes/units.qdr", document);
  open_project();
  open_scene();
  QuaderBridgeSceneView view{};
  ASSERT_EQ(api_->scene_view(scene_, &view, &error_), 1) << error_.message;
  ASSERT_EQ(view.light_count, 27u);
  constexpr std::array<float, 3> kExpectedEnergy{0.0625f, 1.0f, 2.0f};
  constexpr std::array<float, 3> kExpectedPhysical{7539.8223686f, 120637.1579f, 241274.3158f};
  for (std::uint32_t index = 0u; index < view.light_count; ++index) {
    QuaderBridgeLightView light{};
    ASSERT_EQ(api_->light_view(scene_, index, &light, &error_), 1) << error_.message;
    ASSERT_GE(light.entity, 300u);
    const auto offset = static_cast<std::size_t>(light.entity - 300u);
    const auto shape = offset / 9u;
    const auto intensity = offset % 3u;
    EXPECT_EQ(light.type, shape);
    EXPECT_FLOAT_EQ(light.intensity, kCalibrationIntensities[intensity]);
    EXPECT_FLOAT_EQ(light.preview_energy, kExpectedEnergy[intensity]);
    EXPECT_FLOAT_EQ(light.illuminance_lux, shape == 0u ? kExpectedPhysical[intensity] : 0.0f);
    EXPECT_FLOAT_EQ(light.luminous_intensity_candela,
                    shape == 0u ? 0.0f : kExpectedPhysical[intensity]);
    EXPECT_FLOAT_EQ(light.range, 12.0f);
    EXPECT_FLOAT_EQ(light.distance_attenuation, 2.0f);
  }
  QuaderBridgeLightView invalid{};
  EXPECT_EQ(api_->light_view(scene_, view.light_count, &invalid, &error_), 0);
}

TEST_F(BridgeReaderTests, ResolvedLightValuesSaturateWithoutChangingAuthoredIntensity) {
  SceneContentDocument document({.id = 8u, .name = "Large light"});
  EntityId entity = 300u;
  for (const auto shape : kLightShapes) {
    ASSERT_EQ(document.scene().create_entity_with_id(entity, "Large light", EntityKind::Light), entity);
    LightComponent light;
    light.type = shape;
    light.intensity = std::numeric_limits<float>::max();
    ASSERT_TRUE(document.scene().set_light(entity++, light));
  }
  add_scene(kFirstScene, "Scenes/large-lights.qdr", document);
  open_project();
  open_scene();
  for (std::uint32_t index = 0u; index < 3u; ++index) {
    QuaderBridgeLightView light{};
    ASSERT_EQ(api_->light_view(scene_, index, &light, &error_), 1) << error_.message;
    EXPECT_EQ(light.intensity, std::numeric_limits<float>::max());
    EXPECT_FLOAT_EQ(light.preview_energy, std::numeric_limits<float>::max() / 16.0f);
    const float physical = light.type == QUADER_BRIDGE_LIGHT_DIRECTIONAL
        ? light.illuminance_lux : light.luminous_intensity_candela;
    EXPECT_EQ(physical, std::numeric_limits<float>::max());
  }
}

TEST_F(BridgeReaderTests, LightSnapshotsRemainDetachedFromLaterSourceEdits) {
  auto document = make_lighting_document(std::array{16.0f}, 1.0f, false);
  add_scene(kFirstScene, "Scenes/lights.qdr", document);
  open_project();
  open_scene();
  QuaderBridgeLightView initial{};
  ASSERT_EQ(api_->light_view(scene_, 0u, &initial, &error_), 1) << error_.message;
  auto light = *document.scene().light(initial.entity);
  light.intensity = 32.0f;
  ASSERT_TRUE(document.scene().set_light(initial.entity, light));
  auto settings = document.scene().scene_settings();
  settings.camera.use_exposure = false;
  settings.camera.aperture = 2.0f;
  ASSERT_TRUE(document.scene().set_scene_settings(settings));
  const auto snapshot = qdr_project_write_snapshot_from_document(document);
  ASSERT_TRUE(snapshot.ok()) << snapshot.error().message;
  const auto written = QdrDocumentWriter::write_project_document_atomic(
      root_ / "Scenes/lights.qdr", snapshot.value(), scene_id(kFirstScene).bytes());
  ASSERT_TRUE(written.ok) << written.message;
  api_->release_project(std::exchange(project_, nullptr));
  QuaderBridgeLightView retained{};
  ASSERT_EQ(api_->light_view(scene_, 0u, &retained, &error_), 1) << error_.message;
  EXPECT_STREQ(retained.name, "Directional16");
  EXPECT_FLOAT_EQ(retained.intensity, 16.0f);
  EXPECT_FLOAT_EQ(retained.preview_energy, 1.0f);
  EXPECT_FLOAT_EQ(retained.illuminance_lux, initial.illuminance_lux);
  EXPECT_EQ(retained.name, initial.name);
  QuaderBridgeSceneView retained_scene{};
  ASSERT_EQ(api_->scene_view(scene_, &retained_scene, &error_), 1);
  EXPECT_FLOAT_EQ(retained_scene.camera_aperture, 16.0f);
  api_->release_scene(std::exchange(scene_, nullptr));
  open_project();
  open_scene();
  ASSERT_EQ(api_->light_view(scene_, 0u, &retained, &error_), 1) << error_.message;
  EXPECT_FLOAT_EQ(retained.intensity, 32.0f);
  EXPECT_FLOAT_EQ(retained.preview_energy, 2.0f);
}

TEST_F(BridgeReaderTests, SceneStorageSurvivesProjectRelease) {
  add_scene(kFirstScene, "Scenes/first.qdr", make_document());
  open_project();
  open_scene();
  api_->release_project(std::exchange(project_, nullptr));
  QuaderBridgeSceneView scene_view{};
  ASSERT_EQ(api_->scene_view(scene_, &scene_view, &error_), 1);
  EXPECT_STREQ(scene_view.id, kFirstScene);
  EXPECT_EQ(scene_view.mesh_count, 1u);
  EXPECT_EQ(scene_view.material_count, 1u);
  EXPECT_FLOAT_EQ(scene_view.camera_aperture, 16.0f);
  EXPECT_FLOAT_EQ(scene_view.camera_shutter_seconds, 0.008f);
  EXPECT_FLOAT_EQ(scene_view.camera_iso, 100.0f);
  QuaderBridgeMeshView mesh{};
  ASSERT_EQ(api_->mesh_view(scene_, 0u, &mesh, &error_), 1) << error_.message;
  EXPECT_STREQ(mesh.name, "Fixture mesh");
  EXPECT_EQ(mesh.display_entity, 100u);
  EXPECT_EQ(mesh.geometry_entity, 100u);
  EXPECT_EQ(mesh.visible, 1u);
  EXPECT_EQ(mesh.index_count, 6u);
  ASSERT_GE(mesh.vertex_count, 4u);
  ASSERT_NE(mesh.vertices, nullptr);
  ASSERT_NE(mesh.indices, nullptr);
  EXPECT_EQ(mesh.collision_status, QUADER_BRIDGE_COLLISION_TRIMESH_STATIC);
  ASSERT_EQ(mesh.collision_point_count, 6u);
  ASSERT_NE(mesh.collision_points, nullptr);
  const auto *retained_collision = mesh.collision_points;
  for (std::uint32_t index = 0u; index < mesh.index_count; ++index) {
    EXPECT_LT(mesh.indices[index], mesh.vertex_count);
  }
  EXPECT_FLOAT_EQ(mesh.transform[0], 1.0f);
  EXPECT_FLOAT_EQ(mesh.transform[5], 1.0f);
  EXPECT_FLOAT_EQ(mesh.transform[10], 1.0f);
  EXPECT_FLOAT_EQ(mesh.transform[15], 1.0f);
  QuaderBridgeMaterialView material{};
  ASSERT_EQ(api_->material_view(scene_, 0u, &material, &error_), 1);
  EXPECT_STREQ(material.name, "Embedded blue");
  EXPECT_FLOAT_EQ(material.base_color[2], 0.8f);
  EXPECT_FLOAT_EQ(material.roughness, 0.35f);
  EXPECT_FLOAT_EQ(material.metallic, 0.6f);
  EXPECT_FLOAT_EQ(retained_collision[0].position[0], 0.0f);
  EXPECT_FLOAT_EQ(retained_collision[1].position[0], 1.0f);
  EXPECT_FLOAT_EQ(retained_collision[2].position[1], 1.0f);
  QuaderBridgeMeshView reread{};
  ASSERT_EQ(api_->mesh_view(scene_, 0u, &reread, &error_), 1);
  EXPECT_EQ(reread.collision_points, retained_collision);
  EXPECT_EQ(api_->mesh_view(scene_, 1u, &mesh, &error_), 0);
  EXPECT_EQ(api_->material_identity(scene_, 1u, nullptr, &error_), 0);
}

TEST_F(BridgeReaderTests, CollisionEligibilityPreservesSavedUnsupportedMetadata) {
  auto document = make_document();
  auto &source = document.scene();
  const std::array<MeshComponent, 5> components = {{
      {.mesh = 10u, .collision_type = MeshCollisionType::Mesh},
      {.mesh = 10u, .generate_collisions = false,
       .collision_type = MeshCollisionType::Mesh, .display_collision = true},
      {.mesh = 10u, .collision_type = MeshCollisionType::Box},
      {.mesh = 10u, .collision_type = MeshCollisionType::Mesh,
       .body_type = MeshPhysicsBodyType::RigidBody},
      {.mesh = 10u, .generate_collisions = false,
       .collision_type = MeshCollisionType::ConvexHull},
  }};
  for (std::size_t index = 0u; index < components.size(); ++index) {
    const EntityId entity = 100u + index;
    if (index != 0u) {
      ASSERT_EQ(source.create_entity_with_id(entity, "Collision metadata",
                                             EntityKind::Mesh), entity);
      ASSERT_TRUE(source.set_material(entity, *source.material(100u)));
    }
    ASSERT_TRUE(source.set_mesh(entity, components[index]));
    ASSERT_TRUE(source.set_visible(entity, false));
  }
  add_scene(kFirstScene, "Scenes/first.qdr", document);
  open_project();
  open_scene();
  QuaderBridgeSceneView view{};
  ASSERT_EQ(api_->scene_view(scene_, &view, &error_), 1);
  ASSERT_EQ(view.mesh_count, components.size());
  for (std::uint32_t index = 0u; index < view.mesh_count; ++index) {
    QuaderBridgeMeshView mesh{};
    ASSERT_EQ(api_->mesh_view(scene_, index, &mesh, &error_), 1) << error_.message;
    ASSERT_GE(mesh.display_entity, 100u);
    ASSERT_LT(mesh.display_entity, 105u);
    const auto &metadata = components[mesh.display_entity - 100u];
    const auto expected = !metadata.generate_collisions
        ? QUADER_BRIDGE_COLLISION_DISABLED
        : mesh.display_entity == 100u ? QUADER_BRIDGE_COLLISION_TRIMESH_STATIC
                                     : QUADER_BRIDGE_COLLISION_UNSUPPORTED;
    EXPECT_EQ(mesh.collision_status, expected);
    EXPECT_EQ(mesh.visible, 0u);
    EXPECT_EQ(mesh.index_count, 6u);
    EXPECT_EQ(mesh.collision_point_count,
              expected == QUADER_BRIDGE_COLLISION_TRIMESH_STATIC ? 6u : 0u);
    EXPECT_EQ(mesh.collision_points == nullptr,
              expected != QUADER_BRIDGE_COLLISION_TRIMESH_STATIC);
  }
}

TEST_F(BridgeReaderTests, CollisionUsesRenderedPrimitiveSpansAndSnappedLocalFaces) {
  SceneContentDocument document({.id = 7u, .name = "Collision primitive spans"});
  MeshAsset asset;
  auto &geometry = asset.edit_geometry();
  geometry.vertices = {
      {.position = {0.000149f, 0, 0}, .normal = {0, 0, 1}},
      {.position = {1, 0, 0}, .normal = {0, 0, 1}},
      {.position = {0, 1, 0}, .normal = {0, 0, 1}},
      {.position = {2, 2, 0}, .normal = {0, 0, 1}},
  };
  // Native asset storage requires a complete triangle index buffer. Its unused
  // prefix/suffix reference a fourth vertex, while the rendered overlapping
  // spans both select only vertices 0, 1 and 2 at an unaligned offset.
  geometry.indices = {3u, 0u, 1u, 2u, 1u, 3u};
  geometry.primitives = {{.index_offset = 1u, .index_count = 3u},
                         {.index_offset = 1u, .index_count = 3u}};
  ASSERT_EQ(document.assets().add_mesh(10u, std::move(asset)), 10u);
  ASSERT_EQ(document.scene().create_entity_with_id(100u, "Span mesh",
                                                  EntityKind::Mesh), 100u);
  ASSERT_TRUE(document.scene().set_mesh(100u,
      MeshComponent{.mesh = 10u, .collision_type = MeshCollisionType::Mesh}));
  add_scene(kFirstScene, "Scenes/first.qdr", document);
  open_project();
  open_scene();
  QuaderBridgeMeshView mesh{};
  ASSERT_EQ(api_->mesh_view(scene_, 0u, &mesh, &error_), 1) << error_.message;
  EXPECT_EQ(mesh.collision_status, QUADER_BRIDGE_COLLISION_TRIMESH_STATIC);
  ASSERT_EQ(mesh.collision_point_count, 6u);
  EXPECT_EQ(mesh.index_count, 6u);
  for (std::uint32_t first : {0u, 3u}) {
    EXPECT_FLOAT_EQ(mesh.collision_points[first].position[0], 0.0001f);
    EXPECT_FLOAT_EQ(mesh.collision_points[first + 1u].position[0], 1.0f);
    EXPECT_FLOAT_EQ(mesh.collision_points[first + 2u].position[1], 1.0f);
  }
}

TEST_F(BridgeReaderTests, EmptySupportedMeshHasNoCollisionFaces) {
  SceneContentDocument document({.id = 7u, .name = "Empty collision"});
  ASSERT_EQ(document.assets().add_mesh(10u, MeshAsset{}), 10u);
  ASSERT_EQ(document.scene().create_entity_with_id(100u, "Empty mesh",
                                                  EntityKind::Mesh), 100u);
  ASSERT_TRUE(document.scene().set_mesh(100u,
      MeshComponent{.mesh = 10u, .collision_type = MeshCollisionType::Mesh}));
  add_scene(kFirstScene, "Scenes/first.qdr", document);
  open_project();
  open_scene();
  QuaderBridgeMeshView mesh{};
  ASSERT_EQ(api_->mesh_view(scene_, 0u, &mesh, &error_), 1) << error_.message;
  EXPECT_EQ(mesh.collision_status, QUADER_BRIDGE_COLLISION_TRIMESH_STATIC);
  EXPECT_EQ(mesh.collision_point_count, 0u);
  EXPECT_EQ(mesh.index_count, 0u);
}

TEST_F(BridgeReaderTests, MeshSurfacesExposeIdentitiesBeforeReadingMissingQmat) {
  add_scene(kFirstScene, "Scenes/first.qdr", make_document(true),
           kProjectMaterial);
  open_project();
  open_scene();
  QuaderBridgeMeshView mesh{};
  ASSERT_EQ(api_->mesh_view(scene_, 0u, &mesh, &error_), 1) << error_.message;
  ASSERT_EQ(mesh.surface_count, 2u);
  EXPECT_NE(mesh.surfaces[0].material_index, mesh.surfaces[1].material_index);
  EXPECT_EQ(mesh.surfaces[0].index_count, 3u);
  EXPECT_EQ(mesh.surfaces[1].index_count, 3u);
  const auto embedded = material_index(QUADER_BRIDGE_MATERIAL_EMBEDDED);
  const auto project = material_index(QUADER_BRIDGE_MATERIAL_PROJECT);
  QuaderBridgeMaterialIdentity identity{};
  ASSERT_EQ(api_->material_identity(scene_, embedded, &identity, &error_), 1);
  EXPECT_STREQ(identity.id, "20");
  ASSERT_EQ(api_->material_identity(scene_, project, &identity, &error_), 1);
  EXPECT_STREQ(identity.id, kProjectMaterial);
  QuaderBridgeMaterialView material{};
  EXPECT_EQ(api_->material_view(scene_, project, &material, &error_), 0);
  EXPECT_NE(std::string(error_.message).find("surface.qmat"), std::string::npos)
      << error_.message;
  EXPECT_EQ(api_->read_texture(scene_, project, QUADER_BRIDGE_TEXTURE_ALBEDO,
                              &blob_, &error_), 0);
  EXPECT_EQ(blob_, nullptr);
  ASSERT_EQ(api_->material_identity(scene_, project, &identity, &error_), 1);
  EXPECT_STREQ(identity.id, kProjectMaterial);
}

TEST_F(BridgeReaderTests, MaterialMetadataDoesNotRequireTextureAndBlobOwnsBytes) {
  write_project_material();
  add_scene(kFirstScene, "Scenes/first.qdr", make_document(true),
           kProjectMaterial);
  open_project();
  open_scene();
  const auto index = material_index(QUADER_BRIDGE_MATERIAL_PROJECT);
  QuaderBridgeMaterialView material{};
  ASSERT_EQ(api_->material_view(scene_, index, &material, &error_), 1)
      << error_.message;
  EXPECT_FLOAT_EQ(material.roughness, 0.45f);
  EXPECT_FLOAT_EQ(material.metallic, 0.7f);
  EXPECT_FLOAT_EQ(material.normal_strength, 0.8f);
  EXPECT_EQ(material.normal_directx, 1u);
  EXPECT_EQ(material.diffuse_model, QUADER_BRIDGE_MATERIAL_DIFFUSE_BURLEY);
  EXPECT_FLOAT_EQ(material.uv_center[0], 0.25f);
  EXPECT_FLOAT_EQ(material.uv_scale[1], -1.0f);
  EXPECT_FLOAT_EQ(material.uv_offset[1], -0.3f);
  EXPECT_FLOAT_EQ(material.uv_rotation_degrees, 35.0f);
  EXPECT_EQ(material.textures[QUADER_BRIDGE_TEXTURE_ALBEDO].present, 1u);
  EXPECT_EQ(material.textures[QUADER_BRIDGE_TEXTURE_ALBEDO].srgb, 1u);
  EXPECT_STREQ(material.textures[QUADER_BRIDGE_TEXTURE_ALBEDO].id, kTexture);
  EXPECT_EQ(api_->read_texture(scene_, index, QUADER_BRIDGE_TEXTURE_ALBEDO,
                              &blob_, &error_), 0);
  EXPECT_NE(std::string(error_.message).find("albedo.PNG"), std::string::npos)
      << error_.message;
  const std::string bytes("\x89PNG\r\n\x1a\n\0fixture", 16u);
  write_bytes(root_ / "Textures" / "albedo.PNG", bytes);
  ASSERT_EQ(api_->read_texture(scene_, index, QUADER_BRIDGE_TEXTURE_ALBEDO,
                              &blob_, &error_), 1) << error_.message;
  api_->release_scene(std::exchange(scene_, nullptr));
  api_->release_project(std::exchange(project_, nullptr));
  QuaderBridgeBlobView blob{};
  ASSERT_EQ(api_->blob_view(blob_, &blob, &error_), 1);
  EXPECT_STREQ(blob.extension, ".png");
  EXPECT_EQ(blob.byte_count, bytes.size());
  ASSERT_NE(blob.bytes, nullptr);
  EXPECT_EQ(std::string(reinterpret_cast<const char *>(blob.bytes),
                        static_cast<std::size_t>(blob.byte_count)), bytes);
}

TEST_F(BridgeReaderTests, DiffuseModelsPreserveProjectAndEmbeddedChoicesWithoutTextures) {
  write_project_material();
  write_project_material(kMissingMaterial);
  const auto material_path = relative_path("Materials/surface.qmat");
  auto decoded = QmatCodec::read(workspace_.file_system, material_path);
  ASSERT_TRUE(decoded.ok()) << decoded.error().message;
  decoded.value().material.standard_material()->diffuse_model =
      MaterialDiffuseModel::Lambert;
  ASSERT_TRUE(QmatCodec::write_atomic(workspace_.file_system, material_path,
                                     decoded.value().material).ok());
  add_scene(kFirstScene, "Scenes/first.qdr", make_document(true), kProjectMaterial);
  auto second = make_document(true, kMissingMaterial);
  ASSERT_TRUE(second.assets().update_material(20u, [](MaterialDocument &material) {
    material.diffuse_model = MaterialDiffuseModel::Lambert;
  }));
  add_scene(kSecondScene, "Scenes/second.qdr", second, kMissingMaterial);
  ASSERT_FALSE(std::filesystem::exists(root_ / "Textures/albedo.PNG"));
  open_project();
  open_scene();
  QuaderBridgeMaterialView material{};
  ASSERT_EQ(api_->material_view(scene_, material_index(QUADER_BRIDGE_MATERIAL_PROJECT),
                                &material, &error_), 1) << error_.message;
  EXPECT_EQ(material.diffuse_model, QUADER_BRIDGE_MATERIAL_DIFFUSE_LAMBERT);
  ASSERT_EQ(api_->material_view(scene_, material_index(QUADER_BRIDGE_MATERIAL_EMBEDDED),
                                &material, &error_), 1) << error_.message;
  EXPECT_EQ(material.diffuse_model, QUADER_BRIDGE_MATERIAL_DIFFUSE_BURLEY);
  api_->release_scene(std::exchange(scene_, nullptr));
  open_scene(kSecondScene);
  ASSERT_EQ(api_->material_view(scene_, material_index(QUADER_BRIDGE_MATERIAL_PROJECT),
                                &material, &error_), 1) << error_.message;
  EXPECT_EQ(material.diffuse_model, QUADER_BRIDGE_MATERIAL_DIFFUSE_BURLEY);
  ASSERT_EQ(api_->material_view(scene_, material_index(QUADER_BRIDGE_MATERIAL_EMBEDDED),
                                &material, &error_), 1) << error_.message;
  EXPECT_EQ(material.diffuse_model, QUADER_BRIDGE_MATERIAL_DIFFUSE_LAMBERT);
  EXPECT_FALSE(std::filesystem::exists(root_ / "Textures/albedo.PNG"));
}

TEST_F(BridgeReaderTests, DiffuseModelSnapshotsTrackTheDecodedMaterialFingerprint) {
  write_project_material();
  add_scene(kFirstScene, "Scenes/first.qdr", make_document(true), kProjectMaterial);
  open_project();
  open_scene();
  const auto index = material_index(QUADER_BRIDGE_MATERIAL_PROJECT);
  QuaderBridgeMaterialView material{};
  ASSERT_EQ(api_->material_view(scene_, index, &material, &error_), 1);
  ASSERT_EQ(material.diffuse_model, QUADER_BRIDGE_MATERIAL_DIFFUSE_BURLEY);
  QuaderBridgeSourceFingerprint before{};
  ASSERT_EQ(api_->material_source_fingerprint(scene_, index, &before, &error_), 1);
  const auto path = relative_path("Materials/surface.qmat");
  auto decoded = QmatCodec::read(workspace_.file_system, path);
  ASSERT_TRUE(decoded.ok()) << decoded.error().message;
  decoded.value().material.standard_material()->diffuse_model =
      MaterialDiffuseModel::Lambert;
  ASSERT_TRUE(QmatCodec::write_atomic(workspace_.file_system, path,
                                     decoded.value().material).ok());
  ASSERT_EQ(api_->material_view(scene_, index, &material, &error_), 1);
  EXPECT_EQ(material.diffuse_model, QUADER_BRIDGE_MATERIAL_DIFFUSE_BURLEY);
  QuaderBridgeSourceFingerprint retained{};
  ASSERT_EQ(api_->material_source_fingerprint(scene_, index, &retained, &error_), 1);
  EXPECT_EQ(retained.content_hash, before.content_hash);
  EXPECT_EQ(retained.byte_count, before.byte_count);
  api_->release_scene(std::exchange(scene_, nullptr));
  open_scene();
  ASSERT_EQ(api_->material_view(scene_, index, &material, &error_), 1);
  EXPECT_EQ(material.diffuse_model, QUADER_BRIDGE_MATERIAL_DIFFUSE_LAMBERT);
  QuaderBridgeSourceFingerprint updated{};
  ASSERT_EQ(api_->material_source_fingerprint(scene_, index, &updated, &error_), 1);
  EXPECT_NE(updated.content_hash, before.content_hash);
}

TEST_F(BridgeReaderTests, MissingDiffuseChoiceDefaultsToBurleyAndMalformedChoicesFail) {
  write_project_material();
  const auto path = root_ / "Materials/surface.qmat";
  nlohmann::json data;
  {
    std::ifstream stream(path);
    stream >> data;
  }
  data["payload"]["parameters"].erase("diffuseModel");
  write_bytes(path, data.dump());
  add_scene(kFirstScene, "Scenes/first.qdr", make_document(true), kProjectMaterial);
  open_project();
  open_scene();
  QuaderBridgeMaterialView material{};
  const auto index = material_index(QUADER_BRIDGE_MATERIAL_PROJECT);
  ASSERT_EQ(api_->material_view(scene_, index, &material, &error_), 1)
      << error_.message;
  EXPECT_EQ(material.diffuse_model, QUADER_BRIDGE_MATERIAL_DIFFUSE_BURLEY);
  api_->release_scene(std::exchange(scene_, nullptr));
  const std::array<nlohmann::json, 3> invalid_choices{
      nlohmann::json("unsupported"), nlohmann::json(2), nlohmann::json(nullptr)};
  for (const auto &choice : invalid_choices) {
    SCOPED_TRACE(choice.dump());
    data["payload"]["parameters"]["diffuseModel"] = choice;
    write_bytes(path, data.dump());
    open_scene();
    EXPECT_EQ(api_->material_view(scene_, index, &material, &error_), 0);
    EXPECT_NE(error_.message[0], '\0');
    api_->release_scene(std::exchange(scene_, nullptr));
  }
}

#if defined(_WIN32)
TEST_F(BridgeReaderTests, TextureReadsShareDeleteAccessAndReleaseSourceHandle) {
  write_project_material();
  add_scene(kFirstScene, "Scenes/first.qdr", make_document(true), kProjectMaterial);
  const auto texture_path = root_ / "Textures" / "albedo.PNG";
  write_bytes(texture_path, "original encoded texture");
  open_project();
  open_scene();
  const auto index = material_index(QUADER_BRIDGE_MATERIAL_PROJECT);
  const auto close_handle = [](void *handle) { ::CloseHandle(handle); };
  std::unique_ptr<void, decltype(close_handle)> delete_access(
      ::CreateFileW(texture_path.c_str(), DELETE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr),
      close_handle);
  ASSERT_NE(delete_access.get(), INVALID_HANDLE_VALUE);
  // Reciprocal sharing makes this fail deterministically if the reader does
  // not permit deletion, as required by concurrent atomic native saves.
  ASSERT_EQ(api_->read_texture(scene_, index, QUADER_BRIDGE_TEXTURE_ALBEDO,
                               &blob_, &error_), 1) << error_.message;
  delete_access.reset();
  const auto replacement = root_ / "Textures" / "replacement.PNG";
  write_bytes(replacement, "replacement encoded texture");
  ASSERT_TRUE(::MoveFileExW(replacement.c_str(), texture_path.c_str(),
                           MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH));
  QuaderBridgeBlobView view{};
  ASSERT_EQ(api_->blob_view(blob_, &view, &error_), 1) << error_.message;
  EXPECT_EQ(std::string(reinterpret_cast<const char *>(view.bytes),
                        static_cast<std::size_t>(view.byte_count)),
            "original encoded texture");
}
#endif

TEST_F(BridgeReaderTests, IncludesHiddenMeshesAndMirroredInstancePlacementsOnce) {
  auto document = make_document();
  auto &source = document.scene();
  ASSERT_TRUE(source.set_visible(100u, false));
  const auto definition = source.create_entity_with_id(
      200u, "Stored definition", EntityKind::Generic);
  const auto member = source.create_entity_with_id(
      201u, "Definition member", EntityKind::Mesh);
  ASSERT_TRUE(source.set_mesh(member, MeshComponent{.mesh = 10u}));
  ASSERT_TRUE(source.set_material(member, MaterialComponent{.material = 20u}));
  ASSERT_TRUE(source.set_parent(member, definition, false));
  ASSERT_TRUE(source.set_transform(member,
                                  TransformComponent{.location = {2, 0, 0}}));
  ASSERT_TRUE(source.set_instance_definition(definition));
  for (const EntityId id : {202u, 203u}) {
    ASSERT_EQ(source.create_entity_with_id(id, "Placed instance",
                                          EntityKind::Instance), id);
    ASSERT_TRUE(source.set_instance_occurrence(
        id, InstanceOccurrenceComponent{.definition = definition}));
    ASSERT_TRUE(source.set_transform(
        id, TransformComponent{.location = {10, 3, 4}, .scale = {-1, 2, 1}}));
  }
  ASSERT_TRUE(source.set_visible(203u, false));
  add_scene(kFirstScene, "Scenes/first.qdr", document);
  open_project();
  open_scene();
  QuaderBridgeSceneView scene_view{};
  ASSERT_EQ(api_->scene_view(scene_, &scene_view, &error_), 1);
  ASSERT_EQ(scene_view.mesh_count, 3u);
  std::array<bool, 3> found{};
  const QuaderBridgeCollisionPoint *shared_collision = nullptr;
  for (std::uint32_t index = 0u; index < scene_view.mesh_count; ++index) {
    QuaderBridgeMeshView mesh{};
    ASSERT_EQ(api_->mesh_view(scene_, index, &mesh, &error_), 1);
    EXPECT_EQ(mesh.collision_status, QUADER_BRIDGE_COLLISION_TRIMESH_STATIC);
    ASSERT_EQ(mesh.collision_point_count, 6u);
    ASSERT_NE(mesh.collision_points, nullptr);
    if (shared_collision == nullptr) {
      shared_collision = mesh.collision_points;
    }
    EXPECT_EQ(mesh.collision_points, shared_collision);
    EXPECT_FLOAT_EQ(mesh.collision_points[0].position[0], 0.0f);
    EXPECT_FLOAT_EQ(mesh.collision_points[1].position[0], 1.0f);
    if (mesh.display_entity == 100u) {
      found[0] = true;
      EXPECT_EQ(mesh.visible, 0u);
      continue;
    }
    ASSERT_TRUE(mesh.display_entity == 202u || mesh.display_entity == 203u);
    found[mesh.display_entity == 202u ? 1u : 2u] = true;
    EXPECT_EQ(mesh.geometry_entity, member);
    EXPECT_EQ(mesh.definition_entity, definition);
    EXPECT_EQ(mesh.visible, mesh.display_entity == 202u ? 1u : 0u);
    EXPECT_FLOAT_EQ(mesh.transform[0], -1.0f);
    EXPECT_FLOAT_EQ(mesh.transform[5], 2.0f);
    EXPECT_FLOAT_EQ(mesh.transform[12], 8.0f);
    EXPECT_FLOAT_EQ(mesh.transform[13], 3.0f);
    EXPECT_FLOAT_EQ(mesh.transform[14], 4.0f);
  }
  EXPECT_EQ(found, (std::array<bool, 3>{true, true, true}));
}

TEST_F(BridgeReaderTests, ComposesMemberAndOccurrenceUvMappings) {
  auto document = make_document(true);
  auto &source = document.scene();
  ASSERT_EQ(source.create_entity_with_id(200u, "Definition",
                                         EntityKind::Generic), 200u);
  ASSERT_TRUE(source.set_parent(100u, 200u, false));
  ASSERT_TRUE(source.set_instance_definition(200u));
  ASSERT_EQ(source.create_entity_with_id(202u, "Mapped instance",
                                         EntityKind::Instance), 202u);
  ASSERT_TRUE(source.set_instance_occurrence(
      202u, InstanceOccurrenceComponent{.definition = 200u}));
  TextureMappingComponent member_mapping;
  member_mapping.sampling_from_local.values[12] = 1.0f;
  TextureMappingComponent occurrence_mapping;
  occurrence_mapping.sampling_from_local.values[12] = 2.0f;
  ASSERT_TRUE(source.set_texture_mapping(100u, member_mapping));
  ASSERT_TRUE(source.set_texture_mapping(202u, occurrence_mapping));
  add_scene(kFirstScene, "Scenes/first.qdr", document, kProjectMaterial);
  open_project();
  open_scene();
  QuaderBridgeMeshView mesh{};
  ASSERT_EQ(api_->mesh_view(scene_, 0u, &mesh, &error_), 1) << error_.message;
  ASSERT_EQ(mesh.surface_count, 2u);
  ASSERT_EQ(mesh.collision_point_count, 6u);
  const std::array<std::array<float, 3>, 6> expected_collision = {{
      {0, 0, 0}, {1, 0, 0}, {0, 1, 0},
      {1, 0, 0}, {1, 1, 0}, {0, 1, 0},
  }};
  for (std::size_t corner = 0u; corner < expected_collision.size(); ++corner) {
    for (std::size_t axis = 0u; axis < 3u; ++axis) {
      EXPECT_FLOAT_EQ(mesh.collision_points[corner].position[axis],
                      expected_collision[corner][axis]);
    }
  }
  const auto embedded = material_index(QUADER_BRIDGE_MATERIAL_EMBEDDED);
  const QuaderBridgeSurface *triangle = nullptr;
  for (std::uint32_t index = 0u; index < mesh.surface_count; ++index) {
    if (mesh.surfaces[index].material_index == embedded) {
      triangle = &mesh.surfaces[index];
    }
  }
  ASSERT_NE(triangle, nullptr);
  ASSERT_EQ(triangle->index_count, 3u);
  for (std::uint32_t corner = 0u; corner < triangle->index_count; ++corner) {
    const auto &vertex = mesh.vertices[mesh.indices[triangle->index_offset + corner]];
    EXPECT_NEAR(vertex.uv[0], vertex.position[0] + 3.0f, 1e-5f);
    EXPECT_NEAR(vertex.uv[1], vertex.position[1], 1e-5f);
    EXPECT_NEAR(vertex.normal[2], 1.0f, 1e-5f);
  }
  const auto &a = mesh.vertices[mesh.indices[triangle->index_offset]];
  const auto &b = mesh.vertices[mesh.indices[triangle->index_offset + 1u]];
  const auto &c = mesh.vertices[mesh.indices[triangle->index_offset + 2u]];
  EXPECT_GT((b.position[0] - a.position[0]) * (c.position[1] - a.position[1]) -
                (b.position[1] - a.position[1]) * (c.position[0] - a.position[0]),
            0.0f);
}

TEST_F(BridgeReaderTests, RejectsSceneIdentityMismatchAndMalformedScene) {
  add_scene(kFirstScene, "Scenes/first.qdr", make_document());
  open_project();
  const auto replaced = QdrDocumentWriter::write_project_document_atomic(
      root_ / "Scenes" / "first.qdr", make_document(),
      scene_id(kSecondScene).bytes());
  ASSERT_TRUE(replaced.ok) << replaced.message;
  EXPECT_EQ(api_->open_scene(project_, kFirstScene, &scene_, &error_), 0);
  EXPECT_EQ(scene_, nullptr);
  EXPECT_NE(error_.message[0], '\0');
  write_bytes(root_ / "Scenes" / "first.qdr", "not a binary QDR document");
  EXPECT_EQ(api_->open_scene(project_, kFirstScene, &scene_, &error_), 0);
  EXPECT_EQ(scene_, nullptr);
  EXPECT_NE(error_.message[0], '\0');
}

TEST_F(BridgeReaderTests, RevalidatesManifestBeforeOpeningScene) {
  add_scene(kFirstScene, "Scenes/first.qdr", make_document());
  open_project();
  auto replacement = ProjectManifest::create(ProjectId::generate(),
                                             root_.filename().string());
  ASSERT_TRUE(replacement.ok());
  workspace_.manifest = replacement.take_value();
  ASSERT_TRUE(workspace_.manifest.register_scene(
      {.id = scene_id(kFirstScene),
       .path = relative_path("Scenes/first.qdr")}).ok());
  save_manifest();
  EXPECT_EQ(api_->open_scene(project_, kFirstScene, &scene_, &error_), 0);
  EXPECT_EQ(scene_, nullptr);
  write_bytes(workspace_.manifest_path, "{ invalid manifest");
  EXPECT_EQ(api_->open_scene(project_, kFirstScene, &scene_, &error_), 0);
  api_->release_project(std::exchange(project_, nullptr));
  EXPECT_EQ(api_->open_project(utf8_path(workspace_.manifest_path).c_str(),
                              &project_, &error_), 0);
  EXPECT_EQ(project_, nullptr);
}

TEST_F(BridgeReaderTests, ExportGodotAcceptanceFixtures) {
  const char *configured_prefix = std::getenv("QUADER_BRIDGE_TEST_RESOURCE_PREFIX");
  const std::string resource_prefix = configured_prefix == nullptr ? "" : configured_prefix;
  ASSERT_LE(resource_prefix.size(), 64u);
  for (const char c : resource_prefix) {
    ASSERT_TRUE((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '_' || c == '-')
        << "Fixture resource prefix accepts only ASCII letters, digits, '_' and '-'.";
  }
  const std::string texture_path = "Textures/" + resource_prefix + "albedo.PNG";
  const std::string normal_texture_path = "Textures/" + resource_prefix + "normal.PNG";
  const std::string project_material_path = "Materials/" + resource_prefix + "surface.qmat";
  const std::string missing_material_path = "Materials/" + resource_prefix + "missing.qmat";
  const std::string embedded_material_name = resource_prefix + "Embedded blue";
  const std::array<std::string, 2> naming_material_names{
      resource_prefix + "Marmo Caf\xc3\xa9", resource_prefix + "Marmo Blu"};
  const std::array<std::string, 2> naming_material_paths{
      "Materials/" + resource_prefix + "naming-1.qmat",
      "Materials/" + resource_prefix + "naming-2.qmat"};
  const std::array<std::string, 2> naming_texture_filenames{
      naming_material_names[0] + ".PNG", naming_material_names[1] + ".PNG"};
  write_project_material(kProjectMaterial, texture_path, true, resource_prefix, true);
  write_project_material(kMissingMaterial, "Textures/" + resource_prefix + "missing.PNG",
                         false, resource_prefix);
  constexpr std::uint8_t kPng[] = {
      0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
      0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
      0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
      0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0x70, 0x68, 0x38, 0xf0,
      0x1f, 0x00, 0x05, 0x04, 0x02, 0x80, 0x3c, 0x9e, 0x03, 0x80, 0x00, 0x00,
      0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
  };
  write_bytes(root_ / std::filesystem::u8path(texture_path),
              {reinterpret_cast<const char *>(kPng), sizeof(kPng)});
  write_bytes(root_ / std::filesystem::u8path(normal_texture_path),
              {reinterpret_cast<const char *>(kPng), sizeof(kPng)});
  auto document = make_document(true, kProjectMaterial, embedded_material_name);
  auto &source = document.scene();
  ASSERT_TRUE(source.set_mesh(100u,
      MeshComponent{.mesh = 10u, .collision_type = MeshCollisionType::Mesh,
                    .display_collision = true}));
  ASSERT_EQ(source.create_entity_with_id(101u, "Hidden mesh", EntityKind::Mesh),
            101u);
  ASSERT_TRUE(source.set_mesh(101u, MeshComponent{.mesh = 10u}));
  ASSERT_TRUE(source.set_material(101u, *source.material(100u)));
  ASSERT_TRUE(source.set_visible(101u, false));
  ASSERT_TRUE(source.set_transform(101u,
      TransformComponent{.location = {3, 0, 0}, .scale = {1.5f, 2.0f, 0.75f}}));
  ASSERT_EQ(source.create_entity_with_id(200u, "Stored definition",
                                         EntityKind::Generic), 200u);
  ASSERT_EQ(source.create_entity_with_id(201u, "Definition member",
                                         EntityKind::Mesh), 201u);
  ASSERT_TRUE(source.set_mesh(201u, MeshComponent{.mesh = 10u}));
  ASSERT_TRUE(source.set_material(201u, *source.material(100u)));
  ASSERT_TRUE(source.set_parent(201u, 200u, false));
  ASSERT_TRUE(source.set_instance_definition(200u));
  ASSERT_EQ(source.create_entity_with_id(202u, "Mirrored instance",
                                         EntityKind::Instance), 202u);
  ASSERT_TRUE(source.set_instance_occurrence(
      202u, InstanceOccurrenceComponent{.definition = 200u}));
  ASSERT_TRUE(source.set_transform(
      202u, TransformComponent{.location = {6, 0, 0}, .scale = {-1, 2, 1}}));
  add_scene(kFirstScene, "Scenes/mixed_materials.qdr", document,
           kProjectMaterial, project_material_path);
  add_scene(kSecondScene, "Scenes/missing_texture.qdr",
           make_document(true, kMissingMaterial, resource_prefix + "Missing embedded blue"),
           kMissingMaterial, missing_material_path);
  {
    // Unique authored names, non-ASCII text, spaces and uppercase extensions
    // must survive the reader and the flat destination filenames.
    auto naming_document = make_document(false, kProjectMaterial, embedded_material_name);
    MaterialComponent binding{
        .material = MaterialReference::project(*ProjectAssetId::parse(kNamingMaterials[0]))};
    binding.material_slots.emplace(
        1u, MaterialReference::project(*ProjectAssetId::parse(kNamingMaterials[1])));
    ASSERT_TRUE(naming_document.scene().set_material(100u, std::move(binding)));
    auto naming_snapshot = qdr_project_write_snapshot_from_document(naming_document);
    ASSERT_TRUE(naming_snapshot.ok()) << naming_snapshot.error().message;
    for (std::size_t i = 0; i < kNamingMaterials.size(); ++i) {
      const std::string suffix = std::to_string(i + 1u);
      const std::string texture_folder = "Textures/naming-" + suffix;
      const std::string named_texture_path = texture_folder + "/" + naming_texture_filenames[i];
      std::filesystem::create_directories(root_ / std::filesystem::u8path(texture_folder));
      QImage image(1, 1, QImage::Format_RGBA8888);
      image.fill(i == 0u ? QColor(192, 32, 64, 255) : QColor(16, 160, 224, 255));
      ASSERT_TRUE(image.save(QString::fromStdWString(
          (root_ / std::filesystem::u8path(named_texture_path)).wstring()), "PNG"));
      ProjectMaterialDocument material;
      material.id = *ProjectAssetId::parse(kNamingMaterials[i]);
      material.name = naming_material_names[i];
      material.standard_material()->diffuse_model =
          i == 0u ? MaterialDiffuseModel::Lambert : MaterialDiffuseModel::Burley;
      material.standard_material()->base_color_texture = ProjectMaterialTextureSlot{
          .id = *ProjectAssetId::parse(kNamingTextures[i]),
          .project_relative_path = named_texture_path,
          .color_space = ProjectTextureColorSpace::Srgb};
      const auto material_path = relative_path(naming_material_paths[i]);
      const auto written = QmatCodec::write_atomic(workspace_.file_system, material_path, material);
      ASSERT_TRUE(written.ok()) << written.error().message;
      naming_snapshot.value().project_asset_dependencies.push_back({
          .stable_id = material.id,
          .kind = ProjectAssetKind::Material,
          .path = material_path});
    }
    const auto path = relative_path("Scenes/resource_names.qdr");
    const auto resolved = workspace_.file_system.resolve_for_write(path);
    ASSERT_TRUE(resolved.ok()) << resolved.error().message;
    const auto written = QdrDocumentWriter::write_project_document_atomic(
        resolved.value(), naming_snapshot.value(), scene_id(kNamingScene).bytes());
    ASSERT_TRUE(written.ok) << written.message;
    ASSERT_TRUE(workspace_.manifest.register_scene({.id = scene_id(kNamingScene), .path = path}).ok());
    save_manifest();
  }
  // A distinct embedded identity cannot claim an existing project material's
  // flat filename, even though the authored display names are equal.
  add_scene(kNamingCollisionScene, "Scenes/resource_name_collision.qdr",
            make_document(false, kProjectMaterial, naming_material_names[0]));
  {
    // One encoded source used both ordinarily and as a DirectX normal requires
    // incompatible import settings and must fail instead of inventing a name.
    auto material_file = QmatCodec::read(workspace_.file_system,
                                         relative_path(project_material_path));
    ASSERT_TRUE(material_file.ok()) << material_file.error().message;
    auto material = std::move(material_file.value().material);
    material.id = *ProjectAssetId::parse(kNamingNormalCollisionMaterial);
    material.name = resource_prefix + "Normal collision";
    auto &standard = *material.standard_material();
    standard.normal_texture = standard.roughness_texture;
    const std::string material_path = "Materials/" + resource_prefix + "normal-collision.qmat";
    const auto written = QmatCodec::write_atomic(workspace_.file_system,
                                                relative_path(material_path), material);
    ASSERT_TRUE(written.ok()) << written.error().message;
    auto collision_document = make_document(false, kProjectMaterial, embedded_material_name);
    ASSERT_TRUE(collision_document.scene().set_material(100u, MaterialComponent{
        .material = MaterialReference::project(material.id)}));
    add_scene(kNamingNormalCollisionScene, "Scenes/normal_name_collision.qdr",
              collision_document, kNamingNormalCollisionMaterial, material_path);
  }
  const auto lighting_document = make_lighting_document(std::array{16.0f}, 2.0f, false);
  add_scene(kLightingScene, "Scenes/lighting.qdr", lighting_document);
  auto calibration_document = make_lighting_document(kCalibrationIntensities, 1.0f, true);
  ASSERT_EQ(calibration_document.assets().add_mesh(10u, make_two_material_mesh()), 10u);
  for (const auto model : {MaterialDiffuseModel::Burley, MaterialDiffuseModel::Lambert}) {
    const bool lambert = model == MaterialDiffuseModel::Lambert;
    const AssetId material_id = lambert ? 21u : 20u;
    auto receiver_material = make_default_quader_material(
        lambert ? "CalibrationReceiverLambert" : "CalibrationReceiverBurley");
    receiver_material.base_color = {0.18f, 0.18f, 0.18f};
    receiver_material.roughness = 1.0f;
    receiver_material.metallic = 0.0f;
    receiver_material.diffuse_model = model;
    ASSERT_EQ(calibration_document.assets().add_material(
                  material_id, std::move(receiver_material)), material_id);
    const EntityId receiver_id = lambert ? 401u : 400u;
    const EntityId receiver = calibration_document.scene().create_entity_with_id(
        receiver_id, lambert ? "calibration-receiver-lambert" : "calibration-receiver-burley",
        EntityKind::Mesh);
    ASSERT_EQ(receiver, receiver_id);
    ASSERT_TRUE(calibration_document.scene().set_mesh(receiver, MeshComponent{.mesh = 10u}));
    ASSERT_TRUE(calibration_document.scene().set_material(
        receiver, MaterialComponent{.material = material_id}));
  }
  add_scene(kCalibrationScene, "Scenes/lighting_calibration.qdr", calibration_document);
  open_project();
  open_scene();
  QuaderBridgeSceneView view{};
  ASSERT_EQ(api_->scene_view(scene_, &view, &error_), 1) << error_.message;
  ASSERT_EQ(view.mesh_count, 3u);
  ASSERT_EQ(view.material_count, 2u);
  ASSERT_EQ(api_->read_texture(scene_, material_index(QUADER_BRIDGE_MATERIAL_PROJECT),
                              QUADER_BRIDGE_TEXTURE_ALBEDO, &blob_, &error_), 1)
      << error_.message;

  const char *configured = std::getenv("QUADER_BRIDGE_TEST_FIXTURE_DIR");
  if (configured == nullptr || *configured == '\0') {
    return;
  }
  const auto destination = std::filesystem::absolute(
      std::filesystem::path(configured)).lexically_normal();
  ASSERT_FALSE(destination.filename().empty());
  ASSERT_NE(destination, root_);
  ASSERT_NE(destination, destination.root_path());
  std::filesystem::create_directories(destination);
  ASSERT_TRUE(std::filesystem::is_empty(destination))
      << "Fixture export requires a new or empty output directory: "
      << destination.string();
  for (const char *directory : {"Scenes", "Materials", "Textures"}) {
    std::filesystem::copy(root_ / directory, destination / directory,
                          std::filesystem::copy_options::recursive);
  }
  auto exported_fs = ProjectFileSystem::open(destination);
  ASSERT_TRUE(exported_fs.ok()) << exported_fs.error().message;
  const auto catalog = workspace_.manifest.scenes();
  ProjectId exported_project_id = workspace_.manifest.id();
  if (const char *configured_id = std::getenv("QUADER_BRIDGE_TEST_PROJECT_ID");
      configured_id != nullptr && *configured_id != '\0') {
    const auto parsed = ProjectId::parse(configured_id);
    ASSERT_TRUE(parsed.ok()) << parsed.error().message;
    exported_project_id = parsed.value();
  }
  auto exported_manifest = ProjectManifest::hydrate(
      exported_project_id, utf8_path(destination.filename()),
      std::vector<ProjectSceneCatalogEntry>(catalog.begin(), catalog.end()),
      {}, std::nullopt);
  ASSERT_TRUE(exported_manifest.ok()) << exported_manifest.error().message;
  auto manifest_filename = destination.filename();
  manifest_filename += ".qproj";
  const auto saved = ProjectManifestStore::save_atomic(
      exported_fs.value(), destination / manifest_filename,
      exported_manifest.value());
  ASSERT_TRUE(saved.ok()) << saved.error().message;
  const auto reopened = ProjectManifestStore::open(destination / manifest_filename);
  ASSERT_TRUE(reopened.ok()) << reopened.error().message;
  {
    // Produce completion records through the same persistence owner as Ctrl+S.
    ProjectSession native_project(root_ / "export-recent-scenes.json");
    const auto opened = native_project.open(destination / manifest_filename);
    ASSERT_TRUE(opened.ok) << opened.message;
    for (const auto &entry : reopened.value().manifest.scenes()) {
      const auto opened_scene = native_project.open_scene(entry.path);
      ASSERT_TRUE(opened_scene.ok) << opened_scene.message;
      ASSERT_TRUE(opened_scene.scene_session_id.has_value());
      const auto committed = native_project.save(*opened_scene.scene_session_id);
      ASSERT_TRUE(committed.ok) << committed.message;
      EXPECT_TRUE(committed.warnings.empty()) << committed.message;
    }
  }
  const nlohmann::ordered_json expectations{
      {"resource_prefix", resource_prefix},
      {"light_scene_id", kLightingScene},
      {"light_scene_path", "Scenes/lighting.qdr"},
      {"light_entries", lighting_expectations(lighting_document)},
      {"light_updated_intensity", 8.0f},
      {"light_updated_preview_energy", 0.5f},
      {"calibration_scene_id", kCalibrationScene},
      {"calibration_scene_path", "Scenes/lighting_calibration.qdr"},
      {"calibration_lights", lighting_expectations(calibration_document)},
      {"calibration_environment_enabled", false},
      {"calibration_receiver_burley_name", "calibration-receiver-burley"},
      {"calibration_receiver_lambert_name", "calibration-receiver-lambert"},
      {"calibration_receiver_burley_material_name", "CalibrationReceiverBurley"},
      {"calibration_receiver_lambert_material_name", "CalibrationReceiverLambert"},
      {"light_reference_camera", {{"aperture", 16.0f},
                                   {"shutter_seconds", 1.0f / 125.0f},
                                   {"iso", 100.0f},
                                   {"exposure_multiplier", 1.0 / 38400.0}}},
      {"valid_scene_id", kFirstScene},
      {"valid_scene_path", "Scenes/mixed_materials.qdr"},
      {"valid_mesh_count", 3},
      {"surfaces_per_mesh", 2},
      {"visible_mesh_entity", 100},
      {"hidden_mesh_entity", 101},
      {"mirrored_mesh_entity", 202},
      {"collision_mesh_count", 3},
      {"collision_point_count", 6},
      {"missing_scene_id", kSecondScene},
      {"missing_scene_path", "Scenes/missing_texture.qdr"},
      {"texture_path", texture_path},
      {"normal_texture_path", normal_texture_path},
      {"project_material_path", project_material_path},
      {"missing_material_path", missing_material_path},
      {"installed_material_names", {embedded_material_name, resource_prefix + "Project surface"}},
      {"installed_texture_filenames", {resource_prefix + "albedo.PNG", resource_prefix + "normal.PNG"}},
      {"naming_scene_id", kNamingScene},
      {"naming_scene_path", "Scenes/resource_names.qdr"},
      {"naming_material_name", naming_material_names[0]},
      {"naming_material_names", naming_material_names},
      {"naming_material_diffuse_models", {1, 0}},
      {"naming_texture_filenames", naming_texture_filenames},
      {"naming_material_paths", naming_material_paths},
      {"naming_renamed_material_id", kNamingMaterials[0]},
      {"naming_renamed_material_name", resource_prefix + "Renamed Marmo"},
      {"naming_material_ids", kNamingMaterials},
      {"naming_collision_scene_id", kNamingCollisionScene},
      {"naming_normal_collision_scene_id", kNamingNormalCollisionScene},
      {"project_material_id", kProjectMaterial}};
  write_bytes(destination / "expected.json", expectations.dump(2) + "\n");
  RecordProperty("godot_fixture_manifest", utf8_path(destination / manifest_filename));
}

TEST_F(BridgeReaderTests, SaveGodotAcceptanceFixture) {
  const char *configured = std::getenv("QUADER_BRIDGE_MUTATE_FIXTURE_DIR");
  if (configured == nullptr || *configured == '\0') {
    GTEST_SKIP() << "Only runs when an owned Godot acceptance fixture is supplied.";
  }
  const auto destination = std::filesystem::absolute(
      std::filesystem::path(configured)).lexically_normal();
  ASSERT_NE(destination, destination.root_path());
  ASSERT_TRUE(std::filesystem::is_regular_file(destination / "expected.json"))
      << "Mutations require an exported acceptance fixture.";
  std::ifstream expectations_stream(destination / "expected.json", std::ios::binary);
  ASSERT_TRUE(expectations_stream.is_open());
  const auto expectations = nlohmann::json::parse(expectations_stream, nullptr, false);
  ASSERT_TRUE(expectations.is_object()) << "Fixture expectations must contain valid JSON.";
  auto manifest_filename = destination.filename();
  manifest_filename += ".qproj";
  const auto catalog = ProjectManifestStore::open(destination / manifest_filename);
  ASSERT_TRUE(catalog.ok()) << catalog.error().message;
  ASSERT_NE(catalog.value().manifest.find_scene(scene_id(kFirstScene)), nullptr);
  const char *configured_mode = std::getenv("QUADER_BRIDGE_MUTATION");
  const std::string mode = configured_mode == nullptr ? "scene" : configured_mode;
  unsigned revision = 1u;
  if (const char *configured_revision = std::getenv("QUADER_BRIDGE_MUTATION_REVISION");
      configured_revision != nullptr && *configured_revision != '\0') {
    const std::string_view text(configured_revision);
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), revision);
    ASSERT_EQ(parsed.ec, std::errc{});
    ASSERT_EQ(parsed.ptr, text.data() + text.size());
  }
  ASSERT_GE(revision, 1u);
  ASSERT_LE(revision, 50u);
  ProjectSession native_project(root_ / "mutation-recent-scenes.json");
  const auto opened = native_project.open(destination / manifest_filename);
  ASSERT_TRUE(opened.ok) << opened.message;
  if (mode == "baseline") {
    for (const auto &entry : catalog.value().manifest.scenes()) {
      const auto opened_scene = native_project.open_scene(entry.path);
      ASSERT_TRUE(opened_scene.ok) << opened_scene.message;
      ASSERT_TRUE(opened_scene.scene_session_id.has_value());
      const auto committed = native_project.save(*opened_scene.scene_session_id);
      ASSERT_TRUE(committed.ok) << committed.message;
      ASSERT_TRUE(committed.warnings.empty()) << committed.message;
    }
    return;
  }
  if (mode == "lights_half" || mode == "lights_camera") {
    const auto opened_scene = native_project.open_scene(relative_path(
        expectations.at("light_scene_path").get<std::string>()));
    ASSERT_TRUE(opened_scene.ok) << opened_scene.message;
    ASSERT_TRUE(opened_scene.scene_session_id.has_value());
    auto *scene = native_project.find(*opened_scene.scene_session_id);
    ASSERT_NE(scene, nullptr);
    if (mode == "lights_half") {
      for (const auto &entry : expectations.at("light_entries")) {
        const EntityId entity = entry.at("entity").get<EntityId>();
        const auto *current = scene->session->scene().light(entity);
        ASSERT_NE(current, nullptr);
        LightComponent light = *current;
        light.intensity = 8.0f;
        const auto changed = scene->session->document_commands().set_light_component(entity, light);
        ASSERT_TRUE(changed.ok) << changed.message;
      }
    } else {
      auto settings = scene->session->scene().scene_settings();
      settings.camera.aperture = 2.0f;
      settings.camera.shutter_speed = 0.5f;
      settings.camera.sensitivity = 1600.0f;
      settings.camera.use_exposure = false;
      const auto changed = scene->session->document_commands().set_scene_settings(settings);
      ASSERT_TRUE(changed.ok) << changed.message;
    }
    const auto committed = native_project.save(scene->session_id);
    ASSERT_TRUE(committed.ok) << committed.message;
    ASSERT_TRUE(committed.warnings.empty()) << committed.message;
    return;
  }
  if (mode == "naming_rename") {
    auto &editor = native_project.standard_material_editor();
    const auto material = editor.open(*ProjectAssetId::parse(kNamingMaterials[0]));
    ASSERT_TRUE(material.ok) << material.message;
    ASSERT_TRUE(editor.set_name(expectations.value(
        "naming_renamed_material_name", std::string("Renamed Marmo"))).ok);
    const auto committed = editor.save();
    ASSERT_TRUE(committed.ok) << committed.message;
    return;
  }
  if (mode == "collision_enable" || mode == "collision_disable" ||
      mode == "collision_unsupported") {
    const auto opened_scene = native_project.open_scene(
        relative_path("Scenes/mixed_materials.qdr"));
    ASSERT_TRUE(opened_scene.ok) << opened_scene.message;
    ASSERT_TRUE(opened_scene.scene_session_id.has_value());
    auto *scene = native_project.find(*opened_scene.scene_session_id);
    ASSERT_NE(scene, nullptr);
    const auto *saved_metadata = scene->session->scene().mesh(100u);
    ASSERT_NE(saved_metadata, nullptr);
    MeshComponent metadata = *saved_metadata;
    metadata.generate_collisions = mode != "collision_disable";
    metadata.collision_type = mode == "collision_unsupported"
        ? MeshCollisionType::Box : MeshCollisionType::Mesh;
    metadata.body_type = MeshPhysicsBodyType::StaticBody;
    const auto changed = scene->session->document_commands().set_mesh_component(
        100u, metadata);
    ASSERT_TRUE(changed.ok) << changed.message;
    const auto committed = native_project.save(scene->session_id);
    ASSERT_TRUE(committed.ok) << committed.message;
    ASSERT_TRUE(committed.warnings.empty()) << committed.message;
    return;
  }
  if (mode == "scene" || mode == "rapid" || mode == "save_all") {
    const auto opened_scene = native_project.open_scene(
        relative_path("Scenes/mixed_materials.qdr"));
    ASSERT_TRUE(opened_scene.ok) << opened_scene.message;
    ASSERT_TRUE(opened_scene.scene_session_id.has_value());
    auto *scene = native_project.find(*opened_scene.scene_session_id);
    ASSERT_NE(scene, nullptr);
    const unsigned count = mode == "rapid" ? 3u : 1u;
    for (unsigned offset = 0u; offset < count; ++offset) {
      const auto changed = scene->session->document_commands().apply_entity_transform(
          100u, TransformComponent{.location = {static_cast<float>(revision + offset),
                                               0.0f, 0.0f}});
      ASSERT_TRUE(changed.ok) << changed.message;
      const auto committed = mode == "save_all"
          ? native_project.save_all() : native_project.save(scene->session_id);
      ASSERT_TRUE(committed.ok) << committed.message;
      ASSERT_TRUE(committed.warnings.empty()) << committed.message;
    }
    RecordProperty("saved_entity_100_x", std::to_string(revision + count - 1u));
    return;
  }
  ASSERT_TRUE(mode == "material" || mode == "texture" || mode == "failed_texture")
      << "Unknown acceptance mutation: " << mode;
  auto &editor = native_project.standard_material_editor();
  const auto opened_material = editor.open(*ProjectAssetId::parse(kProjectMaterial));
  ASSERT_TRUE(opened_material.ok) << opened_material.message;
  ASSERT_TRUE(editor.set_roughness(0.2f + 0.01f * static_cast<float>(revision)).ok);
  ASSERT_TRUE(editor.set_base_color(
      {0.1f + 0.01f * static_cast<float>(revision), 0.4f, 0.6f}).ok);
  ASSERT_TRUE(editor.set_texture_transform_component(
      StandardMaterialTextureTransformComponent::RotationDegrees,
      35.0f + static_cast<float>(revision)).ok);
  ASSERT_TRUE(editor.set_texture_transform_component(
      StandardMaterialTextureTransformComponent::OffsetU,
      0.2f + 0.01f * static_cast<float>(revision)).ok);
  if (mode == "texture" || mode == "failed_texture") {
    const auto relative = ProjectRelativePath::parse_portable(
        expectations.value("texture_path", std::string{}));
    ASSERT_TRUE(relative.ok()) << relative.error().message;
    const auto texture = catalog.value().file_system.resolve_for_write(relative.value());
    ASSERT_TRUE(texture.ok()) << texture.error().message;
    if (mode == "texture") {
      QImage image(1, 1, QImage::Format_RGBA8888);
      image.fill(QColor(static_cast<int>(revision * 4u), 64, 192, 255));
      ASSERT_TRUE(image.save(QString::fromStdWString(texture.value().wstring()), "PNG"));
    } else {
      write_bytes(texture.value(), "invalid PNG payload");
    }
  }
  const auto committed = editor.save();
  ASSERT_TRUE(committed.ok) << committed.message;
  RecordProperty("saved_material_revision", std::to_string(revision));
}

} // namespace
