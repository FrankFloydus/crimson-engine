module;

#include <vulkan/vulkan.h>

export module quader.render.vulkan_common;

export namespace quader {

constexpr uint32_t kVulkanApiVersion = VK_API_VERSION_1_2;

void check_vk(VkResult result, const char* action);
void check_vk_result_for_imgui(VkResult result);

} // namespace quader
