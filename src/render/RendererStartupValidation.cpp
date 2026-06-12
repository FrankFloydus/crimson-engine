module;

#include <optional>
#include <stdexcept>
#include <string>

module quader.render.startup_validation;

namespace quader {
namespace {

std::string readable_backend_name(const RendererStatus& status)
{
    return status.backend_name.empty() ? "Unavailable" : status.backend_name;
}

std::string readable_failure_detail(const RendererStatus& status)
{
    return status.message.empty()
        ? "Renderer did not provide a failure message."
        : status.message;
}

} // namespace

std::optional<std::string> describe_renderer_startup_failure(const RendererStatus& status)
{
    if (status.initialized) {
        return std::nullopt;
    }

    return "Render engine failed to initialize (" +
        readable_backend_name(status) +
        "): " +
        readable_failure_detail(status);
}

void throw_if_renderer_failed_to_start(const RendererStatus& status)
{
    if (const std::optional<std::string> failure = describe_renderer_startup_failure(status)) {
        throw std::runtime_error(*failure);
    }
}

} // namespace quader
