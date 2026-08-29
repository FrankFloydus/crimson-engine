////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/curve_asset_payload.hpp"

#include <atomic>
#include <bit>
#include <utility>

namespace quader {
namespace {

[[nodiscard]] bool matches_document(const CurveSamplePayload &payload,
                                   const modeling::CurveDocument *document) noexcept {
  return payload.content_identity ==
             (document != nullptr ? document->content_identity()
                                  : modeling::CurveContentIdentity{}) &&
         payload.content_revision ==
             (document != nullptr ? document->content_revision() : 0u);
}

[[nodiscard]] bool matches_options(modeling::CurveSamplingOptions left,
                                  modeling::CurveSamplingOptions right) noexcept {
  // Bit comparison also retains errors for an unchanged NaN tolerance.
  return std::bit_cast<std::uint64_t>(left.tolerance) ==
             std::bit_cast<std::uint64_t>(right.tolerance) &&
         left.max_depth == right.max_depth &&
         left.max_samples == right.max_samples;
}

[[nodiscard]] std::shared_ptr<const CurveSamplePayload> retained_payload(
    const modeling::CurveDocument *document,
    const std::shared_ptr<const CurveSamplePayload> &incoming,
    const std::shared_ptr<const CurveSamplePayload> &previous = {}) noexcept {
  auto payload = std::atomic_load_explicit(&incoming, std::memory_order_acquire);
  if (payload && matches_document(*payload, document)) {
    return payload;
  }
  payload = std::atomic_load_explicit(&previous, std::memory_order_acquire);
  return payload && matches_document(*payload, document) ? payload : nullptr;
}

[[nodiscard]] std::shared_ptr<const CurveSamplePayload> sample_payload(
    const modeling::CurveDocument *document,
    std::shared_ptr<const CurveSamplePayload> &cached,
    modeling::CurveSamplingOptions options) {
  auto previous = std::atomic_load_explicit(&cached, std::memory_order_acquire);
  const auto matches = [&](const std::shared_ptr<const CurveSamplePayload> &item) {
    return item && matches_document(*item, document) &&
           matches_options(item->options, options);
  };
  if (matches(previous)) {
    return previous;
  }

  auto payload = std::make_shared<CurveSamplePayload>();
  payload->options = options;
  if (document != nullptr) {
    payload->content_identity = document->content_identity();
    payload->content_revision = document->content_revision();
    auto sampled = document->sample(options);
    if (sampled.ok()) {
      payload->samples = std::move(sampled).value();
    } else {
      payload->error_message = std::move(sampled).error().message;
    }
  } else {
    payload->error_message = "Curve asset has no authored document.";
  }

  const std::shared_ptr<const CurveSamplePayload> published = std::move(payload);
  for (;;) {
    if (matches(previous)) {
      return previous;
    }
    if (std::atomic_compare_exchange_weak_explicit(
            &cached, &previous, published, std::memory_order_acq_rel,
            std::memory_order_acquire)) {
      return published;
    }
  }
}

} // namespace

CurveAsset::CurveAsset(const CurveAsset &other)
    : AssetDocumentBase<CurveAssetRevision>(other), document(other.document),
      sample_payload_(retained_payload(document.get(), other.sample_payload_)) {}

CurveAsset::CurveAsset(CurveAsset &&other) noexcept
    : AssetDocumentBase<CurveAssetRevision>(std::move(other)),
      document(std::move(other.document)),
      sample_payload_(retained_payload(document.get(), other.sample_payload_)) {}

CurveAsset &CurveAsset::operator=(const CurveAsset &other) {
  if (this != &other) {
    auto payload = retained_payload(other.document.get(), other.sample_payload_,
                                    sample_payload_);
    AssetDocumentBase<CurveAssetRevision>::operator=(other);
    document = other.document;
    std::atomic_store_explicit(&sample_payload_, std::move(payload),
                              std::memory_order_release);
  }
  return *this;
}

CurveAsset &CurveAsset::operator=(CurveAsset &&other) noexcept {
  if (this != &other) {
    auto payload = retained_payload(other.document.get(), other.sample_payload_,
                                    sample_payload_);
    AssetDocumentBase<CurveAssetRevision>::operator=(std::move(other));
    document = std::move(other.document);
    std::atomic_store_explicit(&sample_payload_, std::move(payload),
                              std::memory_order_release);
  }
  return *this;
}

std::shared_ptr<const CurveSamplePayload>
curve_sample_payload(const CurveAsset &asset,
                     modeling::CurveSamplingOptions options) {
  return sample_payload(asset.document.get(), asset.sample_payload_, options);
}

std::shared_ptr<const CurveSamplePayload>
curve_sample_payload(const modeling::CurveDocument &document,
                     std::shared_ptr<const CurveSamplePayload> &cached,
                     modeling::CurveSamplingOptions options) {
  return sample_payload(&document, cached, options);
}

} // namespace quader
