#include "GradientWidget.hpp"
#include <array>
#include <iterator>
#include <random>
#include "Settings.hpp"
#include "imgui_draw.hpp"
#include "internal.hpp"
#include "maybe_disabled.hpp"

namespace ImGG {

static auto new_mark_id(const Gradient& new_gradient, const Gradient& old_gradient, MarkId old_mark_id) -> MarkId
{
    const auto old_iterator = old_gradient.find_iterator(old_mark_id);
    if (old_iterator != old_gradient.get_marks().end())
    {
        const auto dist = std::distance(
            old_gradient.get_marks().begin(),
            old_iterator
        );
        return MarkId{
            std::next(
                new_gradient.get_marks().begin(),
                dist
            )
        };
    }
    else
    {
        return MarkId{};
    }
}

GradientWidget::GradientWidget(const GradientWidget& widget)
    : _gradient{widget._gradient}
    , _selected_mark{new_mark_id(_gradient, widget._gradient, widget._selected_mark)}
    , _dragged_mark{new_mark_id(_gradient, widget._gradient, widget._dragged_mark)}
    , _mark_to_hide{new_mark_id(_gradient, widget._gradient, widget._mark_to_hide)}
    , _hover_checker{widget._hover_checker}
{}

static auto random_color(RandomNumberGenerator rng) -> ColorRGBA
{
    return ColorRGBA{rng(), rng(), rng(), 1.f};
}

void GradientWidget::track_item_activity()
{
    _interaction_active |= ImGui::IsItemActive();
}
void GradientWidget::add_mark_with_chosen_mode(const RelativePosition relative_pos, RandomNumberGenerator rng, bool add_a_random_color, Settings const& settings)
{
    if (_gradient.get_marks().size() >= settings.maximum_mark_count)
        return;
    const auto position = settings.constrain_mark_position
                              ? RelativePosition{settings.constrain_mark_position({}, relative_pos.get()), WrapMode::Clamp}
                              : relative_pos;
    const auto mark = Mark{
        position,
        add_a_random_color
            ? random_color(rng)
            : _gradient.at(position)
    };
    _selected_mark = _gradient.add_mark(mark);
}

static auto delete_button(const bool disable, const char* reason_for_disabling, const bool should_show_tooltip, Settings const& settings) -> bool
{
    bool b = false;
    maybe_disabled(disable, reason_for_disabling, [&]() {
        b |= settings.minus_button_widget();
    });
    if (!disable && should_show_tooltip)
        ImGui::SetItemTooltip("%s", "Removes the selected mark.\nYou can also middle click on it,\nor drag it down.");
    return b;
}

static auto add_button(const bool should_show_tooltip, Settings const& settings) -> bool
{
    bool const b = settings.plus_button_widget();
    if (should_show_tooltip)
        ImGui::SetItemTooltip("%s", "Add a mark here\nor click on the gradient to choose its position.");
    return b;
}

static auto color_button(
    Mark&                     selected_mark,
    const bool                should_show_tooltip,
    Settings const&           settings,
    bool&                     interaction_active
) -> bool
{
    if (settings.color_picker_widget)
    {
        const auto display = settings.display_color ? settings.display_color(selected_mark.color) : selected_mark.color;
        if (ImGui::ColorButton("##colorpicker1", display, settings.color_edit_flags))
            ImGui::OpenPopup("SelectedMarkColorPicker");
        interaction_active |= ImGui::IsItemActive();
        return false;
    }
    const bool modified = ImGui::ColorEdit4(
        "##colorpicker1",
        reinterpret_cast<float*>(&selected_mark.color),
        settings.color_edit_flags | ImGuiColorEditFlags_NoInputs | (should_show_tooltip ? 0 : ImGuiColorEditFlags_NoTooltip)
    );
    interaction_active |= ImGui::IsItemActive();
    return modified;
}

static auto open_color_picker_popup(
    Mark&                     selected_mark,
    const float               popup_size,
    const bool                should_show_tooltip,
    Settings const&           settings,
    bool&                     interaction_active
) -> bool
{
    if (ImGui::BeginPopup("SelectedMarkColorPicker"))
    {
        ImGui::SetNextItemWidth(popup_size);
        const bool modified = settings.color_picker_widget
            ? settings.color_picker_widget(selected_mark.color, interaction_active)
            : ImGui::ColorPicker4(
                "##colorpicker2",
                reinterpret_cast<float*>(&selected_mark.color),
                settings.color_edit_flags | (should_show_tooltip ? 0 : ImGuiColorEditFlags_NoTooltip)
            );
        interaction_active |= ImGui::IsItemActive();
        ImGui::EndPopup();
        return modified;
    }
    else
    {
        return false;
    }
}

static void draw_gradient_bar(
    Gradient&       gradient,
    const ImVec2    gradient_bar_position,
    const ImVec2    gradient_size,
    Settings const& settings
)
{
    ImDrawList& draw_list = *ImGui::GetWindowDrawList();
    draw_border({
        gradient_bar_position,
        gradient_bar_position + gradient_size,
    });
    if (!gradient.is_empty())
    {
        draw_gradient(
            draw_list,
            gradient,
            gradient_bar_position,
            gradient_size,
            settings
        );
    }
    ImGui::SetCursorScreenPos(
        gradient_bar_position + ImVec2{0.f, gradient_size.y}
    );
}

static auto handle_interactions_with_hovered_mark(
    MarkId& dragged_mark,
    MarkId& selected_mark,
    MarkId& mark_to_delete,
    MarkId  hovered_mark
) -> bool
{
    bool interacted{false};
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        dragged_mark  = hovered_mark;
        selected_mark = hovered_mark;
        interacted    = true;
    }
    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
        ImGui::OpenPopup("SelectedMarkColorPicker");
        selected_mark = hovered_mark;
        dragged_mark.reset();
        interacted = true;
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Middle))
    {
        mark_to_delete = hovered_mark; // When we middle click to delete a non selected mark it is impossible to remove this mark in the loop
        interacted     = true;
    }
    return interacted;
}

auto GradientWidget::draw_gradient_marks(
    MarkId&         mark_to_delete,
    const ImVec2    gradient_bar_position,
    const ImVec2    gradient_size,
    Settings const& settings
) -> internal::draw_gradient_marks_Result
{
    auto        res       = internal::draw_gradient_marks_Result{};
    ImDrawList& draw_list = *ImGui::GetWindowDrawList();
    for (const Mark& mark : _gradient.get_marks())
    {
        MarkId current_mark_id{mark};
        if (_mark_to_hide != current_mark_id)
        {
            ImGui::PushID(current_mark_id.as_imgui_id());
            draw_marks(
                draw_list,
                gradient_bar_position + ImVec2{mark.position.get(), 1.f} * gradient_size,
                ImGui::ColorConvertFloat4ToU32(settings.display_color ? settings.display_color(mark.color) : mark.color),
                gradient_size.y,
                _selected_mark == current_mark_id,
                settings
            );
            track_item_activity();
            ImGui::PopID();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
            {
                res.hitbox_is_hovered     = true;
                res.selected_mark_changed = handle_interactions_with_hovered_mark(
                    _dragged_mark,
                    _selected_mark,
                    mark_to_delete,
                    current_mark_id
                );
            }
        }
    }
    static constexpr float space_between_gradient_bar_and_options = 20.f;
    ImGui::SetCursorScreenPos(
        gradient_bar_position + ImVec2{0.f, gradient_size.y + space_between_gradient_bar_and_options}
    );
    ImGui::Dummy({0.f, 0.f}); // Needed by ImGui after a SetCursorScreenPos
    return res;
}

static auto position_where_to_add_next_mark(const Gradient& gradient) -> float
{
    if (gradient.is_empty())
    {
        return 0.f;
    }
    else if (gradient.get_marks().size() == 1)
    {
        const auto first_position_mark = gradient.get_marks().front().position.get();
        return first_position_mark > 0.5f
                   ? 0.f
                   : 1.f;
    }
    else
    {
        // Find where is the bigger space between two marks
        // to return the position between these two marks.
        const auto first_mark_iterator{gradient.get_marks().begin()};
        const auto last_mark_iterator{std::prev(gradient.get_marks().end())};
        auto       max_value_mark_position{0.f};
        auto       max_value_between_two_marks{first_mark_iterator->position.get()};
        for (auto mark_iterator = first_mark_iterator; mark_iterator != last_mark_iterator; ++mark_iterator)
        {
            const Mark& mark{*mark_iterator};
            const auto  mark_next_iterator{std::next(mark_iterator)->position.get()};
            const auto  mark_position{mark.position.get()};
            if (max_value_between_two_marks < abs(mark_next_iterator - mark_position))
            {
                max_value_mark_position     = mark_position;
                max_value_between_two_marks = abs(mark_next_iterator - max_value_mark_position);
            }
        }
        const auto last_mark_position{last_mark_iterator->position.get()};
        if (max_value_between_two_marks < abs(1.f - last_mark_position))
        {
            max_value_mark_position     = last_mark_position;
            max_value_between_two_marks = abs(1.f - max_value_mark_position);
        }
        return max_value_mark_position + max_value_between_two_marks / 2.f;
    }
}

auto GradientWidget::mouse_dragging_interactions(
    const ImVec2    gradient_bar_position,
    const ImVec2    gradient_size,
    const Settings& settings
) -> bool
{
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        _dragged_mark.reset();
    }
    bool        is_dragging  = false;
    auto* const drag_mark_id = gradient().find(_dragged_mark);
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && drag_mark_id)
    {
        auto map{ImClamp((ImGui::GetIO().MousePos.x - gradient_bar_position.x) / gradient_size.x, 0.f, 1.f)};
        if (settings.constrain_mark_position)
            map = settings.constrain_mark_position(_dragged_mark, map);
        if (drag_mark_id->position.get() != map)
        {
            gradient().set_mark_position(_dragged_mark, RelativePosition{map});
            is_dragging = true;
        }
        if (!(settings.flags & Flag::NoDragDownToDelete))
        { // hide dragging mark when mouse under gradient bar
            const auto diffY{ImGui::GetIO().MousePos.y - gradient_bar_position.y - gradient_size.y};
            if (diffY >= settings.distance_to_delete_mark_by_dragging_down)
            {
                _mark_to_hide = _dragged_mark;
            }
            // do not hide it anymore when mouse on gradient bar
            if (_gradient.contains(_mark_to_hide) && diffY <= settings.distance_to_delete_mark_by_dragging_down)
            {
                _mark_to_hide.reset();
            }
        }
    }
    return is_dragging;
}

static auto next_selected_mark(const std::list<Mark>& gradient, MarkId mark) -> MarkId
{
    assert(!gradient.empty());
    if (gradient.size() == 1)
    {
        return MarkId{};
    }
    else if (mark == MarkId{gradient.front()})
    {
        return MarkId{gradient.back()};
    }
    else
    {
        return MarkId{gradient.front()};
    };
}

static auto compute_number_of_lines_under_bar(Settings const& settings) -> float
{
    float res{0.f};

    if (!(settings.flags & Flag::NoResetButton))
    {
        res += 1.f;
    }

    if (!(settings.flags & Flag::NoAddButton)
        || !(settings.flags & Flag::NoRemoveButton)
        || !(settings.flags & Flag::NoPositionSlider)
        || !(settings.flags & Flag::NoColorEdit))
    {
        res += 1.f;
    }

    return res;
}

static auto compute_border_rect(const char* label, Settings const& settings, ImVec2 gradient_bar_position, ImVec2 gradient_size) -> ImRect
{
    const float space_over_bar = !(settings.flags & Flag::NoLabel)
                                     ? ImGui::CalcTextSize(label).y + ImGui::GetStyle().ItemSpacing.y * 3.f
                                     : ImGui::GetStyle().ItemSpacing.y * 2.f;

    const float            number_of_lines_under_bar              = compute_number_of_lines_under_bar(settings);
    static constexpr float space_between_gradient_bar_and_options = 20.f;
    const float            space_under_bar                        = (internal::line_height() + ImGui::GetStyle().ItemSpacing.y * 2.f) * number_of_lines_under_bar + space_between_gradient_bar_and_options;

    return ImRect{
        gradient_bar_position - ImVec2{settings.horizontal_margin + 4.f, space_over_bar},
        gradient_bar_position + gradient_size + ImVec2{settings.horizontal_margin + 4.f, space_under_bar},
    };
}

auto GradientWidget::widget(
    const char*           label,
    RandomNumberGenerator rng,
    const Settings&       settings
) -> bool
{
    auto modified{false};
    _interaction_active = false;

    ImGui::PushID(label);
    ImGui::BeginGroup();
    if (!(settings.flags & Flag::NoLabel))
        ImGui::TextUnformatted(label);

    const auto gradient_bar_position = ImVec2{internal::gradient_position(settings.horizontal_margin)};
    const auto gradient_size         = ImVec2{
        // To avoid a width equal to zero and library crash the minimum width value is 1.f
        std::max(
            1.f,
            // When the window is smaller than gradient width we compute a relative width
            std::min(
                ImGui::GetContentRegionAvail().x - settings.horizontal_margin * 2.f,
                settings.gradient_width * ImGui::GetFontSize() / 20.f
            )
        ),
        settings.gradient_height * ImGui::GetFontSize() / 20.f
    };
    _bar_minimum = gradient_bar_position;
    _bar_maximum = gradient_bar_position + gradient_size;

    ImGui::BeginGroup();
    // Align the input rectangle with the drawn bar's horizontal margin.
    ImGui::SetCursorScreenPos(gradient_bar_position);
    ImGui::InvisibleButton("gradient_editor", gradient_size);
    track_item_activity();
    if (!(settings.flags & Flag::NoTooltip))
        ImGui::SetItemTooltip("%s", "Click to add a stop. Drag any stop, including the first or last. Double-click a stop for color and opacity.");
    draw_gradient_bar(_gradient, gradient_bar_position, gradient_size, settings);

    const auto wants_to_add_mark{ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)}; // We need to declare it before drawing the marks because we want to
                                                                                                          // test if the mouse is hovering the gradient bar not the marks.
    MarkId mark_to_delete{};

    // We declare it here because even if we cannot add a mark we need to draw gradient marks.
    const auto res = draw_gradient_marks(mark_to_delete, gradient_bar_position, gradient_size, settings);

    const auto mark_hitbox_is_hovered = res.hitbox_is_hovered;
    modified |= res.selected_mark_changed;

    if (wants_to_add_mark && !mark_hitbox_is_hovered)
    {
        const auto position{(ImGui::GetIO().MousePos.x - gradient_bar_position.x) / gradient_size.x};
        add_mark_with_chosen_mode({position, WrapMode::Clamp}, rng, settings.should_use_a_random_color_for_the_new_marks, settings);
        _dragged_mark.reset();
        modified = true;
        // ImGui::OpenPopup("SelectedMarkColorPicker");
    }

    modified |= mouse_dragging_interactions(gradient_bar_position, gradient_size, settings);
    if (!(settings.flags & Flag::NoDragDownToDelete))
    {
        // If mouse released and there is still a mark hidden, then it becomes a mark to delete
        if (_gradient.contains(_mark_to_hide) && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            mark_to_delete = _mark_to_hide;
        }
    }
    // Remove mark_to_delete if it exists
    if (_gradient.contains(mark_to_delete) && _gradient.get_marks().size() > settings.minimum_mark_count)
    {
        if (_gradient.contains(_selected_mark) && _selected_mark == mark_to_delete)
        {
            _selected_mark = next_selected_mark(_gradient.get_marks(), _selected_mark);
        }
        gradient().remove_mark(mark_to_delete);
        modified = true;
    }
    ImGui::EndGroup();
    const auto is_there_a_tooltip{!(settings.flags & Flag::NoTooltip)};
    const auto is_there_remove_button{!(settings.flags & Flag::NoRemoveButton)};
    { // "Remove" button

        const auto editor_bounds = compute_border_rect(label, settings, gradient_bar_position, gradient_size);
        const bool editor_is_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)
                                       && ImGui::IsMouseHoveringRect(editor_bounds.Min, editor_bounds.Max);

        const auto delete_button_pressed = is_there_remove_button
                                               ? delete_button(!_gradient.contains(_selected_mark) || _gradient.get_marks().size() <= settings.minimum_mark_count,
                                                   _gradient.contains(_selected_mark) ? "Keep at least two gradient stops" : "There is no mark selected", is_there_a_tooltip, settings)
                                               : false;
        track_item_activity();

        // A selected stop must not consume Delete/Backspace intended for an
        // adjacent curve or text field in this same properties child window.
        const bool owns_active_item = !GImGui->ActiveId || GImGui->ActiveId == _active_item || _interaction_active;
        const auto delete_key_pressed = editor_is_hovered
                                        && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
                                        && owns_active_item
                                        && !ImGui::GetIO().WantTextInput
                                        && (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace));

        const auto wants_to_delete = delete_button_pressed || delete_key_pressed;
        if (wants_to_delete && _gradient.contains(_selected_mark) && _gradient.get_marks().size() > settings.minimum_mark_count)
        {
            const MarkId new_selected_mark = next_selected_mark(_gradient.get_marks(), _selected_mark);
            gradient().remove_mark(_selected_mark);
            _selected_mark = new_selected_mark;
            modified       = true;
        }
    }

    const auto is_there_add_button{!(settings.flags & Flag::NoAddButton)};
    if (is_there_add_button)
    {
        if (is_there_remove_button)
        {
            ImGui::SameLine();
        }
        const bool full = _gradient.get_marks().size() >= settings.maximum_mark_count;
        ImGui::BeginDisabled(full);
        const bool add_pressed = add_button(is_there_a_tooltip, settings);
        track_item_activity();
        ImGui::EndDisabled();
        if (add_pressed)
        {
            const auto position = RelativePosition{position_where_to_add_next_mark(_gradient), WrapMode::Clamp};
            add_mark_with_chosen_mode(position, rng, settings.should_use_a_random_color_for_the_new_marks, settings);
            modified = true;
        }
    }

    bool force_dont_deselect_mark = false;

    const auto selected_mark = gradient().find(_selected_mark);
    if (selected_mark)
    {
        const auto is_there_color_edit{!(settings.flags & Flag::NoColorEdit)};
        if (is_there_color_edit)
        {
            if (is_there_remove_button || is_there_add_button)
            {
                ImGui::SameLine();
            }
            modified |= color_button(*selected_mark, is_there_a_tooltip, settings, _interaction_active);
            force_dont_deselect_mark = ImGui::IsItemActive(); // The color popup can go outside the border, but we don't want to deselect the mark when we click on it
        }

        if (!(settings.flags & Flag::NoPositionSlider))
        {
            if ((is_there_remove_button || is_there_add_button || is_there_color_edit))
            {
                ImGui::SameLine();
            }
            auto position = selected_mark->position; // Make a copy, we can't modify the position directly because we need to pass through set_mark_position() because it has some invariants to presere (sorting the marks)
            if (position.imgui_widget("##3", gradient_size.x * 0.25f))
            {
                _dragged_mark.reset();
                if (settings.constrain_mark_position)
                    position = RelativePosition{settings.constrain_mark_position(_selected_mark, position.get()), WrapMode::Clamp};
                gradient().set_mark_position(_selected_mark, position);
                modified = true;
            }
            track_item_activity();
        }
    }

    if (!(settings.flags & Flag::NoResetButton))
    {
        if (ImGui::Button("Reset"))
        {
            if (settings.reset_gradient)
                settings.reset_gradient(_gradient);
            else
                _gradient = {};
            _selected_mark.reset();
            _dragged_mark.reset();
            _mark_to_hide.reset();
            modified  = true;
        }
        track_item_activity();
    }

    if (auto* popup_mark = _gradient.find(_selected_mark)) // Reset can invalidate the earlier selected_mark pointer.
    {
        const auto picker_popup_size{internal::line_height() * 12.f};
        modified |= open_color_picker_popup(
            *popup_mark,
            picker_popup_size,
            is_there_a_tooltip,
            settings,
            _interaction_active
        );
    }

    { // Border
        const ImRect border_rect = compute_border_rect(label, settings, gradient_bar_position, gradient_size);

        // Draw border
        if (!(settings.flags & Flag::NoBorder))
            draw_border(border_rect);

        // Deselect mark if we click outside the border
        {
            // Check if bounding box hovered
            ImGui::ItemAdd(border_rect, ImGui::GetID("gradient border"));
            _hover_checker.update();
            // The mark's active/hovered ID differs from this enclosing border.
            // ImGui 1.92 correctly reports the border item as blocked; a click
            // on a stop must nevertheless retain the stop just selected.
            if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)
                && ImGui::IsMouseHoveringRect(border_rect.Min, border_rect.Max))
                _hover_checker.force_consider_hovered();

            // Check if one of the widgets is active
            if (ImGui::IsPopupOpen("SelectedMarkColorPicker")
                || force_dont_deselect_mark)
            {
                _hover_checker.force_consider_hovered();
            }

            // Deselect mark if clicking while not hovered
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !_hover_checker.is_item_hovered())
                _selected_mark = {};
        }
    }

    ImGui::PopID();
    ImGui::EndGroup();
    ImGui::SetCursorScreenPos(
        internal::gradient_position(0.f)
        + ImVec2{
            0.f,
            !(settings.flags & Flag::NoLabel)
                ? ImGui::GetStyle().ItemSpacing.y * 2.f
                : ImGui::GetStyle().ItemSpacing.y * 3.f,
        }
    );
    ImGui::Dummy({0.f, 0.f}); // Needed by ImGui after a SetCursorScreenPos
    _interaction_active |= _gradient.contains(_dragged_mark) && ImGui::IsMouseDown(ImGuiMouseButton_Left);
    ImGuiContext& g = *GImGui;
    _active_item = _interaction_active ? g.ActiveId : 0;

    if (modified)
    {
        ImGui::MarkItemEdited(g.ActiveId);
    }
    return modified;
}

static auto default_rng() -> float
{
    static auto rng          = std::default_random_engine{std::random_device{}()};
    static auto distribution = std::uniform_real_distribution<float>{0.f, 1.f};
    return distribution(rng);
}

auto GradientWidget::widget(const char* label, const Settings& settings) -> bool
{
    return widget(label, &default_rng, settings);
}
}; // namespace ImGG
