module;

#include <imgui_impl_vulkan.h>
#include <tracy/Tracy.hpp>

#include <cstring>
#include <stdexcept>

module quader.ui.vulkan_preview_texture;

import quader.render.vulkan_common;

namespace quader {

VulkanPreviewTexture::VulkanPreviewTexture(const VulkanDevice& device)
    : device_(device)
    , upload_command_pool_(create_upload_command_pool())
{
    create_upload_slots();
}

VulkanPreviewTexture::~VulkanPreviewTexture()
{
    device_.wait_idle();
    destroy_texture();
    destroy_upload_slots();

    if (upload_command_pool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_.device(), upload_command_pool_, device_.allocator());
        upload_command_pool_ = VK_NULL_HANDLE;
    }
}

void VulkanPreviewTexture::upload_rgba(uint32_t width, uint32_t height, const uint8_t* rgba)
{
    ZoneScopedN("Vulkan preview texture upload");

    if (width == 0 || height == 0 || rgba == nullptr) {
        return;
    }

    if (image_ == VK_NULL_HANDLE || width_ != width || height_ != height) {
        create_texture(width, height);
    }

    const VkDeviceSize buffer_size = static_cast<VkDeviceSize>(width) * height * 4;
    UploadSlot& upload_slot = acquire_upload_slot();
    ensure_staging_buffer(upload_slot, buffer_size);

    void* mapped = nullptr;
    check_vk(vkMapMemory(device_.device(), upload_slot.staging_memory, 0, buffer_size, 0, &mapped), "vkMapMemory");
    std::memcpy(mapped, rgba, static_cast<size_t>(buffer_size));
    vkUnmapMemory(device_.device(), upload_slot.staging_memory);

    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check_vk(vkBeginCommandBuffer(upload_slot.command_buffer, &begin_info), "vkBeginCommandBuffer");

    transition_image(
        upload_slot.command_buffer,
        has_pixels_ ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copy_buffer_to_image(upload_slot.command_buffer, upload_slot.staging_buffer, width, height);
    transition_image(upload_slot.command_buffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    check_vk(vkEndCommandBuffer(upload_slot.command_buffer), "vkEndCommandBuffer");
    submit_upload_commands(upload_slot);

    has_pixels_ = true;
}

ImTextureID VulkanPreviewTexture::texture_id() const
{
    return reinterpret_cast<ImTextureID>(descriptor_);
}

bool VulkanPreviewTexture::has_pixels() const
{
    return descriptor_ != VK_NULL_HANDLE && has_pixels_;
}

uint32_t VulkanPreviewTexture::find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) const
{
    VkPhysicalDeviceMemoryProperties memory_properties = {};
    vkGetPhysicalDeviceMemoryProperties(device_.physical_device(), &memory_properties);

    for (uint32_t index = 0; index < memory_properties.memoryTypeCount; ++index) {
        const bool type_is_allowed = (type_filter & (1u << index)) != 0;
        const bool type_has_properties =
            (memory_properties.memoryTypes[index].propertyFlags & properties) == properties;

        if (type_is_allowed && type_has_properties) {
            return index;
        }
    }

    throw std::runtime_error("No Vulkan memory type satisfies the preview texture allocation.");
}

void VulkanPreviewTexture::create_texture(uint32_t width, uint32_t height)
{
    device_.wait_idle();
    destroy_texture();

    width_ = width;
    height_ = height;

    VkImageCreateInfo image_info = {};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent.width = width;
    image_info.extent.height = height;
    image_info.extent.depth = 1;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    check_vk(vkCreateImage(device_.device(), &image_info, device_.allocator(), &image_), "vkCreateImage");

    VkMemoryRequirements memory_requirements = {};
    vkGetImageMemoryRequirements(device_.device(), image_, &memory_requirements);

    VkMemoryAllocateInfo allocation_info = {};
    allocation_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation_info.allocationSize = memory_requirements.size;
    allocation_info.memoryTypeIndex = find_memory_type(
        memory_requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check_vk(vkAllocateMemory(device_.device(), &allocation_info, device_.allocator(), &memory_), "vkAllocateMemory");
    check_vk(vkBindImageMemory(device_.device(), image_, memory_, 0), "vkBindImageMemory");

    VkImageViewCreateInfo view_info = {};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = image_;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;
    check_vk(vkCreateImageView(device_.device(), &view_info, device_.allocator(), &image_view_), "vkCreateImageView");

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
    check_vk(vkCreateSampler(device_.device(), &sampler_info, device_.allocator(), &sampler_), "vkCreateSampler");

    descriptor_ = ImGui_ImplVulkan_AddTexture(
        sampler_,
        image_view_,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    has_pixels_ = false;
}

void VulkanPreviewTexture::destroy_texture()
{
    if (descriptor_ != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(descriptor_);
        descriptor_ = VK_NULL_HANDLE;
    }

    if (sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device_.device(), sampler_, device_.allocator());
        sampler_ = VK_NULL_HANDLE;
    }

    if (image_view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_.device(), image_view_, device_.allocator());
        image_view_ = VK_NULL_HANDLE;
    }

    if (image_ != VK_NULL_HANDLE) {
        vkDestroyImage(device_.device(), image_, device_.allocator());
        image_ = VK_NULL_HANDLE;
    }

    if (memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_.device(), memory_, device_.allocator());
        memory_ = VK_NULL_HANDLE;
    }

    width_ = 0;
    height_ = 0;
    has_pixels_ = false;
}

void VulkanPreviewTexture::ensure_staging_buffer(UploadSlot& slot, VkDeviceSize required_size)
{
    if (slot.staging_buffer != VK_NULL_HANDLE && slot.staging_size >= required_size) {
        return;
    }

    destroy_staging_buffer(slot);

    VkBufferCreateInfo buffer_info = {};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = required_size;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check_vk(vkCreateBuffer(device_.device(), &buffer_info, device_.allocator(), &slot.staging_buffer), "vkCreateBuffer");

    VkMemoryRequirements memory_requirements = {};
    vkGetBufferMemoryRequirements(device_.device(), slot.staging_buffer, &memory_requirements);

    VkMemoryAllocateInfo allocation_info = {};
    allocation_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation_info.allocationSize = memory_requirements.size;
    allocation_info.memoryTypeIndex = find_memory_type(
        memory_requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check_vk(vkAllocateMemory(device_.device(), &allocation_info, device_.allocator(), &slot.staging_memory), "vkAllocateMemory");
    check_vk(vkBindBufferMemory(device_.device(), slot.staging_buffer, slot.staging_memory, 0), "vkBindBufferMemory");

    slot.staging_size = required_size;
}

void VulkanPreviewTexture::destroy_staging_buffer(UploadSlot& slot)
{
    if (slot.staging_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_.device(), slot.staging_buffer, device_.allocator());
        slot.staging_buffer = VK_NULL_HANDLE;
    }

    if (slot.staging_memory != VK_NULL_HANDLE) {
        vkFreeMemory(device_.device(), slot.staging_memory, device_.allocator());
        slot.staging_memory = VK_NULL_HANDLE;
    }

    slot.staging_size = 0;
}

VkCommandPool VulkanPreviewTexture::create_upload_command_pool()
{
    VkCommandPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = device_.queue_family();

    VkCommandPool command_pool = VK_NULL_HANDLE;
    check_vk(vkCreateCommandPool(device_.device(), &pool_info, device_.allocator(), &command_pool), "vkCreateCommandPool");
    return command_pool;
}

void VulkanPreviewTexture::create_upload_slots()
{
    VkFenceCreateInfo fence_info = {};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    VkCommandBuffer command_buffers[upload_slot_count_] {};
    VkCommandBufferAllocateInfo command_buffer_info = {};
    command_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_buffer_info.commandPool = upload_command_pool_;
    command_buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_buffer_info.commandBufferCount = upload_slot_count_;
    check_vk(vkAllocateCommandBuffers(device_.device(), &command_buffer_info, command_buffers), "vkAllocateCommandBuffers");

    for (uint32_t index = 0; index < upload_slot_count_; ++index) {
        upload_slots_[index].command_buffer = command_buffers[index];
        check_vk(vkCreateFence(device_.device(), &fence_info, device_.allocator(), &upload_slots_[index].fence), "vkCreateFence");
    }
}

void VulkanPreviewTexture::destroy_upload_slots()
{
    for (UploadSlot& slot : upload_slots_) {
        destroy_staging_buffer(slot);

        if (slot.fence != VK_NULL_HANDLE) {
            vkDestroyFence(device_.device(), slot.fence, device_.allocator());
            slot.fence = VK_NULL_HANDLE;
        }

        slot.command_buffer = VK_NULL_HANDLE;
    }
}

VulkanPreviewTexture::UploadSlot& VulkanPreviewTexture::acquire_upload_slot()
{
    for (uint32_t offset = 0; offset < upload_slot_count_; ++offset) {
        const uint32_t slot_index = (next_upload_slot_ + offset) % upload_slot_count_;
        UploadSlot& slot = upload_slots_[slot_index];
        const VkResult fence_status = vkGetFenceStatus(device_.device(), slot.fence);
        if (fence_status == VK_SUCCESS) {
            reset_upload_slot(slot);
            next_upload_slot_ = (slot_index + 1) % upload_slot_count_;
            return slot;
        }

        if (fence_status == VK_NOT_READY) {
            continue;
        }

        check_vk(fence_status, "vkGetFenceStatus");
    }

    UploadSlot& slot = upload_slots_[next_upload_slot_];
    check_vk(vkWaitForFences(device_.device(), 1, &slot.fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");
    reset_upload_slot(slot);
    next_upload_slot_ = (next_upload_slot_ + 1) % upload_slot_count_;
    return slot;
}

void VulkanPreviewTexture::reset_upload_slot(UploadSlot& slot) const
{
    check_vk(vkResetFences(device_.device(), 1, &slot.fence), "vkResetFences");
    check_vk(vkResetCommandBuffer(slot.command_buffer, 0), "vkResetCommandBuffer");
}

void VulkanPreviewTexture::submit_upload_commands(const UploadSlot& slot) const
{
    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &slot.command_buffer;

    check_vk(vkQueueSubmit(device_.queue(), 1, &submit_info, slot.fence), "vkQueueSubmit");
}

void VulkanPreviewTexture::transition_image(
    VkCommandBuffer command_buffer,
    VkImageLayout old_layout,
    VkImageLayout new_layout) const
{
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image_;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags destination_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;

    if (old_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        source_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }

    if (new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        destination_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        source_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destination_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }

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

void VulkanPreviewTexture::copy_buffer_to_image(
    VkCommandBuffer command_buffer,
    VkBuffer source,
    uint32_t width,
    uint32_t height) const
{
    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { width, height, 1 };

    vkCmdCopyBufferToImage(
        command_buffer,
        source,
        image_,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region);
}

} // namespace quader
