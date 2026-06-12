module;

#include <stdexcept>
#include <string>

#include "diagnostics/LogMacros.h"

module quader.render.vulkan_common;

import quader.diagnostics.logger;

namespace quader {

void check_vk(VkResult result, const char* action)
{
    if (result == VK_SUCCESS) {
        return;
    }

    throw std::runtime_error(std::string(action) + " failed with VkResult " + std::to_string(result));
}

void check_vk_result_for_imgui(VkResult result)
{
    if (result == VK_SUCCESS) {
        return;
    }

    const std::string message =
        "Dear ImGui Vulkan backend reported VkResult " + std::to_string(result);

    if (result < 0) {
        QUADER_LOG_ERROR("{}", message);
        throw std::runtime_error(message);
    }

    QUADER_LOG_WARN("{}", message);
}

} // namespace quader
