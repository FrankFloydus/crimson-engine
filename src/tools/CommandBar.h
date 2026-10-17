#pragma once
#include "editor/CommandConsole.h"
#include <array>

namespace crimson {
struct CommandBarFrame {
    std::optional<ConsoleCommand> command;
    std::string argument;
    bool inputActive{};
};
class CommandBar {
public:
    // Draw before the main DockSpace. ImGui reserves a bottom main-viewport
    // sidebar so docking and viewport panels cannot cover the command input.
    CommandBarFrame draw();
    void setStatus(std::string value){console_.setStatus(std::move(value));}
    void setEffects(std::vector<ConsoleEffect> effects){console_.setEffects(std::move(effects));}
    void releaseFocus();
private:
    CommandConsole console_;
    std::array<char,1024> input_{};
    unsigned inputId_{};
    bool completionsVisible_{};
    void requestFocus();
};
}
