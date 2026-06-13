#include <gtest/gtest.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
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
    [[nodiscard]] uint64_t add_texture(const quader::ViewportTextureToken& texture) override
    {
        added_backends.push_back(texture.backend);
        added_tokens.push_back(texture);

        if (const std::optional<quader::ViewportVulkanTextureTokenPayload> vulkan_payload =
                quader::unpack_vulkan_viewport_texture_token(texture)) {
            added_samplers.push_back(vulkan_payload->sampler);
            added_image_views.push_back(vulkan_payload->image_view);
            added_layouts.push_back(vulkan_payload->image_layout);
        }

        if (const std::optional<quader::ViewportOpenGlTextureTokenPayload> opengl_payload =
                quader::unpack_opengl_viewport_texture_token(texture)) {
            added_opengl_texture_ids.push_back(opengl_payload->texture_id);
        }

        return next_texture_id++;
    }

    void remove_texture(uint64_t texture_id) override
    {
        removed_texture_ids.push_back(texture_id);
    }

    uint64_t next_texture_id = 100;
    std::vector<quader::ViewportTextureToken> added_tokens;
    std::vector<quader::ViewportTextureTokenBackend> added_backends;
    std::vector<VkSampler> added_samplers;
    std::vector<VkImageView> added_image_views;
    std::vector<VkImageLayout> added_layouts;
    std::vector<uint32_t> added_opengl_texture_ids;
    std::vector<uint64_t> removed_texture_ids;
};

quader::ViewportGpuImageHandle ready_handle(
    uint64_t viewport_id = 1,
    uint64_t generation = 2,
    uint64_t resource_id = 3,
    uint64_t image_generation = 4)
{
    return quader::ViewportGpuImageHandle {
        .viewport_id = quader::ViewportId { viewport_id },
        .generation = quader::ViewportGeneration { generation },
        .slot_index = 1,
        .image_generation = image_generation,
        .resource_id = resource_id,
        .extent = { 640, 360 },
        .readiness = quader::ViewportGpuImageReadiness::ready,
    };
}

quader::ViewportTextureToken ready_vulkan_texture(
    uint64_t viewport_id = 1,
    uint64_t generation = 2,
    uint64_t resource_id = 3,
    uint64_t image_generation = 4,
    std::uintptr_t image_view = 5,
    std::uintptr_t sampler = 6)
{
    return quader::make_vulkan_viewport_texture_token(
        ready_handle(viewport_id, generation, resource_id, image_generation),
        quader::ViewportVulkanTextureTokenPayload {
            .image_view = fake_vulkan_handle<VkImageView>(image_view),
            .sampler = fake_vulkan_handle<VkSampler>(sampler),
            .image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        });
}

quader::ViewportTextureToken ready_opengl_texture(
    uint64_t texture_id = 42,
    uint64_t viewport_id = 1,
    uint64_t generation = 2,
    uint64_t resource_id = 3,
    uint64_t image_generation = 4)
{
    return quader::make_opengl_viewport_texture_token(
        ready_handle(viewport_id, generation, resource_id, image_generation),
        quader::ViewportOpenGlTextureTokenPayload {
            .texture_id = static_cast<uint32_t>(texture_id),
        });
}

} // namespace

TEST(ViewportGpuTextureCache, PresentsReadyTokenAsViewportTextureView)
{
    FakeTextureRegistry registry;
    quader::ViewportGpuTextureDescriptorCache cache(registry);

    EXPECT_TRUE(cache.present(ready_vulkan_texture()));

    const quader::ViewportTextureView view = cache.texture_view(quader::ViewportId { 1 });
    EXPECT_TRUE(view.has_pixels);
    EXPECT_EQ(100U, view.texture_id);
    EXPECT_EQ(640U, view.extent.width);
    EXPECT_EQ(360U, view.extent.height);
    EXPECT_EQ(quader::ViewportTextureUvOrigin::top_left, view.uv_origin);
    EXPECT_EQ(1U, registry.added_image_views.size());
    EXPECT_TRUE(registry.removed_texture_ids.empty());
}

TEST(ViewportGpuTextureCache, ReusesDescriptorForSameTextureToken)
{
    FakeTextureRegistry registry;
    quader::ViewportGpuTextureDescriptorCache cache(registry);

    EXPECT_TRUE(cache.present(ready_vulkan_texture()));
    EXPECT_TRUE(cache.present(ready_vulkan_texture()));

    EXPECT_EQ(1U, registry.added_image_views.size());
    EXPECT_TRUE(registry.removed_texture_ids.empty());
    EXPECT_EQ(100U, cache.texture_view(quader::ViewportId { 1 }).texture_id);
}

TEST(ViewportGpuTextureCache, ReplacesDescriptorWhenImageGenerationChanges)
{
    FakeTextureRegistry registry;
    quader::ViewportGpuTextureDescriptorCache cache(registry);

    EXPECT_TRUE(cache.present(ready_vulkan_texture()));
    EXPECT_TRUE(cache.present(ready_vulkan_texture(1, 2, 3, 5, 7, 6)));

    EXPECT_EQ(2U, registry.added_image_views.size());
    EXPECT_TRUE(registry.removed_texture_ids.empty());
    EXPECT_TRUE(cache.has_retired_textures());

    cache.collect_retired_textures();
    ASSERT_EQ(1U, registry.removed_texture_ids.size());
    EXPECT_EQ(100U, registry.removed_texture_ids[0]);
    EXPECT_FALSE(cache.has_retired_textures());
    EXPECT_EQ(101U, cache.texture_view(quader::ViewportId { 1 }).texture_id);
}

TEST(ViewportGpuTextureCache, ReplacesDescriptorWhenOpaqueTexturePayloadChanges)
{
    FakeTextureRegistry registry;
    quader::ViewportGpuTextureDescriptorCache cache(registry);

    EXPECT_TRUE(cache.present(ready_vulkan_texture()));
    EXPECT_TRUE(cache.present(ready_vulkan_texture(1, 2, 3, 4, 9, 6)));

    EXPECT_EQ(2U, registry.added_tokens.size());
    EXPECT_TRUE(cache.has_retired_textures());

    cache.collect_retired_textures();
    ASSERT_EQ(1U, registry.removed_texture_ids.size());
    EXPECT_EQ(100U, registry.removed_texture_ids[0]);
    EXPECT_EQ(101U, cache.texture_view(quader::ViewportId { 1 }).texture_id);
}

TEST(ViewportGpuTextureCache, RejectsRenderingOrUnresolvedTokens)
{
    FakeTextureRegistry registry;
    quader::ViewportGpuTextureDescriptorCache cache(registry);

    quader::ViewportTextureToken texture = ready_vulkan_texture();
    texture.handle.readiness = quader::ViewportGpuImageReadiness::rendering;
    EXPECT_FALSE(cache.present(texture));

    texture = quader::make_vulkan_viewport_texture_token(
        ready_handle(),
        quader::ViewportVulkanTextureTokenPayload {
            .image_view = VK_NULL_HANDLE,
            .sampler = fake_vulkan_handle<VkSampler>(6),
            .image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        });
    EXPECT_FALSE(cache.present(texture));

    EXPECT_TRUE(registry.added_image_views.empty());
    EXPECT_FALSE(cache.texture_view(quader::ViewportId { 1 }).has_pixels);
}

TEST(ViewportGpuTextureCache, PresentsOpenGlTextureId)
{
    FakeTextureRegistry registry;
    quader::ViewportGpuTextureDescriptorCache cache(registry);

    EXPECT_TRUE(cache.present(ready_opengl_texture(42)));

    const quader::ViewportTextureView view = cache.texture_view(quader::ViewportId { 1 });
    EXPECT_TRUE(view.has_pixels);
    EXPECT_EQ(100U, view.texture_id);
    EXPECT_EQ(quader::ViewportTextureUvOrigin::bottom_left, view.uv_origin);
    ASSERT_EQ(1U, registry.added_backends.size());
    EXPECT_EQ(quader::ViewportTextureTokenBackend::opengl, registry.added_backends[0]);
    EXPECT_EQ(42U, registry.added_opengl_texture_ids[0]);
}

TEST(ViewportGpuTextureCache, PruneRemovesDescriptorsForHiddenViewports)
{
    FakeTextureRegistry registry;
    quader::ViewportGpuTextureDescriptorCache cache(registry);

    EXPECT_TRUE(cache.present(ready_vulkan_texture(1)));
    EXPECT_TRUE(cache.present(ready_vulkan_texture(2, 2, 9, 4, 8, 6)));

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
