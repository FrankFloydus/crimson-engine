module;

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

module quader.render.vulkan_viewport_image_ring;

import quader.render.vulkan_common;

namespace quader {
namespace {

constexpr uint64_t kFenceWaitForever = std::numeric_limits<uint64_t>::max();

uint64_t next_ring_resource_id()
{
    static uint64_t next_id = 1;
    return next_id++;
}

bool same_extent(RenderExtent left, RenderExtent right) noexcept
{
    return left.width == right.width && left.height == right.height;
}

VkExtent3D extent_3d_from(RenderExtent extent)
{
    return VkExtent3D {
        .width = extent.width,
        .height = extent.height,
        .depth = 1,
    };
}

VkExtent2D extent_2d_from(RenderExtent extent)
{
    return VkExtent2D {
        .width = extent.width,
        .height = extent.height,
    };
}

VkImageSubresourceRange color_subresource_range()
{
    return VkImageSubresourceRange {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
}

VkImageSubresourceRange depth_subresource_range(VkFormat depth_format)
{
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (depth_format == VK_FORMAT_D24_UNORM_S8_UINT || depth_format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
        aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }

    return VkImageSubresourceRange {
        .aspectMask = aspect,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
}

bool format_supports(
    VkPhysicalDevice physical_device,
    VkFormat format,
    VkFormatFeatureFlags required_features)
{
    VkFormatProperties properties = {};
    vkGetPhysicalDeviceFormatProperties(physical_device, format, &properties);
    return (properties.optimalTilingFeatures & required_features) == required_features;
}

VkFormat choose_depth_format(VkPhysicalDevice physical_device, VkFormat requested_format)
{
    if (requested_format != VK_FORMAT_UNDEFINED) {
        if (!format_supports(
                physical_device,
                requested_format,
                VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
            throw std::runtime_error("Requested viewport depth format is not supported as a Vulkan depth attachment.");
        }

        return requested_format;
    }

    constexpr VkFormat candidates[] {
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
    };

    for (VkFormat candidate : candidates) {
        if (format_supports(
                physical_device,
                candidate,
                VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
            return candidate;
        }
    }

    throw std::runtime_error("No supported Vulkan depth format was found for viewport rendering.");
}

void verify_color_format(VkPhysicalDevice physical_device, VkFormat color_format)
{
    constexpr VkFormatFeatureFlags required_features =
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
        VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if (!format_supports(physical_device, color_format, required_features)) {
        throw std::runtime_error("Viewport color format is not supported for color attachment, transfer source, and sampling.");
    }
}

uint32_t find_memory_type(
    VkPhysicalDevice physical_device,
    uint32_t type_filter,
    VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memory_properties = {};
    vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);

    for (uint32_t index = 0; index < memory_properties.memoryTypeCount; ++index) {
        const bool type_is_allowed = (type_filter & (1u << index)) != 0;
        const bool type_has_properties =
            (memory_properties.memoryTypes[index].propertyFlags & properties) == properties;

        if (type_is_allowed && type_has_properties) {
            return index;
        }
    }

    throw std::runtime_error("No Vulkan memory type satisfies the viewport image allocation.");
}

struct VulkanOwnedImage {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

void destroy_owned_image(
    VkDevice device,
    const VkAllocationCallbacks* allocator,
    VulkanOwnedImage& image)
{
    if (image.view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, image.view, allocator);
        image.view = VK_NULL_HANDLE;
    }

    if (image.image != VK_NULL_HANDLE) {
        vkDestroyImage(device, image.image, allocator);
        image.image = VK_NULL_HANDLE;
    }

    if (image.memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, image.memory, allocator);
        image.memory = VK_NULL_HANDLE;
    }

    image.layout = VK_IMAGE_LAYOUT_UNDEFINED;
}

VulkanOwnedImage create_owned_image(
    const VulkanSharedDeviceContext& context,
    RenderExtent extent,
    VkFormat format,
    VkImageUsageFlags usage,
    VkImageAspectFlags aspect,
    VkImageLayout initial_layout = VK_IMAGE_LAYOUT_UNDEFINED)
{
    VulkanOwnedImage image;

    VkImageCreateInfo image_info = {};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent = extent_3d_from(extent);
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.format = format;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage = usage;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    try {
        check_vk(vkCreateImage(context.device, &image_info, context.allocator, &image.image), "vkCreateImage");

        VkMemoryRequirements memory_requirements = {};
        vkGetImageMemoryRequirements(context.device, image.image, &memory_requirements);

        VkMemoryAllocateInfo allocation_info = {};
        allocation_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation_info.allocationSize = memory_requirements.size;
        allocation_info.memoryTypeIndex = find_memory_type(
            context.physical_device,
            memory_requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check_vk(vkAllocateMemory(context.device, &allocation_info, context.allocator, &image.memory), "vkAllocateMemory");
        check_vk(vkBindImageMemory(context.device, image.image, image.memory, 0), "vkBindImageMemory");

        VkImageViewCreateInfo view_info = {};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = image.image;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = format;
        view_info.subresourceRange = VkImageSubresourceRange {
            .aspectMask = aspect,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        };
        check_vk(vkCreateImageView(context.device, &view_info, context.allocator, &image.view), "vkCreateImageView");
        image.layout = initial_layout;
        return image;
    } catch (...) {
        destroy_owned_image(context.device, context.allocator, image);
        throw;
    }
}

VkSampler create_viewport_sampler(const VulkanSharedDeviceContext& context)
{
    VkSamplerCreateInfo sampler_info = {};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.minLod = 0.0f;
    sampler_info.maxLod = 0.0f;
    sampler_info.maxAnisotropy = 1.0f;

    VkSampler sampler = VK_NULL_HANDLE;
    check_vk(vkCreateSampler(context.device, &sampler_info, context.allocator, &sampler), "vkCreateSampler");
    return sampler;
}

VkCommandPool create_command_pool(const VulkanSharedDeviceContext& context)
{
    VkCommandPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = context.renderer_graphics_queue.family_index;

    VkCommandPool command_pool = VK_NULL_HANDLE;
    check_vk(vkCreateCommandPool(context.device, &pool_info, context.allocator, &command_pool), "vkCreateCommandPool");
    return command_pool;
}

std::vector<VkCommandBuffer> allocate_command_buffers(
    VkDevice device,
    VkCommandPool command_pool,
    uint32_t count)
{
    std::vector<VkCommandBuffer> command_buffers(count, VK_NULL_HANDLE);
    VkCommandBufferAllocateInfo command_buffer_info = {};
    command_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_buffer_info.commandPool = command_pool;
    command_buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_buffer_info.commandBufferCount = count;
    check_vk(vkAllocateCommandBuffers(device, &command_buffer_info, command_buffers.data()), "vkAllocateCommandBuffers");
    return command_buffers;
}

VkFence create_fence(const VulkanSharedDeviceContext& context, VkFenceCreateFlags flags)
{
    VkFenceCreateInfo fence_info = {};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = flags;

    VkFence fence = VK_NULL_HANDLE;
    check_vk(vkCreateFence(context.device, &fence_info, context.allocator, &fence), "vkCreateFence");
    return fence;
}

void record_image_barrier(
    VkCommandBuffer command_buffer,
    VkImage image,
    VkImageLayout old_layout,
    VkImageLayout new_layout,
    VkAccessFlags source_access,
    VkAccessFlags destination_access,
    VkPipelineStageFlags source_stage,
    VkPipelineStageFlags destination_stage,
    VkImageSubresourceRange subresource_range)
{
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = subresource_range;
    barrier.srcAccessMask = source_access;
    barrier.dstAccessMask = destination_access;

    vkCmdPipelineBarrier(
        command_buffer,
        source_stage,
        destination_stage,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier);
}

void record_copy_image(
    VkCommandBuffer command_buffer,
    VkImage source,
    VkImage destination,
    RenderExtent extent)
{
    VkImageCopy copy_region = {};
    copy_region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy_region.srcSubresource.mipLevel = 0;
    copy_region.srcSubresource.baseArrayLayer = 0;
    copy_region.srcSubresource.layerCount = 1;
    copy_region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy_region.dstSubresource.mipLevel = 0;
    copy_region.dstSubresource.baseArrayLayer = 0;
    copy_region.dstSubresource.layerCount = 1;
    copy_region.extent = extent_3d_from(extent);

    vkCmdCopyImage(
        command_buffer,
        source,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        destination,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &copy_region);
}

VkAccessFlags access_for_sampled_layout(VkImageLayout layout)
{
    return layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ? VK_ACCESS_SHADER_READ_BIT : 0;
}

VkPipelineStageFlags stage_for_sampled_layout(VkImageLayout layout)
{
    return layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
        : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
}

} // namespace

struct ViewportGpuImageRingLedger::Slot {
    uint64_t image_generation = 0;
    ViewportGpuImageReadiness readiness = ViewportGpuImageReadiness::ready;
};

class ViewportGpuImageRingLedger::Impl {
public:
    Impl(
        ViewportId viewport_id,
        ViewportGeneration generation,
        RenderExtent extent,
        uint32_t image_count,
        uint64_t resource_id)
        : viewport_id(viewport_id)
        , generation(generation)
        , extent(extent)
        , resource_id(resource_id)
        , slots(image_count)
    {
        if (!viewport_id.is_valid() || !generation.is_valid() || !extent.is_valid() ||
            image_count < 2 || resource_id == 0) {
            throw std::invalid_argument("Viewport GPU image ring ledger requires valid viewport, generation, extent, image count, and resource id.");
        }
    }

    ViewportId viewport_id;
    ViewportGeneration generation;
    RenderExtent extent;
    uint64_t resource_id = 0;
    uint32_t next_slot = 0;
    std::vector<Slot> slots;
};

ViewportGpuImageRingLedger::ViewportGpuImageRingLedger(
    ViewportId viewport_id,
    ViewportGeneration generation,
    RenderExtent extent,
    uint32_t image_count,
    uint64_t resource_id)
    : impl_(std::make_unique<Impl>(viewport_id, generation, extent, image_count, resource_id))
{
}

ViewportGpuImageRingLedger::~ViewportGpuImageRingLedger() = default;

bool ViewportGpuImageRingLedger::matches(ViewportGeneration generation, RenderExtent extent) const noexcept
{
    return impl_->generation == generation && same_extent(impl_->extent, extent);
}

bool ViewportGpuImageRingLedger::owns(const ViewportGpuImageHandle& handle) const noexcept
{
    return slot_for(handle) != nullptr;
}

uint32_t ViewportGpuImageRingLedger::image_count() const noexcept
{
    return static_cast<uint32_t>(impl_->slots.size());
}

uint64_t ViewportGpuImageRingLedger::resource_id() const noexcept
{
    return impl_->resource_id;
}

std::optional<uint32_t> ViewportGpuImageRingLedger::try_acquire_ready_slot() noexcept
{
    for (uint32_t offset = 0; offset < impl_->slots.size(); ++offset) {
        const uint32_t slot_index = (impl_->next_slot + offset) % static_cast<uint32_t>(impl_->slots.size());
        Slot& slot = impl_->slots[slot_index];
        if (slot.readiness != ViewportGpuImageReadiness::rendering) {
            impl_->next_slot = (slot_index + 1) % static_cast<uint32_t>(impl_->slots.size());
            return slot_index;
        }
    }

    return std::nullopt;
}

ViewportGpuImageLease ViewportGpuImageRingLedger::begin_render(uint32_t slot_index)
{
    Slot* slot = slot_for(slot_index);
    if (slot == nullptr) {
        throw std::out_of_range("Viewport GPU image slot is outside the ring.");
    }

    if (slot->readiness == ViewportGpuImageReadiness::rendering) {
        throw std::logic_error("Viewport GPU image slot is already rendering.");
    }

    ++slot->image_generation;
    slot->readiness = ViewportGpuImageReadiness::rendering;

    return ViewportGpuImageLease {
        .slot_index = slot_index,
        .handle = ViewportGpuImageHandle {
            .viewport_id = impl_->viewport_id,
            .generation = impl_->generation,
            .slot_index = slot_index,
            .image_generation = slot->image_generation,
            .resource_id = impl_->resource_id,
            .extent = impl_->extent,
            .readiness = ViewportGpuImageReadiness::rendering,
        },
    };
}

std::optional<ViewportGpuImageHandle> ViewportGpuImageRingLedger::mark_ready(
    const ViewportGpuImageHandle& handle)
{
    Slot* slot = slot_for(handle);
    if (slot == nullptr) {
        return std::nullopt;
    }

    slot->readiness = ViewportGpuImageReadiness::ready;
    ViewportGpuImageHandle ready_handle = handle;
    ready_handle.readiness = ViewportGpuImageReadiness::ready;
    return ready_handle;
}

std::optional<ViewportGpuImageHandle> ViewportGpuImageRingLedger::ready_handle_for(
    const ViewportGpuImageHandle& handle) const
{
    const Slot* slot = slot_for(handle);
    if (slot == nullptr || slot->readiness != ViewportGpuImageReadiness::ready) {
        return std::nullopt;
    }

    ViewportGpuImageHandle ready_handle = handle;
    ready_handle.readiness = ViewportGpuImageReadiness::ready;
    return ready_handle;
}

ViewportGpuImageRingLedger::Slot* ViewportGpuImageRingLedger::slot_for(uint32_t slot_index) noexcept
{
    if (slot_index >= impl_->slots.size()) {
        return nullptr;
    }

    return &impl_->slots[slot_index];
}

const ViewportGpuImageRingLedger::Slot* ViewportGpuImageRingLedger::slot_for(uint32_t slot_index) const noexcept
{
    if (slot_index >= impl_->slots.size()) {
        return nullptr;
    }

    return &impl_->slots[slot_index];
}

ViewportGpuImageRingLedger::Slot* ViewportGpuImageRingLedger::slot_for(
    const ViewportGpuImageHandle& handle) noexcept
{
    return const_cast<Slot*>(std::as_const(*this).slot_for(handle));
}

const ViewportGpuImageRingLedger::Slot* ViewportGpuImageRingLedger::slot_for(
    const ViewportGpuImageHandle& handle) const noexcept
{
    if (!handle.is_valid() ||
        handle.viewport_id != impl_->viewport_id ||
        handle.generation != impl_->generation ||
        handle.resource_id != impl_->resource_id ||
        !same_extent(handle.extent, impl_->extent)) {
        return nullptr;
    }

    const Slot* slot = slot_for(handle.slot_index);
    if (slot != nullptr && slot->image_generation == handle.image_generation) {
        return slot;
    }

    return nullptr;
}

class VulkanViewportImageRing::Impl {
public:
    Impl(VulkanSharedDeviceContext context, VulkanViewportImageRingDesc desc)
        : context(std::move(context))
        , desc(desc)
        , depth_format(choose_depth_format(this->context.physical_device, desc.depth_format))
        , ledger(desc.viewport_id, desc.generation, desc.extent, desc.image_count, next_ring_resource_id())
    {
        if (!this->context.is_valid()) {
            throw std::invalid_argument("Vulkan viewport image ring requires a valid shared Vulkan device context.");
        }

        if (!desc.is_valid()) {
            throw std::invalid_argument("Vulkan viewport image ring requires a valid descriptor.");
        }

        verify_color_format(this->context.physical_device, desc.color_format);
        create_resources();
    }

    ~Impl()
    {
        wait_for_slots();
        destroy_resources();
    }

    struct Slot {
        VulkanOwnedImage render_color;
        VulkanOwnedImage sampled_color;
        VkCommandBuffer command_buffer = VK_NULL_HANDLE;
        VkFence ready_fence = VK_NULL_HANDLE;
        std::optional<ViewportGpuImageHandle> submitted_handle;
    };

    VulkanSharedDeviceContext context;
    VulkanViewportImageRingDesc desc;
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    ViewportGpuImageRingLedger ledger;
    VkSampler sampler = VK_NULL_HANDLE;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VulkanOwnedImage depth;
    std::vector<Slot> slots;
    std::vector<VkImage> render_color_images;

    [[nodiscard]] bool matches(ViewportGeneration generation, RenderExtent extent) const noexcept
    {
        return ledger.matches(generation, extent);
    }

    [[nodiscard]] std::optional<VulkanViewportRenderImage> acquire_next_render_image()
    {
        refresh_completed_slots(ViewportGpuResolveWait::poll);

        std::optional<uint32_t> slot_index = ledger.try_acquire_ready_slot();
        if (!slot_index.has_value()) {
            return std::nullopt;
        }

        ViewportGpuImageLease lease = ledger.begin_render(*slot_index);
        Slot& slot = slots[*slot_index];
        slot.submitted_handle = lease.handle;

        return VulkanViewportRenderImage {
            .slot_index = *slot_index,
            .color_image = slot.render_color.image,
            .depth_image = depth.image,
            .extent = desc.extent,
            .color_format = desc.color_format,
            .depth_format = depth_format,
        };
    }

    [[nodiscard]] std::optional<ViewportGpuImageHandle> mark_render_submitted(
        uint32_t slot_index,
        VkSemaphore finished_drawing)
    {
        if (slot_index >= slots.size()) {
            return std::nullopt;
        }

        Slot& slot = slots[slot_index];
        if (!slot.submitted_handle.has_value() || !ledger.owns(*slot.submitted_handle)) {
            return std::nullopt;
        }

        submit_copy_to_sampled_image(slot, finished_drawing);
        return slot.submitted_handle;
    }

    [[nodiscard]] std::optional<ViewportSampledGpuImage> resolve_sampled_image(
        const ViewportGpuImageHandle& handle,
        ViewportGpuResolveWait wait_policy)
    {
        if (!ledger.owns(handle)) {
            return std::nullopt;
        }

        Slot* slot = slot_for(handle);
        if (slot == nullptr) {
            return std::nullopt;
        }

        const VkResult fence_status = fence_status_for(*slot, wait_policy);
        if (fence_status == VK_NOT_READY) {
            return std::nullopt;
        }
        check_vk(fence_status, "vkGetFenceStatus");

        std::optional<ViewportGpuImageHandle> ready_handle = ledger.mark_ready(handle);
        if (!ready_handle.has_value()) {
            return std::nullopt;
        }

        return ViewportSampledGpuImage {
            .handle = *ready_handle,
            .image_view = slot->sampled_color.view,
            .sampler = sampler,
            .image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
    }

    [[nodiscard]] Slot* slot_for(const ViewportGpuImageHandle& handle)
    {
        for (Slot& slot : slots) {
            if (slot.submitted_handle.has_value() &&
                slot.submitted_handle->slot_index < slots.size() &&
                &slot == &slots[slot.submitted_handle->slot_index] &&
                slot.submitted_handle->slot_index == handle.slot_index &&
                slot.submitted_handle->image_generation == handle.image_generation &&
                slot.submitted_handle->resource_id == handle.resource_id) {
                return &slot;
            }
        }

        return nullptr;
    }

    void refresh_completed_slots(ViewportGpuResolveWait wait_policy)
    {
        for (Slot& slot : slots) {
            if (!slot.submitted_handle.has_value()) {
                continue;
            }

            if (!ledger.owns(*slot.submitted_handle)) {
                slot.submitted_handle.reset();
                continue;
            }

            const VkResult status = fence_status_for(slot, wait_policy);
            if (status == VK_NOT_READY) {
                continue;
            }
            check_vk(status, "vkGetFenceStatus");
            (void)ledger.mark_ready(*slot.submitted_handle);
        }
    }

    [[nodiscard]] VkResult fence_status_for(Slot& slot, ViewportGpuResolveWait wait_policy) const
    {
        if (wait_policy == ViewportGpuResolveWait::wait) {
            return vkWaitForFences(context.device, 1, &slot.ready_fence, VK_TRUE, kFenceWaitForever);
        }

        return vkGetFenceStatus(context.device, slot.ready_fence);
    }

    void create_resources()
    {
        slots.resize(desc.image_count);
        render_color_images.resize(desc.image_count, VK_NULL_HANDLE);
        sampler = create_viewport_sampler(context);
        command_pool = create_command_pool(context);

        depth = create_owned_image(
            context,
            desc.extent,
            depth_format,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            depth_subresource_range(depth_format).aspectMask);

        const std::vector<VkCommandBuffer> command_buffers =
            allocate_command_buffers(context.device, command_pool, desc.image_count);

        for (uint32_t index = 0; index < desc.image_count; ++index) {
            Slot& slot = slots[index];
            slot.command_buffer = command_buffers[index];
            slot.ready_fence = create_fence(context, VK_FENCE_CREATE_SIGNALED_BIT);
            slot.render_color = create_owned_image(
                context,
                desc.extent,
                desc.color_format,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                    VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            slot.sampled_color = create_owned_image(
                context,
                desc.extent,
                desc.color_format,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT);
            render_color_images[index] = slot.render_color.image;
        }
    }

    void wait_for_slots() const
    {
        std::vector<VkFence> fences;
        fences.reserve(slots.size());
        for (const Slot& slot : slots) {
            if (slot.ready_fence != VK_NULL_HANDLE) {
                fences.push_back(slot.ready_fence);
            }
        }

        if (!fences.empty()) {
            (void)vkWaitForFences(
                context.device,
                static_cast<uint32_t>(fences.size()),
                fences.data(),
                VK_TRUE,
                kFenceWaitForever);
        }
    }

    void destroy_resources()
    {
        for (Slot& slot : slots) {
            destroy_owned_image(context.device, context.allocator, slot.sampled_color);
            destroy_owned_image(context.device, context.allocator, slot.render_color);
            if (slot.ready_fence != VK_NULL_HANDLE) {
                vkDestroyFence(context.device, slot.ready_fence, context.allocator);
                slot.ready_fence = VK_NULL_HANDLE;
            }
            slot.command_buffer = VK_NULL_HANDLE;
            slot.submitted_handle.reset();
        }
        slots.clear();
        render_color_images.clear();

        destroy_owned_image(context.device, context.allocator, depth);

        if (sampler != VK_NULL_HANDLE) {
            vkDestroySampler(context.device, sampler, context.allocator);
            sampler = VK_NULL_HANDLE;
        }

        if (command_pool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(context.device, command_pool, context.allocator);
            command_pool = VK_NULL_HANDLE;
        }
    }

    void reset_slot_for_submit(Slot& slot)
    {
        check_vk(vkWaitForFences(context.device, 1, &slot.ready_fence, VK_TRUE, kFenceWaitForever), "vkWaitForFences");
        check_vk(vkResetFences(context.device, 1, &slot.ready_fence), "vkResetFences");
        check_vk(vkResetCommandBuffer(slot.command_buffer, 0), "vkResetCommandBuffer");
    }

    void submit_copy_to_sampled_image(Slot& slot, VkSemaphore finished_drawing)
    {
        reset_slot_for_submit(slot);

        VkCommandBufferBeginInfo begin_info = {};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check_vk(vkBeginCommandBuffer(slot.command_buffer, &begin_info), "vkBeginCommandBuffer");

        record_image_barrier(
            slot.command_buffer,
            slot.render_color.image,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            color_subresource_range());

        record_image_barrier(
            slot.command_buffer,
            slot.sampled_color.image,
            slot.sampled_color.layout,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            access_for_sampled_layout(slot.sampled_color.layout),
            VK_ACCESS_TRANSFER_WRITE_BIT,
            stage_for_sampled_layout(slot.sampled_color.layout),
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            color_subresource_range());

        record_copy_image(
            slot.command_buffer,
            slot.render_color.image,
            slot.sampled_color.image,
            desc.extent);

        record_image_barrier(
            slot.command_buffer,
            slot.sampled_color.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            color_subresource_range());

        record_image_barrier(
            slot.command_buffer,
            slot.render_color.image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_ACCESS_TRANSFER_READ_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            color_subresource_range());

        check_vk(vkEndCommandBuffer(slot.command_buffer), "vkEndCommandBuffer");

        slot.sampled_color.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkSubmitInfo submit_info = {};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &slot.command_buffer;
        const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        if (finished_drawing != VK_NULL_HANDLE) {
            submit_info.waitSemaphoreCount = 1;
            submit_info.pWaitSemaphores = &finished_drawing;
            submit_info.pWaitDstStageMask = &wait_stage;
        }

        check_vk(vkQueueSubmit(context.renderer_graphics_queue.queue, 1, &submit_info, slot.ready_fence), "vkQueueSubmit");
    }
};

VulkanViewportImageRing::VulkanViewportImageRing(
    VulkanSharedDeviceContext context,
    VulkanViewportImageRingDesc desc)
    : impl_(std::make_unique<Impl>(std::move(context), desc))
{
}

VulkanViewportImageRing::~VulkanViewportImageRing() = default;

bool VulkanViewportImageRing::matches(ViewportGeneration generation, RenderExtent extent) const noexcept
{
    return impl_->matches(generation, extent);
}

ViewportId VulkanViewportImageRing::viewport_id() const noexcept
{
    return impl_->desc.viewport_id;
}

ViewportGeneration VulkanViewportImageRing::generation() const noexcept
{
    return impl_->desc.generation;
}

uint64_t VulkanViewportImageRing::resource_id() const noexcept
{
    return impl_->ledger.resource_id();
}

RenderExtent VulkanViewportImageRing::extent() const noexcept
{
    return impl_->desc.extent;
}

VkFormat VulkanViewportImageRing::color_format() const noexcept
{
    return impl_->desc.color_format;
}

VkFormat VulkanViewportImageRing::depth_format() const noexcept
{
    return impl_->depth_format;
}

VkImage VulkanViewportImageRing::depth_image() const noexcept
{
    return impl_->depth.image;
}

std::span<const VkImage> VulkanViewportImageRing::color_images() const noexcept
{
    return impl_->render_color_images;
}

std::optional<VulkanViewportRenderImage> VulkanViewportImageRing::acquire_next_render_image()
{
    return impl_->acquire_next_render_image();
}

std::optional<ViewportGpuImageHandle> VulkanViewportImageRing::mark_render_submitted(
    uint32_t slot_index,
    VkSemaphore finished_drawing)
{
    return impl_->mark_render_submitted(slot_index, finished_drawing);
}

std::optional<ViewportSampledGpuImage> VulkanViewportImageRing::resolve_sampled_image(
    const ViewportGpuImageHandle& handle,
    ViewportGpuResolveWait wait_policy)
{
    return impl_->resolve_sampled_image(handle, wait_policy);
}

class VulkanViewportImageRingPool::Impl {
public:
    explicit Impl(VulkanSharedDeviceContext context)
        : context(std::move(context))
    {
        if (!this->context.is_valid()) {
            throw std::invalid_argument("Vulkan viewport image ring pool requires a valid shared Vulkan device context.");
        }
    }

    VulkanSharedDeviceContext context;
    std::unordered_map<ViewportId, std::unique_ptr<VulkanViewportImageRing>, ViewportIdHash> rings;
    std::vector<std::unique_ptr<VulkanViewportImageRing>> retired_rings;

    [[nodiscard]] VulkanViewportImageRing& ensure_ring(
        ViewportId viewport_id,
        ViewportGeneration generation,
        RenderExtent extent)
    {
        auto existing = rings.find(viewport_id);
        if (existing != rings.end() && existing->second->matches(generation, extent)) {
            return *existing->second;
        }

        auto ring = std::make_unique<VulkanViewportImageRing>(
            context,
            VulkanViewportImageRingDesc {
                .viewport_id = viewport_id,
                .generation = generation,
                .extent = extent,
            });
        VulkanViewportImageRing& ring_ref = *ring;
        if (existing != rings.end()) {
            retired_rings.push_back(std::move(existing->second));
            existing->second = std::move(ring);
        } else {
            rings.emplace(viewport_id, std::move(ring));
        }
        return ring_ref;
    }

    void release_viewport(ViewportId viewport_id)
    {
        const auto existing = rings.find(viewport_id);
        if (existing == rings.end()) {
            return;
        }

        retired_rings.push_back(std::move(existing->second));
        rings.erase(existing);
    }

    [[nodiscard]] bool has_retired_resources() const noexcept
    {
        return !retired_rings.empty();
    }

    void collect_retired_resources()
    {
        retired_rings.clear();
    }
};

VulkanViewportImageRingPool::VulkanViewportImageRingPool(VulkanSharedDeviceContext context)
    : impl_(std::make_unique<Impl>(std::move(context)))
{
}

VulkanViewportImageRingPool::~VulkanViewportImageRingPool() = default;

VulkanViewportImageRing& VulkanViewportImageRingPool::ensure_ring(
    ViewportId viewport_id,
    ViewportGeneration generation,
    RenderExtent extent)
{
    return impl_->ensure_ring(viewport_id, generation, extent);
}

void VulkanViewportImageRingPool::release_viewport(ViewportId viewport_id)
{
    if (!viewport_id.is_valid()) {
        return;
    }

    impl_->release_viewport(viewport_id);
}

void VulkanViewportImageRingPool::prune(std::span<const ViewportId> live_viewport_ids)
{
    std::vector<ViewportId> dead_ids;
    for (const auto& [viewport_id, _] : impl_->rings) {
        const bool live = std::find(live_viewport_ids.begin(), live_viewport_ids.end(), viewport_id) !=
            live_viewport_ids.end();
        if (!live) {
            dead_ids.push_back(viewport_id);
        }
    }

    for (ViewportId viewport_id : dead_ids) {
        release_viewport(viewport_id);
    }
}

bool VulkanViewportImageRingPool::has_retired_resources() const noexcept
{
    return impl_->has_retired_resources();
}

void VulkanViewportImageRingPool::collect_retired_resources()
{
    impl_->collect_retired_resources();
}

std::optional<ViewportSampledGpuImage> VulkanViewportImageRingPool::resolve_sampled_image(
    const ViewportGpuImageHandle& handle,
    ViewportGpuResolveWait wait_policy)
{
    if (!handle.is_valid()) {
        return std::nullopt;
    }

    auto resolve_from_ring = [&handle, wait_policy](VulkanViewportImageRing& ring) {
        if (ring.resource_id() != handle.resource_id) {
            return std::optional<ViewportSampledGpuImage> {};
        }

        return ring.resolve_sampled_image(handle, wait_policy);
    };

    auto existing = impl_->rings.find(handle.viewport_id);
    if (existing != impl_->rings.end()) {
        if (std::optional<ViewportSampledGpuImage> image = resolve_from_ring(*existing->second)) {
            return image;
        }
    }

    for (const std::unique_ptr<VulkanViewportImageRing>& retired_ring : impl_->retired_rings) {
        if (retired_ring == nullptr) {
            continue;
        }

        if (std::optional<ViewportSampledGpuImage> image = resolve_from_ring(*retired_ring)) {
            return image;
        }
    }

    return std::nullopt;
}

} // namespace quader
