module;

#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <tracy/Tracy.hpp>
#include <SDL3/SDL.h>

#include <iterator>
#include <memory>
#include <stdexcept>

module quader.render.vulkan_swapchain;

import quader.render.vulkan_common;

namespace quader {
namespace {

VkExtent2D framebuffer_size_of(const SdlWindow& window)
{
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(window.native_handle(), &width, &height);

    return VkExtent2D {
        static_cast<uint32_t>(width > 0 ? width : 0),
        static_cast<uint32_t>(height > 0 ? height : 0),
    };
}

bool window_is_minimized(const SdlWindow& window)
{
    return (SDL_GetWindowFlags(window.native_handle()) & SDL_WINDOW_MINIMIZED) != 0;
}

bool framebuffer_has_pixels(VkExtent2D size)
{
    return size.width > 0 && size.height > 0;
}

bool result_means_swapchain_changed(VkResult result)
{
    return result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR;
}

VkRenderPassBeginInfo describe_render_pass(
    const ImGui_ImplVulkanH_Window& window_data,
    const ImGui_ImplVulkanH_Frame& frame)
{
    VkRenderPassBeginInfo render_pass_info = {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = window_data.RenderPass;
    render_pass_info.framebuffer = frame.Framebuffer;
    render_pass_info.renderArea.extent.width = window_data.Width;
    render_pass_info.renderArea.extent.height = window_data.Height;
    render_pass_info.clearValueCount = 1;
    render_pass_info.pClearValues = &window_data.ClearValue;
    return render_pass_info;
}

} // namespace

struct VulkanSwapchain::Resources {
    Resources(const VulkanDevice& device, const SdlWindow& window)
        : device_(device)
    {
        create_or_resize(window);
    }

    Resources(const Resources&) = delete;
    Resources& operator=(const Resources&) = delete;

    ~Resources()
    {
        if (window_data_.Swapchain != VK_NULL_HANDLE) {
            ImGui_ImplVulkanH_DestroyWindow(device_.instance(), device_.device(), &window_data_, device_.allocator());
        }
    }

    [[nodiscard]] uint32_t min_image_count() const
    {
        return min_image_count_;
    }

    [[nodiscard]] uint32_t image_count() const
    {
        return window_data_.ImageCount;
    }

    [[nodiscard]] VkRenderPass render_pass() const
    {
        return window_data_.RenderPass;
    }

    void recreate_if_needed(const SdlWindow& window)
    {
        if (window_is_minimized(window)) {
            SDL_Delay(10);
            return;
        }

        const VkExtent2D framebuffer_size = framebuffer_size_of(window);
        if (!framebuffer_has_pixels(framebuffer_size) || already_matches(framebuffer_size)) {
            return;
        }

        recreate_swapchain(framebuffer_size);
    }

    void render_current_imgui_frame()
    {
        ZoneScopedN("Vulkan swapchain render");

        ImDrawData* draw_data = ImGui::GetDrawData();
        if (draw_data == nullptr || draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f) {
            return;
        }

        window_data_.ClearValue.color.float32[0] = 0.09f;
        window_data_.ClearValue.color.float32[1] = 0.10f;
        window_data_.ClearValue.color.float32[2] = 0.12f;
        window_data_.ClearValue.color.float32[3] = 1.00f;

        frame_render(draw_data);
        frame_present();
    }

private:
    void create_or_resize(const SdlWindow& window)
    {
        ensure_selected_queue_can_present();
        window_data_.Surface = device_.surface();
        choose_surface_format();
        choose_present_mode();
        create_initial_swapchain(framebuffer_size_of(window));
    }

    void ensure_selected_queue_can_present() const
    {
        VkBool32 present_supported = VK_FALSE;
        check_vk(
            vkGetPhysicalDeviceSurfaceSupportKHR(
                device_.physical_device(),
                device_.queue_family(),
                device_.surface(),
                &present_supported),
            "vkGetPhysicalDeviceSurfaceSupportKHR");

        if (present_supported != VK_TRUE) {
            throw std::runtime_error("Selected Vulkan queue family does not support presentation.");
        }
    }

    void choose_surface_format()
    {
        const VkFormat requested_formats[] = {
            VK_FORMAT_B8G8R8A8_UNORM,
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_FORMAT_B8G8R8_UNORM,
            VK_FORMAT_R8G8B8_UNORM,
        };

        window_data_.SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(
            device_.physical_device(),
            window_data_.Surface,
            requested_formats,
            static_cast<int>(std::size(requested_formats)),
            VK_COLORSPACE_SRGB_NONLINEAR_KHR);
    }

    void choose_present_mode()
    {
        const VkPresentModeKHR present_modes[] = { VK_PRESENT_MODE_FIFO_KHR };

        window_data_.PresentMode = ImGui_ImplVulkanH_SelectPresentMode(
            device_.physical_device(),
            window_data_.Surface,
            present_modes,
            static_cast<int>(std::size(present_modes)));
    }

    void create_initial_swapchain(VkExtent2D framebuffer_size)
    {
        if (!framebuffer_has_pixels(framebuffer_size)) {
            return;
        }

        ImGui_ImplVulkanH_CreateOrResizeWindow(
            device_.instance(),
            device_.physical_device(),
            device_.device(),
            &window_data_,
            device_.queue_family(),
            device_.allocator(),
            static_cast<int>(framebuffer_size.width),
            static_cast<int>(framebuffer_size.height),
            min_image_count_,
            0);

        window_data_.FrameIndex = 0;
        swapchain_rebuild_ = false;
    }

    void recreate_swapchain(VkExtent2D framebuffer_size)
    {
        ImGui_ImplVulkan_SetMinImageCount(min_image_count_);
        create_initial_swapchain(framebuffer_size);
    }

    [[nodiscard]] bool already_matches(VkExtent2D framebuffer_size) const
    {
        return !swapchain_rebuild_ &&
            window_data_.Width == static_cast<int>(framebuffer_size.width) &&
            window_data_.Height == static_cast<int>(framebuffer_size.height);
    }

    [[nodiscard]] bool acquire_next_image()
    {
        VkSemaphore image_acquired = window_data_.FrameSemaphores[window_data_.SemaphoreIndex].ImageAcquiredSemaphore;

        VkResult result = vkAcquireNextImageKHR(
            device_.device(),
            window_data_.Swapchain,
            UINT64_MAX,
            image_acquired,
            VK_NULL_HANDLE,
            &window_data_.FrameIndex);

        if (result_means_swapchain_changed(result)) {
            swapchain_rebuild_ = true;
        }

        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            return false;
        }

        if (result != VK_SUBOPTIMAL_KHR) {
            check_vk(result, "vkAcquireNextImageKHR");
        }

        return true;
    }

    void frame_render(ImDrawData* draw_data)
    {
        ZoneScopedN("Vulkan frame render");

        if (!acquire_next_image()) {
            return;
        }

        ImGui_ImplVulkanH_Frame& frame = window_data_.Frames[window_data_.FrameIndex];

        wait_until_frame_is_available(frame);
        begin_frame_commands(frame);
        begin_render_pass(frame);
        draw_imgui(draw_data, frame);
        submit_frame(frame);
    }

    void wait_until_frame_is_available(ImGui_ImplVulkanH_Frame& frame) const
    {
        check_vk(vkWaitForFences(device_.device(), 1, &frame.Fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");
        check_vk(vkResetFences(device_.device(), 1, &frame.Fence), "vkResetFences");
        check_vk(vkResetCommandPool(device_.device(), frame.CommandPool, 0), "vkResetCommandPool");
    }

    void begin_frame_commands(ImGui_ImplVulkanH_Frame& frame) const
    {
        VkCommandBufferBeginInfo command_buffer_info = {};
        command_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        command_buffer_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        check_vk(vkBeginCommandBuffer(frame.CommandBuffer, &command_buffer_info), "vkBeginCommandBuffer");
    }

    void begin_render_pass(ImGui_ImplVulkanH_Frame& frame) const
    {
        const VkRenderPassBeginInfo render_pass_info = describe_render_pass(window_data_, frame);
        vkCmdBeginRenderPass(frame.CommandBuffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
    }

    void draw_imgui(ImDrawData* draw_data, ImGui_ImplVulkanH_Frame& frame) const
    {
        ImGui_ImplVulkan_RenderDrawData(draw_data, frame.CommandBuffer);
        vkCmdEndRenderPass(frame.CommandBuffer);
    }

    void submit_frame(ImGui_ImplVulkanH_Frame& frame)
    {
        VkSemaphore image_acquired = window_data_.FrameSemaphores[window_data_.SemaphoreIndex].ImageAcquiredSemaphore;
        VkSemaphore render_complete = window_data_.FrameSemaphores[window_data_.SemaphoreIndex].RenderCompleteSemaphore;
        const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        VkSubmitInfo submit_info = {};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = &image_acquired;
        submit_info.pWaitDstStageMask = &wait_stage;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &frame.CommandBuffer;
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = &render_complete;

        check_vk(vkEndCommandBuffer(frame.CommandBuffer), "vkEndCommandBuffer");
        check_vk(vkQueueSubmit(device_.queue(), 1, &submit_info, frame.Fence), "vkQueueSubmit");
    }

    void frame_present()
    {
        if (swapchain_rebuild_) {
            return;
        }

        VkSemaphore render_complete = window_data_.FrameSemaphores[window_data_.SemaphoreIndex].RenderCompleteSemaphore;

        VkPresentInfoKHR present_info = {};
        present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present_info.waitSemaphoreCount = 1;
        present_info.pWaitSemaphores = &render_complete;
        present_info.swapchainCount = 1;
        present_info.pSwapchains = &window_data_.Swapchain;
        present_info.pImageIndices = &window_data_.FrameIndex;

        VkResult result = vkQueuePresentKHR(device_.queue(), &present_info);
        if (result_means_swapchain_changed(result)) {
            swapchain_rebuild_ = true;
        }

        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            return;
        }

        if (result != VK_SUBOPTIMAL_KHR) {
            check_vk(result, "vkQueuePresentKHR");
        }

        window_data_.SemaphoreIndex = (window_data_.SemaphoreIndex + 1) % window_data_.SemaphoreCount;
    }

    const VulkanDevice& device_;
    ImGui_ImplVulkanH_Window window_data_;
    uint32_t min_image_count_ = 2;
    bool swapchain_rebuild_ = false;
};

VulkanSwapchain::VulkanSwapchain(const VulkanDevice& device, const SdlWindow& window)
    : resources_(std::make_unique<Resources>(device, window))
{
}

VulkanSwapchain::~VulkanSwapchain() = default;

uint32_t VulkanSwapchain::min_image_count() const
{
    return resources_->min_image_count();
}

uint32_t VulkanSwapchain::image_count() const
{
    return resources_->image_count();
}

VkRenderPass VulkanSwapchain::render_pass() const
{
    return resources_->render_pass();
}

void VulkanSwapchain::recreate_if_needed(const SdlWindow& window)
{
    resources_->recreate_if_needed(window);
}

void VulkanSwapchain::render_current_imgui_frame()
{
    resources_->render_current_imgui_frame();
}

} // namespace quader
