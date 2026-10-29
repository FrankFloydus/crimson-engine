# Workspace capabilities and editor selection contexts

The active project owns `EditorWorkspaceMode::Default` or `Modeling` through
`ProjectSession`. Every resident scene reads that single owner through its
`EditorModeService` binding. Creating or activating scene tabs does not change
the workspace mode. Standalone Quader scene consumers retain Modeling as their
initial capability; the Crimson application chooses Default on project entry.
Workspace mode is transient project state, not an authored scene property or an
undoable document edit.

`EditorMode::Object` / `Edit` remains separate. Modeling permits the complete
original Quader toolset and its Object/Edit context. Default keeps object
selection, lasso/paint selection, Move/Rotate/Scale, ordinary duplication/deletion,
copy/paste, asset placement and light placement. Component selection, geometry
and curve authoring, instance-definition entry and modeling operations reject
execution in Default. Source modeling documents and existing content remain
available to rendering and placement.

`QuaderEditorModeApi::workspace_mode()` and `set_workspace_mode()` expose the
project owner. The native selector lives in `QtSelectionToolbar`; component
actions reflect the accepted mode. Default hides component selectors, geometry
launchers, Mesh/Vertex/Edge/Face/UV menus, geometry palette entries, Face Texture and
Commands Library. Modeling reveals the retained controls; one native scene,
viewport, Outliner, Inspector and history remain authoritative. Dock suppression
preserves requested visibility and toolbar preferences. This does not add a new
mode shortcut.

Changing to Default finishes existing property/tool drafts through their retained
owners, returns to Object context, exits instance edit, selects the ordinary
selection tool and normalizes incompatible tools in every pane. Failure retains
the prior workspace capability. Workspace changes advance scene mode revisions
for command, toolbar and viewport caches, without writing scene data.

Enforcement is shared by `EditorModeService`, modeling action availability and
execution, modeling tool activation, the native viewport lifecycle coordinator,
public command descriptors/execution and direct geometry-authoring APIs. UI
visibility alone is not an authorization boundary. Runtime and renderer owners
remain independent from this capability switch.

Focused regressions extend `EditorModeServiceTests`,
`EditorModeProjectLifecycleTests` and `PublicReleaseUiTests`: rejected command/API/
native-tool entry without history writes; retained object transforms; scene-tab
and new-scene inheritance; actual native combo routing; component action state and
restoration of Modeling capability. `WorkspaceModePresentationTests` covers native
menu/tool visibility, retained dock preferences and the shared Inspector extension.
