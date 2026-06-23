////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <string_view>

namespace quader {

/**
 * Represents a Quader Command ID value used by the public Quader API surface.
 */
struct QuaderCommandId {
    std::string_view id;

    constexpr QuaderCommandId() = default;
    constexpr explicit QuaderCommandId(std::string_view value)
        : id(value) {}

    [[nodiscard]] constexpr std::string_view string() const { return id; }
    [[nodiscard]] constexpr operator std::string_view() const { return id; }
};

namespace QuaderCommand {

inline constexpr QuaderCommandId EditUndo{"edit.undo"};
inline constexpr QuaderCommandId EditRedo{"edit.redo"};
inline constexpr QuaderCommandId EditRepeatLastAction{"edit.repeat_last_action"};
inline constexpr QuaderCommandId EditClearSelection{"edit.clear_selection"};
inline constexpr QuaderCommandId EditSelectAll{"edit.select_all"};
inline constexpr QuaderCommandId EditInvertSelection{"edit.invert_selection"};
inline constexpr QuaderCommandId DeleteSelection{"edit.delete"};
inline constexpr QuaderCommandId CopySelection{"edit.copy"};
inline constexpr QuaderCommandId PasteSelection{"edit.paste"};
inline constexpr QuaderCommandId DuplicateSelection{"edit.duplicate"};
inline constexpr QuaderCommandId GroupSelection{"edit.group"};
inline constexpr QuaderCommandId UngroupSelection{"edit.ungroup"};
inline constexpr QuaderCommandId ApplyMaterial{"edit.apply_material"};

inline constexpr QuaderCommandId HistoryUndo{"edit.undo"};
inline constexpr QuaderCommandId HistoryRedo{"edit.redo"};
inline constexpr QuaderCommandId HistoryRepeatLastAction{"edit.repeat_last_action"};

inline constexpr QuaderCommandId SelectionClear{"edit.clear_selection"};
inline constexpr QuaderCommandId SelectionSelectAll{"edit.select_all"};
inline constexpr QuaderCommandId SelectionInvert{"edit.invert_selection"};

inline constexpr QuaderCommandId ViewportFrameSelection{"viewport.frame_selection"};
inline constexpr QuaderCommandId ViewportFocusOrigin{"viewport.focus_origin"};

inline constexpr QuaderCommandId ModelingDeleteSelection{"edit.delete"};
inline constexpr QuaderCommandId ModelingDuplicateMeshes{"edit.duplicate"};
inline constexpr QuaderCommandId ModelingDissolveEdges{"modeling.dissolve_edges"};
inline constexpr QuaderCommandId ModelingDissolveVertices{"modeling.dissolve_vertices"};
inline constexpr QuaderCommandId ModelingCombineMeshes{"modeling.combine_meshes"};
inline constexpr QuaderCommandId ModelingMergeVerticesToActive{"modeling.merge_vertices_to_active"};
inline constexpr QuaderCommandId ModelingMergeVerticesToCenter{"modeling.merge_vertices_to_center"};
inline constexpr QuaderCommandId ModelingMirrorSelectionX{"modeling.mirror_selection_x"};
inline constexpr QuaderCommandId ModelingMirrorSelectionY{"modeling.mirror_selection_y"};
inline constexpr QuaderCommandId ModelingMirrorSelectionZ{"modeling.mirror_selection_z"};
inline constexpr QuaderCommandId ModelingInvertMesh{"modeling.invert_mesh"};
inline constexpr QuaderCommandId ModelingFlipHorizontal{"modeling.flip_horizontal"};
inline constexpr QuaderCommandId ModelingFlipVertical{"modeling.flip_vertical"};
inline constexpr QuaderCommandId ModelingApplyActiveMaterial{"edit.apply_material"};

inline constexpr QuaderCommandId ToolsActivateSelect{"tools.activate.select"};
inline constexpr QuaderCommandId ToolsActivateMove{"tools.activate.move"};
inline constexpr QuaderCommandId ToolsActivateExtend{"tools.activate.extend"};
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
