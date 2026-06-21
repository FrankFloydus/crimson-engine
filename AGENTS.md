# Quader Agent Guide

This repository is the bootstrap for a professional 3D modeling app. Keep early code boring,
testable, and future-friendly.

## Project Identity And Source Of Truth

- This repository is the native standalone Windows Quader app. It is not the Unity plugin, and it
  should not keep the old plugin/DLL protection layer behavior unless a native-app requirement
  explicitly asks for it.
- The Unity plugin checkout at `C:\Users\Drako\Desktop\_QUADER_DEV\quader-unity` is the active
  source of truth for modeling viewport parity: viewport camera behavior, grid visuals, gizmo
  visuals and placement, keybinds, box creation, selection modes, overlay colors, transparency,
  hover/selected states, multiselection, and depth behavior.
- The original Unity package lives at
  `C:\Users\Drako\Desktop\_QUADER_DEV\quader-unity\package\com.qvertexworks.quader`.
- The original SDK lives at `C:\Users\Drako\Desktop\_QUADER_DEV\quader-unity\sdk`. This app keeps a
  copied SDK under `sdk`; preserve the SDK boundary and adapt native-app behavior around
  it instead of rewriting SDK semantics in UI or renderer code.
- For portable modeling, selection, picking, overlay payloads, box creation, navigation, and gizmo
  semantics, check SDK/editor-core first. Important files include:
  `sdk/packages/editor-core/src/session_selection_overlay.cpp`,
  `sdk/packages/editor-core/src/selection_policy.cpp`,
  `sdk/packages/editor-core/src/session_picking.cpp`,
  `sdk/packages/editor-core/src/overlay_visibility.cpp`, and
  `sdk/packages/editor-core/include/quader_editor_core/session.h`.
- For presentation parity, check the Unity viewport renderer and shaders first:
  `Editor/Viewport/QuaderViewportRenderer.cs`,
  `Editor/Viewport/QuaderOverlayMeshCache.cs`,
  `Editor/Viewport/QuaderViewportSettingsState.cs`,
  `Runtime/Shaders/QuaderOverlayLine.shader`,
  `Runtime/Shaders/QuaderOverlayPoint.shader`, and
  `Runtime/Shaders/QuaderOverlayFlat.shader`.
- Do not invent overlay, selection, gizmo, grid, or navigation behavior from screenshots or local
  guesses. Read the Unity plugin and SDK source, mirror it, then document any intentional
  standalone-native divergence.

## NON-NEGOTIABLE RESTORE AND PARITY RULES

- WHEN THE USER TELLS YOU TO RESTORE A PREVIOUS WORKING LOGIC, FUNCTIONING, OR BEHAVIOR, DO NOT
  REDESIGN IT, DO NOT "IMPROVE" IT, DO NOT REINTERPRET IT, AND DO NOT INVENT A NEW POLICY. Find the
  previous working code and integrate the exact same logic into the current architecture.
- I MUST JUST INTEGRATE THE EXACT SAME CODE WITHOUT INVENTING NEW THING.
- I MUST JUST INTEGRATE THE EXACT SAME CODE WITHOUT INVENTING NEW THING.
- I MUST JUST INTEGRATE THE EXACT SAME CODE WITHOUT INVENTING NEW THING.
- Refactors may move that code behind cleaner classes or smaller functions only if the runtime
  behavior, state transitions, ordering, cache invalidation, and side effects remain identical to
  the previous working version. If exact behavior cannot be preserved, stop and explain the specific
  conflict before changing anything.
- WHEN THE USER ASKS FOR 100% UNITY PARITY, IT MEANS CHECK THE UNITY CODE FIRST AND IMPLEMENT THE
  CODE AND BEHAVIOR 100% THE SAME, unless the user explicitly specifies a different native-app
  behavior. Do not substitute guesses, approximations, screenshots, or local preferences for Unity
  source parity.

## Architecture Rules

- Treat the Unity plugin at `C:\Users\Drako\Desktop\_QUADER_DEV\quader-unity` as the source of
  truth for Quader modeling behavior, viewport visuals, grid behavior, gizmo visuals, keybinds,
  overlays, selection, and tool interaction. Do not invent native-app behavior when parity is
  unclear; inspect the Unity plugin and copied SDK first, then implement the matching behavior.
- Vulkan is the first runtime backend. Do not introduce OpenGL-only assumptions into shared code.
- Keep backend-specific behavior behind `RenderSystem`, `PlatformWindow`, or explicit backend
  adapters so OpenGL and Metal can be added later.
- `SceneDocument` is the editor source of truth. Filament entities, buffers, materials, and views
  are render proxies derived from document state.
- UI panels do not own Filament resources directly. Route rendering and texture requests through
  renderer-facing services.
- The in-app console is an ImGui dockable panel. Do not spawn or rely on a separate console window
  for app logs.
- Use the `Logger` macros (`LOG_INFO`, `LOG_ERROR`, etc.) instead of direct `spdlog` calls outside
  the logging layer.

## OOP Refactor Architecture Rules

- These OOP rules are based on the C++ Core Guidelines, Fowler's refactoring catalog, Object
  Mentor/SOLID sources, LLVM/Google C++ style guidance, and GoogleTest practice. Use them as
  decision rules, not slogans.
- Before adding a class, name its responsibility in one sentence. If the sentence needs "and",
  "misc", "helper", "manager", or "utility" to explain the class, redesign the boundary before
  coding. A good class has one cohesive responsibility and one dominant reason to change.
- Prefer high cohesion and low coupling over superficial class count. Split a class when state and
  methods form separate responsibility clusters; do not split just to create more files.
- Assign behavior to the information expert: the class/service that already owns the data,
  invariants, policy, or lifetime needed to perform the behavior. UI/controllers may initiate a
  use case, but they should not absorb domain, SDK, renderer, or document rules just because they
  receive the input first.
- Use `class` for types with invariants and behavior; use `struct` for passive data records whose
  fields can vary independently. Constructors establish invariants and public methods preserve
  them. Keep invariant-bearing data `private`.
- Interfaces are contracts. Make interfaces explicit, strongly typed, small, and client-focused.
  Prefer return values over output parameters. Use references for required collaborators and
  pointers only when nullability is part of the contract.
- Prefer composition for code reuse. Use public inheritance only when the derived type is truly
  substitutable for the base type and existing code written for the base should work correctly with
  the derived type without special cases.
- Public inheritance means "is-a", never "I want this implementation." If the goal is shared
  implementation, first consider a contained helper/service, policy object, free function, template,
  or strategy object.
- Distinguish interface inheritance from implementation inheritance. Interface bases should usually
  be pure abstract classes with no data. Keep base-class data `private`; avoid `protected` data
  because it lets every derived class weaken the base invariant.
- Do not add virtual functions "just in case." A virtual function is a long-term extension
  contract. Add it only when callers need runtime polymorphism and the override contract is clear.
- Mark first virtual declarations with `virtual`, overrides with `override`, and only use `final`
  when deliberately closing extension. A polymorphic base needs either a public virtual destructor
  or a protected non-virtual destructor.
- Keep inheritance shallow. If a hierarchy needs more than a narrow base plus concrete variants,
  re-check whether state/strategy composition would express the variation more clearly.
- Split broad interfaces. Do not force derived classes or clients to implement irrelevant methods,
  no-op methods, "unsupported" branches, or methods that throw because the interface is too wide.
- Use polymorphism when a stable set of variants has different behavior and callers are branching
  on type/kind/tool/action ids in multiple places. Put the variant behavior on the variant class.
  Keep simple conditionals when the branch is local, unlikely to grow, and clearer than a hierarchy.
- Use State or Strategy objects when behavior changes at runtime, when the object already has a
  separate inheritance reason, or when changing the algorithm should not change the owning object.
- Use Command objects for user-triggered operations with execution policy, undo/redo, history,
  preview, or routing semantics. A command class should represent one user intent and own the data
  needed to execute and undo/redo that intent.
- Use facades only to simplify access to a subsystem. A facade delegates to focused collaborators;
  it must not hide unrelated ownership or become the new large class.
- Use controllers for input translation and orchestration. Controllers may route events to
  commands/services/tools, but they must not own domain rules, renderer algorithms, long-lived
  resources, or SDK semantics.
- Use services as focused use-case boundaries. A service should coordinate a coherent capability,
  not become a dumping ground below a UI panel.
- Apply dependency inversion at subsystem boundaries. High-level policy should not directly include
  or construct low-level renderer/platform/persistence/SDK details; both sides should meet through
  the narrowest existing app-owned contract.
- Design for testability by isolating pure behavior from I/O, rendering, global state, platform
  state, and time. Inject collaborators through constructors, context structs, or narrow interfaces
  when that makes behavior independently testable.
- Prefer dependency injection over service locator. Configuration and wiring belong at composition
  roots such as `App`, `ProjectSession`, `UISystem`, or explicit factory/catalog code; use sites
  should receive the already chosen dependency.
- Do not add abstraction only for ceremony. Add an interface, strategy, adapter, or policy object
  only when it creates a real seam for alternate production behavior, fast tests, platform/backend
  substitution, command routing, or expected extension.
- Avoid hidden globals and singletons in new architecture. If shared state is required, make the
  owner explicit in `ProjectSession`, `UIContext`, `ViewportToolContext`, a render service, or a
  modeling session service.
- Separate commands from queries. Query methods must not change observable state; mutation methods
  should be named and routed as commands, return explicit results/events, and avoid hidden side
  effects.
- Use RAII for every owned resource. Prefer values and scoped members first, `std::unique_ptr` for
  singular dynamic ownership, and `std::shared_ptr` only when shared ownership is truly semantic.
  Raw pointers and references are non-owning by default.
- Prefer the rule of zero. If a type defines or deletes any copy/move/destructor operation, decide
  all special member functions explicitly. Polymorphic bases should suppress public copy/move to
  avoid slicing; add an explicit `clone()` only when polymorphic copying is required.
- Keep headers self-contained and dependency-light. Headers declare contracts; non-trivial
  algorithms and object definitions belong in `.cpp` files. A `.cpp` that implements a public
  header includes that header first.
- Use forward declarations only to reduce real coupling safely. Include the full header when a type
  is inherited from, stored by value, used inline/template code, or needed for ownership semantics.
- Use Pimpl sparingly for stable, widely included interfaces where compile-time isolation or
  ABI/layout stability matters. Do not add Pimpl to small local classes, hot-path value types, or as
  a substitute for good ownership.
- Refactoring must preserve observable behavior. Separate mechanical moves/renames from behavior
  edits, keep tests or characterization checks around moved behavior, and build/test after
  meaningful architectural steps.
- Tests should verify observable behavior through public subsystem APIs, command results, document
  state, emitted events, or render/cache payloads. Avoid tests that lock down private methods, call
  order, or helper classes unless that interaction is itself the contract.
- Prefer state-based assertions over mock interaction assertions. Use mocks mainly at architecture
  boundaries where the interaction is the contract; prefer fakes/in-memory implementations when
  they make behavior clearer.
- Keep tests isolated and deterministic. Each test builds its own fixture state and must not depend
  on mutations from previous tests, wall-clock timing, renderer availability, or global state.
- For C++ seams, prefer narrow abstract interfaces for runtime substitution. If virtual dispatch is
  too costly and profiling proves it, use template-based or compile-time injection at the hot-path
  boundary while keeping the public architecture contract readable.
- When restoring old behavior or Unity parity, first capture the current expected behavior from the
  source/test, then move ownership without changing ordering, side effects, state transitions,
  invalidation, or error handling.
- Reference sources for these OOP rules:
  `https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines`,
  `https://objectmentor.com/resources/articles/srp.pdf`,
  `https://objectmentor.com/resources/articles/ocp.pdf`,
  `https://objectmentor.com/resources/articles/lsp.pdf`,
  `https://objectmentor.com/resources/articles/isp.pdf`,
  `https://objectmentor.com/resources/articles/dip.pdf`,
  `https://martinfowler.com/books/refactoring.html`,
  `https://refactoring.com/catalog/replaceConditionalWithPolymorphism.html`,
  `https://martinfowler.com/articles/injection.html`,
  `https://google.github.io/styleguide/cppguide.html`,
  `https://llvm.org/docs/CodingStandards.html`,
  `https://google.github.io/googletest/primer.html`,
  and `https://abseil.io/resources/swe-book/html/ch12.html`.
- `oop-refactor.md` records the accepted OOP architecture and guardrails. Preserve its architecture
  unless the user explicitly approves a new architecture pass. Do not re-collapse extracted owners
  back into large facade, panel, or renderer classes.
- A class is valid only when it owns behavior, state, policy, lifetime, or a real contract. Do not
  add empty `.cpp` files, constructor-only `.cpp` files, pass-through subclasses, or giant inline
  catalogs. Header-only user-facing tool classes are acceptable when they provide meaningful
  descriptor/id/operation overrides and all shared behavior lives in the correct base class.
- Keep user-facing tools discoverable by name. If someone asks where the clipping, bevel, merge,
  dissolve, box, cut, or transform tool lives, the answer should be an obvious file under
  `src/modeling/tools/**` or `src/viewport/tools/**`, not a hidden branch inside a panel or service.
- Add source files to `CMakeLists.txt` whenever a new `.cpp` is introduced. Add or update the
  relevant guardrail tests when changing architecture boundaries, command routing, catalogs,
  viewport tools, session services, render extraction, or document command ownership.

Decision tree for new behavior:

- SDK/editor-core semantic behavior belongs in `sdk` first when it should be portable across
  Quader hosts. The native app should call it through app services instead of duplicating SDK
  semantics in UI, render, or viewport code.
- Scene graph and asset truth belongs in `SceneDocument`, `ProjectDocument`, `ProjectSession`,
  `AssetRegistry`, typed asset stores, and document commands. `ProjectDocument` owns project
  records, the scene document, asset registry, project object ids, and project revision.
  `ProjectSession` owns live editor-session wiring: document, undo/redo, `DocumentCommandService`,
  and `ModelingSessionService`. UI must not directly create, destroy, parent, select, rename,
  transform, or mutate document entities when a command/service owner exists.
- User-triggered document mutations belong in `src/document/commands/**` as `IUndoableCommand`
  implementations, routed by `DocumentCommandService` or `EditorCommandService`.
- Scene-only transform previews belong in `SceneTransformPreviewTransaction`. Do not preview scene
  transforms by directly mutating scene state from UI panels.
- User-triggered modeling operations belong in `src/modeling/tools/**` as `CommandTool`
  subclasses and are executed through `ModelingCommandDispatcher`, `ModelingPreviewCommandService`,
  or `EditorCommandService`, not direct UI calls to raw modeling mutation APIs.
- Modeling session state and SDK synchronization belong in `src/modeling/session/**`. Keep
  `ModelingSessionService` as a facade. Put raw SDK operation calls in `SdkOperationExecutor`,
  command execution and sync in `ModelingCommandExecutor`, selection in
  `ModelingSelectionService`, overlays in `ModelingOverlayService`, mesh rebuild/sync in
  `ModelingMeshSyncService` and `SceneModelingSyncService`, transform drag/preview state in
  `TransformDragController` and `TransformPreviewService`, modal tool runtime state in
  `ModalToolRuntime`, interaction settings in `ModelingInteractionSettingsService`, and repeat,
  clipboard, registry, and tool settings in their named services. `ModelingSelectionService` owns
  picking caches and transformed picking payloads; `ModelingOverlayService` owns SDK overlay
  materialization; `SceneModelingSyncService` owns SDK object/entity mapping validity and content
  revision tracking.
- Viewport pointer, modal, transform, and drawing interactions belong in `src/viewport/tools/**`
  and `ViewportToolRuntime`. `ViewportPanel` builds context and delegates; it does not own modal
  tool implementation, selection picking policy, transform gizmo behavior, or box/cut/knife/mirror
  state machines.
- UI panels and controllers own presentation and routing only. Put menu wiring in `MainMenuBar`,
  viewport context menu presentation in `ViewportContextMenuPresenter`, per-frame viewport input
  assembly in `ViewportInputFrameBuilder`, hover in `ViewportHoverController`, selection click
  routing in `ViewportSelectionController`, viewport pane state in `ViewportPaneController`,
  shortcuts in `ViewportShortcutController`, gizmo UI routing in `TransformGizmoController`, and
  modal overlay drawing in `ModalToolOverlayRenderer`.
- Filament resource ownership and render extraction belong in `src/render/**`. Keep backend and
  lifetime orchestration in `RenderSystem`, viewport render pass orchestration in
  `ViewportRenderer`/`ViewportRenderPassCoordinator`, document-to-render synchronization in
  `SceneRenderBridge` and `SceneRenderSyncService`, mesh/light/preview proxy lifetime in their
  cache classes, material/texture binding in their binding services, buffer uploads in
  `FilamentBufferDescriptorFactory`, and overlay geometry/proxy construction in
  `src/render/overlay/**`.
- `SceneRenderBridge` is a renderer facade/composition root, not the owner of new render
  algorithms. New Filament resource ownership belongs in focused caches, factories, and services.
- Do not add new modal tool logic, selection picking, transform gizmo math, SDK command execution,
  overlay construction, render proxy lifetime, or document mutation directly to `ViewportPanel`,
  `UISystem`, `SceneRenderBridge`, or `ModelingSessionService::Impl`. When a file starts collecting
  unrelated state, create or extend a focused sibling service/controller instead of adding another
  private section to the facade.

Modeling command tool rules:

- `CommandTool` is the base contract. Every modeling command must provide a descriptor,
  availability policy, history policy, and execution path through that contract.
- SDK-backed instant operations use `SdkInstantOperationTool` and live under
  `src/modeling/tools/commands`. The subclass must expose the user-facing descriptor and the exact
  `ModelingInstantOperationId`. Examples: `DeleteSelectionTool`, `DuplicateMeshesTool`,
  `CombineMeshesTool`, `DissolveEdgesTool`, `MergeVerticesToCenterTool`.
- Native service-backed instant commands use `CustomInstantCommandTool` or a narrower existing base
  and should put real implementation in a `.cpp` only when there is meaningful custom behavior.
  Do not create a `.cpp` merely to call a base constructor or repeat base behavior.
- SDK context actions use `ContextActionTool` and live under `src/modeling/tools/context`.
  Examples: `BridgeEdgesTool`, `RadialAlignFacesTool`, `ApplyActiveMaterialTool`.
- Preview/modal operation commands use `PreviewCommandTool` or `SdkPreviewOperationTool` and live
  under `src/modeling/tools/preview` when they begin an operation that can be committed, canceled,
  or adjusted. Examples: `EdgeBevelTool`, `ThickenTool`, `SliceQuadTool`, `MergeByDistanceTool`.
- Register command tools in `ModelingCommandToolCatalog.cpp`. Keep catalog implementation out of
  headers, keep ids unique, and update `tests/modeling_tool_catalog_tests.cpp` for every new
  command family or routing rule.

Viewport tool rules:

- `Tool` and `ViewportTool` are the base viewport contracts. New viewport tools must communicate
  through `ViewportToolEvent`, `ViewportToolContext`, and `ViewportToolResult`.
- Selection/activation tools derive from `ViewportTool` or `InteractiveViewportTool` when they
  only need activation/fallback behavior. Transform tools use `TransformViewportTool`. Modal tools
  use the narrowest existing base: `TwoStageBoxTool` for box-like draw/height workflows,
  `DrawnPlaneTool` for plane-captured point workflows, `PointSequenceTool` for click-sequence
  tools, `DragCommitTool` for drag/commit tools, or `ModalViewportTool` when no narrower base fits.
- Put concrete viewport tools under `src/viewport/tools/interactive`. Descriptor-only wrappers may
  remain header-only. If a tool overrides event handling or owns a state machine, put that behavior
  in a `.cpp` and keep the base-class contract clear.
- Register viewport tools in the viewport catalog implementation, not inline in headers, and update
  `tests/viewport_tool_catalog_tests.cpp` when ids, shortcuts, ownership, or runtime behavior
  changes.

## UI Architecture Rules

- Use `UI` casing for UI infrastructure types (`UISystem`, `UIPanel`, `UIContext`,
  `UIPanelManager`, `UISettings`, `UIStyleTokens`), not `Ui`.
- Keep `UISystem` as the composition root for ImGui initialization, menus, dockspace, layout
  persistence, and panel registration. Put panel contents in concrete `UIPanel` classes such as
  `ViewportPanel`, `OutlinerPanel`, `InspectorPanel`, and `ConsolePanel`.
- Route frame dependencies through `UIContext`. Do not make panels reach sideways into app globals.
- Always keep localization in mind when adding or changing user-facing UI text. Route labels,
  menu items, panel titles, tool names, settings names, status text, and undo/history text through
  `LocalizationService` with stable ImGui IDs where needed, and update `resources/locales/*.lang`
  plus localization/guardrail tests in the same change. Keep raw English literals only for stable
  internal identifiers, logs, or non-user-facing fallback/debug text.
- Put shared UI colors, font size, paddings, and other visual constants in `UIStyleTokens` instead
  of scattering hardcoded values across panels.
- Persist dock layout through ImGui's ini file and panel visibility through `UISettings`; keep these
  under the user profile rather than repo-local files.
- Keep ImGui's dock/window menu button available (`WindowMenuButtonPosition` must not be
  `ImGuiDir_None`) so users can restore and hide tab bars from docked panels.
- Avoid fragile fill-width widgets in resizable dock panels. Do not put `SetNextItemWidth(-1.0f)`
  inside collapsing table cells or use saved stretch table columns for inspector-style form rows.
  Use responsive fixed/minimum widths or `UILayout` helpers so inputs cannot collapse to 1px after a
  splitter resize.
- For scene object names, use `SceneEntityNaming`. Mesh names start as `Mesh`, then `Mesh_1`,
  `Mesh_2`, etc.; do not show or create user-facing `SDK Object X`, `Mesh_0`, or `Mesh 0` names.
- Do not append text type suffixes such as `[Mesh]` or `[Light]` in the outliner. Use plain entity
  names until icon support is added.
- Do not create placeholder startup scene content such as default meshes or fake lights unless a
  product requirement explicitly asks for it.

## Transform Preview And Scene Sync Rules

- Unity Shift+gizmo behavior is the reference: Shift duplicates only in object selection mode for
  Move/Rotate/Scale; in component modes Shift maps to topology behavior (`Move -> Extrude`,
  `Scale -> Inset`) and must not duplicate objects.
- SDK transform previews are transactions. `begin_transform_preview(true)` creates a zero-offset
  duplicate, selects the duplicate, and uses that post-duplicate state as the preview base. Preview
  update deltas are absolute from drag start, not accumulated frame deltas.
- Native transform component inputs and viewport gizmo transforms should both behave in global/world
  space unless a future product requirement explicitly introduces local-space editing. Share pivot
  and world-delta helpers instead of adding direct local Euler/scale mutations in individual panels.
- Do not treat SDK `content_revision` alone as a unique identity for every live preview mesh state.
  Preview code can restore a base state and reapply a new delta, so bridge code that renders or
  caches scene objects must force-refresh during transform preview begin/update/active commit/cancel.
- If `AssetRegistry::replaceMesh` mutates an existing mesh under the same `AssetId`, make sure the
  `SceneDocument` revision changes before relying on `SceneRenderBridge::sync`; otherwise rendering
  can show stale geometry while SDK picking/selection sees the updated mesh.
- For Shift duplicate regressions, keep app-level coverage that verifies duplicate preview updates
  move beyond the first offset, commit at the final position, and remain pick-selectable.

## Overlay And Selection Rules

- `src/render/ViewportOverlayPolicy.h` is this app's local mirror of Unity overlay
  presentation policy. Keep layer draw order, colors, alpha, widths, point sizes, and
  depth/draw-on-top decisions centralized there.
- `ModelingSessionService` should transport SDK overlay payloads and use the overlay policy for
  presentation color mapping. Do not scatter Unity color constants through unrelated files.
- `SceneRenderBridge` renders face/fill triangle overlays and GPU screen-space wire/vertex
  overlays. Do not reintroduce raw Filament `LINES`/`POINTS`, ImGui wire fallbacks, or unfeathered
  triangle-strip shortcuts for selection overlays; those regressed line quality, depth behavior,
  and Unity-like source wire visibility.
- Wire and vertex overlays are built as screen-space quads in `SceneRenderBridge` from the
  `ViewportOverlayCamera` passed by `ViewportPanel`. The overlay materials live at
  `resources/materials/quader_overlay_line.mat` and `resources/materials/quader_overlay_point.mat`
  and must use `vertexDomain : device` with `POSITION` bound as `FLOAT4`.
- Filament device-domain overlay vertices must be normalized device coordinates with `w = 1`.
  Do not feed perspective clip-space positions with varying `w`; that caused selected/source wires
  to disappear or explode into giant viewport-sized cages. Let Filament perform its own backend
  Y/z fixups for Vulkan/Metal/OpenGL.
- Apply Unity-style overlay depth bias in view space before projection. Lines must be clipped
  against the near plane before pixel expansion. Device-domain overlay renderables must disable
  renderable culling because world-space bounds are not meaningful for final device-space quads.
- Filament uses reversed depth. For depth-tested overlay passes, use `DepthFunc::GE`, not Unity's
  literal `LessEqual` enum mapping. Draw-on-top layers use `DepthFunc::A`; depth-stamped face color
  passes use `DepthFunc::E`.
- Unity overlay contract: `SourceWire`, `OpenEdge`, `HoverFaceEdge`, and `HoverEdge` are
  depth-tested; `SelectedFaceEdge`, `SelectedEdge`, and `DiagnosticEdge` draw on top; selected,
  hovered, and snap vertices draw on top; face fills use Unity's depth-stamped two-pass behavior.
- The Unity layer order is the reference order: `SelectedVisibleFace`, `SelectedFaceMask`,
  `SourceWire`, `SelectedXrayFace`, `HoverFace`, `OpenEdge`, `DiagnosticEdge`,
  `SelectedFaceEdge`, `SelectedEdge`, `HoverFaceEdge`, `HoverEdge`, `UnselectedVertex`,
  `SelectedVertex`, `HoverVertex`, `SnapTargetVertex`.

## TDD Workflow

- Add or update GoogleTest coverage before implementing non-trivial core behavior.
- Keep tests fast and headless. Scene data, assets, baking descriptors, logger sinks, and viewport
  navigation must be testable without opening a window.
- GPU/window behavior is validated with manual smoke tests until a render-test harness exists.

## Material And Baking Direction

- Treat materials as renderer-independent documents first, Filament packages second.
- Preserve CPU-side mesh data in `MeshAsset`; do not throw topology/UV data away after uploading.
- Texture assets must carry usage metadata such as base color, normal, roughness, metallic,
  curvature, thickness, AO, position, ID, and masks.
- Baking is job-oriented. Add future map generation through `BakeService` and `BakeBackend`, not by
  burying it in the viewport renderer.

## Dependency Rules

- Use CMake and vcpkg manifest mode for dependencies.
- Do not vendor SDL, ImGui, spdlog, GoogleTest, or a new Filament SDK into this repo.
- Use the Filament SDK path provided by `FILAMENT_SDK_ROOT`; default is:
  `C:\Users\Drako\Desktop\quader-windows\_SOURCES\filament-v1.71.6-windows`.

## Expected Commands

```powershell
cmake --preset vs2026
cmake --build --preset vs2026-debug
ctest --preset vs2026-debug
```

If PowerShell inherits conflicting `PATH`/`Path` entries and CMake cannot find tools, use the
local workaround:

```powershell
$p = $env:Path
cmd /c "set PATH=& set Path=$p& cmake --build --preset vs2026-debug --target quader"
```

Manual smoke checklist:

- Vulkan backend initializes.
- Main SDL window opens without a separate console window.
- Dockspace appears.
- Console panel is docked in-app and receives log lines.
- Viewport panel docks/moves and renders Quader SDK-created meshes with the default material.
- Create and select a cube, capture the viewport, and verify selected/source mesh wires are visible
  on the cube with no giant viewport-sized overlay lines.
- MMB orbits, Shift+MMB pans, mouse wheel zooms.
