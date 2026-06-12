module;

#include <optional>
#include <string>

export module quader.render.startup_validation;

export import quader.render.status;

export namespace quader {

[[nodiscard]] std::optional<std::string> describe_renderer_startup_failure(const RendererStatus& status);
void throw_if_renderer_failed_to_start(const RendererStatus& status);

} // namespace quader
