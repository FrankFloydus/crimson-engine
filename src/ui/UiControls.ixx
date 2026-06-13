module;

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <span>
#include <string_view>

export module quader.ui.controls;

export import quader.render.types;

export namespace quader {

inline constexpr float kResettableFloatComparisonEpsilon = 0.00001f;
inline constexpr float kMinimumResettableControlWidth = 80.0f;

class DisabledScope final {
public:
    explicit DisabledScope(bool disabled)
        : disabled_(disabled)
    {
        if (disabled_) {
            ImGui::BeginDisabled();
        }
    }

    DisabledScope(const DisabledScope&) = delete;
    DisabledScope& operator=(const DisabledScope&) = delete;

    ~DisabledScope()
    {
        if (disabled_) {
            ImGui::EndDisabled();
        }
    }

private:
    bool disabled_ = false;
};

class IdScope final {
public:
    explicit IdScope(int id)
    {
        ImGui::PushID(id);
    }

    explicit IdScope(const void* id)
    {
        ImGui::PushID(id);
    }

    explicit IdScope(const char* id)
    {
        ImGui::PushID(id);
    }

    IdScope(const IdScope&) = delete;
    IdScope& operator=(const IdScope&) = delete;

    ~IdScope()
    {
        ImGui::PopID();
    }
};

class StyleVarScope final {
public:
    StyleVarScope(ImGuiStyleVar variable, float value)
    {
        ImGui::PushStyleVar(variable, value);
    }

    StyleVarScope(ImGuiStyleVar variable, ImVec2 value)
    {
        ImGui::PushStyleVar(variable, value);
    }

    StyleVarScope(const StyleVarScope&) = delete;
    StyleVarScope& operator=(const StyleVarScope&) = delete;

    ~StyleVarScope()
    {
        ImGui::PopStyleVar();
    }
};

class WindowScope final {
public:
    WindowScope(const char* title, bool* open = nullptr, ImGuiWindowFlags flags = 0)
        : visible_(ImGui::Begin(title, open, flags))
    {
    }

    WindowScope(const WindowScope&) = delete;
    WindowScope& operator=(const WindowScope&) = delete;

    ~WindowScope()
    {
        ImGui::End();
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return visible_;
    }

private:
    bool visible_ = false;
};

class MainMenuBarScope final {
public:
    MainMenuBarScope()
        : visible_(ImGui::BeginMainMenuBar())
    {
    }

    MainMenuBarScope(const MainMenuBarScope&) = delete;
    MainMenuBarScope& operator=(const MainMenuBarScope&) = delete;

    ~MainMenuBarScope()
    {
        if (visible_) {
            ImGui::EndMainMenuBar();
        }
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return visible_;
    }

private:
    bool visible_ = false;
};

class MenuScope final {
public:
    explicit MenuScope(const char* label, bool enabled = true)
        : visible_(ImGui::BeginMenu(label, enabled))
    {
    }

    MenuScope(const MenuScope&) = delete;
    MenuScope& operator=(const MenuScope&) = delete;

    ~MenuScope()
    {
        if (visible_) {
            ImGui::EndMenu();
        }
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return visible_;
    }

private:
    bool visible_ = false;
};

class ComboScope final {
public:
    ComboScope(const char* label, const char* preview_value, ImGuiComboFlags flags = 0)
        : visible_(ImGui::BeginCombo(label, preview_value, flags))
    {
    }

    ComboScope(const ComboScope&) = delete;
    ComboScope& operator=(const ComboScope&) = delete;

    ~ComboScope()
    {
        if (visible_) {
            ImGui::EndCombo();
        }
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return visible_;
    }

private:
    bool visible_ = false;
};

class PopupScope final {
public:
    explicit PopupScope(const char* id, ImGuiWindowFlags flags = 0)
        : visible_(ImGui::BeginPopup(id, flags))
    {
    }

    PopupScope(const PopupScope&) = delete;
    PopupScope& operator=(const PopupScope&) = delete;

    ~PopupScope()
    {
        if (visible_) {
            ImGui::EndPopup();
        }
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return visible_;
    }

private:
    bool visible_ = false;
};

class TableScope final {
public:
    TableScope(
        const char* id,
        int column_count,
        ImGuiTableFlags flags = 0,
        ImVec2 outer_size = ImVec2(0.0f, 0.0f),
        float inner_width = 0.0f)
        : visible_(ImGui::BeginTable(id, column_count, flags, outer_size, inner_width))
    {
    }

    TableScope(const TableScope&) = delete;
    TableScope& operator=(const TableScope&) = delete;

    ~TableScope()
    {
        if (visible_) {
            ImGui::EndTable();
        }
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return visible_;
    }

private:
    bool visible_ = false;
};

class ChildScope final {
public:
    ChildScope(const char* id, ImVec2 size = ImVec2(0.0f, 0.0f), bool border = false, ImGuiWindowFlags flags = 0)
        : visible_(ImGui::BeginChild(id, size, border, flags))
    {
    }

    ChildScope(const ChildScope&) = delete;
    ChildScope& operator=(const ChildScope&) = delete;

    ~ChildScope()
    {
        ImGui::EndChild();
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return visible_;
    }

private:
    bool visible_ = false;
};

inline void render_item_tooltip(const char* text)
{
    if (text == nullptr || text[0] == '\0') {
        return;
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", text);
    }
}

[[nodiscard]] inline std::string_view visible_label_from(const char* label)
{
    if (label == nullptr) {
        return {};
    }

    const std::string_view text { label };
    const size_t hidden_id_separator = text.find("##");
    if (hidden_id_separator == std::string_view::npos) {
        return text;
    }

    return text.substr(0U, hidden_id_separator);
}

[[nodiscard]] inline float reset_button_width()
{
    const ImGuiStyle& style = ImGui::GetStyle();
    return ImGui::CalcTextSize("Reset").x + style.FramePadding.x * 2.0f;
}

[[nodiscard]] inline float changed_marker_width()
{
    return ImGui::CalcTextSize("(*)").x;
}

struct ResettablePropertyLayout {
    bool changed_from_default = false;
    bool reset_inline = false;
    bool marker_on_caption = false;
};

inline void render_changed_marker()
{
    ImGui::TextDisabled("(*)");
    render_item_tooltip("This value differs from its default.");
}

[[nodiscard]] inline ResettablePropertyLayout begin_resettable_property(
    const char* label,
    const char* tooltip,
    bool changed_from_default,
    float preferred_control_width = 0.0f)
{
    ImGui::PushID(label);

    ResettablePropertyLayout layout {
        .changed_from_default = changed_from_default,
    };

    const std::string_view visible_label = visible_label_from(label);
    if (!visible_label.empty()) {
        ImGui::TextUnformatted(visible_label.data(), visible_label.data() + visible_label.size());
        render_item_tooltip(tooltip);
        if (changed_from_default) {
            ImGui::SameLine();
            render_changed_marker();
            layout.marker_on_caption = true;
        }
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const float available_width = ImGui::GetContentRegionAvail().x;
    const float trailing_width =
        changed_from_default ?
            reset_button_width() + (layout.marker_on_caption ? 0.0f : changed_marker_width() + style.ItemInnerSpacing.x) :
            0.0f;
    layout.reset_inline =
        changed_from_default &&
        available_width >= kMinimumResettableControlWidth + trailing_width + style.ItemInnerSpacing.x;
    const float full_width_control_width =
        layout.reset_inline ? available_width - trailing_width - style.ItemInnerSpacing.x : available_width;
    const float preferred_width =
        preferred_control_width > 0.0f ? std::min(preferred_control_width, available_width) : full_width_control_width;
    const float control_width =
        preferred_control_width > 0.0f ? preferred_width : full_width_control_width;
    ImGui::SetNextItemWidth(std::max(1.0f, control_width));
    return layout;
}

[[nodiscard]] inline bool end_resettable_property(
    const ResettablePropertyLayout& layout,
    const char* reset_tooltip = "Reset this value to its default.")
{
    bool reset_clicked = false;
    if (layout.changed_from_default) {
        if (layout.reset_inline) {
            ImGui::SameLine();
        }

        if (!layout.marker_on_caption) {
            render_changed_marker();
            ImGui::SameLine();
        }

        reset_clicked = ImGui::SmallButton("Reset");
        render_item_tooltip(reset_tooltip);
    }

    ImGui::PopID();
    return reset_clicked;
}

[[nodiscard]] inline bool render_changed_reset_control(
    const char* id,
    bool changed_from_default,
    const char* reset_tooltip = "Reset this value to its default.")
{
    if (!changed_from_default) {
        return false;
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const float required_width = changed_marker_width() + style.ItemInnerSpacing.x + reset_button_width();
    if (ImGui::GetContentRegionAvail().x >= required_width + style.ItemSpacing.x) {
        ImGui::SameLine();
    }

    render_changed_marker();
    ImGui::SameLine();
    const IdScope reset_id(id);
    const bool reset_clicked = ImGui::SmallButton("Reset");
    render_item_tooltip(reset_tooltip);
    return reset_clicked;
}

[[nodiscard]] inline bool floats_match(float left, float right)
{
    return std::abs(left - right) <= kResettableFloatComparisonEpsilon;
}

[[nodiscard]] inline bool colors_match(LinearColor left, LinearColor right)
{
    return floats_match(left.r, right.r) && floats_match(left.g, right.g) && floats_match(left.b, right.b);
}

[[nodiscard]] inline bool render_srgb_color_edit(const char* label, LinearColor& linear_color)
{
    SrgbColor srgb = linear_to_srgb(linear_color);
    float values[3] = { srgb.r, srgb.g, srgb.b };
    if (!ImGui::ColorEdit3(label, values, ImGuiColorEditFlags_Float)) {
        return false;
    }

    linear_color = srgb_to_linear(SrgbColor { values[0], values[1], values[2] });
    return true;
}

[[nodiscard]] inline bool render_resettable_srgb_color_edit(
    const char* label,
    LinearColor& value,
    LinearColor default_value,
    const char* tooltip)
{
    const bool changed_from_default = !colors_match(value, default_value);
    const ResettablePropertyLayout layout = begin_resettable_property(label, tooltip, changed_from_default);
    bool changed = render_srgb_color_edit("##Value", value);
    render_item_tooltip(tooltip);
    if (end_resettable_property(layout, "Reset this color to its default.")) {
        value = default_value;
        changed = true;
    }
    return changed;
}

[[nodiscard]] inline bool render_resettable_float_drag(
    const char* label,
    float& value,
    float default_value,
    float speed,
    float min_value,
    float max_value,
    const char* format,
    const char* tooltip)
{
    const bool changed_from_default = !floats_match(value, default_value);
    const ResettablePropertyLayout layout = begin_resettable_property(label, tooltip, changed_from_default);
    bool changed = ImGui::DragFloat("##Value", &value, speed, 0.0f, 0.0f, format);
    render_item_tooltip(tooltip);
    if (changed && min_value < max_value) {
        value = std::clamp(value, min_value, max_value);
    }
    if (end_resettable_property(layout)) {
        value = default_value;
        changed = true;
    }
    return changed;
}

[[nodiscard]] inline bool render_resettable_float_drag(
    const char* label,
    float& value,
    float default_value,
    float speed,
    float min_value,
    float max_value,
    const char* tooltip)
{
    return render_resettable_float_drag(label, value, default_value, speed, min_value, max_value, "%.3f", tooltip);
}

[[nodiscard]] inline bool render_resettable_int_drag(
    const char* label,
    int& value,
    int default_value,
    float speed,
    int min_value,
    int max_value,
    const char* tooltip)
{
    const bool changed_from_default = value != default_value;
    const ResettablePropertyLayout layout = begin_resettable_property(label, tooltip, changed_from_default);
    bool changed = ImGui::DragInt("##Value", &value, speed);
    render_item_tooltip(tooltip);
    if (changed && min_value < max_value) {
        value = std::clamp(value, min_value, max_value);
    }
    if (end_resettable_property(layout)) {
        value = default_value;
        changed = true;
    }
    return changed;
}

[[nodiscard]] inline bool render_resettable_checkbox(
    const char* label,
    bool& value,
    bool default_value,
    const char* tooltip)
{
    const bool changed_from_default = value != default_value;
    const ResettablePropertyLayout layout = begin_resettable_property(label, tooltip, changed_from_default);
    bool changed = ImGui::Checkbox("##Value", &value);
    render_item_tooltip(tooltip);
    if (end_resettable_property(layout)) {
        value = default_value;
        changed = true;
    }
    return changed;
}

template <typename Value, typename LabelForValue>
[[nodiscard]] inline bool render_resettable_combo(
    const char* label,
    Value& value,
    Value default_value,
    std::span<const Value> options,
    LabelForValue label_for_value,
    const char* tooltip,
    float preferred_control_width = 0.0f)
{
    bool changed = false;
    const ResettablePropertyLayout layout =
        begin_resettable_property(label, tooltip, value != default_value, preferred_control_width);
    {
        const ComboScope combo("##Value", label_for_value(value));
        render_item_tooltip(tooltip);
        if (combo) {
            for (const Value option : options) {
                const bool selected = value == option;
                if (ImGui::Selectable(label_for_value(option), selected)) {
                    value = option;
                    changed = true;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
        }
    }
    if (end_resettable_property(layout)) {
        value = default_value;
        changed = true;
    }
    return changed;
}

[[nodiscard]] inline bool render_resettable_text_input(
    const char* label,
    char* value,
    size_t value_capacity,
    std::string_view default_value,
    const char* tooltip,
    ImGuiInputTextFlags flags = 0)
{
    if (value == nullptr || value_capacity == 0U) {
        return false;
    }

    const bool differs_from_default = std::string_view(value) != default_value;
    const ResettablePropertyLayout layout = begin_resettable_property(label, tooltip, differs_from_default);
    bool changed = ImGui::InputText("##Value", value, value_capacity, flags);
    render_item_tooltip(tooltip);
    if (end_resettable_property(layout)) {
        const size_t copy_count = std::min(value_capacity - 1U, default_value.size());
        std::snprintf(value, value_capacity, "%.*s", static_cast<int>(copy_count), default_value.data());
        changed = true;
    }
    return changed;
}

} // namespace quader
