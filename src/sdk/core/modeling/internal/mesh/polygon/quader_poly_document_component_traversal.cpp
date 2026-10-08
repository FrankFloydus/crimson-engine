////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/neutral/polygon/document_picking.hpp>
#include <quader/modeling/neutral/polygon/document_topology.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <map>
#include <optional>
#include <ranges>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quader_poly {
namespace {

/// Orders canonical edges independently of authored face and vertex storage.
struct EdgeLess {
    bool operator()(Edge left, Edge right) const {
        return left.a < right.a || (left.a == right.a && left.b < right.b);
    }
};

template <typename Value>
void append_unique(std::vector<Value>& values, const Value& value) {
    if (std::ranges::find(values, value) == values.end()) {
        values.push_back(value);
    }
}

bool is_edge(Edge edge) {
    return edge.a != kInvalidElementId && edge.b != kInvalidElementId && edge.a != edge.b;
}

bool face_contains_edge(const Face& face, Edge edge) {
    for (std::size_t corner = 0U; corner < face.vertices.size(); ++corner) {
        if (make_edge(face.vertices[corner],
                      face.vertices[(corner + 1U) % face.vertices.size()]) == edge) {
            return true;
        }
    }
    return false;
}

bool unique_live_vertex(const Document& document, ElementId id) {
    return id != kInvalidElementId &&
           std::ranges::count(document.vertices, id, &Vertex::id) == 1;
}

bool eligible_face(const Document& document, const Face& face) {
    if (face.id == kInvalidElementId || face.vertices.size() < 3U ||
        std::ranges::count(document.faces, face.id, &Face::id) != 1) {
        return false;
    }
    std::unordered_map<ElementId, std::size_t> corner_counts;
    corner_counts.reserve(face.vertices.size());
    for (const ElementId vertex : face.vertices) {
        if (vertex == kInvalidElementId || !corner_counts.emplace(vertex, 0U).second) {
            return false;
        }
    }
    for (const Vertex& vertex : document.vertices) {
        const auto found = corner_counts.find(vertex.id);
        if (found != corner_counts.end()) {
            ++found->second;
        }
    }
    return std::ranges::all_of(corner_counts, [](const auto& entry) {
        return entry.second == 1U;
    });
}

/// Retains raw incidence even when a face cannot supply a traversal relation.
struct IndexedEdge {
    Edge edge;
    std::vector<std::size_t> faces;
};

/// Owns a face's stable identity and its canonical perimeter edge indices.
struct IndexedFace {
    const Face* source = nullptr;
    bool eligible = false;
    std::vector<std::size_t> edges;
};

/// Describes one vertex's authored edge fan without geometric angle tests.
struct IndexedVertex {
    std::size_t occurrences = 0U;
    bool eligible_faces = true;
    std::vector<std::size_t> edges;
    std::vector<std::pair<std::size_t, std::size_t>> corners;
};

/// Builds a query-local, immutable index over authored component identities.
class TraversalTopology {
public:
    explicit TraversalTopology(const Document& document) {
        for (const Vertex& vertex : document.vertices) {
            ++vertices_[vertex.id].occurrences;
        }
        std::map<ElementId, std::size_t> face_counts;
        for (const Face& face : document.faces) {
            ++face_counts[face.id];
        }

        // Allocate edge indices in stable-ID order, including edges of invalid
        // faces. Dropping those faces would manufacture a traversable boundary.
        std::map<Edge, std::vector<std::size_t>, EdgeLess> incidence;
        for (const Face& face : document.faces) {
            const std::size_t face_index = faces_.size();
            bool eligible = face.id != kInvalidElementId &&
                            face_counts[face.id] == 1U && face.vertices.size() >= 3U;
            std::unordered_set<ElementId> corners;
            corners.reserve(face.vertices.size());
            for (const ElementId vertex : face.vertices) {
                eligible = eligible && live_vertex(vertex) &&
                           corners.insert(vertex).second;
            }
            if (!eligible) {
                for (const ElementId vertex : face.vertices) {
                    if (live_vertex(vertex)) {
                        vertices_[vertex].eligible_faces = false;
                    }
                }
            }
            faces_.push_back({ &face, eligible, {} });
            if (face_counts[face.id] == 1U && face.id != kInvalidElementId) {
                face_indices_.emplace(face.id, face_index);
            }
            for (std::size_t corner = 0U; corner < face.vertices.size(); ++corner) {
                const Edge edge = make_edge(face.vertices[corner],
                    face.vertices[(corner + 1U) % face.vertices.size()]);
                if (is_edge(edge) && live_vertex(edge.a) && live_vertex(edge.b)) {
                    auto& incident_faces = incidence[edge];
                    if (incident_faces.empty() || incident_faces.back() != face_index) {
                        incident_faces.push_back(face_index);
                    }
                }
            }
        }
        for (auto& [edge, incident_faces] : incidence) {
            const std::size_t edge_index = edges_.size();
            std::ranges::sort(incident_faces, [&](std::size_t left, std::size_t right) {
                return faces_[left].source->id < faces_[right].source->id;
            });
            edge_indices_.emplace(edge, edge_index);
            edges_.push_back({ edge, std::move(incident_faces) });
            vertices_[edge.a].edges.push_back(edge_index);
            vertices_[edge.b].edges.push_back(edge_index);
        }
        for (IndexedFace& face : faces_) {
            if (!face.eligible) {
                continue;
            }
            for (std::size_t corner = 0U; corner < face.source->vertices.size(); ++corner) {
                const Edge edge = make_edge(face.source->vertices[corner],
                    face.source->vertices[(corner + 1U) % face.source->vertices.size()]);
                face.edges.push_back(edge_indices_.at(edge));
            }
            for (std::size_t corner = 0U; corner < face.edges.size(); ++corner) {
                const std::size_t previous = (corner + face.edges.size() - 1U) % face.edges.size();
                vertices_[face.source->vertices[corner]].corners.emplace_back(
                    face.edges[previous], face.edges[corner]);
            }
        }
    }

    [[nodiscard]] bool live_vertex(ElementId id) const {
        const auto found = vertices_.find(id);
        return id != kInvalidElementId && found != vertices_.end() &&
               found->second.occurrences == 1U;
    }

    [[nodiscard]] std::optional<std::size_t> edge_index(Edge edge) const {
        const auto found = edge_indices_.find(make_edge(edge.a, edge.b));
        return found == edge_indices_.end() ? std::nullopt :
            std::optional<std::size_t> { found->second };
    }

    [[nodiscard]] std::optional<std::size_t> face_index(ElementId id) const {
        const auto found = face_indices_.find(id);
        return found == face_indices_.end() ? std::nullopt :
            std::optional<std::size_t> { found->second };
    }

    [[nodiscard]] const std::vector<IndexedEdge>& edges() const { return edges_; }
    [[nodiscard]] const std::vector<IndexedFace>& faces() const { return faces_; }
    [[nodiscard]] const std::map<ElementId, IndexedVertex>& vertices() const { return vertices_; }

private:
    std::vector<IndexedEdge> edges_;
    std::vector<IndexedFace> faces_;
    std::map<ElementId, IndexedVertex> vertices_;
    std::map<Edge, std::size_t, EdgeLess> edge_indices_;
    std::map<ElementId, std::size_t> face_indices_;
};

using EdgeRelations = std::vector<std::vector<std::size_t>>;

void connect(EdgeRelations& relations, std::size_t first, std::size_t second) {
    append_unique(relations[first], second);
    append_unique(relations[second], first);
}

/// The corner-link graph distinguishes a disk fan from branches or separate fans.
struct VertexFan {
    bool valid = false;
    bool closed = false;
    EdgeRelations links;
};

VertexFan vertex_fan(const IndexedVertex& vertex) {
    VertexFan fan;
    if (!vertex.eligible_faces) {
        return fan;
    }
    fan.links.resize(vertex.edges.size());
    for (const auto& [first, second] : vertex.corners) {
        const std::size_t first_local = static_cast<std::size_t>(
            std::ranges::lower_bound(vertex.edges, first) - vertex.edges.begin());
        const std::size_t second_local = static_cast<std::size_t>(
            std::ranges::lower_bound(vertex.edges, second) - vertex.edges.begin());
        connect(fan.links, first_local, second_local);
        if (fan.links[first_local].size() > 2U || fan.links[second_local].size() > 2U) {
            return fan;
        }
    }
    if (fan.links.empty()) {
        return fan;
    }
    std::size_t ends = 0U;
    for (const auto& neighbors : fan.links) {
        if (neighbors.size() == 1U) {
            ++ends;
        } else if (neighbors.size() != 2U) {
            return fan;
        }
    }
    if (ends != 0U && ends != 2U) {
        return fan;
    }
    std::vector<bool> reached(fan.links.size(), false);
    std::vector<std::size_t> pending { 0U };
    reached[0] = true;
    for (std::size_t index = 0U; index < pending.size(); ++index) {
        for (const std::size_t neighbor : fan.links[pending[index]]) {
            if (!reached[neighbor]) {
                reached[neighbor] = true;
                pending.push_back(neighbor);
            }
        }
    }
    fan.valid = pending.size() == fan.links.size();
    fan.closed = ends == 0U;
    return fan;
}

std::vector<std::size_t> edge_component(const EdgeRelations& relations, std::size_t seed) {
    std::vector<std::size_t> component { seed };
    std::vector<bool> reached(relations.size(), false);
    reached[seed] = true;
    for (std::size_t index = 0U; index < component.size(); ++index) {
        for (const std::size_t neighbor : relations[component[index]]) {
            if (!reached[neighbor]) {
                reached[neighbor] = true;
                component.push_back(neighbor);
            }
        }
    }
    return component;
}

std::vector<Edge> component_edges(const TraversalTopology& topology,
                                const EdgeRelations& relations, std::size_t seed) {
    std::vector<Edge> result;
    for (const std::size_t edge : edge_component(relations, seed)) {
        result.push_back(topology.edges()[edge].edge);
    }
    return result;
}

std::vector<ElementId> edge_endpoints(std::span<const Edge> edges) {
    std::vector<ElementId> result;
    std::unordered_set<ElementId> reached;
    reached.reserve(edges.size() * 2U);
    for (const Edge edge : edges) {
        for (const ElementId vertex : { edge.a, edge.b }) {
            if (reached.insert(vertex).second) {
                result.push_back(vertex);
            }
        }
    }
    return result;
}

std::vector<std::size_t> incident_ngons(const TraversalTopology& topology, std::size_t edge) {
    std::vector<std::size_t> result;
    for (const std::size_t face : topology.edges()[edge].faces) {
        if (topology.faces()[face].eligible && topology.faces()[face].edges.size() > 4U) {
            result.push_back(face);
        }
    }
    return result;
}

EdgeRelations loop_relations(const TraversalTopology& topology, std::size_t seed) {
    EdgeRelations relations(topology.edges().size());
    const bool boundary = topology.edges()[seed].faces.size() == 1U;
    const auto seed_ngons = incident_ngons(topology, seed);
    const std::optional<std::size_t> hub = !boundary && seed_ngons.size() == 1U ?
        std::optional<std::size_t> { seed_ngons.front() } : std::nullopt;
    if (!boundary && seed_ngons.size() > 1U) {
        return relations;
    }
    for (const auto& [vertex_id, vertex] : topology.vertices()) {
        if (!topology.live_vertex(vertex_id)) {
            continue;
        }
        if (!boundary && vertex.edges.size() != (hub.has_value() ? 3U : 4U)) {
            continue;
        }
        const VertexFan fan = vertex_fan(vertex);
        if (!fan.valid) {
            continue;
        }
        if (boundary) {
            std::vector<std::size_t> exposed;
            for (const std::size_t edge : vertex.edges) {
                if (topology.edges()[edge].faces.size() == 1U) {
                    exposed.push_back(edge);
                }
            }
            if (fan.closed || exposed.size() != 2U) {
                continue;
            }
            const std::size_t first_face = topology.edges()[exposed[0]].faces.front();
            const std::size_t second_face = topology.edges()[exposed[1]].faces.front();
            const auto first_size = topology.faces()[first_face].edges.size();
            const auto second_size = topology.faces()[second_face].edges.size();
            if ((first_face != second_face && first_size == 4U && second_size == 4U) ||
                (first_face == second_face && first_size > 4U)) {
                connect(relations, exposed[0], exposed[1]);
            }
        } else if (hub.has_value()) {
            if (!fan.closed || vertex.edges.size() != 3U) {
                continue;
            }
            std::vector<std::size_t> perimeter;
            for (const std::size_t edge : vertex.edges) {
                const auto ngons = incident_ngons(topology, edge);
                if (topology.edges()[edge].faces.size() == 2U && ngons.size() == 1U &&
                    ngons.front() == *hub) {
                    perimeter.push_back(edge);
                }
            }
            if (perimeter.size() == 2U) {
                connect(relations, perimeter[0], perimeter[1]);
            }
        } else if (fan.closed && vertex.edges.size() == 4U) {
            for (std::size_t first = 0U; first < vertex.edges.size(); ++first) {
                if (!incident_ngons(topology, vertex.edges[first]).empty()) {
                    continue;
                }
                for (std::size_t second = first + 1U; second < vertex.edges.size(); ++second) {
                    if (incident_ngons(topology, vertex.edges[second]).empty() &&
                        std::ranges::find(fan.links[first], second) == fan.links[first].end()) {
                        connect(relations, vertex.edges[first], vertex.edges[second]);
                    }
                }
            }
        }
    }
    // At each endpoint the relation is unique. Stable neighboring edge order
    // gives breadth-first endpoint ordering independent of document storage.
    for (auto& neighbors : relations) {
        std::ranges::sort(neighbors);
    }
    return relations;
}

EdgeRelations ring_relations(const TraversalTopology& topology) {
    EdgeRelations relations(topology.edges().size());
    for (const IndexedFace& face : topology.faces()) {
        if (!face.eligible || face.edges.size() != 4U) {
            continue;
        }
        for (std::size_t axis = 0U; axis < 2U; ++axis) {
            const std::size_t first = face.edges[axis];
            const std::size_t second = face.edges[axis + 2U];
            if (topology.edges()[first].faces.size() <= 2U &&
                topology.edges()[second].faces.size() <= 2U) {
                connect(relations, first, second);
            }
        }
    }
    for (auto& neighbors : relations) {
        std::ranges::sort(neighbors);
    }
    return relations;
}

std::optional<std::size_t> quad_axis(const TraversalTopology& topology,
                                     std::size_t face, std::size_t edge) {
    const IndexedFace& indexed = topology.faces()[face];
    if (!indexed.eligible || indexed.edges.size() != 4U) {
        return std::nullopt;
    }
    const auto found = std::ranges::find(indexed.edges, edge);
    if (found == indexed.edges.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(found - indexed.edges.begin()) % 2U;
}

/// Each quad has two independent strip nodes, one for each opposite-edge pair.
struct StripNode {
    std::array<std::optional<std::size_t>, 2> neighbors;
};

std::vector<StripNode> face_strip_nodes(const TraversalTopology& topology) {
    std::vector<StripNode> nodes(topology.faces().size() * 2U);
    for (std::size_t face = 0U; face < topology.faces().size(); ++face) {
        const IndexedFace& indexed = topology.faces()[face];
        if (!indexed.eligible || indexed.edges.size() != 4U) {
            continue;
        }
        for (std::size_t corner = 0U; corner < 4U; ++corner) {
            const auto& shared = topology.edges()[indexed.edges[corner]].faces;
            if (shared.size() != 2U) {
                continue;
            }
            const std::size_t adjacent = shared[0] == face ? shared[1] : shared[0];
            const auto adjacent_axis = quad_axis(topology, adjacent, indexed.edges[corner]);
            if (adjacent_axis.has_value()) {
                nodes[face * 2U + corner % 2U].neighbors[corner / 2U] =
                    adjacent * 2U + *adjacent_axis;
            }
        }
    }
    return nodes;
}

std::vector<ElementId> face_component(const TraversalTopology& topology,
                                     std::size_t seed, ElementId context) {
    const auto picked = topology.face_index(context);
    const auto& incident = topology.edges()[seed].faces;
    if (!picked.has_value() || !topology.faces()[*picked].eligible || incident.size() > 2U ||
        std::ranges::find(incident, *picked) == incident.end()) {
        return {};
    }
    std::optional<std::size_t> initial;
    if (quad_axis(topology, *picked, seed).has_value()) {
        initial = picked;
    } else {
        for (const std::size_t face : incident) {
            if (quad_axis(topology, face, seed).has_value()) {
                initial = face;
                break;
            }
        }
    }
    if (!initial.has_value()) {
        return {};
    }

    const auto nodes = face_strip_nodes(topology);

    const auto& initial_edges = topology.faces()[*initial].edges;
    const std::size_t entry_corner = static_cast<std::size_t>(
        std::ranges::find(initial_edges, seed) - initial_edges.begin());
    const std::size_t initial_node = *initial * 2U + entry_corner % 2U;
    std::vector<ElementId> result { topology.faces()[*initial].source->id };
    std::vector<bool> emitted(topology.faces().size(), false);
    emitted[*initial] = true;
    std::vector<bool> reached(nodes.size(), false);
    reached[initial_node] = true;
    std::vector<std::size_t> pending;
    const auto append_pending = [&](const std::optional<std::size_t>& neighbor) {
        if (neighbor.has_value()) {
            pending.push_back(*neighbor);
        }
    };
    append_pending(nodes[initial_node].neighbors[entry_corner / 2U]);
    append_pending(nodes[initial_node].neighbors[1U - entry_corner / 2U]);
    while (!pending.empty()) {
        const std::size_t node = pending.back();
        pending.pop_back();
        if (reached[node]) {
            continue;
        }
        reached[node] = true;
        if (!emitted[node / 2U]) {
            emitted[node / 2U] = true;
            result.push_back(topology.faces()[node / 2U].source->id);
        }
        for (const auto& neighbor : nodes[node].neighbors) {
            append_pending(neighbor);
        }
    }
    return result;
}

std::vector<ElementId> canonical_ids(std::span<const ElementId> ids) {
    std::vector<ElementId> result(ids.begin(), ids.end());
    std::ranges::sort(result);
    result.erase(std::ranges::unique(result).begin(), result.end());
    return result;
}

/// Orders one unbranched relation component so bounds refer to topology distance.
struct OrderedComponent {
    std::vector<std::size_t> nodes;
    bool closed = false;
};

template <typename Less>
OrderedComponent order_component(const EdgeRelations& relations, std::size_t seed,
                                 const Less& less) {
    const auto component = edge_component(relations, seed);
    std::vector<std::size_t> ends;
    for (const std::size_t node : component) {
        if (relations[node].size() > 2U) {
            return {};
        }
        if (relations[node].size() < 2U) {
            ends.push_back(node);
        }
    }
    if (component.size() == 1U) {
        return { component, false };
    }
    if (!ends.empty() && ends.size() != 2U) {
        return {};
    }
    const auto& starts = ends.empty() ? component : ends;
    const std::size_t start = *std::ranges::min_element(starts, less);
    OrderedComponent ordered { { start }, ends.empty() };
    std::optional<std::size_t> previous;
    std::size_t current = start;
    while (ordered.nodes.size() < component.size()) {
        std::optional<std::size_t> next;
        for (const std::size_t neighbor : relations[current]) {
            if ((!previous.has_value() || neighbor != *previous) &&
                (!next.has_value() || less(neighbor, *next))) {
                next = neighbor;
            }
        }
        if (!next.has_value() || *next == start) {
            return {};
        }
        ordered.nodes.push_back(*next);
        previous = current;
        current = *next;
    }
    return ordered;
}

/// Fills gaps between seeds on an already determined loop, never across the mesh.
template <typename Selected, typename Less>
std::vector<std::size_t> bounded_component(const OrderedComponent& component,
                                          const Selected& selected, const Less& less) {
    std::vector<std::size_t> seeds;
    for (std::size_t position = 0U; position < component.nodes.size(); ++position) {
        if (selected(component.nodes[position])) {
            seeds.push_back(position);
        }
    }
    if (seeds.size() < 2U) {
        return component.nodes;
    }
    if (!component.closed) {
        const std::size_t begin = seeds.front();
        const std::size_t end = seeds.back() + 1U;
        // One existing run establishes direction rather than an interval.
        if (end - begin == seeds.size()) {
            return component.nodes;
        }
        return { component.nodes.begin() + static_cast<std::ptrdiff_t>(begin),
                 component.nodes.begin() + static_cast<std::ptrdiff_t>(end) };
    }

    const std::size_t count = component.nodes.size();
    std::size_t gap_count = 0U;
    std::size_t largest_gap = 0U;
    std::size_t begin = 0U;
    for (std::size_t index = 0U; index < seeds.size(); ++index) {
        const std::size_t next = seeds[(index + 1U) % seeds.size()];
        const std::size_t gap = (next + count - seeds[index] - 1U) % count;
        gap_count += gap != 0U ? 1U : 0U;
        // Removing the largest seed-free gap leaves the fewest selected nodes.
        // Equal gaps choose the smallest stable ID at the retained start.
        if (gap > largest_gap || (gap == largest_gap &&
            less(component.nodes[next], component.nodes[begin]))) {
            largest_gap = gap;
            begin = next;
        }
    }
    if (gap_count <= 1U) {
        return component.nodes;
    }
    std::vector<std::size_t> result;
    result.reserve(count - largest_gap);
    for (std::size_t offset = 0U; offset < count - largest_gap; ++offset) {
        result.push_back(component.nodes[(begin + offset) % count]);
    }
    return result;
}

std::optional<std::size_t> guided_face_anchor(const TraversalTopology& topology,
                                             const Selection& prior, ElementId clicked) {
    if (prior.mode != SelectionMode::Face || !prior.has_active ||
        prior.active_kind != ElementKind::Face ||
        std::ranges::find(prior.faces, prior.active_face) == prior.faces.end()) {
        return std::nullopt;
    }
    ElementId anchor = prior.active_face;
    if (anchor == clicked) {
        const auto previous = std::ranges::find_if(prior.faces | std::views::reverse,
            [&](ElementId id) { return id != clicked; });
        if (previous == prior.faces.rend()) {
            return std::nullopt;
        }
        anchor = *previous;
    }
    const auto found = topology.face_index(anchor);
    return found.has_value() && topology.faces()[*found].eligible ? found : std::nullopt;
}

std::optional<std::vector<ElementId>> guided_face_interval(
    const TraversalTopology& topology, std::size_t picked, const Selection& prior) {
    const auto anchor = guided_face_anchor(topology, prior, topology.faces()[picked].source->id);
    if (!anchor.has_value() || topology.faces()[picked].edges.size() != 4U ||
        topology.faces()[*anchor].edges.size() != 4U) {
        return std::nullopt;
    }
    std::optional<std::size_t> adjacent_axis;
    for (const std::size_t edge : topology.faces()[picked].edges) {
        if (std::ranges::find(topology.faces()[*anchor].edges, edge) !=
            topology.faces()[*anchor].edges.end()) {
            // A single shared edge establishes local direction even if the two
            // global strips intersect again elsewhere on an unusual surface.
            if (adjacent_axis.has_value() || topology.edges()[edge].faces.size() != 2U) {
                return std::nullopt;
            }
            adjacent_axis = quad_axis(topology, picked, edge);
        }
    }
    const auto nodes = face_strip_nodes(topology);
    EdgeRelations relations(nodes.size());
    for (std::size_t node = 0U; node < nodes.size(); ++node) {
        for (const auto neighbor : nodes[node].neighbors) {
            if (neighbor.has_value()) {
                connect(relations, node, *neighbor);
            }
        }
    }
    const auto less = [&](std::size_t left, std::size_t right) {
        return topology.faces()[left / 2U].source->id < topology.faces()[right / 2U].source->id;
    };
    std::optional<OrderedComponent> guided;
    for (std::size_t axis = 0U; axis < 2U; ++axis) {
        if (adjacent_axis.has_value() && axis != *adjacent_axis) {
            continue;
        }
        auto ordered = order_component(relations, picked * 2U + axis, less);
        if (std::ranges::none_of(ordered.nodes, [&](std::size_t node) {
            return node / 2U == *anchor;
        })) {
            continue;
        }
        // A self-crossing strip cannot provide an unambiguous interval of faces.
        std::unordered_set<std::size_t> unique_faces;
        for (const std::size_t node : ordered.nodes) {
            if (!unique_faces.insert(node / 2U).second) {
                return std::nullopt;
            }
        }
        if (guided.has_value()) {
            return std::nullopt;
        }
        guided = std::move(ordered);
    }
    if (!guided.has_value()) {
        return std::nullopt;
    }
    const std::unordered_set<ElementId> selected(prior.faces.begin(), prior.faces.end());
    const auto interval = bounded_component(*guided, [&](std::size_t node) {
        return node / 2U == picked || selected.contains(topology.faces()[node / 2U].source->id);
    }, less);
    std::vector<ElementId> result;
    result.reserve(interval.size());
    for (const std::size_t node : interval) {
        result.push_back(topology.faces()[node / 2U].source->id);
    }
    return result;
}

} // namespace

Selection component_double_click_selection(const Document& document, const PickResult& clicked,
                                           const Selection& prior_selection) {
    Selection result;
    result.mode = clicked.kind == ElementKind::Edge ? SelectionMode::Edge :
        (clicked.kind == ElementKind::Face ? SelectionMode::Face : SelectionMode::Vertex);
    if (!clicked.hit) {
        return result;
    }
    const TraversalTopology topology(document);
    if (clicked.kind == ElementKind::Vertex) {
        return topology.live_vertex(clicked.vertex_id) ?
            connected_component_selection(document, clicked) : result;
    }
    if (clicked.kind == ElementKind::Face) {
        const auto picked = topology.face_index(clicked.face_id);
        if (!picked.has_value() || !topology.faces()[*picked].eligible) {
            return result;
        }
        auto guided = guided_face_interval(topology, *picked, prior_selection);
        if (!guided.has_value()) {
            return connected_component_selection(document, clicked);
        }
        select_only(result, clicked);
        result.faces = std::move(*guided);
        return result;
    }
    const auto picked = topology.edge_index(clicked.edge);
    if (!picked.has_value()) {
        return result;
    }
    const auto relations = loop_relations(topology, *picked);
    const auto less = [](std::size_t left, std::size_t right) { return left < right; };
    const auto ordered = order_component(relations, *picked, less);
    std::unordered_set<std::size_t> selected;
    if (prior_selection.mode == SelectionMode::Edge) {
        for (const Edge edge : prior_selection.edges) {
            if (const auto index = topology.edge_index(edge); index.has_value()) {
                selected.insert(*index);
            }
        }
    }
    const auto interval = bounded_component(ordered, [&](std::size_t node) {
        return node == *picked || selected.contains(node);
    }, less);
    if (interval.empty()) {
        return result;
    }
    select_only(result, clicked);
    result.active_edge = topology.edges()[*picked].edge;
    result.edges.clear();
    result.edges.reserve(interval.size());
    for (const std::size_t node : interval) {
        result.edges.push_back(topology.edges()[node].edge);
    }
    return result;
}

std::vector<Edge> edge_loop_edges(const Document& document, Edge seed_edge) {
    const TraversalTopology topology(document);
    const auto seed = topology.edge_index(seed_edge);
    if (!seed.has_value()) {
        return {};
    }
    return component_edges(topology, loop_relations(topology, *seed), *seed);
}

std::vector<Edge> edge_loop_edges(const Document& document, Edge seed_edge, ElementId face_id) {
    static_cast<void>(face_id);
    return edge_loop_edges(document, seed_edge);
}

std::vector<ElementId> vertex_loop_vertices(const Document& document, Edge seed_edge,
                                          ElementId face_id) {
    return edge_endpoints(edge_loop_edges(document, seed_edge, face_id));
}

std::vector<Edge> edge_boundary_component_edges(const Document& document, Edge seed_edge) {
    const TraversalTopology topology(document);
    const auto seed = topology.edge_index(seed_edge);
    if (!seed.has_value() || topology.edges()[*seed].faces.size() != 1U) {
        return {};
    }
    std::vector<std::size_t> component { *seed };
    std::vector<bool> reached(topology.edges().size(), false);
    reached[*seed] = true;
    std::unordered_set<ElementId> expanded_vertices;
    std::vector<std::size_t> neighbors;
    for (std::size_t index = 0U; index < component.size(); ++index) {
        const Edge edge = topology.edges()[component[index]].edge;
        neighbors.clear();
        for (const ElementId vertex : { edge.a, edge.b }) {
            if (expanded_vertices.insert(vertex).second) {
                for (const std::size_t incident : topology.vertices().at(vertex).edges) {
                    if (topology.edges()[incident].faces.size() == 1U && !reached[incident]) {
                        neighbors.push_back(incident);
                    }
                }
            }
        }
        std::ranges::sort(neighbors);
        for (const std::size_t neighbor : neighbors) {
            if (!reached[neighbor]) {
                reached[neighbor] = true;
                component.push_back(neighbor);
            }
        }
    }
    std::vector<Edge> result;
    result.reserve(component.size());
    for (const std::size_t edge : component) {
        result.push_back(topology.edges()[edge].edge);
    }
    return result;
}

std::vector<Edge> edge_ring_edges(const Document& document, Edge seed_edge) {
    const TraversalTopology topology(document);
    const auto seed = topology.edge_index(seed_edge);
    if (!seed.has_value()) {
        return {};
    }
    return component_edges(topology, ring_relations(topology), *seed);
}

bool face_loop_seed_eligible(const Document& document, Edge seed_edge, ElementId face_id) {
    const Edge seed = make_edge(seed_edge.a, seed_edge.b);
    if (!is_edge(seed) || !unique_live_vertex(document, seed.a) ||
        !unique_live_vertex(document, seed.b)) {
        return false;
    }
    std::size_t incident_count = 0U;
    bool valid_context = false;
    bool incident_quad = false;
    for (const Face& face : document.faces) {
        if (!face_contains_edge(face, seed)) {
            continue;
        }
        if (++incident_count > 2U) {
            return false;
        }
        const bool eligible = eligible_face(document, face);
        valid_context = valid_context || (face.id == face_id && eligible);
        incident_quad = incident_quad || (face.vertices.size() == 4U && eligible);
    }
    return valid_context && incident_quad;
}

std::vector<ElementId> face_loop_faces(const Document& document, Edge seed_edge,
                                     ElementId face_id) {
    const TraversalTopology topology(document);
    const auto seed = topology.edge_index(seed_edge);
    return seed.has_value() ? face_component(topology, *seed, face_id) : std::vector<ElementId> {};
}

std::optional<Edge> component_loop_seed(const Document& document, const PickResult& clicked,
                                      const Selection& prior_selection, Edge pointer_hint,
                                      bool ring) {
    if (!clicked.hit) {
        return std::nullopt;
    }
    const TraversalTopology topology(document);
    std::vector<std::size_t> candidates;
    std::optional<std::size_t> picked_face;
    switch (clicked.kind) {
    case ElementKind::Edge: {
        const auto picked = topology.edge_index(clicked.edge);
        if (picked.has_value()) {
            candidates.push_back(*picked);
        }
        break;
    }
    case ElementKind::Vertex:
        if (topology.live_vertex(clicked.vertex_id)) {
            candidates = topology.vertices().at(clicked.vertex_id).edges;
        }
        break;
    case ElementKind::Face:
        picked_face = topology.face_index(clicked.face_id);
        if (picked_face.has_value() && topology.faces()[*picked_face].eligible) {
            candidates = topology.faces()[*picked_face].edges;
        }
        break;
    }
    if (candidates.empty()) {
        return std::nullopt;
    }
    std::ranges::sort(candidates);
    const bool explicit_hint = pointer_hint.a != kInvalidElementId || pointer_hint.b != kInvalidElementId;
    const auto hint = topology.edge_index(pointer_hint);
    if (explicit_hint && (!hint.has_value() ||
        std::ranges::find(candidates, *hint) == candidates.end())) {
        return std::nullopt;
    }
    if (clicked.kind == ElementKind::Edge) {
        return topology.edges()[candidates.front()].edge;
    }

    const bool matching_active = prior_selection.has_active && prior_selection.active_kind == clicked.kind &&
        ((clicked.kind == ElementKind::Vertex && prior_selection.mode == SelectionMode::Vertex &&
          topology.live_vertex(prior_selection.active_vertex) &&
          std::ranges::find(prior_selection.vertices, prior_selection.active_vertex) != prior_selection.vertices.end()) ||
         (clicked.kind == ElementKind::Face && prior_selection.mode == SelectionMode::Face &&
          topology.face_index(prior_selection.active_face).has_value() &&
          std::ranges::find(prior_selection.faces, prior_selection.active_face) != prior_selection.faces.end()));
    if (matching_active) {
        if (clicked.kind == ElementKind::Vertex && prior_selection.active_vertex != clicked.vertex_id) {
            const auto direction = topology.edge_index(make_edge(clicked.vertex_id, prior_selection.active_vertex));
            if (direction.has_value()) {
                return topology.edges()[*direction].edge;
            }
        } else if (clicked.kind == ElementKind::Face && prior_selection.active_face != clicked.face_id) {
            const IndexedFace& active = topology.faces()[*topology.face_index(prior_selection.active_face)];
            if (active.eligible) {
                std::vector<std::size_t> shared;
                for (const std::size_t edge : candidates) {
                    if (std::ranges::find(active.edges, edge) != active.edges.end()) {
                        shared.push_back(edge);
                    }
                }
                if (shared.size() > 1U) {
                    return std::nullopt;
                }
                if (shared.size() == 1U) {
                    return topology.edges()[shared.front()].edge;
                }
            }
        }

        const bool same_active = clicked.kind == ElementKind::Vertex ?
            prior_selection.active_vertex == clicked.vertex_id : prior_selection.active_face == clicked.face_id;
        if (same_active) {
            const EdgeRelations rings = ring ? ring_relations(topology) : EdgeRelations {};
            const auto selected = canonical_ids(clicked.kind == ElementKind::Face ?
                prior_selection.faces : prior_selection.vertices);
            std::optional<std::size_t> recovered;
            std::vector<ElementId> recovered_component;
            bool ambiguous = false;
            for (const std::size_t candidate : candidates) {
                std::vector<ElementId> component;
                if (clicked.kind == ElementKind::Face) {
                    component = canonical_ids(face_component(topology, candidate, clicked.face_id));
                } else {
                    const auto edges = component_edges(topology,
                        ring ? rings : loop_relations(topology, candidate), candidate);
                    component = canonical_ids(edge_endpoints(edges));
                }
                if (!component.empty() && std::ranges::includes(selected, component)) {
                    if (recovered.has_value() && recovered_component != component) {
                        ambiguous = true;
                        break;
                    }
                    recovered = candidate;
                    recovered_component = std::move(component);
                }
            }
            if (recovered.has_value() && !ambiguous) {
                return topology.edges()[*recovered].edge;
            }
        }
    }
    return topology.edges()[hint.has_value() ? *hint : candidates.front()].edge;
}

} // namespace quader_poly
