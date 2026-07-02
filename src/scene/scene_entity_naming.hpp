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

/**
 * Owns Scene Entity Naming behavior within the scene graph document layer.
 */
class SceneEntityNaming {
public:
  static std::string next_name(const SceneDocument &document,
                               std::string_view base_name) {
    const std::string base(base_name.empty() ? "Entity" : base_name);
    if (!document_has_name(document, base)) {
      return base;
    }

    for (std::size_t index = 1;; ++index) {
      std::string candidate = base + "_" + std::to_string(index);
      if (!document_has_name(document, candidate)) {
        return candidate;
      }
    }
  }

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

private:
  static bool document_has_name(const SceneDocument &document,
                                const std::string &name) {
    for (const EntityRecord &record : document.entity_records()) {
      if (record.name == name) {
        return true;
      }
    }
    return false;
  }
};

/**
 * Allocates unique Scene Entity names for batch creation without repeatedly scanning the document.
 */
class SceneEntityNameAllocator {
public:
    explicit SceneEntityNameAllocator(const SceneDocument& document) {
      const std::vector<EntityRecord> records = document.entity_records();
      names_.reserve(records.size());
      for (const EntityRecord &record : records) {
        reserve_name(record.name);
      }
    }

    void reserve_name(std::string_view name) {
      if (!name.empty()) {
        names_.insert(std::string(name));
      }
    }

    std::string next_name(std::string_view base_name) {
      const std::string base(base_name.empty() ? "Entity" : base_name);
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
    std::unordered_set<std::string> names_;
    std::unordered_map<std::string, std::size_t> next_index_by_base_;
};

} // namespace quader
