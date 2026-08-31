////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

// Source-only neutral modeling core contract shared by SDK and native owners.

#include <quader/modeling/neutral/polygon/document_selection.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace quader_poly {

/**
 * Stores the Pick Options data contract used by the polygon document and mesh editing core.
 */
struct PickOptions {
  SelectionMode mode = SelectionMode::Vertex;
  float vertex_radius = 0.12F;
  float edge_radius = 0.10F;
  bool has_hidden_material_slot = false;
  std::uint32_t hidden_material_slot = 0;
  bool pick_backfaces = false;
  bool prefer_front_facing_faces = true;
  bool reverse_face_winding = false;

  [[nodiscard]] bool hides_material_slot(std::uint32_t material_slot) const {
    return has_hidden_material_slot && material_slot == hidden_material_slot;
  }
};

/**
 * Stores the Pick Result data contract used by the polygon document and mesh editing core.
 */
struct PickResult {
    bool hit = false;
    ElementKind kind = ElementKind::Vertex;
    ElementId vertex_id = kInvalidElementId;
    Edge edge;
    ElementId face_id = kInvalidElementId;
    float distance = 0.0F;
    quader::QVec3 position;
};

/**
 * Represents a Pick Face Triangle value used by the polygon document and mesh editing core.
 */
struct PickFaceTriangle {
  ElementId face_id = kInvalidElementId;
  quader::QVec3 a;
  quader::QVec3 b;
  quader::QVec3 c;
  quader::QVec3 normal;
  quader::QVec3 bounds_min;
  quader::QVec3 bounds_max;
};

/**
 * Caches immutable topology and facing facts for a batch of polygon picks.
 * Native host projections use this source-only neutral query instead of
 * reaching into the modeling core's implementation headers.
 */
class DocumentPickQuery final {
public:
  explicit DocumentPickQuery(const Document &document,
                             const PickOptions &options = {});
  DocumentPickQuery(const DocumentPickQuery &) = delete;
  DocumentPickQuery &operator=(const DocumentPickQuery &) = delete;
  DocumentPickQuery(DocumentPickQuery &&) noexcept;
  DocumentPickQuery &operator=(DocumentPickQuery &&) noexcept;
  ~DocumentPickQuery();

  [[nodiscard]] PickResult surface_hit(const Ray &local_ray);
  [[nodiscard]] PickResult pick_element(
      const Ray &local_ray,
      const std::optional<PickResult> *precomputed_surface_hit = nullptr);
  [[nodiscard]] PickResult pick_element(
      const Ray &local_ray, SelectionMode mode,
      const std::optional<PickResult> *precomputed_surface_hit = nullptr);

  [[nodiscard]] bool vertex_can_be_picked(ElementId vertex_id) const;
  [[nodiscard]] bool edge_can_be_picked(Edge edge) const;
  [[nodiscard]] bool vertex_has_incident_face(ElementId vertex_id) const;
  [[nodiscard]] bool edge_has_incident_face(Edge edge) const;
  [[nodiscard]] bool face_is_front_facing_pickable(ElementId face_id,
                                                    const Ray &ray) const;
  [[nodiscard]] bool vertex_has_front_facing_pickable_face(
      ElementId vertex_id, const Ray &ray) const;
  [[nodiscard]] bool edge_has_front_facing_pickable_face(
      Edge edge, const Ray &ray, bool include_edge_on = false) const;
  [[nodiscard]] ElementId best_edge_context_face_id(Edge edge,
                                                    const Ray &ray) const;
  [[nodiscard]] ElementId edge_pick_context_face_id(
      Edge edge, const Ray &ray,
      const std::optional<PickResult> &surface_hit) const;
  [[nodiscard]] std::span<const Edge> edges() const noexcept;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

[[nodiscard]] PickResult pick_element(const Document& document, const Ray& local_ray, const PickOptions& options);
[[nodiscard]] std::vector<PickFaceTriangle> build_face_pick_geometry(const Document& document);
[[nodiscard]] std::vector<PickFaceTriangle> build_face_pick_geometry(const Document& document, const PickOptions& options);
[[nodiscard]] std::vector<PickFaceTriangle> build_face_pick_geometry(
    const Document &document, ElementId face_id,
    const PickOptions &options = {});
[[nodiscard]] PickResult pick_face_geometry(std::span<const PickFaceTriangle> triangles, const Ray& local_ray, const PickOptions& options = {});

} // namespace quader_poly
