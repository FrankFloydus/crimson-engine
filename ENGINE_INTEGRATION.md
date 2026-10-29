# Crimson Engine on the Quader Public 0.1 application

The application base is the complete `quader-crimson` tree and history at
`f6e0c6762592b7c17aa5d264fa3d3b834601f563`. Modeling, topology, tools,
selection, project documents, history, assets, the Qt main window, input,
composition preparation and the native event loop remain Quader-owned.
The application links `quader_release_profile_public`, the immutable Quader
Public 0.1 profile. Its existing public feature gates own the modeling toolset;
Full-only UV and Face Texture authoring remain unavailable. Crimson owns the
Engine material, light, Environment and gameplay components independently.
An explicit host viewport capability restores Rendered presentation without
changing the Public modeling profile. It follows native scene bindings and uses
the same viewport and authored scene; it does not create another scene editor.

The compatible native UI owners are updated from local Quader `main`
`517ab3fcef2e2be9e5c3d8df0891d6175f985e8c`, including flat tab/section styling
from `6c7166e1` and docked Tool Settings/Commands Library from `7b4ce8eb`, with
later panel refinements. This UI port retains this branch's scene, mode and
Crimson renderer contracts; it does not import unrelated renderer or modeling-core
changes from that branch.

The retained Engine domain source is `47d332534a5b6b4d255b311341d3ce4a5de61698`,
under `engine-core/`. The superseded partial Qt migration is preserved on
`codex/qt-editor` at `8584aac`, outside this application's base history. The
source Quader worktrees and their uncommitted changes are not modified by this
integration.

The twelve tracked working-tree fixes present in that source branch were also
copied without modifying the source checkout. Their initial binary diff snapshot
has SHA-256 `cdf3128a2c483d4decf03bc7d89e9aa7fdab4d82965ff36bba3ae34161451e2c`;
it includes dissolve/selection behavior and corresponding regressions. Build and
temporary files from the source checkout are not application inputs.

## Application and device ownership

There is one QApplication, one native event loop and one application root,
created by Quader's `main`, `App`, `UISystem` and `QtMainWindow`. QTimer schedules
semantic input, domain updates and frame preparation. Qt owns native windows,
focus, relative pointer capture, geometry, device, queue, swapchain and pane
composition textures. Engine domains do not wrap Qt HWNDs with SDL and do not
pump native events independently.

The canonical renderer is the single submodule at `thirdparty/crimson-renderer`.
One renderer instance borrows the selected Qt Direct3D 12 device and DIRECT queue,
or its Direct3D 11 device and immediate context, and
hosts both modeling and rich Engine recipes. Existing rich view IDs 0–511 stay
unchanged. Layout 7 reserves modeling shadows at 512–623, four rich modeling
camera profiles at 624–879, and ordinary modeling/editor views at 880–1023.
Every visible pane
is preflighted before recording, and the shared host advances the renderer frame
once. Native copies return textures in the state tracked by QRhi. The primary
queue orders copies before Qt samples them; detached Qt queues use explicit GPU
handoffs in both directions. A consumer-prefix marker protects its previous
reads before the next renderer write. Completion markers protect both queues'
fence references before bounded handoff entries can retire.

Direct3D 11 callbacks preserve and restore the primary context's complete native
state, including initialization and shutdown. Detached Qt windows use separate
devices on the same adapter. Their copies use a bounded shared-texture ring,
nonblocking keyed-mutex acquisition and exact producer/consumer completion
queries. A busy copy retains its accepted image and staged source receipt.
Polling never flushes, waits or advances a renderer frame.

Resource and document owners retire before the renderer, and the renderer drains
before Qt releases its device. A timeout never establishes native completion.
Canvas resizing changes independent pane targets, never resets an unrelated
primary swapchain. Native allocations, pending submissions and retention remain
bounded and observable in the renderer's regression fixtures.

## Projects and workspace modes

Startup retains Quader's Projects page and recent-project workflow. New Project
and Open Project choose a folder. That folder is the sole project root for the
modeling session, Engine data and the filesystem browser; preexisting asset files
are allowed. A single existing `.qproj` is opened through the validated project
pipeline, while a folder without one receives metadata and the ordinary initial
scene. Ambiguous manifests are rejected before replacing the active project.
Initial metadata uses no-replace atomic publication, and failed creation removes
only files and directories created by that attempt.

`ProjectSession` owns the transient `Default` / `Modeling` workspace capability.
The application selects Default on project entry. Every resident and new scene
reads that project owner, so changing scene tabs preserves the accepted mode.
Object/Edit selection context remains separate. Default preserves object
placement, selection, transforms, asset documents and runtime controls; Modeling
allows Public 0.1's geometry, component and instance-edit tools.
Default hides modeling-only toolbar actions, component selection, geometry menus,
geometry palette entries and Commands Library. The ordinary Select, Move, Rotate
and Scale tools remain available. Modeling reveals its retained public controls
without recreating their scene or layout. Full-only Face Texture and UV tools are
absent in both modes. Tool Settings remains available to ordinary scene transforms. Command,
direct API and native gesture owners enforce the same capability gate.

The selector changes capability in the same workspace viewport. It does not
create a second application or project. The original project Asset Browser
presents the whole chosen filesystem with Engine asset support in that view.
Private bookkeeping and disposable caches remain below the project's hidden
metadata directories.

## One authored scene

The native `quader::SceneDocument` and SceneSession services own the sole authored
scene: entity identity, hierarchy, transforms, modeling data, selection, dirty state
and undo/redo. The application has one authoring viewport, one Outliner and one
Inspector. Engine authoring changes native entities and optional component envelopes
through that same command/history boundary. Native General/Transform/light
properties and the mounted Engine component editor share the Inspector panel.

Native QDR's SCNE chunk uses schema 21. It appends optional versioned opaque
`EngineComponentsComponent` payloads and `EngineSceneSettings`, with explicit native
entity UUID references. Missing envelopes retain ordinary Quader scene behavior.
The native codec preserves these payloads without interpreting game components;
the Engine adapter validates their typed contents. Duplication, clipboard,
parenting, scene history and Save use the native scene owners and remap declared
references through their canonical identity rules.

Engine internal scene codec 25 remains the typed payload/projection contract,
separate from QDR schema 21 and material ABI 40. `crimson::SceneDocument`, LevelSession
and PlaySession provide derived CPU evaluation, resource preparation and transient
runtime state. They do not own a second saved level or independent scene history.
There are no per-scene external level bindings or `.crimson-scene` save workflows.

The host publishes immutable geometry/material/light/collision snapshots from the
active native scene into the shared renderer and runtime. Pending preparation
retains the last accepted publication under its binding and lifetime guards.
Play evaluates a transient runtime world of that scene; Stop discards runtime
poses/state while retaining the native authored document and history. Game is its
runtime view. Game input owns physical movement/look/jump only after focus
acquisition and releases ownership on interruption.

Typed material, Blend, decal, particle, animation and ragdoll asset documents retain
their own File/Edit/save/conflict controllers. Project replacement settles Play,
dirty native scenes and asset documents before rebinding resources. Presence of a
source file or a successful library build does not establish user-workflow parity.

## Verification boundaries

The complete modeling source is retained rather than approximated. Required
integration evidence includes the full application link, retained domain tests,
modeling and rich rendering on the same borrowed device/frame, native encoded
pixels after bounded resize, generation-safe readback/copy retirement, and
Qt input/document transaction checks. Application feature acceptance remains a
separate check from low-level rendering or CPU domain tests. Performance suites
and desktop input automation are not routine completion checks.

`crimson-modeler-checks` builds the retained modeling-domain, SDK-consumer and
full native modeler suites. The full native suite uses minimal QPA with the
pinned Windows font database and a test-only virtual 1920×1080 screen at 96 dpi;
this preserves native font/layout checks without desktop windows or cursor
input. The platform color-scheme lifecycle case runs separately on Windows QPA
and creates no widgets. Its paired fatal-diagnostics helper is staged beside
the isolated test executable.
