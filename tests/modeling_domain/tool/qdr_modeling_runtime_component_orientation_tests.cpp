////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <editor/modeling/runtime/component_orientation.hpp>
#include <quader/modeling/neutral/polygon/document.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace {

constexpr float kEpsilon = 0.0001F;

bool vector_near(quader::QVec3 left, quader::QVec3 right,
                 float epsilon = kEpsilon) {
  return quader::length_squared(left - right) <= epsilon * epsilon;
}

void expect_orthonormal(const quader_poly::Transform3 &frame) {
  EXPECT_NEAR(quader::length(frame.x_axis), 1.0F, kEpsilon);
  EXPECT_NEAR(quader::length(frame.y_axis), 1.0F, kEpsilon);
  EXPECT_NEAR(quader::length(frame.z_axis), 1.0F, kEpsilon);
  EXPECT_NEAR(quader::dot(frame.x_axis, frame.y_axis), 0.0F, kEpsilon);
  EXPECT_NEAR(quader::dot(frame.x_axis, frame.z_axis), 0.0F, kEpsilon);
  EXPECT_NEAR(quader::dot(frame.y_axis, frame.z_axis), 0.0F, kEpsilon);
  EXPECT_TRUE(vector_near(quader::cross(frame.x_axis, frame.y_axis),
                          frame.z_axis));
}

void expect_axes(const quader_poly::Transform3 &frame, quader::QVec3 x_axis,
                 quader::QVec3 y_axis, quader::QVec3 z_axis) {
  expect_orthonormal(frame);
  EXPECT_TRUE(vector_near(frame.x_axis, x_axis));
  EXPECT_TRUE(vector_near(frame.y_axis, y_axis));
  EXPECT_TRUE(vector_near(frame.z_axis, z_axis));
}

/// Reproduces the thin border face from test_scene.qdr's Box mesh.
quader_poly::Document make_box_border(std::size_t first_corner = 0U,
                                      bool reverse_vertex_ids = false) {
  constexpr std::array<quader::QVec3, 4> kPositions{{
      {3.0F, -0.5F, 3.5F},
      {3.0F, -0.2142857313156128F, 3.0F},
      {3.0F, -0.2142857313156128F, -3.0F},
      {3.0F, -0.5F, -3.5F},
  }};
  quader_poly::Document document;
  std::array<quader_poly::ElementId, kPositions.size()> vertices{};
  for (std::size_t index = 0U; index < kPositions.size(); ++index) {
    const std::size_t corner =
        reverse_vertex_ids ? kPositions.size() - 1U - index : index;
    vertices[corner] = quader_poly::add_vertex(document, kPositions[corner]);
  }
  std::rotate(vertices.begin(), vertices.begin() + first_corner, vertices.end());
  EXPECT_NE(quader_poly::add_face(document, vertices),
            quader_poly::kInvalidElementId);
  return document;
}

/// Owns a non-axis-aligned triangle and the stable IDs used by frame tests.
struct TiltedTriangle {
  quader_poly::Document document;
  quader_poly::ElementId first = quader_poly::kInvalidElementId;
  quader_poly::ElementId second = quader_poly::kInvalidElementId;
  quader_poly::ElementId third = quader_poly::kInvalidElementId;
  quader_poly::ElementId face = quader_poly::kInvalidElementId;
};

TiltedTriangle make_tilted_triangle() {
  TiltedTriangle fixture;
  fixture.first =
      quader_poly::add_vertex(fixture.document, {0.0F, 0.0F, 0.0F});
  fixture.second =
      quader_poly::add_vertex(fixture.document, {1.0F, 0.0F, 0.0F});
  fixture.third =
      quader_poly::add_vertex(fixture.document, {0.0F, 1.0F, 1.0F});
  const std::array vertices{fixture.first, fixture.second, fixture.third};
  fixture.face = quader_poly::add_face(fixture.document, vertices);
  return fixture;
}

TEST(QdrModelingRuntimeComponentOrientationTests,
     BoxBorderFaceUsesTextureAxesAcrossPerimeterStartsAndVertexIds) {
  for (const bool reverse_vertex_ids : {false, true}) {
    for (std::size_t first_corner = 0U; first_corner < 4U; ++first_corner) {
      SCOPED_TRACE(::testing::Message() << "first corner " << first_corner
                                      << ", reverse IDs " << reverse_vertex_ids);
      const quader_poly::Document document =
          make_box_border(first_corner, reverse_vertex_ids);
      const auto frame = quader_editor_core::component_face_local_orientation(
          document, document.faces.front().id);

      ASSERT_TRUE(frame.has_value());
      expect_axes(*frame, {0.0F, 0.0F, 1.0F}, {0.0F, 1.0F, 0.0F},
                  {-1.0F, 0.0F, 0.0F});
      EXPECT_TRUE(vector_near(frame->origin,
                              {3.0F, -0.3571428656578064F, 0.0F}));
    }
  }
}

TEST(QdrModelingRuntimeComponentOrientationTests,
     FaceUsesAuthoredTextureGradientWithCollinearLeadingCorners) {
  constexpr std::array<quader::QVec3, 5> kPositions{{
      {3.0F, 0.0F, 0.0F}, {3.0F, 0.0F, 1.0F}, {3.0F, 0.0F, 2.0F},
      {3.0F, 1.0F, 2.0F}, {3.0F, 1.0F, 0.0F},
  }};
  const float inverse_sqrt_two = 1.0F / std::sqrt(2.0F);
  for (std::size_t first_corner = 0U; first_corner < kPositions.size();
       ++first_corner) {
    SCOPED_TRACE(first_corner);
    quader_poly::Document document;
    std::array<quader_poly::ElementId, kPositions.size()> vertices{};
    for (std::size_t corner = 0U; corner < kPositions.size(); ++corner) {
      vertices[corner] = quader_poly::add_vertex(document, kPositions[corner]);
    }
    std::rotate(vertices.begin(), vertices.begin() + first_corner,
                vertices.end());
    ASSERT_NE(quader_poly::add_face(document, vertices),
              quader_poly::kInvalidElementId);
    auto &face = document.faces.front();
    for (std::size_t corner = 0U; corner < kPositions.size(); ++corner) {
      const auto position = kPositions[(corner + first_corner) % kPositions.size()];
      face.uvs.push_back({position.z, position.z - position.y});
    }
    const auto frame = quader_editor_core::component_face_local_orientation(
        document, face.id);

    ASSERT_TRUE(frame.has_value());
    expect_axes(*frame, {0.0F, inverse_sqrt_two, inverse_sqrt_two},
                {0.0F, inverse_sqrt_two, -inverse_sqrt_two},
                {-1.0F, 0.0F, 0.0F});
  }
}

TEST(QdrModelingRuntimeComponentOrientationTests,
     FaceTextureRotationAndSignedVScaleDetermineItsAxes) {
  struct TextureCase {
    quader_poly::FaceTextureTransform transform;
    quader::QVec3 x_axis;
    quader::QVec3 y_axis;
  };
  const std::array<TextureCase, 4> cases{{
      {{.scale = {1.0F, 1.0F}}, {0.0F, 0.0F, 1.0F}, {0.0F, 1.0F, 0.0F}},
      {{.scale = {3.0F, 4.0F},
        .shift_uv = {10.0F, -20.0F},
        .rotation_degrees = 90.0F},
       {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, -1.0F}},
      {{.scale = {3.0F, -4.0F}, .rotation_degrees = 90.0F},
       {0.0F, -1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}},
      {{.scale = {-2.0F, 3.0F}}, {0.0F, 0.0F, 1.0F}, {0.0F, 1.0F, 0.0F}},
  }};
  for (std::size_t index = 0U; index < cases.size(); ++index) {
    SCOPED_TRACE(index);
    quader_poly::Document document = make_box_border();
    auto &face = document.faces.front();
    for (const auto &vertex : document.vertices) {
      face.uvs.push_back({vertex.position.z, -vertex.position.y});
    }
    face.texture_transform = cases[index].transform;
    const auto frame = quader_editor_core::component_face_local_orientation(
        document, face.id);

    ASSERT_TRUE(frame.has_value());
    expect_axes(*frame, cases[index].x_axis, cases[index].y_axis,
                {-1.0F, 0.0F, 0.0F});
  }
}

TEST(QdrModelingRuntimeComponentOrientationTests,
     FaceOrientationIgnoresPositiveTextureScaleMagnitude) {
  quader_poly::Document document;
  const std::array vertices{
      quader_poly::add_vertex(document, {0.0F, 0.0F, 0.0F}),
      quader_poly::add_vertex(document, {1.0F, 0.0F, 0.0F}),
      quader_poly::add_vertex(document, {1.0F, 1.0F, 0.0F}),
      quader_poly::add_vertex(document, {0.0F, 1.0F, 0.0F}),
  };
  ASSERT_NE(quader_poly::add_face(document, vertices),
            quader_poly::kInvalidElementId);
  auto &face = document.faces.front();
  face.uvs = {{0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}};

  for (const float scale : {1.0F, 1.0e7F, 1.0e-7F}) {
    SCOPED_TRACE(scale);
    face.texture_transform.scale = {scale, scale};
    const auto frame = quader_editor_core::component_face_local_orientation(
        document, face.id);

    ASSERT_TRUE(frame.has_value());
    expect_axes(*frame, {-1.0F, 0.0F, 0.0F}, {0.0F, -1.0F, 0.0F},
                {0.0F, 0.0F, 1.0F});
    EXPECT_TRUE(vector_near(frame->origin, {0.5F, 0.5F, 0.0F}));
  }
}

TEST(QdrModelingRuntimeComponentOrientationTests,
     DegenerateTextureMappingUsesStableObjectTangentAndFaceNormal) {
  const quader_poly::Transform3 fallback{
      .x_axis = {0.0F, 1.0F, 0.0F},
      .y_axis = {0.0F, 0.0F, -1.0F},
      .z_axis = {-1.0F, 0.0F, 0.0F},
  };
  for (std::size_t first_corner = 0U; first_corner < 4U; ++first_corner) {
    SCOPED_TRACE(first_corner);
    quader_poly::Document document = make_box_border(first_corner);
    auto &face = document.faces.front();
    face.uvs.assign(face.vertices.size(), {0.5F, 0.5F});
    const auto frame = quader_editor_core::component_face_local_orientation(
        document, face.id, fallback);

    ASSERT_TRUE(frame.has_value());
    expect_axes(*frame, fallback.x_axis, fallback.y_axis, fallback.z_axis);
  }
}

TEST(QdrModelingRuntimeComponentOrientationTests,
     EdgeUsesAuthoredDirectionWithEquivalentReversedEndpointRequest) {
  const TiltedTriangle fixture = make_tilted_triangle();
  const float inverse_sqrt_two = 1.0F / std::sqrt(2.0F);
  for (const quader_poly::Edge edge :
       {quader_poly::Edge{fixture.first, fixture.second},
        quader_poly::Edge{fixture.second, fixture.first}}) {
    const auto frame = quader_editor_core::component_edge_local_orientation(
        fixture.document, edge);

    ASSERT_TRUE(frame.has_value());
    expect_axes(*frame, {0.0F, inverse_sqrt_two, inverse_sqrt_two},
                {-1.0F, 0.0F, 0.0F},
                {0.0F, -inverse_sqrt_two, inverse_sqrt_two});
    EXPECT_TRUE(vector_near(frame->origin, {0.5F, 0.0F, 0.0F}));
  }
}

TEST(QdrModelingRuntimeComponentOrientationTests,
     VertexAndEdgeUseAuthoredOutgoingDirectionAcrossVertexIdOrderings) {
  constexpr std::array<quader::QVec3, 3> kPositions{{
      {0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 1.0F},
  }};
  constexpr std::array<std::array<std::size_t, 3>, 3> kCreationOrders{{
      {0U, 1U, 2U}, {0U, 2U, 1U}, {2U, 1U, 0U},
  }};
  const float inverse_sqrt_two = 1.0F / std::sqrt(2.0F);
  for (const auto &creation_order : kCreationOrders) {
    quader_poly::Document document;
    std::array<quader_poly::ElementId, kPositions.size()> vertices{};
    for (const std::size_t corner : creation_order) {
      vertices[corner] = quader_poly::add_vertex(document, kPositions[corner]);
    }
    ASSERT_NE(quader_poly::add_face(document, vertices),
              quader_poly::kInvalidElementId);
    const auto vertex_frame =
        quader_editor_core::component_vertex_local_orientation(document,
                                                                vertices[0]);
    const auto edge_frame = quader_editor_core::component_edge_local_orientation(
        document, {vertices[0], vertices[1]});

    ASSERT_TRUE(vertex_frame.has_value());
    ASSERT_TRUE(edge_frame.has_value());
    for (const auto &frame : {*vertex_frame, *edge_frame}) {
      expect_axes(frame, {0.0F, inverse_sqrt_two, inverse_sqrt_two},
                  {-1.0F, 0.0F, 0.0F},
                  {0.0F, -inverse_sqrt_two, inverse_sqrt_two});
    }
    EXPECT_TRUE(vector_near(vertex_frame->origin, kPositions[0]));
    EXPECT_TRUE(vector_near(edge_frame->origin, {0.5F, 0.0F, 0.0F}));
  }
}

TEST(QdrModelingRuntimeComponentOrientationTests,
     SharpVertexAndEdgeUseFirstIncidentFaceInsteadOfAveragedNormals) {
  quader_poly::Document document;
  const auto first = quader_poly::add_vertex(document, {0.0F, 0.0F, 0.0F});
  const auto second = quader_poly::add_vertex(document, {1.0F, 0.0F, 0.0F});
  const auto third = quader_poly::add_vertex(document, {0.0F, 1.0F, 0.0F});
  const auto fourth = quader_poly::add_vertex(document, {0.0F, 0.0F, 1.0F});
  const std::array first_face{first, second, third};
  const std::array second_face{second, first, fourth};
  ASSERT_NE(quader_poly::add_face(document, first_face),
            quader_poly::kInvalidElementId);
  ASSERT_NE(quader_poly::add_face(document, second_face),
            quader_poly::kInvalidElementId);

  const auto vertex_frame =
      quader_editor_core::component_vertex_local_orientation(document, first);
  const auto edge_frame = quader_editor_core::component_edge_local_orientation(
      document, {first, second});
  ASSERT_TRUE(vertex_frame.has_value());
  ASSERT_TRUE(edge_frame.has_value());
  for (const auto &frame : {*vertex_frame, *edge_frame}) {
    expect_axes(frame, {0.0F, 1.0F, 0.0F}, {-1.0F, 0.0F, 0.0F},
                {0.0F, 0.0F, 1.0F});
  }

  std::reverse(document.faces.begin(), document.faces.end());
  const auto reordered_vertex_frame =
      quader_editor_core::component_vertex_local_orientation(document, first);
  const auto reordered_edge_frame =
      quader_editor_core::component_edge_local_orientation(document,
                                                          {first, second});
  ASSERT_TRUE(reordered_vertex_frame.has_value());
  ASSERT_TRUE(reordered_edge_frame.has_value());
  expect_axes(*reordered_vertex_frame, {1.0F, 0.0F, 0.0F},
              {0.0F, 0.0F, -1.0F}, {0.0F, 1.0F, 0.0F});
  expect_axes(*reordered_edge_frame, {0.0F, 0.0F, 1.0F},
              {1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F});
}

TEST(QdrModelingRuntimeComponentOrientationTests,
     DegenerateEdgeUsesObjectOrientationFallback) {
  quader_poly::Document document;
  const quader_poly::ElementId first =
      quader_poly::add_vertex(document, {2.0F, 3.0F, 4.0F});
  const quader_poly::ElementId second =
      quader_poly::add_vertex(document, {2.0F, 3.0F, 4.0F});
  const quader_poly::ElementId third =
      quader_poly::add_vertex(document, {2.0F, 4.0F, 4.0F});
  const std::array vertices{first, second, third};
  ASSERT_NE(quader_poly::add_face(document, vertices),
            quader_poly::kInvalidElementId);
  const quader_poly::Transform3 fallback{
      .x_axis = {0.0F, 1.0F, 0.0F},
      .y_axis = {-1.0F, 0.0F, 0.0F},
      .z_axis = {0.0F, 0.0F, 1.0F},
  };

  const std::optional<quader_poly::Transform3> frame =
      quader_editor_core::component_edge_local_orientation(
          document, {second, first}, fallback);

  ASSERT_TRUE(frame.has_value());
  expect_orthonormal(*frame);
  EXPECT_TRUE(vector_near(frame->x_axis, fallback.x_axis));
  EXPECT_TRUE(vector_near(frame->y_axis, fallback.y_axis));
  EXPECT_TRUE(vector_near(frame->z_axis, fallback.z_axis));
  EXPECT_TRUE(vector_near(frame->origin, {2.0F, 3.0F, 4.0F}));
}

TEST(QdrModelingRuntimeComponentOrientationTests,
     MissingActiveElementsDoNotInventFrames) {
  TiltedTriangle fixture = make_tilted_triangle();
  const auto isolated =
      quader_poly::add_vertex(fixture.document, {2.0F, 2.0F, 2.0F});
  EXPECT_FALSE(quader_editor_core::component_vertex_local_orientation(
                   fixture.document, 9999U)
                   .has_value());
  EXPECT_FALSE(quader_editor_core::component_edge_local_orientation(
                   fixture.document, {fixture.first, 9999U})
                   .has_value());
  EXPECT_FALSE(quader_editor_core::component_edge_local_orientation(
                   fixture.document, {fixture.first, isolated})
                   .has_value());
  EXPECT_FALSE(quader_editor_core::component_face_local_orientation(
                   fixture.document, 9999U)
                   .has_value());
}

} // namespace
