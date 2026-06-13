module;
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <iterator>
#include <memory>
#include <span>
#include <stdexcept>

module quader.ui.imgui_layer;

import quader.platform.user_settings_paths;
import quader.render.vulkan_common;
import quader.ui.imgui_backend_common;
import quader.ui.viewport_texture_store;

namespace quader {
namespace {

class ImGuiViewportTextureRegistry final : public IViewportTextureRegistry {
public:
    [[nodiscard]] uint64_t add_texture(const ViewportSampledGpuImage& image) override
    {
        if (image.backend != ViewportSampledGpuImageBackend::vulkan) {
            return 0;
        }

        VkDescriptorSet descriptor = ImGui_ImplVulkan_AddTexture(
            image.sampler,
            image.image_view,
            image.image_layout);
        return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(descriptor));
    }

    void remove_texture(uint64_t texture_id) override
    {
        if (texture_id == 0) {
            return;
        }

        ImGui_ImplVulkan_RemoveTexture(
            reinterpret_cast<VkDescriptorSet>(static_cast<uintptr_t>(texture_id)));
    }
};

} // namespace

class ImGuiLayer::Impl {
public:
    Impl(const SdlWindow& window, const VulkanDevice& device, const VulkanSwapchain& swapchain);

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    ~Impl();

    void process_sdl_event(const SDL_Event& event);
    void begin_frame();
    void end_frame(VulkanSwapchain& swapchain);
    [[nodiscard]] bool present_viewport_gpu_image(const ViewportSampledGpuImage& image);
    [[nodiscard]] bool has_retired_viewport_gpu_textures() const noexcept;
    void collect_retired_viewport_gpu_textures();
    void remove_viewport_texture(ViewportId viewport_id);
    void prune_viewport_textures(std::span<const ViewportId> live_viewport_ids);

    [[nodiscard]] bool text_input_active() const;
    [[nodiscard]] ViewportTextureView viewport_texture(ViewportId viewport_id) const;

private:
    VkDescriptorPool create_descriptor_pool();
    void shutdown_backend_state();

    const VulkanDevice& device_;
    UserSettingsPaths user_settings_paths_;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    ImGuiViewportTextureRegistry texture_registry_;
    std::unique_ptr<ViewportTextureStore> viewport_textures_;
    bool imgui_context_created_ = false;
    bool sdl_backend_initialized_ = false;
    bool vulkan_backend_initialized_ = false;
};

ImGuiLayer::Impl::Impl(const SdlWindow& window, const VulkanDevice& device, const VulkanSwapchain& swapchain)
    : device_(device)
{
    try {
        descriptor_pool_ = create_descriptor_pool();

        create_editor_imgui_context(user_settings_paths_.imgui_ini_file_c_str());
        imgui_context_created_ = true;

        if (!ImGui_ImplSDL3_InitForVulkan(window.native_handle())) {
            throw std::runtime_error("ImGui SDL3 backend failed to initialize for Vulkan.");
        }
        sdl_backend_initialized_ = true;

        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.ApiVersion = kVulkanApiVersion;
        init_info.Instance = device_.instance();
        init_info.PhysicalDevice = device_.physical_device();
        init_info.Device = device_.device();
        init_info.QueueFamily = device_.queue_family();
        init_info.Queue = device_.queue();
        init_info.PipelineCache = device_.pipeline_cache();
        init_info.DescriptorPool = descriptor_pool_;
        init_info.MinImageCount = swapchain.min_image_count();
        init_info.ImageCount = swapchain.image_count();
        init_info.Allocator = device_.allocator();
        init_info.PipelineInfoMain.RenderPass = swapchain.render_pass();
        init_info.PipelineInfoMain.Subpass = 0;
        init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        init_info.CheckVkResultFn = check_vk_result_for_imgui;

        if (!ImGui_ImplVulkan_Init(&init_info)) {
            throw std::runtime_error("ImGui Vulkan backend failed to initialize.");
        }
        vulkan_backend_initialized_ = true;

        viewport_textures_ = std::make_unique<ViewportTextureStore>(texture_registry_);
    } catch (...) {
        shutdown_backend_state();
        throw;
    }
}

ImGuiLayer::Impl::~Impl()
{
    shutdown_backend_state();
}

void ImGuiLayer::Impl::shutdown_backend_state()
{
    device_.wait_idle();

    viewport_textures_.reset();

    if (vulkan_backend_initialized_) {
        ImGui_ImplVulkan_Shutdown();
        vulkan_backend_initialized_ = false;
    }

    if (sdl_backend_initialized_) {
        ImGui_ImplSDL3_Shutdown();
        sdl_backend_initialized_ = false;
    }

    if (imgui_context_created_) {
        ImGui::DestroyContext();
        imgui_context_created_ = false;
    }

    if (descriptor_pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_.device(), descriptor_pool_, device_.allocator());
        descriptor_pool_ = VK_NULL_HANDLE;
    }
}

void ImGuiLayer::Impl::process_sdl_event(const SDL_Event& event)
{
    ImGui_ImplSDL3_ProcessEvent(&event);
}

void ImGuiLayer::Impl::begin_frame()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::Impl::end_frame(VulkanSwapchain& swapchain)
{
    ImGui::Render();
    swapchain.render_current_imgui_frame();
}

bool ImGuiLayer::Impl::present_viewport_gpu_image(const ViewportSampledGpuImage& image)
{
    return viewport_textures_ != nullptr && viewport_textures_->present_gpu_image(image);
}

bool ImGuiLayer::Impl::has_retired_viewport_gpu_textures() const noexcept
{
    return viewport_textures_ != nullptr && viewport_textures_->has_retired_gpu_textures();
}

void ImGuiLayer::Impl::collect_retired_viewport_gpu_textures()
{
    if (viewport_textures_ != nullptr) {
        viewport_textures_->collect_retired_gpu_textures();
    }
}

void ImGuiLayer::Impl::remove_viewport_texture(ViewportId viewport_id)
{
    if (viewport_textures_ != nullptr) {
        viewport_textures_->remove(viewport_id);
    }
}

void ImGuiLayer::Impl::prune_viewport_textures(std::span<const ViewportId> live_viewport_ids)
{
    if (viewport_textures_ != nullptr) {
        viewport_textures_->prune(live_viewport_ids);
    }
}

bool ImGuiLayer::Impl::text_input_active() const
{
    return ImGui::GetIO().WantTextInput;
}

ViewportTextureView ImGuiLayer::Impl::viewport_texture(ViewportId viewport_id) const
{
    return viewport_textures_ ? viewport_textures_->texture_view(viewport_id) : ViewportTextureView {};
}

VkDescriptorPool ImGuiLayer::Impl::create_descriptor_pool()
{
    VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE },
        { VK_DESCRIPTOR_TYPE_SAMPLER, IMGUI_IMPL_VULKAN_MINIMUM_SAMPLER_POOL_SIZE },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 32 },
    };

    uint32_t max_sets = 0;
    for (const VkDescriptorPoolSize& pool_size : pool_sizes) {
        max_sets += pool_size.descriptorCount;
    }

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = max_sets;
    pool_info.poolSizeCount = static_cast<uint32_t>(std::size(pool_sizes));
    pool_info.pPoolSizes = pool_sizes;

    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    check_vk(vkCreateDescriptorPool(device_.device(), &pool_info, device_.allocator(), &descriptor_pool), "vkCreateDescriptorPool");
    return descriptor_pool;
}

ImGuiLayer::ImGuiLayer(const SdlWindow& window, const VulkanDevice& device, const VulkanSwapchain& swapchain)
    : impl_(std::make_unique<Impl>(window, device, swapchain))
{
}

ImGuiLayer::~ImGuiLayer() = default;

void ImGuiLayer::process_sdl_event(const SDL_Event& event)
{
    impl_->process_sdl_event(event);
}

void ImGuiLayer::begin_frame()
{
    impl_->begin_frame();
}

void ImGuiLayer::end_frame(VulkanSwapchain& swapchain)
{
    impl_->end_frame(swapchain);
}

bool ImGuiLayer::present_viewport_gpu_image(const ViewportSampledGpuImage& image)
{
    return impl_->present_viewport_gpu_image(image);
}

bool ImGuiLayer::has_retired_viewport_gpu_textures() const noexcept
{
    return impl_->has_retired_viewport_gpu_textures();
}

void ImGuiLayer::collect_retired_viewport_gpu_textures()
{
    impl_->collect_retired_viewport_gpu_textures();
}

void ImGuiLayer::remove_viewport_texture(ViewportId viewport_id)
{
    impl_->remove_viewport_texture(viewport_id);
}

void ImGuiLayer::prune_viewport_textures(std::span<const ViewportId> live_viewport_ids)
{
    impl_->prune_viewport_textures(live_viewport_ids);
}

bool ImGuiLayer::text_input_active() const
{
    return impl_->text_input_active();
}

ViewportTextureView ImGuiLayer::viewport_texture(ViewportId viewport_id) const
{
    return impl_->viewport_texture(viewport_id);
}

} // namespace quader
