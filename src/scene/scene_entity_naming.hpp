////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "scene/scene_document.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace quader {

/**
 * Enumerates SceneEntityKind values used by the scene layer.
 */
enum class SceneEntityKind {
    Empty,
    Mesh,
    Light,
    Generic,
};

/// Selects literal uniqueness or sequential numeric families for duplication.
enum class SceneEntityNamePolicy {
  Unique,
  DuplicateFamily,
};

/**
 * Owns Scene Entity Naming behavior within the scene graph document layer.
 */
class SceneEntityNaming {
public:
  static std::string next_name(const SceneDocument &document,
                               std::string_view base_name,
                               SceneEntityNamePolicy policy =
                                   SceneEntityNamePolicy::Unique);

  static std::string next_name_for_kind(const SceneDocument &document,
                                        SceneEntityKind kind) {
    return next_name(document, base_name_for_kind(kind));
  }

  static std::string base_name_for_kind(SceneEntityKind kind) {
    switch (kind) {
    case SceneEntityKind::Empty:
      return "Empty";
    case SceneEntityKind::Mesh:
      return "Mesh";
    case SceneEntityKind::Light:
      return "Light";
    case SceneEntityKind::Generic:
      return "Entity";
    }
    return "Entity";
  }

};

/**
 * Allocates unique Scene Entity names for batch creation without repeatedly scanning the document.
 */
class SceneEntityNameAllocator {
public:
    explicit SceneEntityNameAllocator(
        const SceneDocument &document,
        SceneEntityNamePolicy policy = SceneEntityNamePolicy::Unique)
        : policy_(policy) {
      const std::vector<EntityRecord> records = document.entity_records();
      names_.reserve(records.size());
      for (const EntityRecord &record : records) {
        reserve_name(record.name);
      }
    }

    void reserve_name(std::string_view name) {
      if (!name.empty()) {
        names_.insert(std::string(name));
        if (policy_ == SceneEntityNamePolicy::DuplicateFamily) {
          const NameFamily family = name_family(name);
          if (!family.number.empty()) {
            std::string &maximum = maximum_by_family_[std::string(family.stem)];
            if (family.number.size() > maximum.size() ||
                (family.number.size() == maximum.size() &&
                 family.number > maximum)) {
              maximum = family.number;
            }
          }
        }
      }
    }

    std::string next_name(std::string_view base_name) {
      const std::string base(base_name.empty() ? "Entity" : base_name);
      if (policy_ == SceneEntityNamePolicy::DuplicateFamily) {
        return next_duplicate_name(base);
      }
      if (names_.insert(base).second) {
        next_index_by_base_.try_emplace(base, 1u);
        return base;
      }

      std::size_t &next_index = next_index_by_base_[base];
      next_index = std::max<std::size_t>(next_index, 1u);
      for (std::size_t index = next_index;; ++index) {
        std::string candidate = base + "_" + std::to_string(index);
        if (names_.insert(candidate).second) {
          next_index = index + 1u;
          return candidate;
        }
      }
    }

    std::string next_name_for_kind(SceneEntityKind kind) {
      return next_name(SceneEntityNaming::base_name_for_kind(kind));
    }

private:
    /// Views the retained stem and normalized decimal suffix of one scene name.
    struct NameFamily {
      std::string_view stem;
      std::string_view number;
    };

    static NameFamily name_family(std::string_view name) {
      const std::size_t separator = name.rfind('_');
      if (separator == std::string_view::npos || separator == 0u ||
          separator + 1u == name.size()) {
        return {.stem = name, .number = {}};
      }
      std::string_view number = name.substr(separator + 1u);
      if (!std::all_of(number.begin(), number.end(),
                       [](char digit) { return digit >= '0' && digit <= '9'; })) {
        return {.stem = name, .number = {}};
      }
      const std::size_t first_digit = number.find_first_not_of('0');
      number.remove_prefix(first_digit == std::string_view::npos
                               ? number.size() - 1u
                               : first_digit);
      return {.stem = name.substr(0u, separator), .number = number};
    }

    std::string next_duplicate_name(const std::string &source_name) {
      // Sources may be snapshots from outside the current document. Their
      // suffix still contributes to the family maximum before allocation.
      reserve_name(source_name);
      const NameFamily family = name_family(source_name);
      std::string &maximum = maximum_by_family_[std::string(family.stem)];
      if (maximum.empty()) {
        maximum = "0";
      }
      // Decimal strings avoid overflow for arbitrarily long authored suffixes.
      std::size_t digit = maximum.size();
      while (digit > 0u && maximum[digit - 1u] == '9') {
        maximum[--digit] = '0';
      }
      if (digit == 0u) {
        maximum.insert(0u, 1u, '1');
      } else {
        ++maximum[digit - 1u];
      }
      std::string candidate(family.stem);
      candidate += '_';
      if (maximum.size() < 3u) {
        candidate.append(3u - maximum.size(), '0');
      }
      candidate += maximum;
      names_.insert(candidate);
      return candidate;
    }

    SceneEntityNamePolicy policy_;
    std::unordered_set<std::string> names_;
    std::unordered_map<std::string, std::size_t> next_index_by_base_;
    std::unordered_map<std::string, std::string> maximum_by_family_;
};

inline std::string SceneEntityNaming::next_name(
    const SceneDocument &document, std::string_view base_name,
    SceneEntityNamePolicy policy) {
  SceneEntityNameAllocator names(document, policy);
  return names.next_name(base_name);
}

} // namespace quader
