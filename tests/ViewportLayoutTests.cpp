#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <limits>
#include <span>
#include <vector>

import quader.ui_shell.editor_viewport_layout;

namespace {

constexpr quader::ViewportId id1 { 1 };
constexpr quader::ViewportId id2 { 2 };
constexpr quader::ViewportId id3 { 3 };
constexpr quader::ViewportId id4 { 4 };

struct ExpectedSlot {
    quader::ViewportId viewport_id;
    quader::ViewportRect rect;
};

struct PresetLayoutCase {
    quader::ViewportLayoutPreset preset;
    std::array<ExpectedSlot, 4> slots;
    std::size_t slot_count = 0;
};

constexpr float root_split = 0.35f;
constexpr float secondary_split = 0.65f;

constexpr std::array<PresetLayoutCase, 8> preset_layout_cases {
    PresetLayoutCase {
        .preset = quader::ViewportLayoutPreset::single_view,
        .slots = { ExpectedSlot { id1, { 0.0f, 0.0f, 1.0f, 1.0f } } },
        .slot_count = 1,
    },
    PresetLayoutCase {
        .preset = quader::ViewportLayoutPreset::two_view_horizontal,
        .slots = {
            ExpectedSlot { id1, { 0.0f, 0.0f, 1.0f, root_split } },
            ExpectedSlot { id2, { 0.0f, root_split, 1.0f, 1.0f - root_split } },
        },
        .slot_count = 2,
    },
    PresetLayoutCase {
        .preset = quader::ViewportLayoutPreset::two_view_vertical,
        .slots = {
            ExpectedSlot { id1, { 0.0f, 0.0f, root_split, 1.0f } },
            ExpectedSlot { id2, { root_split, 0.0f, 1.0f - root_split, 1.0f } },
        },
        .slot_count = 2,
    },
    PresetLayoutCase {
        .preset = quader::ViewportLayoutPreset::three_view_primary_right,
        .slots = {
            ExpectedSlot { id1, { 1.0f - root_split, 0.0f, root_split, 1.0f } },
            ExpectedSlot { id2, { 0.0f, 0.0f, 1.0f - root_split, secondary_split } },
            ExpectedSlot { id3, { 0.0f, secondary_split, 1.0f - root_split, 1.0f - secondary_split } },
        },
        .slot_count = 3,
    },
    PresetLayoutCase {
        .preset = quader::ViewportLayoutPreset::three_view_primary_left,
        .slots = {
            ExpectedSlot { id1, { 0.0f, 0.0f, root_split, 1.0f } },
            ExpectedSlot { id2, { root_split, 0.0f, 1.0f - root_split, secondary_split } },
            ExpectedSlot { id3, { root_split, secondary_split, 1.0f - root_split, 1.0f - secondary_split } },
        },
        .slot_count = 3,
    },
    PresetLayoutCase {
        .preset = quader::ViewportLayoutPreset::three_view_primary_bottom,
        .slots = {
            ExpectedSlot { id1, { 0.0f, 1.0f - root_split, 1.0f, root_split } },
            ExpectedSlot { id2, { 0.0f, 0.0f, secondary_split, 1.0f - root_split } },
            ExpectedSlot { id3, { secondary_split, 0.0f, 1.0f - secondary_split, 1.0f - root_split } },
        },
        .slot_count = 3,
    },
    PresetLayoutCase {
        .preset = quader::ViewportLayoutPreset::three_view_primary_top,
        .slots = {
            ExpectedSlot { id1, { 0.0f, 0.0f, 1.0f, root_split } },
            ExpectedSlot { id2, { 0.0f, root_split, secondary_split, 1.0f - root_split } },
            ExpectedSlot { id3, { secondary_split, root_split, 1.0f - secondary_split, 1.0f - root_split } },
        },
        .slot_count = 3,
    },
    PresetLayoutCase {
        .preset = quader::ViewportLayoutPreset::four_view_quad,
        .slots = {
            ExpectedSlot { id1, { 0.0f, 0.0f, root_split, secondary_split } },
            ExpectedSlot { id2, { root_split, 0.0f, 1.0f - root_split, secondary_split } },
            ExpectedSlot { id3, { 0.0f, secondary_split, root_split, 1.0f - secondary_split } },
            ExpectedSlot { id4, { root_split, secondary_split, 1.0f - root_split, 1.0f - secondary_split } },
        },
        .slot_count = 4,
    },
};

void expect_rect(const quader::ViewportRect& actual, const quader::ViewportRect& expected)
{
    EXPECT_FLOAT_EQ(expected.x, actual.x);
    EXPECT_FLOAT_EQ(expected.y, actual.y);
    EXPECT_FLOAT_EQ(expected.width, actual.width);
    EXPECT_FLOAT_EQ(expected.height, actual.height);
}

void expect_layout_matches(
    const std::vector<quader::ViewportLayoutSlot>& slots,
    const PresetLayoutCase& expected_layout)
{
    ASSERT_EQ(expected_layout.slot_count, slots.size());
    for (std::size_t index = 0; index < expected_layout.slot_count; ++index) {
        EXPECT_EQ(expected_layout.slots[index].viewport_id, slots[index].viewport_id);
        expect_rect(slots[index].rect, expected_layout.slots[index].rect);
    }
}

std::vector<quader::ViewportLayoutSlot> build_layout_for(
    quader::ViewportLayoutPreset preset,
    std::span<const quader::ViewportId> ids)
{
    quader::ViewportLayoutState state;
    state.preset = preset;
    state.root_split_ratio = root_split;
    state.secondary_split_ratio = secondary_split;
    return quader::build_viewport_layout(state, ids);
}

} // namespace

TEST(ViewportLayout, PresetMetadataCoversEveryLayoutPreset)
{
    const std::span<const quader::ViewportLayoutPresetInfo> presets = quader::viewport_layout_presets();

    ASSERT_EQ(preset_layout_cases.size(), presets.size());
    for (std::size_t index = 0; index < preset_layout_cases.size(); ++index) {
        SCOPED_TRACE(presets[index].name);

        EXPECT_EQ(preset_layout_cases[index].preset, presets[index].preset);
        EXPECT_STREQ(presets[index].name, quader::viewport_layout_preset_name(presets[index].preset));
        EXPECT_EQ(
            preset_layout_cases[index].slot_count,
            quader::viewport_layout_required_viewport_count(presets[index].preset));
    }
}

TEST(ViewportLayout, BuildLayoutCoversAllPresets)
{
    const std::array ids { id1, id2, id3, id4 };

    for (const PresetLayoutCase& layout_case : preset_layout_cases) {
        SCOPED_TRACE(quader::viewport_layout_preset_name(layout_case.preset));

        const std::vector<quader::ViewportLayoutSlot> slots = build_layout_for(layout_case.preset, ids);

        expect_layout_matches(slots, layout_case);
    }
}

TEST(ViewportLayout, LayoutsReturnOnlyAvailableViewportsWhenPresetNeedsMore)
{
    const std::array ids { id1, id2, id3, id4 };

    for (const quader::ViewportLayoutPresetInfo& preset : quader::viewport_layout_presets()) {
        for (std::size_t count = 0; count < preset.required_viewport_count; ++count) {
            SCOPED_TRACE(preset.name);
            SCOPED_TRACE(count);

            const std::span<const quader::ViewportId> available_ids { ids.data(), count };
            const std::vector<quader::ViewportLayoutSlot> slots =
                build_layout_for(preset.preset, available_ids);

            ASSERT_EQ(count, slots.size());
            for (std::size_t index = 0; index < count; ++index) {
                EXPECT_EQ(ids[index], slots[index].viewport_id);
            }
        }
    }
}

TEST(ViewportLayout, MaximizedViewportOverridesPreset)
{
    const std::array ids { id1, id2, id3, id4 };
    quader::ViewportLayoutState state;
    state.preset = quader::ViewportLayoutPreset::four_view_quad;
    state.maximized_viewport = id3;

    const std::vector<quader::ViewportLayoutSlot> slots = quader::build_viewport_layout(state, ids);

    ASSERT_EQ(1U, slots.size());
    EXPECT_EQ(id3, slots[0].viewport_id);
    expect_rect(slots[0].rect, { 0.0f, 0.0f, 1.0f, 1.0f });
}

TEST(ViewportLayout, InvalidMaximizedViewportFallsBackToPresetLayout)
{
    const std::array ids { id1, id2 };
    quader::ViewportLayoutState state;
    state.preset = quader::ViewportLayoutPreset::two_view_vertical;
    state.root_split_ratio = root_split;
    state.maximized_viewport = quader::ViewportId { 99 };

    std::vector<quader::ViewportLayoutSlot> slots = quader::build_viewport_layout(state, ids);

    ASSERT_EQ(2U, slots.size());
    EXPECT_EQ(id1, slots[0].viewport_id);
    expect_rect(slots[0].rect, { 0.0f, 0.0f, root_split, 1.0f });
    EXPECT_EQ(id2, slots[1].viewport_id);
    expect_rect(slots[1].rect, { root_split, 0.0f, 1.0f - root_split, 1.0f });

    state.maximized_viewport = quader::ViewportId {};
    slots = quader::build_viewport_layout(state, ids);

    ASSERT_EQ(2U, slots.size());
    EXPECT_EQ(id1, slots[0].viewport_id);
    EXPECT_EQ(id2, slots[1].viewport_id);
}

TEST(ViewportLayout, NonFiniteBuildSplitRatiosFallBackToCenteredPanes)
{
    const std::array ids { id1, id2, id3, id4 };
    quader::ViewportLayoutState state;
    state.preset = quader::ViewportLayoutPreset::four_view_quad;
    state.root_split_ratio = std::numeric_limits<float>::quiet_NaN();
    state.secondary_split_ratio = std::numeric_limits<float>::infinity();

    const std::vector<quader::ViewportLayoutSlot> slots = quader::build_viewport_layout(state, ids);

    ASSERT_EQ(4U, slots.size());
    expect_rect(slots[0].rect, { 0.0f, 0.0f, 0.5f, 0.5f });
    expect_rect(slots[1].rect, { 0.5f, 0.0f, 0.5f, 0.5f });
    expect_rect(slots[2].rect, { 0.0f, 0.5f, 0.5f, 0.5f });
    expect_rect(slots[3].rect, { 0.5f, 0.5f, 0.5f, 0.5f });
}

TEST(ViewportLayout, SplitRatioClampsToMinimumPanePixels)
{
    EXPECT_FLOAT_EQ(0.25f, quader::clamp_viewport_split_ratio(0.1f, 400.0f, 100.0f));
    EXPECT_FLOAT_EQ(0.75f, quader::clamp_viewport_split_ratio(0.9f, 400.0f, 100.0f));
    EXPECT_FLOAT_EQ(0.5f, quader::clamp_viewport_split_ratio(0.9f, 180.0f, 100.0f));
    EXPECT_FLOAT_EQ(0.5f, quader::clamp_viewport_split_ratio(0.2f, 200.0f, 100.0f));
    EXPECT_NEAR(100.0f / 201.0f, quader::clamp_viewport_split_ratio(0.0f, 201.0f, 100.0f), 0.000001f);
    EXPECT_NEAR(101.0f / 201.0f, quader::clamp_viewport_split_ratio(1.0f, 201.0f, 100.0f), 0.000001f);
}

TEST(ViewportLayout, SplitRatioClampHandlesNonFiniteRatiosAndInvalidBounds)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();

    EXPECT_FLOAT_EQ(0.5f, quader::clamp_viewport_split_ratio(nan, 400.0f, 100.0f));
    EXPECT_FLOAT_EQ(0.0f, quader::clamp_viewport_split_ratio(-0.25f, 400.0f, 0.0f));
    EXPECT_FLOAT_EQ(1.0f, quader::clamp_viewport_split_ratio(1.25f, -1.0f, 100.0f));
    EXPECT_FLOAT_EQ(0.75f, quader::clamp_viewport_split_ratio(0.75f, 400.0f, nan));
}
