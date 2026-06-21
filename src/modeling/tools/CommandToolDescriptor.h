#pragma once

#include "modeling/ModelingActionCatalog.h"
#include "modeling/tools/CommandToolKind.h"

#include <string_view>

namespace quader {

struct CommandToolDescriptor {
    std::string_view id;
    std::string_view operationId;
    std::string_view label;
    CommandToolKind kind = CommandToolKind::Instant;
    ModelingActionPreviewKind previewKind = ModelingActionPreviewKind::None;
    bool rebuildsMeshOnSuccess = true;
};

} // namespace quader
