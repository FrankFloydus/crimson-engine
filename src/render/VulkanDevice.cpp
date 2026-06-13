module;
#include <vulkan/vulkan.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <vector>

module quader.render.vulkan_device;

import quader.render.vulkan_common;

namespace quader {
namespace {

struct InstanceRequest {
    std::vector<const char*> extensions;
    VkInstanceCreateFlags flags = 0;
};

struct QueueSelection {
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    uint32_t queue_family = UINT32_MAX;
    uint32_t queue_family_available_count = 0;
    VkPhysicalDeviceType device_type = VK_PHYSICAL_DEVICE_TYPE_OTHER;
};

constexpr uint32_t kAppQueueIndex = 0;
constexpr uint32_t kDedicatedRendererQueueIndex = 1;
constexpr uint32_t kDesiredGraphicsQueueCount = 2;

bool extension_list_contains(const std::vector<VkExtensionProperties>& extensions, const char* name)
{
    return std::any_of(extensions.begin(), extensions.end(), [name](const VkExtensionProperties& extension) {
        return std::strcmp(extension.extensionName, name) == 0;
    });
}

std::vector<VkExtensionProperties> ask_vulkan_for_instance_extensions()
{
    uint32_t count = 0;
    check_vk(vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr), "vkEnumerateInstanceExtensionProperties");

    std::vector<VkExtensionProperties> extensions(count);
    check_vk(vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data()), "vkEnumerateInstanceExtensionProperties");
    return extensions;
}

std::vector<VkExtensionProperties> ask_vulkan_for_device_extensions(VkPhysicalDevice physical_device)
{
    uint32_t count = 0;
    check_vk(vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &count, nullptr), "vkEnumerateDeviceExtensionProperties");

    std::vector<VkExtensionProperties> extensions(count);
    check_vk(vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &count, extensions.data()), "vkEnumerateDeviceExtensionProperties");
    return extensions;
}

std::vector<VkPhysicalDevice> ask_vulkan_for_physical_devices(VkInstance instance)
{
    uint32_t device_count = 0;
    check_vk(vkEnumeratePhysicalDevices(instance, &device_count, nullptr), "vkEnumeratePhysicalDevices");

    if (device_count == 0) {
        throw std::runtime_error("No Vulkan physical devices found.");
    }

    std::vector<VkPhysicalDevice> devices(device_count);
    check_vk(vkEnumeratePhysicalDevices(instance, &device_count, devices.data()), "vkEnumeratePhysicalDevices");
    return devices;
}

void add_extension_when_available(
    std::vector<const char*>& requested_extensions,
    const std::vector<VkExtensionProperties>& available_extensions,
    const char* extension_name)
{
    if (extension_list_contains(available_extensions, extension_name)) {
        requested_extensions.push_back(extension_name);
    }
}

InstanceRequest describe_instance_extensions_needed_by_sdl(const SdlWindow& window)
{
    uint32_t sdl_extension_count = 0;
    const char* const* sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_extension_count);
    if (sdl_extensions == nullptr) {
        throw std::runtime_error(window.vulkan_error());
    }

    InstanceRequest request;
    request.extensions.reserve(sdl_extension_count + 2);
    for (uint32_t index = 0; index < sdl_extension_count; ++index) {
        request.extensions.push_back(sdl_extensions[index]);
    }

    const std::vector<VkExtensionProperties> available_extensions = ask_vulkan_for_instance_extensions();
    add_extension_when_available(
        request.extensions,
        available_extensions,
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
    if (extension_list_contains(available_extensions, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        request.extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        request.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
#endif

    return request;
}

VkApplicationInfo describe_quader_to_vulkan()
{
    VkApplicationInfo app_info = {};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Quader Native";
    app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app_info.pEngineName = "Quader";
    app_info.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    app_info.apiVersion = kVulkanApiVersion;
    return app_info;
}

VkInstanceCreateInfo describe_the_instance_we_want(
    const VkApplicationInfo& app_info,
    const InstanceRequest& request)
{
    VkInstanceCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.flags = request.flags;
    create_info.pApplicationInfo = &app_info;
    create_info.enabledExtensionCount = static_cast<uint32_t>(request.extensions.size());
    create_info.ppEnabledExtensionNames = request.extensions.data();
    return create_info;
}

bool device_supports_swapchains(VkPhysicalDevice physical_device)
{
    const std::vector<VkExtensionProperties> extensions = ask_vulkan_for_device_extensions(physical_device);
    return extension_list_contains(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
}

VkPhysicalDeviceType device_type_of(VkPhysicalDevice physical_device)
{
    VkPhysicalDeviceProperties properties = {};
    vkGetPhysicalDeviceProperties(physical_device, &properties);
    return properties.deviceType;
}

int score_device_type(VkPhysicalDeviceType type)
{
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        return 4;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        return 3;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        return 2;
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        return 1;
    default:
        return 0;
    }
}

bool queue_family_can_draw(const VkQueueFamilyProperties& queue_family)
{
    return (queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
}

bool queue_family_can_present(VkPhysicalDevice physical_device, VkSurfaceKHR surface, uint32_t queue_family)
{
    VkBool32 present_supported = VK_FALSE;
    check_vk(
        vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, queue_family, surface, &present_supported),
        "vkGetPhysicalDeviceSurfaceSupportKHR");

    return present_supported == VK_TRUE;
}

std::vector<VkQueueFamilyProperties> queue_families_for(VkPhysicalDevice physical_device)
{
    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, nullptr);

    std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, queue_families.data());
    return queue_families;
}

std::optional<QueueSelection> find_queue_that_can_draw_and_present(
    VkPhysicalDevice physical_device,
    VkSurfaceKHR surface)
{
    if (!device_supports_swapchains(physical_device)) {
        return std::nullopt;
    }

    const std::vector<VkQueueFamilyProperties> queue_families = queue_families_for(physical_device);
    for (uint32_t index = 0; index < queue_families.size(); ++index) {
        if (queue_family_can_draw(queue_families[index]) &&
            queue_family_can_present(physical_device, surface, index)) {
            return QueueSelection {
                physical_device,
                index,
                queue_families[index].queueCount,
                device_type_of(physical_device),
            };
        }
    }

    return std::nullopt;
}

bool first_selection_is_better(
    const QueueSelection& first,
    const QueueSelection& second)
{
    return score_device_type(first.device_type) > score_device_type(second.device_type);
}

QueueSelection choose_best_device_for_window(VkInstance instance, VkSurfaceKHR surface)
{
    std::optional<QueueSelection> best_selection;

    for (VkPhysicalDevice device : ask_vulkan_for_physical_devices(instance)) {
        std::optional<QueueSelection> selection = find_queue_that_can_draw_and_present(device, surface);
        if (!selection.has_value()) {
            continue;
        }

        if (!best_selection.has_value() || first_selection_is_better(*selection, *best_selection)) {
            best_selection = selection;
        }
    }

    if (!best_selection.has_value()) {
        throw std::runtime_error("No Vulkan physical device supports graphics and presentation for this SDL window.");
    }

    return *best_selection;
}

std::vector<const char*> device_extensions_for(VkPhysicalDevice physical_device)
{
    std::vector<const char*> extensions { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    const std::vector<VkExtensionProperties> available_extensions = ask_vulkan_for_device_extensions(physical_device);
#ifdef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
    add_extension_when_available(extensions, available_extensions, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
#endif

    return extensions;
}

uint32_t choose_graphics_queue_count_to_create(uint32_t available_queue_count)
{
    if (available_queue_count == 0) {
        throw std::runtime_error("Selected Vulkan graphics queue family reports zero queues.");
    }

    return std::min(available_queue_count, kDesiredGraphicsQueueCount);
}

uint32_t renderer_queue_index_for(uint32_t created_queue_count)
{
    return created_queue_count >= kDesiredGraphicsQueueCount ? kDedicatedRendererQueueIndex : kAppQueueIndex;
}

VulkanSharedQueueMode queue_mode_for_created_queue_count(uint32_t created_queue_count)
{
    return created_queue_count >= kDesiredGraphicsQueueCount
        ? VulkanSharedQueueMode::dedicated_renderer_queue
        : VulkanSharedQueueMode::serialized_single_queue;
}

std::vector<float> describe_graphics_queue_priorities(uint32_t queue_count)
{
    return std::vector<float>(queue_count, 1.0f);
}

VkDeviceQueueCreateInfo describe_the_graphics_queues_we_need(
    uint32_t queue_family,
    const std::vector<float>& queue_priorities)
{
    VkDeviceQueueCreateInfo queue_info = {};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = queue_family;
    queue_info.queueCount = static_cast<uint32_t>(queue_priorities.size());
    queue_info.pQueuePriorities = queue_priorities.data();
    return queue_info;
}

VkDeviceCreateInfo describe_the_device_we_want(
    const VkDeviceQueueCreateInfo& queue_info,
    const std::vector<const char*>& extensions)
{
    VkDeviceCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info.queueCreateInfoCount = 1;
    create_info.pQueueCreateInfos = &queue_info;
    create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    create_info.ppEnabledExtensionNames = extensions.data();
    return create_info;
}

} // namespace

VulkanDevice::VulkanDevice(const SdlWindow& window)
{
    create_instance(window);
    create_surface(window);
    select_physical_device();
    create_device();
}

VulkanDevice::~VulkanDevice()
{
    wait_idle();

    if (surface_ != VK_NULL_HANDLE) {
        SDL_Vulkan_DestroySurface(instance_, surface_, allocator_);
    }

    if (device_ != VK_NULL_HANDLE) {
        vkDestroyDevice(device_, allocator_);
    }

    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, allocator_);
    }
}

VkInstance VulkanDevice::instance() const
{
    return instance_;
}

VkSurfaceKHR VulkanDevice::surface() const
{
    return surface_;
}

VkPhysicalDevice VulkanDevice::physical_device() const
{
    return physical_device_;
}

VkDevice VulkanDevice::device() const
{
    return device_;
}

uint32_t VulkanDevice::queue_family() const
{
    return queue_family_;
}

uint32_t VulkanDevice::queue_index() const
{
    return app_queue_index_;
}

uint32_t VulkanDevice::graphics_queue_family_available_count() const
{
    return queue_family_available_count_;
}

uint32_t VulkanDevice::created_graphics_queue_count() const
{
    return created_graphics_queue_count_;
}

VkQueue VulkanDevice::queue() const
{
    return queue_;
}

VkPipelineCache VulkanDevice::pipeline_cache() const
{
    return pipeline_cache_;
}

const VkAllocationCallbacks* VulkanDevice::allocator() const
{
    return allocator_;
}

VulkanSharedQueueMode VulkanDevice::queue_mode_for_renderer() const
{
    return queue_mode_for_renderer_;
}

VulkanSharedDeviceContext VulkanDevice::shared_device_context() const
{
    const VulkanQueueContext app_graphics_queue {
        queue_,
        queue_family_,
        app_queue_index_,
    };

    const VulkanQueueContext renderer_graphics_queue {
        renderer_queue_,
        queue_family_,
        renderer_queue_index_,
    };

    return VulkanSharedDeviceContext {
        instance_,
        physical_device_,
        device_,
        allocator_,
        pipeline_cache_,
        app_graphics_queue,
        renderer_graphics_queue,
        queue_family_available_count_,
        created_graphics_queue_count_,
        queue_mode_for_renderer_,
        true,
        true,
        false,
        false,
        false,
    };
}

void VulkanDevice::wait_idle() const
{
    if (device_ != VK_NULL_HANDLE) {
        check_vk(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle");
    }
}

void VulkanDevice::create_instance(const SdlWindow& window)
{
    const InstanceRequest instance_request = describe_instance_extensions_needed_by_sdl(window);
    const VkApplicationInfo app_info = describe_quader_to_vulkan();
    const VkInstanceCreateInfo create_info = describe_the_instance_we_want(app_info, instance_request);

    check_vk(vkCreateInstance(&create_info, allocator_, &instance_), "vkCreateInstance");
}

void VulkanDevice::create_surface(const SdlWindow& window)
{
    if (!SDL_Vulkan_CreateSurface(window.native_handle(), instance_, allocator_, &surface_)) {
        throw std::runtime_error(window.vulkan_error());
    }
}

void VulkanDevice::select_physical_device()
{
    const QueueSelection selection = choose_best_device_for_window(instance_, surface_);
    physical_device_ = selection.physical_device;
    queue_family_ = selection.queue_family;
    queue_family_available_count_ = selection.queue_family_available_count;
}

void VulkanDevice::create_device()
{
    const std::vector<const char*> extensions = device_extensions_for(physical_device_);
    created_graphics_queue_count_ = choose_graphics_queue_count_to_create(queue_family_available_count_);
    const std::vector<float> queue_priorities = describe_graphics_queue_priorities(created_graphics_queue_count_);
    const VkDeviceQueueCreateInfo queue_info = describe_the_graphics_queues_we_need(queue_family_, queue_priorities);
    const VkDeviceCreateInfo create_info = describe_the_device_we_want(queue_info, extensions);

    check_vk(vkCreateDevice(physical_device_, &create_info, allocator_, &device_), "vkCreateDevice");
    app_queue_index_ = kAppQueueIndex;
    renderer_queue_index_ = renderer_queue_index_for(created_graphics_queue_count_);
    queue_mode_for_renderer_ = queue_mode_for_created_queue_count(created_graphics_queue_count_);
    vkGetDeviceQueue(device_, queue_family_, app_queue_index_, &queue_);
    vkGetDeviceQueue(device_, queue_family_, renderer_queue_index_, &renderer_queue_);
}

} // namespace quader
