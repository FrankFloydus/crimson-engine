module;

#include <string>
#include <string_view>

export module quader.diagnostics.fatal_diagnostics;

export namespace quader {

void install_fatal_diagnostics() noexcept;
void record_fatal_operation(std::string_view operation);
[[nodiscard]] std::string last_fatal_operation();

} // namespace quader
