# Crimson Engine on the complete Quader application

The application base is the complete `quader-crimson` tree and history at
`f6e0c6762592b7c17aa5d264fa3d3b834601f563`. Modeling, topology, UVs, tools,
selection, project documents, history, assets, the Qt main window, input,
composition preparation and the native event loop remain Quader-owned.
The application links `quader_release_profile_full`.

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

## Domain boundaries

Quader's ProjectSession and SceneSession remain the modeling document, history,
selection and tool authorities. Engine LevelSession owns its schema-25 level,
UUID asset library, reusable document controllers and first-person PlaySession.
The two asset-ID domains are distinct. Modeling publications enter levels through
the existing detached Quader reader/import/bridge contract, including paint,
materials, hierarchy and source identity; they do not alias either registry.

Engine views and document controls are added to the existing Qt shell. Their
File/Edit/history/dirty/save/cancel boundaries remain with their Engine document
controllers. Level selection and viewport picking retain persistent IDs and
immutable generation/camera/target guards. Game owns physical movement/look/jump
only after explicit focus acquisition, and releases ownership on interruption.

The integration must preserve generated materials and Blend recipes, decals,
particle/animation/ragdoll documents and previews, authored camera/light data,
collision/navigation, fixed-step Play, resource workers and source synchronization.
Presence of a source file or a successful library build does not establish that
its user workflow is integrated.

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
