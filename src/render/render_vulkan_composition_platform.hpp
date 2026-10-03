////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/render_vulkan_composition.hpp"

#include <backend/platforms/VulkanPlatformWindows.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace filament {
class Engine;
class SwapChain;
}

namespace quader {

/**
 * Owns offscreen Vulkan images and GPU copies without depending on the UI toolkit.
 * Native preview swapchains continue through VulkanPlatformWindows.
 */
class RenderVulkanCompositionPlatform
    : public filament::backend::VulkanPlatformWindows {
public:
  explicit RenderVulkanCompositionPlatform(
      const RenderVulkanSharedContext *shared_context = nullptr);
  ~RenderVulkanCompositionPlatform() override;

  [[nodiscard]] VulkanSharedContext *shared_context();
  [[nodiscard]] bool has_shared_context() const { return shared_context_enabled_; }
  [[nodiscard]] void *create_composited_surface(std::uint32_t width,
                                               std::uint32_t height);
  filament::SwapChain *create_composited_swap_chain(filament::Engine &engine,
                                                   void *token);
  [[nodiscard]] RenderVulkanCompositionCapacity capacity(void *token) const;
  void begin_composited_frame(void *token);
  bool copy_composited_frame(void *token,
                             const RenderVulkanCompositionTarget &target);
  // Requires completed GPU use and a processed destroySwapChain command.
  void retire_composited_images(void *token);
  static bool finish_cpu_commands(filament::Engine &engine);

  Customization getCustomization() const noexcept override;
  SwapChainPtr createSwapChain(void *native_window, std::uint64_t flags,
                               VkExtent2D extent) override;
  SwapChainBundle getSwapChainBundle(SwapChainPtr swap_chain) override;
  VkResult acquire(SwapChainPtr swap_chain, ImageSyncData *sync) override;
  VkResult present(SwapChainPtr swap_chain, std::uint32_t image_index,
                    VkSemaphore finished_drawing) override;
  bool hasResized(SwapChainPtr swap_chain) override;
  bool isProtected(SwapChainPtr swap_chain) override;
  VkResult recreate(SwapChainPtr swap_chain) override;
  void destroy(SwapChainPtr swap_chain) override;

private:
  struct CompositionSurface;
  bool create_image(CompositionSurface &surface, VkFormat format,
                     VkImageUsageFlags usage, VkImage &image,
                     VkDeviceMemory &memory);
  void destroy_images(CompositionSurface &surface);

  RenderVulkanSharedContext context_;
  VulkanSharedContext filament_context_;
  bool shared_context_enabled_ = false;
  std::atomic<bool> creating_composited_surface_ = false;
  mutable std::mutex composition_mutex_;
  std::unordered_map<void *, std::unique_ptr<CompositionSurface>> compositions_;
};

} // namespace quader
