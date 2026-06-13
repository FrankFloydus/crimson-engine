#include <gtest/gtest.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

import quader.ui.viewport_gpu_texture_cache;

namespace {

template <typename Handle>
Handle fake_vulkan_handle(std::uintptr_t value)
{
    return reinterpret_cast<Handle>(value);
}

class FakeTextureRegistry final : public quader::IViewportTextureRegistry {
public:
    [[nodiscard]] uint64_t add_texture(
        VkSampler sampler,
        VkImageView image_view,
        VkImageLayout image_layout) override
    {
        added_samplers.push_back(sampler);
        added_image_views.push_back(image_view);
        added_layouts.push_back(image_layout);
        return next_texture_id++;
    }

    void remove_texture(uint64_t texture_id) override
    {
        removed_texture_ids.push_back(texture_id);
    }

    uint64_t next_texture_id = 100;
    std::vector<VkSampler> added_samplers;
    std::vector<VkImageView> added_image_views;
    std::vector<VkImageLayout> added_layouts;
    std::vector<uint64_t> removed_texture_ids;
};

quader::ViewportSampledGpuImage ready_image(
    uint64_t viewport_id = 1,
    uint64_t generation = 2,
    uint64_t resource_id = 3,
    uint64_t image_generation = 4,
    std::uintptr_t image_view = 5,
    std::uintptr_t sampler = 6)
{
    return quader::ViewportSampledGpuImage {
        .handle = quader::ViewportGpuImageHandle {
            .viewport_id = quader::ViewportId { viewport_id },
            .generation = quader::ViewportGeneration { generation },
            .slot_index = 1,
            .image_generation = image_generation,
            .resource_id = resource_id,
            .extent = { 640, 360 },
            .readiness = quader::ViewportGpuImageReadiness::ready,
        },
        .image_view = fake_vulkan_handle<VkImageView>(image_view),
        .sampler = fake_vulkan_handle<VkSampler>(sampler),
        .image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
}

} // namespace

TEST(ViewportGpuTextureCache, PresentsReadyImageAsViewportTextureView)
{
    FakeTextureRegistry registry;
    quader::ViewportGpuTextureDescriptorCache cache(registry);

    EXPECT_TRUE(cache.present(ready_image()));

    const quader::ViewportTextureView view = cache.texture_view(quader::ViewportId { 1 });
    EXPECT_TRUE(view.has_pixels);
    EXPECT_EQ(100U, view.texture_id);
    EXPECT_EQ(640U, view.extent.width);
    EXPECT_EQ(360U, view.extent.height);
    EXPECT_EQ(1U, registry.added_image_views.size());
    EXPECT_TRUE(registry.removed_texture_ids.empty());
}

TEST(ViewportGpuTextureCache, ReusesDescriptorForSameImageKey)
{
    FakeTextureRegistry registry;
    quader::ViewportGpuTextureDescriptorCache cache(registry);

    EXPECT_TRUE(cache.present(ready_image()));
    EXPECT_TRUE(cache.present(ready_image()));

    EXPECT_EQ(1U, registry.added_image_views.size());
    EXPECT_TRUE(registry.removed_texture_ids.empty());
    EXPECT_EQ(100U, cache.texture_view(quader::ViewportId { 1 }).texture_id);
}

TEST(ViewportGpuTextureCache, ReplacesDescriptorWhenImageGenerationChanges)
{
    FakeTextureRegistry registry;
    quader::ViewportGpuTextureDescriptorCache cache(registry);

    EXPECT_TRUE(cache.present(ready_image()));
    EXPECT_TRUE(cache.present(ready_image(1, 2, 3, 5, 7, 6)));

    EXPECT_EQ(2U, registry.added_image_views.size());
    EXPECT_TRUE(registry.removed_texture_ids.empty());
    EXPECT_TRUE(cache.has_retired_textures());

    cache.collect_retired_textures();
    ASSERT_EQ(1U, registry.removed_texture_ids.size());
    EXPECT_EQ(100U, registry.removed_texture_ids[0]);
    EXPECT_FALSE(cache.has_retired_textures());
    EXPECT_EQ(101U, cache.texture_view(quader::ViewportId { 1 }).texture_id);
}

TEST(ViewportGpuTextureCache, RejectsRenderingOrUnresolvedImages)
{
    FakeTextureRegistry registry;
    quader::ViewportGpuTextureDescriptorCache cache(registry);

    quader::ViewportSampledGpuImage image = ready_image();
    image.handle.readiness = quader::ViewportGpuImageReadiness::rendering;
    EXPECT_FALSE(cache.present(image));

    image = ready_image();
    image.image_view = VK_NULL_HANDLE;
    EXPECT_FALSE(cache.present(image));

    EXPECT_TRUE(registry.added_image_views.empty());
    EXPECT_FALSE(cache.texture_view(quader::ViewportId { 1 }).has_pixels);
}

TEST(ViewportGpuTextureCache, PruneRemovesDescriptorsForHiddenViewports)
{
    FakeTextureRegistry registry;
    quader::ViewportGpuTextureDescriptorCache cache(registry);

    EXPECT_TRUE(cache.present(ready_image(1)));
    EXPECT_TRUE(cache.present(ready_image(2, 2, 9, 4, 8, 6)));

    const quader::ViewportId live[] = { quader::ViewportId { 2 } };
    cache.prune(live);

    EXPECT_FALSE(cache.texture_view(quader::ViewportId { 1 }).has_pixels);
    EXPECT_TRUE(cache.texture_view(quader::ViewportId { 2 }).has_pixels);
    EXPECT_TRUE(registry.removed_texture_ids.empty());
    EXPECT_TRUE(cache.has_retired_textures());

    cache.collect_retired_textures();
    ASSERT_EQ(1U, registry.removed_texture_ids.size());
    EXPECT_EQ(100U, registry.removed_texture_ids[0]);
}
