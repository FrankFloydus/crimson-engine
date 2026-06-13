#include <gtest/gtest.h>

#include <optional>
#include <stdexcept>

import quader.render.vulkan_viewport_image_ring;

namespace {

quader::ViewportGpuImageRingLedger make_ledger(uint64_t resource_id = 42)
{
    return quader::ViewportGpuImageRingLedger(
        quader::ViewportId { 7 },
        quader::ViewportGeneration { 3 },
        quader::RenderExtent { 320, 180 },
        3,
        resource_id);
}

} // namespace

TEST(VulkanViewportImageRing, DescriptorRequiresRenderableExtentAndMultipleImages)
{
    quader::VulkanViewportImageRingDesc desc {
        .viewport_id = quader::ViewportId { 1 },
        .generation = quader::ViewportGeneration { 1 },
        .extent = quader::RenderExtent { 64, 64 },
        .image_count = 2,
    };
    EXPECT_TRUE(desc.is_valid());

    desc.image_count = 1;
    EXPECT_FALSE(desc.is_valid());

    desc.image_count = 2;
    desc.extent = {};
    EXPECT_FALSE(desc.is_valid());
}

TEST(VulkanViewportImageRing, LedgerRejectsInvalidIdentity)
{
    EXPECT_THROW(
        (quader::ViewportGpuImageRingLedger(
            quader::ViewportId {},
            quader::ViewportGeneration { 1 },
            quader::RenderExtent { 320, 180 },
            3,
            1)),
        std::invalid_argument);

    EXPECT_THROW(
        (quader::ViewportGpuImageRingLedger(
            quader::ViewportId { 1 },
            quader::ViewportGeneration { 1 },
            quader::RenderExtent { 320, 180 },
            1,
            1)),
        std::invalid_argument);
}

TEST(VulkanViewportImageRing, LedgerTracksRenderingThenReadyHandles)
{
    quader::ViewportGpuImageRingLedger ledger = make_ledger();

    std::optional<uint32_t> slot_index = ledger.try_acquire_ready_slot();
    ASSERT_TRUE(slot_index.has_value());

    const quader::ViewportGpuImageLease lease = ledger.begin_render(*slot_index);
    ASSERT_TRUE(lease.is_valid());
    EXPECT_EQ(lease.handle.readiness, quader::ViewportGpuImageReadiness::rendering);
    EXPECT_TRUE(ledger.owns(lease.handle));
    EXPECT_FALSE(ledger.ready_handle_for(lease.handle).has_value());

    std::optional<quader::ViewportGpuImageHandle> ready = ledger.mark_ready(lease.handle);
    ASSERT_TRUE(ready.has_value());
    EXPECT_TRUE(ready->is_ready());

    std::optional<quader::ViewportGpuImageHandle> resolved = ledger.ready_handle_for(lease.handle);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_TRUE(resolved->is_ready());
}

TEST(VulkanViewportImageRing, LedgerDoesNotReuseRenderingSlots)
{
    quader::ViewportGpuImageRingLedger ledger(
        quader::ViewportId { 2 },
        quader::ViewportGeneration { 1 },
        quader::RenderExtent { 320, 180 },
        2,
        99);

    const uint32_t first_slot = *ledger.try_acquire_ready_slot();
    const quader::ViewportGpuImageLease first = ledger.begin_render(first_slot);
    const uint32_t second_slot = *ledger.try_acquire_ready_slot();
    const quader::ViewportGpuImageLease second = ledger.begin_render(second_slot);

    EXPECT_FALSE(ledger.try_acquire_ready_slot().has_value());

    (void)ledger.mark_ready(first.handle);
    const std::optional<uint32_t> available_slot = ledger.try_acquire_ready_slot();
    ASSERT_TRUE(available_slot.has_value());
    EXPECT_EQ(*available_slot, first_slot);

    (void)second;
}

TEST(VulkanViewportImageRing, LedgerKeepsSimultaneousSlotHandlesDistinct)
{
    quader::ViewportGpuImageRingLedger ledger(
        quader::ViewportId { 2 },
        quader::ViewportGeneration { 1 },
        quader::RenderExtent { 320, 180 },
        2,
        99);

    const uint32_t first_slot = *ledger.try_acquire_ready_slot();
    const quader::ViewportGpuImageLease first = ledger.begin_render(first_slot);
    const uint32_t second_slot = *ledger.try_acquire_ready_slot();
    const quader::ViewportGpuImageLease second = ledger.begin_render(second_slot);

    ASSERT_NE(first.handle.slot_index, second.handle.slot_index);
    ASSERT_EQ(first.handle.image_generation, second.handle.image_generation);

    std::optional<quader::ViewportGpuImageHandle> first_ready = ledger.mark_ready(first.handle);
    ASSERT_TRUE(first_ready.has_value());

    EXPECT_TRUE(ledger.ready_handle_for(first.handle).has_value());
    EXPECT_FALSE(ledger.ready_handle_for(second.handle).has_value());

    std::optional<quader::ViewportGpuImageHandle> second_ready = ledger.mark_ready(second.handle);
    ASSERT_TRUE(second_ready.has_value());
    EXPECT_TRUE(ledger.ready_handle_for(second.handle).has_value());
}

TEST(VulkanViewportImageRing, LedgerRejectsStaleHandles)
{
    quader::ViewportGpuImageRingLedger old_ledger = make_ledger(10);
    const uint32_t old_slot = *old_ledger.try_acquire_ready_slot();
    const quader::ViewportGpuImageLease old_lease = old_ledger.begin_render(old_slot);
    ASSERT_TRUE(old_ledger.mark_ready(old_lease.handle).has_value());

    quader::ViewportGpuImageRingLedger new_ledger = make_ledger(11);
    EXPECT_FALSE(new_ledger.owns(old_lease.handle));
    EXPECT_FALSE(new_ledger.ready_handle_for(old_lease.handle).has_value());
}

TEST(VulkanViewportImageRing, LedgerMatchesOnlyCurrentGenerationAndExtent)
{
    quader::ViewportGpuImageRingLedger ledger = make_ledger();

    EXPECT_TRUE(ledger.matches(
        quader::ViewportGeneration { 3 },
        quader::RenderExtent { 320, 180 }));
    EXPECT_FALSE(ledger.matches(
        quader::ViewportGeneration { 4 },
        quader::RenderExtent { 320, 180 }));
    EXPECT_FALSE(ledger.matches(
        quader::ViewportGeneration { 3 },
        quader::RenderExtent { 640, 360 }));
}
