////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/project_asset_id.hpp"

#include <stduuid/uuid.h>

#include <algorithm>
#include <array>
#include <random>
#include <string>

namespace quader {
namespace {

std::mt19937 &project_asset_uuid_random_engine() {
  thread_local std::mt19937 engine = [] {
    std::random_device random_device;
    std::array<std::uint32_t, 8> seed_data{};
    for (std::uint32_t &value : seed_data) {
      value = random_device();
    }
    std::seed_seq seed(seed_data.begin(), seed_data.end());
    return std::mt19937(seed);
  }();
  return engine;
}

} // namespace

ProjectAssetId::ProjectAssetId(std::string_view normalized) {
  std::copy(normalized.begin(), normalized.end(), value_.begin());
}

ProjectAssetId ProjectAssetId::generate() {
  uuids::uuid_random_generator generator{project_asset_uuid_random_engine()};
  return ProjectAssetId(uuids::to_string(generator()));
}

std::optional<ProjectAssetId> ProjectAssetId::parse(std::string_view text) {
  const std::optional<uuids::uuid> parsed = uuids::uuid::from_string(text);
  if (!parsed.has_value()) {
    return std::nullopt;
  }
  return ProjectAssetId(uuids::to_string(*parsed));
}

ProjectAssetId
ProjectAssetId::from_bytes(const std::array<std::byte, 16> &bytes) {
  std::array<std::uint8_t, 16> values{};
  std::transform(bytes.begin(), bytes.end(), values.begin(),
                 [](std::byte value) {
                   return std::to_integer<std::uint8_t>(value);
                 });
  return ProjectAssetId(uuids::to_string(uuids::uuid(values)));
}

std::array<std::byte, 16> ProjectAssetId::bytes() const {
  std::array<std::byte, 16> result{};
  const std::optional<uuids::uuid> parsed = uuids::uuid::from_string(str());
  if (!parsed.has_value()) {
    return result;
  }
  const auto values = parsed->as_bytes();
  std::copy(values.begin(), values.end(), result.begin());
  return result;
}

} // namespace quader
