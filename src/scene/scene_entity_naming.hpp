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
    static std::string nextName(const SceneDocument& document, std::string_view baseName) {
        const std::string base(baseName.empty() ? "Entity" : baseName);
        if (!documentHasName(document, base)) {
            return base;
        }

        for (std::size_t index = 1;; ++index) {
            std::string candidate = base + "_" + std::to_string(index);
            if (!documentHasName(document, candidate)) {
                return candidate;
            }
        }
    }

    static std::string nextNameForKind(const SceneDocument& document, SceneEntityKind kind) {
        return nextName(document, baseNameForKind(kind));
    }

    static std::string baseNameForKind(SceneEntityKind kind) {
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
    static bool documentHasName(const SceneDocument& document, const std::string& name) {
        for (const EntityRecord& record : document.entityRecords()) {
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
        const std::vector<EntityRecord> records = document.entityRecords();
        names_.reserve(records.size());
        for (const EntityRecord& record : records) {
            reserveName(record.name);
        }
    }

    void reserveName(std::string_view name) {
        if (!name.empty()) {
            names_.insert(std::string(name));
        }
    }

    std::string nextName(std::string_view baseName) {
        const std::string base(baseName.empty() ? "Entity" : baseName);
        if (names_.insert(base).second) {
            nextIndexByBase_.try_emplace(base, 1u);
            return base;
        }

        std::size_t& nextIndex = nextIndexByBase_[base];
        nextIndex = std::max<std::size_t>(nextIndex, 1u);
        for (std::size_t index = nextIndex;; ++index) {
            std::string candidate = base + "_" + std::to_string(index);
            if (names_.insert(candidate).second) {
                nextIndex = index + 1u;
                return candidate;
            }
        }
    }

    std::string nextNameForKind(SceneEntityKind kind) {
        return nextName(SceneEntityNaming::baseNameForKind(kind));
    }

private:
    std::unordered_set<std::string> names_;
    std::unordered_map<std::string, std::size_t> nextIndexByBase_;
};

} // namespace quader
