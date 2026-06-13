module;

#include <backend/platforms/VulkanPlatformWindows.h>
#include <filament/Engine.h>
#include <filament/SwapChain.h>

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <utility>

module quader.filament.viewport_vulkan_platform;

namespace quader {
namespace {

struct ViewportSwapChainToken {
    ViewportId viewport_id;
    ViewportGeneration generation;
    RenderExtent extent;
};

struct ViewportPlatformSwapChain final : filament::backend::Platform::SwapChain {
    VulkanViewportImageRing* ring = nullptr;

    [[nodiscard]] bool is_valid() const noexcept
    {
        return ring != nullptr;
    }
};

ViewportPlatformSwapChain* viewport_swapchain_from(filament::backend::VulkanPlatform::SwapChainPtr handle)
{
    return static_cast<ViewportPlatformSwapChain*>(handle);
}

} // namespace

class FilamentViewportVulkanPlatform::Impl {
public:
    explicit Impl(VulkanSharedDeviceContext shared_device_context)
        : ring_pool(std::move(shared_device_context))
    {
    }

    struct SubmittedViewportFrames {
        mutable std::mutex mutex;
        std::unordered_map<ViewportId, std::deque<ViewportGpuImageHandle>, ViewportIdHash> handles;

        void push(ViewportGpuImageHandle handle)
        {
            if (!handle.is_valid()) {
                return;
            }

            std::scoped_lock lock(mutex);
            handles[handle.viewport_id].push_back(handle);
        }

    [[nodiscard]] std::optional<ViewportGpuImageHandle> pop_next(ViewportId viewport_id)
    {
        std::scoped_lock lock(mutex);
        const auto existing = handles.find(viewport_id);
        if (existing == handles.end() || existing->second.empty()) {
                return std::nullopt;
            }

        ViewportGpuImageHandle handle = existing->second.front();
        existing->second.pop_front();
        if (existing->second.empty()) {
            handles.erase(existing);
        }
        return handle;
    }

        void erase(ViewportId viewport_id)
        {
            std::scoped_lock lock(mutex);
            handles.erase(viewport_id);
        }
    };

    VulkanViewportImageRingPool ring_pool;
    std::unordered_map<void*, std::unique_ptr<ViewportSwapChainToken>> pending_tokens;
    SubmittedViewportFrames submitted_frames;

    [[nodiscard]] ViewportSwapChainToken* create_pending_token(
        ViewportId viewport_id,
        ViewportGeneration generation,
        RenderExtent extent)
    {
        auto token = std::make_unique<ViewportSwapChainToken>(
            ViewportSwapChainToken {
                .viewport_id = viewport_id,
                .generation = generation,
                .extent = extent,
            });
        ViewportSwapChainToken* token_ptr = token.get();
        pending_tokens.emplace(token_ptr, std::move(token));
        return token_ptr;
    }

    [[nodiscard]] std::unique_ptr<ViewportSwapChainToken> take_pending_token(void* native_window)
    {
        const auto existing = pending_tokens.find(native_window);
        if (existing == pending_tokens.end()) {
            return {};
        }

        std::unique_ptr<ViewportSwapChainToken> token = std::move(existing->second);
        pending_tokens.erase(existing);
        return token;
    }

    void abandon_pending_token(ViewportSwapChainToken* token)
    {
        pending_tokens.erase(token);
    }
};

FilamentViewportVulkanPlatform::FilamentViewportVulkanPlatform(VulkanSharedDeviceContext shared_device_context)
    : impl_(std::make_unique<Impl>(std::move(shared_device_context)))
{
}

FilamentViewportVulkanPlatform::~FilamentViewportVulkanPlatform() = default;

filament::SwapChain* FilamentViewportVulkanPlatform::create_viewport_swapchain(
    filament::Engine& engine,
    ViewportId viewport_id,
    ViewportGeneration generation,
    RenderExtent extent)
{
    if (!viewport_id.is_valid() || !generation.is_valid() || !extent.is_valid()) {
        return nullptr;
    }

    ViewportSwapChainToken* token = impl_->create_pending_token(viewport_id, generation, extent);
    filament::SwapChain* swapchain = engine.createSwapChain(static_cast<void*>(token));
    if (swapchain == nullptr) {
        impl_->abandon_pending_token(token);
    }
    return swapchain;
}

void FilamentViewportVulkanPlatform::release_viewport(ViewportId viewport_id)
{
    impl_->submitted_frames.erase(viewport_id);
    impl_->ring_pool.release_viewport(viewport_id);
}

std::optional<ViewportGpuImageHandle> FilamentViewportVulkanPlatform::take_latest_viewport_gpu_image(
    ViewportId viewport_id)
{
    return impl_->submitted_frames.pop_next(viewport_id);
}

std::optional<ViewportSampledGpuImage> FilamentViewportVulkanPlatform::resolve_viewport_gpu_image(
    const ViewportGpuImageHandle& handle,
    ViewportGpuResolveWait wait_policy)
{
    return impl_->ring_pool.resolve_sampled_image(handle, wait_policy);
}

bool FilamentViewportVulkanPlatform::has_retired_viewport_gpu_resources() const noexcept
{
    return impl_->ring_pool.has_retired_resources();
}

void FilamentViewportVulkanPlatform::collect_retired_viewport_gpu_resources()
{
    impl_->ring_pool.collect_retired_resources();
}

FilamentViewportVulkanPlatform::Customization FilamentViewportVulkanPlatform::getCustomization() const noexcept
{
    Customization customization = VulkanPlatformWindows::getCustomization();
    customization.flushAndWaitOnWindowResize = false;
    customization.transitionSwapChainImageLayoutForPresent = false;
    return customization;
}

FilamentViewportVulkanPlatform::SwapChainBundle FilamentViewportVulkanPlatform::getSwapChainBundle(
    SwapChainPtr handle)
{
    const ViewportPlatformSwapChain* swapchain = viewport_swapchain_from(handle);
    if (swapchain == nullptr || !swapchain->is_valid()) {
        return {};
    }

    SwapChainBundle bundle;
    const std::span<const VkImage> colors = swapchain->ring->color_images();
    bundle.colors.reserve(static_cast<decltype(bundle.colors)::size_type>(colors.size()));
    for (VkImage color : colors) {
        bundle.colors.push_back(color);
    }
    bundle.depth = swapchain->ring->depth_image();
    bundle.colorFormat = swapchain->ring->color_format();
    bundle.depthFormat = swapchain->ring->depth_format();
    bundle.extent = VkExtent2D {
        .width = swapchain->ring->extent().width,
        .height = swapchain->ring->extent().height,
    };
    bundle.layerCount = 1;
    bundle.isProtected = false;
    return bundle;
}

VkResult FilamentViewportVulkanPlatform::acquire(
    SwapChainPtr handle,
    ImageSyncData* out_image_sync_data)
{
    if (out_image_sync_data == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    out_image_sync_data->imageIndex = ImageSyncData::INVALID_IMAGE_INDEX;
    out_image_sync_data->imageReadySemaphore = VK_NULL_HANDLE;

    ViewportPlatformSwapChain* swapchain = viewport_swapchain_from(handle);
    if (swapchain == nullptr || !swapchain->is_valid()) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    std::optional<VulkanViewportRenderImage> render_image = swapchain->ring->acquire_next_render_image();
    if (!render_image.has_value()) {
        return VK_NOT_READY;
    }

    out_image_sync_data->imageIndex = render_image->slot_index;
    return VK_SUCCESS;
}

VkResult FilamentViewportVulkanPlatform::present(
    SwapChainPtr handle,
    uint32_t image_index,
    VkSemaphore finished_drawing)
{
    ViewportPlatformSwapChain* swapchain = viewport_swapchain_from(handle);
    if (swapchain == nullptr || !swapchain->is_valid()) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    std::optional<ViewportGpuImageHandle> handle_after_submit =
        swapchain->ring->mark_render_submitted(image_index, finished_drawing);
    if (!handle_after_submit.has_value()) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    impl_->submitted_frames.push(*handle_after_submit);
    return VK_SUCCESS;
}

bool FilamentViewportVulkanPlatform::hasResized(SwapChainPtr handle)
{
    (void)handle;
    return false;
}

bool FilamentViewportVulkanPlatform::isProtected(SwapChainPtr handle)
{
    (void)handle;
    return false;
}

VkResult FilamentViewportVulkanPlatform::recreate(SwapChainPtr handle)
{
    (void)handle;
    return VK_SUCCESS;
}

FilamentViewportVulkanPlatform::SwapChainPtr FilamentViewportVulkanPlatform::createSwapChain(
    void* native_window,
    uint64_t flags,
    VkExtent2D extent)
{
    (void)flags;
    (void)extent;

    std::unique_ptr<ViewportSwapChainToken> token = impl_->take_pending_token(native_window);
    if (token == nullptr) {
        return nullptr;
    }

    VulkanViewportImageRing& ring = impl_->ring_pool.ensure_ring(
        token->viewport_id,
        token->generation,
        token->extent);

    auto swapchain = std::make_unique<ViewportPlatformSwapChain>();
    swapchain->ring = &ring;
    return swapchain.release();
}

void FilamentViewportVulkanPlatform::destroy(SwapChainPtr handle)
{
    if (handle == nullptr) {
        return;
    }

    delete viewport_swapchain_from(handle);
}

} // namespace quader
