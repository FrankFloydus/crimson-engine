////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace quader {

enum class RenderCompositionApi : std::uint8_t { Vulkan, Direct3D12, Direct3D11 };

/**
 * Immutable capabilities known to be enabled by the compositor device owner.
 * Device extension names are a verified subset, not an enumeration of supported
 * extensions. Consumers must disable extensions absent from this allowlist.
 * Owning this snapshot does not extend the lifetime of the borrowed device.
 */
struct RenderVulkanEnabledCapabilities {
  std::uint32_t api_version = VK_API_VERSION_1_0;
  VkPhysicalDeviceFeatures features{};
  std::vector<std::string> instance_extensions;
  std::vector<std::string> device_extensions;
};

/** Borrowed compositor device and queue; their owner must outlive RenderSystem. */
struct RenderVulkanSharedContext {
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice physical_device = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue graphics_queue = VK_NULL_HANDLE;
  std::uint32_t graphics_queue_family = 0;
  std::uint32_t graphics_queue_index = 0;
  // Null when the compositor cannot attest which capabilities were enabled.
  std::shared_ptr<const RenderVulkanEnabledCapabilities> enabled_capabilities;
  RenderCompositionApi api = RenderCompositionApi::Vulkan;
  void *d3d12_device = nullptr;
  void *d3d12_queue = nullptr;
  void *d3d11_device = nullptr;
  void *d3d11_context = nullptr;
};

/** Retained offscreen allocation extent, independent of the visible pane size. */
struct RenderVulkanCompositionCapacity {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
};

/** Borrowed current-frame color image, returned in shader-read layout after copy. */
struct RenderVulkanCompositionTarget {
  VkImage image = VK_NULL_HANDLE;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  VkImageLayout old_layout = VK_IMAGE_LAYOUT_UNDEFINED;
  RenderCompositionApi api = RenderCompositionApi::Vulkan;
  void *d3d12_resource = nullptr;
  void *d3d12_queue = nullptr;
  std::uint32_t d3d12_state = 0;
  void *d3d11_texture = nullptr;
  void *d3d11_context = nullptr;
  // Native addresses may be reused after resize/recreation. This opaque Qt
  // image generation is part of the identity of retained destination pixels.
  std::uint64_t destination_generation = 0;
};

/** Tracks command producers across nested compositor callbacks without GPU waits. */
class RenderSharedQueueSubmissionState {
public:
  void begin_batch() {
    ++batch_depth_;
    pending_ = true;
  }
  void end_batch() {
    if (batch_depth_ != 0) {
      --batch_depth_;
      pending_ = true;
    }
  }
  void mark_pending() { pending_ = true; }
  [[nodiscard]] bool needs_handoff() const {
    return pending_ || batch_depth_ != 0;
  }
  void complete_handoff() {
    pending_ = false;
    ++handoff_count_;
  }
  [[nodiscard]] std::uint64_t handoff_count() const { return handoff_count_; }

private:
  std::uint32_t batch_depth_ = 0;
  bool pending_ = false;
  std::uint64_t handoff_count_ = 0;
};

} // namespace quader
