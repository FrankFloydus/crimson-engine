module;

#include <imgui.h>

export module quader.ui.controls;

export import quader.render.types;

export namespace quader {

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

} // namespace quader
