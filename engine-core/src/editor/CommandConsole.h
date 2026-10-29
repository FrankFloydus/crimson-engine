#pragma once
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace crimson {
enum class ConsoleCommand { ShowDebugAttributes,ApplyEffect,RemoveEffect };
struct ConsoleEffect {std::string id,name;};
struct ConsoleCommandResult {
    std::optional<ConsoleCommand> command;
    std::string status;
    std::string argument;
};
// UUIDs are exact identities; names must identify one effect in the catalog.
std::optional<std::string> resolveConsoleEffect(std::string_view,std::span<const ConsoleEffect>,std::string& diagnostic);
// Pure session state. The editor dispatches parsed commands to the active Play
// world; neither parsing nor history owns simulation or persisted settings.
class CommandConsole {
public:
    static constexpr std::size_t MaximumHistory=32;
    static ConsoleCommandResult parse(std::string_view);
    const std::string& text() const{return text_;}
    const std::string& status() const{return status_;}
    const std::vector<std::string>& history() const{return history_;}
    const std::string& argument() const{return argument_;}
    void setEffects(std::vector<ConsoleEffect> effects){effects_=std::move(effects);}
    void setText(std::string);
    void setStatus(std::string value){status_=std::move(value);}
    std::vector<std::string> completions() const;
    bool acceptCompletion(std::size_t index=0);
    bool previousHistory();
    bool nextHistory();
    std::optional<ConsoleCommand> submit();
private:
    std::string text_,draft_,status_,argument_;
    std::vector<ConsoleEffect> effects_;
    std::vector<std::string> history_;
    std::optional<std::size_t> historyPosition_;
};
}
