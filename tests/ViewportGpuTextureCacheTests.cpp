#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

import quader.ui.viewport_gpu_texture_cache;

namespace {

constexpr quader::ViewportTexturePayloadKind kTopLeftTestTextureKind =
    quader::viewport_texture_payload_kind("quader.tests.viewport_texture.top_left.v1");
constexpr quader::ViewportTexturePayloadKind kBottomLeftTestTextureKind =
    quader::viewport_texture_payload_kind("quader.tests.viewport_texture.bottom_left.v1");

class FakeTextureRegistry final : public quader::IViewportTextureRegistry {
public:
    [[nodiscard]] uint64_t add_texture(const quader::ViewportTextureToken& texture) override
    {
        added_tokens.push_back(texture);
        added_payload_kinds.push_back(texture.payload_kind);
        added_payload_word0.push_back(texture.payload.word0);
        return next_texture_id++;
    }

    void remove_texture(uint64_t texture_id) override
    {
        removed_texture_ids.push_back(texture_id);
    }

    uint64_t next_texture_id = 100;
    std::vector<quader::ViewportTextureToken> added_tokens;
    std::vector<quader::ViewportTexturePayloadKind> added_payload_kinds;
    std::vector<uint64_t> added_payload_word0;
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

quader::ViewportTextureToken ready_texture(
    uint64_t viewport_id = 1,
    uint64_t generation = 2,
    uint64_t resource_id = 3,
    uint64_t image_generation = 4,
    uint64_t payload_word0 = 5,
    quader::ViewportTexturePayloadKind kind = kTopLeftTestTextureKind,
    bool uv_origin_bottom_left = false)
{
    return quader::ViewportTextureToken {
        .handle = ready_handle(viewport_id, generation, resource_id, image_generation),
        .payload_kind = kind,
        .payload = {
            payload_word0,
            6,
            7,
        },
        .uv_origin_bottom_left = uv_origin_bottom_left,
    };
}

} // namespace

TEST(ViewportGpuTextureCache, PresentsReadyTokenAsViewportTextureView)
{
    FakeTextureRegistry registry;
    quader::ViewportGpuTextureDescriptorCache cache(registry);

    EXPECT_TRUE(cache.present(ready_texture()));

    const quader::ViewportTextureView view = cache.texture_view(quader::ViewportId { 1 });
    EXPECT_TRUE(view.has_pixels);
    EXPECT_EQ(100U, view.texture_id);
    EXPECT_EQ(640U, view.extent.width);
    EXPECT_EQ(360U, view.extent.height);
    EXPECT_EQ(quader::ViewportTextureUvOrigin::top_left, view.uv_origin);
    ASSERT_EQ(1U, registry.added_tokens.size());
    EXPECT_EQ(kTopLeftTestTextureKind, registry.added_payload_kinds[0]);
    EXPECT_TRUE(registry.removed_texture_ids.empty());
}

TEST(ViewportGpuTextureCache, ReusesDescriptorForSameTextureToken)
{
    FakeTextureRegistry registry;
    quader::ViewportGpuTextureDescriptorCache cache(registry);

    EXPECT_TRUE(cache.present(ready_texture()));
    EXPECT_TRUE(cache.present(ready_texture()));

    EXPECT_EQ(1U, registry.added_tokens.size());
    EXPECT_TRUE(registry.removed_texture_ids.empty());
    EXPECT_EQ(100U, cache.texture_view(quader::ViewportId { 1 }).texture_id);
}

TEST(ViewportGpuTextureCache, ReplacesDescriptorWhenImageGenerationChanges)
{
    FakeTextureRegistry registry;
    quader::ViewportGpuTextureDescriptorCache cache(registry);

    EXPECT_TRUE(cache.present(ready_texture()));
    EXPECT_TRUE(cache.present(ready_texture(1, 2, 3, 5, 7)));

    EXPECT_EQ(2U, registry.added_tokens.size());
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

    EXPECT_TRUE(cache.present(ready_texture()));
    EXPECT_TRUE(cache.present(ready_texture(1, 2, 3, 4, 9)));

    EXPECT_EQ(2U, registry.added_tokens.size());
    EXPECT_EQ(9U, registry.added_payload_word0[1]);
    EXPECT_TRUE(cache.has_retired_textures());

    cache.collect_retired_textures();
    ASSERT_EQ(1U, registry.removed_texture_ids.size());
    EXPECT_EQ(100U, registry.removed_texture_ids[0]);
    EXPECT_EQ(101U, cache.texture_view(quader::ViewportId { 1 }).texture_id);
}

TEST(ViewportGpuTextureCache, ReplacesDescriptorWhenOpaquePayloadKindChanges)
{
    FakeTextureRegistry registry;
    quader::ViewportGpuTextureDescriptorCache cache(registry);

    EXPECT_TRUE(cache.present(ready_texture()));
    EXPECT_TRUE(cache.present(ready_texture(
        1,
        2,
        3,
        4,
        5,
        kBottomLeftTestTextureKind,
        true)));

    EXPECT_EQ(2U, registry.added_tokens.size());
    EXPECT_EQ(kBottomLeftTestTextureKind, registry.added_payload_kinds[1]);
    EXPECT_TRUE(cache.has_retired_textures());
    EXPECT_EQ(quader::ViewportTextureUvOrigin::bottom_left, cache.texture_view(quader::ViewportId { 1 }).uv_origin);
}

TEST(ViewportGpuTextureCache, RejectsRenderingOrUnresolvedTokens)
{
    FakeTextureRegistry registry;
    quader::ViewportGpuTextureDescriptorCache cache(registry);

    quader::ViewportTextureToken texture = ready_texture();
    texture.handle.readiness = quader::ViewportGpuImageReadiness::rendering;
    EXPECT_FALSE(cache.present(texture));

    texture = ready_texture();
    texture.payload = {};
    EXPECT_FALSE(cache.present(texture));

    texture = ready_texture();
    texture.payload_kind = {};
    EXPECT_FALSE(cache.present(texture));

    EXPECT_TRUE(registry.added_tokens.empty());
    EXPECT_FALSE(cache.texture_view(quader::ViewportId { 1 }).has_pixels);
}

TEST(ViewportGpuTextureCache, PresentsBottomLeftUvOriginFromOpaqueTokenMetadata)
{
    FakeTextureRegistry registry;
    quader::ViewportGpuTextureDescriptorCache cache(registry);

    EXPECT_TRUE(cache.present(ready_texture(
        1,
        2,
        3,
        4,
        42,
        kBottomLeftTestTextureKind,
        true)));

    const quader::ViewportTextureView view = cache.texture_view(quader::ViewportId { 1 });
    EXPECT_TRUE(view.has_pixels);
    EXPECT_EQ(100U, view.texture_id);
    EXPECT_EQ(quader::ViewportTextureUvOrigin::bottom_left, view.uv_origin);
    ASSERT_EQ(1U, registry.added_payload_kinds.size());
    EXPECT_EQ(kBottomLeftTestTextureKind, registry.added_payload_kinds[0]);
    EXPECT_EQ(42U, registry.added_payload_word0[0]);
}

TEST(ViewportGpuTextureCache, PruneRemovesDescriptorsForHiddenViewports)
{
    FakeTextureRegistry registry;
    quader::ViewportGpuTextureDescriptorCache cache(registry);

    EXPECT_TRUE(cache.present(ready_texture(1)));
    EXPECT_TRUE(cache.present(ready_texture(2, 2, 9, 4, 8)));

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
