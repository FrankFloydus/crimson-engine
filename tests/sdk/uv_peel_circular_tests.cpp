////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>
#include <quader/modeling/modeling.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace {
using namespace quader::modeling;

AuthoredTopology circular_strip(bool straight = false) {
  AuthoredTopology topology;
  for (std::uint32_t station = 0U; station < 3U; ++station) {
    const float angle = .2F * static_cast<float>(station);
    for (std::uint32_t rail = 0U; rail < 3U; ++rail) {
      const float radius = static_cast<float>(rail + 1U);
      topology.vertices.push_back({make_id<VertexTag>(station * 3U + rail + 1U),
          straight ? Vec3{static_cast<float>(station), 0.0F, radius}
                   : Vec3{radius * std::cos(angle), 0.0F, radius * std::sin(angle)}});
    }
  }
  for (std::uint32_t station = 0U; station < 2U; ++station) {
    for (std::uint32_t rail = 0U; rail < 2U; ++rail) {
      const auto a = station * 3U + rail + 1U;
      topology.faces.push_back({.id = make_id<FaceTag>(station * 2U + rail + 1U),
          .vertices = {make_id<VertexTag>(a), make_id<VertexTag>(a + 3U),
                       make_id<VertexTag>(a + 4U), make_id<VertexTag>(a + 1U)}});
    }
  }
  topology.next_vertex_id = 10U;
  topology.next_face_id = 5U;
  return topology;
}

UvPeelTarget circular_target(const PolygonDocument &document) {
  return {.key = 1U, .document = &document,
          .edges = {make_edge_key(make_id<VertexTag>(2U), make_id<VertexTag>(5U)),
                    make_edge_key(make_id<VertexTag>(5U), make_id<VertexTag>(8U))}};
}

std::size_t annotation_count(const PolygonDocument &document) {
  const auto topology = document.authored_topology().value();
  return static_cast<std::size_t>(std::count_if(topology.faces.begin(), topology.faces.end(),
      [](const auto &face) { return face.circular_uv_interpolation.has_value(); }));
}

std::array<double,2> polar_coordinate(const CircularUvInterpolation &field, Vec3 p) {
  const double x = static_cast<double>(p.x) - field.center.x;
  const double y = static_cast<double>(p.y) - field.center.y;
  const double z = static_cast<double>(p.z) - field.center.z;
  const double u = x * field.axis_u.x + y * field.axis_u.y + z * field.axis_u.z;
  const double v = x * field.axis_v.x + y * field.axis_v.y + z * field.axis_v.z;
  return {std::hypot(u,v),std::atan2(v,u)};
}

// The public sparse table describes an analytic field relative to any corner.
// This tests its exported meaning, not a private solver's ordering or internals.
std::array<double,2> sampled_field(const MeshPayload &mesh, std::size_t first, Vec3 p) {
  const auto &corner = mesh.vertices[first];
  const auto &field = mesh.circular_uv_interpolations[corner.circular_uv_index - 1U];
  const auto q = polar_coordinate(field,p), origin = polar_coordinate(field,corner.position);
  const double r = q[0] - origin[0], a = q[1] - origin[1];
  return {corner.uv0.x + field.uv_from_polar[0]*r + field.uv_from_polar[1]*a,
          corner.uv0.y + field.uv_from_polar[2]*r + field.uv_from_polar[3]*a};
}

TEST(UvPeelCircularTests, DefaultsOffAndAuthorsSparseLiveFieldWithoutGeometryChanges) {
  auto document = PolygonDocument::from_authored_topology(circular_strip());
  ASSERT_TRUE(document.ok()) << document.error_message();
  auto target = circular_target(document.value());
  const auto standard = prepare_uv_peel(std::span(&target,1U),{});
  ASSERT_TRUE(standard.ok()) << standard.error_message();
  EXPECT_EQ(annotation_count(standard.value().documents[0].document),0U);
  UvPeelOptions options;
  options.smooth_circular_flow = true;
  const auto prepared = prepare_uv_peel(std::span(&target,1U),options);
  ASSERT_TRUE(prepared.ok()) << prepared.error_message();
  ASSERT_TRUE(prepared.value().changed);
  const auto &result = prepared.value().documents[0].document;
  EXPECT_EQ(annotation_count(result),4U);
  const auto topology = result.authored_topology().value();
  const auto original = circular_strip();
  ASSERT_EQ(topology.vertices.size(),original.vertices.size());
  ASSERT_EQ(topology.faces.size(),original.faces.size());
  for (std::size_t i=0U;i<topology.vertices.size();++i) {
    EXPECT_EQ(topology.vertices[i].id,original.vertices[i].id);
    EXPECT_FLOAT_EQ(topology.vertices[i].position.x,original.vertices[i].position.x);
    EXPECT_FLOAT_EQ(topology.vertices[i].position.z,original.vertices[i].position.z);
  }
  for (std::size_t i=0U;i<topology.faces.size();++i) {
    EXPECT_EQ(topology.faces[i].vertices,original.faces[i].vertices);
  }
  const auto compiled = result.compile_mesh();
  ASSERT_TRUE(compiled.ok()) << compiled.error_message();
  EXPECT_EQ(compiled.value().vertices.size(),16U);
  EXPECT_EQ(compiled.value().indices.size(),24U);
  EXPECT_EQ(compiled.value().circular_uv_interpolations.size(),4U);
  for (std::size_t i=0U;i<compiled.value().vertices.size();++i) {
    EXPECT_EQ(compiled.value().vertices[i].circular_uv_index,i/4U+1U);
  }
  auto repeated_target = circular_target(result);
  const auto repeated = prepare_uv_peel(std::span(&repeated_target,1U),options);
  ASSERT_TRUE(repeated.ok()) << repeated.error_message();
  EXPECT_FALSE(repeated.value().changed);
  const auto disabled = prepare_uv_peel(std::span(&repeated_target,1U),{});
  ASSERT_TRUE(disabled.ok()) << disabled.error_message();
  EXPECT_TRUE(disabled.value().changed);
  EXPECT_EQ(annotation_count(disabled.value().documents[0].document),0U);
}

TEST(UvPeelCircularTests, ExportedFieldFollowsCircularIsolinesAcrossPanelAndWidthJoins) {
  auto document = PolygonDocument::from_authored_topology(circular_strip()).value();
  auto target = circular_target(document);
  UvPeelOptions options;
  options.smooth_circular_flow = true;
  const auto prepared = prepare_uv_peel(std::span(&target,1U),options);
  ASSERT_TRUE(prepared.ok()) << prepared.error_message();
  const auto compiled = prepared.value().documents[0].document.compile_mesh().value();
  ASSERT_EQ(compiled.circular_uv_interpolations.size(),4U);
  for (float angle : {.05F,.15F,.2F,.25F,.35F}) {
    const Vec3 p{2.0F*std::cos(angle),0.0F,2.0F*std::sin(angle)};
    const auto reference = sampled_field(compiled,0U,p);
    for (std::size_t face=1U;face<4U;++face) {
      const auto actual = sampled_field(compiled,face*4U,p);
      EXPECT_NEAR(actual[0],reference[0],2e-6);
      EXPECT_NEAR(actual[1],reference[1],2e-6);
    }
    EXPECT_NEAR(reference[1],0.0,2e-6);
  }
  const auto before = sampled_field(compiled,0U,{2.0F*std::cos(.199F),0.0F,2.0F*std::sin(.199F)});
  const auto middle = sampled_field(compiled,0U,{2.0F*std::cos(.2F),0.0F,2.0F*std::sin(.2F)});
  const auto after = sampled_field(compiled,8U,{2.0F*std::cos(.201F),0.0F,2.0F*std::sin(.201F)});
  EXPECT_NEAR(middle[0]-before[0],after[0]-middle[0],2e-6);
  EXPECT_GT(std::abs(after[0]-before[0]),.001);
}

TEST(UvPeelCircularTests, NoncircularAndInsufficientGuidesRetainStandardPeel) {
  auto document = PolygonDocument::from_authored_topology(circular_strip(true)).value();
  auto target = circular_target(document);
  UvPeelOptions options;
  options.smooth_circular_flow = true;
  const auto enabled = prepare_uv_peel(std::span(&target,1U),options);
  const auto disabled = prepare_uv_peel(std::span(&target,1U),{});
  ASSERT_TRUE(enabled.ok()) << enabled.error_message();
  ASSERT_TRUE(disabled.ok()) << disabled.error_message();
  EXPECT_EQ(annotation_count(enabled.value().documents[0].document),0U);
  EXPECT_EQ(serialize_qdr_authored_topology(enabled.value().documents[0].document).value(),
            serialize_qdr_authored_topology(disabled.value().documents[0].document).value());
  auto circle = PolygonDocument::from_authored_topology(circular_strip()).value();
  target = circular_target(circle);
  target.edges.resize(1U);
  const auto short_guide = prepare_uv_peel(std::span(&target,1U),options);
  ASSERT_TRUE(short_guide.ok()) << short_guide.error_message();
  EXPECT_EQ(annotation_count(short_guide.value().documents[0].document),0U);
}

TEST(UvPeelCircularTests, AcceptsRigidUniformAndTiltedPlanesButKeepsIncompatibleWorldMetricsStandard) {
  auto reference = PolygonDocument::from_authored_topology(circular_strip()).value();
  auto reference_target = circular_target(reference);
  UvPeelOptions options;
  options.smooth_circular_flow = true;
  const auto baseline = prepare_uv_peel(std::span(&reference_target,1U),options);
  ASSERT_TRUE(baseline.ok()) << baseline.error_message();
  const auto reference_fields = baseline.value().documents[0].document.compile_mesh().value()
                                    .circular_uv_interpolations;
  ASSERT_EQ(reference_fields.size(),4U);
  for (int scenario=0;scenario<4;++scenario) {
    SCOPED_TRACE(scenario);
    auto topology = circular_strip();
    if (scenario==1) {
      for (auto &vertex : topology.vertices) {
        const float z=vertex.position.z;
        vertex.position.y=-.8F*z;
        vertex.position.z=.6F*z;
      }
    }
    auto document = PolygonDocument::from_authored_topology(topology).value();
    auto target = circular_target(document);
    if (scenario==0) {
      target.world_from_local={.x_axis={0.0F,3.0F,0.0F},.y_axis={-3.0F,0.0F,0.0F},
                               .z_axis={0.0F,0.0F,3.0F},.origin={100.0F,-70.0F,9.0F}};
    } else if (scenario==2) {
      target.world_from_local.x_axis={2.0F,0.0F,0.0F};
    } else if (scenario==3) {
      target.world_from_local.z_axis={.3F,0.0F,1.0F};
    }
    const auto enabled = prepare_uv_peel(std::span(&target,1U),options);
    ASSERT_TRUE(enabled.ok()) << enabled.error_message();
    if (scenario<2) {
      const auto &result=enabled.value().documents[0].document;
      EXPECT_EQ(annotation_count(result),4U);
      const auto fields=result.compile_mesh().value().circular_uv_interpolations;
      ASSERT_EQ(fields.size(),4U);
      for (std::size_t i=0U;i<4U;++i) {
        EXPECT_NEAR(fields[0].uv_from_polar[i],
                    reference_fields[0].uv_from_polar[i]*(scenario==0?3.0F:1.0F),2e-5);
      }
    } else {
      const auto standard=prepare_uv_peel(std::span(&target,1U),{});
      ASSERT_TRUE(standard.ok()) << standard.error_message();
      EXPECT_EQ(annotation_count(enabled.value().documents[0].document),0U);
      EXPECT_EQ(serialize_qdr_authored_topology(enabled.value().documents[0].document).value(),
                serialize_qdr_authored_topology(standard.value().documents[0].document).value());
    }
  }
}

TEST(UvPeelCircularTests, PersistenceRetainsFieldsAndInvalidRawRecordsWithoutSilentlyRepairingThem) {
  auto document = PolygonDocument::from_authored_topology(circular_strip()).value();
  auto target = circular_target(document);
  UvPeelOptions options;
  options.smooth_circular_flow = true;
  const auto prepared = prepare_uv_peel(std::span(&target,1U),options);
  ASSERT_TRUE(prepared.ok()) << prepared.error_message();
  auto topology = prepared.value().documents[0].document.authored_topology().value();
  // Distinct components expose read-order corruption independently of the
  // symmetric axes and near-zero center that circular fixtures can produce.
  topology.faces[0].circular_uv_interpolation = CircularUvInterpolation{
      .center={1.0F,2.0F,3.0F},.axis_u={4.0F,5.0F,6.0F},.axis_v={7.0F,8.0F,10.0F},
      .uv_from_polar={11.0F,12.0F,13.0F,14.0F}};
  const auto asymmetric = PolygonDocument::from_authored_topology(topology);
  ASSERT_TRUE(asymmetric.ok()) << asymmetric.error_message();
  const auto encoded = serialize_qdr_authored_topology(asymmetric.value());
  ASSERT_TRUE(encoded.ok());
  EXPECT_EQ(std::to_integer<unsigned int>(encoded.value()[6]),4U);
  const auto decoded = deserialize_qdr_authored_topology(encoded.value());
  ASSERT_TRUE(decoded.ok()) << decoded.error_message();
  EXPECT_EQ(annotation_count(decoded.value()),4U);
  EXPECT_EQ(serialize_qdr_authored_topology(decoded.value()).value(),encoded.value());
  auto invalid = decoded.value().authored_topology().value();
  invalid.faces[0].circular_uv_interpolation->axis_u = {};
  invalid.faces[1].circular_uv_interpolation->uv_from_polar[0] =
      std::numeric_limits<float>::quiet_NaN();
  EXPECT_FALSE(PolygonDocument::from_authored_topology(invalid).ok());
  const auto raw = PolygonDocument::from_persisted_topology(invalid);
  ASSERT_TRUE(raw.ok()) << raw.error_message();
  const auto bytes = serialize_qdr_authored_topology(raw.value()).value();
  const auto raw_decoded = deserialize_qdr_authored_topology(bytes);
  ASSERT_TRUE(raw_decoded.ok()) << raw_decoded.error_message();
  EXPECT_EQ(serialize_qdr_authored_topology(raw_decoded.value()).value(),bytes);
  EXPECT_FALSE(raw_decoded.value().validate().ok());
  EXPECT_EQ(raw_decoded.value().compile_mesh().value().circular_uv_interpolations.size(),2U);
}

TEST(UvPeelCircularTests, ReadsPreviousQgeoMinorWithNoAnnotationsAndRejectsMalformedSparseIndices) {
  const auto document = PolygonDocument::from_authored_topology(circular_strip()).value();
  auto bytes = serialize_qdr_authored_topology(document).value();
  bytes.resize(bytes.size()-4U);
  bytes[6]=std::byte{3U};
  for (std::size_t i=0U;i<8U;++i) {
    bytes[16U+i]=static_cast<std::byte>((static_cast<std::uint64_t>(bytes.size())>>(8U*i))&255U);
  }
  const auto legacy = deserialize_qdr_authored_topology(bytes);
  ASSERT_TRUE(legacy.ok()) << legacy.error_message();
  EXPECT_EQ(annotation_count(legacy.value()),0U);
  auto target = circular_target(document);
  UvPeelOptions options;
  options.smooth_circular_flow = true;
  const auto prepared = prepare_uv_peel(std::span(&target,1U),options).value();
  bytes = serialize_qdr_authored_topology(prepared.documents[0].document).value();
  // Four sparse 56-byte records follow their count; index is codec structure.
  const std::size_t first_record=bytes.size()-4U*56U;
  bytes[first_record]=std::byte{255U};
  EXPECT_FALSE(deserialize_qdr_authored_topology(bytes).ok());
}

TEST(UvPeelCircularTests, PositionMutationsClearTouchedFieldsAndTextureTransformsRetainThem) {
  auto document = PolygonDocument::from_authored_topology(circular_strip()).value();
  auto target = circular_target(document);
  UvPeelOptions options;
  options.smooth_circular_flow = true;
  const auto prepared = prepare_uv_peel(std::span(&target,1U),options);
  ASSERT_TRUE(prepared.ok()) << prepared.error_message();
  document = prepared.value().documents[0].document;
  const std::array texture_update{FaceTextureTransformUpdate{
      .face=make_id<FaceTag>(1U), .scale_x=2.0F, .rotation_degrees=30.0F}};
  ASSERT_TRUE(document.set_face_texture_transforms(texture_update).ok());
  EXPECT_EQ(annotation_count(document),4U);
  const auto old = document.authored_topology().value().vertices[0].position;
  const std::array move{VertexPositionUpdate{make_id<VertexTag>(1U),{old.x,.1F,old.z}}};
  auto direct = document;
  ASSERT_TRUE(direct.set_vertex_positions(move).ok());
  EXPECT_EQ(annotation_count(direct),3U);
  ASSERT_TRUE(document.select_vertex(make_id<VertexTag>(1U)).ok());
  const auto gesture = document.prepare_selection_transform();
  ASSERT_TRUE(gesture.ok()) << gesture.error_message();
  const auto identity = gesture.value().evaluate_translation({});
  ASSERT_TRUE(identity.ok());
  EXPECT_FALSE(identity.value().receipt.changed);
  EXPECT_EQ(annotation_count(identity.value().document),4U);
  const auto moved = gesture.value().evaluate_translation({0.0F,.1F,0.0F});
  ASSERT_TRUE(moved.ok()) << moved.error_message();
  EXPECT_EQ(annotation_count(moved.value().document),3U);
  ASSERT_TRUE(document.translate_selection({0.0F,.1F,0.0F}).ok());
  EXPECT_EQ(annotation_count(document),3U);
}

} // namespace
