////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <mesh/polygon/internal/quader_poly_document_topology_backend.hpp>

#include <mesh/polygon/internal/quader_poly_document_hull_plane_helpers.hpp>
#include <mesh/polygon/internal/quader_poly_document_topology_helpers.hpp>

#if defined(QUADER_POLY_USE_OPENMESH)
#include <OpenMesh/Core/Mesh/PolyMesh_ArrayKernelT.hh>
#endif

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace quader_poly::document_internal {

#if defined(QUADER_POLY_USE_OPENMESH)
/**
 * Represents an Open Mesh Topology Traits value used by the polygon document and mesh editing core.
 */
struct OpenMeshTopologyTraits : public OpenMesh::DefaultTraits {
  VertexAttributes(OpenMesh::Attributes::Status);
  HalfedgeAttributes(OpenMesh::Attributes::Status);
  EdgeAttributes(OpenMesh::Attributes::Status);
  FaceAttributes(OpenMesh::Attributes::Status);
};

using OpenMeshTopologyMesh =
    OpenMesh::PolyMesh_ArrayKernelT<OpenMeshTopologyTraits>;
using OpenMeshVertexIdProperty = OpenMesh::VPropHandleT<ElementId>;
using OpenMeshFaceIdProperty = OpenMesh::FPropHandleT<ElementId>;
using OpenMeshFaceIndexProperty = OpenMesh::FPropHandleT<std::size_t>;
using OpenMeshFaceMaterialProperty = OpenMesh::FPropHandleT<std::uint32_t>;

/**
 * Represents an Open Mesh Topology Document value used by the polygon document and mesh editing core.
 */
struct OpenMeshTopologyDocument {
    OpenMeshTopologyMesh mesh;
    OpenMeshVertexIdProperty vertex_ids;
    OpenMeshFaceIdProperty face_ids;
    OpenMeshFaceIndexProperty face_indices;
    OpenMeshFaceMaterialProperty face_materials;
    std::map<ElementId, OpenMeshTopologyMesh::VertexHandle> vertices_by_id;
};

OpenMeshTopologyMesh::Point openmesh_point_from_vec3(const quader::QVec3& position)
{
    return { position.x, position.y, position.z };
}

quader::QVec3 vec3_from_openmesh_point(const OpenMeshTopologyMesh::Point& point)
{
    return { point[0], point[1], point[2] };
}

bool build_openmesh_topology_document(const Document& document, OpenMeshTopologyDocument& topology, std::string& message)
{
    topology.mesh.request_vertex_status();
    topology.mesh.request_edge_status();
    topology.mesh.request_face_status();
    topology.mesh.add_property(topology.vertex_ids, "qdr:vertex_id");
    topology.mesh.add_property(topology.face_ids, "qdr:face_id");
    topology.mesh.add_property(topology.face_indices, "qdr:face_index");
    topology.mesh.add_property(topology.face_materials, "qdr:face_material");

    for (const Vertex& vertex : document.vertices) {
      if (vertex.id == kInvalidElementId ||
          topology.vertices_by_id.contains(vertex.id)) {
        message = "Current document has duplicate or invalid vertices.";
        return false;
      }
        const OpenMeshTopologyMesh::VertexHandle handle = topology.mesh.add_vertex(openmesh_point_from_vec3(vertex.position));
        topology.mesh.property(topology.vertex_ids, handle) = vertex.id;
        topology.vertices_by_id.emplace(vertex.id, handle);
    }

    for (std::size_t face_index = 0; face_index < document.faces.size(); ++face_index) {
        const Face& face = document.faces[face_index];
        if (face.id == kInvalidElementId || face.vertices.size() < 3 ||
            has_repeated_vertex(face.vertices)) {
          message = "Current document has invalid polygon faces.";
          return false;
        }

        std::vector<OpenMeshTopologyMesh::VertexHandle> handles;
        handles.reserve(face.vertices.size());
        for (const ElementId vertex_id : face.vertices) {
            const auto found = topology.vertices_by_id.find(vertex_id);
            if (found == topology.vertices_by_id.end()) {
                message = "Current document has faces that reference missing vertices.";
                return false;
            }
            handles.push_back(found->second);
        }

        try {
            const OpenMeshTopologyMesh::FaceHandle handle = topology.mesh.add_face(handles);
            if (!handle.is_valid()) {
                message = "Current document could not be represented as a halfedge topology mesh.";
                return false;
            }
            topology.mesh.property(topology.face_ids, handle) = face.id;
            topology.mesh.property(topology.face_indices, handle) = face_index;
            topology.mesh.property(topology.face_materials, handle) = face.material_slot;
        } catch (const std::exception& exception) {
            message = exception.what();
            return false;
        }
    }

    return true;
}

Document document_from_openmesh_topology(OpenMeshTopologyDocument& topology, ElementId next_vertex_id, ElementId next_face_id)
{
    topology.mesh.garbage_collection();

    Document document;
    document.next_vertex_id = next_vertex_id;
    document.next_face_id = next_face_id;

    for (const OpenMeshTopologyMesh::VertexHandle vertex_handle : topology.mesh.vertices()) {
        Vertex vertex;
        vertex.id = topology.mesh.property(topology.vertex_ids, vertex_handle);
        vertex.position = vec3_from_openmesh_point(topology.mesh.point(vertex_handle));
        document.vertices.push_back(vertex);
    }

    for (const OpenMeshTopologyMesh::FaceHandle face_handle : topology.mesh.faces()) {
        Face face;
        face.id = topology.mesh.property(topology.face_ids, face_handle);
        if (face.id == kInvalidElementId) {
          face.id = next_valid_face_id(document);
        }
        face.material_slot = topology.mesh.property(topology.face_materials, face_handle);
        for (const OpenMeshTopologyMesh::VertexHandle vertex_handle : topology.mesh.fv_range(face_handle)) {
            const ElementId vertex_id = topology.mesh.property(topology.vertex_ids, vertex_handle);
            if (vertex_id != kInvalidElementId) {
              face.vertices.push_back(vertex_id);
            }
        }
        face.uvs.clear();
        if (face.vertices.size() >= 3 && !has_repeated_vertex(face.vertices)) {
            document.faces.push_back(std::move(face));
        }
    }

    return document;
}

std::size_t face_index_for_handle(const OpenMeshTopologyDocument& topology, OpenMeshTopologyMesh::FaceHandle face_handle)
{
    if (!face_handle.is_valid()) {
        return std::numeric_limits<std::size_t>::max();
    }
    return topology.mesh.property(topology.face_indices, face_handle);
}

ElementId vertex_id_for_handle(const OpenMeshTopologyDocument& topology, OpenMeshTopologyMesh::VertexHandle vertex_handle)
{
    if (!vertex_handle.is_valid()) {
      return kInvalidElementId;
    }
    return topology.mesh.property(topology.vertex_ids, vertex_handle);
}

Edge edge_for_halfedge(const OpenMeshTopologyDocument& topology, OpenMeshTopologyMesh::HalfedgeHandle halfedge_handle)
{
    if (!halfedge_handle.is_valid()) {
      return {kInvalidElementId, kInvalidElementId};
    }
    const ElementId from_id = vertex_id_for_handle(topology, topology.mesh.from_vertex_handle(halfedge_handle));
    const ElementId to_id = vertex_id_for_handle(topology, topology.mesh.to_vertex_handle(halfedge_handle));
    return make_edge(from_id, to_id);
}

Edge edge_for_edge_handle(const OpenMeshTopologyDocument& topology, OpenMeshTopologyMesh::EdgeHandle edge_handle)
{
    const OpenMeshTopologyMesh::HalfedgeHandle halfedge_handle = topology.mesh.halfedge_handle(edge_handle, 0);
    if (!halfedge_handle.is_valid()) {
      return {kInvalidElementId, kInvalidElementId};
    }
    return edge_for_halfedge(topology, halfedge_handle);
}

void sort_unique(std::vector<std::size_t>& values)
{
    std::ranges::sort(values);
    const auto unique_tail = std::ranges::unique(values);
    values.erase(unique_tail.begin(), unique_tail.end());
}

void sort_unique(std::vector<ElementId>& values)
{
    std::ranges::sort(values);
    const auto unique_tail = std::ranges::unique(values);
    values.erase(unique_tail.begin(), unique_tail.end());
}

void sort_unique_edges(std::vector<Edge>& edges)
{
    std::ranges::sort(edges, [](const Edge& left, const Edge& right) {
        return std::tie(left.a, left.b) < std::tie(right.a, right.b);
    });
    const auto unique_tail = std::ranges::unique(edges);
    edges.erase(unique_tail.begin(), unique_tail.end());
}

bool build_topology_query(const Document& document, DocumentTopologyQuery& query, std::string& message)
{
    query = {};

    OpenMeshTopologyDocument topology;
    if (!build_openmesh_topology_document(document, topology, message)) {
        return false;
    }

    for (const OpenMeshTopologyMesh::EdgeHandle edge_handle : topology.mesh.edges()) {
        const Edge edge = edge_for_edge_handle(topology, edge_handle);
        if (edge.a == kInvalidElementId || edge.b == kInvalidElementId ||
            edge.a == edge.b) {
          continue;
        }

        const std::pair<ElementId, ElementId> key { edge.a, edge.b };
        std::vector<std::size_t> face_indices;
        int incidence_count = 0;

        const OpenMeshTopologyMesh::HalfedgeHandle halfedges[2] = {
            topology.mesh.halfedge_handle(edge_handle, 0),
            topology.mesh.halfedge_handle(edge_handle, 1),
        };
        for (const OpenMeshTopologyMesh::HalfedgeHandle halfedge_handle : halfedges) {
            if (!halfedge_handle.is_valid() || topology.mesh.is_boundary(halfedge_handle)) {
                continue;
            }

            const OpenMeshTopologyMesh::FaceHandle face_handle = topology.mesh.face_handle(halfedge_handle);
            const std::size_t face_index = face_index_for_handle(topology, face_handle);
            if (face_index == std::numeric_limits<std::size_t>::max() || face_index >= document.faces.size()) {
                continue;
            }

            ++incidence_count;
            face_indices.push_back(face_index);
        }

        sort_unique(face_indices);
        query.edge_incidence_counts[key] = incidence_count;
        query.face_indices_by_edge[key] = std::move(face_indices);

        if (incidence_count == 1) {
            query.boundary_edges.push_back(edge);
        } else if (incidence_count == 2) {
            query.closed_edges.push_back(edge);
        } else if (incidence_count > 2) {
            query.nonmanifold_edges.push_back(edge);
        }
    }

    for (const OpenMeshTopologyMesh::VertexHandle vertex_handle : topology.mesh.vertices()) {
        const ElementId vertex_id = vertex_id_for_handle(topology, vertex_handle);
        if (vertex_id == kInvalidElementId) {
          continue;
        }

        std::vector<ElementId> adjacent_vertices;
        for (const OpenMeshTopologyMesh::VertexHandle adjacent_handle : topology.mesh.vv_range(vertex_handle)) {
            const ElementId adjacent_id = vertex_id_for_handle(topology, adjacent_handle);
            if (adjacent_id != kInvalidElementId && adjacent_id != vertex_id) {
              adjacent_vertices.push_back(adjacent_id);
            }
        }
        sort_unique(adjacent_vertices);
        query.adjacent_vertices_by_vertex[vertex_id] = std::move(adjacent_vertices);

        std::vector<std::size_t> face_indices;
        for (const OpenMeshTopologyMesh::FaceHandle face_handle : topology.mesh.vf_range(vertex_handle)) {
            const std::size_t face_index = face_index_for_handle(topology, face_handle);
            if (face_index != std::numeric_limits<std::size_t>::max() && face_index < document.faces.size()) {
                face_indices.push_back(face_index);
            }
        }
        sort_unique(face_indices);
        query.face_indices_by_vertex[vertex_id] = std::move(face_indices);
    }

    sort_unique_edges(query.boundary_edges);
    sort_unique_edges(query.closed_edges);
    sort_unique_edges(query.nonmanifold_edges);

    return true;
}

OperationResult merge_selected_vertices_to_active_with_topology_backend(
    Document& document,
    Selection& selection,
    const std::set<ElementId>& merge_vertex_ids,
    ElementId active_vertex_id)
{
    Document candidate;
    const Vertex* active_vertex = find_vertex(document, active_vertex_id);
    if (active_vertex == nullptr) {
        return { false, "Active vertex was not found." };
    }

    const OperationResult candidate_result = build_vertex_merge_candidate(document, candidate, merge_vertex_ids, active_vertex_id, active_vertex->position);
    if (!candidate_result.changed) {
        return candidate_result;
    }

    document = std::move(candidate);
    prune_unused_vertices(document);

    selection.clear();
    selection.mode = SelectionMode::Vertex;
    selection.vertices.push_back(active_vertex_id);
    activate_vertex_selection(selection, active_vertex_id);

    return { true, {} };
}
#else
bool build_topology_query(const Document&, DocumentTopologyQuery& query, std::string& message)
{
    query = {};
    message = "OpenMesh topology support is disabled.";
    return false;
}

OperationResult merge_selected_vertices_to_active_with_topology_backend(
    Document&,
    Selection&,
    const std::set<ElementId>&,
    ElementId)
{
    return { false, "OpenMesh topology support is disabled." };
}
#endif

} // namespace quader_poly::document_internal
