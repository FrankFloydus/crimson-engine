<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Quader Fluent Modeling API Plan

Status: review draft only. This document specifies the desired public API shape. It is not an
implementation patch.

## Goal

Quader needs a public modeling API that reads like modeling intent:

```cpp
auto mesh = quader.activate_box_tool({.name = "Trim"})
                  .drag_footprint(press, drag)
                  .drag_height(height_drag)
                  .commit();

auto edges = mesh.edges().all();
edges.bevel({.width = 0.15F, .segments = 2}).commit("Bevel Trim Edges");
```

The current portable SDK is a good kernel API, but it is too low-level for normal public API use.
The new layer must be fluent, explicit, and complete enough for Quader, tests, automation, and
portable C++ API use.

## Non-Negotiable Design Rules

- The API must talk in user/editor intent: `create_box`, `activate_box_tool`,
  `mesh.edges().all()`, `edges.bevel(...)`, `faces.thicken(...)`, `preview.commit(...)`.
- The fluent API is not allowed to become a second modeling source of truth. It delegates to SDK
  modeling owners.
- The raw SDK remains available for advanced code and tests, but it is not the recommended
  day-to-day integration surface.
- Every current app-reachable tool, public action, and editor operation gets a named public entry
  point or an explicitly documented reason why it remains internal.
- Kernel-only helpers that are not current app workflows stay out of the fluent public surface until
  the app exposes them through a command, tool, or public workflow.
- String dispatch remains available for command palettes, scripts, and escape hatches, but C++ API
  examples must use typed fluent methods.
- Normal fluent calls return the requested object or receipt directly. Public examples must not
  require manual result unwrapping. Checked calls live behind `try_` names or `checked()`.
- Pointers are not public handles. Public handles are typed, stale-checkable values.
- Selection is a first-class value, not hidden global state.
- Shared selection behavior is modeled once through a `Selection` base contract. Vertex, edge,
  face, and object selections specialize behavior instead of duplicating boilerplate.
- Mesh APIs are organized by object-oriented facets: `mesh.vertices()`, `mesh.edges()`,
  `mesh.faces()`, `mesh.transform()`, `mesh.materials()`, and `mesh.payloads()`.
- Expensive work must be named explicitly. The API must not hide mesh compilation, topology hashing,
  full snapshot allocation, preview mesh regeneration, render upload, or undo/history boundaries.
- All mutations return structured status, dirty flags, revisions, diagnostics, deltas, and selection
  remap data where available.

## Layers

### 1. Portable Core SDK

Namespace: `quader::modeling`

Include:

```cpp
#include <quader/modeling/modeling.hpp>
```

Primary CMake target:

```cmake
target_link_libraries(my_target PRIVATE quader::modeling_sdk)
```

The portable SDK owns polygon documents, typed IDs, operations, validation, QDR document IO,
selection remaps, preview transactions, undo/redo, mesh payloads, semantic overlays, and
renderer-neutral snapshots.

### 2. Fluent SDK API

Namespace: `quader::modeling`

Proposed headers:

```text
src/sdk/core/modeling/include/quader/modeling/fluent/modeling_api.hpp
src/sdk/core/modeling/include/quader/modeling/fluent/mesh_handle.hpp
src/sdk/core/modeling/include/quader/modeling/fluent/selection.hpp
src/sdk/core/modeling/include/quader/modeling/fluent/tools.hpp
src/sdk/core/modeling/include/quader/modeling/fluent/previews.hpp
```

The fluent SDK wraps only public SDK types. It must not include or name native app owners.

### 3. Native Editor Facade

Namespace: `quader`

The native app keeps `QuaderEditorApi` and `QuaderApi` as editor facades. They should expose the
same readable grammar, but they may adapt scene entities, assets, command IDs, viewport panels, and
native history around SDK-owned modeling truth.

## Banned Public Dependencies

Public SDK and fluent SDK headers must not expose:

- SDL, ImGui, Filament, Win32, Qt, or platform headers;
- `SceneDocument`, `ProjectDocument`, `ProjectSession`, `AssetRegistry`;
- `SceneRenderBridge`, render caches, `ViewportOverlayFrame`;
- `QuaderEditorApi`, native command IDs, native command classes;
- `ModelingSessionService`, `DocumentCommandService`, `EditorHistoryService`;
- old copied SDK package names, presets, macros, or checkout wiring.

Native `src/api` headers may expose native public API types, but still must not leak owner internals
such as `SceneDocument`, `AssetRegistry`, or service implementation classes.

## Naming Decision

Do not create a `ModelingWorld` public type. The user-facing root should be an API object that can
reasonably be named `quader` at the call site.

Preferred SDK type:

```cpp
namespace quader::modeling {

class ModelingApi;

}
```

Usage:

```cpp
quader::modeling::ModelingApi quader;
```

Preferred native type:

```cpp
quader::QuaderEditorApi editor;
```

Usage:

```cpp
auto mesh = editor.modeling().create_box({.name = "Wall"});
```

## Error Policy

The fluent API preserves structured failure without forcing every call site to unwrap a result.
Normal calls return the requested object directly and report failures through a configured policy.

```cpp
enum class ErrorPolicy {
  ThrowException,
  StoreLastError,
};

class ModelingException : public std::runtime_error {
public:
  const Error& error() const noexcept;
};
```

Default C++ fluent behavior:

```cpp
auto quader = quader::modeling::ModelingApi::create();
auto mesh = quader.create_box({.name = "Wall"});
mesh.edges().all().bevel({.width = 0.08F}).commit("Bevel Wall");
```

Checked behavior:

```cpp
auto created = quader.checked().create_box({.name = "Wall"});
if (!created.ok()) {
  report(created.error());
  return;
}

auto mesh = created.or_throw();
```

`ThrowException` is the default C++ policy. `StoreLastError` is for exception-free call sites;
object-returning failures yield an invalid handle and keep the full structured error available
through `last_error()`.

No fluent API example should require manual result unwrapping. Explicit `Result<T>` exists for
call sites that cannot use exceptions, for test assertions, and for low-level SDK/kernel work.

Required lower-level result type:

```cpp
template <typename T>
class Result {
public:
  bool ok() const;
  explicit operator bool() const;
  T& or_throw() &;
  T&& or_throw() &&;
  T value_or(T fallback) const;
  const Error& error() const;
  const std::string& error_message() const;
};
```

Checked APIs must be named so they are obvious at the call site: `checked().create_box(...)`,
`try_create_box(...)`, `checked().io().serialize_qdr(...)`. The direct fluent method name stays
clean: `create_box(...)`.

Checked facade shape:

```cpp
class CheckedModelingApi {
public:
  Result<MeshHandle> create_box(BoxOptions options = {});
  Result<MeshHandle> add_document(PolygonDocument document, std::string name = "Mesh");
  CheckedIoApi io();
  CheckedToolsApi tools();
  CheckedOperationsApi operations();
};
```

## Fluent OOP Grammar

The public API should feel closer to a fluent application framework than to a kernel wrapper:
objects expose relationships, relationships expose scopes, and scoped objects expose actions.

Preferred grammar:

```cpp
auto mesh = quader.create_box({.name = "Panel"});

mesh.transform().translate({2.0F, 0.0F, 0.0F});
mesh.faces().by_material_slot(2).shade_smooth();
mesh.edges().boundary().bevel({.width = 0.04F}).commit("Soften Boundary");
mesh.vertices().selected().merge_to_center();
```

Design rules for this grammar:

- nouns own verbs: mesh verbs live on `MeshHandle`, face verbs live on `FaceSelection`, edge verbs
  live on `EdgeSelection`;
- query-like methods return scoped objects: `all()`, `selected()`, `only(...)`, `boundary()`,
  `by_material_slot(...)`, `by_normal(...)`;
- mutating methods return `OperationReceipt` when they finish immediately;
- previewable methods return `PreviewHandle` and require `commit(...)` or `cancel()`;
- chainable configuration methods return `&` only for tool/builder objects, not for completed
  document mutations;
- escape hatches exist, but examples use the expressive object grammar first.

## Error And Diagnostic Types

```cpp
enum class ErrorCode {
  Ok,
  InvalidArgument,
  InvalidId,
  StaleId,
  UnsupportedOperation,
  ValidationFailed,
  IoError,
  InternalError,
};

enum class DiagnosticSeverity {
  Info,
  Warning,
  Error,
};

struct Diagnostic {
  std::string code;
  DiagnosticSeverity severity = DiagnosticSeverity::Error;
  std::string path;
  std::string message;
};

struct Error {
  ErrorCode code = ErrorCode::Ok;
  std::string message;
  std::vector<Diagnostic> diagnostics;
};
```

Expected invalid input, stale IDs, missing objects, operation precondition failures, validation
failures, and IO failures are represented as `Error` values. Direct fluent calls report them through
the configured `ErrorPolicy`; checked calls return them as `Result<T>` failures.

## Identity Types

The SDK uses typed generational IDs:

```cpp
using VertexId = Id<VertexTag>;
using EdgeId = Id<EdgeTag>;
using FaceId = Id<FaceTag>;
using ObjectId = Id<ObjectTag>;
using MaterialId = Id<MaterialTag>;

struct EdgeKey {
  VertexId a;
  VertexId b;
};
```

The fluent layer exposes these directly for C++ callers.

Native `src/api` may expose `QuaderMeshId`, `QuaderVertexId`, `QuaderEdgeId`, and `QuaderFaceId`,
but they must map cleanly to SDK typed IDs.

Handles must revalidate on use. A stale component handle returns `ErrorCode::StaleId`.

## Core Value Types

```cpp
struct Vec2 {
  float x = 0.0F;
  float y = 0.0F;
};

struct Vec3 {
  float x = 0.0F;
  float y = 0.0F;
  float z = 0.0F;
};

struct Transform3 {
  Vec3 x_axis{1.0F, 0.0F, 0.0F};
  Vec3 y_axis{0.0F, 1.0F, 0.0F};
  Vec3 z_axis{0.0F, 0.0F, 1.0F};
  Vec3 origin{};
};

struct Ray {
  Vec3 origin{};
  Vec3 direction{0.0F, 0.0F, 1.0F};
};

enum class SelectionKind {
  Object,
  Vertex,
  Edge,
  Face,
};

struct SelectionUnion {
  SelectionKind kind = SelectionKind::Object;
  ObjectId object{};
  std::vector<ObjectId> objects;
  std::vector<VertexId> vertices;
  std::vector<EdgeKey> edges;
  std::vector<FaceId> faces;
};

struct SelectionSummary {
  std::vector<ObjectId> objects;
  std::size_t vertex_count = 0;
  std::size_t edge_count = 0;
  std::size_t face_count = 0;
  std::uint64_t selection_revision = 0;
};
```

## Public Options

All operations with meaningful parameters use named option structs. No positional argument clusters
for multi-parameter operations.

```cpp
struct BoxOptions {
  std::string name = "Mesh";
  Vec3 min{-1.0F, -1.0F, -1.0F};
  Vec3 max{1.0F, 1.0F, 1.0F};
  MaterialId material{};
};

struct BridgeOptions {
  int steps = 1;
};

struct EdgeBevelOptions {
  float width = 1.0F;
  float profile = 0.5F;
  int segments = 1;
};

struct ThickenOptions {
  float thickness = 0.25F;
  bool from_center = false;
};

struct SliceQuadOptions {
  int x_slices = 1;
  int y_slices = 1;
};

struct MergeByDistanceOptions {
  float tolerance = 0.125F;
};

struct BevelVerticesOptions {
  std::optional<float> distance;
};

struct DuplicateOptions {
  Vec3 offset{};
  bool use_grid_default = false;
};

struct TransformOptions {
  Transform3 transform;
  Vec3 pivot{};
  bool texture_lock = false;
  bool texture_lock_scale = false;
};

struct TranslateOptions {
  Vec3 delta{};
  bool texture_lock = false;
};

struct RotateOptions {
  Vec3 radians{};
  Vec3 pivot{};
};

struct ScaleOptions {
  Vec3 scale{1.0F, 1.0F, 1.0F};
  Vec3 pivot{};
};

struct ExtrudeOptions {
  Vec3 offset{};
  float closed_edge_ledge_size = 0.0F;
};

struct InsetOptions {
  Transform3 transform;
  Vec3 pivot{};
};

enum class CutKeepMode {
  DiscardLeft,
  DiscardRight,
  KeepBoth,
};

struct PlaneCutOptions {
  Vec3 a{};
  Vec3 b{};
  Vec3 c{};
  CutKeepMode keep = CutKeepMode::KeepBoth;
};

struct KnifeSegmentOptions {
  Ray from;
  Ray to;
};

struct KnifeStrokeOptions {
  std::vector<Ray> points;
};

struct InsertEdgeLoopOptions {
  EdgeKey edge{};
  float t = 0.5F;
};
```

Defaults mirror the existing operation schemas:

- bridge steps: `1`, min `1`, max `64`;
- edge bevel width: `1.0`, profile `0.5`, segments `1`, max segments `16`;
- thicken thickness: `0.25`, from center `false`;
- slice quad x/y slices: `1`;
- merge by distance tolerance: `0.125`.

## Operation Receipt

Mutating fluent methods return `OperationReceipt` directly when the operation completes. Operations
that naturally create another fluent object return that object directly, and the object exposes the
receipt that created or last modified it.

```cpp
struct ElementDelta {
  std::vector<VertexId> vertices;
  std::vector<EdgeKey> edges;
  std::vector<FaceId> faces;
};

struct DirtyFlags {
  bool topology = false;
  bool geometry = false;
  bool selection = false;
  bool materials = false;
  bool overlays = false;
};

struct RevisionStamp {
  std::uint64_t content = 0;
  std::uint64_t selection = 0;
  std::uint64_t scene = 0;
  std::uint64_t assets = 0;
};

struct SelectionRemap {
  ElementDelta source;
  ElementDelta destination;
};

struct OperationReceipt {
  bool changed = false;
  RevisionStamp revisions;
  DirtyFlags dirty;
  ElementDelta created;
  ElementDelta deleted;
  ElementDelta affected;
  ElementDelta modified;
  SelectionRemap selection_remap;
  std::vector<Diagnostic> diagnostics;
};
```

This is the user-facing equivalent of SDK `OperationResult`, with revision data required for
viewport and render sync.

```cpp
auto mesh = quader.create_box({.name = "Column"});
OperationReceipt moved = mesh.transform().translate({0.0F, 2.0F, 0.0F});

auto preview = mesh.edges().all().bevel({.width = 0.1F});
OperationReceipt committed = preview.commit("Bevel Column");
```

## Root Fluent SDK API

```cpp
namespace quader::modeling {

class ModelingApi {
public:
  static ModelingApi create(ModelingApiOptions options = {});
  CheckedModelingApi checked();
  const Error* last_error() const;
  void clear_error();

  MeshHandle create_box(BoxOptions options = {});
  MeshHandle add_document(PolygonDocument document, std::string name = "Mesh");

  BoxTool activate_box_tool(BoxToolOptions options = {});
  KnifeTool activate_knife_tool(KnifeToolOptions options = {});
  CutTool activate_cut_tool(CutToolOptions options = {});

  MeshHandle mesh(ObjectId object);
  MeshCollection meshes();
  std::vector<MeshSummary> mesh_summaries() const;
  std::vector<MeshHandle> selected_meshes() const;

  SelectionApi& selection();
  OperationsApi& operations();
  ToolsApi& tools();
  PreviewApi& preview();
  MeshSyncApi& mesh_sync();
  IoApi& io();
  CommandApi& commands();
  ProfilingApi& profiling();

  OperationReceipt undo();
  OperationReceipt redo();
  bool can_undo() const;
  bool can_redo() const;

  RevisionStamp revisions() const;
};

}
```

Call-site target:

```cpp
auto quader = quader::modeling::ModelingApi::create();

auto mesh = quader.create_box({.name = "Blockout"});
mesh.edges().all().bevel({.width = 0.125F, .segments = 2}).commit("Bevel Blockout");
```

Root mesh collection facet:

```cpp
class MeshCollection {
public:
  std::vector<MeshHandle> all() const;
  ObjectSelection selected() const;
  ObjectSelection only(std::span<const ObjectId> objects) const;

  MeshHandle first_selected() const;
  OperationReceipt delete_selected();
  MeshHandle duplicate_selected(DuplicateOptions options = {});
  OperationReceipt combine_selected();
};
```

## Mesh Handle API

`MeshHandle` is the primary fluent object. It is a lightweight handle into a `ModelingApi` session.
It must not own a raw `PolygonDocument*`.

```cpp
class MeshHandle {
public:
  ObjectId id() const;
  bool valid() const;
  MeshSummary summary() const;
  RevisionStamp revisions() const;

  OperationReceipt rename(std::string name);
  OperationReceipt destroy();
  MeshHandle duplicate(DuplicateOptions options = {});
  OperationReceipt combine_with(std::span<const MeshHandle> meshes);

  ObjectSelection select();
  VertexSelection select_all_vertices(); // Convenience for vertices().all().
  EdgeSelection select_all_edges();      // Convenience for edges().all().
  FaceSelection select_all_faces();      // Convenience for faces().all().

  MeshVertices vertices();
  MeshEdges edges();
  MeshFaces faces();
  MeshTransform transform();
  MeshMaterials materials();
  MeshPayloads payloads() const;
  MeshValidation validation() const;
};
```

The mesh handle is intentionally compact. Element-specific behavior lives on element collections and
typed selections, not as one giant repeated method list.

```cpp
class MeshVertices {
public:
  VertexSelection all() const;
  VertexSelection selected() const;
  VertexSelection only(std::span<const VertexId> vertices) const;
  VertexSelection active() const;
};

class MeshEdges {
public:
  EdgeSelection all() const;
  EdgeSelection selected() const;
  EdgeSelection only(std::span<const EdgeKey> edges) const;
  EdgeSelection boundary() const;
  EdgeSelection hard() const;
  EdgeSelection soft() const;
};

class MeshFaces {
public:
  FaceSelection all() const;
  FaceSelection selected() const;
  FaceSelection only(std::span<const FaceId> faces) const;
  FaceSelection by_material_slot(std::uint32_t slot) const;
  FaceSelection by_normal(Vec3 direction, float tolerance = 0.01F) const;
};

class MeshTransform {
public:
  OperationReceipt translate(Vec3 delta);
  OperationReceipt translate(TranslateOptions options);
  OperationReceipt rotate(RotateOptions options);
  OperationReceipt scale(ScaleOptions options);
  OperationReceipt apply(TransformOptions options);
  PreviewHandle preview(TransformOptions options = {});
};

class MeshMaterials {
public:
  OperationReceipt assign(MaterialId material);
  OperationReceipt assign_slot(std::uint32_t slot);
  OperationReceipt assign_slot(FaceSelection faces, std::uint32_t slot);
};

class MeshPayloads {
public:
  AuthoredPolygonPayload authored_polygon() const;
  MeshPayload compile_mesh(MeshCompileOptions options = {}) const;
  MeshCompileResult compile_mesh_if_changed(std::uint64_t previous_revision,
                                            MeshCompileOptions options = {}) const;
};

class MeshValidation {
public:
  OperationReceipt validate() const;
};
```

## Selection Types

Selection values are immutable handles into a session unless explicitly committed back into the
session selection state. Shared behavior lives once on `Selection`; typed subclasses add the
operations that make sense for their element kind.

```cpp
enum class SelectionEdit {
  Replace,
  Add,
  Remove,
  Toggle,
};

class Selection {
public:
  SelectionKind kind() const;
  ObjectId object() const;
  bool empty() const;
  std::size_t count() const;

  bool is_object_selection() const;
  bool is_vertex_selection() const;
  bool is_edge_selection() const;
  bool is_face_selection() const;

  OperationReceipt apply(SelectionEdit edit = SelectionEdit::Replace) const;
  OperationReceipt replace() const;
  OperationReceipt add() const;
  OperationReceipt remove() const;
  OperationReceipt toggle() const;

  SelectionUnion to_union() const;
};

class ObjectSelection final : public Selection {
public:
  std::span<const ObjectId> objects() const;
  OperationReceipt delete_objects() const;
  MeshHandle duplicate(DuplicateOptions options = {}) const;
  OperationReceipt combine() const;
};

class VertexSelection final : public Selection {
public:
  std::span<const VertexId> vertices() const;

  OperationReceipt snap_to_active() const;
  OperationReceipt merge_to_active() const;
  OperationReceipt merge_to_center() const;
  PreviewHandle merge_by_distance(MergeByDistanceOptions options = {}) const;
  OperationReceipt remove_doubles(MergeByDistanceOptions options = {}) const;
  OperationReceipt bevel(BevelVerticesOptions options = {}) const;
  OperationReceipt connect() const;
  OperationReceipt dissolve() const;
  OperationReceipt radial_align() const;
};

class EdgeSelection final : public Selection {
public:
  std::span<const EdgeKey> edges() const;

  OperationReceipt snap_to_active() const;
  OperationReceipt connect() const;
  OperationReceipt split() const;
  OperationReceipt harden_normals() const;
  OperationReceipt soften_normals() const;
  PreviewHandle bevel(EdgeBevelOptions options = {}) const;
  OperationReceipt bridge(BridgeOptions options = {}) const;
  PreviewHandle interpolated_bridge(BridgeOptions options = {}) const;
  OperationReceipt bridge_pairs(BridgeOptions options = {}) const;
  OperationReceipt bridge_boundaries(BridgeOptions options = {}) const;
  OperationReceipt dissolve() const;
  OperationReceipt merge() const;
  OperationReceipt collapse() const;
  OperationReceipt fill_hole() const;
  OperationReceipt radial_align() const;
  OperationReceipt insert_loop(InsertEdgeLoopOptions options) const;
};

class FaceSelection final : public Selection {
public:
  std::span<const FaceId> faces() const;

  OperationReceipt bridge(BridgeOptions options = {}) const;
  PreviewHandle interpolated_bridge(BridgeOptions options = {}) const;
  OperationReceipt flip_normals() const;
  OperationReceipt recalculate_normals(RecalculateNormalsOptions options = {}) const;
  OperationReceipt shade_smooth() const;
  OperationReceipt shade_flat() const;
  OperationReceipt combine() const;
  OperationReceipt collapse() const;
  OperationReceipt radial_align() const;
  MeshHandle extract() const;
  OperationReceipt detach() const;
  PreviewHandle slice_quads(SliceQuadOptions options = {}) const;
  PreviewHandle thicken(ThickenOptions options = {}) const;
  OperationReceipt extrude(ExtrudeOptions options = {}) const;
  OperationReceipt inset(InsetOptions options = {}) const;
  OperationReceipt delete_faces() const;
};
```

Selection examples:

```cpp
auto mesh = quader.create_box({.name = "Trim"});

auto edges = mesh.edges().all();
edges.bevel({.width = 0.15F}).commit("Bevel Trim");

auto faces = mesh.faces().only({front, back});
faces.replace();
faces.thicken({.thickness = 0.2F}).commit("Thicken Faces");
```

This keeps the expressive user grammar while avoiding repeated selection boilerplate.

Selection implementation rules:

- public selection objects are cheap handles over SDK state plus captured element IDs;
- `Selection` provides the shared contract once, but operations remain on typed subclasses;
- callers can pass typed selections by value;
- no public API exposes raw owning pointers to selections;
- if runtime polymorphism is used internally, public destruction and copying must stay boring and
  safe;
- wrong-kind operations fail at the checked/kernel layer, but the fluent API should make those
  mistakes uncommon by putting verbs on typed subclasses.

## Selection API

```cpp
class SelectionApi {
public:
  SelectionEdit default_edit() const;
  OperationReceipt set_default_edit(SelectionEdit edit);

  OperationReceipt clear();
  OperationReceipt select_all();
  OperationReceipt invert();

  ObjectSelection objects(std::span<const ObjectId> objects,
                          SelectionEdit edit = SelectionEdit::Replace);
  VertexSelection vertices(ObjectId object,
                           std::span<const VertexId> vertices,
                           SelectionEdit edit = SelectionEdit::Replace);
  EdgeSelection edges(ObjectId object,
                      std::span<const EdgeKey> edges,
                      SelectionEdit edit = SelectionEdit::Replace);
  FaceSelection faces(ObjectId object,
                      std::span<const FaceId> faces,
                      SelectionEdit edit = SelectionEdit::Replace);

  PickResult pick(PickRequest request);
  OperationReceipt pick_select(PickSelectRequest request);
  BoxSelectionBaseline capture_box_baseline();
  OperationReceipt preview_box(BoxSelectionRequest request);
  OperationReceipt commit_box(BoxSelectionRequest request);
  HoverResult preview_hover(HoverRequest request);
  OperationReceipt apply_hover(HoverTarget target);
  OperationReceipt clear_hover();

  SelectionSummary summary() const;
};
```

## Complete Operation Inventory

The fluent API must cover this inventory. This inventory mirrors current app-reachable commands,
tools, and public workflows; kernel-only non-box primitive factories and UV layout helpers are
intentionally excluded.

### Mesh Creation And Import

- `create_box`
- `add_document`

### Selection And Clipboard

- `select_object`
- `select_vertices`
- `select_edges`
- `select_faces`
- `select_all`
- `clear_selection`
- `invert_selection`
- `delete_selection`
- `copy_selection`
- `paste_selection`
- `repeat_last_action`

### Object/Mesh Operations

- `duplicate_meshes`
- `combine_meshes`
- `invert_mesh_normals`
- `recalculate_face_normals`
- `shade_smooth_mesh`
- `shade_flat_mesh`
- `create_outer_hull`
- `assign_material`
- `assign_material_slot`
- `mirror_selection_x`
- `mirror_selection_y`
- `mirror_selection_z`
- `flip_horizontal`
- `flip_vertical`

### Transform Operations

- `translate_selection`
- `transform_selection`
- `move_selection`
- `rotate_selection`
- `scale_selection`
- `begin_transform_preview`
- `update_transform_translate`
- `update_transform_matrix`
- `update_transform_extrude`
- `update_transform_inset`
- `commit_transform_preview`
- `cancel_transform_preview`
- `begin_transform_drag`
- `update_transform_drag`
- `commit_transform_drag`
- `cancel_transform_drag`

### Vertex Operations

- `snap_vertices_to_active`
- `merge_vertices_to_active`
- `merge_vertices_to_center`
- `merge_vertices_by_distance`
- `remove_doubles`
- `bevel_vertices`
- `connect_vertices`
- `dissolve_vertices`
- `radial_align_vertices`

### Edge Operations

- `snap_edges_to_active`
- `connect_edges`
- `split_edges`
- `harden_edge_normals`
- `soften_edge_normals`
- `bevel_edges`
- `bridge_edges`
- `interpolated_bridge_edges`
- `bridge_edge_pairs`
- `bridge_edge_boundaries`
- `dissolve_edges`
- `merge_edges`
- `collapse_edges`
- `fill_hole`
- `radial_align_edges`
- `insert_edge_loop`

### Face Operations

- `bridge_faces`
- `interpolated_bridge_faces`
- `flip_face_normals`
- `recalculate_face_normals`
- `shade_faces_smooth`
- `shade_faces_flat`
- `combine_faces`
- `collapse_faces`
- `radial_align_faces`
- `slice_quads`
- `thicken_faces`
- `extrude_faces`
- `inset_faces`
- `extract_faces`
- `detach_faces`
- `assign_face_material_slot`

### Cutting And Drawing Operations

- `plane_cut`
- `knife_segment`
- `knife_stroke`
- `cut_tool`
- `knife_tool`
- `poly_tool`
- `mirror_tool`

### Element Operations

- `extrude_elements`
- `inset_elements`
- `delete_selection`

### IO And Payload Operations

- `serialize_qdr`
- `deserialize_qdr`
- `serialize_single_document_as_qdr`
- `serialize_obj`
- `compile_mesh`
- `compile_mesh_if_changed`
- `authored_polygon`
- `semantic_overlay`
- `render_snapshot`
- `validate`

## Operations API

The root operations API exists for global/current-selection workflows, command palette parity, tests,
and scripting escape hatches. It is complete, but it is not the preferred C++ modeling grammar.
New examples should use object facets and typed selections first.

```cpp
class OperationsApi {
public:
  std::vector<CommandDescriptor> catalog() const;
  CommandDescriptor describe(std::string_view id) const;
  OperationReceipt execute(std::string_view id, OperationSettings settings = {});

  OperationReceipt delete_selection();
  MeshHandle duplicate_meshes(DuplicateOptions options = {});
  OperationReceipt combine_meshes();

  OperationReceipt translate_selection(Vec3 delta);
  OperationReceipt transform_selection(TransformOptions options);
  OperationReceipt rotate_selection(RotateOptions options);
  OperationReceipt scale_selection(ScaleOptions options);
  OperationReceipt extrude(ExtrudeOptions options);
  OperationReceipt inset(InsetOptions options);

  OperationReceipt mirror_selection(Axis axis);
  OperationReceipt flip_horizontal();
  OperationReceipt flip_vertical();

  OperationReceipt invert_mesh_normals();
  OperationReceipt shade_smooth_mesh();
  OperationReceipt shade_flat_mesh();
  PreviewHandle create_outer_hull(ThickenOptions options = {});

  OperationReceipt snap_vertices_to_active();
  OperationReceipt merge_vertices_to_active();
  OperationReceipt merge_vertices_to_center();
  PreviewHandle merge_by_distance(MergeByDistanceOptions options = {});
  OperationReceipt remove_doubles(MergeByDistanceOptions options = {});
  OperationReceipt bevel_vertices(BevelVerticesOptions options = {});
  OperationReceipt connect_vertices();
  OperationReceipt dissolve_vertices();
  OperationReceipt radial_align_vertices();

  OperationReceipt snap_edges_to_active();
  OperationReceipt connect_edges();
  OperationReceipt split_edges();
  OperationReceipt harden_edge_normals();
  OperationReceipt soften_edge_normals();
  PreviewHandle bevel_edges(EdgeBevelOptions options = {});
  OperationReceipt bridge_edges(BridgeOptions options = {});
  PreviewHandle interpolated_bridge_edges(BridgeOptions options = {});
  OperationReceipt dissolve_edges();
  OperationReceipt merge_edges();
  OperationReceipt collapse_edges();
  OperationReceipt fill_hole();
  OperationReceipt radial_align_edges();

  OperationReceipt bridge_faces(BridgeOptions options = {});
  PreviewHandle interpolated_bridge_faces(BridgeOptions options = {});
  OperationReceipt invert_faces();
  OperationReceipt combine_faces();
  OperationReceipt collapse_faces();
  OperationReceipt radial_align_faces();
  PreviewHandle slice_quad(SliceQuadOptions options = {});
  PreviewHandle thicken_faces(ThickenOptions options = {});
  OperationReceipt extract_faces();
  OperationReceipt detach_faces();
  OperationReceipt assign_material_slot_to_selection(std::uint32_t slot);

  OperationReceipt plane_cut(PlaneCutOptions options);
  OperationReceipt knife_segment(KnifeSegmentOptions options);
  OperationReceipt knife_stroke(KnifeStrokeOptions options);
  OperationReceipt insert_edge_loop(InsertEdgeLoopOptions options);
};
```

## Preview API

There is one active modeling operation preview at a time per session.

```cpp
class PreviewHandle {
public:
  PreviewId id() const;
  bool active() const;
  PreviewTransactionSummary summary() const;

  OperationReceipt update(BridgeOptions options);
  OperationReceipt update(EdgeBevelOptions options);
  OperationReceipt update(ThickenOptions options);
  OperationReceipt update(SliceQuadOptions options);
  OperationReceipt update(MergeByDistanceOptions options);
  OperationReceipt update(TranslateOptions options);
  OperationReceipt update(TransformOptions options);

  ToolPreviewPayload payload(PayloadRequest request = {}) const;
  MeshPayload mesh_payload(PayloadRequest request = {}) const;
  SemanticOverlayPayload overlay_payload(PayloadRequest request = {}) const;

  OperationReceipt commit(std::string label = {});
  OperationReceipt cancel();
};
```

Preview lifecycle:

```cpp
auto preview = mesh.edges().selected().bevel({.width = 0.1F, .segments = 2});

preview.update(EdgeBevelOptions{.width = 0.25F, .segments = 3});
auto payload = preview.payload({.lifetime = SnapshotLifetime::BorrowedSameFrame});
preview.commit("Bevel Edges");
```

Preview invariants:

- begin/update can change preview payloads, but permanent document truth changes only on commit;
- cancel restores pre-preview state;
- commit creates one undoable operation;
- preview payload materialization is optional;
- preview outputs expose changed/reused status and cache keys.

## Tools API

Stable tool IDs:

```text
select
move
extend
extrude
rotate
scale
box
insert_edge_loop
knife
cut
poly
mirror
pivot
```

`pivot` is native-editor-owned until the SDK gains portable pivot semantics.

```cpp
class ToolsApi {
public:
  std::vector<ToolDescriptor> catalog() const;
  std::string active_tool() const;
  ToolStatus status() const;

  SelectTool activate_select_tool();
  TransformTool activate_move_tool();
  TransformTool activate_extend_tool();
  TransformTool activate_extrude_tool();
  TransformTool activate_rotate_tool();
  TransformTool activate_scale_tool();
  BoxTool activate_box_tool(BoxToolOptions options = {});
  InsertEdgeLoopTool activate_insert_edge_loop_tool(InsertEdgeLoopToolOptions options = {});
  KnifeTool activate_knife_tool(KnifeToolOptions options = {});
  CutTool activate_cut_tool(CutToolOptions options = {});
  PolyTool activate_poly_tool(PolyToolOptions options = {});
  MirrorTool activate_mirror_tool(MirrorToolOptions options = {});
  PivotTool activate_pivot_tool(PivotToolOptions options = {});

  OperationReceipt handle_frame(ViewportId viewport,
                                const ViewportInputSnapshot& input,
                                const ViewportCameraSnapshot& camera);
  OperationReceipt commit_active_tool_preview();
  OperationReceipt cancel_active_tool_preview();
  OperationReceipt cancel_modal_tool();
};
```

Root convenience:

```cpp
quader.activate_box_tool();
quader.activate_knife_tool();
quader.activate_cut_tool({.keep = CutKeepMode::KeepBoth});
```

Tool controller shape:

```cpp
class BoxTool {
public:
  BoxTool& drag_footprint(PointerEvent begin, PointerEvent update);
  BoxTool& drag_height(PointerEvent update);
  MeshHandle commit(std::string label = "Create Box");
  OperationReceipt cancel();
};

class KnifeTool {
public:
  KnifeTool& add_point(Ray ray);
  KnifeTool& add_segment(Ray from, Ray to);
  OperationReceipt commit(std::string label = "Knife");
  OperationReceipt cancel();
};

class CutTool {
public:
  CutTool& set_keep_mode(CutKeepMode mode);
  CutTool& set_plane(Vec3 a, Vec3 b, Vec3 c);
  OperationReceipt commit(std::string label = "Cut");
  OperationReceipt cancel();
};
```

## Viewport Input

The input layer converts raw UI/platform events into SDK-neutral snapshots.

```cpp
struct ViewportInputSnapshot {
  Vec2 pointer_position;
  ViewportRect viewport_rect;
  float wheel_delta = 0.0F;
  ViewportPointerButtons buttons;
  ViewportModifiers modifiers;
  bool focused = false;
  bool text_input_active = false;
  bool navigation_capture = false;
  std::vector<std::uint32_t> pressed_chords;
  double frame_time_seconds = 0.0;
};

struct ViewportCameraSnapshot {
  ViewportProjection projection = ViewportProjection::Perspective;
  Transform3 camera_transform;
  Vec2 viewport_size;
  double orthographic_size = 24.0;
  double fov_degrees = 60.0;
  MatrixConvention matrix_convention = MatrixConvention::ColumnMajor;
  std::array<float, 16> view_projection;
};
```

The input layer owns raw input and camera state. Quader owns semantic modeling/tool interpretation.

## Mesh Sync API

Callers must not poll full meshes every frame.

```cpp
struct ObjectMeshDelta {
  ObjectId object;
  std::string name;
  MeshPayload mesh;
  DirtyFlags dirty;
  bool deleted = false;
};

struct MeshSyncSnapshot {
  std::uint64_t content_revision = 0;
  std::vector<ObjectMeshDelta> objects;
};

class MeshSyncApi {
public:
  MeshSyncSnapshot changes_since(std::uint64_t previous_content_revision) const;
  MeshPayload mesh(ObjectId object, MeshCompileOptions options = {}) const;
  AuthoredPolygonPayload authored_polygon(ObjectId object) const;
};
```

Update loop:

```cpp
OperationReceipt frame = quader.tools().handle_frame(viewport, input, camera);
if (frame.dirty.geometry || frame.dirty.topology) {
  MeshSyncSnapshot changes = quader.mesh_sync().changes_since(last_revision);
  renderer.apply(changes);
  last_revision = changes.content_revision;
}
```

## Payload And Lifetime API

Every payload/view that can be retained must state its lifetime.

```cpp
enum class SnapshotLifetime {
  Owned,
  Pinned,
  BorrowedSameFrame,
};

struct PayloadRequest {
  SnapshotLifetime lifetime = SnapshotLifetime::Owned;
  bool only_if_changed = false;
  std::uint64_t previous_revision = 0;
};
```

Rules:

- `Owned`: copied vectors, safe to store, most expensive.
- `Pinned`: immutable shared storage, safe while token is retained.
- `BorrowedSameFrame`: valid only during current call/frame, cheapest, cannot escape.

Examples:

```cpp
auto mesh = object.compile_mesh({.lifetime = SnapshotLifetime::Owned});

quader.render().with_scene_snapshot({
  .lifetime = SnapshotLifetime::BorrowedSameFrame,
}, [&](RenderSceneView view) {
  renderer.sync(view);
});
```

## Render And Overlay API

```cpp
struct MaterialPayload {
  MaterialId id{};
  std::string name;
  Vec3 base_color{0.8F, 0.8F, 0.8F};
  float roughness = 0.7F;
  float metallic = 0.0F;
};

struct MeshPayload {
  std::vector<MeshVertexPayload> vertices;
  std::vector<std::uint32_t> indices;
  std::vector<MeshPrimitivePayload> primitives;
  std::uint64_t content_revision = 0;
};

enum class SemanticOverlayKind {
  Wire,
  VertexHandle,
  FaceHighlight,
  ToolPreview,
};

struct SemanticOverlayPayload {
  std::vector<SemanticOverlayRecord> records;
  std::uint64_t selection_revision = 0;
};

struct RenderSnapshotPayload {
  SnapshotLifetime lifetime = SnapshotLifetime::Owned;
  std::vector<MeshPayload> meshes;
  std::vector<MaterialPayload> materials;
  std::vector<SemanticOverlayPayload> overlays;
  RevisionStamp revisions;
  bool reused_previous_records = false;
};
```

Render sync must be revision-gated before mesh compile/snapshot work:

```cpp
auto revisions = quader.revisions();
if (!render_cache.matches(revisions)) {
  auto snapshot = quader.render().scene_snapshot({
    .previous = render_cache.previous_snapshot(),
    .lifetime = SnapshotLifetime::BorrowedSameFrame,
  });
  renderer.sync(snapshot);
}
```

## IO API

```cpp
class IoApi {
public:
  std::string serialize_qdr() const;
  std::string serialize_qdr(ObjectId object) const;
  std::string serialize_qdr(std::span<const ObjectId> objects) const;

  MeshHandle deserialize_qdr_object(std::string_view text, std::string name = "Mesh");
  std::vector<MeshHandle> deserialize_qdr_document(std::string_view text);

  std::string serialize_obj(ObjectId object) const;
  std::string serialize_obj(std::span<const ObjectId> objects) const;
};
```

Portable IO must not open native file dialogs. Native file dialog and durable project writes remain
in native project/IO APIs.

## Command API

```cpp
struct CommandDescriptor {
  std::string id;
  std::string label;
  std::string category;
  bool enabled = true;
  std::string disabled_reason;
  bool starts_preview = false;
  bool commits_preview = false;
  bool rebuilds_mesh_on_success = true;
};

class CommandApi {
public:
  std::vector<CommandDescriptor> catalog() const;
  CommandDescriptor describe(std::string_view command_id) const;
  OperationReceipt execute(std::string_view command_id, OperationSettings settings = {});
};
```

Command IDs are stable strings for command palettes and scripting. Native typed command IDs remain
native app API details.

## Batch API

Batching is required for readable and efficient workflows.

```cpp
auto result = quader.batch("Create and bevel")
    .create_box({.name = "Body"}).as("body")
    .with_mesh("body", [](MeshHandle mesh) {
      mesh.edges().all().bevel({.width = 0.08F, .segments = 2}).commit();
    })
    .commit();
```

Batch requirements:

- collect intent before mutation where possible;
- validate dependencies;
- avoid per-step render sync and mesh compilation;
- produce one history transaction unless configured otherwise;
- expose one combined receipt plus per-step receipts;
- materialize snapshots only if requested.

## Native API Mirror

Native `QuaderEditorApi` should offer the same grammar where practical:

```cpp
auto mesh = editor.modeling().create_box({.name = "Room"});
mesh.faces().only({front_face}).thicken({.thickness = 0.25F}).commit("Thicken Room Face");
```

Native public types:

```cpp
class QuaderModelingApi;
class QuaderMeshHandle;
class QuaderModelingSelectionApi;
class QuaderModelingOperationsApi;
class QuaderPreviewHandle;
class QuaderToolsApi;
```

Native implementations route through existing owners:

- `QuaderApiMutationRunner`;
- `ModelingCommandExecutionService`;
- `ModelingCommandDispatcher`;
- `ModelingSessionService`;
- `EditorSelectionService`;
- `DocumentCommandService`;
- `EditorHistoryService`;
- `PivotToolService`;
- SDK `ModelingSession` and `PolygonDocument`.

## Current Kernel API To Preserve

The current portable API remains valid as a lower-level kernel surface. It may keep explicit
`Result<T>` returns because it is not the recommended fluent integration layer.

```cpp
ModelingSession session;
auto document = PolygonDocument::make_box({});
auto object = session.add_document(std::move(document).or_throw(), "Box");
session.select_object(object.or_throw());
session.translate_selection({1.0F, 0.0F, 0.0F});
```

This is the kernel layer. It is allowed to be lower-level. The fluent layer wraps it.

## API Examples

### Create And Bevel

```cpp
auto mesh = quader.create_box({
  .name = "Frame",
  .min = {-1.0F, -1.0F, -1.0F},
  .max = {1.0F, 1.0F, 1.0F},
});

auto preview = mesh.edges().all().bevel({.width = 0.1F, .segments = 2});
preview.update(EdgeBevelOptions{.width = 0.15F, .segments = 3});
preview.commit("Bevel Frame");
```

### Face Workflow

```cpp
auto mesh = quader.create_box({.name = "Wall"});
auto face = mesh.faces().only({front_face});

face.extrude({.offset = {0.0F, 0.0F, 1.0F}});
mesh.faces().selected().inset({
  .transform = inset_transform,
  .pivot = {0.0F, 0.0F, 0.0F},
});
mesh.faces().selected().thicken({.thickness = 0.2F}).commit("Thicken Wall");
```

### Vertex Workflow

```cpp
auto vertices = mesh.vertices().only({v0, v1, v2});
vertices.replace();
vertices.merge_to_center();
mesh.vertices().selected().bevel({.distance = 0.05F});
```

### Edge Workflow

```cpp
auto edges = mesh.edges().only({edge_a, edge_b});
edges.connect();
mesh.edges().selected().split();
mesh.edges().selected().fill_hole();
```

### Tool Workflow

```cpp
auto box = quader.activate_box_tool({.name = "Trim"});
box.drag_footprint(mouse_down, mouse_drag)
   .drag_height(height_drag);

MeshHandle mesh = box.commit("Create Trim");
```

### Viewport Frame Loop

```cpp
ViewportInputSnapshot input = input_from_event(event);
ViewportCameraSnapshot camera = camera_from_viewport(viewport);

auto frame = quader.tools().handle_frame(viewport.id(), input, camera);
if (frame.dirty.geometry || frame.dirty.topology) {
  MeshSyncSnapshot changes = quader.mesh_sync().changes_since(last_revision);
  renderer.apply(changes);
  last_revision = changes.content_revision;
}
```

### QDR And OBJ

```cpp
auto qdr = quader.io().serialize_qdr(mesh.id());
auto imported = quader.io().deserialize_qdr_object(qdr, "Imported");

auto obj = quader.io().serialize_obj(imported.id());
write_text("imported.obj", obj);
```

## Performance Rules

The fluent API must not hide:

- mesh compilation;
- full snapshot allocation;
- topology hashing;
- render upload;
- cache rebuilds;
- preview mesh regeneration;
- overlay rebuilds;
- history transaction boundaries;
- selection side effects;
- component ID remapping;
- stale handle failures;
- borrowed/pinned payload lifetime.

Cheap calls should be cheap:

```cpp
mesh.id();
mesh.summary();
mesh.revisions();
mesh.bounds_if_cached();
quader.selection().summary();
quader.revisions();
```

Expensive calls must be named:

```cpp
mesh.payloads().compile_mesh();
mesh.payloads().compile_mesh_if_changed(previous_revision);
quader.mesh_sync().changes_since(previous_revision);
quader.render().scene_snapshot();
```

## Testing And Guardrails

Required tests when implemented:

- fluent SDK public-header test using only `quader/modeling/modeling.hpp`;
- named method coverage for every operation in this document;
- tool controller tests for box, insert edge loop, knife, cut, mirror, poly, pivot;
- preview begin/update/commit/cancel tests;
- transform preview and drag tests;
- stale handle failure tests;
- selection remap tests after topology-changing operations;
- mesh sync delta/revision tests;
- public header guardrails banning native owner types from SDK headers;
- performance checks for idle large scene, orbit camera, dense hover, component drag, box preview,
  mirror preview, and overlay/wireframe rebuilds.

Required verification commands for implementation:

```powershell
cmake --preset vs2026
cmake --build --preset vs2026-debug --target quader_modeling_sdk_consumer_tests quader_tests quader_guardrail_tests
ctest --preset vs2026-debug --output-on-failure
cmake --build --preset vs2026-debug --target quader
```

## Implementation Plan

1. Add fluent SDK headers under `src/sdk/core/modeling/include/quader/modeling/fluent`.
2. Add SDK-only tests for the fluent API before native API routing.
3. Expand `ModelingSession` so every operation in this document has a portable method or command.
4. Add `MeshHandle`, the shared `Selection` contract, typed selections, `PreviewHandle`,
   `ToolsApi`, and `MeshSyncApi`.
5. Add native `src/api` wrappers that mirror the fluent grammar while routing through native owners.
6. Add neutral C++ examples that use only the public fluent modeling API.
7. Replace examples that teach raw `PolygonDocument` plumbing with fluent examples, while keeping
   raw SDK examples for kernel tests.

## Review Checklist

- Does the API read like modeling intent?
- Are there named methods for every current operation and tool?
- Does the design avoid a `ModelingWorld` abstraction?
- Are handles values, not raw pointers?
- Do selections use one shared OOP contract with typed subclasses for object, vertex, edge, and face?
- Are all mutations receipt-bearing and diagnostic-bearing?
- Are dirty flags, revisions, deltas, and remaps preserved?
- Are preview lifecycle rules explicit?
- Are expensive calls visibly named?
- Are snapshot lifetimes explicit?
- Can C++ callers include one umbrella header and link `quader::modeling_sdk`?
- Are native app internals absent from portable SDK headers?
- Are command string APIs present but not the primary C++ surface?
- Are source and binary packaging expectations clear?
