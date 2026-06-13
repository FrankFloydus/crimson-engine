module;

#include <backend/platforms/VulkanPlatformWindows.h>
#include <filament/Engine.h>
#include <filament/SwapChain.h>

#include <memory>
#include <optional>

export module quader.filament.viewport_vulkan_platform;

export import quader.render.vulkan_viewport_image_ring;

export namespace quader {

class FilamentViewportVulkanPlatform final : public filament::backend::VulkanPlatformWindows {
public:
    explicit FilamentViewportVulkanPlatform(VulkanSharedDeviceContext shared_device_context);

    FilamentViewportVulkanPlatform(const FilamentViewportVulkanPlatform&) = delete;
    FilamentViewportVulkanPlatform& operator=(const FilamentViewportVulkanPlatform&) = delete;

    ~FilamentViewportVulkanPlatform() override;

    [[nodiscard]] filament::SwapChain* create_viewport_swapchain(
        filament::Engine& engine,
        ViewportId viewport_id,
        ViewportGeneration generation,
        RenderExtent extent);
    void release_viewport(ViewportId viewport_id);
    [[nodiscard]] std::optional<ViewportGpuImageHandle> take_latest_viewport_gpu_image(ViewportId viewport_id);
    [[nodiscard]] std::optional<ViewportSampledGpuImage> resolve_viewport_gpu_image(
        const ViewportGpuImageHandle& handle,
        ViewportGpuResolveWait wait_policy);
    [[nodiscard]] bool has_retired_viewport_gpu_resources() const noexcept;
    void collect_retired_viewport_gpu_resources();

    [[nodiscard]] Customization getCustomization() const noexcept override;
    [[nodiscard]] SwapChainBundle getSwapChainBundle(SwapChainPtr handle) override;
    [[nodiscard]] VkResult acquire(SwapChainPtr handle, ImageSyncData* out_image_sync_data) override;
    [[nodiscard]] VkResult present(SwapChainPtr handle, uint32_t image_index, VkSemaphore finished_drawing) override;
    [[nodiscard]] bool hasResized(SwapChainPtr handle) override;
    [[nodiscard]] bool isProtected(SwapChainPtr handle) override;
    [[nodiscard]] VkResult recreate(SwapChainPtr handle) override;
    [[nodiscard]] SwapChainPtr createSwapChain(
        void* native_window,
        uint64_t flags = 0,
        VkExtent2D extent = { 0, 0 }) override;
    void destroy(SwapChainPtr handle) override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace quader
