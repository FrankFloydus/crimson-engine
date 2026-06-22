#pragma once

#include <string_view>

namespace quader {

struct QuaderCommandId {
    std::string_view id;

    constexpr QuaderCommandId() = default;
    constexpr explicit QuaderCommandId(std::string_view value)
        : id(value) {}

    [[nodiscard]] constexpr std::string_view string() const { return id; }
    [[nodiscard]] constexpr operator std::string_view() const { return id; }
};

namespace QuaderCommand {

inline constexpr QuaderCommandId HistoryUndo{"history.undo"};
inline constexpr QuaderCommandId HistoryRedo{"history.redo"};
inline constexpr QuaderCommandId HistoryRepeatLastAction{"history.repeat_last_action"};

inline constexpr QuaderCommandId SelectionClear{"selection.clear"};
inline constexpr QuaderCommandId SelectionSelectAll{"selection.select_all"};
inline constexpr QuaderCommandId SelectionInvert{"selection.invert"};

inline constexpr QuaderCommandId ViewportFrameSelection{"viewport.frame_selection"};
inline constexpr QuaderCommandId ViewportFocusOrigin{"viewport.focus_origin"};

inline constexpr QuaderCommandId ModelingDeleteSelection{"modeling.delete_selection"};
inline constexpr QuaderCommandId ModelingDuplicateMeshes{"modeling.duplicate_meshes"};
inline constexpr QuaderCommandId ModelingDissolveEdges{"modeling.dissolve_edges"};
inline constexpr QuaderCommandId ModelingDissolveVertices{"modeling.dissolve_vertices"};
inline constexpr QuaderCommandId ModelingCombineMeshes{"modeling.combine_meshes"};
inline constexpr QuaderCommandId ModelingApplyActiveMaterial{"modeling.apply_active_material"};

inline constexpr QuaderCommandId ToolsActivateSelect{"tools.activate.select"};
inline constexpr QuaderCommandId ToolsActivateMove{"tools.activate.move"};
inline constexpr QuaderCommandId ToolsActivateRotate{"tools.activate.rotate"};
inline constexpr QuaderCommandId ToolsActivateScale{"tools.activate.scale"};
inline constexpr QuaderCommandId ToolsActivateBox{"tools.activate.box"};
inline constexpr QuaderCommandId ToolsActivateExtrude{"tools.activate.extrude"};
inline constexpr QuaderCommandId ToolsActivateInsertEdgeLoop{"tools.activate.insert_edge_loop"};
inline constexpr QuaderCommandId ToolsActivateKnife{"tools.activate.knife"};
inline constexpr QuaderCommandId ToolsActivateCut{"tools.activate.cut"};
inline constexpr QuaderCommandId ToolsActivatePoly{"tools.activate.poly"};
inline constexpr QuaderCommandId ToolsActivateMirror{"tools.activate.mirror"};

} // namespace QuaderCommand

} // namespace quader
