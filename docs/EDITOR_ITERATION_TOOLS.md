# Editor iteration tools

These five tools support finding content, returning to useful views and testing a first-person level. Existing scene assets, materials and mesh paint keep their current formats. Camera bookmarks are project-local editor metadata; the other new search and report state is transient. Play controls operate on the existing isolated runtime copy.

## Outliner search and type filters

Use **Search** and the type picker beside **Add** in the Outliner. Search matches part of an entity name without ASCII case sensitivity. Types include Models, Lights, Cameras, Colliders, Particles, Actors, Decals, Environment, Navigation and Quader. A composite entity matches every applicable category.

Matching descendants remain reachable even when their ordinary tree branch is collapsed. Their ancestors stay visible as hierarchy context. Actor-owned inline cameras continue to use their existing Actor row. Clear the search and choose **All** to restore the ordinary tree and its retained expansion state. Shift selection spans only displayed rows; filtered or collapsed entities do not silently enter the range. An active rename remains visible until the edit finishes.

The projection is cached by scene identity, authoring revision, normalized query and type. Stable UI frames reuse copied metadata and child lists. Selection and runtime pose updates do not rebuild name/type matching. Search does not load models, inspect files, change authored visibility or create history commands.

## Camera bookmarks

Open **View → Camera Bookmarks**. Enter a name and click **Save View**. Click a saved row to restore its position, look direction, up vector and lens in the free editor camera. A bookmark can capture the visible view while looking through a scene camera; recalling it returns to the free editor view and leaves camera entities untouched.

Right-click a row to **Update from Current View**, **Rename…** or **Delete** it. Names must be unique within their scene. Save an untitled scene before adding bookmarks. Each saved scene has up to 64 bookmarks; Save As creates a distinct scene identity and therefore a separate bookmark list.

The atomic metadata file is `.crimson/camera-bookmarks.json`. Opening a project does not rewrite it. Failed writes retain the prior in-memory views and disk file. External changes require **Reload Bookmarks** before another edit, preventing an older editor instance from overwriting another owner's work. Malformed metadata retains the last accepted views and is not silently replaced. Scene dirty state and scene Undo/Redo do not own bookmark edits.

## Asset References

Right-click an asset in the Asset Browser and choose **Find References**. The **Asset References** window has **Uses** and **Dependencies** tabs and a search field. It distinguishes saved, current/open, history and editor-state references from the captured project snapshot.

Use **Select** to select a current-scene entity in the Outliner and Inspector. **Open** opens the owning scene or native asset editor; opening another scene follows the usual unsaved-change flow and selects the referenced entity after the scene loads. Models, textures and environments are revealed in the Asset Browser. Historical/editor-state rows do not imply a currently selectable entity.

The report is an explicit worker request over immutable source, catalog, location and reference-policy snapshots. It shares the deletion/retention reference traversal, without publishing replacement policy or warming GPU assets. Browser selection and hover do not trigger a report. Changed inputs mark the retained report stale; **Refresh** captures current inputs. Stale results cannot navigate to obsolete owners. Closing or replacing a request cancels its publication. List filtering and drawing reuse cached rows and the table clipper.

## Play From Here

Choose **Play → Play From Here** or press **Shift+F5** in Edit mode. This starts the scene's existing designated Player at the Scene view's eye position and look direction. It uses the Player's real owned Camera, authored eye offset, Movement and Controller profile. The Game lens remains the authored Player camera lens; the editor's field of view is not copied into it.

The command changes only the transient Play copy. It converts the requested world placement through the Player's complete parent transform, retains the authored camera basis/follow definition, and selects the owned camera only for this run. It does not create a replacement Player or modify the saved spawn. An absent/disabled Player or unavailable owned camera reports an error. The accepted native character capsule must fit against solid collision and other characters. Touching support is allowed; an obstructed placement stays in Edit. A valid elevated placement begins falling under ordinary gravity.

Mesh-collision preparation retains the requested view while current resources become ready. Scene changes cancel that pending start. Ordinary **P / Play** still starts from the authored placement. **Shift+Escape / Stop** restores the original Edit world, selection, dirty state, history and editor view.

## Pause, Resume and Step

During Play, use the top bar or **Play** menu:

| Action | Shortcut | Behavior |
| --- | --- | --- |
| Pause / Resume | **F6** | Holds or resumes the existing runtime world |
| Step | **F7** | While paused, advances exactly one 1/60 second tick and stays paused |
| Stop | **Shift+Escape** | Discards the runtime copy and restores Edit |

Pause releases Game input and leaves Scene navigation and the runtime Inspector available. Physics, characters, ragdolls, enemies, Gameplay Effects, particles, imported animation and light presentation keep their state. Step advances those fixed-tick owners together and displays the completed pose. Paused wall time does not accumulate into a catch-up burst on Resume. Held movement, pending jump/look and trace edges are cancelled across these transitions; a Step has no gameplay input.

The last stepped trigger-event batch remains available to read while paused. The next Step replaces it; Resume and Stop clear it. Ordinary unpaused advances retain their existing latest-advance batch contract. Material **Time → Ignore Pause** remains an explicit wall-time source and can continue animating a material while simulation time is held.

Shortcuts respect active text fields, popups, drags, native-window focus and document transactions. **P** remains layout-aware and starts only an inactive session.

## Ownership and regression entry points

`SceneOutlinerProjection` owns cached hierarchy filtering. `CameraBookmarks` owns validated atomic editor metadata, with `CameraBookmarksPanel` providing its UI. `AssetReferencesPanel` owns request cancellation, freshness and retained rows over `AssetReferenceReport`; `AssetLibrary` captures the authoritative source snapshot. `PlaySession` owns placement, pause/step and accepted time; `PhysicsWorld` owns native capsule clearance. `EditorSession` dispatches actions at its safe document boundary and owns Game input acquisition.

Focused checks are `scene_component_core`, `outliner_ui_core`, `camera_bookmark_core`, `editor_iteration_ui_core`, `asset_reference_report_core`, `play_runtime_core`, `play_shortcut_core`, `ragdoll_runtime_core`, `physics_core`, `game_input_core`, `game_view_input_core`, `camera_pilot_core` and `fps_controller_core`. Existing browser, deletion/retention and scene history regressions remain applicable. These features introduce no material ABI, shader source or renderer view-layout change.
