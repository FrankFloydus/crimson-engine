#include <gtest/gtest.h>
#include <vulkan/vulkan.h>

#include <cstdint>

import quader.render.vulkan_device;

namespace {

template <typename Handle>
Handle fake_vulkan_handle(std::uintptr_t value)
{
    return reinterpret_cast<Handle>(value);
}

quader::VulkanQueueContext valid_queue_context(std::uintptr_t queue, uint32_t queue_index)
{
    return quader::VulkanQueueContext {
        .queue = fake_vulkan_handle<VkQueue>(queue),
        .family_index = 2,
        .queue_index = queue_index,
    };
}

quader::VulkanSharedDeviceContext valid_shared_device_context()
{
    return quader::VulkanSharedDeviceContext {
        .instance = fake_vulkan_handle<VkInstance>(1),
        .physical_device = fake_vulkan_handle<VkPhysicalDevice>(2),
        .device = fake_vulkan_handle<VkDevice>(3),
        .allocator = nullptr,
        .pipeline_cache = VK_NULL_HANDLE,
        .app_graphics_queue = valid_queue_context(5, 0),
        .renderer_graphics_queue = valid_queue_context(6, 1),
        .graphics_queue_family_available_count = 2,
        .created_graphics_queue_count = 2,
        .queue_mode = quader::VulkanSharedQueueMode::dedicated_renderer_queue,
        .owns_instance = true,
        .owns_device = true,
        .debug_utils_supported = false,
        .debug_markers_supported = false,
        .multiview_supported = false,
    };
}

} // namespace

TEST(VulkanDeviceContext, SharedDeviceContextRequiresBorrowedDeviceAndQueueHandles)
{
    quader::VulkanSharedDeviceContext context = valid_shared_device_context();
    EXPECT_TRUE(context.is_valid());

    context.renderer_graphics_queue.queue = VK_NULL_HANDLE;
    EXPECT_FALSE(context.is_valid());
}

TEST(VulkanDeviceContext, SharedDeviceContextRequiresCreatedQueuesToFitSelectedFamily)
{
    quader::VulkanSharedDeviceContext context = valid_shared_device_context();

    context.created_graphics_queue_count = 3;
    EXPECT_FALSE(context.is_valid());
}

TEST(VulkanDeviceContext, SharedDeviceContextReportsQueueSerializationRequirement)
{
    quader::VulkanSharedDeviceContext context = valid_shared_device_context();
    EXPECT_FALSE(context.requires_queue_serialization());

    context.queue_mode = quader::VulkanSharedQueueMode::serialized_single_queue;
    EXPECT_TRUE(context.requires_queue_serialization());
}
