module;
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <iterator>
#include <memory>
#include <span>
#include <stdexcept>

module quader.ui.imgui_layer;

import quader.render.vulkan_common;

namespace quader {

ImGuiLayer::ImGuiLayer(const SdlWindow& window, const VulkanDevice& device, const VulkanSwapchain& swapchain)
    : device_(device)
{
    descriptor_pool_ = create_descriptor_pool();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    io.IniFilename = "quader-native-imgui.ini";

    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.TabRounding = 3.0f;

    ImGui_ImplSDL3_InitForVulkan(window.native_handle());

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
        vkDestroyDescriptorPool(device_.device(), descriptor_pool_, device_.allocator());
        descriptor_pool_ = VK_NULL_HANDLE;
        throw std::runtime_error("ImGui Vulkan backend failed to initialize.");
    }

    viewport_textures_ = std::make_unique<ViewportTextureStore>(device_);
}

ImGuiLayer::~ImGuiLayer()
{
    device_.wait_idle();

    viewport_textures_.reset();

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    if (descriptor_pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_.device(), descriptor_pool_, device_.allocator());
    }
}

void ImGuiLayer::process_sdl_event(const SDL_Event& event)
{
    ImGui_ImplSDL3_ProcessEvent(&event);
}

void ImGuiLayer::begin_frame()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::end_frame(VulkanSwapchain& swapchain)
{
    ImGui::Render();
    swapchain.render_current_imgui_frame();
}

void ImGuiLayer::upload_viewport_texture(ViewportId viewport_id, uint32_t width, uint32_t height, const uint8_t* rgba)
{
    if (viewport_textures_ != nullptr) {
        viewport_textures_->upload(viewport_id, width, height, rgba);
    }
}

void ImGuiLayer::remove_viewport_texture(ViewportId viewport_id)
{
    if (viewport_textures_ != nullptr) {
        viewport_textures_->remove(viewport_id);
    }
}

void ImGuiLayer::prune_viewport_textures(std::span<const ViewportId> live_viewport_ids)
{
    if (viewport_textures_ != nullptr) {
        viewport_textures_->prune(live_viewport_ids);
    }
}

ViewportTextureView ImGuiLayer::viewport_texture(ViewportId viewport_id) const
{
    return viewport_textures_ ? viewport_textures_->texture_view(viewport_id) : ViewportTextureView {};
}

VkDescriptorPool ImGuiLayer::create_descriptor_pool()
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

} // namespace quader
