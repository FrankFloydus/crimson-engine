////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/neutral/polygon/poly_operation.hpp>
#include <quader/modeling/neutral/polygon/poly_operation_descriptor.hpp>

#include <quader/modeling/neutral/polygon/internal/quader_poly_document_bridge_surface_helpers.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_hull_plane_helpers.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_topology_helpers.hpp>

#include <algorithm>
#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace quader_poly {

using namespace document_internal;

namespace {

using SplitEdgeKey = std::pair<ElementId, ElementId>;

struct SplitEdgeUse {
    std::size_t face_index = 0U;
    ElementId from = kInvalidElementId;
    ElementId to = kInvalidElementId;
};

struct SplitSourceTopology {
    std::map<SplitEdgeKey, std::vector<SplitEdgeUse>> edge_uses;
    std::map<ElementId, std::vector<std::size_t>> incident_faces;
};

struct SplitChain {
    std::vector<ElementId> vertices;
    bool closed = false;
};

struct SplitVertexFan {
    std::map<std::size_t,
             std::vector<std::pair<SplitEdgeKey, std::size_t>>> neighbors;
    bool closed = false;
};

SplitEdgeKey split_edge_key(ElementId first, ElementId second)
{
    const Edge edge = make_edge(first, second);
    return {edge.a, edge.b};
}

SplitSourceTopology split_source_topology(const Document& document)
{
    SplitSourceTopology topology;
    for (std::size_t face_index = 0U; face_index < document.faces.size();
         ++face_index) {
        const Face& face = document.faces[face_index];
        for (std::size_t corner = 0U; corner < face.vertices.size(); ++corner) {
            const ElementId from = face.vertices[corner];
            const ElementId to =
                face.vertices[(corner + 1U) % face.vertices.size()];
            topology.edge_uses[split_edge_key(from, to)].push_back(
                {face_index, from, to});
            topology.incident_faces[from].push_back(face_index);
        }
    }
    return topology;
}

std::optional<std::vector<SplitChain>> ordered_split_chains(
    const std::vector<Edge>& edges)
{
    std::map<ElementId, std::vector<ElementId>> neighbors;
    for (const Edge edge : edges) {
        neighbors[edge.a].push_back(edge.b);
        neighbors[edge.b].push_back(edge.a);
    }
    for (auto& [vertex_id, adjacent] : neighbors) {
        static_cast<void>(vertex_id);
        std::ranges::sort(adjacent);
        if (adjacent.size() > 2U) {
            return std::nullopt;
        }
    }

    std::vector<SplitChain> chains;
    std::set<ElementId> remaining;
    for (const auto& [vertex_id, adjacent] : neighbors) {
        static_cast<void>(adjacent);
        remaining.insert(vertex_id);
    }
    while (!remaining.empty()) {
        std::set<ElementId> component;
        std::vector<ElementId> pending{*remaining.begin()};
        while (!pending.empty()) {
            const ElementId vertex_id = pending.back();
            pending.pop_back();
            if (!component.insert(vertex_id).second) {
                continue;
            }
            for (const ElementId adjacent : neighbors.at(vertex_id)) {
                pending.push_back(adjacent);
            }
        }
        std::vector<ElementId> endpoints;
        for (const ElementId vertex_id : component) {
            remaining.erase(vertex_id);
            if (neighbors.at(vertex_id).size() == 1U) {
                endpoints.push_back(vertex_id);
            }
        }
        if (!endpoints.empty() && endpoints.size() != 2U) {
            return std::nullopt;
        }

        SplitChain chain;
        chain.closed = endpoints.empty();
        ElementId current =
            chain.closed ? *component.begin() : endpoints.front();
        ElementId previous = kInvalidElementId;
        do {
            chain.vertices.push_back(current);
            const std::vector<ElementId>& adjacent = neighbors.at(current);
            const auto next = std::ranges::find_if(
                adjacent, [previous](ElementId candidate) {
                    return candidate != previous;
                });
            if (next == adjacent.end()) {
                break;
            }
            previous = current;
            current = *next;
        } while (current != chain.vertices.front());
        if (chain.vertices.size() != component.size() ||
            (chain.closed && chain.vertices.size() < 3U)) {
            return std::nullopt;
        }
        chains.push_back(std::move(chain));
    }
    return chains;
}

std::optional<std::size_t> split_left_face(
    const SplitSourceTopology& topology, ElementId from, ElementId to)
{
    const auto found = topology.edge_uses.find(split_edge_key(from, to));
    if (found == topology.edge_uses.end() || found->second.size() != 2U) {
        return std::nullopt;
    }
    for (const SplitEdgeUse& use : found->second) {
        if (use.from == from && use.to == to) {
            return use.face_index;
        }
    }
    return std::nullopt;
}

std::vector<std::vector<std::size_t>> split_fan_components(
    const SplitVertexFan& fan, const std::set<SplitEdgeKey>& cuts)
{
    std::vector<std::vector<std::size_t>> components;
    std::set<std::size_t> visited;
    for (const auto& [face_index, neighbors] : fan.neighbors) {
        static_cast<void>(neighbors);
        if (visited.contains(face_index)) {
            continue;
        }
        std::vector<std::size_t> component;
        std::vector<std::size_t> pending{face_index};
        while (!pending.empty()) {
            const std::size_t current = pending.back();
            pending.pop_back();
            if (!visited.insert(current).second) {
                continue;
            }
            component.push_back(current);
            for (const auto& [edge_key, adjacent] : fan.neighbors.at(current)) {
                if (!cuts.contains(edge_key)) {
                    pending.push_back(adjacent);
                }
            }
        }
        components.push_back(std::move(component));
    }
    return components;
}

std::optional<SplitVertexFan> split_vertex_fan(
    const Document& document, const SplitSourceTopology& topology,
    ElementId vertex_id)
{
    SplitVertexFan fan;
    std::set<SplitEdgeKey> radial_edges;
    for (const std::size_t face_index : topology.incident_faces.at(vertex_id)) {
        const Face& face = document.faces[face_index];
        const auto corner = std::ranges::find(face.vertices, vertex_id);
        const std::size_t index =
            static_cast<std::size_t>(corner - face.vertices.begin());
        radial_edges.insert(split_edge_key(
            vertex_id, face.vertices[(index + 1U) % face.vertices.size()]));
        radial_edges.insert(split_edge_key(
            vertex_id, face.vertices[(index + face.vertices.size() - 1U) %
                                     face.vertices.size()]));
        fan.neighbors[face_index];
    }

    std::size_t boundary_count = 0U;
    for (const SplitEdgeKey& edge_key : radial_edges) {
        const std::vector<SplitEdgeUse>& uses = topology.edge_uses.at(edge_key);
        if (uses.size() == 1U) {
            ++boundary_count;
            continue;
        }
        if (uses.size() != 2U || uses[0].from != uses[1].to ||
            uses[0].to != uses[1].from) {
            return std::nullopt;
        }
        fan.neighbors.at(uses[0].face_index).push_back(
            {edge_key, uses[1].face_index});
        fan.neighbors.at(uses[1].face_index).push_back(
            {edge_key, uses[0].face_index});
    }
    if ((boundary_count != 0U && boundary_count != 2U) ||
        split_fan_components(fan, {}).size() != 1U) {
        return std::nullopt;
    }
    fan.closed = boundary_count == 0U;
    return fan;
}

std::optional<std::vector<std::size_t>> split_chain_vertex_side(
    const Document& document, const SplitSourceTopology& topology,
    const SplitVertexFan& fan, const SplitChain& chain, std::size_t index)
{
    const ElementId vertex_id = chain.vertices[index];
    const bool has_previous = chain.closed || index > 0U;
    const bool has_next = chain.closed || index + 1U < chain.vertices.size();
    std::set<SplitEdgeKey> cuts;
    std::optional<std::size_t> incoming_face;
    std::optional<std::size_t> outgoing_face;
    if (has_previous) {
        const ElementId previous = chain.vertices[
            (index + chain.vertices.size() - 1U) % chain.vertices.size()];
        incoming_face = split_left_face(topology, previous, vertex_id);
        cuts.insert(split_edge_key(previous, vertex_id));
        if (!incoming_face) {
            return std::nullopt;
        }
    }
    if (has_next) {
        const ElementId next =
            chain.vertices[(index + 1U) % chain.vertices.size()];
        outgoing_face = split_left_face(topology, vertex_id, next);
        cuts.insert(split_edge_key(vertex_id, next));
        if (!outgoing_face) {
            return std::nullopt;
        }
    }

    const std::size_t seed = outgoing_face ? *outgoing_face : *incoming_face;
    if (fan.closed && (!has_previous || !has_next)) {
        // An open chain ending inside a closed fan needs a terminal cut.
        // Extend through the neighboring boundary of its oriented side face.
        const Face& face = document.faces[seed];
        const std::size_t corner = static_cast<std::size_t>(
            std::ranges::find(face.vertices, vertex_id) - face.vertices.begin());
        const std::size_t neighbor = !has_previous
            ? (corner + face.vertices.size() - 1U) % face.vertices.size()
            : (corner + 1U) % face.vertices.size();
        cuts.insert(split_edge_key(vertex_id, face.vertices[neighbor]));
    }

    const std::vector<std::vector<std::size_t>> components =
        split_fan_components(fan, cuts);
    if (components.size() != 2U) {
        return std::nullopt;
    }
    for (const std::vector<std::size_t>& component : components) {
        if (std::ranges::find(component, seed) == component.end()) {
            continue;
        }
        if (incoming_face &&
            std::ranges::find(component, *incoming_face) == component.end()) {
            return std::nullopt;
        }
        return component;
    }
    return std::nullopt;
}

class SplitEdgesOperation final : public PolyOperation {
public:
    [[nodiscard]] std::string_view id() const override
    {
        return poly_operation_descriptor(PolyOperationKey::SplitSelectedEdges).id;
    }
    [[nodiscard]] std::string_view label() const override
    {
        return poly_operation_descriptor(PolyOperationKey::SplitSelectedEdges).label;
    }
    [[nodiscard]] OperationResult apply(Document& document,
                                        Selection& selection) const override;
};

OperationResult SplitEdgesOperation::apply(Document& document,
                                           Selection& selection) const
{
    if (selection.mode != SelectionMode::Edge || selection.edges.empty()) {
        return OperationResult::failure("Select one or more edges to disconnect.");
    }
    const OperationResult source_validation =
        validate_authored_operation_candidate(document, document);
    if (!source_validation.success) {
        return source_validation;
    }
    const std::vector<Edge> selected_edges = selected_valid_edges(document, selection);
    if (selected_edges.empty()) {
        return OperationResult::failure("Select one or more valid edges to disconnect.");
    }
    const SplitSourceTopology topology = split_source_topology(document);
    for (const Edge edge : selected_edges) {
        const auto found = topology.edge_uses.find(split_edge_key(edge.a, edge.b));
        if (found == topology.edge_uses.end() || found->second.size() != 2U) {
            return OperationResult::failure(
                "Disconnect Edge needs selected interior edges with exactly two adjacent faces.");
        }
    }
    std::optional<std::vector<SplitChain>> chains =
        ordered_split_chains(selected_edges);
    if (!chains) {
        return OperationResult::failure(
            "Disconnect Edge needs nonbranching edge chains or loops.");
    }

    std::map<ElementId, std::vector<std::size_t>> split_faces_by_vertex;
    for (SplitChain& chain : *chains) {
        // Retain the first adjacent face as the copied side for a single edge,
        // and use that same deterministic orientation for an entire chain.
        const ElementId first = chain.vertices[0];
        const ElementId second = chain.vertices[1];
        const SplitEdgeUse& first_use =
            topology.edge_uses.at(split_edge_key(first, second)).front();
        if (first_use.from != first) {
            std::ranges::reverse(chain.vertices);
        }
        for (std::size_t index = 0U; index < chain.vertices.size(); ++index) {
            const ElementId vertex_id = chain.vertices[index];
            const std::optional<SplitVertexFan> fan =
                split_vertex_fan(document, topology, vertex_id);
            if (!fan) {
                return OperationResult::failure(
                    "Disconnect Edge needs a connected manifold face fan at each selected vertex.");
            }
            std::optional<std::vector<std::size_t>> side =
                split_chain_vertex_side(document, topology, *fan, chain, index);
            if (!side) {
                return OperationResult::failure(
                    "Disconnect Edge could not separate the ordered chain sides at a selected vertex.");
            }
            split_faces_by_vertex.emplace(vertex_id, std::move(*side));
        }
    }

    Document candidate = document;
    OperationResult result;
    std::map<ElementId, ElementId> duplicate_vertices;
    for (const auto& [vertex_id, face_indices] : split_faces_by_vertex) {
        const Vertex* source_vertex = find_vertex(document, vertex_id);
        if (source_vertex == nullptr) {
            return OperationResult::failure("Disconnect Edge could not resolve a selected vertex.");
        }
        const ElementId duplicate_id = add_vertex(candidate, source_vertex->position);
        duplicate_vertices.emplace(vertex_id, duplicate_id);
        result.created.vertices.push_back(duplicate_id);
        for (const std::size_t face_index : face_indices) {
            Face& face = candidate.faces[face_index];
            std::ranges::replace(face.vertices, vertex_id, duplicate_id);
            add_unique_id(result.affected.faces, face.id);
        }
    }

    std::vector<Edge> seam_edges;
    for (const Edge edge : selected_edges) {
        const Edge duplicate_edge = make_edge(
            duplicate_vertices.at(edge.a), duplicate_vertices.at(edge.b));
        add_unique_edge(seam_edges, edge);
        add_unique_edge(seam_edges, duplicate_edge);
        add_unique_edge(result.created.edges, duplicate_edge);
        add_unique_edge(result.affected.edges, edge);
    }
    std::vector<EdgeNormalModeCopy> normal_mode_copies;
    for (const ElementId face_id : result.affected.faces) {
        const Face& source_face = *find_face(document, face_id);
        const Face& target_face = *find_face(candidate, face_id);
        for (std::size_t corner = 0U; corner < source_face.vertices.size();
             ++corner) {
            const std::size_t next =
                (corner + 1U) % source_face.vertices.size();
            normal_mode_copies.push_back({
                make_edge(source_face.vertices[corner], source_face.vertices[next]),
                make_edge(target_face.vertices[corner], target_face.vertices[next]),
            });
        }
    }
    static_cast<void>(copy_edge_normal_modes(document, candidate, normal_mode_copies));
    prune_normal_edge_states(candidate);
    const OperationResult candidate_validation =
        validate_authored_operation_candidate(document, candidate);
    if (!candidate_validation.success) {
        return candidate_validation;
    }

    document = std::move(candidate);
    selection.clear();
    selection.mode = SelectionMode::Edge;
    selection.edges = std::move(seam_edges);
    activate_last_selection(selection);
    result.changed = true;
    return result;
}

} // namespace

OperationResult split_selected_edges(Document& document, Selection& selection)
{
    return SplitEdgesOperation().apply(document, selection);
}

} // namespace quader_poly
