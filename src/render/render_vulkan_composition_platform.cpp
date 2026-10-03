////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/render_vulkan_composition_platform.hpp"

#include "core/logger.hpp"

#include <filament/Engine.h>
#include <filament/Fence.h>
#include <filament/SwapChain.h>

#include <array>
#include <limits>

namespace quader {

/** Retains one GPU color/stencil allocation and its compositor copy command. */
struct RenderVulkanCompositionPlatform::CompositionSurface final
    : filament::backend::Platform::SwapChain {
  VkExtent2D extent{};
  VkImage color = VK_NULL_HANDLE;
  VkImage depth = VK_NULL_HANDLE;
  VkDeviceMemory color_memory = VK_NULL_HANDLE;
  VkDeviceMemory depth_memory = VK_NULL_HANDLE;
  VkFormat depth_format = VK_FORMAT_UNDEFINED;
  VkCommandPool command_pool = VK_NULL_HANDLE;
  VkCommandBuffer copy_command = VK_NULL_HANDLE;
  VkSemaphore finished_drawing = VK_NULL_HANDLE;
};

namespace {

bool succeeded(VkResult result, const char *operation) {
  if (result == VK_SUCCESS) {
    return true;
  }
  LOG_ERROR("Vulkan composition {} failed: {}", operation,
            static_cast<int>(result));
  return false;
}

} // namespace

RenderVulkanCompositionPlatform::RenderVulkanCompositionPlatform(
    const RenderVulkanSharedContext *shared_context) {
  if (shared_context != nullptr) {
    context_ = *shared_context;
    filament_context_.instance = context_.instance;
    filament_context_.physicalDevice = context_.physical_device;
    filament_context_.logicalDevice = context_.device;
    filament_context_.graphicsQueueFamilyIndex = context_.graphics_queue_family;
    filament_context_.graphicsQueueIndex = context_.graphics_queue_index;
    shared_context_enabled_ = true;
  }
}

RenderVulkanCompositionPlatform::~RenderVulkanCompositionPlatform() {
  // Engine shutdown normally consumes all tokens; failed creation may leave one.
  for (auto &entry : compositions_) {
    destroy_images(*entry.second);
  }
}

RenderVulkanCompositionPlatform::VulkanSharedContext *
RenderVulkanCompositionPlatform::shared_context() {
  return shared_context_enabled_ ? &filament_context_ : nullptr;
}

bool RenderVulkanCompositionPlatform::finish_cpu_commands(
    filament::Engine &engine) {
  return filament::Fence::waitAndDestroy(engine.createFence(),
                                         filament::Fence::Mode::FLUSH) ==
         filament::Fence::FenceStatus::CONDITION_SATISFIED;
}

RenderVulkanCompositionPlatform::Customization
RenderVulkanCompositionPlatform::getCustomization() const noexcept {
  Customization customization;
  if (creating_composited_surface_.load(std::memory_order_relaxed)) {
    customization.transitionSwapChainImageLayoutForPresent = false;
    customization.flushAndWaitOnWindowResize = false;
  }
  return customization;
}

bool RenderVulkanCompositionPlatform::create_image(
    CompositionSurface &surface, VkFormat format, VkImageUsageFlags usage,
    VkImage &image, VkDeviceMemory &memory) {
  VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  image_info.imageType = VK_IMAGE_TYPE_2D;
  image_info.format = format;
  image_info.extent = {surface.extent.width, surface.extent.height, 1};
  image_info.mipLevels = 1;
  image_info.arrayLayers = 1;
  image_info.samples = VK_SAMPLE_COUNT_1_BIT;
  image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  image_info.usage = usage;
  image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (!succeeded(bluevk::vkCreateImage(context_.device, &image_info, nullptr,
                                      &image), "image creation")) {
    return false;
  }
  VkMemoryRequirements requirements{};
  bluevk::vkGetImageMemoryRequirements(context_.device, image, &requirements);
  VkPhysicalDeviceMemoryProperties memory_properties{};
  bluevk::vkGetPhysicalDeviceMemoryProperties(context_.physical_device,
                                             &memory_properties);
  std::uint32_t memory_type = std::numeric_limits<std::uint32_t>::max();
  for (std::uint32_t index = 0; index < memory_properties.memoryTypeCount;
       ++index) {
    if ((requirements.memoryTypeBits & (1u << index)) != 0 &&
        (memory_properties.memoryTypes[index].propertyFlags &
         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0) {
      memory_type = index;
      break;
    }
  }
  if (memory_type == std::numeric_limits<std::uint32_t>::max()) {
    LOG_ERROR("No device-local Vulkan composition memory type");
    return false;
  }
  VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocation.allocationSize = requirements.size;
  allocation.memoryTypeIndex = memory_type;
  return succeeded(bluevk::vkAllocateMemory(context_.device, &allocation,
                                           nullptr, &memory),
                    "image allocation") &&
         succeeded(bluevk::vkBindImageMemory(context_.device, image, memory, 0),
                    "image memory binding");
}

void RenderVulkanCompositionPlatform::destroy_images(
    CompositionSurface &surface) {
  if (surface.command_pool != VK_NULL_HANDLE) {
    bluevk::vkDestroyCommandPool(context_.device, surface.command_pool, nullptr);
    surface.command_pool = VK_NULL_HANDLE;
    surface.copy_command = VK_NULL_HANDLE;
  }
  for (VkImage *image : {&surface.color, &surface.depth}) {
    if (*image != VK_NULL_HANDLE) {
      bluevk::vkDestroyImage(context_.device, *image, nullptr);
      *image = VK_NULL_HANDLE;
    }
  }
  for (VkDeviceMemory *memory : {&surface.color_memory, &surface.depth_memory}) {
    if (*memory != VK_NULL_HANDLE) {
      bluevk::vkFreeMemory(context_.device, *memory, nullptr);
      *memory = VK_NULL_HANDLE;
    }
  }
}

void *RenderVulkanCompositionPlatform::create_composited_surface(
    std::uint32_t width, std::uint32_t height) {
  if (!shared_context_enabled_ || width == 0 || height == 0) {
    return nullptr;
  }
  auto surface = std::make_unique<CompositionSurface>();
  surface->extent = {width, height};
  const auto supports_format = [&](VkFormat format, VkFormatFeatureFlags flags) {
    VkFormatProperties properties{};
    bluevk::vkGetPhysicalDeviceFormatProperties(context_.physical_device,
                                                format, &properties);
    return (properties.optimalTilingFeatures & flags) == flags;
  };
  for (VkFormat format : {VK_FORMAT_D32_SFLOAT_S8_UINT,
                          VK_FORMAT_D24_UNORM_S8_UINT}) {
    if (supports_format(format, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
      surface->depth_format = format;
      break;
    }
  }
  if (surface->depth_format == VK_FORMAT_UNDEFINED ||
      !supports_format(VK_FORMAT_R8G8B8A8_SRGB,
                        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
                            VK_FORMAT_FEATURE_TRANSFER_SRC_BIT)) {
    LOG_ERROR("Required Vulkan composition color/stencil format unavailable");
    return nullptr;
  }
  if (!create_image(*surface, VK_FORMAT_R8G8B8A8_SRGB,
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                         VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                     surface->color, surface->color_memory) ||
      !create_image(*surface, surface->depth_format,
                     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                     surface->depth, surface->depth_memory)) {
    destroy_images(*surface);
    return nullptr;
  }
  VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pool_info.queueFamilyIndex = context_.graphics_queue_family;
  VkCommandBufferAllocateInfo allocation{
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocation.commandBufferCount = 1;
  if (!succeeded(bluevk::vkCreateCommandPool(context_.device, &pool_info, nullptr,
                                            &surface->command_pool),
                   "command pool creation")) {
    destroy_images(*surface);
    return nullptr;
  }
  allocation.commandPool = surface->command_pool;
  if (!succeeded(bluevk::vkAllocateCommandBuffers(context_.device, &allocation,
                                                  &surface->copy_command),
                   "copy command allocation")) {
    destroy_images(*surface);
    return nullptr;
  }
  void *token = surface.get();
  std::lock_guard lock(composition_mutex_);
  compositions_.emplace(token, std::move(surface));
  return token;
}

filament::SwapChain *
RenderVulkanCompositionPlatform::create_composited_swap_chain(
    filament::Engine &engine, void *token) {
  if (!finish_cpu_commands(engine)) {
    return nullptr;
  }
  // The pinned backend captures these two options in its swapchain constructor.
  // Serialize that constructor so native preview swapchains keep native options.
  creating_composited_surface_.store(true, std::memory_order_relaxed);
  try {
    filament::SwapChain *swap_chain = engine.createSwapChain(
        token, filament::SwapChain::CONFIG_SRGB_COLORSPACE |
                   filament::SwapChain::CONFIG_HAS_STENCIL_BUFFER);
    const bool completed = finish_cpu_commands(engine);
    creating_composited_surface_.store(false, std::memory_order_relaxed);
    return completed ? swap_chain : nullptr;
  } catch (...) {
    creating_composited_surface_.store(false, std::memory_order_relaxed);
    throw;
  }
}

RenderVulkanCompositionCapacity RenderVulkanCompositionPlatform::capacity(
    void *token) const {
  std::lock_guard lock(composition_mutex_);
  const auto found = compositions_.find(token);
  return found != compositions_.end()
             ? RenderVulkanCompositionCapacity{found->second->extent.width,
                                                found->second->extent.height}
             : RenderVulkanCompositionCapacity{};
}

void RenderVulkanCompositionPlatform::begin_composited_frame(void *token) {
  std::lock_guard lock(composition_mutex_);
  if (const auto found = compositions_.find(token); found != compositions_.end()) {
    found->second->finished_drawing = VK_NULL_HANDLE;
  }
}

bool RenderVulkanCompositionPlatform::copy_composited_frame(
    void *token, const RenderVulkanCompositionTarget &target) {
  std::lock_guard lock(composition_mutex_);
  const auto found = compositions_.find(token);
  if (found == compositions_.end() || target.image == VK_NULL_HANDLE ||
      target.width == 0 || target.height == 0) {
    return false;
  }
  CompositionSurface &surface = *found->second;
  if (target.width > surface.extent.width ||
      target.height > surface.extent.height ||
      surface.finished_drawing == VK_NULL_HANDLE) {
    return false;
  }
  // The compositor's previous offscreen-frame fence completed this command
  // buffer and consumed the previous binary signal before the next callback.
  const VkCommandBuffer command = surface.copy_command;
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  if (!succeeded(bluevk::vkResetCommandBuffer(command, 0), "copy reset") ||
      !succeeded(bluevk::vkBeginCommandBuffer(command, &begin), "copy begin")) {
    return false;
  }
  std::array<VkImageMemoryBarrier, 2> barriers{};
  for (auto &barrier : barriers) {
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  }
  barriers[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  barriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  barriers[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  barriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  barriers[0].image = surface.color;
  barriers[1].srcAccessMask = target.old_layout == VK_IMAGE_LAYOUT_UNDEFINED
                                  ? 0
                                  : VK_ACCESS_MEMORY_READ_BIT |
                                        VK_ACCESS_MEMORY_WRITE_BIT;
  barriers[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barriers[1].oldLayout = target.old_layout;
  barriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barriers[1].image = target.image;
  bluevk::vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
                                0, nullptr, 2, barriers.data());
  VkImageCopy copy{};
  copy.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  copy.dstSubresource = copy.srcSubresource;
  copy.extent = {target.width, target.height, 1};
  // The viewport owner rasterizes the visible crop at the retained image origin.
  // Copy preserves sRGB encoded bytes; the compositor samples an UNORM texture.
  bluevk::vkCmdCopyImage(command, surface.color,
                          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target.image,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
  barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  barriers[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  barriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  barriers[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  barriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  barriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barriers[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  bluevk::vkCmdPipelineBarrier(
      command, VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      0, 0, nullptr, 0, nullptr, 2, barriers.data());
  if (!succeeded(bluevk::vkEndCommandBuffer(command), "copy end")) {
    return false;
  }
  const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  VkSubmitInfo submission{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submission.commandBufferCount = 1;
  submission.pCommandBuffers = &command;
  submission.waitSemaphoreCount = 1;
  submission.pWaitSemaphores = &surface.finished_drawing;
  submission.pWaitDstStageMask = &wait_stage;
  return succeeded(bluevk::vkQueueSubmit(context_.graphics_queue, 1, &submission,
                                         VK_NULL_HANDLE), "copy submission");
}

void RenderVulkanCompositionPlatform::retire_composited_images(void *token) {
  std::lock_guard lock(composition_mutex_);
  if (const auto found = compositions_.find(token); found != compositions_.end()) {
    destroy_images(*found->second);
  }
}

RenderVulkanCompositionPlatform::SwapChainPtr
RenderVulkanCompositionPlatform::createSwapChain(void *native_window,
                                                std::uint64_t flags,
                                                VkExtent2D extent) {
  {
    std::lock_guard lock(composition_mutex_);
    if (const auto found = compositions_.find(native_window);
        found != compositions_.end()) {
      return found->second.get();
    }
  }
  return VulkanPlatformWindows::createSwapChain(native_window, flags, extent);
}

RenderVulkanCompositionPlatform::SwapChainBundle
RenderVulkanCompositionPlatform::getSwapChainBundle(SwapChainPtr swap_chain) {
  {
    std::lock_guard lock(composition_mutex_);
    if (const auto found = compositions_.find(swap_chain);
        found != compositions_.end()) {
      const auto &surface = *found->second;
      SwapChainBundle bundle;
      bundle.colors = utils::FixedCapacityVector<VkImage>::with_capacity(1);
      bundle.colors.push_back(surface.color);
      bundle.depth = surface.depth;
      bundle.colorFormat = VK_FORMAT_R8G8B8A8_SRGB;
      bundle.depthFormat = surface.depth_format;
      bundle.extent = surface.extent;
      return bundle;
    }
  }
  return VulkanPlatformWindows::getSwapChainBundle(swap_chain);
}

VkResult RenderVulkanCompositionPlatform::acquire(SwapChainPtr swap_chain,
                                                  ImageSyncData *sync) {
  {
    std::lock_guard lock(composition_mutex_);
    if (compositions_.contains(swap_chain)) {
      sync->imageIndex = 0;
      sync->imageReadySemaphore = VK_NULL_HANDLE;
      return VK_SUCCESS;
    }
  }
  return VulkanPlatformWindows::acquire(swap_chain, sync);
}

VkResult RenderVulkanCompositionPlatform::present(SwapChainPtr swap_chain,
                                                  std::uint32_t image_index,
                                                  VkSemaphore finished_drawing) {
  {
    std::lock_guard lock(composition_mutex_);
    if (const auto found = compositions_.find(swap_chain);
        found != compositions_.end()) {
      found->second->finished_drawing = finished_drawing;
      return VK_SUCCESS;
    }
  }
  return VulkanPlatformWindows::present(swap_chain, image_index, finished_drawing);
}

bool RenderVulkanCompositionPlatform::hasResized(SwapChainPtr swap_chain) {
  {
    std::lock_guard lock(composition_mutex_);
    if (compositions_.contains(swap_chain)) {
      return false;
    }
  }
  return VulkanPlatformWindows::hasResized(swap_chain);
}

bool RenderVulkanCompositionPlatform::isProtected(SwapChainPtr swap_chain) {
  {
    std::lock_guard lock(composition_mutex_);
    if (compositions_.contains(swap_chain)) {
      return false;
    }
  }
  return VulkanPlatformWindows::isProtected(swap_chain);
}

VkResult RenderVulkanCompositionPlatform::recreate(SwapChainPtr swap_chain) {
  {
    std::lock_guard lock(composition_mutex_);
    if (compositions_.contains(swap_chain)) {
      return VK_SUCCESS;
    }
  }
  return VulkanPlatformWindows::recreate(swap_chain);
}

void RenderVulkanCompositionPlatform::destroy(SwapChainPtr swap_chain) {
  {
    std::lock_guard lock(composition_mutex_);
    if (const auto found = compositions_.find(swap_chain);
        found != compositions_.end()) {
      // Surface retirement or Engine shutdown completed external copy work;
      // Filament's swapchain destructor completed its own submitted commands.
      destroy_images(*found->second);
      compositions_.erase(found);
      return;
    }
  }
  VulkanPlatformWindows::destroy(swap_chain);
}

} // namespace quader
