////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <mesh/polygon/document_selection.hpp>

#include <cstdint>
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

[[nodiscard]] PickResult pick_element(const Document& document, const Ray& local_ray, const PickOptions& options);
[[nodiscard]] std::vector<PickFaceTriangle> build_face_pick_geometry(const Document& document);
[[nodiscard]] std::vector<PickFaceTriangle> build_face_pick_geometry(const Document& document, const PickOptions& options);
[[nodiscard]] PickResult pick_face_geometry(std::span<const PickFaceTriangle> triangles, const Ray& local_ray, const PickOptions& options = {});

} // namespace quader_poly
