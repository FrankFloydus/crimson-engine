#pragma once

#include "quader/ui/ConsoleModel.h"

namespace quader {

class ConsolePanel {
public:
    void draw();

private:
    ConsoleFilter filter_;
    bool autoScroll_ = true;
};

} // namespace quader
