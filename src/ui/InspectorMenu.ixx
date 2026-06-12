module;

#include <optional>

export module quader.ui.inspector_menu;

export namespace quader {

enum class CreateSceneItemKind {
    cube,
    plane,
    directional_light,
    point_light,
};

struct InspectorCreateMenuResult {
    std::optional<CreateSceneItemKind> requested_create;
};

[[nodiscard]] InspectorCreateMenuResult draw_inspector_create_menu();

} // namespace quader
