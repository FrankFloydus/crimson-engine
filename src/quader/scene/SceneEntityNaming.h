#pragma once

#include "quader/scene/SceneDocument.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace quader {

enum class SceneEntityKind {
    Empty,
    Mesh,
    Light,
    Generic,
};

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

private:
    static bool documentHasName(const SceneDocument& document, const std::string& name) {
        for (const EntityRecord& record : document.entityRecords()) {
            if (record.name == name) {
                return true;
            }
        }
        return false;
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
};

} // namespace quader
