////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/interactions/selection_box.hpp"
#include "editor/interactions/viewport_projection.hpp"

#include <span>

namespace quader_editor_core {

/** Boundary-inclusive even-odd freehand region shared by lasso hit providers. */
class SelectionLassoRegion final {
public:
  explicit SelectionLassoRegion(std::span<const quader::QVec2> points);
  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] bool contains_point(quader::QVec2 point) const;
  [[nodiscard]] bool contains_segment(quader::QVec2 start,
                                      quader::QVec2 end) const;
  [[nodiscard]] bool intersects_segment(quader::QVec2 start,
                                        quader::QVec2 end) const;
  [[nodiscard]] bool contains_polygon(
      std::span<const quader::QVec2> polygon) const;
  [[nodiscard]] bool intersects_polygon(
      std::span<const quader::QVec2> polygon) const;
  [[nodiscard]] bool matches_object(const RectangleProjectedObject &object,
                                     bool partial_intersection) const;

private:
  std::vector<quader::QVec2> points_;
  std::vector<quader::QVec2> exterior_witnesses_;
  bool has_interior_ = false;
};

/** Resolves distinct element identities without consulting surface depth. */
[[nodiscard]] RectangleSelectionHits lasso_hits_from_projected_candidates(
    const RectangleProjectedHitsRequest &candidates,
    const SelectionLassoRegion &region, bool partial_intersection);

/** Clips a polygon to camera depth before perspective projection. */
[[nodiscard]] std::vector<quader::QVec2> project_lasso_polygon(
    const ViewportCameraState &camera, std::span<const quader::QVec3> vertices,
    double near_plane, double far_plane);

} // namespace quader_editor_core
