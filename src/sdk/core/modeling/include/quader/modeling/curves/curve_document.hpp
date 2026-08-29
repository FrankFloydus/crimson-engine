////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <quader/modeling/ids.hpp>
#include <quader/modeling/operations/operation_result.hpp>
#include <quader/modeling/result.hpp>
#include <quader/modeling/types.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace quader::modeling {

struct CurvePointTag;
struct CurveSegmentTag;
using CurvePointId = Id<CurvePointTag>;
using CurveSegmentId = Id<CurveSegmentTag>;

/// Identifies one immutable publication of authored curve geometry.
struct CurveContentIdentity final {
  std::uint64_t value = 0;

  [[nodiscard]] constexpr bool valid() const noexcept { return value != 0; }
  friend constexpr bool operator==(const CurveContentIdentity &,
                                   const CurveContentIdentity &) = default;
};

/// Describes a cubic anchor and its incoming/outgoing control offsets in local space.
struct CurvePointSpec {
  Vec3 position{};
  Vec3 incoming_handle{};
  Vec3 outgoing_handle{};
};

/// Replaces one authored point while retaining its stable identity and path index.
struct CurvePointEdit {
  CurvePointId id{};
  CurvePointSpec point{};
};

/// Stores an authored anchor with an identity independent of its path index.
struct CurveControlPoint {
  CurvePointId id{};
  Vec3 position{};
  Vec3 incoming_handle{};
  Vec3 outgoing_handle{};
};

/// Retains next-to-allocate component indices, including the exhausted uint32+1 value.
struct CurveIdentityHighWater {
  std::uint64_t next_point_index = 1;
  std::uint64_t next_segment_index = 1;

  friend constexpr bool operator==(const CurveIdentityHighWater &,
                                   const CurveIdentityHighWater &) = default;
};

/**
 * Owns the durable control data of one ordered cubic Bezier path.
 * Segment i connects point i to point i+1, wrapping only for a closed path.
 * The open last point has no outgoing segment. IDs use generation 1 and are
 * allocated monotonically; removed IDs are never reused in the same lineage.
 */
struct AuthoredCurve {
  std::vector<CurveControlPoint> points;
  std::vector<CurveSegmentId> segments;
  bool closed = false;
  CurveIdentityHighWater identity_high_water{};
};

/// Describes a local-space curve AABB; an empty path has valid=false.
struct CurveBounds {
  Vec3 min{};
  Vec3 max{};
  bool valid = false;
};

/// Returns a position and unnormalized derivative with respect to segment parameter.
struct CurveEvaluation {
  Vec3 position{};
  Vec3 tangent{};
};

/// Associates each derived display sample with its authored segment and parameter.
struct CurveSample {
  Vec3 position{};
  Vec3 tangent{};
  CurveSegmentId segment{};
  double parameter = 0.0;
};

/// Bounds adaptive tessellation error in local units and caps its work explicitly.
struct CurveSamplingOptions {
  double tolerance = 0.01;
  std::uint32_t max_depth = 16;
  std::uint32_t max_samples = 1'000'000;
};

struct CurveDocumentData;

/**
 * Owns a portable open/closed cubic Bezier path with immutable shared content.
 * Copies pin authored state until the first real edit. Failed/no-op edits retain
 * identity and revision. Selection, scene placement and history stay with their
 * existing owners; tessellation is derived and never becomes authored geometry.
 */
class CurveDocument {
public:
  CurveDocument();

  [[nodiscard]] static Result<CurveDocument>
  make_bezier(std::span<const CurvePointSpec> points, bool closed = false);
  [[nodiscard]] static Result<CurveDocument>
  from_authored_curve(const AuthoredCurve &authored);

  [[nodiscard]] std::size_t point_count() const noexcept;
  [[nodiscard]] std::size_t segment_count() const noexcept;
  [[nodiscard]] bool closed() const noexcept;
  /// Borrows immutable points until this facade is mutated or destroyed.
  [[nodiscard]] std::span<const CurveControlPoint> points() const noexcept;
  /// Borrows ordered segment IDs until this facade is mutated or destroyed.
  [[nodiscard]] std::span<const CurveSegmentId> segments() const noexcept;
  [[nodiscard]] CurveContentIdentity content_identity() const noexcept;
  [[nodiscard]] std::uint64_t content_revision() const noexcept;
  [[nodiscard]] CurveIdentityHighWater identity_high_water() const noexcept;
  /// Merges allocation cursors without changing the authored publication.
  void preserve_identity_high_water(CurveIdentityHighWater observed) noexcept;
  [[nodiscard]] AuthoredCurve authored_curve() const;
  /// Compares authored points, stable IDs, segment order and closure exactly.
  [[nodiscard]] bool has_same_geometry(const CurveDocument &other) const noexcept;
  [[nodiscard]] Result<OperationResult> validate() const;

  [[nodiscard]] Result<OperationResult> append_point(const CurvePointSpec &point);
  [[nodiscard]] Result<OperationResult> set_point(CurvePointId id,
                                                const CurvePointSpec &point);
  /**
   * Applies unique point replacements as one atomic authored publication.
   * Every ID and replacement is validated before storage is copied. An empty
   * batch or a batch containing only exact no-ops retains content identity.
   */
  [[nodiscard]] Result<OperationResult>
  set_points(std::span<const CurvePointEdit> edits);
  /**
   * Collapses nonzero handles to a corner, or creates aligned handles along the
   * adjacent chord bisector, with one third of each chord's length. Open ends
   * mirror their available chord; coincident sides keep zero length. An isolated
   * or fully coincident corner is unchanged. Anchors and component IDs are kept.
   */
  [[nodiscard]] Result<OperationResult> toggle_point_smoothness(CurvePointId id);
  [[nodiscard]] Result<OperationResult> remove_point(CurvePointId id);
  [[nodiscard]] Result<OperationResult> set_closed(bool closed);
  /**
   * Splits by de Casteljau; the first half retains its original ID. Each stored
   * control must stay within 64 * float epsilon * original control-hull diameter
   * of its ideal local-space value. An unrepresentable split fails atomically;
   * moving a curve far from the origin never increases this error budget.
   */
  [[nodiscard]] Result<OperationResult> split_segment(CurveSegmentId id,
                                                    double parameter = 0.5);
  [[nodiscard]] Result<OperationResult> transform(Transform3 transform);

  [[nodiscard]] CurveBounds bounds() const noexcept;
  [[nodiscard]] Result<CurveEvaluation> evaluate(CurveSegmentId id,
                                                double parameter) const;
  /**
   * Returns owned samples in path order. Each segment includes both endpoints,
   * so adjacent anchors are repeated with each segment's own provenance. An
   * isolated anchor produces one sample with an invalid segment ID. Failure to
   * meet tolerance within the depth/sample budget returns a structured error.
   */
  [[nodiscard]] Result<std::vector<CurveSample>>
  sample(CurveSamplingOptions options = {}) const;

private:
  void publish(AuthoredCurve authored);

  std::shared_ptr<const CurveDocumentData> data_;
  CurveIdentityHighWater identity_high_water_{};
};

/// Encodes durable controls, closure, IDs and allocation cursors in little-endian form.
[[nodiscard]] Result<std::vector<std::byte>>
serialize_curve_document(const CurveDocument &document);
/// Decodes and validates an authored curve; runtime revision starts at zero.
[[nodiscard]] Result<CurveDocument>
deserialize_curve_document(std::span<const std::byte> bytes);

} // namespace quader::modeling
