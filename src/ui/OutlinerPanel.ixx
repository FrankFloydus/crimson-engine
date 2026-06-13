module;

#include <optional>

export module quader.ui.outliner_panel;

export import quader.ui_shell.editor_panel_contracts;

export namespace quader {

class OutlinerPanel {
public:
    [[nodiscard]] bool is_open() const noexcept;
    void set_open(bool open) noexcept;

    [[nodiscard]] OutlinerPanelResult draw(const SceneOutlinerPanelInput& input);

private:
    bool open_ = true;
};

} // namespace quader
