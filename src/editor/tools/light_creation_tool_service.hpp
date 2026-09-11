////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/command_result.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace quader {

/** Identifies the editor-owned one-shot light placement operation. */
enum class LightCreationKind {
  Point,
  Directional,
};

/** Identifies one exact armed light-placement operation. */
struct LightCreationOperation final {
  LightCreationKind kind = LightCreationKind::Point;
  std::uint64_t generation = 0;

  friend bool operator==(const LightCreationOperation &,
                         const LightCreationOperation &) = default;
};

inline constexpr std::string_view kCreatePointLightToolId =
    "create_point_light";
inline constexpr std::string_view kCreateDirectionalLightToolId =
    "create_directional_light";

/**
 * Owns the armed kind for one-shot light placement.
 */
class LightCreationToolService final {
public:
  [[nodiscard]] bool active() const { return active_kind_.has_value(); }
  [[nodiscard]] std::optional<LightCreationKind> active_kind() const {
    return active_kind_;
  }
  [[nodiscard]] std::optional<LightCreationOperation> active_operation() const {
    if (!active_kind_.has_value()) {
      return std::nullopt;
    }
    return LightCreationOperation{*active_kind_, generation_};
  }
  [[nodiscard]] std::string_view active_tool_id() const;

  CommandResult begin(LightCreationKind kind);
  CommandResult cancel();
  CommandResult complete(LightCreationOperation operation);
  void reset();

private:
  std::optional<LightCreationKind> active_kind_;
  std::uint64_t generation_ = 0;
};

} // namespace quader
