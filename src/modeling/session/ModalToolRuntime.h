#pragma once

#include "modeling/ModelingSessionService.h"

namespace quader {

class ModalToolRuntime {
public:
    [[nodiscard]] static bool ownsViewport(ModelingViewportTool tool);
};

} // namespace quader
