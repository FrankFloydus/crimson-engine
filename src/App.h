#pragma once

#include <cstdint>
#include <string>

namespace crimson {

struct AppOptions {
    uint32_t width{1280};
    uint32_t height{800};
    uint32_t frameLimit{0};
    uint32_t resizeWidth{0};
    uint32_t resizeHeight{0};
    bool hidden{false};
    bool windowCycle{false};
    bool editorSmoke{false};
    bool iblSmoke{false};
    bool modelSmoke{false};
    float uiScale{0.0f}; // Zero follows Windows; explicit values are for DPI regression checks.
    std::string capture;
    std::string scenario{"default"};
    std::string projectRoot; // Optional isolated project for focused verification.
    std::string frameProfile; // Optional user-driven frame report; no automatic input.
};

AppOptions parseOptions(int argc, char** argv);
int run(const AppOptions& options);

} // namespace crimson
