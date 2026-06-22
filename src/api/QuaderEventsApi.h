#pragma once

#include <string>
#include <vector>

namespace quader {

class QuaderEventsApi final {
public:
    [[nodiscard]] bool available() const { return false; }
    [[nodiscard]] std::vector<std::string> eventNames() const { return {}; }
};

} // namespace quader
