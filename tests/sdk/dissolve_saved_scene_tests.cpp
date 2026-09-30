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
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <utility>
#include <vector>

namespace {

using namespace quader::modeling;

AuthoredTopologyFace saved_arch_face(
    std::uint32_t id, std::initializer_list<std::uint32_t> vertices,
    std::initializer_list<Vec2> uvs) {
  AuthoredTopologyFace face{.id = make_id<FaceTag>(id),
                           .uvs = uvs,
                           .material_slot = 1U,
                           .texture_transform = {.scale = {0.125F, 0.125F}}};
  for (const auto vertex : vertices) {
    face.vertices.push_back(make_id<VertexTag>(vertex));
  }
  return face;
}

/// Exact adjacent front-band faces from the user's saved Arch, mesh 139.
/// The third face protects unaffected authored data beside the dissolved pair.
AuthoredTopology saved_arch_front_band() {
  AuthoredTopology topology;
  topology.next_vertex_id = 2499U;
  topology.next_face_id = 2472U;
  topology.vertices = {
      {make_id<VertexTag>(14U),
       {-0.41573482751846313F, 0.5F, 0.2777850925922394F}},
      {make_id<VertexTag>(15U),
       {-0.46193981170654297F, 0.5F, 0.19134163856506348F}},
      {make_id<VertexTag>(16U),
       {-0.49039265513420105F, 0.5F, 0.09754515439271927F}},
      {make_id<VertexTag>(17U), {-0.5F, 0.5F, -4.371138828673793e-08F}},
      {make_id<VertexTag>(1296U),
       {-0.4427083432674408F, 0.4999999701976776F, -3.8702793858647055e-08F}},
      {make_id<VertexTag>(1297U),
       {-0.43420183658599854F, 0.4999999701976776F, 0.08636810630559921F}},
      {make_id<VertexTag>(1298U),
       {-0.4090092182159424F, 0.4999999701976776F, 0.16941708326339722F}},
      {make_id<VertexTag>(1299U),
       {-0.36809855699539185F, 0.4999999701976776F, 0.24595555663108826F}},
  };
  topology.faces = {
      saved_arch_face(1266U, {17U, 16U, 1297U, 1296U},
                      {{1.8791431188583374F, 0.31787073612213135F},
                       {1.8859373331069946F, 0.3178905248641968F},
                       {1.8897173404693604F, 0.3143077492713928F},
                       {1.8848148584365845F, 0.31429025530815125F}}),
      saved_arch_face(1267U, {16U, 15U, 1298U, 1297U},
                      {{1.5963165760040283F, 0.5520917773246765F},
                       {1.6030794382095337F, 0.552140474319458F},
                       {1.6064454317092896F, 0.5485491156578064F},
                       {1.6015726327896118F, 0.548505961894989F}}),
      saved_arch_face(1268U, {15U, 14U, 1299U, 1298U},
                      {{1.3186187744140625F, 0.7898561954498291F},
                       {1.325365662574768F, 0.7899271249771118F},
                       {1.328323245048523F, 0.7863215804100037F},
                       {1.323468804359436F, 0.7862588167190552F}}),
  };
  return topology;
}

/// Exact pre-dissolve mid-depth strip from the same saved Arch.
AuthoredTopology saved_arch_mid_depth_strip() {
  AuthoredTopology topology;
  topology.next_vertex_id = 2499U;
  topology.next_face_id = 2472U;
  topology.vertices = {
      {make_id<VertexTag>(1307U),
       {0.24595555663108826F, 0.4999999701976776F, 0.36809855699539185F}},
      {make_id<VertexTag>(1308U),
       {0.3130420744419098F, 0.4999999701976776F, 0.3130420744419098F}},
      {make_id<VertexTag>(1309U),
       {0.36809852719306946F, 0.4999999701976776F, 0.24595558643341064F}},
      {make_id<VertexTag>(1310U),
       {0.4090091586112976F, 0.4999999701976776F, 0.16941715776920319F}},
      {make_id<VertexTag>(2427U),
       {0.24595555663108826F, -0.5F, 0.36809855699539185F}},
      {make_id<VertexTag>(2428U),
       {0.3130420744419098F, -0.5F, 0.3130420744419098F}},
      {make_id<VertexTag>(2429U),
       {0.36809852719306946F, -0.5F, 0.24595558643341064F}},
      {make_id<VertexTag>(2430U),
       {0.4090091586112976F, -0.5F, 0.16941715776920319F}},
      {make_id<VertexTag>(2465U),
       {0.4090091586112976F, -2.9802322387695312e-08F, 0.16941715776920319F}},
      {make_id<VertexTag>(2466U),
       {0.36809852719306946F, -2.9802322387695312e-08F, 0.24595558643341064F}},
      {make_id<VertexTag>(2468U),
       {0.3130420744419098F, -2.9802322387695312e-08F, 0.3130420744419098F}},
      {make_id<VertexTag>(2470U),
       {0.24595555663108826F, -2.9802322387695312e-08F, 0.36809855699539185F}},
  };
  topology.faces = {
      saved_arch_face(2399U, {2430U, 2429U, 2466U, 2465U},
                      {{-0.10705913603305817F, -0.7420440316200256F},
                       {-0.10707459598779678F, -0.7365901470184326F},
                       {-0.053154416382312775F, -0.7365901470184326F},
                       {-0.0531635582447052F, -0.7420440316200256F}}),
      saved_arch_face(2438U, {1310U, 2465U, 2466U, 1309U},
                      {{-0.0007557369535788894F, -0.7420440316200256F},
                       {-0.05465131998062134F, -0.7420440316200256F},
                       {-0.05466046556830406F, -0.7365901470184326F},
                       {-0.0007402816554531455F, -0.7365901470184326F}}),
      saved_arch_face(2398U, {2429U, 2428U, 2468U, 2466U},
                      {{-0.10679581016302109F, -0.9265787601470947F},
                       {-0.10681414604187012F, -0.9211002588272095F},
                       {-0.05288495495915413F, -0.9211002588272095F},
                       {-0.05289580300450325F, -0.9265787601470947F}}),
      saved_arch_face(2440U, {1309U, 2466U, 2468U, 1308U},
                      {{-0.0010314499959349632F, -0.9265787601470947F},
                       {-0.05493147671222687F, -0.9265787601470947F},
                       {-0.05494231730699539F, -0.9211002588272095F},
                       {-0.0010131365852430463F, -0.9211002588272095F}}),
      saved_arch_face(2397U, {2428U, 2427U, 2470U, 2468U},
                      {{-0.1082620769739151F, 1.2199985980987549F},
                       {-0.10828039795160294F, 1.2253705263137817F},
                       {-0.054343827068805695F, 1.2253705263137817F},
                       {-0.05435481667518616F, 1.2199985980987549F}}),
      saved_arch_face(2442U, {1308U, 2468U, 2470U, 1307U},
                      {{0.00041845219675451517F, 1.2199985980987549F},
                       {-0.053488798439502716F, 1.2199985980987549F},
                       {-0.05349979177117348F, 1.2253705263137817F},
                       {0.00043678266229107976F, 1.2253705263137817F}}),
  };
  return topology;
}

const AuthoredTopologyFace* find_face(const AuthoredTopology& topology,
                                    std::uint32_t id) {
  const auto found = std::ranges::find(topology.faces, make_id<FaceTag>(id),
                                     &AuthoredTopologyFace::id);
  return found == topology.faces.end() ? nullptr : &*found;
}

void expect_exact_face(const AuthoredTopologyFace& actual,
                       const AuthoredTopologyFace& expected) {
  EXPECT_EQ(actual.id, expected.id);
  EXPECT_EQ(actual.vertices, expected.vertices);
  EXPECT_EQ(actual.material_slot, expected.material_slot);
  EXPECT_EQ(actual.shading, expected.shading);
  EXPECT_EQ(actual.texture_transform, expected.texture_transform);
  ASSERT_EQ(actual.uvs.size(), expected.uvs.size());
  for (std::size_t index = 0; index < actual.uvs.size(); ++index) {
    EXPECT_EQ(actual.uvs[index].x, expected.uvs[index].x);
    EXPECT_EQ(actual.uvs[index].y, expected.uvs[index].y);
  }
}

TEST(SavedArchDissolveSdkTests,
     DissolvesPeeledFrontBandWithBoundaryUvsAndOneUndoStep) {
  const auto topology = saved_arch_front_band();
  auto imported = PolygonDocument::from_authored_topology(topology);
  ASSERT_TRUE(imported.ok()) << imported.error_message();
  PolygonDocument original = std::move(imported).value();
  const auto edge = make_edge_key(make_id<VertexTag>(16U),
                                  make_id<VertexTag>(1297U));
  ASSERT_TRUE(original.set_selection_mode(SelectionMode::Edge).ok());
  ASSERT_TRUE(original.select_edge(edge).ok());
  const auto before = serialize_qdr_authored_topology(original);
  ASSERT_TRUE(before.ok()) << before.error_message();

  ModelingSession session;
  const auto added = session.add_document(original, "Arch");
  ASSERT_TRUE(added.ok()) << added.error_message();
  session.clear_history();
  PolygonDocument dissolved = original;
  const auto operation = dissolved.dissolve_selected_edges();
  ASSERT_TRUE(operation.ok()) << operation.error_message();
  ASSERT_TRUE(operation.value().success) << operation.value().message;
  ASSERT_TRUE(operation.value().changed);
  ASSERT_TRUE(dissolved.validate().ok());
  const auto result = dissolved.authored_topology();
  ASSERT_TRUE(result.ok()) << result.error_message();
  ASSERT_EQ(result.value().faces.size(), 2U);
  EXPECT_EQ(result.value().vertices.size(), 8U);
  EXPECT_EQ(result.value().next_vertex_id, topology.next_vertex_id);
  EXPECT_EQ(result.value().next_face_id, topology.next_face_id);
  EXPECT_EQ(result.value().hard_normal_edges, topology.hard_normal_edges);
  for (const auto& vertex : topology.vertices) {
    const auto found = std::ranges::find(result.value().vertices, vertex.id,
                                       &AuthoredTopologyVertex::id);
    ASSERT_NE(found, result.value().vertices.end());
    EXPECT_EQ(found->position.x, vertex.position.x);
    EXPECT_EQ(found->position.y, vertex.position.y);
    EXPECT_EQ(found->position.z, vertex.position.z);
  }

  const auto* merged = find_face(result.value(), 1266U);
  ASSERT_NE(merged, nullptr);
  EXPECT_EQ(find_face(result.value(), 1267U), nullptr);
  EXPECT_EQ(merged->material_slot, topology.faces[0].material_slot);
  EXPECT_EQ(merged->shading, topology.faces[0].shading);
  EXPECT_EQ(merged->texture_transform, topology.faces[0].texture_transform);
  ASSERT_EQ(merged->vertices.size(), 6U);
  ASSERT_EQ(merged->uvs.size(), 6U);

  // Both boundary endpoints turn about 11.25 degrees, so normal dissolve keeps
  // them. Each retained UV corner follows its surviving outgoing boundary edge.
  const std::array<std::pair<std::uint32_t, Vec2>, 6> expected_uvs{{
      {17U, topology.faces[0].uvs[0]},
      {16U, topology.faces[1].uvs[0]},
      {1297U, topology.faces[0].uvs[2]},
      {1296U, topology.faces[0].uvs[3]},
      {15U, topology.faces[1].uvs[1]},
      {1298U, topology.faces[1].uvs[2]},
  }};
  for (const auto& [vertex, expected] : expected_uvs) {
    const auto corner = std::ranges::find(merged->vertices,
                                        make_id<VertexTag>(vertex));
    ASSERT_NE(corner, merged->vertices.end());
    const auto index = static_cast<std::size_t>(corner - merged->vertices.begin());
    EXPECT_FLOAT_EQ(merged->uvs[index].x, expected.x);
    EXPECT_FLOAT_EQ(merged->uvs[index].y, expected.y);
  }
  const auto* untouched = find_face(result.value(), 1268U);
  ASSERT_NE(untouched, nullptr);
  expect_exact_face(*untouched, topology.faces[2]);
  EXPECT_EQ(dissolved.selection().kind, SelectionKind::Face);
  EXPECT_EQ(dissolved.selection().faces,
            std::vector<FaceId>{make_id<FaceTag>(1266U)});
  EXPECT_TRUE(dissolved.selection().edges.empty());
  EXPECT_TRUE(dissolved.selection().has_active);
  EXPECT_EQ(dissolved.selection().active_face, make_id<FaceTag>(1266U));
  const auto after = serialize_qdr_authored_topology(dissolved);
  ASSERT_TRUE(after.ok()) << after.error_message();

  const auto committed = session.commit_document(
      added.value(), dissolved, operation.value());
  ASSERT_TRUE(committed.ok()) << committed.error_message();
  ASSERT_TRUE(session.can_undo());
  ASSERT_TRUE(session.undo().ok());
  EXPECT_FALSE(session.can_undo());
  auto restored = session.document_copy(added.value());
  ASSERT_TRUE(restored.ok()) << restored.error_message();
  const auto undone = serialize_qdr_authored_topology(restored.value());
  ASSERT_TRUE(undone.ok());
  EXPECT_EQ(undone.value(), before.value());
  EXPECT_EQ(restored.value().selection().kind, SelectionKind::Edge);
  EXPECT_EQ(restored.value().selection().edges, std::vector<EdgeKey>{edge});
  EXPECT_TRUE(restored.value().selection().has_active);
  EXPECT_EQ(restored.value().selection().active_edge, edge);

  ASSERT_TRUE(session.redo().ok());
  EXPECT_FALSE(session.can_redo());
  restored = session.document_copy(added.value());
  ASSERT_TRUE(restored.ok()) << restored.error_message();
  const auto redone = serialize_qdr_authored_topology(restored.value());
  ASSERT_TRUE(redone.ok());
  EXPECT_EQ(redone.value(), after.value());
  EXPECT_EQ(restored.value().selection().kind, SelectionKind::Face);
  EXPECT_EQ(restored.value().selection().faces, dissolved.selection().faces);
  EXPECT_EQ(restored.value().selection().active_face,
            dissolved.selection().active_face);
}

TEST(SavedArchDissolveSdkTests,
     CleansMidDepthEndpointsAfterBatchWhileKeepingUnselectedSeamJunction) {
  const auto topology = saved_arch_mid_depth_strip();
  const std::array<EdgeKey, 2> selected_edges{{
      make_edge_key(make_id<VertexTag>(2465U), make_id<VertexTag>(2466U)),
      make_edge_key(make_id<VertexTag>(2466U), make_id<VertexTag>(2468U)),
  }};
  std::vector<std::byte> forward_topology;
  for (const bool reversed : {false, true}) {
    SCOPED_TRACE(reversed ? "reversed edge order" : "forward edge order");
    auto imported = PolygonDocument::from_authored_topology(topology);
    ASSERT_TRUE(imported.ok()) << imported.error_message();
    PolygonDocument original = std::move(imported).value();
    auto edges = selected_edges;
    if (reversed) {
      std::ranges::reverse(edges);
    }
    ASSERT_TRUE(original.set_selection_mode(SelectionMode::Edge).ok());
    ASSERT_TRUE(original.select_edges(edges).ok());
    const auto original_selection = original.selection();
    const auto before = serialize_qdr_authored_topology(original);
    ASSERT_TRUE(before.ok()) << before.error_message();

    ModelingSession session;
    const auto added = session.add_document(original, "Arch");
    ASSERT_TRUE(added.ok()) << added.error_message();
    session.clear_history();
    PolygonDocument dissolved = original;
    const auto operation = dissolved.dissolve_selected_edges();
    ASSERT_TRUE(operation.ok()) << operation.error_message();
    ASSERT_TRUE(operation.value().success) << operation.value().message;
    ASSERT_TRUE(operation.value().changed);
    ASSERT_TRUE(dissolved.validate().ok());
    const auto result = dissolved.authored_topology();
    ASSERT_TRUE(result.ok()) << result.error_message();
    EXPECT_EQ(result.value().vertices.size(), 10U);
    ASSERT_EQ(result.value().faces.size(), 4U);
    EXPECT_EQ(result.value().next_vertex_id, topology.next_vertex_id);
    EXPECT_EQ(result.value().next_face_id, topology.next_face_id);
    EXPECT_EQ(result.value().hard_normal_edges, topology.hard_normal_edges);
    for (const auto& vertex : topology.vertices) {
      const auto found = std::ranges::find(result.value().vertices, vertex.id,
                                         &AuthoredTopologyVertex::id);
      if (vertex.id.index == 2465U || vertex.id.index == 2466U) {
        EXPECT_EQ(found, result.value().vertices.end());
        continue;
      }
      ASSERT_NE(found, result.value().vertices.end());
      EXPECT_EQ(found->position.x, vertex.position.x);
      EXPECT_EQ(found->position.y, vertex.position.y);
      EXPECT_EQ(found->position.z, vertex.position.z);
    }

    const auto remaining_edges = dissolved.edge_ids();
    for (const auto edge : selected_edges) {
      EXPECT_EQ(std::ranges::find(remaining_edges, edge), remaining_edges.end());
    }
    const auto retained_seam = make_edge_key(make_id<VertexTag>(2468U),
                                             make_id<VertexTag>(2470U));
    EXPECT_NE(std::ranges::find(remaining_edges, retained_seam),
              remaining_edges.end());

    const auto* first = find_face(result.value(), 2399U);
    const auto* second = find_face(result.value(), 2398U);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(find_face(result.value(), 2438U), nullptr);
    EXPECT_EQ(find_face(result.value(), 2440U), nullptr);
    const auto expect_corners = [](
        const AuthoredTopologyFace& face,
        std::initializer_list<std::pair<std::uint32_t, Vec2>> expected) {
      ASSERT_EQ(face.vertices.size(), expected.size());
      ASSERT_EQ(face.uvs.size(), expected.size());
      for (const auto& [vertex, uv] : expected) {
        const auto found = std::ranges::find(face.vertices,
                                            make_id<VertexTag>(vertex));
        ASSERT_NE(found, face.vertices.end());
        const auto index = static_cast<std::size_t>(found - face.vertices.begin());
        EXPECT_FLOAT_EQ(face.uvs[index].x, uv.x);
        EXPECT_FLOAT_EQ(face.uvs[index].y, uv.y);
      }
    };
    expect_corners(*first, {{2430U, topology.faces[0].uvs[0]},
                            {2429U, topology.faces[0].uvs[1]},
                            {1309U, topology.faces[1].uvs[3]},
                            {1310U, topology.faces[1].uvs[0]}});
    expect_corners(*second, {{2429U, topology.faces[2].uvs[0]},
                             {2428U, topology.faces[2].uvs[1]},
                             {2468U, topology.faces[3].uvs[2]},
                             {1308U, topology.faces[3].uvs[3]},
                             {1309U, topology.faces[3].uvs[0]}});
    for (const auto* face : {first, second}) {
      EXPECT_EQ(face->material_slot, 1U);
      EXPECT_EQ(face->shading, topology.faces[0].shading);
      EXPECT_EQ(face->texture_transform, topology.faces[0].texture_transform);
    }
    for (const auto index : {4U, 5U}) {
      const auto* untouched = find_face(result.value(), topology.faces[index].id.index);
      ASSERT_NE(untouched, nullptr);
      expect_exact_face(*untouched, topology.faces[index]);
    }

    const auto result_selection = dissolved.selection();
    EXPECT_EQ(result_selection.kind, SelectionKind::Face);
    EXPECT_TRUE(result_selection.edges.empty());
    EXPECT_TRUE(result_selection.has_active);
    auto selected_faces = result_selection.faces;
    std::ranges::sort(selected_faces);
    const std::vector<FaceId> expected_faces{make_id<FaceTag>(2398U),
                                            make_id<FaceTag>(2399U)};
    EXPECT_EQ(selected_faces, expected_faces);
    EXPECT_NE(std::ranges::find(selected_faces, result_selection.active_face),
              selected_faces.end());
    const auto after = serialize_qdr_authored_topology(dissolved);
    ASSERT_TRUE(after.ok()) << after.error_message();
    if (reversed) {
      EXPECT_EQ(after.value(), forward_topology);
    } else {
      forward_topology = after.value();
    }

    const auto committed = session.commit_document(
        added.value(), dissolved, operation.value());
    ASSERT_TRUE(committed.ok()) << committed.error_message();
    ASSERT_TRUE(session.can_undo());
    ASSERT_TRUE(session.undo().ok());
    EXPECT_FALSE(session.can_undo());
    auto restored = session.document_copy(added.value());
    ASSERT_TRUE(restored.ok()) << restored.error_message();
    const auto undone = serialize_qdr_authored_topology(restored.value());
    ASSERT_TRUE(undone.ok());
    EXPECT_EQ(undone.value(), before.value());
    EXPECT_EQ(restored.value().selection().kind, original_selection.kind);
    EXPECT_EQ(restored.value().selection().edges, original_selection.edges);
    EXPECT_EQ(restored.value().selection().active_edge, original_selection.active_edge);
    EXPECT_EQ(restored.value().selection().has_active, original_selection.has_active);

    ASSERT_TRUE(session.redo().ok());
    EXPECT_FALSE(session.can_redo());
    restored = session.document_copy(added.value());
    ASSERT_TRUE(restored.ok()) << restored.error_message();
    const auto redone = serialize_qdr_authored_topology(restored.value());
    ASSERT_TRUE(redone.ok());
    EXPECT_EQ(redone.value(), after.value());
    EXPECT_EQ(restored.value().selection().kind, result_selection.kind);
    EXPECT_EQ(restored.value().selection().faces, result_selection.faces);
    EXPECT_EQ(restored.value().selection().active_face, result_selection.active_face);
    EXPECT_EQ(restored.value().selection().has_active, result_selection.has_active);
  }
}

} // namespace
