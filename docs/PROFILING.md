# CPU and GPU profiling

Crimson integrates [Tracy 0.14.1](https://github.com/wolfpld/tracy/releases/tag/v0.14.1), its official capture/export tools and [official MCP server](https://github.com/wolfpld/tracy/tree/v0.14.1/extra/mcp). Saved `.tracy` files are the timing evidence; JSON, CSV and Markdown reports are derived views.

## Build and tools

Use `Build.cmd Release configure` after changing dependencies. The normal Release build includes instrumentation and prepares paired tools under `build/Release/bin/profiling`. First preparation requires CPython 3.10 x64 and internet access. Source archives, binary wheels and tool dependencies are locked by hash in `scripts/profiling`; subsequent builds reuse the verified local cache. An ordinary editor launch starts no compiler, Python environment, collector or connection handshake.

The canonical vcpkg overlays supply one static `Tracy::TracyClient` 0.14.1 shared by Engine, renderer and bgfx. The tools have one server implementation and no second client. The native `crimson-tracy-capture.exe` broker also has no client, so it cannot masquerade as the captured application.

```powershell
.\scripts\Prepare-Tracy.ps1 -Configuration Release -Build
# After sources and wheels have been cached:
.\scripts\Prepare-Tracy.ps1 -Configuration Release -Offline -Build
```

`CRIMSON_ENABLE_PROFILING=OFF` disables application profiling and capture controls. It does not remove the SDK from the shared instrumented bgfx dependency. Preserve the pinned baseline, overlays, native hooks and matching client/tool versions.

## Capture editor, startup and gameplay

```powershell
& '.\build\Release\bin\crimson-renderer.exe' --profile startup --profile-duration 15
& '.\build\Release\bin\crimson-renderer.exe' --profile editor --profile-duration 10
& '.\build\Release\bin\crimson-renderer.exe' --profile gameplay --profile-duration 10
```

An explicit startup recording waits at most five seconds for the official collector and this engine's client to connect before SDL, renderer and asset initialization. It covers initialization, model/material preparation and the first coherent rendered frames. Readiness combines production render-preparation results with resource queue/running counts. Eight further distinct submissions let delayed GPU timestamps arrive. Recording ends after that tail or the duration cap; the editor continues running. A successfully saved trace can have incomplete startup coverage, which the manifest reports separately.

For an interactive editor or viewport recording, choose **Profiling → Record editor / viewport** while editing. Play is not required. Wait for **Recording editor**, reproduce the slowdown by selecting, transforming or navigating as usual, then choose **Stop and save**. This captures the whole process, including editor/resource preparation, workers and all visible Scene/Game/preview GPU passes. The recording continues across starting or stopping Play, and ends only on an explicit stop or application shutdown. Captures use an `editor-...` folder and `capture_type: "editor"` in the manifest, with Scene/Game image dimensions and the Play state of the first and last observed frames.

For an interactive gameplay recording:

1. Start **Play**.
2. Choose **Profiling → Record gameplay** in the top menu bar, beside **File / Edit / View**. Press **Escape** first if Game has captured the mouse.
3. Wait for the visible status to change from **Connecting** to **Recording**. The recording indicator shows elapsed capture time while the menu is closed. The same status appears at the bottom-left of the Game image, including in a detached window, independently of the F1 metrics overlay.
4. Reproduce the workload for as long as needed, then click **Stop and save**. Leaving Play also stops and saves the recording.
5. Wait for **Saving** to finish. The saved result remains visible, and **Profiling → Copy trace path** copies the saved file's location. A failed connection or save produces a visible error.

Menu recordings run until stopped; they have no fixed ten-second cutoff. Connection time is excluded from the elapsed recording time, which stops advancing while the trace saves. Stopping Play saves a gameplay recording; closing the application requests a graceful save for every recording kind.

The CLI's `--profile gameplay` mode arms a timed recording of the next actual PlaySession. `--profile editor` begins a timed editor recording after initialization without requiring Play. Neither mode starts Play, supplies movement or changes input ownership. The same visible capture status and **Stop and save** control apply after the editor opens.

`--profile-duration` accepts 1–3600 seconds; CLI defaults are 15 seconds for startup and 10 for editor/gameplay. This option limits CLI recordings and does not impose a duration on later manual menu recordings. `--profile-output DIR` changes this process's capture root, including later menu captures. An isolated `--project-root DIR` keeps project/cache/session writes in that project. Cold-cache experiments require disposable snapshots: remove only their proven cache subtree, never authored Content or all of `.crimson`.

## Saved evidence

The default root is `<project>/captures/profiling`. Each request receives a unique UTC/process/sequence directory:

- `capture.tracy`: native official trace, readable by the matching 0.14.1 viewer/server.
- `manifest.json`: schema, state/completeness/errors, UTC times, engine/renderer commits and dirty flags, configured source/renderer hashes, backend/GPU identity, requested/latest metadata, frame identifiers and dimensions, effect/scene settings and resource/compiler counters where available.
- `collector.log`: official collector output and broker errors.

Shutdown sends SIGINT inside the helper's private hidden console. The engine never attaches to a user's console. Bounded failures terminate only the contained collector job and mark evidence incomplete. A nonempty file alone does not establish success: collector exit and official save confirmation are required. Existing captures are never overwritten.

The manifest describes recording inputs and coverage, not a replay of unsaved scene edits. Missing metadata remains unavailable. Source hashes describe the configured build; use the build wrapper's `configure` argument when publishing a new source/pin identity.

## Analyze saved traces

```powershell
.\scripts\Analyze-Profile.ps1 -Capture 'C:\path\to\capture.tracy' `
    -MetadataPath 'C:\path\to\manifest.json' -ExportEvents
```

This opens the file through official `TracyServerBindings`, waits for background statistics, and writes `summary.json`, `analysis.md`, CPU/GPU CSV summaries and optional official CSV exports beside the trace. `-OutputDirectory DIR` selects another location; `-Top N` controls zone rows. `-SkipFrames N` applies only to frame-time distributions, not whole-capture zone totals. `-ArchiveDirectory DIR` preserves the raw trace, supplied metadata and reports in a hash-indexed archive, refusing to overwrite different evidence.

Frame statistics include count, mean, p50, p95 and p99. Schema 2 retains raw frame counts and separately reports complete intervals between recorded frame marks. The pinned Worker adds three synthetic initialization/connection intervals and a final partial interval; these are excluded automatically before applying `-SkipFrames`. A long preconnection interval must never be reported as a gameplay frame. Zone totals still cover the complete capture. CPU tracked-event and GPU timestamp-started counts can exceed positive completed event counts; zero-duration and boundary events are not timed samples.

Zone rows retain count, inclusive total, minimum, maximum and mean. Parent/child zones, workers and GPU contexts overlap. Do not add their inclusive totals to claim a frame time; use source locations, self/child statistics and per-frame occurrences.

## Official MCP and future AI analysis

```powershell
.\scripts\Start-TracyMcp.ps1 -Configuration Release -Background
```

The upstream server binds to `http://127.0.0.1:47380/mcp`. Repository `.codex/config.toml` registers `crimson_tracy` for trusted projects; a fresh/reloaded MCP session loads it. The server must be running. Upstream 0.14.1 supports HTTP/SSE, not stdio. The launcher reports its process ID and logs, reuses its existing owned listener and rejects unknown port owners. `-CapturesDirectory DIR` selects another archive; the default matches `captures/profiling`. `-PrintConfiguration` prints endpoint settings without starting anything.

Upstream `list_captures` is non-recursive. Crimson keeps unique session directories, so discover files with `Get-ChildItem captures/profiling -Recurse -Filter *.tracy` and pass an absolute path to `load_capture`; an empty root listing does not mean no recordings exist.

Read `tracy://prompt` and `tracy://eval-guide` first. Use `load_capture` with a unique alias, then `eval` against that instance. Wait for `ctx.is_background_done()` before aggregates. Useful official Worker API queries include:

```python
ctx.get_frame_count()
ctx.get_frame_times()
ctx.get_root_zone_stats()
ctx.get_all_zone_stats()
ctx.get_all_gpu_zone_stats()
ctx.get_gpu_contexts()
ctx.get_threads()
```

Worker times are nanoseconds. The eval guide documents source-location, frame, child/self, duration, plot and message queries. Always `unload_capture` when finished and retain the raw trace/manifest with conclusions. When this chat has no newly loaded connector, the analysis script can exercise the actual MCP endpoint:

```powershell
.\scripts\Analyze-Profile.ps1 -Capture 'C:\path\to\capture.tracy' `
    -MetadataPath 'C:\path\to\manifest.json' -McpUrl 'http://127.0.0.1:47380/mcp'
```

The maintained [server binding patch](../scripts/profiling/server-bindings-correctness.patch) corrects two upstream 0.14.1 saved-file defects: GPU child traversal must handle inline `is_magic()` vectors, and CPU occurrence thread IDs must use the Worker's compressed-thread decoder. The equivalent canonical GUI/Worker code supplies the reference behavior. Preparation applies these corrections only to a generated binding source copy, hashes the patch in the tool manifest and leaves the verified upstream source archive unchanged. Native client/protocol/version remain 0.14.1. Both corrected methods are verified against saved application traces through direct bindings and the official MCP server.

## GPU semantics and overhead

Vertex painting has separate CPU zones for `Vertex paint tool update`, surface and target queries, source validation, candidate projection, seam preparation, hover visibility preflight, ordered dab batches, brush application, scene publication, history commit/cancel and the pre-UI release boundary. Subdivision, resampling and surface BVH construction have worker zones. `Vertex paint prepared geometry and weights` also covers painted-model preparation used by rendering independently of tool activation. Start with these parent stages, then inspect their children; their inclusive totals overlap.

Connected recordings include `Vertex paint active`, pending GPU requests, queued/applied dabs, projected candidates, CPU rays, GPU visibility checks and changed-vertex counters. These counters describe each tool update, including a possible release update before panels are drawn. GPU request/accept CPU scopes measure host preparation and result handling, not GPU execution or the full asynchronous latency. Existing native GPU scopes retain that distinction. Ordinary frames without a connected recording do not collect the per-vertex diagnostic counts.

`Vertex paint candidate preparations` is cumulative and should stay unchanged when only occluders animate around an unchanged selected mesh and camera. Visibility still revalidates. `Vertex paint GPU stream update` contains CPU `Vertex paint upload planning`, exact changed-control detection and `Vertex paint GPU range enqueue`. Per-update counters expose changed controls, changed cells, upload range count and bytes. Storage-page replacement alone is not a changed-control count. The CPU shadow retains unchanged values between coalesced ranges, with at most four updates per draw. These zones measure CPU planning/submission; backend command processing and native GPU timestamps remain separate. `Thumbnail descriptor validation`, `Thumbnail descriptor publication` and `Thumbnail source path normalization` distinguish readiness polling from source descriptor work.

`Material graph cache rebuild` measures validation and source expansion only when an immutable graph or its active material dependencies change. `Decal spatial preparation` measures receiver discovery and seed projection on a spatial-cache miss; unchanged chart/material readiness retries should no longer repeat that scope. `External scene preparation` consumes worker fingerprints rather than rereading saved scene bytes each frame.

The maintained bgfx overlay uses official D3D11/D3D12 Tracy contexts. GPU zones enclose native view/pass execution and compute dispatches. CPU `bgfx::submit` timing is never labeled as GPU execution. The separate asynchronous bgfx elapsed plot retains its source frame ID and suppresses duplicate samples.

Both query rings contain 8,192 timestamps. At most 1,024 zones are admitted per frame, with available-capacity checks and drop counters instead of ring-pressure waits. DX12 collection requires proof that the command list resolving those queries completed. DX11 polls do not flush. Context calibration can synchronize once during device initialization; ordinary tracing adds no Flush or blocking query wait. Disconnected tracing skips new GPU scopes. Profiling overhead is measurable; compare equivalent workloads with collector state recorded.

Frame marks cover the whole editor process, including visible Scene/Game, previews and platform windows. Native tracing here covers Engine DX11/DX12, without establishing Vulkan GPU support in another consumer. Shader/material interfaces, pass ordering, settings and threading ownership are unchanged.

## Focused checks

`profiling_capture_core` and `app_options_core` cover editor/gameplay mode parsing, idle behavior, durations, readiness/tail handling, missing/incompatible tools, failure manifests and retries. The CPU-only `profiling_capture_tests --live TOOLS OUTPUT` diagnostic records through the official collector and checks manual stop, reconnect, gameplay auto-save and editor recording across Play state changes. It creates no application window. The existing benchmark target also has an explicit hidden integration diagnostic:

```powershell
& '.\build\Release\bin\forward_plus_room_benchmark.exe' --tracy-project `
    'C:\path\to\project' 'C:\path\to\shaderc.exe' `
    'C:\path\to\staged\shaders' 'C:\path\to\staged\include' `
    'C:\path\to\capture-output' 'C:\path\to\bin\profiling'
```

It snapshots the project before opening stores, waits for resources, enters production PlaySession, renders Scene and Game and records a short gameplay trace. It excludes editor UI/physical input and is not a standalone gameplay performance claim. `CRIMSON_TEST_RENDERER=dx11` selects DX11; default is DX12. Run only for profiling integration or unresolved rendering concerns, with competing renderers closed. Query saved files for nonzero CPU and native GPU events before claiming capture support.
