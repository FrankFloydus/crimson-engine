module;

#include <string_view>

export module quader.platform.fatal_error_dialog;

export namespace quader {

void show_fatal_error_dialog(std::string_view exact_message);

} // namespace quader
