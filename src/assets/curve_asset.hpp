////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/asset_document.hpp"

#include <memory>

namespace quader::modeling {
class CurveDocument;
struct CurveSamplingOptions;
}

namespace quader {

struct CurveSamplePayload;

/// Pins the SDK-authored curve published by the modeling session.
struct CurveAsset final : AssetDocumentBase<CurveAssetRevision> {
  CurveAsset() = default;
  CurveAsset(const CurveAsset &other);
  CurveAsset(CurveAsset &&other) noexcept;
  CurveAsset &operator=(const CurveAsset &other);
  CurveAsset &operator=(CurveAsset &&other) noexcept;

  std::shared_ptr<const modeling::CurveDocument> document;

private:
  friend std::shared_ptr<const CurveSamplePayload>
  curve_sample_payload(const CurveAsset &asset,
                       modeling::CurveSamplingOptions options);

  // Derived data is shared by snapshots and accessed atomically by samplers.
  // Replacing the authored wrapper does not invalidate unchanged SDK content.
  mutable std::shared_ptr<const CurveSamplePayload> sample_payload_;
};

} // namespace quader
