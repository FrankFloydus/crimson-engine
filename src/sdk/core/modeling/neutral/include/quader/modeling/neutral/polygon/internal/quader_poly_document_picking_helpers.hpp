////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

// Source-only neutral modeling core contract shared by SDK and native owners.

#include <quader/modeling/neutral/polygon/document.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace quader_poly::document_internal {

bool ray_intersects_triangle( const Ray& ray, const quader::QVec3& a, const quader::QVec3& b, const quader::QVec3& c, float& distance, quader::QVec3& position);
float point_ray_distance(const Ray& ray, const quader::QVec3& point, float& ray_distance);
float segment_ray_distance(const Ray& ray, const quader::QVec3& a, const quader::QVec3& b, float& ray_distance, quader::QVec3& position);

/**
 * Counts deterministic units of component-picking work for scale regression
 * tests. The counters describe topology/query work rather than wall-clock
 * timing, so they remain stable across build configurations and machines.
 */
struct PickWorkCounters {
    std::size_t fact_sets_built = 0U;
    std::size_t face_facts_built = 0U;
    std::size_t face_triangle_facts_built = 0U;
    std::size_t surface_triangle_tests = 0U;
    std::size_t incident_face_visits = 0U;
    std::size_t component_candidates_tested = 0U;

    [[nodiscard]] std::size_t total() const noexcept
    {
        return fact_sets_built +
            face_facts_built +
            face_triangle_facts_built +
            surface_triangle_tests +
            incident_face_visits +
            component_candidates_tested;
    }
};

/**
 * Prepares the face normals, triangulation, and vertex/edge incidence used by
 * one immutable document pick. Component queries then visit only incident
 * faces instead of rescanning the complete face domain for every candidate.
 */
class DocumentPickFacts final {
public:
    explicit DocumentPickFacts(
        const Document& document,
        const PickOptions& options,
        PickWorkCounters* work_counters = nullptr);

    [[nodiscard]] PickResult surface_hit(const Ray& local_ray);
    [[nodiscard]] PickResult pick_element(
        const Ray& local_ray,
        const std::optional<PickResult>* precomputed_surface_hit = nullptr);
    [[nodiscard]] PickResult pick_element(
        const Ray& local_ray,
        SelectionMode mode,
        const std::optional<PickResult>* precomputed_surface_hit = nullptr);

    [[nodiscard]] bool vertex_can_be_picked(ElementId vertex_id) const;
    [[nodiscard]] bool edge_can_be_picked(Edge edge) const;
    [[nodiscard]] bool vertex_has_incident_face(ElementId vertex_id) const;
    [[nodiscard]] bool edge_has_incident_face(Edge edge) const;
    [[nodiscard]] bool face_is_front_facing_pickable(
        ElementId face_id,
        const Ray& ray) const;
    [[nodiscard]] bool vertex_has_front_facing_pickable_face(
        ElementId vertex_id,
        const Ray& ray) const;
    /// Optionally admits grazing faces for mesh boundary picks, not components.
    [[nodiscard]] bool edge_has_front_facing_pickable_face(
        Edge edge,
        const Ray& ray,
        bool include_edge_on = false) const;
    [[nodiscard]] ElementId best_edge_context_face_id(
        Edge edge,
        const Ray& ray) const;
    [[nodiscard]] ElementId edge_pick_context_face_id(
        Edge edge,
        const Ray& ray,
        const std::optional<PickResult>& surface_hit) const;
    [[nodiscard]] std::span<const Edge> edges() const noexcept;

private:
    /// Stores one triangulated face primitive for repeated pick queries.
    struct FaceTriangleFact {
        quader::QVec3 a;
        quader::QVec3 b;
        quader::QVec3 c;
    };

    /// Caches the pickability, normal, and triangles of one document face.
    struct FaceFact {
        const Face* face = nullptr;
        quader::QVec3 normal;
        bool pickable = false;
        bool triangles_built = false;
        std::vector<FaceTriangleFact> triangles;
    };

    [[nodiscard]] static std::uint64_t edge_key(Edge edge) noexcept;
    [[nodiscard]] std::span<const std::size_t> incident_faces(
        ElementId vertex_id) const;
    [[nodiscard]] std::span<const std::size_t> incident_faces(Edge edge) const;
    [[nodiscard]] FaceFact* face_fact(ElementId face_id);
    [[nodiscard]] const FaceFact* face_fact(ElementId face_id) const;
    void build_face_triangles(FaceFact& fact);
    [[nodiscard]] PickResult normalized_surface_hit(const Ray& ray);

    const Document& document_;
    PickOptions options_;
    PickWorkCounters* work_counters_ = nullptr;
    std::vector<FaceFact> face_facts_;
    std::vector<Edge> edges_;
    std::unordered_map<ElementId, std::vector<std::size_t>>
        incident_faces_by_vertex_;
    std::unordered_map<std::uint64_t, std::vector<std::size_t>>
        incident_faces_by_edge_;
};

} // namespace quader_poly::document_internal
