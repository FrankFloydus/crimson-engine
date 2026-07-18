////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace quader {

class QuaderEditorApi;

namespace test {

/// Identifies one independently comparable whole-editor behavior domain.
enum class CommandObservableDomain : std::uint32_t {
  Documents = 1u << 0u,
  Scene = 1u << 1u,
  Geometry = 1u << 2u,
  Selection = 1u << 3u,
  Overlay = 1u << 4u,
  Assets = 1u << 5u,
  History = 1u << 6u,
  Tools = 1u << 7u,
  Viewports = 1u << 8u,
  Preferences = 1u << 9u,
  Revisions = 1u << 10u,
};

using CommandObservableDomainMask = std::uint32_t;

inline constexpr std::size_t kCommandObservableDomainCount = 11u;

inline constexpr CommandObservableDomainMask kAllCommandObservableDomains =
    (1u << kCommandObservableDomainCount) - 1u;

/// Returns the bit represented by one observable domain.
[[nodiscard]] constexpr CommandObservableDomainMask
command_observable_domain_mask(CommandObservableDomain domain) {
  return static_cast<CommandObservableDomainMask>(domain);
}

/// Combines two observable domains into one comparison mask.
[[nodiscard]] constexpr CommandObservableDomainMask
operator|(CommandObservableDomain left, CommandObservableDomain right) {
  return command_observable_domain_mask(left) |
         command_observable_domain_mask(right);
}

/// Stores the stable digest and deterministic, line-oriented source payload for one domain.
struct CommandObservableDomainState final {
  std::string fingerprint;
  std::string canonical;

  bool operator==(const CommandObservableDomainState &) const = default;
};

/// Captures all public editor state used by command behavior-lock tests.
struct CommandObservableState final {
  std::array<CommandObservableDomainState, kCommandObservableDomainCount>
      domains;

  [[nodiscard]] const CommandObservableDomainState &
  domain(CommandObservableDomain value) const;
  [[nodiscard]] std::string combined_fingerprint() const;

  bool operator==(const CommandObservableState &) const = default;
};

/// Describes which behavior domains changed and the first readable mismatch in each domain.
struct CommandObservableStateDiff final {
  CommandObservableDomainMask changed_domains = 0u;
  std::string summary;

  [[nodiscard]] bool empty() const { return changed_domains == 0u; }
  [[nodiscard]] bool changed(CommandObservableDomain domain) const {
    return (changed_domains & command_observable_domain_mask(domain)) != 0u;
  }
};

/// Returns the stable presentation name used by fingerprints and diff output.
[[nodiscard]] std::string_view
command_observable_domain_name(CommandObservableDomain domain);

/// Captures a deterministic whole-editor snapshot through public APIs only.
[[nodiscard]] CommandObservableState
capture_command_observable_state(const QuaderEditorApi &editor);

/// Compares two snapshots, optionally limiting comparison to selected domains.
[[nodiscard]] CommandObservableStateDiff diff_command_observable_state(
    const CommandObservableState &before, const CommandObservableState &after,
    CommandObservableDomainMask domains = kAllCommandObservableDomains);

} // namespace test
} // namespace quader
