////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "support/behavior_expect.hpp"
#include "support/behavior_scenario.hpp"

#include "api/quader_polygon_document_internal_access.hpp"

#include <gtest/gtest.h>
#include <quader/modeling/neutral/polygon/document_texture.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace {

quader::QuaderApiVoidResult edit_face_texture(
    quader::QuaderApi &api, quader::QuaderFaceTextureField field, double value,
    std::string merge_key = {}) {
  const auto settings = api.modeling().face_texture_settings();
  return api.modeling().set_face_texture_setting({
      .field = field,
      .value = value,
      .selection_token = settings.selection_token,
      .merge_key = std::move(merge_key),
  });
}

quader::QuaderApiVoidResult apply_face_texture_action(
    quader::QuaderApi &api, quader::QuaderFaceTextureAction action) {
  return api.modeling().apply_face_texture_action({
      .action = action,
      .selection_token = api.modeling().face_texture_settings().selection_token,
  });
}

quader::QuaderMeshFaceTexture face_texture(
    const quader::QuaderApi &api, quader::QuaderModelingObjectRef object,
    quader::QuaderElementId face_id) {
  const auto mesh = api.query().compiled_object_mesh(object);
  if (!mesh.has_value()) {
    ADD_FAILURE() << "Expected a compiled mesh for the modeling object.";
    return {};
  }
  const auto face = std::find_if(
      mesh->face_textures.begin(), mesh->face_textures.end(),
      [face_id](const auto &texture) { return texture.face == face_id; });
  if (face == mesh->face_textures.end()) {
    ADD_FAILURE() << "Expected authored texture properties for face " << face_id;
    return {};
  }
  return *face;
}

void expect_texture_mesh_equal(const std::optional<quader::QuaderMeshAsset> &mesh,
                               const quader::QuaderMeshAsset &expected) {
  ASSERT_TRUE(mesh.has_value());
  const auto &actual = *mesh;
  EXPECT_EQ(actual.indices, expected.indices);
  ASSERT_EQ(actual.primitives.size(), expected.primitives.size());
  for (std::size_t index = 0; index < actual.primitives.size(); ++index) {
    EXPECT_EQ(actual.primitives[index].index_offset,
              expected.primitives[index].index_offset);
    EXPECT_EQ(actual.primitives[index].index_count,
              expected.primitives[index].index_count);
    EXPECT_EQ(actual.primitives[index].material_slot,
              expected.primitives[index].material_slot);
  }
  ASSERT_EQ(actual.vertices.size(), expected.vertices.size());
  for (std::size_t index = 0; index < actual.vertices.size(); ++index) {
    SCOPED_TRACE(index);
    const auto &vertex = actual.vertices[index];
    const auto &reference = expected.vertices[index];
    EXPECT_EQ(vertex.position.x, reference.position.x);
    EXPECT_EQ(vertex.position.y, reference.position.y);
    EXPECT_EQ(vertex.position.z, reference.position.z);
    EXPECT_EQ(vertex.normal.x, reference.normal.x);
    EXPECT_EQ(vertex.normal.y, reference.normal.y);
    EXPECT_EQ(vertex.normal.z, reference.normal.z);
    EXPECT_EQ(vertex.uv.x, reference.uv.x);
    EXPECT_EQ(vertex.uv.y, reference.uv.y);
    EXPECT_EQ(vertex.uv_carrier, reference.uv_carrier);
    EXPECT_EQ(vertex.uv_geometric_normal.x, reference.uv_geometric_normal.x);
    EXPECT_EQ(vertex.uv_geometric_normal.y, reference.uv_geometric_normal.y);
    EXPECT_EQ(vertex.uv_geometric_normal.z, reference.uv_geometric_normal.z);
    EXPECT_EQ(vertex.rgba, reference.rgba);
  }
  ASSERT_EQ(actual.face_textures.size(), expected.face_textures.size());
  for (std::size_t index = 0; index < actual.face_textures.size(); ++index) {
    SCOPED_TRACE(index);
    const auto &face = actual.face_textures[index];
    const auto &reference = expected.face_textures[index];
    EXPECT_EQ(face.face, reference.face);
    EXPECT_EQ(face.scale.x, reference.scale.x);
    EXPECT_EQ(face.scale.y, reference.scale.y);
    EXPECT_EQ(face.shift_uv.x, reference.shift_uv.x);
    EXPECT_EQ(face.shift_uv.y, reference.shift_uv.y);
    EXPECT_EQ(face.rotation_degrees, reference.rotation_degrees);
  }
}

void expect_document_revisions_equal(const quader::QuaderRevisionStamp &actual,
                                     const quader::QuaderRevisionStamp &expected) {
  EXPECT_EQ(actual.project_revision, expected.project_revision);
  EXPECT_EQ(actual.scene_revision, expected.scene_revision);
  EXPECT_EQ(actual.scene_layer_revision, expected.scene_layer_revision);
  EXPECT_EQ(actual.asset_revision, expected.asset_revision);
  EXPECT_EQ(actual.modeling_content_revision, expected.modeling_content_revision);
  EXPECT_EQ(actual.modeling_selection_revision,
            expected.modeling_selection_revision);
}

quader::QuaderPolygonDocument face_texture_wall_document() {
  quader_poly::Document document;
  const std::array vertices{
      quader_poly::add_vertex(document, {-1.0f, -1.0f, 0.0f}),
      quader_poly::add_vertex(document, {1.0f, -1.0f, 0.0f}),
      quader_poly::add_vertex(document, {1.0f, 1.0f, 0.0f}),
      quader_poly::add_vertex(document, {-1.0f, 1.0f, 0.0f}),
  };
  (void)quader_poly::add_face(document, vertices);
  document.faces.front().uvs = {
      {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f},
  };
  return quader::QuaderPolygonDocumentInternalAccess::from_backend(
      std::move(document));
}

quader::QuaderPolygonDocument uv_peel_strip_document() {
  quader_poly::Document document;
  const std::array vertices{
      quader_poly::add_vertex(document, {-2.0f, -1.0f, 0.0f}),
      quader_poly::add_vertex(document, {0.0f, -1.0f, 0.0f}),
      quader_poly::add_vertex(document, {2.0f, -1.0f, 0.0f}),
      quader_poly::add_vertex(document, {-2.0f, 1.0f, 0.0f}),
      quader_poly::add_vertex(document, {0.0f, 1.0f, 0.0f}),
      quader_poly::add_vertex(document, {2.0f, 1.0f, 0.0f}),
  };
  (void)quader_poly::add_face(
      document, std::array{vertices[0], vertices[1], vertices[4], vertices[3]});
  (void)quader_poly::add_face(
      document, std::array{vertices[1], vertices[2], vertices[5], vertices[4]});
  return quader::QuaderPolygonDocumentInternalAccess::from_backend(
      std::move(document));
}

quader_poly::Document uv_peel_cylinder_document() {
  // Saved 32-sided cylinder coordinates and split-face IDs, independent of a scene file.
  constexpr std::array<quader::QVec2, 32> kRing{{
      {0.5F, 0.0F},
      {0.49039262533187866F, 0.09754516184329987F},
      {0.4619397521018982F, 0.19134172797203064F},
      {0.41573479771614075F, 0.27778512239456177F},
      {0.3535533845424652F, 0.3535533845424652F},
      {0.2777850925922394F, 0.41573482751846313F},
      {0.19134171307086945F, 0.4619397521018982F},
      {0.09754511713981628F, 0.49039265513420105F},
      {-2.1855694143368964e-08F, 0.5F},
      {-0.09754516184329987F, 0.49039262533187866F},
      {-0.19134175777435303F, 0.4619397521018982F},
      {-0.27778518199920654F, 0.41573476791381836F},
      {-0.3535533845424652F, 0.3535533845424652F},
      {-0.41573482751846313F, 0.2777850925922394F},
      {-0.46193981170654297F, 0.19134163856506348F},
      {-0.49039265513420105F, 0.09754515439271927F},
      {-0.5F, -4.371138828673793e-08F},
      {-0.49039262533187866F, -0.09754524379968643F},
      {-0.4619397521018982F, -0.19134171307086945F},
      {-0.41573476791381836F, -0.27778515219688416F},
      {-0.35355332493782043F, -0.35355344414711F},
      {-0.2777850031852722F, -0.4157348871231079F},
      {-0.1913415640592575F, -0.46193984150886536F},
      {-0.09754519164562225F, -0.49039262533187866F},
      {5.962440319251527e-09F, -0.5F},
      {0.09754520654678345F, -0.49039262533187866F},
      {0.1913418024778366F, -0.4619397222995758F},
      {0.27778521180152893F, -0.41573473811149597F},
      {0.35355350375175476F, -0.35355326533317566F},
      {0.41573479771614075F, -0.27778515219688416F},
      {0.4619397819042206F, -0.19134171307086945F},
      {0.49039265513420105F, -0.09754511713981628F}}};
  constexpr std::array<std::size_t, 32> kMiddleStations{{
      10, 9, 11, 8, 12, 7, 13, 6, 14, 5, 15, 4, 16, 3, 17, 2, 18, 1, 19, 0, 20, 31, 21, 30, 22, 29, 23, 28, 24, 27, 25, 26}};
  constexpr std::array<std::array<quader_poly::ElementId, 5>, 64> kSideFaces{{
      {{10, 11, 10, 66, 65}},
      {{35, 43, 65, 66, 42}},
      {{11, 43, 44, 67, 65}},
      {{36, 11, 65, 67, 12}},
      {{9, 10, 9, 68, 66}},
      {{37, 42, 66, 68, 41}},
      {{12, 44, 45, 69, 67}},
      {{38, 12, 67, 69, 13}},
      {{8, 9, 8, 70, 68}},
      {{39, 41, 68, 70, 40}},
      {{13, 45, 46, 71, 69}},
      {{40, 13, 69, 71, 14}},
      {{7, 8, 7, 72, 70}},
      {{41, 40, 70, 72, 39}},
      {{14, 46, 47, 73, 71}},
      {{42, 14, 71, 73, 15}},
      {{6, 7, 6, 74, 72}},
      {{43, 39, 72, 74, 38}},
      {{15, 47, 48, 75, 73}},
      {{44, 15, 73, 75, 16}},
      {{5, 6, 5, 76, 74}},
      {{45, 38, 74, 76, 37}},
      {{16, 48, 49, 77, 75}},
      {{46, 16, 75, 77, 17}},
      {{4, 5, 4, 78, 76}},
      {{47, 37, 76, 78, 36}},
      {{17, 49, 50, 79, 77}},
      {{48, 17, 77, 79, 18}},
      {{3, 4, 3, 80, 78}},
      {{49, 36, 78, 80, 35}},
      {{18, 50, 51, 81, 79}},
      {{50, 18, 79, 81, 19}},
      {{2, 3, 2, 82, 80}},
      {{51, 35, 80, 82, 34}},
      {{19, 51, 52, 83, 81}},
      {{52, 19, 81, 83, 20}},
      {{1, 2, 1, 84, 82}},
      {{53, 34, 82, 84, 33}},
      {{20, 52, 53, 85, 83}},
      {{54, 20, 83, 85, 21}},
      {{32, 1, 32, 86, 84}},
      {{55, 33, 84, 86, 64}},
      {{21, 53, 54, 87, 85}},
      {{56, 21, 85, 87, 22}},
      {{31, 32, 31, 88, 86}},
      {{57, 64, 86, 88, 63}},
      {{22, 54, 55, 89, 87}},
      {{58, 22, 87, 89, 23}},
      {{30, 31, 30, 90, 88}},
      {{59, 63, 88, 90, 62}},
      {{23, 55, 56, 91, 89}},
      {{60, 23, 89, 91, 24}},
      {{29, 30, 29, 92, 90}},
      {{61, 62, 90, 92, 61}},
      {{24, 56, 57, 93, 91}},
      {{62, 24, 91, 93, 25}},
      {{28, 29, 28, 94, 92}},
      {{63, 61, 92, 94, 60}},
      {{25, 57, 58, 95, 93}},
      {{64, 25, 93, 95, 26}},
      {{27, 28, 27, 96, 94}},
      {{65, 60, 94, 96, 59}},
      {{26, 58, 59, 96, 95}},
      {{66, 26, 95, 96, 27}}}};
  quader_poly::Document document;
  constexpr std::array kHeights{-0.125F, 0.875F, 0.375F};
  for (std::size_t rail = 0; rail < kHeights.size(); ++rail) {
    for (std::size_t index = 0; index < kRing.size(); ++index) {
      const auto station = rail == 2U ? kMiddleStations[index] : index;
      document.vertices.push_back({
          static_cast<quader_poly::ElementId>(rail * kRing.size() + index + 1U),
          {kRing[station].x, kHeights[rail], kRing[station].y}});
    }
  }
  quader_poly::Face bottom{.id = 33};
  quader_poly::Face top{.id = 34};
  for (quader_poly::ElementId index = 0; index < 32; ++index) {
    bottom.vertices.push_back(index + 1);
    top.vertices.push_back(64 - index);
    bottom.uvs.push_back({kRing[index].x * 0.03125F, -kRing[index].y * 0.03125F});
    top.uvs.push_back({kRing[31U - index].x * 0.03125F, kRing[31U - index].y * 0.03125F});
  }
  bottom.texture_transform.scale = top.texture_transform.scale = {0.125F, 0.125F};
  document.faces = {std::move(bottom), std::move(top)};
  for (const auto &entry : kSideFaces) {
    document.faces.push_back({.id = entry[0],
        .vertices = {entry[1], entry[2], entry[3], entry[4]},
        .uvs = {{0.0F, 0.0F}, {0.125F, 0.0F}, {0.125F, 0.125F}, {0.0F, 0.125F}},
        .texture_transform = {.scale = {0.125F, 0.125F}}});
  }
  document.next_vertex_id = 97;
  document.next_face_id = 67;
  return document;
}

quader_poly::Document uv_peel_unsplit_cylinder_document() {
  auto document = uv_peel_cylinder_document();
  document.vertices.resize(64U);
  document.faces.resize(2U);
  for (quader_poly::ElementId index = 0; index < 32; ++index) {
    const auto first = index + 1;
    const auto next = (index + 1) % 32 + 1;
    document.faces.push_back({.id = first,
        .vertices = {next, first, first + 32, next + 32},
        .uvs = {{0.0F, 0.0F}, {0.125F, 0.0F}, {0.125F, 0.125F}, {0.0F, 0.125F}},
        .texture_transform = {.scale = {0.125F, 0.125F}}});
  }
  document.next_vertex_id = 65;
  document.next_face_id = 35;
  return document;
}

quader_poly::Document uv_peel_arch_document(bool split_depth) {
  constexpr std::array kAngles{
      0.0, 0.12, 0.35, 0.64, 1.02, 1.47, 1.94, 2.38, 2.77,
      3.14159265358979323846};
  const std::vector<quader::QVec2> profile = split_depth
      ? std::vector<quader::QVec2>{{2.0F, -0.75F}, {2.0F, 0.0F},
                                 {2.0F, 0.75F}, {3.0F, 0.75F},
                                 {3.0F, 0.0F}, {3.0F, -0.75F}}
      : std::vector<quader::QVec2>{{2.0F, -0.75F}, {2.0F, 0.75F},
                                 {3.0F, 0.75F}, {3.0F, -0.75F}};
  quader_poly::Document document;
  for (const auto point : profile) {
    for (const double angle : kAngles) {
      (void)quader_poly::add_vertex(document,
          {point.x * static_cast<float>(std::cos(angle)),
           point.x * static_cast<float>(std::sin(angle)), point.y});
    }
  }
  const auto vertex_id = [&kAngles](std::size_t rail, std::size_t station) {
    return static_cast<quader_poly::ElementId>(rail * kAngles.size() + station + 1U);
  };
  for (std::size_t rail = 0; rail < profile.size(); ++rail) {
    const auto next_rail = (rail + 1U) % profile.size();
    for (std::size_t station = 0; station + 1U < kAngles.size(); ++station) {
      (void)quader_poly::add_face(document,
          std::array{vertex_id(rail, station), vertex_id(next_rail, station),
                     vertex_id(next_rail, station + 1U), vertex_id(rail, station + 1U)},
          static_cast<std::uint32_t>(rail % 3U));
    }
  }
  std::vector<quader_poly::ElementId> first_cap;
  std::vector<quader_poly::ElementId> last_cap;
  for (std::size_t rail = 0; rail < profile.size(); ++rail) {
    first_cap.push_back(vertex_id(profile.size() - 1U - rail, 0U));
    last_cap.push_back(vertex_id(rail, kAngles.size() - 1U));
  }
  (void)quader_poly::add_face(document, first_cap, 1U);
  (void)quader_poly::add_face(document, last_cap, 2U);
  for (auto &face : document.faces) {
    face.texture_transform = {.scale = {0.5F, 0.75F},
                              .shift_uv = {4.0F, -5.0F},
                              .rotation_degrees = 17.0F};
  }
  return document;
}

void expect_uv_peel_preserves_mesh_geometry(
    const quader::QuaderMeshAsset &actual,
    const quader::QuaderMeshAsset &expected) {
  EXPECT_EQ(actual.indices, expected.indices);
  ASSERT_EQ(actual.primitives.size(), expected.primitives.size());
  for (std::size_t index = 0; index < actual.primitives.size(); ++index) {
    EXPECT_EQ(actual.primitives[index].index_offset, expected.primitives[index].index_offset);
    EXPECT_EQ(actual.primitives[index].index_count, expected.primitives[index].index_count);
    EXPECT_EQ(actual.primitives[index].material_slot, expected.primitives[index].material_slot);
  }
  ASSERT_EQ(actual.vertices.size(), expected.vertices.size());
  for (std::size_t index = 0; index < actual.vertices.size(); ++index) {
    const auto &vertex = actual.vertices[index];
    const auto &reference = expected.vertices[index];
    EXPECT_EQ(vertex.position.x, reference.position.x);
    EXPECT_EQ(vertex.position.y, reference.position.y);
    EXPECT_EQ(vertex.position.z, reference.position.z);
    EXPECT_EQ(vertex.normal.x, reference.normal.x);
    EXPECT_EQ(vertex.normal.y, reference.normal.y);
    EXPECT_EQ(vertex.normal.z, reference.normal.z);
    EXPECT_EQ(vertex.rgba, reference.rgba);
    EXPECT_EQ(vertex.uv_geometric_normal.x, reference.uv_geometric_normal.x);
    EXPECT_EQ(vertex.uv_geometric_normal.y, reference.uv_geometric_normal.y);
    EXPECT_EQ(vertex.uv_geometric_normal.z, reference.uv_geometric_normal.z);
  }
  ASSERT_EQ(actual.face_textures.size(), expected.face_textures.size());
  for (std::size_t index = 0; index < actual.face_textures.size(); ++index) {
    const auto &face = actual.face_textures[index];
    const auto &reference = expected.face_textures[index];
    EXPECT_EQ(face.face, reference.face);
    EXPECT_EQ(face.scale.x, reference.scale.x);
    EXPECT_EQ(face.scale.y, reference.scale.y);
    EXPECT_EQ(face.shift_uv.x, reference.shift_uv.x);
    EXPECT_EQ(face.shift_uv.y, reference.shift_uv.y);
    EXPECT_EQ(face.rotation_degrees, reference.rotation_degrees);
  }
}

void expect_uv_peel_arch_face_changes(
    const quader_poly::Document &document,
    const quader::QuaderMeshAsset &before,
    const quader::QuaderMeshAsset &after,
    const std::vector<bool> &changed_faces) {
  ASSERT_EQ(document.faces.size(), changed_faces.size());
  ASSERT_EQ(before.indices, after.indices);
  ASSERT_EQ(before.vertices.size(), after.vertices.size());
  for (std::size_t face_index = 0; face_index < document.faces.size(); ++face_index) {
    SCOPED_TRACE(document.faces[face_index].id);
    const auto &face = document.faces[face_index];
    const auto belongs_to_face = [&](const quader::QVec3 &position) {
      return std::any_of(face.vertices.begin(), face.vertices.end(),
          [&](quader_poly::ElementId id) {
            const auto &expected = document.vertices[static_cast<std::size_t>(id - 1U)].position;
            return position.x == expected.x && position.y == expected.y && position.z == expected.z;
          });
    };
    bool found_triangle = false;
    bool changed = false;
    for (std::size_t index = 0; index + 2U < before.indices.size(); index += 3U) {
      const std::array corners{
          before.indices[index], before.indices[index + 1U], before.indices[index + 2U]};
      if (!std::all_of(corners.begin(), corners.end(), [&](std::uint32_t corner) {
            return belongs_to_face(before.vertices[corner].position);
          })) {
        continue;
      }
      found_triangle = true;
      for (const auto corner : corners) {
        const auto &a = after.vertices[corner];
        const auto &b = before.vertices[corner];
        EXPECT_TRUE(std::isfinite(a.uv.x));
        EXPECT_TRUE(std::isfinite(a.uv.y));
        changed |= a.uv.x != b.uv.x || a.uv.y != b.uv.y;
        if (!changed_faces[face_index]) {
          EXPECT_EQ(a.uv.x, b.uv.x);
          EXPECT_EQ(a.uv.y, b.uv.y);
          EXPECT_EQ(a.uv_carrier, b.uv_carrier);
        }
      }
    }
    EXPECT_TRUE(found_triangle);
    EXPECT_EQ(changed, changed_faces[face_index]);
  }
}

quader::QuaderApiVoidResult peel_uv(
    quader::QuaderApi &api, const quader::QuaderUvPeelOptions &options) {
  return api.modeling().peel_uv({
      .options = options,
      .selection_token = api.modeling().uv_peel_settings().selection_token,
  });
}

std::optional<quader::QuaderMeshAsset> effective_texture_mesh(
    const quader::QuaderApi &api, quader::QuaderModelingObjectRef object) {
  auto mesh = api.query().compiled_object_mesh(object);
  const auto entity = api.query().entity(object.entity);
  if (!mesh.has_value() || !entity.has_value()) {
    return std::nullopt;
  }
  const auto mapping =
      entity->texture_sampling_from_local.value_or(quader::QMat4{});
  const auto &matrix = mapping.values;
  const quader_poly::Transform3 sampling{
      .x_axis = {matrix[0], matrix[1], matrix[2]},
      .y_axis = {matrix[4], matrix[5], matrix[6]},
      .z_axis = {matrix[8], matrix[9], matrix[10]},
      .origin = {matrix[12], matrix[13], matrix[14]},
  };
  for (auto &vertex : mesh->vertices) {
    vertex.uv = quader_poly::evaluate_texture_mapping(
        vertex.uv, vertex.position, vertex.uv_geometric_normal,
        vertex.uv_carrier, sampling);
  }
  return mesh;
}

void expect_texture_uv_bounds(const std::optional<quader::QuaderMeshAsset> &mesh,
                              float minimum_u, float maximum_u,
                              float minimum_v = 0.0f, float maximum_v = 1.0f) {
  ASSERT_TRUE(mesh.has_value());
  ASSERT_FALSE(mesh->vertices.empty());
  quader::QVec2 minimum = mesh->vertices.front().uv;
  quader::QVec2 maximum = minimum;
  for (const auto &vertex : mesh->vertices) {
    minimum.x = std::min(minimum.x, vertex.uv.x);
    minimum.y = std::min(minimum.y, vertex.uv.y);
    maximum.x = std::max(maximum.x, vertex.uv.x);
    maximum.y = std::max(maximum.y, vertex.uv.y);
  }
  EXPECT_NEAR(minimum.x, minimum_u, 0.00001f);
  EXPECT_NEAR(maximum.x, maximum_u, 0.00001f);
  EXPECT_NEAR(minimum.y, minimum_v, 0.00001f);
  EXPECT_NEAR(maximum.y, maximum_v, 0.00001f);
}

void expect_uv_peel_arch_operation(bool split_depth) {
  auto s = quader::test::BehaviorScenario::new_project();
  const auto original = uv_peel_arch_document(split_depth);
  s.given().mesh("arch",
      quader::QuaderPolygonDocumentInternalAccess::from_backend(original));
  auto &api = s.api();
  const auto object = s.object("arch");
  quader::QuaderComponentSelectionArgs guide{
      .object = object, .kind = quader::QuaderComponentKind::Edge,
      .active = {.object = object, .kind = quader::QuaderComponentKind::Edge,
                 .edge_a = split_depth ? 11 : 5,
                 .edge_b = split_depth ? 12 : 15}};
  if (split_depth) {
    for (quader::QuaderElementId station = 0; station < 9U; ++station) {
      guide.edges.emplace_back(11U + station, 12U + station);
    }
  } else {
    guide.edges.emplace_back(5U, 15U);
  }
  s.given().edit_mode();
  ASSERT_TRUE(api.selection().select_components(guide).ok());
  const auto before = api.query().compiled_object_mesh(object);
  const auto effective_before = effective_texture_mesh(api, object);
  ASSERT_TRUE(before.has_value());
  ASSERT_TRUE(effective_before.has_value());
  const auto selection_revision = api.revisions().modeling_selection_revision;
  const auto history_before = api.history().snapshot();
  const quader::QuaderUvPeelOptions options{
      .world_space = false, .u_repeat = 2.0, .v_repeat = 3.0,
      .u_offset = 0.25, .v_offset = -0.5};
  const auto peeled = peel_uv(api, options);
  ASSERT_TRUE(peeled.ok()) << peeled.error_message();
  ASSERT_TRUE(peeled.changed());
  const auto after = api.query().compiled_object_mesh(object);
  const auto effective_after = effective_texture_mesh(api, object);
  ASSERT_TRUE(after.has_value());
  ASSERT_TRUE(effective_after.has_value());
  expect_uv_peel_preserves_mesh_geometry(*after, *before);
  std::vector<bool> changed_faces(original.faces.size(), false);
  if (split_depth) {
    // A longitudinal guide walks both depth halves and around front, outer,
    // and back faces. Only the two six-sided arch ends stop traversal.
    std::fill_n(changed_faces.begin(), 54U, true);
  } else {
    // One width edge walks the whole inner arch, both quad ends, and the
    // opposite outer arch; neither front nor back strip is in that walk.
    std::fill_n(changed_faces.begin(), 9U, true);
    std::fill_n(changed_faces.begin() + 18U, 9U, true);
    changed_faces[36U] = changed_faces[37U] = true;
  }
  expect_uv_peel_arch_face_changes(
      original, *effective_before, *effective_after, changed_faces);
  EXPECT_EQ(api.revisions().modeling_selection_revision, selection_revision);
  s.then().selection().mode(quader::QuaderSelectionMode::Edge)
      .edge_count(guide.edges.size()).active_alias("arch");
  EXPECT_EQ(api.history().snapshot().undo_label, "UV Peel");
  const auto revisions_after = api.revisions();
  const auto repeated = peel_uv(api, options);
  ASSERT_TRUE(repeated.ok()) << repeated.error_message();
  EXPECT_FALSE(repeated.changed());
  expect_document_revisions_equal(api.revisions(), revisions_after);
  expect_texture_mesh_equal(api.query().compiled_object_mesh(object), *after);
  ASSERT_TRUE(api.history().undo().ok());
  expect_texture_mesh_equal(api.query().compiled_object_mesh(object), *before);
  EXPECT_EQ(api.history().snapshot().undo_label, history_before.undo_label);
  EXPECT_EQ(api.history().snapshot().redo_label, "UV Peel");
  s.then().selection().mode(quader::QuaderSelectionMode::Edge)
      .edge_count(guide.edges.size()).active_alias("arch");
  ASSERT_TRUE(api.history().redo().ok());
  expect_texture_mesh_equal(api.query().compiled_object_mesh(object), *after);
  s.then().selection().mode(quader::QuaderSelectionMode::Edge)
      .edge_count(guide.edges.size()).active_alias("arch");
}

/// Describes one synthetic component gizmo workflow so the same behavior can be
/// verified across vertex, edge, and face selection modes.
struct ComponentPickCase {
    quader::QuaderSelectionMode mode = quader::QuaderSelectionMode::Face;
    quader::QVec3 ray_origin = {};
    quader::ModelingTransformTool tool = quader::ModelingTransformTool::Move;
};

/// Describes one repeat-rotation workflow across component and object modes.
struct RepeatRotationCase {
  quader::QuaderSelectionMode mode = quader::QuaderSelectionMode::Face;
  quader::QVec3 ray_origin = {};
  const char *label = "";
};

constexpr quader::QVec3 kRepeatRotationWorldPivot{4.0f, -3.0f, 0.25f};

using PointKey = std::array<int, 3>;
using EdgeKey = std::array<int, 6>;

/**
 * Records one directed edge use in a triangulated mesh snapshot.
 */
struct TriangleEdgeUse {
  PointKey from = {};
  PointKey to = {};
};

const quader::test::MeshSnapshot *
mesh_snapshot_for_alias(const quader::test::BehaviorSnapshot &snapshot,
                        std::string_view alias) {
  const auto entity =
      std::find_if(snapshot.entities.begin(), snapshot.entities.end(),
                   [alias](const quader::test::EntitySnapshot &value) {
                     return value.alias == alias;
                   });
  if (entity == snapshot.entities.end()) {
    return nullptr;
  }
  const auto mesh = std::find_if(
      snapshot.meshes.begin(), snapshot.meshes.end(),
      [mesh_id = entity->mesh](const quader::test::MeshSnapshot &value) {
        return value.mesh == mesh_id;
      });
  return mesh == snapshot.meshes.end() ? nullptr : &*mesh;
}

void expect_mesh_bounds_near(const quader::test::MeshSnapshot &actual,
                             const quader::test::MeshSnapshot &expected,
                             float epsilon = 0.001f) {
  EXPECT_NEAR(actual.bounds.min.x, expected.bounds.min.x, epsilon);
  EXPECT_NEAR(actual.bounds.min.y, expected.bounds.min.y, epsilon);
  EXPECT_NEAR(actual.bounds.min.z, expected.bounds.min.z, epsilon);
  EXPECT_NEAR(actual.bounds.max.x, expected.bounds.max.x, epsilon);
  EXPECT_NEAR(actual.bounds.max.y, expected.bounds.max.y, epsilon);
  EXPECT_NEAR(actual.bounds.max.z, expected.bounds.max.z, epsilon);
}

void expect_vec3_near(quader::QVec3 actual, quader::QVec3 expected,
                      float epsilon = 0.001f) {
  EXPECT_NEAR(actual.x, expected.x, epsilon);
  EXPECT_NEAR(actual.y, expected.y, epsilon);
  EXPECT_NEAR(actual.z, expected.z, epsilon);
}

void expect_orientation_near(const quader::QuaderTransform3 &actual,
                             const quader::QuaderTransform3 &expected,
                             float epsilon = 0.001f) {
  expect_vec3_near(actual.x_axis, expected.x_axis, epsilon);
  expect_vec3_near(actual.y_axis, expected.y_axis, epsilon);
  expect_vec3_near(actual.z_axis, expected.z_axis, epsilon);
}

float orientation_axis_difference(const quader::QuaderTransform3 &first,
                                  const quader::QuaderTransform3 &second) {
  return quader::length(first.x_axis - second.x_axis) +
         quader::length(first.y_axis - second.y_axis) +
         quader::length(first.z_axis - second.z_axis);
}

bool orientation_is_slanted(const quader::QuaderTransform3 &orientation) {
  const std::array axes{
      orientation.x_axis,
      orientation.y_axis,
      orientation.z_axis,
  };
  return std::any_of(axes.begin(), axes.end(), [](quader::QVec3 axis) {
    const std::array magnitudes{
        std::abs(axis.x),
        std::abs(axis.y),
        std::abs(axis.z),
    };
    return static_cast<int>(std::count_if(
               magnitudes.begin(), magnitudes.end(),
               [](float value) { return value > 0.1f; })) >= 2;
  });
}

quader::QuaderComponentSelectionArgs component_orientation_selection(
    quader::QuaderModelingObjectRef object, quader::QuaderSelectionMode mode,
    bool include_first, bool include_second, bool second_is_active) {
  quader::QuaderComponentSelectionArgs args;
  args.object = object;
  args.edit = quader::QuaderSelectionEdit::Replace;
  switch (mode) {
  case quader::QuaderSelectionMode::Vertex:
    args.kind = quader::QuaderComponentKind::Vertex;
    if (include_first) {
      args.vertices.push_back(1);
    }
    if (include_second) {
      args.vertices.push_back(2);
    }
    args.active = {
        .object = object,
        .kind = quader::QuaderComponentKind::Vertex,
        .vertex = second_is_active ? 2 : 1,
    };
    break;
  case quader::QuaderSelectionMode::Edge:
    args.kind = quader::QuaderComponentKind::Edge;
    if (include_first) {
      args.edges.emplace_back(1, 2);
    }
    if (include_second) {
      args.edges.emplace_back(1, 4);
    }
    args.active = {
        .object = object,
        .kind = quader::QuaderComponentKind::Edge,
        .edge_a = 1,
        .edge_b = second_is_active ? 4 : 2,
    };
    break;
  case quader::QuaderSelectionMode::Face:
    args.kind = quader::QuaderComponentKind::Face;
    if (include_first) {
      args.faces.push_back(1);
    }
    if (include_second) {
      args.faces.push_back(3);
    }
    args.active = {
        .object = object,
        .kind = quader::QuaderComponentKind::Face,
        .face = second_is_active ? 3 : 1,
    };
    break;
  case quader::QuaderSelectionMode::Object:
  case quader::QuaderSelectionMode::Group:
    break;
  }
  return args;
}

std::array<quader::QVec3, 4> beveled_box_side_positions(bool oblique = false) {
  std::array positions{
      quader::QVec3{3.0f, -0.5f, 3.5f},
      quader::QVec3{3.0f, -0.2142857313156128f, 3.0f},
      quader::QVec3{3.0f, -0.2142857313156128f, -3.0f},
      quader::QVec3{3.0f, -0.5f, -3.5f},
  };
  if (oblique) {
    const float cosine = std::cos(0.3f);
    const float sine = std::sin(0.3f);
    for (auto &position : positions) {
      position = {cosine * position.x - sine * position.y,
                  sine * position.x + cosine * position.y, position.z};
    }
  }
  return positions;
}

quader::QuaderPolygonDocument beveled_box_side_document(
    std::size_t first_corner = 0U, bool oblique = false) {
  quader_poly::Document document;
  std::array<quader_poly::ElementId, 4> vertices;
  const auto positions = beveled_box_side_positions(oblique);
  for (std::size_t index = 0; index < vertices.size(); ++index) {
    vertices[index] = quader_poly::add_vertex(document, positions[index]);
  }
  std::rotate(vertices.begin(), vertices.begin() + first_corner, vertices.end());
  (void)quader_poly::add_face(document, vertices);
  return quader::QuaderPolygonDocumentInternalAccess::from_backend(
      std::move(document));
}

const std::array<quader::QVec3, 4> &local_transform_tetrahedron_positions() {
  static constexpr std::array<quader::QVec3, 4> kPositions{
      quader::QVec3{0.0f, 0.0f, 0.0f},
      quader::QVec3{2.0f, 0.0f, 0.0f},
      quader::QVec3{0.0f, 2.0f, 0.0f},
      quader::QVec3{0.0f, 0.0f, 2.0f},
  };
  return kPositions;
}

quader::QuaderPolygonDocument local_transform_tetrahedron_document() {
  quader_poly::Document document;
  std::array<quader_poly::ElementId, 4> vertices;
  for (std::size_t index = 0; index < vertices.size(); ++index) {
    vertices[index] = quader_poly::add_vertex(
        document, local_transform_tetrahedron_positions()[index]);
  }
  (void)quader_poly::add_face(
      document,
      std::array{vertices[0], vertices[2], vertices[1]});
  (void)quader_poly::add_face(
      document,
      std::array{vertices[0], vertices[3], vertices[2]});
  (void)quader_poly::add_face(
      document,
      std::array{vertices[0], vertices[1], vertices[3]});
  (void)quader_poly::add_face(
      document,
      std::array{vertices[1], vertices[2], vertices[3]});
  return quader::QuaderPolygonDocumentInternalAccess::from_backend(
      std::move(document));
}

quader::QuaderComponentSelectionArgs tetrahedron_component_selection(
    quader::QuaderModelingObjectRef object, quader::QuaderSelectionMode mode,
    bool first_is_active = false) {
  quader::QuaderComponentSelectionArgs args;
  args.object = object;
  args.edit = quader::QuaderSelectionEdit::Replace;
  switch (mode) {
  case quader::QuaderSelectionMode::Vertex:
    args.kind = quader::QuaderComponentKind::Vertex;
    args.vertices = {1, 2};
    args.active = {
        .object = object,
        .kind = quader::QuaderComponentKind::Vertex,
        .vertex = first_is_active ? 1 : 2,
    };
    break;
  case quader::QuaderSelectionMode::Edge:
    args.kind = quader::QuaderComponentKind::Edge;
    args.edges = {{1, 2}, {2, 3}};
    args.active = {
        .object = object,
        .kind = quader::QuaderComponentKind::Edge,
        .edge_a = first_is_active ? 1 : 2,
        .edge_b = first_is_active ? 2 : 3,
    };
    break;
  case quader::QuaderSelectionMode::Face:
    args.kind = quader::QuaderComponentKind::Face;
    args.faces = {1, 4};
    args.active = {
        .object = object,
        .kind = quader::QuaderComponentKind::Face,
        .face = first_is_active ? 1 : 4,
    };
    break;
  case quader::QuaderSelectionMode::Object:
  case quader::QuaderSelectionMode::Group:
    break;
  }
  return args;
}

std::vector<std::size_t>
tetrahedron_selected_vertex_indices(quader::QuaderSelectionMode mode) {
  switch (mode) {
  case quader::QuaderSelectionMode::Vertex:
    return {0u, 1u};
  case quader::QuaderSelectionMode::Edge:
    return {0u, 1u, 2u};
  case quader::QuaderSelectionMode::Face:
    return {0u, 1u, 2u, 3u};
  case quader::QuaderSelectionMode::Object:
  case quader::QuaderSelectionMode::Group:
    return {};
  }
  return {};
}

bool mesh_contains_position(const quader::QuaderMeshAsset &mesh,
                            quader::QVec3 expected,
                            float epsilon = 0.002f) {
  return std::any_of(mesh.vertices.begin(), mesh.vertices.end(),
                     [expected, epsilon](const quader::QuaderMeshVertex &vertex) {
                       return quader::length(vertex.position - expected) <=
                              epsilon;
                     });
}

quader::QuaderViewportCamera local_component_drag_camera() {
  return {
      .orthographic = true,
      .origin = {0.0f, 0.0f, 8.0f},
      .x_axis = {1.0f, 0.0f, 0.0f},
      .y_axis = {0.0f, 1.0f, 0.0f},
      .z_axis = {0.0f, 0.0f, 1.0f},
      .viewport_size = {800.0f, 600.0f},
      .orthographic_size = 8.0,
      .fov_degrees = 60.0,
  };
}

quader::QuaderRay local_component_drag_ray() {
  return {
      .origin = {0.0f, 0.0f, 8.0f},
      .direction = {0.0f, 0.0f, -1.0f},
  };
}

quader::QuaderTransformDragPreview perform_local_component_drag(
    quader::test::BehaviorScenario &scenario,
    quader::QuaderTransformTool tool,
    const quader::QuaderTransform3 &orientation, quader::QVec3 pivot,
    quader::QuaderTransformAxis axis = quader::QuaderTransformAxis::X) {
  const bool scale = tool == quader::QuaderTransformTool::Scale;
  const bool rotate = tool == quader::QuaderTransformTool::Rotate;
  const quader::QVec2 screen_pivot =
      (scale || rotate) ? quader::QVec2{100.0f, 100.0f}
                        : quader::QVec2{0.0f, 0.0f};
  const quader::QVec2 screen_position =
      scale ? screen_pivot
            : (rotate ? quader::QVec2{120.0f, 100.0f}
                      : quader::QVec2{0.0f, 0.0f});
  const quader::QVec2 screen_tip =
      scale ? quader::QVec2{160.0f, 100.0f}
            : (rotate ? quader::QVec2{120.0f, 100.0f}
                      : quader::QVec2{60.0f, 0.0f});
  const quader::QVec2 update_position =
      scale ? quader::QVec2{220.0f, 100.0f}
            : (rotate ? quader::QVec2{140.0f, 80.0f}
                      : quader::QVec2{120.0f, 0.0f});
  const quader::QuaderViewportCamera camera = local_component_drag_camera();
  const quader::QuaderRay ray = local_component_drag_ray();
  const auto world_direction = axis == quader::QuaderTransformAxis::Y
                                   ? orientation.y_axis
                               : axis == quader::QuaderTransformAxis::Z
                                   ? orientation.z_axis
                                   : orientation.x_axis;

  const quader::QuaderApiResult<quader::QuaderTransformDragPreview> begin =
      scenario.api().modeling().begin_transform_drag({
          .tool = tool,
          .operation = quader::QuaderTransformOperation::Transform,
          .axis = axis,
          .space = quader::QuaderTransformSpace::Local,
          .orientation = orientation,
          .pivot = pivot,
          .screen_position = screen_position,
          .screen_pivot = screen_pivot,
          .screen_tip = screen_tip,
          .has_world_direction = true,
          .world_direction = world_direction,
          .ray = ray,
          .camera = camera,
          .grid_size = 1.0,
          .vertex_snap_radius = 0.0,
          .grid_snap_enabled = false,
          .rotation_snap_degrees = 0.0,
      });
  EXPECT_TRUE(begin.ok()) << begin.error_message();
  if (!begin.ok()) {
    return {};
  }
  const quader::QuaderApiResult<quader::QuaderTransformDragPreview> update =
      scenario.api().modeling().update_transform_drag({
          .screen_position = update_position,
          .ray = ray,
          .camera = camera,
          .grid_size = 1.0,
          .vertex_snap_radius = 0.0,
          .grid_snap_enabled = false,
          .rotation_snap_degrees = 0.0,
      });
  EXPECT_TRUE(update.ok()) << update.error_message();
  if (!update.ok()) {
    return {};
  }
  EXPECT_TRUE(update.changed());
  const quader::QuaderApiResult<quader::QuaderTransformDragPreview> commit =
      scenario.api().modeling().commit_transform_drag();
  EXPECT_TRUE(commit.ok()) << commit.error_message();
  EXPECT_TRUE(commit.changed());
  return update.value();
}

quader::QVec3 rotate_about_axis(quader::QVec3 point, quader::QVec3 pivot,
                                quader::QVec3 axis,
                                double angle_degrees) {
  axis = quader::normalize(axis);
  const quader::QVec3 relative = point - pivot;
  const double radians = angle_degrees * std::acos(-1.0) / 180.0;
  const float cosine = static_cast<float>(std::cos(radians));
  const float sine = static_cast<float>(std::sin(radians));
  return pivot + relative * cosine + quader::cross(axis, relative) * sine +
         axis * (quader::dot(axis, relative) * (1.0f - cosine));
}

/// Starts free rotation in a side view, independent of the requested local frame.
quader::QuaderTransformDragBeginRequest trackball_drag_request() {
  auto camera = local_component_drag_camera();
  camera.origin = {8.0f, 0.0f, 0.0f};
  camera.x_axis = {0.0f, 0.0f, -1.0f};
  camera.z_axis = {1.0f, 0.0f, 0.0f};
  return {
      .tool = quader::QuaderTransformTool::Rotate,
      .axis = quader::QuaderTransformAxis::All,
      .space = quader::QuaderTransformSpace::Local,
      .orientation = {.x_axis = {0.0f, 1.0f, 0.0f},
                      .y_axis = {-1.0f, 0.0f, 0.0f}},
      .pivot = kRepeatRotationWorldPivot,
      .screen_position = {100.0f, 100.0f},
      .screen_pivot = {100.0f, 100.0f},
      .screen_tip = {160.0f, 100.0f},
      .ray = {.origin = camera.origin, .direction = {-1.0f, 0.0f, 0.0f}},
      .camera = camera,
      .vertex_snap_radius = 0.0,
      .grid_snap_enabled = false,
      .rotation_snap_degrees = 0.0,
  };
}

/// Deliberately supplies a different camera to verify the basis is frozen at begin.
quader::QuaderTransformDragUpdateRequest
trackball_drag_update(quader::QVec2 position) {
  return {
      .screen_position = position,
      .ray = local_component_drag_ray(),
      .camera = local_component_drag_camera(),
      .vertex_snap_radius = 0.0,
      .grid_snap_enabled = false,
      .rotation_snap_degrees = 0.0,
  };
}

quader::QVec3 expected_local_component_position(
    quader::QVec3 point, quader::QVec3 pivot,
    const quader::QuaderTransform3 &orientation,
    const quader::QuaderTransformDragPreview &preview) {
  switch (preview.tool) {
  case quader::QuaderTransformTool::Move:
    return point + preview.move_delta;
  case quader::QuaderTransformTool::Rotate:
    return rotate_about_axis(point, pivot, orientation.x_axis,
                             preview.rotate_angle_degrees);
  case quader::QuaderTransformTool::Scale: {
    const quader::QVec3 relative = point - pivot;
    return pivot +
           orientation.x_axis *
               (quader::dot(relative, orientation.x_axis) * preview.scale.x) +
           orientation.y_axis *
               (quader::dot(relative, orientation.y_axis) * preview.scale.y) +
           orientation.z_axis *
               (quader::dot(relative, orientation.z_axis) * preview.scale.z);
  }
  }
  return point;
}

quader::QVec3 rotate_positive_quarter_turn_about(quader::QVec3 point,
                                                  quader::QVec3 pivot) {
  const quader::QVec3 relative = point - pivot;
  return {
      pivot.x - relative.y,
      pivot.y + relative.x,
      point.z,
  };
}

float rotation_radius_xy(quader::QVec3 point, quader::QVec3 pivot) {
  const float x = point.x - pivot.x;
  const float y = point.y - pivot.y;
  return std::sqrt(x * x + y * y);
}

void expect_positive_quarter_turn_step(quader::QVec3 before,
                                       quader::QVec3 after,
                                       quader::QVec3 pivot) {
  expect_vec3_near(after, rotate_positive_quarter_turn_about(before, pivot));
  EXPECT_NEAR(rotation_radius_xy(after, pivot),
              rotation_radius_xy(before, pivot), 0.001f);

  const quader::QVec3 before_relative = before - pivot;
  const quader::QVec3 after_relative = after - pivot;
  EXPECT_NEAR(before_relative.x * after_relative.x +
                  before_relative.y * after_relative.y,
              0.0f, 0.001f);
  EXPECT_GT(before_relative.x * after_relative.y -
                before_relative.y * after_relative.x,
            0.0f);
}

quader::test::DragBeginOptions
repeat_rotation_drag_options(quader::ModelingTransformOperation operation,
                             bool duplicate_selected = false) {
  return {
      .tool = quader::ModelingTransformTool::Rotate,
      .axis = quader::ModelingTransformAxis::Z,
      .operation = operation,
      .pivot = kRepeatRotationWorldPivot,
      .use_selection_center = false,
      .screen_position = {120.0f, 100.0f},
      .screen_pivot = {100.0f, 100.0f},
      .screen_tip = {120.0f, 100.0f},
      .world_direction = {0.0f, 0.0f, 1.0f},
      .grid_snap_enabled = false,
      .duplicate_selected = duplicate_selected,
  };
}

void select_repeat_rotation_target(quader::test::BehaviorScenario &s,
                                   const RepeatRotationCase &test_case) {
  if (test_case.mode == quader::QuaderSelectionMode::Object) {
    s.given().selection_mode(test_case.mode).selected("box");
    return;
  }
  s.given().edit_mode().selection_mode(test_case.mode);
  s.when().pick(test_case.ray_origin);
}

void expect_repeat_rotation_selection(quader::test::BehaviorScenario &s,
                                      quader::QuaderSelectionMode mode) {
  s.then().selection().mode(mode);
  switch (mode) {
  case quader::QuaderSelectionMode::Edge:
    s.then().selection().edge_count(1).active_alias("box");
    break;
  case quader::QuaderSelectionMode::Face:
    s.then().selection().face_count(1).active_alias("box");
    break;
  case quader::QuaderSelectionMode::Object:
    s.then().selection().object_count(1);
    break;
  case quader::QuaderSelectionMode::Vertex:
  case quader::QuaderSelectionMode::Group:
    ADD_FAILURE() << "repeat rotation test received an unsupported mode";
    break;
  }
}

int quantized_component(float value) {
  return static_cast<int>(std::lround(value * 1000.0f));
}

PointKey point_key(quader::QVec3 position) {
  return {
      quantized_component(position.x),
      quantized_component(position.y),
      quantized_component(position.z),
  };
}

EdgeKey edge_key(PointKey first, PointKey second) {
  if (second < first) {
    std::swap(first, second);
  }
  return {
      first[0], first[1], first[2],
      second[0], second[1], second[2],
  };
}

bool shared_triangle_edges_have_opposite_winding(
    const quader::QuaderMeshAsset &mesh, bool require_closed = false,
    std::size_t* open_edge_count = nullptr) {
  if (open_edge_count != nullptr) {
    *open_edge_count = 0U;
  }
  if (mesh.indices.size() % 3U != 0U) {
    return false;
  }

  std::map<EdgeKey, std::vector<TriangleEdgeUse>> uses_by_edge;
  for (std::size_t triangle = 0; triangle < mesh.indices.size();
       triangle += 3U) {
    std::array<PointKey, 3> points;
    for (std::size_t corner = 0; corner < points.size(); ++corner) {
      const std::uint32_t vertex_index = mesh.indices[triangle + corner];
      if (vertex_index >= mesh.vertices.size()) {
        return false;
      }
      points[corner] = point_key(mesh.vertices[vertex_index].position);
    }
    for (std::size_t corner = 0; corner < points.size(); ++corner) {
      const PointKey from = points[corner];
      const PointKey to = points[(corner + 1U) % points.size()];
      if (from == to) {
        return false;
      }
      uses_by_edge[edge_key(from, to)].push_back({from, to});
    }
  }

  for (const auto &[edge, uses] : uses_by_edge) {
    (void)edge;
    if (uses.size() == 1U) {
      if (open_edge_count != nullptr) {
        ++*open_edge_count;
      }
      if (require_closed) {
        return false;
      }
      continue;
    }
    if (uses.size() != 2U) {
      return false;
    }
    if (uses[0].from != uses[1].to || uses[0].to != uses[1].from) {
      return false;
    }
  }
  return true;
}

void expect_compiled_mesh_winding_coherent(quader::test::BehaviorScenario &s,
                                           std::string_view alias) {
  const std::optional<quader::QuaderMeshAsset> mesh =
      s.api().query().compiled_object_mesh(s.object(alias));
  ASSERT_TRUE(mesh.has_value());
  EXPECT_TRUE(shared_triangle_edges_have_opposite_winding(*mesh));
}

void expect_compiled_mesh_open_base_and_winding_coherent(
    quader::test::BehaviorScenario &s, std::string_view alias) {
  const std::optional<quader::QuaderMeshAsset> mesh =
      s.api().query().compiled_object_mesh(s.object(alias));
  ASSERT_TRUE(mesh.has_value());
  std::size_t open_edge_count = 0U;
  ASSERT_TRUE(shared_triangle_edges_have_opposite_winding(*mesh, false, &open_edge_count));
  EXPECT_EQ(open_edge_count, 4U);
  for (std::size_t triangle = 0; triangle < mesh->indices.size(); triangle += 3U) {
    bool has_moved_vertex = false;
    for (std::size_t corner = 0; corner < 3U; ++corner) {
      const auto index = mesh->indices[triangle + corner];
      ASSERT_LT(index, mesh->vertices.size());
      has_moved_vertex = has_moved_vertex || mesh->vertices[index].position.y > 0.0F;
    }
    EXPECT_TRUE(has_moved_vertex) << "Extrusion retained a triangle on its source base";
  }
}

void run_shift_rotate_extrude_stability_case(
    quader::test::BehaviorScenario &s, std::string_view alias) {
  const std::optional<quader::QVec3> center_before =
      s.api().query().selection_center();
  ASSERT_TRUE(center_before.has_value());
  const quader::test::BehaviorSnapshot before =
      s.snapshot("before shift rotate extrude");
  const quader::test::MeshSnapshot *before_mesh =
      mesh_snapshot_for_alias(before, alias);
  ASSERT_NE(before_mesh, nullptr);

  s.when()
      .begin_transform_drag({
          .tool = quader::ModelingTransformTool::Rotate,
          .axis = quader::ModelingTransformAxis::Z,
          .operation = quader::ModelingTransformOperation::Extrude,
          .screen_position = {120.0f, 100.0f},
          .screen_pivot = {100.0f, 100.0f},
          .screen_tip = {120.0f, 100.0f},
          .world_direction = {0.0f, 0.0f, 1.0f},
      })
      .update_transform_drag({116.0f, 112.0f})
      .commit_drag();

  s.then().command().ok_changed();
  s.then()
      .selection()
      .mode(quader::QuaderSelectionMode::Face)
      .face_count(1)
      .active_alias(alias);

  const std::optional<quader::QVec3> center_after =
      s.api().query().selection_center();
  ASSERT_TRUE(center_after.has_value());
  expect_vec3_near(*center_after, *center_before);
  s.then().mesh(alias).vertices_greater_than(*before_mesh);
}

void expect_selection_count_for_mode(quader::test::BehaviorScenario &s,
                                     quader::QuaderSelectionMode mode) {
  s.then().selection().mode(mode).active_alias("box");
  switch (mode) {
  case quader::QuaderSelectionMode::Vertex:
    s.then().selection().vertex_count(1);
    break;
  case quader::QuaderSelectionMode::Edge:
    s.then().selection().edge_count(1);
    break;
  case quader::QuaderSelectionMode::Face:
    s.then().selection().face_count(1);
    break;
  case quader::QuaderSelectionMode::Object:
  case quader::QuaderSelectionMode::Group:
    ADD_FAILURE() << "component selection test received non-component mode";
    break;
  }
}

void run_component_transform_drag_case(ComponentPickCase test_case) {
  auto s = quader::test::BehaviorScenario::new_project();
  s.given()
      .unit_box("box", "Component Drag Box")
      .edit_mode().selection_mode(test_case.mode);
  s.when().pick(test_case.ray_origin);

  const quader::test::BehaviorSnapshot before =
      s.snapshot("before component drag");
  ASSERT_EQ(before.meshes.size(), 1u);

  s.when().begin_transform_drag([&] {
    quader::test::DragBeginOptions options;
    options.tool = test_case.tool;
    if (test_case.mode == quader::QuaderSelectionMode::Vertex &&
        test_case.tool != quader::ModelingTransformTool::Move) {
      options.use_selection_center = false;
      options.pivot = {};
    }
    if (test_case.tool == quader::ModelingTransformTool::Rotate) {
      options.axis = quader::ModelingTransformAxis::Z;
      options.screen_position = {120.0f, 100.0f};
      options.screen_pivot = {100.0f, 100.0f};
      options.screen_tip = {120.0f, 100.0f};
      options.world_direction = {0.0f, 0.0f, 1.0f};
    } else if (test_case.tool == quader::ModelingTransformTool::Scale) {
      options.axis = quader::ModelingTransformAxis::X;
      options.screen_position = {200.0f, 200.0f};
      options.screen_pivot = {200.0f, 200.0f};
      options.screen_tip = {260.0f, 200.0f};
      options.world_direction = {1.0f, 0.0f, 0.0f};
    }
    return options;
  }());
  if (test_case.tool == quader::ModelingTransformTool::Rotate) {
    s.when().update_transform_drag({100.0f, 80.0f});
  } else if (test_case.tool == quader::ModelingTransformTool::Scale) {
    s.when().update_transform_drag({300.0f, 200.0f});
  } else {
    s.when().update_transform_drag(120.0f);
  }
  s.when().commit_drag();

  s.then().command().ok_changed();
  expect_selection_count_for_mode(s, test_case.mode);
  s.then()
      .mesh("box")
      .vertex_count(before.meshes.front().vertex_count)
      .vertices_differ_from(before.meshes.front());
}

} // namespace

TEST(ProgrammaticBehaviorScenarioTests,
     FaceTextureScaleUsesAbsoluteMixedValuesAcrossSelectedFaces) {
  auto s = quader::test::BehaviorScenario::new_project();
  s.given().unit_box("box").edit_mode().selection_mode(quader::QuaderSelectionMode::Face);
  auto &api = s.api();
  const auto object = s.object("box");
  ASSERT_TRUE(api.selection().select_face(object, 1).ok());
  ASSERT_TRUE(edit_face_texture(api, quader::QuaderFaceTextureField::ScaleY, 2).ok());
  ASSERT_TRUE(api.selection().select_face(object, 2).ok());
  ASSERT_TRUE(edit_face_texture(api, quader::QuaderFaceTextureField::ScaleY, 4).ok());
  ASSERT_TRUE(api.selection().select_faces(object, {1, 2}).ok());
  auto summary = api.modeling().face_texture_settings();
  ASSERT_EQ(summary.selected_face_count, 2u);
  EXPECT_TRUE(summary.editable);
  ASSERT_TRUE(summary.scale_x.has_value());
  EXPECT_DOUBLE_EQ(*summary.scale_x, 1);
  EXPECT_FALSE(summary.scale_y.has_value());
  const auto selection_revision = api.revisions().modeling_selection_revision;
  ASSERT_TRUE(edit_face_texture(api, quader::QuaderFaceTextureField::ScaleX, 5).ok());
  EXPECT_EQ(api.revisions().modeling_selection_revision, selection_revision);
  ASSERT_TRUE(edit_face_texture(api, quader::QuaderFaceTextureField::ScaleY, 0.5).ok());
  summary = api.modeling().face_texture_settings();
  EXPECT_EQ(summary.scale_x, 5.0);
  EXPECT_EQ(summary.scale_y, 0.5);

  for (const int face : {1, 2}) {
    ASSERT_TRUE(api.selection().select_face(object, face).ok());
    summary = api.modeling().face_texture_settings();
    EXPECT_EQ(summary.scale_x, 5.0);
    EXPECT_EQ(summary.scale_y, 0.5);
  }
  ASSERT_TRUE(api.selection().select_face(object, 3).ok());
  summary = api.modeling().face_texture_settings();
  EXPECT_EQ(summary.scale_x, 1.0);
  EXPECT_EQ(summary.scale_y, 1.0);
}

TEST(ProgrammaticBehaviorScenarioTests,
     FaceTextureRotationUsesAbsoluteAnglesAndOneUndoPerGesture) {
  auto s = quader::test::BehaviorScenario::new_project();
  auto &api = s.api();
  const auto created = api.modeling().create_box(
      {.min = {-1, -1, -1}, .max = {1, 1, 1}}, "Texture Rotation");
  ASSERT_TRUE(created.ok());
  const auto object = created.value();
  ASSERT_TRUE(s.editor().selection().select_all().ok());
  s.given().edit_mode();
  ASSERT_TRUE(api.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(api.selection().select_face(object, 1).ok());
  ASSERT_TRUE(edit_face_texture(api, quader::QuaderFaceTextureField::ShiftX, 16).ok());
  ASSERT_TRUE(edit_face_texture(api, quader::QuaderFaceTextureField::ShiftY, -8).ok());
  ASSERT_TRUE(edit_face_texture(api, quader::QuaderFaceTextureField::Rotate, 30).ok());
  EXPECT_NEAR(api.modeling().face_texture_settings().rotation_degrees, 30, 1.0e-4);
  ASSERT_TRUE(api.selection().select_face(object, 2).ok());
  ASSERT_TRUE(edit_face_texture(api, quader::QuaderFaceTextureField::ShiftX, -4).ok());
  ASSERT_TRUE(edit_face_texture(api, quader::QuaderFaceTextureField::ShiftY, 12).ok());
  ASSERT_TRUE(edit_face_texture(api, quader::QuaderFaceTextureField::Rotate, -15).ok());
  EXPECT_NEAR(api.modeling().face_texture_settings().rotation_degrees, -15, 1.0e-4);
  ASSERT_TRUE(api.selection().select_faces(object, {1, 2}).ok());
  EXPECT_TRUE(api.modeling().face_texture_settings().rotation_mixed);
  const auto before_gesture = api.query().compiled_object_mesh(object);
  ASSERT_TRUE(before_gesture.has_value());

  ASSERT_TRUE(edit_face_texture(api, quader::QuaderFaceTextureField::Rotate, 10,
                               "texture.rotation.gesture").ok());
  ASSERT_TRUE(edit_face_texture(api, quader::QuaderFaceTextureField::Rotate, 20,
                               "texture.rotation.gesture").ok());
  EXPECT_FLOAT_EQ(face_texture(api, object, 1).rotation_degrees, 20);
  EXPECT_FLOAT_EQ(face_texture(api, object, 2).rotation_degrees, 20);
  EXPECT_FALSE(api.modeling().face_texture_settings().rotation_mixed);
  EXPECT_NEAR(api.modeling().face_texture_settings().rotation_degrees, 20, 1.0e-4);
  const auto after_gesture = api.query().compiled_object_mesh(object);
  ASSERT_TRUE(after_gesture.has_value());
  ASSERT_TRUE(api.history().undo().ok());
  EXPECT_FLOAT_EQ(face_texture(api, object, 1).rotation_degrees, 30);
  EXPECT_FLOAT_EQ(face_texture(api, object, 2).rotation_degrees, -15);
  EXPECT_TRUE(api.modeling().face_texture_settings().rotation_mixed);
  expect_texture_mesh_equal(api.query().compiled_object_mesh(object), *before_gesture);
  ASSERT_TRUE(api.history().redo().ok());
  EXPECT_FALSE(api.modeling().face_texture_settings().rotation_mixed);
  EXPECT_NEAR(api.modeling().face_texture_settings().rotation_degrees, 20, 1.0e-4);
  expect_texture_mesh_equal(api.query().compiled_object_mesh(object), *after_gesture);

  ASSERT_TRUE(edit_face_texture(api, quader::QuaderFaceTextureField::ScaleX, 2).ok());
  EXPECT_NEAR(api.modeling().face_texture_settings().rotation_degrees, 20, 1.0e-4);
  ASSERT_TRUE(edit_face_texture(api, quader::QuaderFaceTextureField::Rotate, 25).ok());
  EXPECT_FLOAT_EQ(face_texture(api, object, 1).rotation_degrees, 25);
  EXPECT_FLOAT_EQ(face_texture(api, object, 2).rotation_degrees, 25);
  ASSERT_TRUE(api.modeling().face_texture_settings().scale_x.has_value());
  EXPECT_NEAR(*api.modeling().face_texture_settings().scale_x, 2, 1.0e-5);

  const auto before_selection = api.modeling().face_texture_settings();
  ASSERT_TRUE(api.selection().select_face(object, 2, quader::QuaderSelectionEdit::Remove).ok());
  EXPECT_NEAR(api.modeling().face_texture_settings().rotation_degrees, 25, 1.0e-4);
  EXPECT_NEAR(api.modeling().face_texture_settings().shift_x, 16, 1.0e-4);
  EXPECT_NEAR(api.modeling().face_texture_settings().shift_y, -8, 1.0e-4);
  EXPECT_FALSE(api.modeling().set_face_texture_setting({
      .field = quader::QuaderFaceTextureField::Rotate,
      .value = 90,
      .selection_token = before_selection.selection_token,
  }).ok());
  EXPECT_FLOAT_EQ(face_texture(api, object, 1).rotation_degrees, 25);
  ASSERT_TRUE(api.selection().select_face(object, 2, quader::QuaderSelectionEdit::Add).ok());
  EXPECT_FALSE(api.modeling().face_texture_settings().rotation_mixed);
  EXPECT_NEAR(api.modeling().face_texture_settings().rotation_degrees, 25, 1.0e-4);
  EXPECT_FLOAT_EQ(face_texture(api, object, 2).rotation_degrees, 25);
  ASSERT_TRUE(api.selection().select_face(object, 2).ok());
  EXPECT_NEAR(api.modeling().face_texture_settings().rotation_degrees, 25, 1.0e-4);
  EXPECT_NEAR(api.modeling().face_texture_settings().shift_x, -4, 1.0e-4);
  EXPECT_NEAR(api.modeling().face_texture_settings().shift_y, 12, 1.0e-4);
  ASSERT_TRUE(api.selection().clear().ok());
  EXPECT_EQ(api.modeling().face_texture_settings().selected_face_count, 0u);

  // Whole turns normalize in the readout; fractional absolute edits still
  // share one selection epoch and one history gesture across both faces.
  ASSERT_TRUE(api.selection().select_face(object, 1).ok());
  ASSERT_TRUE(edit_face_texture(api, quader::QuaderFaceTextureField::Rotate, 0).ok());
  ASSERT_TRUE(api.selection().select_face(object, 2).ok());
  ASSERT_TRUE(edit_face_texture(api, quader::QuaderFaceTextureField::Rotate, 1000).ok());
  EXPECT_NEAR(api.modeling().face_texture_settings().rotation_degrees, -80, 1.0e-4);
  ASSERT_TRUE(api.selection().select_faces(object, {1, 2}).ok());
  EXPECT_TRUE(api.modeling().face_texture_settings().rotation_mixed);
  const auto before_fractional = api.query().compiled_object_mesh(object);
  ASSERT_TRUE(before_fractional.has_value());
  const auto fractional_token = api.modeling().face_texture_settings().selection_token;
  ASSERT_TRUE(edit_face_texture(api, quader::QuaderFaceTextureField::Rotate, 0.1,
                               "texture.fractional.rotation.gesture").ok());
  EXPECT_EQ(api.modeling().face_texture_settings().selection_token, fractional_token);
  EXPECT_FALSE(api.modeling().face_texture_settings().rotation_mixed);
  EXPECT_NEAR(api.modeling().face_texture_settings().rotation_degrees, 0.1, 1.0e-4);
  ASSERT_TRUE(edit_face_texture(api, quader::QuaderFaceTextureField::Rotate, 0.2,
                               "texture.fractional.rotation.gesture").ok());
  EXPECT_EQ(api.modeling().face_texture_settings().selection_token, fractional_token);
  EXPECT_FALSE(api.modeling().face_texture_settings().rotation_mixed);
  EXPECT_NEAR(api.modeling().face_texture_settings().rotation_degrees, 0.2, 1.0e-4);
  EXPECT_FLOAT_EQ(face_texture(api, object, 1).rotation_degrees, 0.2f);
  EXPECT_FLOAT_EQ(face_texture(api, object, 2).rotation_degrees, 0.2f);
  const auto after_fractional = api.query().compiled_object_mesh(object);
  ASSERT_TRUE(after_fractional.has_value());
  ASSERT_TRUE(api.history().undo().ok());
  EXPECT_FLOAT_EQ(face_texture(api, object, 1).rotation_degrees, 0.0f);
  EXPECT_FLOAT_EQ(face_texture(api, object, 2).rotation_degrees, 1000.0f);
  EXPECT_TRUE(api.modeling().face_texture_settings().rotation_mixed);
  expect_texture_mesh_equal(api.query().compiled_object_mesh(object), *before_fractional);
  ASSERT_TRUE(api.history().redo().ok());
  EXPECT_FLOAT_EQ(face_texture(api, object, 1).rotation_degrees, 0.2f);
  EXPECT_FLOAT_EQ(face_texture(api, object, 2).rotation_degrees, 0.2f);
  EXPECT_FALSE(api.modeling().face_texture_settings().rotation_mixed);
  EXPECT_NEAR(api.modeling().face_texture_settings().rotation_degrees, 0.2, 1.0e-4);
  expect_texture_mesh_equal(api.query().compiled_object_mesh(object), *after_fractional);
}

TEST(ProgrammaticBehaviorScenarioTests,
     FaceTextureShiftIsSingleFacePixelsAndInvalidEditsPreserveState) {
  auto s = quader::test::BehaviorScenario::new_project();
  auto &api = s.api();
  const auto created = api.modeling().create_box(
      {.min = {-1, -1, -1}, .max = {1, 1, 1}}, "Texture Shift");
  ASSERT_TRUE(created.ok());
  const auto object = created.value();
  ASSERT_TRUE(s.editor().selection().select_all().ok());
  s.given().edit_mode();
  ASSERT_TRUE(api.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(api.selection().select_face(object, 1).ok());
  ASSERT_TRUE(edit_face_texture(api, quader::QuaderFaceTextureField::ShiftX, 16).ok());
  ASSERT_TRUE(edit_face_texture(api, quader::QuaderFaceTextureField::ShiftY, -8).ok());
  EXPECT_FLOAT_EQ(face_texture(api, object, 1).shift_uv.x, 0.25f);
  EXPECT_FLOAT_EQ(face_texture(api, object, 1).shift_uv.y, -0.125f);
  ASSERT_TRUE(edit_face_texture(api, quader::QuaderFaceTextureField::Rotate, 45).ok());
  ASSERT_TRUE(edit_face_texture(api, quader::QuaderFaceTextureField::ScaleX, -2).ok());
  const auto texture = face_texture(api, object, 1);
  EXPECT_FLOAT_EQ(texture.shift_uv.x, 0.25f);
  EXPECT_FLOAT_EQ(texture.shift_uv.y, -0.125f);
  const auto settings = api.modeling().face_texture_settings();
  EXPECT_NEAR(settings.rotation_degrees, 45, 1.0e-4);
  EXPECT_NEAR(settings.shift_x, 16, 1.0e-4);
  EXPECT_NEAR(settings.shift_y, -8, 1.0e-4);
  ASSERT_TRUE(settings.scale_x.has_value());
  EXPECT_NEAR(*settings.scale_x, -2, 1.0e-4);

  const auto before = api.revisions();
  EXPECT_FALSE(edit_face_texture(api, quader::QuaderFaceTextureField::ScaleY, 0).ok());
  EXPECT_FALSE(edit_face_texture(api, quader::QuaderFaceTextureField::Rotate,
                                std::numeric_limits<double>::infinity()).ok());
  EXPECT_FALSE(edit_face_texture(api, quader::QuaderFaceTextureField::ShiftY,
                                std::numeric_limits<double>::quiet_NaN()).ok());
  EXPECT_EQ(api.revisions().modeling_content_revision, before.modeling_content_revision);
  EXPECT_EQ(api.revisions().scene_revision, before.scene_revision);
  EXPECT_EQ(api.revisions().asset_revision, before.asset_revision);
  ASSERT_TRUE(api.selection().select_faces(object, {1, 2}).ok());
  const auto before_shift = api.revisions().modeling_content_revision;
  EXPECT_FALSE(edit_face_texture(api, quader::QuaderFaceTextureField::ShiftX, 50).ok());
  EXPECT_EQ(api.revisions().modeling_content_revision, before_shift);
  EXPECT_FLOAT_EQ(face_texture(api, object, 1).shift_uv.x, 0.25f);
  EXPECT_FLOAT_EQ(face_texture(api, object, 2).shift_uv.x, 0);

  ASSERT_TRUE(api.modeling().begin_transform_preview().ok());
  const auto blocked = api.modeling().face_texture_settings();
  EXPECT_FALSE(blocked.editable);
  EXPECT_EQ(blocked.block_reason, quader::QuaderFaceTextureBlockReason::PreviewActive);
  EXPECT_FALSE(edit_face_texture(api, quader::QuaderFaceTextureField::ScaleX, 3).ok());
  EXPECT_TRUE(api.modeling().can_cancel_active_tool_or_preview());
  ASSERT_TRUE(api.modeling().cancel_transform_preview().ok());
  EXPECT_TRUE(api.modeling().face_texture_settings().editable);
}

TEST(ProgrammaticBehaviorScenarioTests,
     FaceTexturePublicScalePreservesLockedPivotCompensation) {
  auto s = quader::test::BehaviorScenario::new_project();
  auto &api = s.api();
  s.editor().preferences().set_runtime_hooks(quader::QuaderPreferencesRuntimeHooks{
      .texture_lock_enabled = [] { return false; },
      .texture_lock_scale_enabled = [] { return true; },
  });
  const auto created = api.modeling().create_box(
      {.min = {-1, -1, -1}, .max = {1, 1, 1}}, "Locked Pivot Scale");
  ASSERT_TRUE(created.ok());
  ASSERT_TRUE(api.selection().select_object(created.value()).ok());
  ASSERT_TRUE(api.modeling().scale_selection({2, 2, 2}, {5, 3, 0}).ok());
  const auto transformed = api.query().entity(created.value().entity);
  ASSERT_TRUE(transformed.has_value());
  const quader::QMat4 identity{};
  const auto mapping = transformed->texture_sampling_from_local.value_or(identity);
  for (std::size_t index = 0; index < identity.values.size(); ++index) {
    EXPECT_NEAR(mapping.values[index], identity.values[index], 0.00001f);
  }
  ASSERT_TRUE(api.history().undo().ok());
  ASSERT_TRUE(api.history().redo().ok());
  const auto redone = api.query().entity(created.value().entity);
  ASSERT_TRUE(redone.has_value());
  const auto redone_mapping = redone->texture_sampling_from_local.value_or(identity);
  for (std::size_t index = 0; index < identity.values.size(); ++index) {
    EXPECT_NEAR(redone_mapping.values[index], identity.values[index], 0.00001f);
  }
}

TEST(ProgrammaticBehaviorScenarioTests,
     FaceTextureActionRotationCapturesDisabledSnapAngleAndRepeatsAfterUndoRedo) {
  auto s = quader::test::BehaviorScenario::new_project();
  s.given().unit_box("box").edit_mode().selection_mode(quader::QuaderSelectionMode::Face);
  auto &api = s.api();
  const auto object = s.object("box");
  double rotation_snap_degrees = 22.5;
  s.editor().preferences().set_runtime_hooks(quader::QuaderPreferencesRuntimeHooks{
      .rotation_snap_enabled = [] { return false; },
      .rotation_snap_degrees = [&] { return rotation_snap_degrees; },
  });
  ASSERT_TRUE(api.selection().select_face(object, 1).ok());
  ASSERT_TRUE(edit_face_texture(api, quader::QuaderFaceTextureField::Rotate,
                               30.0).ok());
  ASSERT_TRUE(api.selection().select_face(object, 2).ok());
  ASSERT_TRUE(edit_face_texture(api, quader::QuaderFaceTextureField::Rotate,
                               -15.0).ok());
  ASSERT_TRUE(api.selection().select_faces(object, {1, 2}).ok());
  const auto token = api.modeling().face_texture_settings().selection_token;

  const auto rotated = apply_face_texture_action(
      api, quader::QuaderFaceTextureAction::RotateClockwise);
  ASSERT_TRUE(rotated.ok()) << rotated.error_message();
  ASSERT_TRUE(rotated.changed());
  EXPECT_FLOAT_EQ(face_texture(api, object, 1).rotation_degrees, 7.5f);
  EXPECT_FLOAT_EQ(face_texture(api, object, 2).rotation_degrees, -37.5f);
  EXPECT_EQ(api.modeling().face_texture_settings().selection_token, token);
  EXPECT_TRUE(api.modeling().face_texture_settings().rotation_mixed);
  EXPECT_EQ(api.history().snapshot().undo_label, "Rotate Texture Clockwise");

  rotation_snap_degrees = 90.0;
  const auto repeated = api.history().repeat_last_action();
  ASSERT_TRUE(repeated.ok()) << repeated.error_message();
  ASSERT_TRUE(repeated.changed());
  EXPECT_FLOAT_EQ(face_texture(api, object, 1).rotation_degrees, -15.0f);
  EXPECT_FLOAT_EQ(face_texture(api, object, 2).rotation_degrees, -60.0f);
  EXPECT_TRUE(api.modeling().face_texture_settings().rotation_mixed);
  ASSERT_TRUE(api.history().undo().ok());
  EXPECT_FLOAT_EQ(face_texture(api, object, 1).rotation_degrees, 7.5f);
  EXPECT_FLOAT_EQ(face_texture(api, object, 2).rotation_degrees, -37.5f);
  EXPECT_TRUE(api.modeling().face_texture_settings().rotation_mixed);
  ASSERT_TRUE(api.history().redo().ok());
  EXPECT_TRUE(api.modeling().face_texture_settings().rotation_mixed);

  ASSERT_TRUE(api.selection().select_face(object, 2).ok());
  const auto before_repeat = api.modeling().face_texture_settings();
  EXPECT_FALSE(before_repeat.rotation_mixed);
  EXPECT_NEAR(before_repeat.rotation_degrees,
              api.modeling().face_texture_canvas().rotation_degrees, 1.0e-4);
  const auto new_selection_repeat = api.history().repeat_last_action();
  ASSERT_TRUE(new_selection_repeat.ok()) << new_selection_repeat.error_message();
  EXPECT_FLOAT_EQ(face_texture(api, object, 1).rotation_degrees, -15.0f);
  EXPECT_FLOAT_EQ(face_texture(api, object, 2).rotation_degrees, -82.5f);
  const double repeated_angle = api.modeling().face_texture_settings().rotation_degrees;
  EXPECT_NEAR(std::abs(std::remainder(
                  repeated_angle - before_repeat.rotation_degrees, 360.0)),
              22.5, 1.0e-4);
  EXPECT_NEAR(repeated_angle,
              api.modeling().face_texture_canvas().rotation_degrees, 1.0e-4);
  ASSERT_TRUE(api.history().undo().ok());
  EXPECT_NEAR(api.modeling().face_texture_settings().rotation_degrees,
              before_repeat.rotation_degrees, 1.0e-4);
  const auto repeat_after_undo = api.history().repeat_last_action();
  ASSERT_TRUE(repeat_after_undo.ok()) << repeat_after_undo.error_message();
  EXPECT_FLOAT_EQ(face_texture(api, object, 2).rotation_degrees, -82.5f);
  EXPECT_NEAR(api.modeling().face_texture_settings().rotation_degrees,
              repeated_angle, 1.0e-4);
}

TEST(ProgrammaticBehaviorScenarioTests,
     FaceTextureActionClicksCreateSeparateAtomicMultiObjectUndoEntries) {
  auto s = quader::test::BehaviorScenario::new_project();
  s.given().unit_box("first").unit_box("second")
      .selected_aliases({"first", "second"}, "second").edit_mode().selection_mode(quader::QuaderSelectionMode::Face);
  auto &api = s.api();
  const auto first = s.object("first");
  const auto second = s.object("second");
  ASSERT_TRUE(api.selection().select_face(first, 1).ok());
  ASSERT_TRUE(api.selection().select_face(
      second, 1, quader::QuaderSelectionEdit::Add).ok());
  ASSERT_EQ(api.modeling().face_texture_settings().selected_face_count, 2u);
  const auto before_first = api.query().compiled_object_mesh(first);
  const auto before_second = api.query().compiled_object_mesh(second);
  ASSERT_TRUE(before_first.has_value());
  ASSERT_TRUE(before_second.has_value());
  const auto selection_revision = api.revisions().modeling_selection_revision;
  for (int index = 0; index < 2; ++index) {
    const auto shifted = apply_face_texture_action(
        api, quader::QuaderFaceTextureAction::ShiftRight);
    ASSERT_TRUE(shifted.ok()) << shifted.error_message();
    ASSERT_TRUE(shifted.changed());
    EXPECT_EQ(api.history().snapshot().undo_label, "Shift Texture Right");
  }
  EXPECT_EQ(api.revisions().modeling_selection_revision, selection_revision);
  const auto after_first = api.query().compiled_object_mesh(first);
  const auto after_second = api.query().compiled_object_mesh(second);
  ASSERT_TRUE(after_first.has_value());
  ASSERT_TRUE(after_second.has_value());
  for (const auto object : {first, second}) {
    EXPECT_FLOAT_EQ(face_texture(api, object, 1).shift_uv.x, -2.0f / 64.0f);
    EXPECT_FLOAT_EQ(face_texture(api, object, 2).shift_uv.x, 0.0f);
  }

  ASSERT_TRUE(api.history().undo().ok());
  for (const auto object : {first, second}) {
    EXPECT_FLOAT_EQ(face_texture(api, object, 1).shift_uv.x, -1.0f / 64.0f);
  }
  ASSERT_TRUE(api.history().undo().ok());
  expect_texture_mesh_equal(api.query().compiled_object_mesh(first), *before_first);
  expect_texture_mesh_equal(api.query().compiled_object_mesh(second), *before_second);
  EXPECT_EQ(api.selection().summary().selected_face_count, 2u);
  ASSERT_TRUE(api.history().redo().ok());
  for (const auto object : {first, second}) {
    EXPECT_FLOAT_EQ(face_texture(api, object, 1).shift_uv.x, -1.0f / 64.0f);
  }
  ASSERT_TRUE(api.history().redo().ok());
  expect_texture_mesh_equal(api.query().compiled_object_mesh(first), *after_first);
  expect_texture_mesh_equal(api.query().compiled_object_mesh(second), *after_second);
}

TEST(ProgrammaticBehaviorScenarioTests,
     FaceTextureActionStaleAndInvalidRequestsPreserveMeshesRevisionsAndRedo) {
  auto s = quader::test::BehaviorScenario::new_project();
  s.given().unit_box("first").unit_box("second")
      .selected_aliases({"first", "second"}, "second").edit_mode().selection_mode(quader::QuaderSelectionMode::Face);
  auto &api = s.api();
  const auto first = s.object("first");
  const auto second = s.object("second");
  ASSERT_TRUE(api.selection().select_face(first, 1).ok());
  const auto stale_token = api.modeling().face_texture_settings().selection_token;
  ASSERT_TRUE(api.selection().select_face(second, 1).ok());
  ASSERT_TRUE(edit_face_texture(api, quader::QuaderFaceTextureField::ScaleX,
                               0.1).ok());
  ASSERT_TRUE(api.selection().select_face(
      first, 1, quader::QuaderSelectionEdit::Add).ok());
  ASSERT_TRUE(apply_face_texture_action(
      api, quader::QuaderFaceTextureAction::ShiftRight).ok());
  ASSERT_TRUE(api.history().undo().ok());
  ASSERT_TRUE(api.history().can_redo());
  const auto before_first = api.query().compiled_object_mesh(first);
  const auto before_second = api.query().compiled_object_mesh(second);
  ASSERT_TRUE(before_first.has_value());
  ASSERT_TRUE(before_second.has_value());
  const auto before_revisions = api.revisions();
  const auto before_history = api.history().snapshot();

  const auto stale = api.modeling().apply_face_texture_action({
      .action = quader::QuaderFaceTextureAction::ShiftRight,
      .selection_token = stale_token,
  });
  EXPECT_FALSE(stale.ok());
  EXPECT_FALSE(stale.changed());
  const auto invalid = edit_face_texture(
      api, quader::QuaderFaceTextureField::ScaleX, 0.0);
  EXPECT_FALSE(invalid.ok());
  EXPECT_FALSE(invalid.changed());
  expect_document_revisions_equal(api.revisions(), before_revisions);
  expect_texture_mesh_equal(api.query().compiled_object_mesh(first), *before_first);
  expect_texture_mesh_equal(api.query().compiled_object_mesh(second), *before_second);
  const auto after_history = api.history().snapshot();
  EXPECT_EQ(after_history.undo_label, before_history.undo_label);
  EXPECT_EQ(after_history.redo_label, before_history.redo_label);
  EXPECT_EQ(after_history.can_redo, before_history.can_redo);

  ASSERT_TRUE(api.modeling().begin_transform_preview().ok());
  const auto preview_revisions = api.revisions();
  const auto blocked = apply_face_texture_action(
      api, quader::QuaderFaceTextureAction::ScaleYUp);
  EXPECT_FALSE(blocked.ok());
  EXPECT_FALSE(blocked.changed());
  expect_document_revisions_equal(api.revisions(), preview_revisions);
  EXPECT_TRUE(api.modeling().can_cancel_active_tool_or_preview());
  ASSERT_TRUE(api.modeling().cancel_transform_preview().ok());
  EXPECT_TRUE(api.history().can_redo());
}

TEST(ProgrammaticBehaviorScenarioTests,
     FaceTextureTreatAsOnePersistsAcrossSelectionWithoutDocumentHistory) {
  auto s = quader::test::BehaviorScenario::new_project();
  auto &api = s.api();
  EXPECT_FALSE(api.modeling().face_texture_treat_as_one());
  EXPECT_FALSE(api.modeling().face_texture_settings().treat_as_one);
  EXPECT_FALSE(s.editor().project().dirty());
  const auto before_revisions = api.revisions();
  const auto before_presentation = api.modeling().face_texture_settings_revision();
  const auto enabled = api.modeling().set_face_texture_treat_as_one(true);
  ASSERT_TRUE(enabled.ok()) << enabled.error_message();
  EXPECT_TRUE(enabled.changed());
  EXPECT_TRUE(api.modeling().face_texture_treat_as_one());
  EXPECT_TRUE(api.modeling().face_texture_settings().treat_as_one);
  expect_document_revisions_equal(api.revisions(), before_revisions);
  EXPECT_NE(api.modeling().face_texture_settings_revision(), before_presentation);
  EXPECT_FALSE(s.editor().project().dirty());
  EXPECT_FALSE(api.history().can_undo());
  const auto enabled_presentation = api.modeling().face_texture_settings_revision();
  EXPECT_FALSE(api.modeling().set_face_texture_treat_as_one(true).changed());
  EXPECT_EQ(api.modeling().face_texture_settings_revision(), enabled_presentation);

  s.given().unit_box("box").edit_mode().selection_mode(quader::QuaderSelectionMode::Face);
  ASSERT_TRUE(api.selection().select_face(s.object("box"), 1).ok());
  EXPECT_TRUE(api.modeling().face_texture_settings().treat_as_one);
  ASSERT_TRUE(api.selection().clear().ok());
  EXPECT_TRUE(api.modeling().face_texture_treat_as_one());
  ASSERT_TRUE(api.selection().select_faces(s.object("box"), {1, 2}).ok());
  EXPECT_TRUE(api.modeling().face_texture_settings().treat_as_one);
  const auto history_before_toggle = api.history().snapshot();
  const auto revisions_before_toggle = api.revisions();
  ASSERT_TRUE(api.modeling().set_face_texture_treat_as_one(false).changed());
  expect_document_revisions_equal(api.revisions(), revisions_before_toggle);
  EXPECT_EQ(api.history().snapshot().undo_label, history_before_toggle.undo_label);
  EXPECT_EQ(api.history().snapshot().redo_label, history_before_toggle.redo_label);
  ASSERT_TRUE(api.modeling().set_face_texture_treat_as_one(true).changed());
  EXPECT_TRUE(api.modeling().face_texture_treat_as_one());
}

TEST(ProgrammaticBehaviorScenarioTests,
     FaceTextureActionGroupedFitSpansWorldSpaceGapsAndCapturesTreatAsOneForRepeat) {
  auto s = quader::test::BehaviorScenario::new_project();
  s.given().mesh("left", face_texture_wall_document())
      .mesh("right", face_texture_wall_document())
      .transform("right", {.location = {4.0f, 0.0f, 0.0f}})
      .selected_aliases({"left", "right"}, "right").edit_mode().selection_mode(quader::QuaderSelectionMode::Face);
  auto &api = s.api();
  const auto left = s.object("left");
  const auto right = s.object("right");
  ASSERT_TRUE(api.selection().select_face(right, 1).ok());
  ASSERT_TRUE(api.selection().select_face(
      left, 1, quader::QuaderSelectionEdit::Add).ok());
  ASSERT_EQ(api.selection().summary().active_object_id, left.modeling_object_id);
  ASSERT_TRUE(apply_face_texture_action(
      api, quader::QuaderFaceTextureAction::FitBoth).ok());
  expect_texture_uv_bounds(api.query().compiled_object_mesh(left), 0.0f, 1.0f);
  expect_texture_uv_bounds(api.query().compiled_object_mesh(right), 0.0f, 1.0f);
  const auto individual_left = api.query().compiled_object_mesh(left);
  const auto individual_right = api.query().compiled_object_mesh(right);
  ASSERT_TRUE(individual_left.has_value());
  ASSERT_TRUE(individual_right.has_value());

  ASSERT_TRUE(api.modeling().set_face_texture_treat_as_one(true).ok());
  const auto fitted = apply_face_texture_action(
      api, quader::QuaderFaceTextureAction::FitBoth);
  ASSERT_TRUE(fitted.ok()) << fitted.error_message();
  ASSERT_TRUE(fitted.changed());
  expect_texture_uv_bounds(api.query().compiled_object_mesh(left), 0.0f, 1.0f / 3.0f);
  expect_texture_uv_bounds(api.query().compiled_object_mesh(right), 2.0f / 3.0f, 1.0f);
  const auto grouped_left = api.query().compiled_object_mesh(left);
  const auto grouped_right = api.query().compiled_object_mesh(right);
  ASSERT_TRUE(grouped_left.has_value());
  ASSERT_TRUE(grouped_right.has_value());

  ASSERT_TRUE(api.modeling().set_face_texture_treat_as_one(false).ok());
  ASSERT_TRUE(api.history().undo().ok());
  EXPECT_FALSE(api.modeling().face_texture_treat_as_one());
  expect_texture_mesh_equal(api.query().compiled_object_mesh(left), *individual_left);
  expect_texture_mesh_equal(api.query().compiled_object_mesh(right), *individual_right);
  const auto repeated = api.history().repeat_last_action();
  ASSERT_TRUE(repeated.ok()) << repeated.error_message();
  EXPECT_TRUE(repeated.changed());
  expect_texture_mesh_equal(api.query().compiled_object_mesh(left), *grouped_left);
  expect_texture_mesh_equal(api.query().compiled_object_mesh(right), *grouped_right);
  EXPECT_FALSE(api.modeling().face_texture_treat_as_one());

  ASSERT_TRUE(api.modeling().set_face_texture_treat_as_one(true).ok());
  ASSERT_TRUE(apply_face_texture_action(
      api, quader::QuaderFaceTextureAction::ShiftRight).ok());
  ASSERT_TRUE(api.history().undo().ok());
  const auto before_noop_revisions = api.revisions();
  const auto before_noop_history = api.history().snapshot();
  ASSERT_TRUE(before_noop_history.can_redo);
  const auto noop = apply_face_texture_action(
      api, quader::QuaderFaceTextureAction::FitBoth);
  ASSERT_TRUE(noop.ok()) << noop.error_message();
  EXPECT_FALSE(noop.changed());
  expect_document_revisions_equal(api.revisions(), before_noop_revisions);
  EXPECT_EQ(api.history().snapshot().undo_label, before_noop_history.undo_label);
  EXPECT_EQ(api.history().snapshot().redo_label, before_noop_history.redo_label);
  EXPECT_TRUE(api.history().can_redo());
}

TEST(ProgrammaticBehaviorScenarioTests,
     FaceTextureActionInstanceScopePreservesOccurrenceSamplingAndSharedDefinition) {
  auto s = quader::test::BehaviorScenario::new_project();
  s.given().mesh("wall", face_texture_wall_document()).selected("wall");
  auto &api = s.api();
  const auto member = s.object("wall");
  const auto created = api.modeling().create_instance_from_selection();
  ASSERT_TRUE(created.ok()) << created.error_message();
  const auto first_occurrence = created.value().occurrence.root.id;
  const auto definition = created.value().definition.root.id;
  ASSERT_TRUE(api.scene().set_transform(first_occurrence, {
      .location = {5.0f, 2.0f, 0.0f},
      .scale = {2.0f, 3.0f, 1.0f},
  }).ok());
  const auto first_snapshot = api.query().entity(first_occurrence);
  ASSERT_TRUE(first_snapshot.has_value());
  ASSERT_TRUE(first_snapshot->texture_sampling_from_local.has_value());
  const auto sampling = *first_snapshot->texture_sampling_from_local;
  const auto duplicate = api.scene().duplicate_selected_hierarchy();
  ASSERT_TRUE(duplicate.ok()) << duplicate.error_message();
  ASSERT_EQ(duplicate.value().root_entities.size(), 1u);
  const auto second_occurrence = duplicate.value().root_entities.front().id;
  ASSERT_NE(second_occurrence, first_occurrence);

  const auto face_selection_mode =
      api.selection().set_mode(quader::QuaderSelectionMode::Face);
  ASSERT_TRUE(face_selection_mode.ok());
  EXPECT_TRUE(face_selection_mode.changed());
  EXPECT_EQ(api.selection().summary().mode, quader::QuaderSelectionMode::Face);
  EXPECT_EQ(api.selection().summary().selected_object_count, 0u);
  EXPECT_FALSE(api.selection().select_face(member, 1).ok());
  EXPECT_FALSE(api.modeling().face_texture_settings().editable);
  const auto blocked = apply_face_texture_action(
      api, quader::QuaderFaceTextureAction::FitBoth);
  EXPECT_FALSE(blocked.ok());
  EXPECT_FALSE(blocked.changed());
  ASSERT_TRUE(api.selection().set_mode(quader::QuaderSelectionMode::Object).ok());
  const auto entered = api.modeling().enter_instance_edit(second_occurrence);
  ASSERT_TRUE(entered.ok()) << entered.error_message();
  ASSERT_TRUE(api.selection().select_object(member).ok());
  s.given().edit_mode();
  const auto selected = api.selection().select_face(member, 1);
  ASSERT_TRUE(selected.ok()) << selected.error_message();
  EXPECT_EQ(api.selection().summary().mode, quader::QuaderSelectionMode::Face);
  EXPECT_EQ(api.selection().summary().selected_face_count, 1u);
  ASSERT_TRUE(api.modeling().face_texture_settings().editable);
  const auto before = api.query().compiled_object_mesh(member);
  ASSERT_TRUE(before.has_value());

  const auto fitted = apply_face_texture_action(
      api, quader::QuaderFaceTextureAction::FitBoth);
  ASSERT_TRUE(fitted.ok()) << fitted.error_message();
  EXPECT_TRUE(fitted.changed());
  const auto after = api.query().compiled_object_mesh(member);
  ASSERT_TRUE(after.has_value());
  for (const auto occurrence : {first_occurrence, second_occurrence}) {
    const auto snapshot = api.query().entity(occurrence);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(snapshot->instance_definition, definition);
    ASSERT_TRUE(snapshot->texture_sampling_from_local.has_value());
    EXPECT_EQ(snapshot->texture_sampling_from_local->values, sampling.values);
  }
  const auto member_after = api.query().entity(member.entity);
  ASSERT_TRUE(member_after.has_value());
  EXPECT_EQ(member_after->parent, definition);
  const auto hidden_definition = api.query().entity(definition);
  ASSERT_TRUE(hidden_definition.has_value());
  EXPECT_FALSE(hidden_definition->visible);
  ASSERT_TRUE(api.history().undo().ok());
  expect_texture_mesh_equal(api.query().compiled_object_mesh(member), *before);
  ASSERT_TRUE(api.history().redo().ok());
  expect_texture_mesh_equal(api.query().compiled_object_mesh(member), *after);
  s.given().object_mode();
  ASSERT_TRUE(api.modeling().exit_instance_edit().ok());
  EXPECT_FALSE(api.modeling().face_texture_settings().editable);
  EXPECT_FALSE(apply_face_texture_action(
      api, quader::QuaderFaceTextureAction::ScaleXUp).ok());
}

TEST(ProgrammaticBehaviorScenarioTests,
     FaceTextureActionAlignToViewRepeatRetainsCapturedCameraBasis) {
  auto s = quader::test::BehaviorScenario::new_project();
  s.given().mesh("wall", face_texture_wall_document())
      .edit_mode().selection_mode(quader::QuaderSelectionMode::Face);
  auto &api = s.api();
  const auto wall = s.object("wall");
  ASSERT_TRUE(api.selection().select_face(wall, 1).ok());
  const auto pane = s.editor().viewports().focused_pane();
  ASSERT_TRUE(s.editor().viewports().set_view(pane, quader::ViewportView::Front).ok());
  const auto before = api.query().compiled_object_mesh(wall);
  ASSERT_TRUE(before.has_value());
  const auto aligned = apply_face_texture_action(
      api, quader::QuaderFaceTextureAction::AlignToView);
  ASSERT_TRUE(aligned.ok()) << aligned.error_message();
  ASSERT_TRUE(aligned.changed());
  const auto front = api.query().compiled_object_mesh(wall);
  ASSERT_TRUE(front.has_value());

  ASSERT_TRUE(s.editor().viewports().set_view(pane, quader::ViewportView::Back).ok());
  ASSERT_TRUE(api.history().undo().ok());
  expect_texture_mesh_equal(api.query().compiled_object_mesh(wall), *before);
  EXPECT_EQ(s.editor().viewports().view(pane), quader::ViewportView::Back);
  const auto repeated = api.history().repeat_last_action();
  ASSERT_TRUE(repeated.ok()) << repeated.error_message();
  EXPECT_TRUE(repeated.changed());
  expect_texture_mesh_equal(api.query().compiled_object_mesh(wall), *front);

  const auto realigned = apply_face_texture_action(
      api, quader::QuaderFaceTextureAction::AlignToView);
  ASSERT_TRUE(realigned.ok()) << realigned.error_message();
  EXPECT_TRUE(realigned.changed());
  const auto back = api.query().compiled_object_mesh(wall);
  ASSERT_TRUE(back.has_value());
  ASSERT_EQ(back->vertices.size(), front->vertices.size());
  bool mapping_changed = false;
  for (std::size_t index = 0; index < back->vertices.size(); ++index) {
    const auto actual = back->vertices[index].uv;
    const auto captured = front->vertices[index].uv;
    mapping_changed = mapping_changed ||
        std::abs(actual.x - captured.x) > 0.00001f ||
        std::abs(actual.y - captured.y) > 0.00001f;
  }
  EXPECT_TRUE(mapping_changed);
}

TEST(ProgrammaticBehaviorScenarioTests,
     UvPeelOptionsValidateWithoutChangingDocumentOrHistory) {
  auto s = quader::test::BehaviorScenario::new_project();
  auto &api = s.api();
  const quader::QuaderUvPeelOptions defaults;
  const auto before = api.revisions();
  const auto initial_revision = api.modeling().uv_peel_settings_revision();
  EXPECT_EQ(api.modeling().uv_peel_settings().options, defaults);
  EXPECT_FALSE(api.modeling().uv_peel_settings().editable);
  EXPECT_FALSE(s.editor().project().dirty());
  const quader::QuaderUvPeelOptions options{
      .axis = quader::QuaderUvPeelAxis::V,
      .world_space = false,
      .u_repeat = 2.0,
      .v_repeat = 3.0,
      .u_offset = -0.25,
      .v_offset = 0.5,
  };
  const auto changed = api.modeling().set_uv_peel_options(options);
  ASSERT_TRUE(changed.ok()) << changed.error_message();
  EXPECT_TRUE(changed.changed());
  EXPECT_EQ(api.modeling().uv_peel_settings().options, options);
  EXPECT_NE(api.modeling().uv_peel_settings_revision(), initial_revision);
  expect_document_revisions_equal(api.revisions(), before);
  EXPECT_FALSE(s.editor().project().dirty());
  EXPECT_FALSE(api.history().can_undo());
  const auto accepted_revision = api.modeling().uv_peel_settings_revision();
  EXPECT_FALSE(api.modeling().set_uv_peel_options(options).changed());
  EXPECT_EQ(api.modeling().uv_peel_settings_revision(), accepted_revision);

  std::array invalid{options, options, options, options, options, options};
  invalid[0].u_repeat = 0.0;
  invalid[1].v_repeat = -1.0;
  invalid[2].u_repeat = std::numeric_limits<double>::infinity();
  invalid[3].u_offset = std::numeric_limits<double>::quiet_NaN();
  invalid[4].v_offset = std::numeric_limits<double>::infinity();
  invalid[5].axis = static_cast<quader::QuaderUvPeelAxis>(99);
  for (const auto &candidate : invalid) {
    const auto result = api.modeling().set_uv_peel_options(candidate);
    EXPECT_FALSE(result.ok());
    EXPECT_FALSE(result.changed());
    EXPECT_EQ(api.modeling().uv_peel_settings().options, options);
    EXPECT_EQ(api.modeling().uv_peel_settings_revision(), accepted_revision);
    expect_document_revisions_equal(api.revisions(), before);
  }
  s.given().mesh("strip", uv_peel_strip_document());
  ASSERT_TRUE(s.editor().selection().select_all().ok());
  s.given().edit_mode();
  ASSERT_TRUE(api.selection().select_edge(s.object("strip"), 2, 5).ok());
  const auto selected = api.modeling().uv_peel_settings();
  ASSERT_TRUE(selected.editable);
  ASSERT_EQ(selected.selected_edge_count, 1u);
  EXPECT_EQ(selected.options, options);
  ASSERT_TRUE(api.modeling().set_uv_peel_options(defaults).ok());
  EXPECT_EQ(api.modeling().uv_peel_settings().selection_token,
            selected.selection_token);
}

TEST(ProgrammaticBehaviorScenarioTests,
     UvPeelRejectsChangedDocumentActiveEdgeAndPreviewWithoutDiscardingRedo) {
  auto s = quader::test::BehaviorScenario::new_project();
  s.given().mesh("strip", uv_peel_strip_document());
  auto &api = s.api();
  const auto strip = s.object("strip");
  const quader::QuaderUvPeelOptions options{
      .world_space = false, .u_offset = 0.25,
  };
  EXPECT_FALSE(peel_uv(api, options).ok());
  ASSERT_TRUE(s.editor().selection().select_all().ok());
  s.given().edit_mode();
  ASSERT_TRUE(api.selection().select_edge(strip, 2, 5).ok());
  const auto original_token = api.modeling().uv_peel_settings().selection_token;
  const auto peeled = peel_uv(api, options);
  ASSERT_TRUE(peeled.ok()) << peeled.error_message();
  ASSERT_TRUE(peeled.changed());
  EXPECT_NE(api.modeling().uv_peel_settings().selection_token, original_token);
  const auto after_peel = api.revisions();
  const auto stale_document = api.modeling().peel_uv({
      .options = options, .selection_token = original_token,
  });
  EXPECT_FALSE(stale_document.ok());
  EXPECT_FALSE(stale_document.changed());
  expect_document_revisions_equal(api.revisions(), after_peel);
  ASSERT_TRUE(api.history().undo().ok());
  ASSERT_TRUE(api.history().can_redo());

  quader::QuaderComponentSelectionArgs selection{
      .object = strip,
      .kind = quader::QuaderComponentKind::Edge,
      .edges = {{1, 4}, {2, 5}},
      .active = {.object = strip, .kind = quader::QuaderComponentKind::Edge,
                 .edge_a = 2, .edge_b = 5},
  };
  ASSERT_TRUE(api.selection().select_components(selection).ok());
  const auto prior_active = api.modeling().uv_peel_settings().selection_token;
  selection.active.edge_a = 1;
  selection.active.edge_b = 4;
  ASSERT_TRUE(api.selection().select_components(selection).ok());
  EXPECT_NE(api.modeling().uv_peel_settings().selection_token, prior_active);
  const auto before_rejected = api.revisions();
  const auto before_history = api.history().snapshot();
  const auto stale_active = api.modeling().peel_uv({
      .options = options, .selection_token = prior_active,
  });
  EXPECT_FALSE(stale_active.ok());
  EXPECT_FALSE(stale_active.changed());
  expect_document_revisions_equal(api.revisions(), before_rejected);
  EXPECT_EQ(api.history().snapshot().redo_label, before_history.redo_label);
  EXPECT_TRUE(api.history().can_redo());

  ASSERT_TRUE(api.modeling().begin_transform_preview().ok());
  const auto blocked = api.modeling().uv_peel_settings();
  EXPECT_FALSE(blocked.editable);
  EXPECT_FALSE(blocked.disabled_reason.empty());
  const auto before_preview_action = api.revisions();
  EXPECT_FALSE(peel_uv(api, options).ok());
  expect_document_revisions_equal(api.revisions(), before_preview_action);
  EXPECT_TRUE(api.modeling().can_cancel_active_tool_or_preview());
  ASSERT_TRUE(api.modeling().cancel_transform_preview().ok());
  EXPECT_TRUE(api.history().can_redo());
  ASSERT_TRUE(api.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  EXPECT_FALSE(api.modeling().uv_peel_settings().editable);
  EXPECT_FALSE(peel_uv(api, options).ok());
}

TEST(ProgrammaticBehaviorScenarioTests,
     UvPeelPublishesMultipleObjectsInSeparateExactUndoEntries) {
  auto s = quader::test::BehaviorScenario::new_project();
  s.given().mesh("first", uv_peel_strip_document())
      .mesh("second", uv_peel_strip_document());
  auto &api = s.api();
  const auto first = s.object("first");
  const auto second = s.object("second");
  ASSERT_TRUE(s.editor().selection().select_all().ok());
  s.given().edit_mode();
  ASSERT_TRUE(api.selection().select_edge(first, 2, 5).ok());
  ASSERT_TRUE(api.selection().select_edge(
      second, 2, 5, quader::QuaderSelectionEdit::Add).ok());
  const auto original_first = api.query().compiled_object_mesh(first);
  const auto original_second = api.query().compiled_object_mesh(second);
  ASSERT_TRUE(original_first.has_value());
  ASSERT_TRUE(original_second.has_value());
  const auto selection_revision = api.revisions().modeling_selection_revision;
  const quader::QuaderUvPeelOptions first_options{
      .world_space = false, .u_repeat = 2.0, .v_repeat = 3.0,
      .u_offset = 0.25, .v_offset = -0.5,
  };
  const auto first_peel = peel_uv(api, first_options);
  ASSERT_TRUE(first_peel.ok()) << first_peel.error_message();
  ASSERT_TRUE(first_peel.changed());
  EXPECT_EQ(api.revisions().modeling_selection_revision, selection_revision);
  EXPECT_EQ(api.modeling().uv_peel_settings().selected_edge_count, 2u);
  EXPECT_EQ(api.history().snapshot().undo_label, "UV Peel");
  const auto first_result = api.query().compiled_object_mesh(first);
  const auto second_result = api.query().compiled_object_mesh(second);
  ASSERT_TRUE(first_result.has_value());
  ASSERT_TRUE(second_result.has_value());
  // The two equal transverse sides straddle the guide's V offset.
  expect_texture_uv_bounds(first_result, 0.25f, 2.25f, -2.0f, 1.0f);
  expect_texture_uv_bounds(second_result, 0.25f, 2.25f, -2.0f, 1.0f);
  ASSERT_EQ(first_result->indices.size(), original_first->indices.size());
  for (std::size_t index = 0; index < first_result->indices.size(); ++index) {
    const auto actual =
        first_result->vertices[first_result->indices[index]].position;
    const auto expected =
        original_first->vertices[original_first->indices[index]].position;
    EXPECT_FLOAT_EQ(actual.x, expected.x);
    EXPECT_FLOAT_EQ(actual.y, expected.y);
    EXPECT_FLOAT_EQ(actual.z, expected.z);
  }

  auto second_options = first_options;
  second_options.u_offset = 0.75;
  const auto second_peel = peel_uv(api, second_options);
  ASSERT_TRUE(second_peel.ok()) << second_peel.error_message();
  ASSERT_TRUE(second_peel.changed());
  const auto final_first = api.query().compiled_object_mesh(first);
  const auto final_second = api.query().compiled_object_mesh(second);
  ASSERT_TRUE(final_first.has_value());
  ASSERT_TRUE(final_second.has_value());
  ASSERT_TRUE(api.history().undo().ok());
  expect_texture_mesh_equal(api.query().compiled_object_mesh(first), *first_result);
  expect_texture_mesh_equal(api.query().compiled_object_mesh(second), *second_result);
  const auto before_noop = api.revisions();
  const auto redo_label = api.history().snapshot().redo_label;
  const auto noop = peel_uv(api, first_options);
  ASSERT_TRUE(noop.ok()) << noop.error_message();
  EXPECT_FALSE(noop.changed());
  expect_document_revisions_equal(api.revisions(), before_noop);
  EXPECT_TRUE(api.history().can_redo());
  EXPECT_EQ(api.history().snapshot().redo_label, redo_label);
  ASSERT_TRUE(api.history().undo().ok());
  expect_texture_mesh_equal(api.query().compiled_object_mesh(first), *original_first);
  expect_texture_mesh_equal(api.query().compiled_object_mesh(second), *original_second);
  ASSERT_TRUE(api.history().redo().ok());
  expect_texture_mesh_equal(api.query().compiled_object_mesh(first), *first_result);
  ASSERT_TRUE(api.history().redo().ok());
  expect_texture_mesh_equal(api.query().compiled_object_mesh(first), *final_first);
  expect_texture_mesh_equal(api.query().compiled_object_mesh(second), *final_second);
}

TEST(ProgrammaticBehaviorScenarioTests,
     UvPeelRepeatKeepsCapturedOptionsAndResolvesCurrentGuideSelection) {
  auto s = quader::test::BehaviorScenario::new_project();
  s.given().mesh("first", uv_peel_strip_document())
      .mesh("second", uv_peel_strip_document())
      .transform("second", {.scale = {2.0f, 3.0f, 1.0f}});
  auto &api = s.api();
  const auto first = s.object("first");
  const auto second = s.object("second");
  const quader::QuaderUvPeelOptions captured{
      .axis = quader::QuaderUvPeelAxis::V,
      .world_space = false,
      .u_repeat = 2.0,
      .v_repeat = 3.0,
      .u_offset = 0.25,
      .v_offset = -0.5,
  };
  ASSERT_TRUE(s.editor().selection().select_all().ok());
  s.given().edit_mode();
  ASSERT_TRUE(api.selection().select_edge(first, 2, 5).ok());
  ASSERT_TRUE(peel_uv(api, captured).ok());
  const auto first_result = api.query().compiled_object_mesh(first);
  ASSERT_TRUE(first_result.has_value());
  const quader::QuaderUvPeelOptions displayed{
      .axis = quader::QuaderUvPeelAxis::U,
      .world_space = true,
      .u_repeat = 7.0,
      .v_repeat = 8.0,
      .u_offset = 4.0,
      .v_offset = 5.0,
  };
  ASSERT_TRUE(api.modeling().set_uv_peel_options(displayed).ok());
  ASSERT_TRUE(api.selection().select_edge(second, 2, 5).ok());
  const auto before_second = api.query().compiled_object_mesh(second);
  ASSERT_TRUE(before_second.has_value());
  const auto repeated = api.history().repeat_last_action();
  ASSERT_TRUE(repeated.ok()) << repeated.error_message();
  ASSERT_TRUE(repeated.changed());
  const auto second_result = api.query().compiled_object_mesh(second);
  ASSERT_TRUE(second_result.has_value());
  expect_texture_uv_bounds(
      effective_texture_mesh(api, second), -0.75f, 1.25f, -0.5f, 2.5f);
  expect_texture_mesh_equal(api.query().compiled_object_mesh(first), *first_result);
  EXPECT_EQ(api.modeling().uv_peel_settings().options, displayed);
  ASSERT_TRUE(api.history().undo().ok());
  expect_texture_mesh_equal(api.query().compiled_object_mesh(second), *before_second);
  ASSERT_TRUE(api.history().redo().ok());
  expect_texture_mesh_equal(api.query().compiled_object_mesh(second), *second_result);
  ASSERT_TRUE(api.history().undo().ok());
  const auto repeated_after_undo = api.history().repeat_last_action();
  ASSERT_TRUE(repeated_after_undo.ok()) << repeated_after_undo.error_message();
  EXPECT_TRUE(repeated_after_undo.changed());
  expect_texture_mesh_equal(api.query().compiled_object_mesh(second), *second_result);
  EXPECT_EQ(api.modeling().uv_peel_settings().options, displayed);
}

TEST(ProgrammaticBehaviorScenarioTests,
     UvPeelLongitudinalArchGuideWalksFrontBackAndOuterFaces) {
  expect_uv_peel_arch_operation(true);
}

TEST(ProgrammaticBehaviorScenarioTests,
     UvPeelSingleWidthEdgeWalksWholeArchThroughQuadEnds) {
  expect_uv_peel_arch_operation(false);
}

TEST(ProgrammaticBehaviorScenarioTests,
     UvPeelCylinderRimNeedsNoIntermediateLoopAndPreservesCaps) {
  auto s = quader::test::BehaviorScenario::new_project();
  s.given().mesh("cylinder", quader::QuaderPolygonDocumentInternalAccess::from_backend(
      uv_peel_unsplit_cylinder_document()));
  auto &api = s.api();
  const auto object = s.object("cylinder");
  quader::QuaderComponentSelectionArgs guide{
      .object = object, .kind = quader::QuaderComponentKind::Edge,
      .active = {.object = object, .kind = quader::QuaderComponentKind::Edge,
                 .edge_a = 1, .edge_b = 2}};
  for (quader::QuaderElementId index = 0; index < 32; ++index) {
    guide.edges.emplace_back(index + 1, (index + 1) % 32 + 1);
  }
  ASSERT_TRUE(s.editor().selection().select_all().ok());
  s.given().edit_mode();
  ASSERT_TRUE(api.selection().select_components(guide).ok());
  const auto settings = api.modeling().uv_peel_settings();
  ASSERT_TRUE(settings.editable) << settings.disabled_reason;
  ASSERT_EQ(settings.selected_edge_count, 32U);
  const auto before = api.query().compiled_object_mesh(object);
  ASSERT_TRUE(before.has_value());
  const auto selection_revision = api.revisions().modeling_selection_revision;
  const auto peeled = peel_uv(api, {});
  ASSERT_TRUE(peeled.ok()) << peeled.error_message();
  ASSERT_TRUE(peeled.changed());
  const auto after = api.query().compiled_object_mesh(object);
  ASSERT_TRUE(after.has_value());
  EXPECT_EQ(after->indices, before->indices);
  ASSERT_EQ(after->vertices.size(), before->vertices.size());
  ASSERT_EQ(after->primitives.size(), before->primitives.size());
  for (std::size_t index = 0; index < after->primitives.size(); ++index) {
    EXPECT_EQ(after->primitives[index].material_slot, before->primitives[index].material_slot);
  }
  std::size_t cap_corners = 0;
  for (std::size_t index = 0; index < before->vertices.size(); ++index) {
    const auto &a = after->vertices[index];
    const auto &b = before->vertices[index];
    EXPECT_EQ(a.position.x, b.position.x);
    EXPECT_EQ(a.position.y, b.position.y);
    EXPECT_EQ(a.position.z, b.position.z);
    EXPECT_EQ(a.normal.x, b.normal.x);
    EXPECT_EQ(a.normal.y, b.normal.y);
    EXPECT_EQ(a.normal.z, b.normal.z);
    if (std::abs(b.uv_geometric_normal.y) > 0.9F) {
      ++cap_corners;
      EXPECT_EQ(a.uv.x, b.uv.x);
      EXPECT_EQ(a.uv.y, b.uv.y);
      EXPECT_EQ(a.uv_carrier, b.uv_carrier);
    }
  }
  EXPECT_GE(cap_corners, 64U);
  for (const quader::QuaderElementId cap : {33, 34}) {
    const auto texture = face_texture(api, object, cap);
    EXPECT_EQ(texture.scale.x, 0.125F);
    EXPECT_EQ(texture.scale.y, 0.125F);
    EXPECT_EQ(texture.shift_uv.x, 0.0F);
    EXPECT_EQ(texture.shift_uv.y, 0.0F);
    EXPECT_EQ(texture.rotation_degrees, 0.0F);
  }
  EXPECT_EQ(api.revisions().modeling_selection_revision, selection_revision);
  s.then().selection().mode(quader::QuaderSelectionMode::Edge).edge_count(32).active_alias("cylinder");
  EXPECT_EQ(api.history().snapshot().undo_label, "UV Peel");
  ASSERT_TRUE(api.history().undo().ok());
  expect_texture_mesh_equal(api.query().compiled_object_mesh(object), *before);
  ASSERT_TRUE(api.history().redo().ok());
  expect_texture_mesh_equal(api.query().compiled_object_mesh(object), *after);
  s.then().selection().mode(quader::QuaderSelectionMode::Edge).edge_count(32).active_alias("cylinder");
}

TEST(ProgrammaticBehaviorScenarioTests,
     UvPeelTranslatedCylinderRingPreservesSharedCornersAndCapAttributes) {
  auto s = quader::test::BehaviorScenario::new_project();
  const auto original = uv_peel_cylinder_document();
  s.given().mesh("cylinder",
      quader::QuaderPolygonDocumentInternalAccess::from_backend(original));
  auto &api = s.api();
  const auto object = s.object("cylinder");
  s.editor().preferences().set_runtime_hooks({
      .texture_lock_enabled = [] { return false; },
      .texture_lock_scale_enabled = [] { return false; }});
  ASSERT_TRUE(api.scene().set_transform(object.entity,
      {.location = {4.0F, 0.1249997615814209F, -18.0F}}).ok());
  const auto entity_before = api.query().entity(object.entity);
  ASSERT_TRUE(entity_before.has_value());
  ASSERT_TRUE(entity_before->texture_sampling_from_local.has_value());
  quader::QMat4 expected_sampling;
  expected_sampling.values[12] = 4.0F;
  expected_sampling.values[13] = 0.1249997615814209F;
  expected_sampling.values[14] = -18.0F;
  EXPECT_EQ(entity_before->texture_sampling_from_local->values, expected_sampling.values);
  constexpr std::array<quader::QuaderElementId, 32> kMiddleRing{
      84, 82, 80, 78, 76, 74, 72, 70, 68, 66, 65, 67, 69, 71, 73, 75,
      77, 79, 81, 83, 85, 87, 89, 91, 93, 95, 96, 94, 92, 90, 88, 86};
  quader::QuaderComponentSelectionArgs guide{
      .object = object, .kind = quader::QuaderComponentKind::Edge,
      .active = {.object = object, .kind = quader::QuaderComponentKind::Edge,
                 .edge_a = 65, .edge_b = 66}};
  for (std::size_t index = 0; index < kMiddleRing.size(); ++index) {
    guide.edges.emplace_back(kMiddleRing[index], kMiddleRing[(index + 1U) % kMiddleRing.size()]);
  }
  ASSERT_TRUE(s.editor().selection().select_all().ok());
  s.given().edit_mode();
  ASSERT_TRUE(api.selection().select_components(guide).ok());
  const auto settings = api.modeling().uv_peel_settings();
  ASSERT_TRUE(settings.editable) << settings.disabled_reason;
  ASSERT_EQ(settings.selected_edge_count, 32U);
  const auto before = api.query().compiled_object_mesh(object);
  ASSERT_TRUE(before.has_value());
  const auto selection_revision = api.revisions().modeling_selection_revision;
  const quader::QuaderUvPeelOptions options;
  const auto peeled = peel_uv(api, options);
  ASSERT_TRUE(peeled.ok()) << peeled.error_message();
  ASSERT_TRUE(peeled.changed());
  const auto after = api.query().compiled_object_mesh(object);
  ASSERT_TRUE(after.has_value());
  const auto effective = effective_texture_mesh(api, object);
  ASSERT_TRUE(effective.has_value());
  EXPECT_EQ(after->indices, before->indices);
  ASSERT_EQ(after->vertices.size(), before->vertices.size());
  ASSERT_EQ(after->face_textures.size(), before->face_textures.size());
  for (std::size_t index = 0; index < before->face_textures.size(); ++index) {
    const auto &a = after->face_textures[index];
    const auto &b = before->face_textures[index];
    EXPECT_EQ(a.face, b.face);
    EXPECT_EQ(a.scale.x, b.scale.x);
    EXPECT_EQ(a.scale.y, b.scale.y);
    EXPECT_EQ(a.shift_uv.x, b.shift_uv.x);
    EXPECT_EQ(a.shift_uv.y, b.shift_uv.y);
    EXPECT_EQ(a.rotation_degrees, b.rotation_degrees);
  }
  using Position = std::array<float, 3>;
  std::map<Position, std::vector<quader::QVec2>> side_samples;
  for (std::size_t index = 0; index < before->vertices.size(); ++index) {
    const auto &a = after->vertices[index];
    const auto &b = before->vertices[index];
    EXPECT_EQ(a.position.x, b.position.x);
    EXPECT_EQ(a.position.y, b.position.y);
    EXPECT_EQ(a.position.z, b.position.z);
    EXPECT_EQ(a.normal.x, b.normal.x);
    EXPECT_EQ(a.normal.y, b.normal.y);
    EXPECT_EQ(a.normal.z, b.normal.z);
    if (std::abs(b.uv_geometric_normal.y) > 0.9F) {
      // The middle guide touches only side quads; cap corner UVs stay exact.
      EXPECT_EQ(a.uv.x, b.uv.x);
      EXPECT_EQ(a.uv.y, b.uv.y);
      EXPECT_EQ(a.uv_carrier, b.uv_carrier);
    } else {
      side_samples[{a.position.x, a.position.y, a.position.z}].push_back(
          effective->vertices[index].uv);
    }
  }
  ASSERT_EQ(side_samples.size(), 96U);
  double circumference = 0.0;
  for (std::size_t index = 0; index < 32U; ++index) {
    const auto &a = original.vertices[index].position;
    const auto &b = original.vertices[(index + 1U) % 32U].position;
    circumference += std::hypot(static_cast<double>(a.x) - b.x,
                                static_cast<double>(a.z) - b.z);
  }
  constexpr float kTolerance = 1.0e-5F;
  std::optional<std::array<float, 2>> seam_position;
  std::size_t seam_rows = 0;
  float minimum_v = std::numeric_limits<float>::infinity();
  float maximum_v = -minimum_v;
  for (const auto &[position, samples] : side_samples) {
    ASSERT_GE(samples.size(), 2U);
    float minimum_u = samples.front().x;
    float maximum_u = minimum_u;
    for (const auto uv : samples) {
      minimum_u = std::min(minimum_u, uv.x);
      maximum_u = std::max(maximum_u, uv.x);
      minimum_v = std::min(minimum_v, uv.y);
      maximum_v = std::max(maximum_v, uv.y);
      EXPECT_NEAR(uv.y, samples.front().y, kTolerance);
      EXPECT_NEAR(std::abs(uv.y), std::abs(position[1] - 0.375F) * 0.5F, kTolerance);
    }
    if (maximum_u - minimum_u > circumference * 0.25) {
      ++seam_rows;
      EXPECT_NEAR(maximum_u - minimum_u, circumference * 0.5, kTolerance);
      const std::array xz{position[0], position[2]};
      if (seam_position.has_value()) {
        EXPECT_EQ(xz, *seam_position);
      } else {
        seam_position = xz;
      }
    } else {
      EXPECT_NEAR(maximum_u, minimum_u, kTolerance);
    }
  }
  EXPECT_EQ(seam_rows, 3U);
  EXPECT_NEAR(minimum_v, -0.25F, kTolerance);
  EXPECT_NEAR(maximum_v, 0.25F, kTolerance);
  EXPECT_EQ(api.revisions().modeling_selection_revision, selection_revision);
  s.then().selection().mode(quader::QuaderSelectionMode::Edge).edge_count(32).active_alias("cylinder");
  EXPECT_EQ(api.query().entity(object.entity)->texture_sampling_from_local->values,
            expected_sampling.values);
  EXPECT_EQ(api.history().snapshot().undo_label, "UV Peel");
  const auto revisions_after = api.revisions();
  const auto rejected = api.modeling().peel_uv({
      .options = options, .selection_token = settings.selection_token});
  EXPECT_FALSE(rejected.ok());
  EXPECT_FALSE(rejected.changed());
  expect_document_revisions_equal(api.revisions(), revisions_after);
  expect_texture_mesh_equal(api.query().compiled_object_mesh(object), *after);
  const auto repeated = peel_uv(api, options);
  ASSERT_TRUE(repeated.ok()) << repeated.error_message();
  EXPECT_FALSE(repeated.changed());
  expect_document_revisions_equal(api.revisions(), revisions_after);
  ASSERT_TRUE(api.history().undo().ok());
  expect_texture_mesh_equal(api.query().compiled_object_mesh(object), *before);
  s.then().selection().mode(quader::QuaderSelectionMode::Edge).edge_count(32).active_alias("cylinder");
  ASSERT_TRUE(api.history().redo().ok());
  expect_texture_mesh_equal(api.query().compiled_object_mesh(object), *after);
  s.then().selection().mode(quader::QuaderSelectionMode::Edge).edge_count(32).active_alias("cylinder");
}

TEST(ProgrammaticBehaviorScenarioTests,
     UvPeelInstanceScopePreservesOccurrenceSamplingAndSharedDefinition) {
  auto s = quader::test::BehaviorScenario::new_project();
  s.given().mesh("strip", uv_peel_strip_document()).selected("strip");
  auto &api = s.api();
  const auto member = s.object("strip");
  const auto created = api.modeling().create_instance_from_selection();
  ASSERT_TRUE(created.ok()) << created.error_message();
  const auto first_occurrence = created.value().occurrence.root.id;
  const auto definition = created.value().definition.root.id;
  ASSERT_TRUE(api.scene().set_transform(first_occurrence, {
      .location = {5.0f, 2.0f, 0.0f}, .scale = {2.0f, 3.0f, 1.0f},
  }).ok());
  const auto first_snapshot = api.query().entity(first_occurrence);
  ASSERT_TRUE(first_snapshot.has_value());
  ASSERT_TRUE(first_snapshot->texture_sampling_from_local.has_value());
  const auto sampling = *first_snapshot->texture_sampling_from_local;
  const auto duplicate = api.scene().duplicate_selected_hierarchy();
  ASSERT_TRUE(duplicate.ok()) << duplicate.error_message();
  ASSERT_EQ(duplicate.value().root_entities.size(), 1u);
  const auto second_occurrence = duplicate.value().root_entities.front().id;
  ASSERT_NE(second_occurrence, first_occurrence);
  const auto edge_selection_mode =
      api.selection().set_mode(quader::QuaderSelectionMode::Edge);
  ASSERT_TRUE(edge_selection_mode.ok());
  EXPECT_TRUE(edge_selection_mode.changed());
  EXPECT_EQ(api.selection().summary().mode, quader::QuaderSelectionMode::Edge);
  EXPECT_EQ(api.selection().summary().selected_object_count, 0u);
  EXPECT_FALSE(api.selection().select_edge(member, 2, 5).ok());
  EXPECT_FALSE(api.modeling().uv_peel_settings().editable);

  ASSERT_TRUE(api.selection().set_mode(quader::QuaderSelectionMode::Object).ok());
  const auto entered = api.modeling().enter_instance_edit(second_occurrence);
  ASSERT_TRUE(entered.ok()) << entered.error_message();
  ASSERT_TRUE(api.selection().select_object(member).ok());
  s.given().edit_mode();
  const auto selected = api.selection().select_edge(member, 2, 5);
  ASSERT_TRUE(selected.ok()) << selected.error_message();
  ASSERT_TRUE(api.modeling().uv_peel_settings().editable);
  const auto before = api.query().compiled_object_mesh(member);
  ASSERT_TRUE(before.has_value());
  const quader::QuaderUvPeelOptions options{
      .world_space = false, .u_offset = 0.5, .v_offset = -0.25,
  };
  const auto peeled = peel_uv(api, options);
  ASSERT_TRUE(peeled.ok()) << peeled.error_message();
  ASSERT_TRUE(peeled.changed());
  const auto after = api.query().compiled_object_mesh(member);
  ASSERT_TRUE(after.has_value());
  for (const auto occurrence : {first_occurrence, second_occurrence}) {
    const auto snapshot = api.query().entity(occurrence);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(snapshot->instance_definition, definition);
    ASSERT_TRUE(snapshot->texture_sampling_from_local.has_value());
    EXPECT_EQ(snapshot->texture_sampling_from_local->values, sampling.values);
  }
  const auto member_after = api.query().entity(member.entity);
  ASSERT_TRUE(member_after.has_value());
  EXPECT_EQ(member_after->parent, definition);
  ASSERT_TRUE(api.history().undo().ok());
  expect_texture_mesh_equal(api.query().compiled_object_mesh(member), *before);
  ASSERT_TRUE(api.history().redo().ok());
  expect_texture_mesh_equal(api.query().compiled_object_mesh(member), *after);
  s.given().object_mode();
  ASSERT_TRUE(api.modeling().exit_instance_edit().ok());
  EXPECT_FALSE(api.modeling().uv_peel_settings().editable);
}

TEST(ProgrammaticBehaviorScenarioTests,
     ShiftDuplicatePreviewKeepsDurableMeshStableUntilAbsoluteDeltaCommit) {
  auto s = quader::test::BehaviorScenario::new_project();

  s.given().unit_box("source", "Shift Duplicate Source").selected("source");

  s.when().begin_transform_preview(true).translate_preview({1.0f, 0.0f, 0.0f});

  const auto duplicate = s.object("source.copy");
  ASSERT_NE(duplicate.entity, quader::kInvalidEntityId);
  ASSERT_NE(duplicate.modeling_object_id,
            quader::kInvalidQuaderModelingObjectId);
  const auto duplicate_at_begin = s.api().query().entity(duplicate.entity);
  ASSERT_TRUE(duplicate_at_begin.has_value());
  EXPECT_EQ(duplicate_at_begin->modeling_object_id,
            duplicate.modeling_object_id);

  const quader::test::BehaviorSnapshot first_move = s.snapshot("first move");

  s.when().translate_preview({3.0f, 0.0f, 0.0f}).commit_transform_preview();

  s.then().command().ok_changed();
  s.then()
      .scene()
      .entity_count(2)
      .selected_alias_is("source.copy")
      .selected_bounds_center_near({3.0f, 0.0f, 0.0f});

  // Object and Group structural previews use the scene transform owner, so
  // the complete duplicated hierarchy exposes the current absolute preview
  // delta while the enclosing editor transaction remains cancelable.
  s.then().snapshot(first_move).selected_bounds_center_near({1.0f, 0.0f, 0.0f});

  const auto duplicate_at_commit = s.api().query().entity(duplicate.entity);
  ASSERT_TRUE(duplicate_at_commit.has_value());
  EXPECT_EQ(s.api().history().snapshot().undo_label,
            "Commit Transform Preview");
  EXPECT_EQ(duplicate_at_commit->uuid, duplicate_at_begin->uuid);
  EXPECT_EQ(duplicate_at_commit->editor_object_id,
            duplicate_at_begin->editor_object_id);
  EXPECT_EQ(duplicate_at_commit->modeling_object_id,
            duplicate.modeling_object_id);

  ASSERT_TRUE(s.api().history().undo().ok());
  EXPECT_EQ(s.api().history().snapshot().undo_label, "Create Box");
  EXPECT_FALSE(s.api().query().entity(duplicate.entity).has_value());
  EXPECT_FALSE(s.api()
                   .query()
                   .modeling_object_for_id(duplicate.modeling_object_id)
                   .has_value());
  ASSERT_TRUE(s.api().history().redo().ok());
  const auto duplicate_after_redo = s.api().query().entity(duplicate.entity);
  ASSERT_TRUE(duplicate_after_redo.has_value());
  EXPECT_EQ(duplicate_after_redo->uuid, duplicate_at_begin->uuid);
  EXPECT_EQ(duplicate_after_redo->editor_object_id,
            duplicate_at_begin->editor_object_id);
  EXPECT_EQ(duplicate_after_redo->modeling_object_id,
            duplicate.modeling_object_id);
  const auto modeling_after_redo =
      s.api().query().modeling_object_for_id(duplicate.modeling_object_id);
  ASSERT_TRUE(modeling_after_redo.has_value());
  EXPECT_EQ(modeling_after_redo->entity, duplicate.entity);

  s.when().pick({3.0f, 0.0f, 5.0f});
  s.then().selection().object_count(1).active_alias("source.copy");
}

TEST(ProgrammaticBehaviorScenarioTests,
     ShiftDuplicatePreviewCancelRestoresSourceAndRemovesExactDuplicateIdentity) {
  auto s = quader::test::BehaviorScenario::new_project();
  s.given().unit_box("source", "Shift Duplicate Source").selected("source");
  const auto source = s.object("source");

  s.when().begin_transform_preview(true);
  s.then().command().ok_changed();
  const auto duplicate = s.object("source.copy");
  ASSERT_NE(duplicate.entity, quader::kInvalidEntityId);
  ASSERT_NE(duplicate.modeling_object_id,
            quader::kInvalidQuaderModelingObjectId);
  ASSERT_TRUE(s.api().query().entity(duplicate.entity).has_value());
  ASSERT_TRUE(s.api()
                  .query()
                  .modeling_object_for_id(duplicate.modeling_object_id)
                  .has_value());

  s.when().cancel_transform_preview();
  s.then().command().ok_changed();
  EXPECT_EQ(s.api().history().snapshot().undo_label, "Create Box");
  EXPECT_FALSE(s.api().query().entity(duplicate.entity).has_value());
  EXPECT_FALSE(s.api()
                   .query()
                   .modeling_object_for_id(duplicate.modeling_object_id)
                   .has_value());
  const auto source_after_cancel =
      s.api().query().modeling_object_for_entity(source.entity);
  ASSERT_TRUE(source_after_cancel.has_value());
  EXPECT_EQ(source_after_cancel->modeling_object_id,
            source.modeling_object_id);
  EXPECT_EQ(s.api().selection().summary().active_object_id,
            source.modeling_object_id);
}

TEST(ProgrammaticBehaviorScenarioTests, ComponentGizmoMoveRotateScaleKeepComponentSelectionsAndMutateMesh) {
  for (const ComponentPickCase test_case : {
           ComponentPickCase{
               .mode = quader::QuaderSelectionMode::Face,
               .ray_origin = {0.0f, 0.0f, 5.0f},
               .tool = quader::ModelingTransformTool::Move,
           },
           ComponentPickCase{
               .mode = quader::QuaderSelectionMode::Face,
               .ray_origin = {0.0f, 0.0f, 5.0f},
               .tool = quader::ModelingTransformTool::Rotate,
           },
           ComponentPickCase{
               .mode = quader::QuaderSelectionMode::Face,
               .ray_origin = {0.0f, 0.0f, 5.0f},
               .tool = quader::ModelingTransformTool::Scale,
           },
           ComponentPickCase{
               .mode = quader::QuaderSelectionMode::Edge,
               .ray_origin = {0.0f, 1.0f, 5.0f},
               .tool = quader::ModelingTransformTool::Move,
           },
           ComponentPickCase{
               .mode = quader::QuaderSelectionMode::Edge,
               .ray_origin = {0.0f, 1.0f, 5.0f},
               .tool = quader::ModelingTransformTool::Rotate,
           },
           ComponentPickCase{
               .mode = quader::QuaderSelectionMode::Edge,
               .ray_origin = {0.0f, 1.0f, 5.0f},
               .tool = quader::ModelingTransformTool::Scale,
           },
           ComponentPickCase{
               .mode = quader::QuaderSelectionMode::Vertex,
               .ray_origin = {1.0f, 1.0f, 5.0f},
               .tool = quader::ModelingTransformTool::Move,
           },
           ComponentPickCase{
               .mode = quader::QuaderSelectionMode::Vertex,
               .ray_origin = {1.0f, 1.0f, 5.0f},
               .tool = quader::ModelingTransformTool::Rotate,
           },
           ComponentPickCase{
               .mode = quader::QuaderSelectionMode::Vertex,
               .ray_origin = {1.0f, 1.0f, 5.0f},
               .tool = quader::ModelingTransformTool::Scale,
           },
       }) {
    run_component_transform_drag_case(test_case);
  }
}

TEST(ProgrammaticBehaviorScenarioTests,
     LocalFaceFrameUsesTextureAxesInsteadOfTheBeveledBoxPerimeterStart) {
  for (std::size_t first_corner = 0U; first_corner < 4U; ++first_corner) {
    SCOPED_TRACE(first_corner);
    auto s = quader::test::BehaviorScenario::new_project();
    s.given().mesh("side", beveled_box_side_document(first_corner),
                   "Box Face 10 Local Frame");
    ASSERT_TRUE(s.editor().selection().select_all().ok());
    s.given().edit_mode();
    ASSERT_TRUE(s.api().selection().select_face(s.object("side"), 1).ok());
    const auto orientation = s.api().query().selection_transform_orientation();
    ASSERT_TRUE(orientation.has_value());
    expect_orientation_near(*orientation,
                            {.x_axis = {0.0f, 0.0f, 1.0f},
                             .y_axis = {0.0f, 1.0f, 0.0f},
                             .z_axis = {-1.0f, 0.0f, 0.0f}});
  }
}

TEST(ProgrammaticBehaviorScenarioTests,
     LocalComponentFramesPreserveAuthoredFaceAndEdgeDirectionsThroughTransforms) {
  const float quarter_turn = static_cast<float>(std::acos(-1.0) * 0.5);
  for (const bool oblique : {false, true}) {
    for (const quader::QVec3 scale : {
             quader::QVec3{1.0f, 1.0f, 1.0f},
             quader::QVec3{1.4f, 0.8f, 1.9f},
             quader::QVec3{-1.4f, 0.8f, 1.9f},
         }) {
      SCOPED_TRACE(oblique);
      SCOPED_TRACE(scale.x);
      auto s = quader::test::BehaviorScenario::new_project();
      s.given()
          .mesh("side", beveled_box_side_document(0U, oblique),
                 "Transformed Box Face 10")
          .transform("side", {.location = {2.0f, 1.0f, -3.0f},
                               .rotation_euler = {0.0f, quarter_turn, 0.0f},
                               .scale = scale});
      const auto object = s.object("side");
      const auto positions = beveled_box_side_positions(oblique);
      // The authored scene rotation is a positive quarter turn about Y.
      const auto world_direction = [&](quader::QVec3 direction) {
        return quader::QVec3{scale.z * direction.z, scale.y * direction.y,
                             -scale.x * direction.x};
      };
      const auto first_edge = world_direction(positions[1] - positions[0]);
      const auto diagonal = world_direction(positions[2] - positions[0]);
      const auto world_normal =
          quader::normalize(quader::cross(first_edge, diagonal));
      ASSERT_TRUE(s.editor().selection().select_all().ok());
      s.given().edit_mode();
      for (const auto mode : {quader::QuaderSelectionMode::Face,
                              quader::QuaderSelectionMode::Edge,
                              quader::QuaderSelectionMode::Vertex}) {
        SCOPED_TRACE(static_cast<int>(mode));
        if (mode == quader::QuaderSelectionMode::Face) {
          ASSERT_TRUE(s.api().selection().select_face(object, 1).ok());
        } else if (mode == quader::QuaderSelectionMode::Edge) {
          // Reversed request endpoints must retain the face's directed edge.
          ASSERT_TRUE(s.api().selection().select_edge(object, 2, 1).ok());
        } else {
          ASSERT_TRUE(s.api().selection().select_vertex(object, 1).ok());
        }
        const auto orientation = s.api().query().selection_transform_orientation();
        ASSERT_TRUE(orientation.has_value());
        quader::QuaderTransform3 expected;
        expected.z_axis = world_normal;
        if (mode == quader::QuaderSelectionMode::Face) {
          // Both source normals use the generated downward V axis. Their
          // texture U direction is +Z, unaffected by the short beveled edge.
          expected.x_axis = quader::normalize(world_direction({0.0f, 0.0f, 1.0f}));
          expected.y_axis = quader::cross(expected.z_axis, expected.x_axis);
        } else {
          expected.y_axis = quader::normalize(first_edge) * -1.0f;
          expected.x_axis = quader::cross(expected.y_axis, expected.z_axis);
        }
        expect_orientation_near(*orientation, expected);
        EXPECT_NEAR(quader::length(orientation->x_axis), 1.0f, 0.001f);
        EXPECT_NEAR(quader::length(orientation->y_axis), 1.0f, 0.001f);
        EXPECT_NEAR(quader::length(orientation->z_axis), 1.0f, 0.001f);
        expect_vec3_near(quader::cross(orientation->x_axis, orientation->y_axis),
                         orientation->z_axis);
      }
    }
  }
}

TEST(ProgrammaticBehaviorScenarioTests,
     NonplanarComponentFramesProjectAuthoredEdgesAfterWorldScaling) {
  for (const float scale_x : {2.0f, -2.0f}) {
    SCOPED_TRACE(scale_x);
    quader_poly::Document document;
    std::array<quader_poly::ElementId, 4> vertices;
    const std::array positions{
        quader::QVec3{0.0f, 0.0f, 0.0f},
        quader::QVec3{1.0f, 0.0f, 1.0f},
        quader::QVec3{1.0f, 1.0f, 0.0f},
        quader::QVec3{0.0f, 1.0f, 0.0f},
    };
    for (std::size_t index = 0; index < vertices.size(); ++index) {
      vertices[index] = quader_poly::add_vertex(document, positions[index]);
    }
    (void)quader_poly::add_face(document, vertices);
    auto s = quader::test::BehaviorScenario::new_project();
    s.given()
        .mesh("warped", quader::QuaderPolygonDocumentInternalAccess::from_backend(
                            std::move(document)),
               "Nonplanar Local Component Frame")
        .transform("warped", {.scale = {scale_x, 1.0f, 4.0f}});
    const auto object = s.object("warped");
    // These are the Newell normal and authored edge in the scaled geometry.
    // Projecting the local edge before scaling would lose its normal component.
    const auto world_normal = quader::normalize(
        quader::QVec3{-4.0f, 4.0f * scale_x, 2.0f * scale_x});
    const quader::QVec3 world_edge{scale_x, 0.0f, 4.0f};
    const auto world_x = quader::normalize(quader::cross(world_normal, world_edge));
    const quader::QuaderTransform3 expected{
        .x_axis = world_x,
        .y_axis = quader::cross(world_normal, world_x),
        .z_axis = world_normal,
    };
    ASSERT_TRUE(s.editor().selection().select_all().ok());
    s.given().edit_mode();
    for (const bool select_edge : {false, true}) {
      SCOPED_TRACE(select_edge);
      if (select_edge) {
        ASSERT_TRUE(s.api().selection().select_edge(object, 2, 1).ok());
      } else {
        ASSERT_TRUE(s.api().selection().select_vertex(object, 1).ok());
      }
      const auto orientation = s.api().query().selection_transform_orientation();
      ASSERT_TRUE(orientation.has_value());
      expect_orientation_near(*orientation, expected);
    }
  }
}

TEST(ProgrammaticBehaviorScenarioTests,
     LocalComponentOrientationUsesExplicitActiveVertexEdgeAndFace) {
  for (const quader::QuaderSelectionMode mode : {
           quader::QuaderSelectionMode::Vertex,
           quader::QuaderSelectionMode::Edge,
           quader::QuaderSelectionMode::Face,
       }) {
    SCOPED_TRACE(static_cast<int>(mode));
    auto s = quader::test::BehaviorScenario::new_project();
    s.given()
        .unit_box("box", "Local Component Orientation Box")
        .transform("box",
                   {.rotation_euler = {0.37f, -0.51f, 0.29f},
                    .scale = {1.4f, 0.8f, 1.9f}});
    const quader::QuaderModelingObjectRef object = s.object("box");

    ASSERT_TRUE(s.editor().selection().select_all().ok());
    s.given().edit_mode();
    const quader::QuaderApiVoidResult selected_first =
        s.api().selection().select_components(component_orientation_selection(
            object, mode, true, false, false));
    ASSERT_TRUE(selected_first.ok()) << selected_first.error_message();
    const std::optional<quader::QuaderTransform3> first_orientation =
        s.api().query().selection_transform_orientation();
    ASSERT_TRUE(first_orientation.has_value());
    EXPECT_TRUE(orientation_is_slanted(*first_orientation));

    const quader::QuaderApiVoidResult selected_second =
        s.api().selection().select_components(component_orientation_selection(
            object, mode, false, true, true));
    ASSERT_TRUE(selected_second.ok()) << selected_second.error_message();
    const std::optional<quader::QuaderTransform3> second_orientation =
        s.api().query().selection_transform_orientation();
    ASSERT_TRUE(second_orientation.has_value());
    EXPECT_TRUE(orientation_is_slanted(*second_orientation));
    EXPECT_GT(orientation_axis_difference(*first_orientation,
                                          *second_orientation),
              0.1f);

    const quader::QuaderApiVoidResult selected_both =
        s.api().selection().select_components(component_orientation_selection(
            object, mode, true, true, true));
    ASSERT_TRUE(selected_both.ok()) << selected_both.error_message();
    const quader::QuaderSelectionSummary summary =
        s.api().selection().summary();
    const std::optional<quader::QuaderTransform3> multi_orientation =
        s.api().query().selection_transform_orientation();
    ASSERT_TRUE(multi_orientation.has_value());
    expect_orientation_near(*multi_orientation, *second_orientation);
    EXPECT_EQ(summary.active_object_id, object.modeling_object_id);

    switch (mode) {
    case quader::QuaderSelectionMode::Vertex:
      EXPECT_EQ(summary.active_kind,
                quader::QuaderActiveSelectionKind::Vertex);
      EXPECT_EQ(summary.selected_vertex_count, 2u);
      EXPECT_EQ(summary.active_vertex_id, 2u);
      break;
    case quader::QuaderSelectionMode::Edge:
      EXPECT_EQ(summary.active_kind, quader::QuaderActiveSelectionKind::Edge);
      EXPECT_EQ(summary.selected_edge_count, 2u);
      EXPECT_EQ(summary.active_edge_a, 1u);
      EXPECT_EQ(summary.active_edge_b, 4u);
      break;
    case quader::QuaderSelectionMode::Face:
      EXPECT_EQ(summary.active_kind, quader::QuaderActiveSelectionKind::Face);
      EXPECT_EQ(summary.selected_face_count, 2u);
      EXPECT_EQ(summary.active_face_id, 3u);
      break;
    case quader::QuaderSelectionMode::Object:
    case quader::QuaderSelectionMode::Group:
      FAIL() << "Local component orientation received a non-component mode";
      break;
    }
  }
}

TEST(ProgrammaticBehaviorScenarioTests,
     LocalComponentMoveRotateScaleTransformThroughExplicitActiveFrames) {
  /// Names one transform tool case for scoped multi-mode diagnostics.
  struct TransformCase {
    quader::QuaderTransformTool tool = quader::QuaderTransformTool::Move;
    const char *label = "";
  };

  for (const quader::QuaderSelectionMode mode : {
           quader::QuaderSelectionMode::Vertex,
           quader::QuaderSelectionMode::Edge,
           quader::QuaderSelectionMode::Face,
       }) {
    for (const TransformCase transform_case : {
             TransformCase{quader::QuaderTransformTool::Move, "move"},
             TransformCase{quader::QuaderTransformTool::Rotate, "rotate"},
             TransformCase{quader::QuaderTransformTool::Scale, "scale"},
         }) {
      SCOPED_TRACE(static_cast<int>(mode));
      SCOPED_TRACE(transform_case.label);
      auto s = quader::test::BehaviorScenario::new_project();
      s.given().mesh("tetra", local_transform_tetrahedron_document(),
                     "Local Component Transform Tetrahedron");
      const quader::QuaderModelingObjectRef object = s.object("tetra");
      ASSERT_TRUE(s.editor().selection().select_all().ok());
      s.given().edit_mode();
      const quader::QuaderApiVoidResult selected =
          s.api().selection().select_components(
              tetrahedron_component_selection(object, mode));
      ASSERT_TRUE(selected.ok()) << selected.error_message();

      const std::optional<quader::QuaderTransform3> orientation =
          s.api().query().selection_transform_orientation();
      const std::optional<quader::QVec3> pivot =
          s.api().query().selection_transform_pivot();
      ASSERT_TRUE(orientation.has_value());
      ASSERT_TRUE(pivot.has_value());
      if (mode == quader::QuaderSelectionMode::Vertex) {
        expect_orientation_near(*orientation,
                                {.x_axis = {0.0f, 1.0f, 0.0f},
                                 .y_axis = {1.0f, 0.0f, 0.0f},
                                 .z_axis = {0.0f, 0.0f, -1.0f}});
      } else {
        EXPECT_TRUE(orientation_is_slanted(*orientation));
      }
      EXPECT_LT(std::abs(quader::dot(
                    quader::normalize(orientation->x_axis),
                    quader::QVec3{1.0f, 0.0f, 0.0f})),
                0.99f);

      const std::optional<quader::QuaderMeshAsset> before_mesh =
          s.api().query().compiled_object_mesh(object);
      ASSERT_TRUE(before_mesh.has_value());
      for (const quader::QVec3 position :
           local_transform_tetrahedron_positions()) {
        EXPECT_TRUE(mesh_contains_position(*before_mesh, position));
      }

      // The selected vertices lie along this vertex frame's Y axis. Scaling
      // X would correctly leave both points on the unchanged pivot plane.
      const bool scale_vertex_along_y =
          mode == quader::QuaderSelectionMode::Vertex &&
          transform_case.tool == quader::QuaderTransformTool::Scale;
      const quader::QuaderTransformDragPreview preview =
          perform_local_component_drag(s, transform_case.tool, *orientation,
                                       *pivot,
                                       scale_vertex_along_y
                                           ? quader::QuaderTransformAxis::Y
                                           : quader::QuaderTransformAxis::X);
      if (transform_case.tool == quader::QuaderTransformTool::Move) {
        ASSERT_GT(quader::length(preview.move_delta), 0.01f);
        EXPECT_GT(quader::dot(quader::normalize(preview.move_delta),
                              quader::normalize(orientation->x_axis)),
                  0.999f);
      } else if (transform_case.tool ==
                 quader::QuaderTransformTool::Rotate) {
        EXPECT_GT(std::abs(preview.rotate_angle_degrees), 1.0);
      } else {
        if (scale_vertex_along_y) {
          EXPECT_NEAR(preview.scale.x, 1.0f, 0.001f);
          EXPECT_GT(std::abs(preview.scale.y - 1.0f), 0.01f);
        } else {
          EXPECT_GT(std::abs(preview.scale.x - 1.0f), 0.01f);
          EXPECT_NEAR(preview.scale.y, 1.0f, 0.001f);
        }
        EXPECT_NEAR(preview.scale.z, 1.0f, 0.001f);
      }

      const std::vector<std::size_t> transformed_indices =
          tetrahedron_selected_vertex_indices(mode);
      const std::optional<quader::QuaderMeshAsset> after_mesh =
          s.api().query().compiled_object_mesh(object);
      ASSERT_TRUE(after_mesh.has_value());
      EXPECT_EQ(after_mesh->indices.size(), before_mesh->indices.size());
      bool at_least_one_selected_vertex_moved = false;
      for (std::size_t index = 0;
           index < local_transform_tetrahedron_positions().size(); ++index) {
        const bool transformed =
            std::find(transformed_indices.begin(), transformed_indices.end(),
                      index) != transformed_indices.end();
        const quader::QVec3 source =
            local_transform_tetrahedron_positions()[index];
        const quader::QVec3 expected =
            transformed ? expected_local_component_position(
                              source, *pivot, *orientation, preview)
                        : source;
        EXPECT_TRUE(mesh_contains_position(*after_mesh, expected))
            << "missing authored vertex " << index + 1u;
        if (transformed && quader::length(expected - source) > 0.01f) {
          at_least_one_selected_vertex_moved = true;
        }
      }
      EXPECT_TRUE(at_least_one_selected_vertex_moved);

      const quader::QuaderSelectionSummary summary =
          s.api().selection().summary();
      EXPECT_EQ(summary.active_object_id, object.modeling_object_id);
      if (mode == quader::QuaderSelectionMode::Vertex) {
        EXPECT_EQ(summary.selected_vertex_count, 2u);
        EXPECT_EQ(summary.active_vertex_id, 2u);
      } else if (mode == quader::QuaderSelectionMode::Edge) {
        EXPECT_EQ(summary.selected_edge_count, 2u);
        EXPECT_EQ(summary.active_edge_a, 2u);
        EXPECT_EQ(summary.active_edge_b, 3u);
      } else {
        EXPECT_EQ(summary.selected_face_count, 2u);
        EXPECT_EQ(summary.active_face_id, 4u);
      }
    }
  }
}

TEST(ProgrammaticBehaviorScenarioTests,
     TrackballSceneRotationFreezesCameraAndPreservesSharedPivotThroughHistory) {
  auto s = quader::test::BehaviorScenario::new_project();
  const std::array positions{
      quader::QVec3{-2.0f, 1.0f, 0.5f},
      quader::QVec3{3.0f, -1.0f, 1.5f},
  };
  s.given()
      .unit_box("first", "First Trackball Box")
      .transform("first", {.location = positions[0],
                           .rotation_euler = {0.0f, 0.0f, 0.25f}})
      .unit_box("second", "Second Trackball Box")
      .transform("second", {.location = positions[1],
                            .rotation_euler = {0.0f, 0.0f, 0.25f}})
      .selected_aliases({"first", "second"}, "second");
  const auto initial_orientation =
      s.api().query().selection_transform_orientation();
  ASSERT_TRUE(initial_orientation.has_value());
  const auto initial_center = s.api().query().selection_center();
  ASSERT_TRUE(initial_center.has_value());

  const auto expect_rotation = [&](quader::QVec3 axis, double radians) {
    const double degrees = radians * 180.0 / std::acos(-1.0);
    const std::array<std::string_view, 2> aliases{"first", "second"};
    for (std::size_t index = 0; index < aliases.size(); ++index) {
      const auto entity = s.api().query().entity(s.object(aliases[index]).entity);
      ASSERT_TRUE(entity.has_value());
      expect_vec3_near(entity->transform.location,
                       rotate_about_axis(positions[index],
                                         kRepeatRotationWorldPivot, axis,
                                         degrees));
    }
    const auto orientation = s.api().query().selection_transform_orientation();
    ASSERT_TRUE(orientation.has_value());
    expect_orientation_near(
        *orientation,
        {.x_axis = rotate_about_axis(initial_orientation->x_axis, {}, axis,
                                    degrees),
         .y_axis = rotate_about_axis(initial_orientation->y_axis, {}, axis,
                                    degrees),
         .z_axis = rotate_about_axis(initial_orientation->z_axis, {}, axis,
                                    degrees)});
    const auto center = s.api().query().selection_center();
    ASSERT_TRUE(center.has_value());
    expect_vec3_near(*center, rotate_about_axis(*initial_center,
                                               kRepeatRotationWorldPivot,
                                               axis, degrees));
    s.then().selection().object_count(2);
  };

  auto begin = s.api().modeling().begin_transform_drag(trackball_drag_request());
  ASSERT_TRUE(begin.ok()) << begin.error_message();
  auto update = s.api().modeling().update_transform_drag(
      trackball_drag_update({150.0f, 100.0f}));
  ASSERT_TRUE(update.ok()) << update.error_message();
  EXPECT_TRUE(update.changed());
  expect_rotation({0.0f, 1.0f, 0.0f}, 0.5);

  // Equal-length mouse motion on the other screen axis must replace the preview.
  update = s.api().modeling().update_transform_drag(
      trackball_drag_update({100.0f, 150.0f}));
  ASSERT_TRUE(update.ok()) << update.error_message();
  EXPECT_TRUE(update.changed());
  expect_rotation({0.0f, 0.0f, -1.0f}, 0.5);
  update = s.api().modeling().update_transform_drag(
      trackball_drag_update({100.0f, 150.0f}));
  ASSERT_TRUE(update.ok()) << update.error_message();
  EXPECT_FALSE(update.changed());
  ASSERT_TRUE(s.api().modeling().cancel_transform_drag().ok());
  expect_rotation({0.0f, 1.0f, 0.0f}, 0.0);

  begin = s.api().modeling().begin_transform_drag(trackball_drag_request());
  ASSERT_TRUE(begin.ok()) << begin.error_message();
  update = s.api().modeling().update_transform_drag(
      trackball_drag_update({130.0f, 140.0f}));
  ASSERT_TRUE(update.ok()) << update.error_message();
  EXPECT_TRUE(update.changed());
  const quader::QVec3 diagonal_axis{0.0f, 0.6f, -0.8f};
  expect_rotation(diagonal_axis, 0.5);
  const auto commit = s.api().modeling().commit_transform_drag();
  ASSERT_TRUE(commit.ok()) << commit.error_message();
  EXPECT_TRUE(commit.changed());
  expect_rotation(diagonal_axis, 0.5);

  s.when().undo();
  s.then().command().ok_changed();
  expect_rotation(diagonal_axis, 0.0);
  s.when().redo();
  s.then().command().ok_changed();
  expect_rotation(diagonal_axis, 0.5);
  s.when().execute(quader::quader_command::kEditRepeatLastAction);
  s.then().command().ok_changed();
  expect_rotation(diagonal_axis, 1.0);
  s.when().undo();
  s.then().command().ok_changed();
  expect_rotation(diagonal_axis, 0.5);
  s.when().redo();
  s.then().command().ok_changed();
  expect_rotation(diagonal_axis, 1.0);
}

TEST(ProgrammaticBehaviorScenarioTests,
     TrackballComponentRotationPreservesWorldMotionOnTransformedMeshes) {
  auto s = quader::test::BehaviorScenario::new_project();
  const quader::QVec3 scene_position{5.0f, 2.0f, -1.0f};
  const float quarter_turn = static_cast<float>(std::acos(-1.0) * 0.5);
  s.given()
      .mesh("tetra", local_transform_tetrahedron_document(), "Trackball Tetrahedron")
      .transform("tetra", {.location = scene_position,
                           .rotation_euler = {0.0f, 0.0f, quarter_turn},
                           .scale = {2.0f, 3.0f, 0.5f}});
  const auto object = s.object("tetra");
  ASSERT_TRUE(s.editor().selection().select_all().ok());
  s.given().edit_mode();
  ASSERT_TRUE(s.api().selection().select_components(
      tetrahedron_component_selection(object, quader::QuaderSelectionMode::Edge))
                  .ok());

  const auto expect_rotation = [&](double radians, bool transient_preview = false) {
    const auto mesh = s.api().query().compiled_object_mesh(object);
    ASSERT_TRUE(mesh.has_value());
    quader::QVec3 minimum;
    quader::QVec3 maximum;
    for (std::size_t index = 0;
         index < local_transform_tetrahedron_positions().size(); ++index) {
      const auto source = local_transform_tetrahedron_positions()[index];
      auto expected = source;
      if (index < 3u) {
        const quader::QVec3 world = scene_position +
            quader::QVec3{-3.0f * source.y, 2.0f * source.x, 0.5f * source.z};
        const auto rotated = rotate_about_axis(
            world, kRepeatRotationWorldPivot, {0.0f, 0.6f, -0.8f},
            radians * 180.0 / std::acos(-1.0));
        if (index == 0u) {
          minimum = maximum = rotated;
        } else {
          minimum = {std::min(minimum.x, rotated.x),
                     std::min(minimum.y, rotated.y),
                     std::min(minimum.z, rotated.z)};
          maximum = {std::max(maximum.x, rotated.x),
                     std::max(maximum.y, rotated.y),
                     std::max(maximum.z, rotated.z)};
        }
        const auto relative = rotated - scene_position;
        if (!transient_preview) {
          expected = {relative.y / 2.0f, -relative.x / 3.0f, relative.z / 0.5f};
        }
      }
      EXPECT_TRUE(mesh_contains_position(*mesh, expected))
          << "missing authored vertex " << index + 1u;
    }
    if (transient_preview) {
      // Position-only previews keep the compiled asset durable; selection
      // bounds expose the live transformed component coordinates.
      const auto center = s.api().query().selection_center();
      ASSERT_TRUE(center.has_value());
      expect_vec3_near(*center, (minimum + maximum) * 0.5f);
    }
    const auto entity = s.api().query().entity(object.entity);
    ASSERT_TRUE(entity.has_value());
    expect_vec3_near(entity->transform.location, scene_position);
    expect_vec3_near(entity->transform.rotation_euler, {0.0f, 0.0f, quarter_turn});
    expect_vec3_near(entity->transform.scale, {2.0f, 3.0f, 0.5f});
    const auto selection = s.api().selection().summary();
    EXPECT_EQ(selection.selected_edge_count, 2u);
    EXPECT_EQ(selection.active_edge_a, 2u);
    EXPECT_EQ(selection.active_edge_b, 3u);
  };

  for (const bool commit_drag : {false, true}) {
    const auto begin =
        s.api().modeling().begin_transform_drag(trackball_drag_request());
    ASSERT_TRUE(begin.ok()) << begin.error_message();
    const auto update = s.api().modeling().update_transform_drag(
        trackball_drag_update({130.0f, 140.0f}));
    ASSERT_TRUE(update.ok()) << update.error_message();
    EXPECT_TRUE(update.changed());
    expect_rotation(0.5, true);
    if (commit_drag) {
      const auto commit = s.api().modeling().commit_transform_drag();
      ASSERT_TRUE(commit.ok()) << commit.error_message();
      EXPECT_TRUE(commit.changed());
      expect_rotation(0.5);
    } else {
      ASSERT_TRUE(s.api().modeling().cancel_transform_drag().ok());
      expect_rotation(0.0);
    }
  }

  s.when().undo();
  s.then().command().ok_changed();
  expect_rotation(0.0);
  s.when().redo();
  s.then().command().ok_changed();
  expect_rotation(0.5);
  s.when().execute(quader::quader_command::kEditRepeatLastAction);
  s.then().command().ok_changed();
  expect_rotation(1.0);
  s.when().undo();
  s.then().command().ok_changed();
  expect_rotation(0.5);
  s.when().redo();
  s.then().command().ok_changed();
  expect_rotation(1.0);
}

TEST(ProgrammaticBehaviorScenarioTests,
     RepeatLastComponentTransformKeepsRecordedLocalOrientationAfterActiveFaceChanges) {
  auto s = quader::test::BehaviorScenario::new_project();
  s.given().mesh("tetra", local_transform_tetrahedron_document(),
                 "Repeated Local Component Tetrahedron");
  const quader::QuaderModelingObjectRef object = s.object("tetra");
  ASSERT_TRUE(s.editor().selection().select_all().ok());
  s.given().edit_mode();
  ASSERT_TRUE(s.api()
                  .selection()
                  .select_components(tetrahedron_component_selection(
                      object, quader::QuaderSelectionMode::Face))
                  .ok());
  const std::optional<quader::QuaderTransform3> recorded_orientation =
      s.api().query().selection_transform_orientation();
  const std::optional<quader::QVec3> recorded_pivot =
      s.api().query().selection_transform_pivot();
  ASSERT_TRUE(recorded_orientation.has_value());
  ASSERT_TRUE(recorded_pivot.has_value());

  const quader::QuaderTransformDragPreview preview =
      perform_local_component_drag(s, quader::QuaderTransformTool::Rotate,
                                   *recorded_orientation, *recorded_pivot);
  ASSERT_GT(std::abs(preview.rotate_angle_degrees), 1.0);
  std::array<quader::QVec3, 4> once_transformed;
  for (std::size_t index = 0; index < once_transformed.size(); ++index) {
    once_transformed[index] = expected_local_component_position(
        local_transform_tetrahedron_positions()[index], *recorded_pivot,
        *recorded_orientation, preview);
  }

  ASSERT_TRUE(s.api()
                  .selection()
                  .select_components(tetrahedron_component_selection(
                      object, quader::QuaderSelectionMode::Face, true))
                  .ok());
  const std::optional<quader::QuaderTransform3> current_active_orientation =
      s.api().query().selection_transform_orientation();
  ASSERT_TRUE(current_active_orientation.has_value());
  EXPECT_GT(orientation_axis_difference(*recorded_orientation,
                                        *current_active_orientation),
            0.1f);

  s.when().execute(quader::quader_command::kEditRepeatLastAction);
  s.then().command().ok_changed();
  const std::optional<quader::QuaderMeshAsset> repeated_mesh =
      s.api().query().compiled_object_mesh(object);
  ASSERT_TRUE(repeated_mesh.has_value());
  bool recorded_and_current_results_differ = false;
  bool current_orientation_prediction_missing = false;
  for (std::size_t index = 0; index < once_transformed.size(); ++index) {
    const quader::QVec3 expected_recorded =
        expected_local_component_position(
            once_transformed[index], *recorded_pivot, *recorded_orientation,
            preview);
    const quader::QVec3 would_use_current = expected_local_component_position(
        once_transformed[index], *recorded_pivot,
        *current_active_orientation, preview);
    EXPECT_TRUE(mesh_contains_position(*repeated_mesh, expected_recorded))
        << "repeat did not reuse recorded orientation for authored vertex "
        << index + 1u;
    if (quader::length(expected_recorded - would_use_current) > 0.05f) {
      recorded_and_current_results_differ = true;
      current_orientation_prediction_missing =
          current_orientation_prediction_missing ||
          !mesh_contains_position(*repeated_mesh, would_use_current, 0.01f);
    }
  }
  EXPECT_TRUE(recorded_and_current_results_differ);
  EXPECT_TRUE(current_orientation_prediction_missing);
  const quader::QuaderSelectionSummary summary =
      s.api().selection().summary();
  EXPECT_EQ(summary.selected_face_count, 2u);
  EXPECT_EQ(summary.active_face_id, 1u);
}

TEST(ProgrammaticBehaviorScenarioTests,
     RepeatRotateReusesRecordedWorldPivotForEdgeFaceAndObjectSelections) {
  for (const RepeatRotationCase test_case : {
           RepeatRotationCase{
               .mode = quader::QuaderSelectionMode::Edge,
               .ray_origin = {0.0f, 1.0f, 5.0f},
               .label = "edge",
           },
           RepeatRotationCase{
               .mode = quader::QuaderSelectionMode::Face,
               .ray_origin = {0.0f, 0.0f, 5.0f},
               .label = "face",
           },
           RepeatRotationCase{
               .mode = quader::QuaderSelectionMode::Object,
               .label = "object",
           },
       }) {
    SCOPED_TRACE(test_case.label);
    auto s = quader::test::BehaviorScenario::new_project();
    s.given().unit_box("box", "Repeat Rotate Box");
    select_repeat_rotation_target(s, test_case);
    expect_repeat_rotation_selection(s, test_case.mode);

    const std::optional<quader::QVec3> center_before =
        s.api().query().selection_center();
    ASSERT_TRUE(center_before.has_value());
    const quader::test::BehaviorSnapshot topology_before =
        s.snapshot("before repeat rotate");
    const quader::test::MeshSnapshot *mesh_before =
        mesh_snapshot_for_alias(topology_before, "box");
    ASSERT_NE(mesh_before, nullptr);

    s.when()
        .begin_transform_drag(repeat_rotation_drag_options(
            quader::ModelingTransformOperation::Transform))
        .update_transform_drag({100.0f, 80.0f})
        .commit_drag();

    s.then().command().ok_changed();
    expect_repeat_rotation_selection(s, test_case.mode);
    s.then().scene().entity_count(1);
    const std::optional<quader::QVec3> center_after_drag =
        s.api().query().selection_center();
    ASSERT_TRUE(center_after_drag.has_value());
    expect_positive_quarter_turn_step(*center_before, *center_after_drag,
                                      kRepeatRotationWorldPivot);

    const quader::test::BehaviorSnapshot topology_after_drag =
        s.snapshot("after rotate drag");
    const quader::test::MeshSnapshot *mesh_after_drag =
        mesh_snapshot_for_alias(topology_after_drag, "box");
    ASSERT_NE(mesh_after_drag, nullptr);
    if (test_case.mode != quader::QuaderSelectionMode::Object) {
      EXPECT_EQ(mesh_after_drag->vertex_count, mesh_before->vertex_count);
      EXPECT_EQ(mesh_after_drag->index_count, mesh_before->index_count);
    }

    s.when().execute(quader::quader_command::kEditRepeatLastAction);

    s.then().command().ok_changed();
    expect_repeat_rotation_selection(s, test_case.mode);
    s.then().scene().entity_count(1);
    const std::optional<quader::QVec3> center_after_repeat =
        s.api().query().selection_center();
    ASSERT_TRUE(center_after_repeat.has_value());
    expect_positive_quarter_turn_step(*center_after_drag, *center_after_repeat,
                                      kRepeatRotationWorldPivot);

    const quader::test::BehaviorSnapshot topology_after_repeat =
        s.snapshot("after repeated rotate");
    const quader::test::MeshSnapshot *mesh_after_repeat =
        mesh_snapshot_for_alias(topology_after_repeat, "box");
    ASSERT_NE(mesh_after_repeat, nullptr);
    if (test_case.mode != quader::QuaderSelectionMode::Object) {
      EXPECT_EQ(mesh_after_repeat->vertex_count, mesh_before->vertex_count);
      EXPECT_EQ(mesh_after_repeat->index_count, mesh_before->index_count);
    }
  }
}

TEST(ProgrammaticBehaviorScenarioTests,
     RepeatShiftRotateReusesRecordedWorldPivotAndRepeatsExtrudeOrDuplicate) {
  for (const RepeatRotationCase test_case : {
           RepeatRotationCase{
               .mode = quader::QuaderSelectionMode::Edge,
               .ray_origin = {0.0f, 1.0f, 5.0f},
               .label = "edge extrude",
           },
           RepeatRotationCase{
               .mode = quader::QuaderSelectionMode::Face,
               .ray_origin = {0.0f, 0.0f, 5.0f},
               .label = "face extrude",
           },
           RepeatRotationCase{
               .mode = quader::QuaderSelectionMode::Object,
               .label = "object duplicate",
           },
       }) {
    SCOPED_TRACE(test_case.label);
    auto s = quader::test::BehaviorScenario::new_project();
    s.given().unit_box("box", "Repeat Shift Rotate Box");
    select_repeat_rotation_target(s, test_case);
    expect_repeat_rotation_selection(s, test_case.mode);

    const bool object_mode =
        test_case.mode == quader::QuaderSelectionMode::Object;
    const std::optional<quader::QVec3> center_before =
        s.api().query().selection_center();
    ASSERT_TRUE(center_before.has_value());
    const quader::test::BehaviorSnapshot topology_before =
        s.snapshot("before shift rotate");
    const quader::test::MeshSnapshot *mesh_before =
        mesh_snapshot_for_alias(topology_before, "box");
    ASSERT_NE(mesh_before, nullptr);

    s.when()
        .begin_transform_drag(repeat_rotation_drag_options(
            object_mode ? quader::ModelingTransformOperation::Transform
                        : quader::ModelingTransformOperation::Extrude,
            object_mode))
        .update_transform_drag({100.0f, 80.0f})
        .commit_drag();

    s.then().command().ok_changed();
    expect_repeat_rotation_selection(s, test_case.mode);
    if (object_mode) {
      EXPECT_EQ(s.modeling_object_count(), 2u);
    } else {
      s.then().scene().entity_count(1u);
    }
    const std::optional<quader::QVec3> center_after_drag =
        s.api().query().selection_center();
    ASSERT_TRUE(center_after_drag.has_value());
    expect_positive_quarter_turn_step(*center_before, *center_after_drag,
                                      kRepeatRotationWorldPivot);

    const quader::test::BehaviorSnapshot topology_after_drag =
        s.snapshot("after shift rotate");
    const quader::test::MeshSnapshot *mesh_after_drag =
        mesh_snapshot_for_alias(topology_after_drag, "box");
    ASSERT_NE(mesh_after_drag, nullptr);
    if (!object_mode) {
      EXPECT_GT(mesh_after_drag->vertex_count, mesh_before->vertex_count);
      EXPECT_GT(mesh_after_drag->index_count, mesh_before->index_count);
    }

    s.when().execute(quader::quader_command::kEditRepeatLastAction);

    s.then().command().ok_changed();
    expect_repeat_rotation_selection(s, test_case.mode);
    if (object_mode) {
      EXPECT_EQ(s.modeling_object_count(), 3u);
    } else {
      s.then().scene().entity_count(1u);
    }
    const std::optional<quader::QVec3> center_after_repeat =
        s.api().query().selection_center();
    ASSERT_TRUE(center_after_repeat.has_value());
    expect_positive_quarter_turn_step(*center_after_drag, *center_after_repeat,
                                      kRepeatRotationWorldPivot);

    const quader::test::BehaviorSnapshot topology_after_repeat =
        s.snapshot("after repeated shift rotate");
    const quader::test::MeshSnapshot *mesh_after_repeat =
        mesh_snapshot_for_alias(topology_after_repeat, "box");
    ASSERT_NE(mesh_after_repeat, nullptr);
    if (!object_mode) {
      EXPECT_GT(mesh_after_repeat->vertex_count, mesh_after_drag->vertex_count);
      EXPECT_GT(mesh_after_repeat->index_count, mesh_after_drag->index_count);
    }
  }
}

TEST(ProgrammaticBehaviorScenarioTests, FaceGizmoDragMovesOnlyPickedFaceAndKeepsComponentSelection) {
  auto s = quader::test::BehaviorScenario::new_project();

  s.given()
      .unit_box("box", "Face Gizmo Drag Box")
      .edit_mode().selection_mode(quader::QuaderSelectionMode::Face);

  s.when()
      .pick({0.0f, 0.0f, 5.0f})
      .begin_x_move_drag()
      .update_x_move_drag(120.0f)
      .commit_drag();

  s.then().command().ok_changed();
  s.then()
      .selection()
      .mode(quader::QuaderSelectionMode::Face)
      .face_count(1)
      .active_alias("box");
  s.then()
      .mesh("box")
      .bounds_min_near({-1.0f, -1.0f, -1.0f})
      .bounds_max_near({3.0f, 1.0f, 1.0f});
}

TEST(ProgrammaticBehaviorScenarioTests, ShiftFaceMoveAndRotateExtrudeAndShiftFaceScaleInsetsThroughGizmoDrag) {
    {
      auto s = quader::test::BehaviorScenario::new_project();
      s.given()
          .unit_box("box", "Shift Extrude Box")
          .edit_mode().selection_mode(quader::QuaderSelectionMode::Face);
      s.when().pick({0.0f, 0.0f, 5.0f});
      const quader::test::BehaviorSnapshot before =
          s.snapshot("before shift extrude");
      ASSERT_EQ(before.meshes.size(), 1u);

      s.when()
          .begin_transform_drag({
              .tool = quader::ModelingTransformTool::Move,
              .operation = quader::ModelingTransformOperation::Extrude,
          })
          .update_transform_drag(120.0f)
          .commit_drag();

      s.then().command().ok_changed();
      s.then()
          .selection()
          .mode(quader::QuaderSelectionMode::Face)
          .face_count(1)
          .active_alias("box");
      s.then().mesh("box").vertices_greater_than(before.meshes.front());
    }

    {
      auto s = quader::test::BehaviorScenario::new_project();
      s.given()
          .unit_box("box", "Shift Rotate Extrude Box")
          .edit_mode().selection_mode(quader::QuaderSelectionMode::Face);
      s.when().pick({0.0f, 0.0f, 5.0f});
      const quader::test::BehaviorSnapshot before =
          s.snapshot("before shift rotate extrude");
      ASSERT_EQ(before.meshes.size(), 1u);

      s.when()
          .begin_transform_drag({
              .tool = quader::ModelingTransformTool::Rotate,
              .axis = quader::ModelingTransformAxis::Z,
              .operation = quader::ModelingTransformOperation::Extrude,
              .screen_position = {120.0f, 100.0f},
              .screen_pivot = {100.0f, 100.0f},
              .screen_tip = {120.0f, 100.0f},
              .world_direction = {0.0f, 0.0f, 1.0f},
          })
          .update_transform_drag({116.0f, 112.0f})
          .commit_drag();

      s.then().command().ok_changed();
      s.then()
          .selection()
          .mode(quader::QuaderSelectionMode::Face)
          .face_count(1)
          .active_alias("box");
      s.then().mesh("box").vertices_greater_than(before.meshes.front());
    }

    {
      auto s = quader::test::BehaviorScenario::new_project();
      s.given()
          .unit_box("box", "Shift Inset Box")
          .edit_mode().selection_mode(quader::QuaderSelectionMode::Face);
      s.when().pick({0.0f, 0.0f, 5.0f});
      const quader::test::BehaviorSnapshot before =
          s.snapshot("before shift inset");
      ASSERT_EQ(before.meshes.size(), 1u);

      s.when()
          .begin_transform_drag({
              .tool = quader::ModelingTransformTool::Scale,
              .operation = quader::ModelingTransformOperation::Inset,
          })
          .update_transform_drag(120.0f)
          .commit_drag();

      s.then().command().ok_changed();
      s.then()
          .selection()
          .mode(quader::QuaderSelectionMode::Face)
          .face_count(1)
          .active_alias("box");
      s.then().mesh("box").vertices_greater_than(before.meshes.front());
    }
}

TEST(ProgrammaticBehaviorScenarioTests,
     ShiftMoveExtrudesPolyToolFaceWithOpenBaseAndUndoRedoPreservesIt) {
  quader_poly::Document document;
  const std::array vertices{
      quader_poly::add_vertex(document, {-1.0f, 0.0f, -1.0f}),
      quader_poly::add_vertex(document, {-1.0f, 0.0f, 1.0f}),
      quader_poly::add_vertex(document, {1.0f, 0.0f, 1.0f}),
      quader_poly::add_vertex(document, {1.0f, 0.0f, -1.0f}),
  };
  const quader_poly::ElementId face =
      quader_poly::add_face(document, vertices);
  ASSERT_NE(face, quader_poly::kInvalidElementId);

  auto s = quader::test::BehaviorScenario::new_project();
  s.given()
      .mesh("poly",
            quader::QuaderPolygonDocumentInternalAccess::from_backend(
                std::move(document)),
            "Poly Shift Extrude")
      .edit_mode().selection_mode(quader::QuaderSelectionMode::Face)
      .face_selected("poly", static_cast<int>(face));

  const quader::test::BehaviorSnapshot before =
      s.snapshot("before poly shift extrude");
  const quader::test::MeshSnapshot *before_mesh =
      mesh_snapshot_for_alias(before, "poly");
  ASSERT_NE(before_mesh, nullptr);
  EXPECT_EQ(before_mesh->index_count, 6u);

  s.when()
      .begin_transform_drag({
          .tool = quader::ModelingTransformTool::Move,
          .axis = quader::ModelingTransformAxis::Y,
          .operation = quader::ModelingTransformOperation::Extrude,
          .world_direction = {0.0f, 1.0f, 0.0f},
      })
      .update_transform_drag(120.0f)
      .commit_drag();

  s.then().command().ok_changed();
  s.then()
      .selection()
      .mode(quader::QuaderSelectionMode::Face)
      .face_count(1)
      .active_alias("poly")
      .active_face(static_cast<int>(face));
  s.then()
      .mesh("poly")
      .index_count(30u)
      .bounds_min_near({-1.0f, 0.0f, -1.0f});
  const quader::test::BehaviorSnapshot after_extrude =
      s.snapshot("after poly shift extrude");
  const quader::test::MeshSnapshot *extruded_mesh =
      mesh_snapshot_for_alias(after_extrude, "poly");
  ASSERT_NE(extruded_mesh, nullptr);
  EXPECT_GT(extruded_mesh->bounds.max.y, 0.0f);
  expect_compiled_mesh_open_base_and_winding_coherent(s, "poly");

  s.when().undo();

  s.then().command().ok_changed();
  s.then()
      .selection()
      .mode(quader::QuaderSelectionMode::Face)
      .face_count(1)
      .active_alias("poly")
      .active_face(static_cast<int>(face));
  const quader::test::BehaviorSnapshot after_undo =
      s.snapshot("after poly shift extrude undo");
  const quader::test::MeshSnapshot *undo_mesh =
      mesh_snapshot_for_alias(after_undo, "poly");
  ASSERT_NE(undo_mesh, nullptr);
  EXPECT_EQ(undo_mesh->index_count, before_mesh->index_count);
  expect_mesh_bounds_near(*undo_mesh, *before_mesh);

  s.when().redo();

  s.then().command().ok_changed();
  s.then()
      .selection()
      .mode(quader::QuaderSelectionMode::Face)
      .face_count(1)
      .active_alias("poly")
      .active_face(static_cast<int>(face));
  s.then().mesh("poly").index_count(30u);
  expect_compiled_mesh_open_base_and_winding_coherent(s, "poly");
}

TEST(ProgrammaticBehaviorScenarioTests, ShiftRotateExtrudeUsesWorldPivotForTransformedComponentSelection) {
  auto s = quader::test::BehaviorScenario::new_project();
  s.given()
      .unit_box("box", "Shift Rotate Scene Pivot Box")
      .transform("box", {.location = {10.0f, 0.0f, 0.0f}})
      .edit_mode().selection_mode(quader::QuaderSelectionMode::Face);

  s.when().pick({10.0f, 0.0f, 5.0f});
  ASSERT_TRUE(s.api().query().selection_center().has_value());
  EXPECT_NEAR(s.api().query().selection_center()->x, 10.0f, 0.001f);
  EXPECT_NEAR(s.api().query().selection_center()->y, 0.0f, 0.001f);

  s.when()
      .begin_transform_drag({
          .tool = quader::ModelingTransformTool::Rotate,
          .axis = quader::ModelingTransformAxis::Z,
          .operation = quader::ModelingTransformOperation::Extrude,
          .screen_position = {120.0f, 100.0f},
          .screen_pivot = {100.0f, 100.0f},
          .screen_tip = {120.0f, 100.0f},
          .world_direction = {0.0f, 0.0f, 1.0f},
      })
      .update_transform_drag({116.0f, 112.0f})
      .commit_drag();

  const std::optional<quader::QVec3> selected_center =
      s.api().query().selection_center();
  ASSERT_TRUE(selected_center.has_value());
  EXPECT_NEAR(selected_center->x, 10.0f, 0.001f);
  EXPECT_NEAR(selected_center->y, 0.0f, 0.001f);
}

TEST(ProgrammaticBehaviorScenarioTests, BridgeAfterShiftRotateExtrudeKeepsTransformedSourceInWorldPlace) {
  auto s = quader::test::BehaviorScenario::new_project();
  s.given()
      .unit_box("source", "Bridge Shift Rotate Source")
      .transform("source", {.location = {10.0f, 0.0f, 0.0f}})
      .box("target",
           {.min = {13.0f, -1.0f, -1.0f},
            .max = {15.0f, 1.0f, 1.0f}},
           "Bridge Target")
      .selected_aliases({"source", "target"}, "target").edit_mode().selection_mode(quader::QuaderSelectionMode::Face);

  s.when()
      .pick({10.0f, 0.0f, 5.0f})
      .begin_transform_drag({
          .tool = quader::ModelingTransformTool::Rotate,
          .axis = quader::ModelingTransformAxis::Z,
          .operation = quader::ModelingTransformOperation::Extrude,
          .screen_position = {120.0f, 100.0f},
          .screen_pivot = {100.0f, 100.0f},
          .screen_tip = {120.0f, 100.0f},
          .world_direction = {0.0f, 0.0f, 1.0f},
      })
      .update_transform_drag({116.0f, 112.0f})
      .commit_drag();

  const quader::test::BehaviorSnapshot before_bridge =
      s.snapshot("before bridge");
  const quader::test::MeshSnapshot *source_before_bridge =
      mesh_snapshot_for_alias(before_bridge, "source");
  ASSERT_NE(source_before_bridge, nullptr);

  s.when().pick({14.0f, 0.0f, 5.0f}, {.shift_pressed = true});
  s.then().selection().mode(quader::QuaderSelectionMode::Face).face_count(2);

  s.when().execute(quader::quader_command::kModelingBridgeFaces);

  s.then().command().ok_changed();
  s.then().scene().selected_alias_is("target");
  s.then()
      .mesh("target")
      .bounds_min_near({8.633974f, -1.366026f, -1.0f}, 0.01f);

  s.when().undo();

  s.then().command().ok_changed();
  const quader::test::BehaviorSnapshot after_undo =
      s.snapshot("after bridge undo");
  const quader::test::MeshSnapshot *source_after_undo =
      mesh_snapshot_for_alias(after_undo, "source");
  ASSERT_NE(source_after_undo, nullptr);
  expect_mesh_bounds_near(*source_after_undo, *source_before_bridge, 0.001f);
}

TEST(ProgrammaticBehaviorScenarioTests,
     InterpolatedBridgePreviewUpdateCommitKeepsSceneTransformedSourceInWorldPlace) {
  auto s = quader::test::BehaviorScenario::new_project();
  s.given()
      .unit_box("source", "Bridge Preview Source")
      .transform("source", {.location = {10.0f, 0.0f, 0.0f}})
      .unit_box("target", "Bridge Preview Target")
      .transform("target", {.location = {14.0f, 0.0f, 0.0f}})
      .selected_aliases({"source", "target"}, "target").edit_mode().selection_mode(quader::QuaderSelectionMode::Face);

  const quader::QuaderApiResult<quader::QuaderSelectionSummary> source_pick =
      s.api().selection().pick_select({
          .ray = {.origin = {12.0f, 0.0f, 0.0f},
                  .direction = {-1.0f, 0.0f, 0.0f}},
          .click_count = 1,
      });
  ASSERT_TRUE(source_pick.ok()) << source_pick.error_message();
  const quader::QuaderApiResult<quader::QuaderSelectionSummary> target_pick =
      s.api().selection().pick_select({
          .ray = {.origin = {12.0f, 0.0f, 0.0f},
                  .direction = {1.0f, 0.0f, 0.0f}},
          .click_count = 1,
          .shift_pressed = true,
      });
  ASSERT_TRUE(target_pick.ok()) << target_pick.error_message();
  s.then()
      .selection()
      .mode(quader::QuaderSelectionMode::Face)
      .face_count(2)
      .active_alias("target");

  const quader::test::BehaviorSnapshot before_preview =
      s.snapshot("before bridge preview");
  const quader::test::MeshSnapshot *source_before =
      mesh_snapshot_for_alias(before_preview, "source");
  const quader::test::MeshSnapshot *target_before =
      mesh_snapshot_for_alias(before_preview, "target");
  ASSERT_NE(source_before, nullptr);
  ASSERT_NE(target_before, nullptr);

  const quader::QuaderApiVoidResult begin =
      s.api().modeling().begin_preview("interpolated_bridge_faces");
  ASSERT_TRUE(begin.ok()) << begin.error_message();
  EXPECT_TRUE(begin.changed());
  s.then()
      .mesh("target")
      .bounds_min_near({-5.0f, -1.0f, -1.0f})
      .bounds_max_near({1.0f, 1.0f, 1.0f});

  quader::QuaderPreviewSettings settings;
  settings.interaction.bridge_steps = 3;
  const quader::QuaderApiVoidResult update =
      s.api().modeling().update_preview(settings);
  ASSERT_TRUE(update.ok()) << update.error_message();
  EXPECT_TRUE(update.changed());
  s.then()
      .mesh("target")
      .bounds_min_near({-5.0f, -1.0f, -1.0f})
      .bounds_max_near({1.0f, 1.0f, 1.0f});

  const quader::QuaderApiVoidResult commit =
      s.api().modeling().commit_preview("Interpolated Bridge");
  ASSERT_TRUE(commit.ok()) << commit.error_message();
  EXPECT_TRUE(commit.changed());
  s.then()
      .mesh("target")
      .bounds_min_near({-5.0f, -1.0f, -1.0f})
      .bounds_max_near({1.0f, 1.0f, 1.0f});

  s.when().undo();

  s.then().command().ok_changed();
  const quader::test::BehaviorSnapshot after_undo =
      s.snapshot("after bridge preview undo");
  const quader::test::MeshSnapshot *source_after_undo =
      mesh_snapshot_for_alias(after_undo, "source");
  const quader::test::MeshSnapshot *target_after_undo =
      mesh_snapshot_for_alias(after_undo, "target");
  ASSERT_NE(source_after_undo, nullptr);
  ASSERT_NE(target_after_undo, nullptr);
  expect_mesh_bounds_near(*source_after_undo, *source_before);
  expect_mesh_bounds_near(*target_after_undo, *target_before);
}

TEST(ProgrammaticBehaviorScenarioTests,
     BridgeFacesPreservesWindingForMirroredSceneSource) {
  auto s = quader::test::BehaviorScenario::new_project();
  s.given()
      .unit_box("source", "Mirrored Bridge Source")
      .transform("source",
                 {.location = {10.0f, 0.0f, 0.0f},
                  .scale = {-1.0f, 1.0f, 1.0f}})
      .unit_box("target", "Mirrored Bridge Target")
      .transform("target", {.location = {14.0f, 0.0f, 0.0f}})
      .selected_aliases({"source", "target"}, "target").edit_mode().selection_mode(quader::QuaderSelectionMode::Face);

  ASSERT_TRUE(s.api()
                  .selection()
                  .pick_select({
                      .ray = {.origin = {12.0f, 0.0f, 0.0f},
                              .direction = {-1.0f, 0.0f, 0.0f}},
                      .click_count = 1,
                  })
                  .ok());
  ASSERT_TRUE(s.api()
                  .selection()
                  .pick_select({
                      .ray = {.origin = {12.0f, 0.0f, 0.0f},
                              .direction = {1.0f, 0.0f, 0.0f}},
                      .click_count = 1,
                      .shift_pressed = true,
                  })
                  .ok());
  s.then()
      .selection()
      .mode(quader::QuaderSelectionMode::Face)
      .face_count(2)
      .active_alias("target");

  s.when().execute(quader::quader_command::kModelingBridgeFaces);

  s.then().command().ok_changed();
  s.then().scene().selected_alias_is("target");
  expect_compiled_mesh_winding_coherent(s, "target");
}

TEST(ProgrammaticBehaviorScenarioTests,
     InterpolatedBridgePreviewPreservesWindingForMirroredSceneSource) {
  auto s = quader::test::BehaviorScenario::new_project();
  s.given()
      .unit_box("source", "Mirrored Bridge Preview Source")
      .transform("source",
                 {.location = {10.0f, 0.0f, 0.0f},
                  .scale = {-1.0f, 1.0f, 1.0f}})
      .unit_box("target", "Mirrored Bridge Preview Target")
      .transform("target", {.location = {14.0f, 0.0f, 0.0f}})
      .selected_aliases({"source", "target"}, "target").edit_mode().selection_mode(quader::QuaderSelectionMode::Face);

  ASSERT_TRUE(s.api()
                  .selection()
                  .pick_select({
                      .ray = {.origin = {12.0f, 0.0f, 0.0f},
                              .direction = {-1.0f, 0.0f, 0.0f}},
                      .click_count = 1,
                  })
                  .ok());
  ASSERT_TRUE(s.api()
                  .selection()
                  .pick_select({
                      .ray = {.origin = {12.0f, 0.0f, 0.0f},
                              .direction = {1.0f, 0.0f, 0.0f}},
                      .click_count = 1,
                      .shift_pressed = true,
                  })
                  .ok());
  s.then()
      .selection()
      .mode(quader::QuaderSelectionMode::Face)
      .face_count(2)
      .active_alias("target");

  const quader::QuaderApiVoidResult begin =
      s.api().modeling().begin_preview("interpolated_bridge_faces");
  ASSERT_TRUE(begin.ok()) << begin.error_message();
  EXPECT_TRUE(begin.changed());
  expect_compiled_mesh_winding_coherent(s, "target");

  quader::QuaderPreviewSettings settings;
  settings.interaction.bridge_steps = 3;
  const quader::QuaderApiVoidResult update =
      s.api().modeling().update_preview(settings);
  ASSERT_TRUE(update.ok()) << update.error_message();
  EXPECT_TRUE(update.changed());
  expect_compiled_mesh_winding_coherent(s, "target");

  const quader::QuaderApiVoidResult commit =
      s.api().modeling().commit_preview("Interpolated Bridge");
  ASSERT_TRUE(commit.ok()) << commit.error_message();
  EXPECT_TRUE(commit.changed());
  expect_compiled_mesh_winding_coherent(s, "target");
}

TEST(ProgrammaticBehaviorScenarioTests,
     ShiftRotateExtrudeKeepsWorldCenterStableAndAddsTopologyForMirroredAndPivotEditedSelection) {
  {
    auto s = quader::test::BehaviorScenario::new_project();
    s.given()
        .unit_box("box", "Mirrored Shift Rotate Extrude Box")
        .transform("box",
                   {.location = {10.0f, 0.0f, 0.0f},
                    .scale = {-1.0f, 1.0f, 1.0f}})
        .edit_mode().selection_mode(quader::QuaderSelectionMode::Face);

    s.when().pick({10.0f, 0.0f, 5.0f});
    run_shift_rotate_extrude_stability_case(s, "box");
  }

  {
    auto s = quader::test::BehaviorScenario::new_project();
    s.given().unit_box("box", "Pivot Shift Rotate Extrude Box").selected("box");
    ASSERT_TRUE(s.editor().tools().activate(quader::ViewportToolId::Pivot).ok());
    ASSERT_TRUE(s.editor()
                    .tools()
                    .execute_action("pivot", "pivot.align.top_center")
                    .ok());
    ASSERT_TRUE(s.editor().tools().commit_active_tool_preview().ok());

    s.given().edit_mode().selection_mode(quader::QuaderSelectionMode::Face);
    s.when().pick({0.0f, 0.0f, 5.0f});
    run_shift_rotate_extrude_stability_case(s, "box");
  }
}

TEST(ProgrammaticBehaviorScenarioTests, CombineMeshesConsumesTransformedSourcesInSurvivorSpace) {
  auto s = quader::test::BehaviorScenario::new_project();
  s.given()
      .unit_box("source", "Combine Source")
      .transform("source", {.location = {10.0f, 0.0f, 0.0f}})
      .box("target",
           {.min = {13.0f, -1.0f, -1.0f},
            .max = {15.0f, 1.0f, 1.0f}},
           "Combine Target")
      .selection_mode(quader::QuaderSelectionMode::Object)
      .selected_aliases({"source", "target"}, "target");

  s.when().execute(quader::quader_command::kModelingCombineMeshes);

  s.then().command().ok_changed();
  s.then().scene().selected_alias_is("target");
  s.then()
      .mesh("target")
      .bounds_min_near({9.0f, -1.0f, -1.0f}, 0.01f)
      .bounds_max_near({15.0f, 1.0f, 1.0f}, 0.01f);
}

TEST(ProgrammaticBehaviorScenarioTests,
     CombineMeshesPreservesWindingForMirroredSceneSource) {
  auto s = quader::test::BehaviorScenario::new_project();
  s.given()
      .unit_box("source", "Mirrored Combine Source")
      .transform("source",
                 {.location = {10.0f, 0.0f, 0.0f},
                  .scale = {-1.0f, 1.0f, 1.0f}})
      .unit_box("target", "Mirrored Combine Target")
      .transform("target", {.location = {14.0f, 0.0f, 0.0f}})
      .selection_mode(quader::QuaderSelectionMode::Object)
      .selected_aliases({"source", "target"}, "target");

  s.when().execute(quader::quader_command::kModelingCombineMeshes);

  s.then().command().ok_changed();
  s.then().scene().selected_alias_is("target");
  expect_compiled_mesh_winding_coherent(s, "target");
}

TEST(ProgrammaticBehaviorScenarioTests, RayPickingAndContextClickSemanticsPreserveComponentAuthority) {
    {
      auto s = quader::test::BehaviorScenario::new_project();
      s.given()
          .unit_box("box", "Face Pick Box")
          .edit_mode().selection_mode(quader::QuaderSelectionMode::Face);
      s.when().pick({0.0f, 0.0f, 5.0f});
      s.then()
          .selection()
          .mode(quader::QuaderSelectionMode::Face)
          .face_count(1)
          .active_alias("box");

      const quader::ModelingSelectionSummary selected =
          s.snapshot("selected face").selection;
      s.when().pick({0.0f, 0.0f, 5.0f},
                    {.preserve_selection_on_selected_hit = true});
      s.then().selection().revision_same_as(selected);
    }

    {
      auto s = quader::test::BehaviorScenario::new_project();
      s.given()
          .unit_box("box", "Edge Pick Box")
          .edit_mode().selection_mode(quader::QuaderSelectionMode::Edge);
      s.when().pick({0.0f, 1.0f, 5.0f});
      s.then()
          .selection()
          .mode(quader::QuaderSelectionMode::Edge)
          .edge_count(1)
          .active_alias("box");
    }

    {
      auto s = quader::test::BehaviorScenario::new_project();
      s.given()
          .unit_box("box", "Vertex Pick Box")
          .edit_mode().selection_mode(quader::QuaderSelectionMode::Vertex);
      s.when().pick({1.0f, 1.0f, 5.0f});
      s.then()
          .selection()
          .mode(quader::QuaderSelectionMode::Vertex)
          .vertex_count(1)
          .active_alias("box");
    }

    {
      auto s = quader::test::BehaviorScenario::new_project();
      quader::QuaderTransform transform;
      transform.location = {4.0f, 0.0f, 0.0f};
      s.given()
          .unit_box("box", "Transformed Pick Box")
          .transform("box", transform)
          .edit_mode().selection_mode(quader::QuaderSelectionMode::Face);
      s.when().pick({4.0f, 0.0f, 5.0f});
      s.then()
          .selection()
          .mode(quader::QuaderSelectionMode::Face)
          .face_count(1)
          .active_alias("box");
    }

    {
      auto s = quader::test::BehaviorScenario::new_project();
      s.given()
          .unit_box("left", "Left Pick Box")
          .box("right",
               {.min = {4.0f, -1.0f, -1.0f}, .max = {6.0f, 1.0f, 1.0f}},
               "Right Pick Box")
          .selected("left");
      s.when().pick({5.0f, 0.0f, 5.0f});
      s.then()
          .selection()
          .mode(quader::QuaderSelectionMode::Object)
          .object_count(1)
          .active_alias("right");
    }
}

TEST(ProgrammaticBehaviorScenarioTests, SelectionAndHoverOverlaysAreBoundedDistinctDataLayers) {
    {
      auto s = quader::test::BehaviorScenario::new_project();
      s.given()
          .unit_box("box", "Face Overlay Box")
          .edit_mode().selection_mode(quader::QuaderSelectionMode::Face);
      s.when().pick({0.0f, 0.0f, 5.0f});
      s.then()
          .overlay()
          .has_triangle(quader::ViewportOverlayLayer::SelectedVisibleFace)
          .has_line(quader::ViewportOverlayLayer::SelectedFaceEdge)
          .triangle_bounds_within(
              quader::ViewportOverlayLayer::SelectedVisibleFace,
              {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}})
          .line_bounds_within(
              quader::ViewportOverlayLayer::SelectedFaceEdge,
              {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}})
          .finite_coordinates();

      s.when().hover({0.0f, 1.0f, 5.0f});
      s.then()
          .overlay()
          .has_triangle(quader::ViewportOverlayLayer::SelectedVisibleFace)
          .has_triangle(quader::ViewportOverlayLayer::HoverFace)
          .finite_coordinates();
    }

    {
      auto s = quader::test::BehaviorScenario::new_project();
      s.given()
          .unit_box("box", "Edge Overlay Box")
          .edit_mode().selection_mode(quader::QuaderSelectionMode::Edge);
      s.when().pick({0.0f, 1.0f, 5.0f});
      s.then()
          .overlay()
          .has_line(quader::ViewportOverlayLayer::SelectedEdge)
          .line_bounds_within(
              quader::ViewportOverlayLayer::SelectedEdge,
              {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}})
          .finite_coordinates();
    }

    {
      auto s = quader::test::BehaviorScenario::new_project();
      s.given()
          .unit_box("box", "Vertex Overlay Box")
          .edit_mode().selection_mode(quader::QuaderSelectionMode::Vertex);
      s.when().pick({1.0f, 1.0f, 5.0f});
      s.then()
          .overlay()
          .has_point(quader::ViewportOverlayLayer::SelectedVertex)
          .point_bounds_center_near(
              quader::ViewportOverlayLayer::SelectedVertex, {1.0f, 1.0f, 1.0f})
          .finite_coordinates();
    }
}
