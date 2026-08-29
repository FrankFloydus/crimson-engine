////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/curve_asset.hpp"

#include <quader/modeling/curves/curve_document.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace quader {

/**
 * Pins immutable SDK-derived samples independently of the asset/document wrapper.
 * Failed sampling has no samples and retains its error under the same cache key.
 */
struct CurveSamplePayload final {
  modeling::CurveContentIdentity content_identity{};
  std::uint64_t content_revision = 0;
  modeling::CurveSamplingOptions options{};
  std::vector<modeling::CurveSample> samples;
  std::string error_message;
};

/**
 * Returns cached samples or an error for the asset's authored content and options.
 * The O(1) cache check precedes sampling/allocation. Concurrent sampling is safe;
 * callers must exclude concurrent changes to the asset's authored document.
 */
[[nodiscard]] std::shared_ptr<const CurveSamplePayload>
curve_sample_payload(const CurveAsset &asset,
                     modeling::CurveSamplingOptions options = {});

/**
 * Reuses the same sample cache for transient tool documents without an asset.
 * Access cached through this helper while sampling concurrently; document edits,
 * cache replacement, and destruction require external synchronization.
 */
[[nodiscard]] std::shared_ptr<const CurveSamplePayload>
curve_sample_payload(const modeling::CurveDocument &document,
                     std::shared_ptr<const CurveSamplePayload> &cached,
                     modeling::CurveSamplingOptions options = {});

} // namespace quader
