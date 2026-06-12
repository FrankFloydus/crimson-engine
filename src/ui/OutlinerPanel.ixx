module;

#include <optional>
#include <span>

export module quader.ui.outliner_panel;

export import quader.scene.scene;

export namespace quader {

struct OutlinerPanelResult {
    std::optional<SceneEntityId> selected_entity;
};

class OutlinerPanel {
public:
    [[nodiscard]] bool is_open() const noexcept;
    void set_open(bool open) noexcept;

    [[nodiscard]] OutlinerPanelResult draw(
        std::span<const SceneEntity> entities,
        std::optional<SceneEntityId> selected_entity);

private:
    bool open_ = true;
};

} // namespace quader
