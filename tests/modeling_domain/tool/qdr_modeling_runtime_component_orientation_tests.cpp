////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <editor/modeling/runtime/component_orientation.hpp>
#include <quader/modeling/neutral/polygon/document.hpp>

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
     FaceUsesAuthoredNormalAndFirstPerimeterEdge) {
  const TiltedTriangle fixture = make_tilted_triangle();
  const std::optional<quader_poly::Transform3> frame =
      quader_editor_core::component_face_local_orientation(
          fixture.document, fixture.face);

  ASSERT_TRUE(frame.has_value());
  expect_orthonormal(*frame);
  EXPECT_TRUE(vector_near(frame->x_axis, {1.0F, 0.0F, 0.0F}));
  const float inverse_sqrt_two = 1.0F / std::sqrt(2.0F);
  EXPECT_TRUE(vector_near(
      frame->z_axis, {0.0F, -inverse_sqrt_two, inverse_sqrt_two}));
  EXPECT_TRUE(vector_near(frame->origin,
                          {1.0F / 3.0F, 1.0F / 3.0F, 1.0F / 3.0F}));
}

TEST(QdrModelingRuntimeComponentOrientationTests,
     EdgeDirectionIsCanonicalAndUsesIncidentNormals) {
  const TiltedTriangle fixture = make_tilted_triangle();
  const quader_poly::Edge reversed{fixture.second, fixture.first};
  const std::optional<quader_poly::Transform3> frame =
      quader_editor_core::component_edge_local_orientation(
          fixture.document, reversed);

  ASSERT_TRUE(frame.has_value());
  expect_orthonormal(*frame);
  EXPECT_TRUE(vector_near(frame->x_axis, {1.0F, 0.0F, 0.0F}));
  const float inverse_sqrt_two = 1.0F / std::sqrt(2.0F);
  EXPECT_TRUE(vector_near(
      frame->z_axis, {0.0F, -inverse_sqrt_two, inverse_sqrt_two}));
  EXPECT_TRUE(vector_near(frame->origin, {0.5F, 0.0F, 0.0F}));
}

TEST(QdrModelingRuntimeComponentOrientationTests,
     VertexUsesLowestIdUsableNeighborInsteadOfFaceOrder) {
  quader_poly::Document document;
  const quader_poly::ElementId active =
      quader_poly::add_vertex(document, {0.0F, 0.0F, 0.0F});
  const quader_poly::ElementId lowest_neighbor =
      quader_poly::add_vertex(document, {0.0F, 1.0F, 1.0F});
  const quader_poly::ElementId authored_first_neighbor =
      quader_poly::add_vertex(document, {1.0F, 0.0F, 0.0F});
  const std::array face_vertices{
      active, authored_first_neighbor, lowest_neighbor};
  ASSERT_NE(quader_poly::add_face(document, face_vertices),
            quader_poly::kInvalidElementId);

  const std::optional<quader_poly::Transform3> frame =
      quader_editor_core::component_vertex_local_orientation(document,
                                                              active);

  ASSERT_TRUE(frame.has_value());
  expect_orthonormal(*frame);
  EXPECT_GT(quader::dot(frame->x_axis,
                        quader::normalize_or_zero(
                            quader::QVec3{0.0F, 1.0F, 1.0F})),
            0.999F);
  EXPECT_LT(std::abs(quader::dot(frame->x_axis,
                                 quader::QVec3{1.0F, 0.0F, 0.0F})),
            0.001F);
  EXPECT_TRUE(vector_near(frame->origin, {0.0F, 0.0F, 0.0F}));
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
  const TiltedTriangle fixture = make_tilted_triangle();
  EXPECT_FALSE(quader_editor_core::component_vertex_local_orientation(
                   fixture.document, 9999U)
                   .has_value());
  EXPECT_FALSE(quader_editor_core::component_edge_local_orientation(
                   fixture.document, {fixture.first, 9999U})
                   .has_value());
  EXPECT_FALSE(quader_editor_core::component_face_local_orientation(
                   fixture.document, 9999U)
                   .has_value());
}

} // namespace
