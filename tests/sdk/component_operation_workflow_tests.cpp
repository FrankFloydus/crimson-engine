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
#include <initializer_list>
#include <limits>
#include <span>
#include <string_view>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace quader::modeling;

struct ComponentWorkflow {
  std::string_view name;
  OperationKind operation;
  SelectionKind result_kind;
  std::size_t selected_count;
};

constexpr std::array kComponentWorkflows{
    ComponentWorkflow{"Connect vertices", OperationKind::ConnectVertices, SelectionKind::Edge, 1U},
    ComponentWorkflow{"Merge edges", OperationKind::MergeEdges, SelectionKind::Edge, 1U},
    ComponentWorkflow{"Split edges", OperationKind::SplitEdges, SelectionKind::Edge, 2U},
    ComponentWorkflow{"Connect edges", OperationKind::ConnectEdges, SelectionKind::Edge, 1U},
    ComponentWorkflow{"Bridge edges", OperationKind::Bridge, SelectionKind::Face, 3U},
    ComponentWorkflow{"Dissolve vertices", OperationKind::DissolveVertices, SelectionKind::Vertex, 2U},
    ComponentWorkflow{"Dissolve edges", OperationKind::DissolveEdges, SelectionKind::Face, 1U},
};

VertexId vertex_id(std::uint32_t value) {
  return make_id<VertexTag>(value);
}

AuthoredTopologyFace workflow_face(std::uint32_t id, std::initializer_list<std::uint32_t> vertices,
                                  std::uint32_t material = 7U) {
  AuthoredTopologyFace face{
      .id = make_id<FaceTag>(id),
      .material_slot = material,
      .shading = AuthoredSurfaceShading::Smooth,
      .texture_transform = {.scale = {-2.0F, 0.5F}, .shift_uv = {0.125F, -0.25F}, .rotation_degrees = 17.0F},
  };
  for (const auto index : vertices) {
    face.vertices.push_back(vertex_id(index));
    face.uvs.push_back({static_cast<float>(index) * 0.25F, static_cast<float>(index % 3U)});
  }
  return face;
}

PolygonDocument make_workflow_document(OperationKind operation, bool unrelated = true) {
  AuthoredTopology topology;
  topology.vertices = {{vertex_id(1U), {0.0F, 0.0F, 0.0F}},
                       {vertex_id(2U), {2.0F, 0.0F, 0.0F}},
                       {vertex_id(3U), {2.0F, 2.0F, 0.0F}},
                       {vertex_id(4U), {0.0F, 2.0F, 0.0F}}};
  topology.faces = {workflow_face(1U, {1U, 2U, 3U, 4U})};
  PolygonSelectionSnapshot selection{.kind = SelectionKind::Edge, .has_active = true};
  const auto edge = [](std::uint32_t first, std::uint32_t second) {
    return make_edge_key(vertex_id(first), vertex_id(second));
  };
  switch (operation) {
  case OperationKind::ConnectVertices:
    selection.kind = SelectionKind::Vertex;
    selection.vertices = {vertex_id(1U), vertex_id(3U)};
    selection.active_vertex = vertex_id(3U);
    break;
  case OperationKind::ConnectEdges:
    selection.edges = {edge(1U, 2U), edge(3U, 4U)};
    break;
  case OperationKind::DissolveVertices:
    topology.vertices.push_back({vertex_id(5U), {1.0F, 0.0F, 0.0F}});
    topology.faces = {workflow_face(1U, {1U, 5U, 2U, 3U, 4U})};
    selection.kind = SelectionKind::Vertex;
    selection.vertices = {vertex_id(5U)};
    selection.active_vertex = vertex_id(5U);
    break;
  case OperationKind::SplitEdges:
  case OperationKind::DissolveEdges:
    topology.vertices.push_back({vertex_id(5U), {4.0F, 0.0F, 0.0F}});
    topology.vertices.push_back({vertex_id(6U), {4.0F, 2.0F, 0.0F}});
    topology.faces.push_back(workflow_face(2U, {2U, 5U, 6U, 3U}));
    selection.edges = {edge(2U, 3U)};
    break;
  case OperationKind::Bridge:
    topology.vertices.push_back({vertex_id(5U), {4.0F, 0.0F, 0.0F}});
    topology.vertices.push_back({vertex_id(6U), {6.0F, 0.0F, 0.0F}});
    topology.vertices.push_back({vertex_id(7U), {6.0F, 2.0F, 0.0F}});
    topology.vertices.push_back({vertex_id(8U), {4.0F, 2.0F, 0.0F}});
    topology.faces.push_back(workflow_face(2U, {5U, 6U, 7U, 8U}));
    selection.edges = {edge(2U, 3U), edge(5U, 8U)};
    break;
  case OperationKind::MergeEdges:
    topology.vertices.push_back({vertex_id(5U), {2.0F, 4.0F, 0.0F}});
    topology.vertices.push_back({vertex_id(6U), {0.0F, 4.0F, 0.0F}});
    topology.vertices.push_back({vertex_id(7U), {0.0F, 6.0F, 0.0F}});
    topology.vertices.push_back({vertex_id(8U), {2.0F, 6.0F, 0.0F}});
    topology.faces.push_back(workflow_face(2U, {5U, 6U, 7U, 8U}));
    selection.edges = {edge(1U, 2U), edge(5U, 6U)};
    break;
  default:
    ADD_FAILURE() << "Unexpected component workflow";
    break;
  }
  if (selection.kind == SelectionKind::Edge) {
    selection.active_edge = selection.edges.front();
  }
  if (unrelated) {
    topology.vertices.insert(topology.vertices.end(), {
        {vertex_id(100U), {10.0F, 0.0F, 0.0F}},
        {vertex_id(101U), {11.0F, 0.0F, 0.0F}},
        {vertex_id(102U), {12.0F, 0.0F, 0.0F}},
        {vertex_id(103U), {14.0F, -2.0F, 3.0F}},
        {vertex_id(104U), {14.0F, -2.0F, 3.0F}}});
    topology.faces.push_back(workflow_face(90U, {100U, 101U, 102U}, 99U));
    topology.hard_normal_edges.push_back(edge(100U, 101U));
    topology.soft_normal_edges.push_back(edge(101U, 102U));
  }
  topology.next_vertex_id = 200U;
  topology.next_face_id = 100U;
  PolygonDocument document = PolygonDocument::from_authored_topology(topology).or_throw();
  static_cast<void>(document.apply_selection(selection).or_throw());
  return document;
}

Result<OperationResult> apply_workflow(PolygonDocument& document, OperationKind operation) {
  switch (operation) {
  case OperationKind::ConnectVertices: return document.connect_selected_vertices();
  case OperationKind::MergeEdges: return document.merge_selected_edges();
  case OperationKind::SplitEdges: return document.split_selected_edges();
  case OperationKind::ConnectEdges: return document.connect_selected_edges();
  case OperationKind::Bridge: return document.bridge_selected_edges(3);
  case OperationKind::DissolveVertices: return document.dissolve_selected_vertices();
  case OperationKind::DissolveEdges: return document.dissolve_selected_edges();
  default:
    return Result<OperationResult>::failure({ErrorCode::InvalidArgument, "Unexpected component workflow", {}});
  }
}

void expect_selection_equal(const PolygonSelectionSnapshot& actual, const PolygonSelectionSnapshot& expected) {
  EXPECT_EQ(actual.kind, expected.kind);
  EXPECT_EQ(actual.vertices, expected.vertices);
  EXPECT_EQ(actual.edges, expected.edges);
  EXPECT_EQ(actual.faces, expected.faces);
  EXPECT_EQ(actual.has_active, expected.has_active);
  EXPECT_EQ(actual.active_vertex, expected.active_vertex);
  EXPECT_EQ(actual.active_edge, expected.active_edge);
  EXPECT_EQ(actual.active_face, expected.active_face);
}

void expect_document_equal(const PolygonDocument& actual, const PolygonDocument& expected, bool high_water = false) {
  auto expected_topology = expected.authored_topology().or_throw();
  const auto actual_topology = actual.authored_topology().or_throw();
  if (high_water) {
    EXPECT_GE(actual_topology.next_vertex_id, expected_topology.next_vertex_id);
    EXPECT_GE(actual_topology.next_face_id, expected_topology.next_face_id);
    expected_topology.next_vertex_id = actual_topology.next_vertex_id;
    expected_topology.next_face_id = actual_topology.next_face_id;
  }
  const auto adjusted = PolygonDocument::from_authored_topology(expected_topology).or_throw();
  EXPECT_EQ(serialize_qdr_authored_topology(actual).or_throw(), serialize_qdr_authored_topology(adjusted).or_throw());
  expect_selection_equal(actual.selection(), expected.selection());
}

void expect_retained_authored_island(const PolygonDocument& actual, const PolygonDocument& expected) {
  const auto extract_island = [](const PolygonDocument& document) {
    auto topology = document.authored_topology().or_throw();
    std::erase_if(topology.vertices, [](const auto& vertex) { return vertex.id.index < 100U || vertex.id.index > 104U; });
    std::erase_if(topology.faces, [](const auto& face) { return face.id != make_id<FaceTag>(90U); });
    const auto outside_island = [](const auto& edge) {
      return edge.a.index < 100U || edge.a.index > 104U || edge.b.index < 100U || edge.b.index > 104U;
    };
    std::erase_if(topology.hard_normal_edges, outside_island);
    std::erase_if(topology.soft_normal_edges, outside_island);
    topology.next_vertex_id = 1000U;
    topology.next_face_id = 1000U;
    return PolygonDocument::from_authored_topology(topology).or_throw();
  };
  expect_document_equal(extract_island(actual), extract_island(expected));
}

enum class VertexExtrudeWorkflow { BoundaryCorner, BoundaryChain, InteriorFan };

constexpr std::array kVertexExtrudeWorkflows{
    VertexExtrudeWorkflow::BoundaryCorner, VertexExtrudeWorkflow::BoundaryChain,
    VertexExtrudeWorkflow::InteriorFan};

Vec3 vertex_extrude_offset(VertexExtrudeWorkflow workflow, float amount) {
  return workflow == VertexExtrudeWorkflow::InteriorFan
             ? Vec3{0.0F, 0.0F, amount}
             : Vec3{-amount, -amount, 0.0F};
}

PolygonDocument make_vertex_extrude_document(VertexExtrudeWorkflow workflow) {
  auto topology = make_workflow_document(OperationKind::ConnectVertices, false)
                      .authored_topology()
                      .or_throw();
  if (workflow == VertexExtrudeWorkflow::InteriorFan) {
    topology.vertices = {{vertex_id(1U), {0.0F, 0.0F, 0.0F}},
                         {vertex_id(2U), {2.0F, 0.0F, 0.0F}},
                         {vertex_id(3U), {0.0F, 2.0F, 0.0F}},
                         {vertex_id(4U), {-2.0F, 0.0F, 0.0F}},
                         {vertex_id(5U), {0.0F, -2.0F, 0.0F}}};
    topology.faces = {
        workflow_face(1U, {1U, 2U, 3U}), workflow_face(2U, {1U, 3U, 4U}),
        workflow_face(3U, {1U, 4U, 5U}), workflow_face(4U, {1U, 5U, 2U})};
  }
  PolygonDocument document =
      PolygonDocument::from_authored_topology(topology).or_throw();
  PolygonSelectionSnapshot selection{.kind = SelectionKind::Vertex,
                                     .vertices = {vertex_id(1U)},
                                     .has_active = true,
                                     .active_vertex = vertex_id(1U)};
  if (workflow == VertexExtrudeWorkflow::BoundaryChain) {
    selection.vertices.push_back(vertex_id(2U));
    selection.active_vertex = vertex_id(2U);
  }
  static_cast<void>(document.apply_selection(selection).or_throw());
  return document;
}

void expect_vertex_extrude_selection(const PolygonDocument &document,
                                     const PolygonDocument &source,
                                     VertexExtrudeWorkflow workflow,
                                     Vec3 offset) {
  const auto topology = document.authored_topology().or_throw();
  const auto source_topology = source.authored_topology().or_throw();
  const auto selection = document.selection();
  const auto source_selection = source.selection();
  EXPECT_EQ(selection.kind, SelectionKind::Vertex);
  EXPECT_TRUE(selection.edges.empty());
  EXPECT_TRUE(selection.faces.empty());
  ASSERT_EQ(selection.vertices.size(), source_selection.vertices.size());
  ASSERT_TRUE(selection.has_active);
  EXPECT_NE(std::find(selection.vertices.begin(), selection.vertices.end(),
                      selection.active_vertex),
            selection.vertices.end());
  if (workflow == VertexExtrudeWorkflow::InteriorFan) {
    expect_selection_equal(selection, source_selection);
    EXPECT_EQ(document.vertex_count(), source.vertex_count() + 4U);
    EXPECT_EQ(document.face_count(), source.face_count() + 4U);
  } else {
    EXPECT_EQ(document.vertex_count(),
              source.vertex_count() + source_selection.vertices.size());
    EXPECT_EQ(document.face_count(), source.face_count() + 1U);
    for (const auto selected : selection.vertices) {
      EXPECT_GE(selected.index, source_topology.next_vertex_id);
    }
  }
  for (const auto selected : source_selection.vertices) {
    const auto original = std::find_if(
        source_topology.vertices.begin(), source_topology.vertices.end(),
        [&](const auto &vertex) { return vertex.id == selected; });
    ASSERT_NE(original, source_topology.vertices.end());
    const auto moved = std::find_if(
        topology.vertices.begin(), topology.vertices.end(),
        [&](const auto &vertex) {
          return std::find(selection.vertices.begin(), selection.vertices.end(),
                           vertex.id) != selection.vertices.end() &&
                 vertex.position.x == original->position.x + offset.x &&
                 vertex.position.y == original->position.y + offset.y &&
                 vertex.position.z == original->position.z + offset.z;
        });
    ASSERT_NE(moved, topology.vertices.end());
    if (selected == source_selection.active_vertex) {
      EXPECT_EQ(selection.active_vertex, moved->id);
    }
  }
  EXPECT_TRUE(PolygonDocument(document).apply_selection(selection).ok());
}

PolygonDocument make_edge_loop_boundary_document() {
  auto topology = make_workflow_document(OperationKind::ConnectVertices).authored_topology().or_throw();
  topology.vertices.insert(topology.vertices.end(), {
      {vertex_id(5U), {4.0F, 0.0F, 0.0F}},
      {vertex_id(6U), {5.0F, 1.0F, 0.0F}},
      {vertex_id(7U), {4.0F, 2.0F, 0.0F}}});
  auto neighbor = workflow_face(2U, {2U, 5U, 6U, 7U, 3U}, 11U);
  neighbor.shading = AuthoredSurfaceShading::Flat;
  neighbor.texture_transform = {.scale = {0.75F, -3.0F}, .shift_uv = {2.0F, 4.0F}, .rotation_degrees = 61.0F};
  neighbor.uvs = {{10.0F, 20.0F}, {12.0F, 20.0F}, {13.0F, 22.0F}, {12.0F, 24.0F}, {10.0F, 24.0F}};
  topology.faces.push_back(std::move(neighbor));
  topology.hard_normal_edges.push_back(make_edge_key(vertex_id(2U), vertex_id(3U)));
  topology.soft_normal_edges.push_back(make_edge_key(vertex_id(1U), vertex_id(4U)));
  PolygonDocument document = PolygonDocument::from_authored_topology(topology).or_throw();
  const auto seed = make_edge_key(vertex_id(1U), vertex_id(4U));
  static_cast<void>(document.apply_selection({.kind = SelectionKind::Edge,
                                             .edges = {seed},
                                             .has_active = true,
                                             .active_edge = seed}).or_throw());
  return document;
}

void expect_complete_edge_loop_boundary(const PolygonDocument& document, const PolygonDocument& source, float factor) {
  const auto topology = document.authored_topology().or_throw();
  const auto source_topology = source.authored_topology().or_throw();
  ASSERT_EQ(topology.faces.size(), source_topology.faces.size() + 1U);
  ASSERT_EQ(topology.vertices.size(), source_topology.vertices.size() + 2U);
  for (const auto& original : source_topology.vertices) {
    const auto found = std::find_if(topology.vertices.begin(), topology.vertices.end(), [&](const auto& vertex) {
      return vertex.id == original.id;
    });
    ASSERT_NE(found, topology.vertices.end());
    EXPECT_EQ(found->position.x, original.position.x);
    EXPECT_EQ(found->position.y, original.position.y);
    EXPECT_EQ(found->position.z, original.position.z);
  }
  VertexId entry_vertex;
  VertexId shared_vertex;
  for (const auto& vertex : topology.vertices) {
    if (vertex.id.index < source_topology.next_vertex_id) {
      continue;
    }
    EXPECT_FLOAT_EQ(vertex.position.y, factor * 2.0F);
    EXPECT_FLOAT_EQ(vertex.position.z, 0.0F);
    if (vertex.position.x == 0.0F) {
      EXPECT_FALSE(entry_vertex.valid());
      entry_vertex = vertex.id;
    } else {
      EXPECT_FLOAT_EQ(vertex.position.x, 2.0F);
      EXPECT_FALSE(shared_vertex.valid());
      shared_vertex = vertex.id;
    }
  }
  ASSERT_TRUE(entry_vertex.valid());
  ASSERT_TRUE(shared_vertex.valid());
  const auto edge_uses = [&](EdgeKey edge) {
    std::size_t count = 0U;
    for (const auto& face : topology.faces) {
      for (std::size_t corner = 0U; corner < face.vertices.size(); ++corner) {
        count += make_edge_key(face.vertices[corner], face.vertices[(corner + 1U) % face.vertices.size()]) == edge;
      }
    }
    return count;
  };
  EXPECT_EQ(edge_uses(make_edge_key(vertex_id(2U), vertex_id(3U))), 0U);
  EXPECT_EQ(edge_uses(make_edge_key(vertex_id(2U), shared_vertex)), 2U);
  EXPECT_EQ(edge_uses(make_edge_key(shared_vertex, vertex_id(3U))), 2U);
  const auto inserted_edge = make_edge_key(entry_vertex, shared_vertex);
  EXPECT_EQ(edge_uses(inserted_edge), 2U);
  const auto selection = document.selection();
  EXPECT_EQ(selection.kind, SelectionKind::Edge);
  EXPECT_EQ(selection.edges, std::vector<EdgeKey>{inserted_edge});
  EXPECT_TRUE(selection.has_active);
  EXPECT_EQ(selection.active_edge, inserted_edge);
  EXPECT_TRUE(PolygonDocument(document).apply_selection(selection).ok());

  const auto neighbor = std::find_if(topology.faces.begin(), topology.faces.end(), [](const auto& face) {
    return face.id == make_id<FaceTag>(2U);
  });
  ASSERT_NE(neighbor, topology.faces.end());
  EXPECT_EQ(neighbor->vertices, (std::vector<VertexId>{vertex_id(2U), vertex_id(5U), vertex_id(6U),
                                                    vertex_id(7U), vertex_id(3U), shared_vertex}));
  for (const auto& face : topology.faces) {
    if (face.id == make_id<FaceTag>(90U)) {
      continue;
    }
    const auto original = std::find_if(source_topology.faces.begin(), source_topology.faces.end(), [&](const auto& candidate) {
      return candidate.id == (face.id == make_id<FaceTag>(2U) ? make_id<FaceTag>(2U) : make_id<FaceTag>(1U));
    });
    ASSERT_NE(original, source_topology.faces.end());
    EXPECT_EQ(face.material_slot, original->material_slot);
    EXPECT_EQ(face.shading, original->shading);
    EXPECT_EQ(face.texture_transform, original->texture_transform);
    ASSERT_EQ(face.uvs.size(), face.vertices.size());
    for (std::size_t corner = 0U; corner < face.vertices.size(); ++corner) {
      const auto original_corner = std::find(original->vertices.begin(), original->vertices.end(), face.vertices[corner]);
      if (original_corner != original->vertices.end()) {
        const auto index = static_cast<std::size_t>(original_corner - original->vertices.begin());
        EXPECT_EQ(face.uvs[corner].x, original->uvs[index].x);
        EXPECT_EQ(face.uvs[corner].y, original->uvs[index].y);
      } else if (face.id == make_id<FaceTag>(2U)) {
        EXPECT_EQ(face.vertices[corner], shared_vertex);
        EXPECT_FLOAT_EQ(face.uvs[corner].x, 10.0F);
        EXPECT_FLOAT_EQ(face.uvs[corner].y, 20.0F + factor * 4.0F);
      } else if (face.vertices[corner] == entry_vertex) {
        EXPECT_FLOAT_EQ(face.uvs[corner].x, 0.25F + factor * 0.75F);
        EXPECT_FLOAT_EQ(face.uvs[corner].y, 1.0F);
      } else {
        EXPECT_EQ(face.vertices[corner], shared_vertex);
        EXPECT_FLOAT_EQ(face.uvs[corner].x, 0.5F + factor * 0.25F);
        EXPECT_FLOAT_EQ(face.uvs[corner].y, 2.0F - factor * 2.0F);
      }
    }
  }
  const auto has_normal_edge = [](const auto& edges, EdgeKey edge) {
    return std::find(edges.begin(), edges.end(), edge) != edges.end();
  };
  EXPECT_FALSE(has_normal_edge(topology.hard_normal_edges, make_edge_key(vertex_id(2U), vertex_id(3U))));
  EXPECT_TRUE(has_normal_edge(topology.hard_normal_edges, make_edge_key(vertex_id(2U), shared_vertex)));
  EXPECT_TRUE(has_normal_edge(topology.hard_normal_edges, make_edge_key(shared_vertex, vertex_id(3U))));
  EXPECT_FALSE(has_normal_edge(topology.soft_normal_edges, make_edge_key(vertex_id(1U), vertex_id(4U))));
  EXPECT_TRUE(has_normal_edge(topology.soft_normal_edges, make_edge_key(vertex_id(1U), entry_vertex)));
  EXPECT_TRUE(has_normal_edge(topology.soft_normal_edges, make_edge_key(entry_vertex, vertex_id(4U))));
  expect_retained_authored_island(document, source);
}

TEST(ModelingSdkComponentOperationWorkflowTests, AuthoredOperationsPublishLiveSelectionsAndRetainAttributes) {
  for (const auto& workflow : kComponentWorkflows) {
    SCOPED_TRACE(workflow.name);
    PolygonDocument document = make_workflow_document(workflow.operation);
    const PolygonDocument source = document;

    const auto result = apply_workflow(document, workflow.operation);

    ASSERT_TRUE(result.ok()) << result.error_message();
    ASSERT_TRUE(result.value().changed);
    EXPECT_TRUE(result.value().success);
    EXPECT_NE(document.content_identity(), source.content_identity());
    const auto selection = document.selection();
    EXPECT_EQ(selection.kind, workflow.result_kind);
    EXPECT_TRUE(selection.has_active);
    const std::size_t selected_count = selection.kind == SelectionKind::Edge ? selection.edges.size()
        : selection.kind == SelectionKind::Face ? selection.faces.size() : selection.vertices.size();
    EXPECT_EQ(selected_count, workflow.selected_count);
    EXPECT_TRUE(document.apply_selection(selection).ok());
    const auto topology = document.authored_topology().or_throw();
    for (const auto& face : topology.faces) {
      if (face.id == make_id<FaceTag>(90U)) {
        continue;
      }
      EXPECT_EQ(face.material_slot, 7U);
      EXPECT_EQ(face.shading, AuthoredSurfaceShading::Smooth);
      EXPECT_EQ(face.texture_transform, source.authored_topology().or_throw().faces.front().texture_transform);
      ASSERT_EQ(face.uvs.size(), face.vertices.size());
      for (const auto uv : face.uvs) {
        EXPECT_TRUE(std::isfinite(uv.x));
        EXPECT_TRUE(std::isfinite(uv.y));
      }
    }
    expect_retained_authored_island(document, source);
  }
}

TEST(ModelingSdkComponentOperationWorkflowTests, SessionCommitUndoRedoRestoresAuthoredGeometryAndComponentSelection) {
  for (const auto& workflow : kComponentWorkflows) {
    SCOPED_TRACE(workflow.name);
    const PolygonDocument source = make_workflow_document(workflow.operation);
    ModelingSession session;
    const ObjectId object = session.add_document(source, std::string(workflow.name)).or_throw();
    const std::size_t undo_depth = session.undo_depth();
    PolygonDocument candidate = session.document_copy(object).or_throw();
    const auto receipt = apply_workflow(candidate, workflow.operation);
    ASSERT_TRUE(receipt.ok()) << receipt.error_message();
    ASSERT_TRUE(receipt.value().changed);

    const auto committed = session.commit_documents_batch({{object, candidate, receipt.value()}}, true);

    ASSERT_TRUE(committed.ok()) << committed.error_message();
    EXPECT_TRUE(committed.value().changed);
    EXPECT_EQ(session.undo_depth(), undo_depth + 1U);
    const PolygonDocument published = session.document_copy(object).or_throw();
    expect_document_equal(published, candidate);
    ASSERT_TRUE(session.undo().ok());
    EXPECT_EQ(session.undo_depth(), undo_depth);
    expect_document_equal(session.document_copy(object).or_throw(), source, true);
    ASSERT_TRUE(session.redo().ok());
    expect_document_equal(session.document_copy(object).or_throw(), published);
  }
}

TEST(ModelingSdkComponentOperationWorkflowTests,
     VertexExtrusionCommitUndoRedoPreservesRemappedSelectionAndTopology) {
  for (const auto workflow : kVertexExtrudeWorkflows) {
    SCOPED_TRACE(static_cast<int>(workflow));
    const auto source = make_vertex_extrude_document(workflow);
    ModelingSession session;
    const auto object =
        session.add_document(source, "Vertex extrusion").or_throw();
    session.clear_history();
    auto candidate = session.document_copy(object).or_throw();
    const auto offset = vertex_extrude_offset(workflow, 0.75F);
    const auto receipt = candidate.extrude_selected_elements(offset, 0.5F);
    ASSERT_TRUE(receipt.ok()) << receipt.error_message();
    ASSERT_TRUE(receipt.value().changed);
    EXPECT_TRUE(receipt.value().dirty.topology);
    expect_vertex_extrude_selection(candidate, source, workflow, offset);

    const auto committed = session.commit_documents_batch(
        {{object, candidate, receipt.value()}}, true);

    ASSERT_TRUE(committed.ok()) << committed.error_message();
    ASSERT_TRUE(committed.value().changed);
    EXPECT_EQ(session.undo_depth(), 1U);
    const auto published = session.document_copy(object).or_throw();
    expect_document_equal(published, candidate);
    ASSERT_TRUE(session.undo().ok());
    EXPECT_EQ(session.undo_depth(), 0U);
    expect_document_equal(session.document_copy(object).or_throw(), source,
                          true);
    ASSERT_TRUE(session.redo().ok());
    EXPECT_EQ(session.undo_depth(), 1U);
    expect_document_equal(session.document_copy(object).or_throw(), published);
    expect_vertex_extrude_selection(session.document_copy(object).or_throw(),
                                    source, workflow, offset);
  }
}

TEST(ModelingSdkComponentOperationWorkflowTests,
     VertexExtrusionPreviewRejectCancelAndCommitRetireGeneratedIdentities) {
  for (const auto workflow : kVertexExtrudeWorkflows) {
    SCOPED_TRACE(static_cast<int>(workflow));
    const auto source = make_vertex_extrude_document(workflow);
    ModelingSession session;
    const auto object =
        session.add_document(source, "Vertex extrusion preview").or_throw();
    session.clear_history();
    auto candidate = session.document_copy(object).or_throw();
    const auto first_receipt =
        candidate
            .extrude_selected_elements(vertex_extrude_offset(workflow, 0.25F),
                                       0.5F)
            .or_throw();
    ASSERT_TRUE(first_receipt.changed);
    ASSERT_FALSE(first_receipt.created.vertices.empty());
    ASSERT_FALSE(first_receipt.created.faces.empty());
    const auto retired_topology = candidate.authored_topology().or_throw();
    const auto canceled =
        session
            .begin_document_preview(object, "modeling.extrude", candidate,
                                    first_receipt)
            .or_throw();
    expect_document_equal(
        session.preview_document_copy(canceled, object).or_throw(), candidate);
    expect_document_equal(session.document_copy(object).or_throw(), source,
                          true);
    ASSERT_TRUE(session.cancel_preview(canceled).ok());
    EXPECT_FALSE(session.preview_active());
    EXPECT_FALSE(session.can_undo());
    EXPECT_FALSE(session.can_redo());
    expect_document_equal(session.document_copy(object).or_throw(), source,
                          true);
    const auto restored_topology =
        session.document_copy(object).or_throw().authored_topology().or_throw();
    EXPECT_GE(restored_topology.next_vertex_id,
              retired_topology.next_vertex_id);
    EXPECT_GE(restored_topology.next_face_id, retired_topology.next_face_id);

    candidate = session.document_copy(object).or_throw();
    const auto second_receipt =
        candidate
            .extrude_selected_elements(vertex_extrude_offset(workflow, 0.5F),
                                       0.5F)
            .or_throw();
    for (const auto created : second_receipt.created.vertices) {
      EXPECT_EQ(std::find(first_receipt.created.vertices.begin(),
                          first_receipt.created.vertices.end(), created),
                first_receipt.created.vertices.end());
    }
    for (const auto created : second_receipt.created.faces) {
      EXPECT_EQ(std::find(first_receipt.created.faces.begin(),
                          first_receipt.created.faces.end(), created),
                first_receipt.created.faces.end());
    }
    const auto preview = session
                             .begin_document_preview(object, "modeling.extrude",
                                                     candidate, second_receipt)
                             .or_throw();
    candidate = session.preview_base_document_copy(preview, object).or_throw();
    const auto offset = vertex_extrude_offset(workflow, 1.25F);
    const auto updated_receipt =
        candidate.extrude_selected_elements(offset, 0.5F).or_throw();
    ASSERT_TRUE(
        session.update_document_preview(preview, candidate, updated_receipt)
            .ok());
    expect_vertex_extrude_selection(
        session.preview_document_copy(preview, object).or_throw(), source,
        workflow, offset);
    const auto accepted =
        session.preview_document_copy(preview, object).or_throw();
    const auto preview_revision = session.preview_summary().content_revision;

    auto rejected =
        session.preview_base_document_copy(preview, object).or_throw();
    const auto before_rejection = rejected;
    const auto failure = rejected.extrude_selected_elements(offset, -1.0F);
    ASSERT_FALSE(failure.ok());
    expect_document_equal(rejected, before_rejection);
    OperationResult rejected_receipt;
    rejected_receipt.success = false;
    rejected_receipt.error_code = failure.error().code;
    rejected_receipt.message = failure.error_message();
    EXPECT_FALSE(
        session.update_document_preview(preview, rejected, rejected_receipt)
            .ok());
    EXPECT_EQ(session.preview_summary().content_revision, preview_revision);
    expect_document_equal(
        session.preview_document_copy(preview, object).or_throw(), accepted);
    expect_document_equal(session.document_copy(object).or_throw(), source,
                          true);
    EXPECT_EQ(session.undo_depth(), 0U);

    ASSERT_TRUE(session.commit_preview(preview).ok());
    EXPECT_FALSE(session.preview_active());
    EXPECT_EQ(session.undo_depth(), 1U);
    const auto published = session.document_copy(object).or_throw();
    expect_document_equal(published, accepted);
    ASSERT_TRUE(session.undo().ok());
    expect_document_equal(session.document_copy(object).or_throw(), source,
                          true);
    ASSERT_TRUE(session.redo().ok());
    expect_document_equal(session.document_copy(object).or_throw(), published);
  }
}

TEST(ModelingSdkComponentOperationWorkflowTests,
     VertexExtrusionPortableQdrRetainsExactTopologyAndActiveSelection) {
  for (const auto workflow : kVertexExtrudeWorkflows) {
    SCOPED_TRACE(static_cast<int>(workflow));
    auto document = make_vertex_extrude_document(workflow);
    const auto receipt = document.extrude_selected_elements(
        vertex_extrude_offset(workflow, 0.75F), 0.5F);
    ASSERT_TRUE(receipt.ok()) << receipt.error_message();
    ASSERT_TRUE(receipt.value().changed);
    const auto object = make_id<ObjectTag>(19U, 3U);
    QdrDocumentDto portable;
    portable.objects.push_back({.id = object,
                                .selected = true,
                                .name = "Vertex extrusion",
                                .document = document});
    portable.active_object = object;
    portable.selection_mode = QdrSelectionMode::Vertex;
    const auto encoded = serialize_qdr_document(portable);
    ASSERT_TRUE(encoded.ok()) << encoded.error_message();

    const auto decoded = deserialize_qdr_document(encoded.value());

    ASSERT_TRUE(decoded.ok()) << decoded.error_message();
    ASSERT_EQ(decoded.value().objects.size(), 1U);
    EXPECT_EQ(decoded.value().active_object, object);
    EXPECT_EQ(decoded.value().selection_mode, QdrSelectionMode::Vertex);
    EXPECT_TRUE(decoded.value().objects.front().selected);
    expect_document_equal(decoded.value().objects.front().document, document);
    EXPECT_EQ(serialize_qdr_document(decoded.value()).or_throw(),
              encoded.value());
  }
}

TEST(ModelingSdkComponentOperationWorkflowTests,
     VertexExtrusionRotationAtApexCreatesUndoableSupportFan) {
  const auto source =
      make_vertex_extrude_document(VertexExtrudeWorkflow::InteriorFan);
  ModelingSession session;
  const auto object =
      session.add_document(source, "Vertex rotation extrusion").or_throw();
  session.clear_history();
  auto candidate = session.document_copy(object).or_throw();
  const auto cosine = std::sqrt(0.5F);
  const Transform3 rotation{.x_axis = {cosine, cosine, 0.0F},
                            .y_axis = {-cosine, cosine, 0.0F}};

  const auto receipt =
      candidate.transform_extrude_selected_elements(rotation, {}, 0.5F);

  ASSERT_TRUE(receipt.ok()) << receipt.error_message();
  ASSERT_TRUE(receipt.value().changed);
  EXPECT_TRUE(receipt.value().dirty.topology);
  expect_vertex_extrude_selection(candidate, source,
                                  VertexExtrudeWorkflow::InteriorFan, {});
  ASSERT_TRUE(
      session
          .commit_documents_batch({{object, candidate, receipt.value()}}, true)
          .ok());
  EXPECT_EQ(session.undo_depth(), 1U);
  ASSERT_TRUE(session.undo().ok());
  expect_document_equal(session.document_copy(object).or_throw(), source, true);
  ASSERT_TRUE(session.redo().ok());
  expect_document_equal(session.document_copy(object).or_throw(), candidate);
}

TEST(ModelingSdkComponentOperationWorkflowTests,
     VertexExtrusionInvalidSetbackPreservesDocumentAndAtomicSessionBatch) {
  const auto source =
      make_vertex_extrude_document(VertexExtrudeWorkflow::InteriorFan);
  for (const auto setback : {-1.0F, std::numeric_limits<float>::infinity(),
                             std::numeric_limits<float>::quiet_NaN()}) {
    SCOPED_TRACE(setback);
    auto rejected = source;
    const auto failure =
        rejected.extrude_selected_elements({0.0F, 0.0F, 0.75F}, setback);
    ASSERT_FALSE(failure.ok());
    EXPECT_FALSE(failure.error_message().empty());
    EXPECT_TRUE(rejected.shares_topology_with(source));
    EXPECT_TRUE(rejected.shares_selection_with(source));
    EXPECT_EQ(rejected.content_revision(), source.content_revision());
    EXPECT_EQ(rejected.selection_revision(), source.selection_revision());
    expect_document_equal(rejected, source);

    ModelingSession session;
    const auto first = session.add_document(source, "First").or_throw();
    const auto second = session.add_document(source, "Second").or_throw();
    session.clear_history();
    ASSERT_TRUE(session.rename_object(first, "Renamed").ok());
    ASSERT_TRUE(session.undo().ok());
    const auto revision = session.content_revision();
    const auto selection_revision = session.selection_revision();
    auto candidate = session.document_copy(first).or_throw();
    const auto receipt =
        candidate.extrude_selected_elements({0.0F, 0.0F, 0.75F}, 0.5F)
            .or_throw();
    OperationResult rejected_receipt;
    rejected_receipt.success = false;
    rejected_receipt.error_code = failure.error().code;
    rejected_receipt.message = failure.error_message();

    const auto committed = session.commit_documents_batch(
        {{first, candidate, receipt}, {second, rejected, rejected_receipt}},
        true);

    EXPECT_FALSE(committed.ok());
    EXPECT_EQ(session.content_revision(), revision);
    EXPECT_EQ(session.selection_revision(), selection_revision);
    EXPECT_EQ(session.undo_depth(), 0U);
    EXPECT_TRUE(session.can_redo());
    expect_document_equal(session.document_copy(first).or_throw(), source);
    expect_document_equal(session.document_copy(second).or_throw(), source);
  }
  auto rejected = source;
  const Transform3 translated{.origin = {0.0F, 0.0F, 0.75F}};
  EXPECT_FALSE(
      rejected.transform_extrude_selected_elements(translated, {}, 0.0F).ok());
  expect_document_equal(rejected, source);
}

TEST(ModelingSdkComponentOperationWorkflowTests, SteppedBridgeUvPreviewCancelCommitAndHistoryRetainEndpointCharts) {
  auto topology = make_workflow_document(OperationKind::Bridge, false).authored_topology().or_throw();
  topology.faces[0].texture_transform = {};
  topology.faces[0].uvs = {{0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}};
  topology.faces[1].texture_transform = {};
  topology.faces[1].uvs = {{10.0F, 0.0F}, {11.0F, 0.0F}, {11.0F, 1.0F}, {10.0F, 1.0F}};
  PolygonDocument source = PolygonDocument::from_authored_topology(topology).or_throw();
  static_cast<void>(source.apply_selection(make_workflow_document(OperationKind::Bridge, false).selection()).or_throw());
  const auto pinned_source = source;
  ModelingSession session;
  const auto object = session.add_document(source, "Stepped UV bridge").or_throw();
  session.clear_history();
  const auto expect_strip_uvs = [&](const PolygonDocument& document, int steps) {
    const auto result = document.authored_topology().or_throw();
    ASSERT_EQ(result.faces.size(), 2U + static_cast<std::size_t>(steps));
    std::size_t strip = 0U;
    for (const auto& face : result.faces) {
      if (face.id.index <= 2U) {
        continue;
      }
      ASSERT_EQ(face.uvs.size(), 4U);
      const auto first_u = 1.0F + 9.0F * static_cast<float>(strip) / static_cast<float>(steps);
      const auto second_u = 1.0F + 9.0F * static_cast<float>(strip + 1U) / static_cast<float>(steps);
      const auto [minimum, maximum] = std::minmax_element(face.uvs.begin(), face.uvs.end(),
          [](Vec2 first, Vec2 second) { return first.x < second.x; });
      EXPECT_NEAR(minimum->x, first_u, 0.00001F);
      EXPECT_NEAR(maximum->x, second_u, 0.00001F);
      for (const auto uv : face.uvs) {
        EXPECT_TRUE(uv.y == 0.0F || uv.y == 1.0F);
      }
      ++strip;
    }
  };
  const auto begin_bridge = [&]() {
    auto candidate = session.document_copy(object).or_throw();
    const auto receipt = candidate.bridge_selected_edges(3).or_throw();
    return session.begin_document_preview(object, "modeling.bridge", candidate, receipt).or_throw();
  };
  const auto canceled = begin_bridge();
  expect_strip_uvs(session.preview_document_copy(canceled, object).or_throw(), 3);
  expect_document_equal(session.document_copy(object).or_throw(), source, true);
  ASSERT_TRUE(session.cancel_preview(canceled).ok());
  EXPECT_EQ(session.undo_depth(), 0U);
  expect_document_equal(session.document_copy(object).or_throw(), source, true);

  const auto preview = begin_bridge();
  auto candidate = session.preview_base_document_copy(preview, object).or_throw();
  const auto receipt = candidate.bridge_selected_edges(5).or_throw();
  ASSERT_TRUE(session.update_document_preview(preview, candidate, receipt).ok());
  expect_strip_uvs(session.preview_document_copy(preview, object).or_throw(), 5);
  ASSERT_TRUE(session.commit_preview(preview).ok());
  EXPECT_EQ(session.undo_depth(), 1U);
  const auto published = session.document_copy(object).or_throw();
  expect_strip_uvs(published, 5);
  expect_document_equal(published, candidate);
  expect_document_equal(source, pinned_source);
  ASSERT_TRUE(session.undo().ok());
  expect_document_equal(session.document_copy(object).or_throw(), source, true);
  ASSERT_TRUE(session.redo().ok());
  expect_document_equal(session.document_copy(object).or_throw(), published);
  expect_strip_uvs(session.document_copy(object).or_throw(), 5);
}

TEST(ModelingSdkComponentOperationWorkflowTests, EdgeLoopSplitsSharedBoundaryWithUnwalkedNgonAndRetainsAuthoredData) {
  PolygonDocument document = make_edge_loop_boundary_document();
  const PolygonDocument source = document;

  const auto result = document.insert_edge_loop(0.25F);

  ASSERT_TRUE(result.ok()) << result.error_message();
  EXPECT_TRUE(result.value().success);
  ASSERT_TRUE(result.value().changed);
  EXPECT_NE(document.content_identity(), source.content_identity());
  expect_complete_edge_loop_boundary(document, source, 0.25F);
}

TEST(ModelingSdkComponentOperationWorkflowTests, EdgeLoopPreviewUpdateCancelCommitUndoRedoPreservesCompleteSharedBoundary) {
  const PolygonDocument source = make_edge_loop_boundary_document();
  ModelingSession session;
  const ObjectId object = session.add_document(source, "Quad to ngon edge loop").or_throw();
  session.clear_history();
  const auto begin_loop_preview = [&]() {
    PolygonDocument candidate = session.document_copy(object).or_throw();
    const auto receipt = candidate.insert_edge_loop(0.25F).or_throw();
    EXPECT_TRUE(receipt.success);
    EXPECT_TRUE(receipt.changed);
    return session.begin_document_preview(object, "modeling.insert_edge_loop", candidate, receipt).or_throw();
  };
  const auto update_loop_preview = [&](PreviewId preview) {
    PolygonDocument candidate = session.preview_base_document_copy(preview, object).or_throw();
    const auto receipt = candidate.insert_edge_loop(0.75F).or_throw();
    EXPECT_TRUE(receipt.success);
    EXPECT_TRUE(receipt.changed);
    const auto updated = session.update_document_preview(preview, candidate, receipt);
    EXPECT_TRUE(updated.ok()) << updated.error_message();
    if (updated.ok()) {
      EXPECT_TRUE(updated.value().changed);
    }
    expect_document_equal(session.preview_document_copy(preview, object).or_throw(), candidate);
    expect_complete_edge_loop_boundary(session.preview_document_copy(preview, object).or_throw(), source, 0.75F);
    expect_document_equal(session.document_copy(object).or_throw(), source, true);
    EXPECT_EQ(session.undo_depth(), 0U);
  };

  const PreviewId canceled = begin_loop_preview();
  EXPECT_TRUE(session.preview_active(canceled));
  expect_complete_edge_loop_boundary(session.preview_document_copy(canceled, object).or_throw(), source, 0.25F);
  expect_document_equal(session.document_copy(object).or_throw(), source, true);
  update_loop_preview(canceled);
  ASSERT_TRUE(session.cancel_preview(canceled).ok());
  EXPECT_FALSE(session.preview_active());
  EXPECT_FALSE(session.can_undo());
  EXPECT_FALSE(session.can_redo());
  expect_document_equal(session.document_copy(object).or_throw(), source, true);

  const PreviewId committed_preview = begin_loop_preview();
  update_loop_preview(committed_preview);
  const PolygonDocument candidate = session.preview_document_copy(committed_preview, object).or_throw();
  const auto committed = session.commit_preview(committed_preview);
  ASSERT_TRUE(committed.ok()) << committed.error_message();
  EXPECT_TRUE(committed.value().changed);
  EXPECT_FALSE(session.preview_active());
  EXPECT_EQ(session.undo_depth(), 1U);
  const PolygonDocument published = session.document_copy(object).or_throw();
  expect_document_equal(published, candidate);
  expect_complete_edge_loop_boundary(published, source, 0.75F);
  ASSERT_TRUE(session.undo().ok());
  EXPECT_EQ(session.undo_depth(), 0U);
  expect_document_equal(session.document_copy(object).or_throw(), source, true);
  ASSERT_TRUE(session.redo().ok());
  EXPECT_EQ(session.undo_depth(), 1U);
  expect_document_equal(session.document_copy(object).or_throw(), published);
  expect_complete_edge_loop_boundary(session.document_copy(object).or_throw(), source, 0.75F);
}

TEST(ModelingSdkComponentOperationWorkflowTests, RejectedOperandsPreserveDocumentPublicationAndAtomicSessionBatch) {
  for (const auto& workflow : kComponentWorkflows) {
    SCOPED_TRACE(workflow.name);
    const PolygonDocument source = make_workflow_document(workflow.operation, false);
    PolygonDocument rejected = source;
    static_cast<void>(rejected.clear_selection().or_throw());
    const PolygonDocument before = rejected;
    const auto failure = apply_workflow(rejected, workflow.operation);
    ASSERT_FALSE(failure.ok());
    EXPECT_FALSE(failure.error_message().empty());
    EXPECT_TRUE(rejected.shares_topology_with(before));
    EXPECT_TRUE(rejected.shares_selection_with(before));
    EXPECT_EQ(rejected.content_revision(), before.content_revision());
    EXPECT_EQ(rejected.selection_revision(), before.selection_revision());
    expect_document_equal(rejected, before);

    ModelingSession session;
    const ObjectId first = session.add_document(source).or_throw();
    const ObjectId second = session.add_document(before).or_throw();
    ASSERT_TRUE(session.rename_object(first, "Renamed").ok());
    ASSERT_TRUE(session.undo().ok());
    const auto revision = session.content_revision();
    const auto selection_revision = session.selection_revision();
    const auto undo_depth = session.undo_depth();
    PolygonDocument candidate = session.document_copy(first).or_throw();
    const auto receipt = apply_workflow(candidate, workflow.operation);
    ASSERT_TRUE(receipt.ok()) << receipt.error_message();
    OperationResult rejected_receipt;
    rejected_receipt.success = false;
    rejected_receipt.message = failure.error_message();
    rejected_receipt.error_code = ErrorCode::ValidationFailed;

    const auto committed = session.commit_documents_batch({{first, candidate, receipt.value()}, {second, rejected, rejected_receipt}}, true);

    EXPECT_FALSE(committed.ok());
    EXPECT_EQ(session.content_revision(), revision);
    EXPECT_EQ(session.selection_revision(), selection_revision);
    EXPECT_EQ(session.undo_depth(), undo_depth);
    EXPECT_TRUE(session.can_redo());
    expect_document_equal(session.document_copy(first).or_throw(), source);
    expect_document_equal(session.document_copy(second).or_throw(), before);
  }
}

TEST(
    ModelingSdkComponentOperationWorkflowTests,
    BridgeShapeOnlyUpdatesPublishWithoutRebuildingGeometryAndRestoreCheckpoint) {
  const auto source = make_workflow_document(OperationKind::Bridge, false);
  ModelingSession session;
  const auto object =
      session.add_document(source, "Bridge guide preview").or_throw();
  session.clear_history();
  auto candidate = source;
  const auto receipt =
      candidate.bridge_selected_edges(BridgeOptions{}).or_throw();
  ASSERT_TRUE(receipt.bridge_shape.has_value());
  const auto preview =
      session
          .begin_document_preview(object, "modeling.bridge", candidate, receipt)
          .or_throw();
  ASSERT_EQ(session.preview_bridge_shape(), receipt.bridge_shape);
  ASSERT_TRUE(session.preview_payload(preview).ok());
  const auto checkpoint =
      session.capture_preview_checkpoint(preview).or_throw();
  const auto before = session.preview_summary();
  const auto builds = session.preview_work_stats().mesh_payload_build_count;
  auto changed_candidate = source;
  const auto changed_receipt =
      changed_candidate
          .bridge_selected_edges(
              BridgeOptions{.start_tension = 2.0F, .end_tension = 0.0F})
          .or_throw();
  expect_document_equal(changed_candidate, candidate);
  const auto updated = session.update_document_preview(
      preview, changed_candidate, changed_receipt);
  ASSERT_TRUE(updated.ok()) << updated.error_message();
  EXPECT_TRUE(updated.value().changed);
  EXPECT_GT(session.preview_summary().content_revision,
            before.content_revision);
  ASSERT_EQ(session.preview_bridge_shape(), changed_receipt.bridge_shape);
  ASSERT_TRUE(session.preview_payload(preview).ok());
  EXPECT_EQ(session.preview_work_stats().mesh_payload_build_count, builds);
  EXPECT_EQ(session.undo_depth(), 0U);
  expect_document_equal(session.document_copy(object).or_throw(), source, true);

  const auto stable_revision = session.preview_summary().content_revision;
  const auto repeated = session.update_document_preview(
      preview, changed_candidate, changed_receipt);
  ASSERT_TRUE(repeated.ok());
  EXPECT_FALSE(repeated.value().changed);
  EXPECT_EQ(session.preview_summary().content_revision, stable_revision);

  OperationResult rejected = changed_receipt;
  rejected.success = false;
  rejected.error_code = ErrorCode::InvalidArgument;
  EXPECT_FALSE(
      session.update_document_preview(preview, candidate, rejected).ok());
  EXPECT_EQ(session.preview_bridge_shape(), changed_receipt.bridge_shape);
  EXPECT_EQ(session.preview_summary().content_revision, stable_revision);
  auto flagged_receipt = changed_receipt;
  ASSERT_TRUE(flagged_receipt.bridge_shape.has_value());
  flagged_receipt.bridge_shape->representative_rail = true;
  const auto flag_update = session.update_document_preview(
      preview, changed_candidate, flagged_receipt);
  ASSERT_TRUE(flag_update.ok());
  EXPECT_TRUE(flag_update.value().changed);
  EXPECT_GT(session.preview_summary().content_revision, stable_revision);
  ASSERT_TRUE(session.preview_bridge_shape().has_value());
  EXPECT_TRUE(session.preview_bridge_shape()->representative_rail);
  ASSERT_TRUE(session.preview_payload(preview).ok());
  EXPECT_EQ(session.preview_work_stats().mesh_payload_build_count, builds);
  ASSERT_TRUE(session.restore_preview_checkpoint(checkpoint).ok());
  EXPECT_EQ(session.preview_bridge_shape(), receipt.bridge_shape);
  ASSERT_TRUE(session.preview_payload(preview).ok());
  EXPECT_EQ(session.preview_work_stats().mesh_payload_build_count, builds);
  expect_document_equal(
      session.preview_document_copy(preview, object).or_throw(), candidate);
  ASSERT_TRUE(session.cancel_preview(preview).ok());
  EXPECT_FALSE(session.preview_bridge_shape().has_value());
  expect_document_equal(session.document_copy(object).or_throw(), source, true);
}

TEST(ModelingSdkComponentOperationWorkflowTests,
     BridgeOptionsRejectInvalidTensionsWithoutAuthoredChanges) {
  const auto source = make_workflow_document(OperationKind::Bridge, false);
  for (const float invalid :
       {-0.1F, 3.1F, std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::quiet_NaN()}) {
    auto document = source;
    const auto before_revision = document.content_revision();
    const auto rejected = document.bridge_selected_edges(
        BridgeOptions{.steps = 3, .start_tension = invalid});
    EXPECT_FALSE(rejected.ok());
    EXPECT_EQ(document.content_revision(), before_revision);
    expect_document_equal(document, source);
    EXPECT_FALSE(document
                     .bridge_selected_edges(
                         BridgeOptions{.steps = 3, .end_tension = invalid})
                     .ok());
    expect_document_equal(document, source);
  }
  auto legacy = source;
  auto explicit_defaults = source;
  ASSERT_TRUE(legacy.bridge_selected_edges(4).ok());
  ASSERT_TRUE(
      explicit_defaults.bridge_selected_edges(BridgeOptions{.steps = 4}).ok());
  expect_document_equal(explicit_defaults, legacy);
}

TEST(
    ModelingSdkComponentOperationWorkflowTests,
    CrossObjectSingleSpanGuideUsesSurvivorReceiptAndPublishesShapeOnlyChanges) {
  ModelingSession session;
  const auto first =
      session.add_document(PolygonDocument::make_box({}).or_throw(), "Survivor")
          .or_throw();
  const auto second =
      session.add_document(PolygonDocument::make_box({}).or_throw(), "Source")
          .or_throw();
  ASSERT_TRUE(
      session
          .select_face(
              first, session.document_copy(first).or_throw().face_ids().front())
          .ok());
  ASSERT_TRUE(
      session
          .select_face(
              second,
              session.document_copy(second).or_throw().face_ids().front())
          .ok());
  session.clear_history();
  const Transform3 placement{.origin = {4.0F, 0.0F, 0.0F}};
  const auto candidate =
      session
          .prepare_bridge_documents(first, second, placement,
                                    SelectionKind::Face, BridgeOptions{})
          .or_throw();
  const auto alternative =
      session
          .prepare_bridge_documents(
              first, second, placement, SelectionKind::Face,
              BridgeOptions{.start_tension = 0.5F, .end_tension = 2.0F})
          .or_throw();
  expect_document_equal(alternative.survivor_document,
                        candidate.survivor_document);
  ASSERT_NE(alternative.receipt.bridge_shape, candidate.receipt.bridge_shape);
  SessionPreviewTransactionRequest request;
  request.participants.push_back(
      {.action = SessionPreviewAction::ReplaceExisting,
       .object = first,
       .document = candidate.survivor_document,
       .receipt = candidate.receipt});
  request.participants.push_back(
      {.action = SessionPreviewAction::RemoveExisting, .object = second});
  const auto preview =
      session.begin_preview_transaction("modeling.bridge", std::move(request))
          .or_throw()
          .preview_id;
  EXPECT_EQ(session.preview_summary().object_id, first);
  EXPECT_EQ(session.preview_bridge_shape(), candidate.receipt.bridge_shape);
  ASSERT_TRUE(session.preview_payload(preview).ok());
  const auto builds = session.preview_work_stats().mesh_payload_build_count;
  const auto revision = session.preview_summary().content_revision;
  const auto updated = session.update_preview_transaction(
      preview, {{.object = first,
                 .document = alternative.survivor_document,
                 .receipt = alternative.receipt}});
  ASSERT_TRUE(updated.ok()) << updated.error_message();
  EXPECT_TRUE(updated.value().changed);
  EXPECT_GT(session.preview_summary().content_revision, revision);
  EXPECT_EQ(session.preview_bridge_shape(), alternative.receipt.bridge_shape);
  ASSERT_TRUE(session.preview_payload(preview).ok());
  EXPECT_EQ(session.preview_work_stats().mesh_payload_build_count, builds);
  EXPECT_EQ(session.objects().size(), 2U);
  EXPECT_EQ(session.undo_depth(), 0U);
  ASSERT_TRUE(session.cancel_preview(preview).ok());
  EXPECT_FALSE(session.preview_bridge_shape().has_value());
  EXPECT_EQ(session.objects().size(), 2U);
}

TEST(ModelingSdkComponentOperationWorkflowTests,
     PublicBridgeDescriptorIncludesIndependentTensionContract) {
  const auto *descriptor = operation_descriptor(OperationKind::Bridge);
  ASSERT_NE(descriptor, nullptr);
  EXPECT_EQ(descriptor, operation_descriptor("bridge"));
  ASSERT_EQ(descriptor->parameter_count, 3U);
  const BridgeOptions defaults;
  EXPECT_EQ(descriptor->parameters[0].id, "steps");
  EXPECT_EQ(descriptor->parameters[0].kind, OperationParameterKind::Integer);
  EXPECT_DOUBLE_EQ(descriptor->parameters[0].default_value, defaults.steps);
  const std::array<std::string_view, 2> tension_ids{"start_tension",
                                                    "end_tension"};
  const std::array<float, 2> tension_defaults{defaults.start_tension,
                                              defaults.end_tension};
  for (std::size_t index = 0; index < tension_ids.size(); ++index) {
    const auto &parameter = descriptor->parameters[index + 1U];
    EXPECT_EQ(parameter.id, tension_ids[index]);
    EXPECT_EQ(parameter.kind, OperationParameterKind::Floating);
    EXPECT_DOUBLE_EQ(parameter.default_value, tension_defaults[index]);
    EXPECT_DOUBLE_EQ(parameter.minimum, 0.0);
    EXPECT_DOUBLE_EQ(parameter.maximum, 3.0);
    EXPECT_DOUBLE_EQ(parameter.step, 0.01);
    EXPECT_TRUE(parameter.visible);
    EXPECT_TRUE(parameter.hard_limits);
  }
}

TEST(ModelingSdkComponentOperationWorkflowTests,
     PublicBridgePreviewBeginAndUpdateForwardBothEndpointTensions) {
  const auto source = make_workflow_document(OperationKind::Bridge, false);
  auto api = ModelingApi::create();
  auto mesh = api.add_document(source, "Public bridge options");
  ASSERT_TRUE(mesh.valid());
  ASSERT_TRUE(mesh.select().replace().success);
  const BridgeOptions initial{
      .steps = 4, .start_tension = 0.5F, .end_tension = 2.0F};
  auto begun = api.checked().operations().begin_bridge_edges_preview(initial);
  ASSERT_TRUE(begun.ok()) << begun.error_message();
  auto preview = std::move(begun).value();
  ASSERT_TRUE(preview.active());
  const auto expect_preview = [&](BridgeOptions options) {
    auto candidate = source;
    ASSERT_TRUE(candidate.bridge_selected_edges(options).ok());
    const auto expected = candidate.compile_mesh().or_throw();
    const auto actual = preview.mesh_payload();
    ASSERT_EQ(actual.vertices.size(), expected.vertices.size());
    EXPECT_EQ(actual.indices, expected.indices);
    for (std::size_t index = 0; index < actual.vertices.size(); ++index) {
      EXPECT_FLOAT_EQ(actual.vertices[index].position.x,
                      expected.vertices[index].position.x);
      EXPECT_FLOAT_EQ(actual.vertices[index].position.y,
                      expected.vertices[index].position.y);
      EXPECT_FLOAT_EQ(actual.vertices[index].position.z,
                      expected.vertices[index].position.z);
    }
  };
  ASSERT_NO_FATAL_FAILURE(expect_preview(initial));
  const BridgeOptions edited{
      .steps = 4, .start_tension = 2.0F, .end_tension = 0.5F};
  const auto updated = preview.update(edited);
  ASSERT_TRUE(updated.success) << updated.error.message;
  EXPECT_TRUE(updated.changed);
  ASSERT_NO_FATAL_FAILURE(expect_preview(edited));
  ASSERT_TRUE(preview.cancel().success);
}

TEST(
    ModelingSdkComponentOperationWorkflowTests,
    BridgeGuideCacheRetainsAllocationHighWaterAndInvalidatesOnMaterialChanges) {
  const auto source = make_workflow_document(OperationKind::Bridge, false);
  ModelingSession session;
  const auto object = session.add_document(source).or_throw();
  auto candidate = source;
  const auto receipt =
      candidate.bridge_selected_edges(BridgeOptions{}).or_throw();
  const auto preview =
      session
          .begin_document_preview(object, "modeling.bridge", candidate, receipt)
          .or_throw();
  ASSERT_TRUE(session.preview_payload(preview).ok());
  const auto builds = session.preview_work_stats().mesh_payload_build_count;
  auto edited = source;
  auto edited_receipt =
      edited.bridge_selected_edges(BridgeOptions{.start_tension = 1.5F})
          .or_throw();
  auto topology = edited.authored_topology().or_throw();
  topology.next_vertex_id += 100U;
  topology.next_face_id += 100U;
  auto imported = PolygonDocument::from_authored_topology(topology).or_throw();
  ASSERT_TRUE(imported.apply_selection(edited.selection()).ok());
  ASSERT_TRUE(
      session.update_document_preview(preview, imported, edited_receipt).ok());
  ASSERT_TRUE(session.preview_payload(preview).ok());
  EXPECT_EQ(session.preview_work_stats().mesh_payload_build_count, builds);
  const auto accepted = session.preview_document_copy(preview, object)
                            .or_throw()
                            .authored_topology()
                            .or_throw();
  EXPECT_GE(accepted.next_vertex_id, topology.next_vertex_id);
  EXPECT_GE(accepted.next_face_id, topology.next_face_id);

  ASSERT_TRUE(
      imported.assign_face_material_slot(imported.face_ids().front(), 9U).ok());
  edited_receipt.dirty.materials = true;
  ASSERT_TRUE(
      session.update_document_preview(preview, imported, edited_receipt).ok());
  const auto material_payload = session.preview_payload(preview).or_throw();
  EXPECT_EQ(session.preview_work_stats().mesh_payload_build_count, builds + 1U);
  // Authored slots are zero-based; SDK typed material IDs reserve index zero.
  const MaterialId changed_material = make_id<MaterialTag>(10U);
  EXPECT_TRUE(std::ranges::any_of(
      material_payload.mesh.primitives,
      [changed_material](const auto &primitive) { return primitive.material == changed_material; }));
  ASSERT_TRUE(session.cancel_preview(preview).ok());
  const auto restored =
      session.document_copy(object).or_throw().authored_topology().or_throw();
  EXPECT_GE(restored.next_vertex_id, topology.next_vertex_id);
  EXPECT_GE(restored.next_face_id, topology.next_face_id);
  expect_document_equal(session.document_copy(object).or_throw(), source, true);
}

TEST(ModelingSdkComponentOperationWorkflowTests,
     CrossObjectGuideFollowsHigherIdSurvivorInsteadOfRemovedReceipt) {
  ModelingSession session;
  const auto document = make_workflow_document(OperationKind::Bridge, false);
  const auto source =
      session.add_document(document, "Lower-ID source").or_throw();
  const auto survivor =
      session.add_document(document, "Higher-ID survivor").or_throw();
  ASSERT_GT(survivor.index, source.index);
  ASSERT_TRUE(session.select_face(source, document.face_ids().front()).ok());
  ASSERT_TRUE(session.select_face(survivor, document.face_ids().front()).ok());
  const Transform3 placement{.x_axis = {0.0F, 2.0F, 0.0F},
                             .y_axis = {-1.0F, 0.0F, 0.0F},
                             .z_axis = {0.0F, 0.0F, 1.0F},
                             .origin = {4.0F, -3.0F, 2.0F}};
  const auto initial =
      session
          .prepare_bridge_documents(survivor, source, placement,
                                    SelectionKind::Face, BridgeOptions{})
          .or_throw();
  const auto edited =
      session
          .prepare_bridge_documents(
              survivor, source, placement, SelectionKind::Face,
              BridgeOptions{.start_tension = 1.8F, .end_tension = 0.25F})
          .or_throw();
  SessionPreviewTransactionRequest request;
  request.replace_object_selection = true;
  request.active_participant_index = 0U;
  request.participants.push_back(
      {.action = SessionPreviewAction::ReplaceExisting,
       .object = survivor,
       .document = initial.survivor_document,
       .receipt = initial.receipt,
       .selected = true});
  request.participants.push_back(
      {.action = SessionPreviewAction::RemoveExisting,
       .object = source,
       .receipt = initial.receipt});
  const auto preview =
      session.begin_preview_transaction("modeling.bridge", std::move(request))
          .or_throw()
          .preview_id;
  EXPECT_EQ(session.preview_summary().object_id, source);
  EXPECT_EQ(session.preview_bridge_shape(), initial.receipt.bridge_shape);
  const auto survivor_revision = [&]() -> std::uint64_t {
    for (const auto &object : session.preview_summary().objects) {
      if (object.object_id == survivor)
        return object.content_revision;
    }
    return 0U;
  };
  const auto revision = survivor_revision();
  ASSERT_GT(revision, 0U);
  const auto updated = session.update_preview_transaction(
      preview, {{.object = survivor,
                 .document = edited.survivor_document,
                 .receipt = edited.receipt}});
  ASSERT_TRUE(updated.ok()) << updated.error_message();
  EXPECT_GT(survivor_revision(), revision);
  const auto accepted_shape = session.preview_bridge_shape();
  EXPECT_EQ(accepted_shape, edited.receipt.bridge_shape);
  ASSERT_TRUE(accepted_shape.has_value());
  const auto &shape = *accepted_shape;
  EXPECT_FLOAT_EQ(shape.controls[0].x, 1.0F);
  EXPECT_FLOAT_EQ(shape.controls[0].y, 1.0F);
  EXPECT_FLOAT_EQ(shape.controls[0].z, 0.0F);
  EXPECT_FLOAT_EQ(shape.controls[3].x, 3.0F);
  EXPECT_FLOAT_EQ(shape.controls[3].y, -1.0F);
  EXPECT_FLOAT_EQ(shape.controls[3].z, 2.0F);
  EXPECT_FLOAT_EQ(shape.start_tension, 1.8F);
  EXPECT_FLOAT_EQ(shape.end_tension, 0.25F);
  EXPECT_EQ(session.preview_summary().object_id, source);
  ASSERT_TRUE(session.cancel_preview(preview).ok());
}

} // namespace
