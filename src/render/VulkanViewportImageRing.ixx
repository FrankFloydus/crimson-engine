module;

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>

export module quader.render.vulkan_viewport_image_ring;

export import quader.render.viewport_gpu_image;
export import quader.render.viewport_types;
export import quader.render.vulkan_device;

export namespace quader {

enum class ViewportGpuResolveWait : uint8_t {
    poll,
    wait,
};

struct VulkanViewportImageRingDesc {
    ViewportId viewport_id;
    ViewportGeneration generation;
    RenderExtent extent;
    uint32_t image_count = 3;
    VkFormat color_format = VK_FORMAT_R8G8B8A8_UNORM;
    VkFormat depth_format = VK_FORMAT_UNDEFINED;

    [[nodiscard]] bool is_valid() const noexcept
    {
        return viewport_id.is_valid() &&
            generation.is_valid() &&
            extent.is_valid() &&
            image_count >= 2 &&
            color_format != VK_FORMAT_UNDEFINED;
    }
};

struct VulkanViewportRenderImage {
    uint32_t slot_index = UINT32_MAX;
    VkImage color_image = VK_NULL_HANDLE;
    VkImage depth_image = VK_NULL_HANDLE;
    RenderExtent extent;
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    VkFormat depth_format = VK_FORMAT_UNDEFINED;

    [[nodiscard]] bool is_valid() const noexcept
    {
        return slot_index != UINT32_MAX &&
            color_image != VK_NULL_HANDLE &&
            depth_image != VK_NULL_HANDLE &&
            extent.is_valid() &&
            color_format != VK_FORMAT_UNDEFINED &&
            depth_format != VK_FORMAT_UNDEFINED;
    }
};

struct ViewportGpuImageLease {
    uint32_t slot_index = UINT32_MAX;
    ViewportGpuImageHandle handle;

    [[nodiscard]] bool is_valid() const noexcept
    {
        return slot_index != UINT32_MAX && handle.is_valid();
    }
};

class ViewportGpuImageRingLedger final {
public:
    ViewportGpuImageRingLedger(
        ViewportId viewport_id,
        ViewportGeneration generation,
        RenderExtent extent,
        uint32_t image_count,
        uint64_t resource_id);

    ViewportGpuImageRingLedger(const ViewportGpuImageRingLedger&) = delete;
    ViewportGpuImageRingLedger& operator=(const ViewportGpuImageRingLedger&) = delete;

    ~ViewportGpuImageRingLedger();

    [[nodiscard]] bool matches(ViewportGeneration generation, RenderExtent extent) const noexcept;
    [[nodiscard]] bool owns(const ViewportGpuImageHandle& handle) const noexcept;
    [[nodiscard]] uint32_t image_count() const noexcept;
    [[nodiscard]] uint64_t resource_id() const noexcept;
    [[nodiscard]] std::optional<uint32_t> try_acquire_ready_slot() noexcept;
    [[nodiscard]] ViewportGpuImageLease begin_render(uint32_t slot_index);
    [[nodiscard]] std::optional<ViewportGpuImageHandle> mark_ready(const ViewportGpuImageHandle& handle);
    [[nodiscard]] std::optional<ViewportGpuImageHandle> ready_handle_for(const ViewportGpuImageHandle& handle) const;

private:
    struct Slot;

    [[nodiscard]] Slot* slot_for(uint32_t slot_index) noexcept;
    [[nodiscard]] const Slot* slot_for(uint32_t slot_index) const noexcept;
    [[nodiscard]] Slot* slot_for(const ViewportGpuImageHandle& handle) noexcept;
    [[nodiscard]] const Slot* slot_for(const ViewportGpuImageHandle& handle) const noexcept;

    class Impl;
    std::unique_ptr<Impl> impl_;
};

class VulkanViewportImageRing final {
public:
    VulkanViewportImageRing(
        VulkanSharedDeviceContext context,
        VulkanViewportImageRingDesc desc);

    VulkanViewportImageRing(const VulkanViewportImageRing&) = delete;
    VulkanViewportImageRing& operator=(const VulkanViewportImageRing&) = delete;

    ~VulkanViewportImageRing();

    [[nodiscard]] bool matches(ViewportGeneration generation, RenderExtent extent) const noexcept;
    [[nodiscard]] ViewportId viewport_id() const noexcept;
    [[nodiscard]] ViewportGeneration generation() const noexcept;
    [[nodiscard]] uint64_t resource_id() const noexcept;
    [[nodiscard]] RenderExtent extent() const noexcept;
    [[nodiscard]] VkFormat color_format() const noexcept;
    [[nodiscard]] VkFormat depth_format() const noexcept;
    [[nodiscard]] VkImage depth_image() const noexcept;
    [[nodiscard]] std::span<const VkImage> color_images() const noexcept;

    [[nodiscard]] std::optional<VulkanViewportRenderImage> acquire_next_render_image();
    [[nodiscard]] std::optional<ViewportGpuImageHandle> mark_render_submitted(
        uint32_t slot_index,
        VkSemaphore finished_drawing);
    [[nodiscard]] std::optional<ViewportSampledGpuImage> resolve_sampled_image(
        const ViewportGpuImageHandle& handle,
        ViewportGpuResolveWait wait_policy);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

class VulkanViewportImageRingPool final {
public:
    explicit VulkanViewportImageRingPool(VulkanSharedDeviceContext context);

    VulkanViewportImageRingPool(const VulkanViewportImageRingPool&) = delete;
    VulkanViewportImageRingPool& operator=(const VulkanViewportImageRingPool&) = delete;

    ~VulkanViewportImageRingPool();

    [[nodiscard]] VulkanViewportImageRing& ensure_ring(
        ViewportId viewport_id,
        ViewportGeneration generation,
        RenderExtent extent);
    void release_viewport(ViewportId viewport_id);
    void prune(std::span<const ViewportId> live_viewport_ids);
    [[nodiscard]] bool has_retired_resources() const noexcept;
    void collect_retired_resources();
    [[nodiscard]] std::optional<ViewportSampledGpuImage> resolve_sampled_image(
        const ViewportGpuImageHandle& handle,
        ViewportGpuResolveWait wait_policy);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace quader
