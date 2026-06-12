# AGENTS.md

This project is a standalone C++20 desktop modeling-suite shell. Treat it as an application architecture project from day 0, not as a quick graphics sample.

## Current Stack

- Language: C++20.
- Build: CMake presets plus vcpkg manifest mode.
- Windowing/input: SDL3.
- UI: Dear ImGui docking branch.
- Rendering bootstrap: Vulkan.
- Current render engine bootstrap: Google Filament SDK from `C:\Users\Drako\Desktop\_QUADER_NATIVE\thirdy-party\filament-v1.71.5-windows`.
- Logging: macro-based `quader::Logger` facade over spdlog.
- Tests: GoogleTest.
- Profiling: Tracy.

The app is currently Vulkan-only at runtime. CMake may still link Filament's OpenGL support libraries on Windows because Filament's static `backend.lib` contains OpenGL symbols even when the app creates a Vulkan engine.

## Filament Documentation

Offline Filament documentation is available at `C:\Users\Drako\Desktop\_QUADER_NATIVE\filament-docs`.

Before adding or changing Filament-related code, always consult the relevant offline Filament documentation first. Prefer those local docs over memory or web search for Filament API behavior, renderer setup, materials, lighting, cameras, render targets, and backend integration details.

## Build And Smoke Test

Prefer these commands:

```powershell
cmake --preset vs2026-x64
cmake --build --preset debug
$env:QUADER_AUTOCLOSE_MS = "2000"
.\out\build\vs2026-x64\Debug\quader_native.exe
ctest --test-dir .\out\build\vs2026-x64 -C Debug --output-on-failure
```

Always rebuild Debug unless the user explicitly asks for Release. Do not build or smoke-test Release by default for now.

## Before Starting New Work

The goals are:

- keep code understandable and maintainable;
- avoid duplicated UI logic;
- avoid god objects;
- avoid overengineered widget hierarchies;
- keep UI easy to restyle through centralized colors, spacing, padding, font sizes, and layout tokens;
- keep domain logic separate from UI rendering;
- use C++20 ownership, invariants, RAII, and type-safety deliberately.

Agents must follow these rules when creating, modifying, or refactoring code.

## No Half-Done Or Band-Aid Systems

Never implement half-done systems, shortcut architecture, temporary band-aids, placeholder workflows, or narrowly patched behavior unless the user explicitly asks for a temporary workaround.

When a problem exposes a missing system boundary, build or extend the real project-owned system boundary needed for the feature instead of hiding the issue behind local booleans, duplicated checks, one-off helper paths, or "good enough for now" logic. This is a MUST rule, not a preference.

If a complete solution is too large for the current turn, stop before coding the shortcut, explain the scope honestly, and record the required full-system work in `tasks.md`. Do not land partial infrastructure that future work would need to undo.

## Input Actions And Contexts

Do not read raw keys or mouse buttons directly inside editor tools, viewport navigation, gizmos, panels, or feature controllers when the intent is a command, shortcut, tool selection, camera movement, or other editor action. Raw device state belongs in `src/platform/Input.*`; editor behavior must go through the project-owned app-layer input action/context resolver.

The same physical key may intentionally mean different things in different contexts. For example, `W` selects the move tool in the editor-tools context, while `W` moves the camera forward only in the viewport-navigation context activated by RMB. Context activation, priority, text-input suppression, and capture rules must be centralized and test-covered. Never solve key conflicts with scattered local booleans or one-off checks.

---

# Pre-Implementation Design Questions for C++20 Code

Before implementing new C++20 code, especially in an object-oriented or SOLID-oriented codebase, answer these questions.

## Mandatory OOP And Pattern Discipline

Pay particular attention to OOP design, SOLID boundaries, ownership, and pattern fit before writing code. Do not treat this as optional style guidance.

Before implementing anything non-trivial, stop and do an OOP design pass. This is a required gate, not a nice-to-have. The implementation may begin only after the agent can explain why the chosen shape makes sense.

Before adding a class, inheritance hierarchy, virtual interface, service, manager, command, factory, strategy, adapter, or helper, explicitly decide:

- What responsibility does this abstraction own, in one sentence?
- What invariant or lifecycle does it enforce that a free function or value type would not?
- What concrete extension is expected, and is polymorphism justified now?
- Does this abstraction reduce coupling at a real boundary, or does it hide procedural code behind a class name?
- Who owns the data and resources, who mutates them, and who only observes them?
- Which project layer is allowed to know about this abstraction?
- What code will reuse this abstraction now or in an already-known next task?
- What would become duplicated, harder to test, or harder to replace if this abstraction is not introduced?

If these questions cannot be answered clearly, do not write code yet. Re-read the surrounding architecture, identify the responsibility boundary, and choose a simpler or better-shaped design.

Preferred OOP use:

- Classes own resources, lifecycle, invariants, persistent state, or boundary-facing behavior.
- Interfaces represent stable project-owned boundaries, not convenience wrappers around one call site.
- Polymorphic bases are used when multiple implementations share a lifecycle or contract that callers genuinely need to treat uniformly.
- Factories construct families of related objects when construction details or backend types should stay hidden.
- Strategy/policy objects are used when behavior varies independently from the object that owns the data.
- Composition is preferred over inheritance when behavior can be assembled from smaller owned parts.
- Value types are preferred for plain data and immutable snapshots.
- Free functions are preferred for stateless transformations with no ownership, lifecycle, or polymorphic contract.

Base class or interface rules:

- Add a base class only when callers need to treat multiple implementations uniformly through one stable contract.
- A base class must have a narrow responsibility, a virtual destructor, and no unrelated convenience methods.
- Do not add a base class merely because a future type might exist. Add it when at least one concrete extension point is known from the current task, roadmap, or explicit user request.
- If a current request names a family of future variants, design the common lower-level abstraction now instead of hardcoding the first variant. Example: if the app needs gizmos, traces, debug points, debug lines, boxes, spheres, and arrows, introduce a reusable debug/editor draw primitive layer rather than a one-off light-gizmo renderer.
- Keep implementation inheritance shallow. Prefer one interface plus concrete implementations over deep hierarchies.
- Respect Liskov substitution: every derived implementation must honor the same lifetime, ownership, side-effect, and performance expectations as the base.
- Avoid virtual dispatch in hot inner loops unless the boundary is coarse enough that the cost is irrelevant or measured acceptable.

Reusable abstraction rules:

- Abstract repeated responsibility, not repeated syntax.
- Introduce a reusable abstraction when it removes real duplication, owns a shared lifecycle, protects an invariant, or creates a stable backend boundary.
- Do not abstract tiny one-off code just to look architectural.
- If two call sites are similar but have different ownership, lifetime, or failure behavior, do not force them into the same abstraction.
- If a feature will obviously grow into a family, identify the lowest reusable primitive first, then layer specific features on top of it.
- Keep high-level feature objects declarative where possible. They should describe intent and data; low-level systems should perform backend-specific work.

Avoid weak OOP:

- Do not create broad `Manager`, `Service`, `Helper`, `Controller`, or `Utility` types unless the responsibility is precise and narrow.
- Do not add inheritance just because more types may exist later; name the concrete extension point and lifecycle first.
- Do not turn straightforward data transforms into classes with no ownership, invariants, or state.
- Do not put business logic into UI panels, renderer adapters, or glue objects just because they have convenient access.
- Do not make god objects responsible for creation, storage, mutation, rendering, selection, and UI at the same time.
- Do not let one feature-specific implementation become the hidden home for a general system.
- Do not mix data ownership, command handling, rendering, picking, and UI presentation inside one class.
- Do not duplicate domain state inside renderer or UI helper objects; derive transient presentation from the authoritative model.

For editor/render helper families such as gizmos, overlays, handles, debug primitives, or picking proxies, prefer a small shared abstraction only when it owns a common renderer lifecycle or reusable geometry/material contract. Keep editor-only render representations derived from domain/ECS state; do not duplicate domain data inside helper objects.

Renderer/editor helper rule:

- Domain/ECS data owns what exists.
- Snapshot/render data describes what should be visible.
- Debug draw or editor draw systems own low-level drawing primitives such as lines, points, arrows, boxes, spheres, circles, cylinders, and transient traces.
- Gizmos, handles, overlays, and helper visuals should produce or request low-level draw primitives; they should not each reinvent Filament buffers, materials, renderables, cleanup, or topology.
- Backend-specific draw code stays in backend-specific modules such as `src/filament`; project-owned contracts should use project-owned data types.
- Selection and picking must remain tied to real domain/ECS entity ids, not accidental render helper entities.
- Gizmos are expected to be pickable. Design the pick payload at the same time as the visual primitive. Do not add a visual gizmo first and leave picking as an unrelated later patch.
- Pickable helper visuals must carry project-owned owner ids and roles such as selected entity, hovered entity, transform handle axis, light gizmo, trace marker, or measurement handle. The selection result must resolve back to domain/ECS identity or an explicit editor-tool handle, never to a backend render entity.

Before coding, write down the chosen shape mentally or in the work update:

```text
Responsibility:
Owner:
Lifetime:
Extension point:
Why class/interface/free function:
What stays out:
```

If the design would sound vague in that format, tighten it before implementation.

## What responsibility should this code own?

Clarify the role of the code before choosing a class, pattern, abstraction, or file structure.

Ask:

- What is the single reason this code should change?
- Is this a real domain concept, an implementation detail, or orchestration logic?
- Does this behavior belong in an existing type, a new type, a free function, or a strategy/policy object?
- Am I mixing unrelated concerns such as business rules, I/O, persistence, formatting, threading, rendering, or object lifetime?
- Will this design remain easy to test without excessive mocking or fragile setup?
- Am I creating this abstraction because it clarifies responsibility, or because I am guessing it may be useful later?

Prefer small, focused units with clear responsibilities. Avoid creating broad `Manager`, `Service`, `Helper`, `Controller`, or `Utility` types unless their responsibility is precise and justified.

Do not start by asking:

> What class should I create?

Start by asking:

> What responsibility exists, and where should it live?

OOP is a tool, not the default answer to every design problem. Use classes when they model ownership, invariants, lifecycle, polymorphism, or meaningful state. Use free functions when behavior is stateless, local, or naturally expressed as an operation.

## What invariants, ownership rules, and failure modes must be explicit?

Use C++20's type system and RAII principles to make invalid states difficult or impossible.

Ask:

- Who owns each object or resource?
- Can this value be null, empty, invalid, moved-from, or partially initialized?
- Can invalid states be prevented by construction?
- Should this be represented as a value type, `std::unique_ptr`, `std::shared_ptr`, `std::optional`, `std::variant`, `std::span`, `std::string_view`, or another explicit abstraction?
- What happens on failure: exception, error return, retry, fallback, or fatal error?
- Are lifetime, ownership, and threading assumptions visible at the API boundary?
- Is the caller forced to use the API correctly, or does correctness depend on comments and discipline?

Prefer APIs that make correct usage natural and incorrect usage hard. Do not rely on comments, naming conventions, or caller discipline to maintain critical invariants.

## C++20 guiding principle

Good C++20 design should answer these two questions clearly:

> What is the true responsibility of this code?

> What invariants and ownership rules should the design enforce before runtime?

If these questions cannot be answered simply, pause and refine the design before implementing.

Before starting any new task, automatically make a pre-emptive global architecture and code-health check across the whole repository, not only around the area being changed. Identify code smells, tight coupling, unclear ownership, overgrown classes, misplaced responsibilities, non-modular code, and anything that does not follow OOP/SOLID, modularity, or scalability expectations. After the global pass, call out which findings are directly relevant to the current task and which are broader backlog-level architecture debt.

Use subagents as part of this pre-emptive global health check. For non-trivial work, boot 1 to 6 subagents before implementation begins; use more agents within that range when the repository is large, cross-cutting, or architecturally risky. Each booted subagent should participate in the overall health check by inspecting a clear repository slice and reporting code smells, risky coupling, ownership issues, misplaced responsibilities, missing abstractions, and concrete architecture refactors that should be done before or alongside the requested task.

When opening subagents, always pass the explicit task context in the subagent prompt instead of using full-history mode. Full-history subagent spawning is currently error-prone in this workspace; keep prompts self-contained with the repo path, relevant constraints, ownership boundaries, and expected output.

When using subagents for architecture, refactor, OOP design, code-health review, or implementation planning, always pass down the relevant architecture and best-practice rules explicitly in the subagent prompt. Do not assume the subagent has read, remembered, or will independently infer the project standards. At minimum, include the applicable constraints for:

- OOP/SOLID responsibility boundaries and pattern discipline.
- Ownership, lifecycle, invariants, RAII, and failure modes.
- Project layer boundaries and allowed dependency direction.
- UI rules when touching ImGui or panels.
- Renderer/Filament boundary rules when touching viewport, gizmos, overlays, debug draw, materials, lights, cameras, or render resources.
- ECS/domain-data ownership rules when touching scene data, selection, entities, or components.
- Performance expectations for hot paths and per-frame code.
- The concrete files/modules the subagent may inspect or edit, and what it must not change.

Every architecture/refactor subagent prompt must be self-contained enough that the subagent can make senior-level design decisions without relying on hidden chat context. If the prompt cannot state the relevant rules clearly, tighten the prompt before spawning the subagent.

When the architecture and code-health analysis is complete, and before implementation begins, report what the agents found. List global code smells, architecture debt, risky coupling, ownership issues, misplaced responsibilities, missing abstractions, and any focused refactors that should happen before or alongside the requested task. If nothing meaningful is found because the repository architecture is already healthy enough, explicitly say that the architecture is good for this task and no cleanup is needed this time.

This report is not a stopping point. If the analysis finds bad or smelly code that is directly related to the current task, continue into implementation in the same turn and fix it end to end. No exceptions! This step is not mandatory or delayable "until later time"; these reports MUST be fixed in place in the same worktree, without fear to pollute or adding complexity. Do not leave the user with only an analysis unless the user explicitly requested analysis-only work, the fix is outside the current task scope, or a concrete blocker makes implementation unsafe. When implementation is blocked or deferred, explain the blocker and record any required follow-up in `tasks.md`.

Do not repeat the architecture inspection in the same chat when the first pass has already been completed and nothing meaningful has changed in the relevant code since that pass. Also do not restart the architecture inspection solely because the conversation was automatically compacted; continue from the compacted summary and only inspect again if the code or task scope changed enough to invalidate the earlier findings.

Automatically refactor bad or smelly code on the go when it is directly in the path of the task and the refactor reduces risk or improves the design needed for the new work. Keep these refactors focused; do not wander into unrelated cleanup. When subagents were used for the health check, use the same agents for the focused refactor whenever practical: give them disjoint ownership, have them apply or recommend the cleanup they identified, wait for their final reports, and incorporate the results before moving on.

After the architecture health check and any required refactor are complete, actual implementation work should always be done with multiple agents to make progress faster. Split implementation across disjoint ownership boundaries whenever possible, give each implementation subagent the relevant context, intended architecture, ownership boundaries, and any constraints discovered during the health check, then wait until all implementation-planning subagents have reported before the task implementation starts. Use their input to keep the app modular, scalable, and aligned with OOP/SOLID principles.

When any subagent is spawned, always wait for it to reach a final completed status before finalizing the response or treating the work as complete. Do not leave spawned subagents running in the background, and close completed subagents once their result has been incorporated. Incorporate the completed subagent result into the plan, architecture decision, implementation, or final summary as appropriate.

## Temporary Workarounds And Tasks

If you implement something that is a workaround, temporary patch, temporary logic path, or intentionally incomplete code, write it down in `tasks.md` before finishing the task. The entry should explain what was done, why it is temporary, where it lives, and what needs to replace it.

Before starting new work, check `tasks.md` for pending items. If a pending item is now completable within the current task context, implement it first or alongside the task, then remove that completed item from `tasks.md`.

## Architecture

Keep the code organized around clear responsibilities:

- `src/main.cpp`: entry point only. It should import `quader.app`, create `quader::Application`, call `run()`, and handle fatal top-level errors.
- `src/app`: application orchestration. The app loop may coordinate systems, but it must not own low-level renderer, Vulkan, SDL, ImGui, or Filament details.
- `src/diagnostics`: logging, crash reporting, and profiling support code. Diagnostics may be globally reachable, but they still need narrow APIs and boring behavior.
- `src/platform`: platform/window/input adapters. SDL-specific code belongs here unless it is purely event dispatch in the app loop.
- `src/render`: graphics device, swapchain, frame rendering, and future renderer-facing abstractions.
- `src/ui`: ImGui setup and UI panels. UI code should consume application state and service interfaces, not raw renderer internals.
- `src/filament`: Filament-specific integration. Do not let Filament types leak upward into app/UI/platform code unless there is an explicit abstraction boundary.

Important direction of dependency:

```text
main -> app -> platform/render/ui/engine-facing abstractions
implementation adapters -> third-party APIs
```

Avoid dependency flow in the other direction. For example, a renderer adapter can know about Filament or Vulkan; `Application` should ideally know about renderer interfaces and high-level state, not Filament internals.

## Performance Is Paramount

This is a modeling and rendering app. Performance is a primary product requirement, not a polish pass.

Rules:

- Do not implement half-working or knowingly slow heavy-computation code with excuses like "we can optimize later." Build the right performance shape from the start.
- Any operation that can touch many vertices, edges, faces, triangles, planes, brushes, scene objects, draw calls, materials, resources, pixels, files, or frames must be designed as a hot path until proven otherwise.
- Choose algorithms and data structures deliberately. Prefer spatial indices, broad-phase filtering, cache-friendly layouts, batching, dirty-region updates, incremental rebuilds, pooling, and parallel/GPU work when the workload calls for them.
- Avoid per-frame and per-element heap allocation, string formatting, virtual dispatch chains, logging, map lookups, synchronization, and temporary container churn in hot paths.
- Keep abstractions performance-aware. Interfaces are good, but they must not hide accidental allocation, unnecessary copies, blocking calls, or per-item polymorphic overhead in critical loops.
- Make data ownership and lifetimes explicit so large buffers, meshes, materials, acceleration structures, and GPU resources can be reused instead of rebuilt blindly.
- Measure with Tracy when performance matters. Add boundaries before or during implementation, not after the code becomes mysterious.
- Add performance-sensitive verification hooks such as Tracy zones as needed, but do not run Release-profile verification unless the user explicitly asks for it.
- If a correct high-performance implementation needs more time, take the time. Do not land a naive version that will obviously collapse under real modeling workloads.
- When a temporary fallback is absolutely unavoidable, isolate it behind the same production-facing abstraction, document the limit clearly, and do not let it become the default path for heavy work.

## Diagnostics And Fatal Errors

No obscure crashes. No useless logs. Fatal failures must preserve the exact message that explains what failed.

Rules:

- `main.cpp` is the top-level exception boundary. It should initialize logging, run the app, catch `std::exception`, log the exact `what()` text, show a fatal error modal with the exact same text, and return failure.
- Do not swallow startup exceptions unless you convert them into a precise user-facing status and immediately validate that status.
- If a subsystem cannot initialize and the app cannot run correctly without it, throw or return a failure status with the exact reason. Do not log vague messages like "init failed" without the underlying API failure.
- Use `QUADER_LOG_TRACE`, `QUADER_LOG_DEBUG`, `QUADER_LOG_INFO`, `QUADER_LOG_WARN`, `QUADER_LOG_ERROR`, and `QUADER_LOG_CRITICAL`. These macros route through `quader::Logger`.
- Keep log messages useful: what failed, which subsystem failed, and the specific API/result when available.
- Never replace a precise exception message with a generic modal. The modal must help the developer or user know exactly what failed.
- Avoid logging noisy per-frame messages. Use Tracy zones for hot-path visibility instead.

## C++20 Modules

This project uses C++20 named modules now. Do not add new project-owned `.h` include chains for normal app code.

Rules:

- Public project boundaries should be `.ixx` module interfaces.
- Implementations should be `.cpp` module implementation units.
- Implementation files should begin with any required third-party/std includes in the global module fragment, then `module quader...;`, then `import quader...;`.
- Normal `.cpp` files that import modules should put their own `#include`s before `import` lines. This avoids MSVC STL/header redefinition issues.
- Macro-only headers are allowed for cross-cutting preprocessor APIs such as logging macros. Keep them tiny and backed by module-owned implementation.
- Keep third-party APIs behind module boundaries whenever practical.
- Prefer importing project modules over including project headers.
- If a third-party header cannot be cleanly modularized, hide it inside the narrowest implementation module possible.

Naming convention:

- Module names use the `quader.` prefix.
- Match source folders in module names, e.g. `quader.app`, `quader.platform.sdl_window`, `quader.render.vulkan_device`, `quader.ui.imgui_layer`.
- Render-engine abstractions live under `quader.render.*`.
- Concrete implementation modules include the implementation name, e.g. `quader.filament.render_engine`.

## Abstraction Rules

Design for replacement. Future versions may swap Filament for another renderer, replace SDL windowing on some platform, or add platform-specific backends. Do not couple core application concepts to a third-party implementation.

Use small interfaces or narrow facade classes when crossing important boundaries:

- Renderer engine boundary: expose concepts like `initialize_renderer`, `begin_frame`, `render_viewport`, `shutdown`, or `RendererStatus`, not raw Filament calls.
- Graphics API boundary: keep Vulkan types inside Vulkan implementation files or carefully named Vulkan adapter headers.
- Platform boundary: keep `SDL_Window`, SDL init/shutdown, and SDL-specific flags in the platform layer.
- UI boundary: ImGui code should live in UI layer objects and panels. Do not scatter `ImGui::` calls through renderer or core application code.

When adding a feature, ask: "If we replaced this dependency, what files would need to change?" If the answer is "half the app," introduce a better boundary before adding the feature.

Prefer dependency inversion for important systems:

- Higher-level code depends on stable project-owned abstractions.
- Lower-level adapters implement those abstractions with SDL3, Vulkan, Filament, ImGui, etc.
- Project-owned abstractions should use project-owned data types where practical.

Current renderer-engine abstraction:

- `quader.render.status` exports `RendererStatus`.
- `quader.render.engine` exports `IRenderEngine`.
- `quader.render.viewport_renderer` exports `IViewportRenderer`, the current viewport rendering boundary.
- `quader.render.viewport_types` exports project-owned viewport IDs, render requests, scene snapshots, frame payloads, and temporary readback types.
- `quader.render.engine_factory` creates the current viewport renderer implementation.
- `quader.filament.render_engine` implements `IViewportRenderer` using Filament behind the renderer boundary.
- `Application` and editor viewport orchestration consume `IViewportRenderer`; UI panels consume project-owned viewport texture/status data, not Filament objects.

## Renderer Gizmos And Debug Draw

Gizmos, debug draw, editor handles, selection outlines, traces, and picking proxies should enter the renderer as project-owned value data or batched command buffers, usually through `ViewportRenderRequest` / `ViewportRenderSubject` or a similarly narrow render-layer type.

Do not model each axis, line, handle, trace, or debug primitive as a heap-owned polymorphic object with its own draw/update lifecycle unless it owns a real shared GPU resource lifecycle. Prefer value commands such as line, point, arrow, box, sphere, circle, cylinder, or mesh-overlay requests consumed by a renderer-owned draw system.

Editor/app code may derive gizmo/debug command data from tool state, selection, and scene data, but it must not own Filament entities, materials, buffers, or renderables. Filament implementations own cached GPU resources and update them from stable IDs, revisions, dirty flags, spans, or batched arrays.

Keep picking and hit-testing project-owned. A visual helper must carry an explicit pick payload when it is clickable, including the real owner ECS/entity id and the helper role. Selection must return the real domain/ECS entity id or an explicit editor-tool handle, not a backend render entity. Avoid duplicating domain state inside visual helper objects.

When adding a new gizmo/debug visual, define both sides together:

```text
Visual primitive:
Owner domain id:
Pick role:
Hit proxy shape:
Priority/mask:
Backend resource owner:
```

If the visual representation and picking representation can drift, extract a shared project-owned primitive/proxy description first.

## No God Objects

Do not create or grow God objects.

Specific rules:

- `Application` coordinates; it does not implement subsystems.
- `VulkanDevice` owns Vulkan device/queue selection; it does not render UI panels.
- `VulkanSwapchain` owns swapchain/frame presentation; it does not know application state.
- `ImGuiLayer` owns ImGui backend lifetime; it does not decide business behavior.
- UI panel files render one panel or a tightly related group of panels.
- If a file starts approaching a few hundred lines, pause and look for a natural split.
- If a class has more than one reason to change, split it.

Prefer several boring, obvious classes over one clever central manager.

## Readability Style

Write code so a tired future agent can understand it quickly.

Use intention-revealing, almost-English names:

- Good: `choose_best_device_for_window`, `find_queue_that_can_draw_and_present`, `create_initial_swapchain`, `recreate_swapchain`, `draw_frame`, `render_bootstrap_panel`.
- Bad: `init`, `doStuff`, `handle`, `process`, `manager`, `context`, `data`, `tmp`, when the name hides the real purpose.

Wrap dense API ceremony behind readable helper functions. Vulkan, Filament, SDL, and ImGui all have noisy setup structures; do not leave that noise inline inside orchestration code.

Function shape:

- A function should usually do one thing at one level of abstraction.
- A high-level function should read like a sequence of intentions.
- Low-level API details belong in lower-level helpers with precise names.
- Prefer early returns for guard conditions.
- Avoid clever control flow.

Comments:

- Prefer clear names over comments.
- Add comments only for non-obvious lifetime, ownership, API quirks, or future architectural intent.
- Do not write comments that merely repeat the code.

## C++ Style

- Use RAII for ownership and cleanup.
- Delete copying for owning objects unless copying is explicitly correct.
- Keep headers small and stable.
- Put implementation details in `.cpp` files or anonymous namespaces.
- Avoid global mutable state.
- Prefer `const` and `[[nodiscard]]` when they clarify intent.
- Throw exceptions for startup/configuration failures that should abort boot cleanly.
- Keep third-party includes out of public headers when a forward declaration or private implementation can avoid coupling.

## Dependency Policy

Use vcpkg for external dependencies unless explicitly told otherwise.

Current vcpkg dependencies include SDL3, Dear ImGui docking, GoogleTest, spdlog, and Tracy.

Never reinvent the wheel when a battle-tested solution exists and its license allows commercial use in this app.

Before adding a dependency, verify:

- It solves a real problem better than local code.
- Its license allows use in a proprietary/non-free application.
- It does not force source or binary disclosure.
- It does not introduce GPL, AGPL, LGPL with unclear linking obligations, SSPL, BUSL, or source-available/copyleft restrictions without explicit approval.

Before writing custom implementations for geometry, mesh processing, triangulation, booleans, simplification, spatial indexing, BVHs, task systems, serialization, compression, image loading, asset import, shader reflection, profiling, or editor tooling, actively look for proven libraries first. Prefer established libraries with permissive commercial-friendly licenses such as MIT, BSD, Apache-2.0, or Zlib when they fit the need.

When a library is chosen, wrap it behind project-owned abstractions so we can replace it later without contaminating the whole app. When a library is rejected, record the practical reason: license, performance, maintenance health, platform support, API fit, or binary size.

## Renderer Direction

Today the app boots:

```text
SDL3 Vulkan window -> Vulkan device/swapchain -> ImGui Vulkan backend -> Filament Vulkan engine
```

Future renderer work should move toward project-owned renderer interfaces. Filament should become one implementation behind those interfaces, not the shape of the whole app.

Do not expose Filament objects directly to UI panels or app orchestration. Instead expose project concepts such as renderer status, viewport handles, scene handles, material handles, mesh handles, or command objects.

## Testing

Use GoogleTest for meaningful critical behavior only.

Rules:

- Write tests for business logic, startup validation, renderer contracts, scene/resource rules, serialization, asset import decisions, and other behavior where regressions would hurt.
- Do not spend time testing trivial getters, obvious constructors, UI layout paint code, or third-party library behavior.
- Prefer small focused tests around project-owned functions and contracts.
- When a bug is found in critical logic, add the narrowest regression test that would have caught it.
- Keep test names readable enough to tell the story without opening the implementation.

## Tracy Profiling

Use Tracy zones in critical hot paths from now on.

Rules:

- Add `ZoneScopedN` around frame loops, render passes, expensive scene updates, selection/picking, mesh generation, import/export, spatial queries, and cache rebuilds.
- Add `FrameMark` once per rendered frame.
- Do not add Tracy zones to cold startup ceremony unless it is slow enough to investigate.
- Prefer profiling boundaries over per-frame logs.
- Keep zone names stable and human-readable.

## Before Finishing Work

For changes that touch build, startup, platform, renderer, UI, or Filament:

- Build Debug.
- Run the executable with `QUADER_AUTOCLOSE_MS`.
- Run GoogleTest with `ctest --test-dir .\out\build\vs2026-x64 -C Debug --output-on-failure` when touching tested logic, diagnostics, build structure, startup validation, or renderer contracts.
- Do not build Release unless the user explicitly asks for it.
- For now, keep `imgui.ini` when it is generated; do not remove or cancel it. This preserves ImGui docking/layout state until the app has a proper user-settings path.
- Keep the final status concise and mention any test you could not run.

# 2. Dear ImGui Architecture Rules for Agentic AI

This project uses **Dear ImGui**. Preserve the immediate-mode style.

The goal is:

> UI code must be easy to extend, visually tweak, restyle, and refactor without creating god objects, duplicated widgets, scattered magic numbers, or a custom retained-mode UI framework.

Follow these rules strictly.

---

# 3. Preserve Immediate-Mode UI

Dear ImGui is an immediate-mode UI library. Do not build a retained-mode widget hierarchy on top of it unless explicitly requested and architecturally justified.

Use OOP only for coarse-grained UI concepts:

- panels;
- windows;
- dialogs;
- inspectors;
- tool views;
- complex workflows;
- persistent UI state containers.

Do **not** create classes for simple widgets such as:

- buttons;
- labels;
- checkboxes;
- sliders;
- combo boxes;
- text inputs;
- separators;
- individual table rows.

Prefer direct ImGui calls inside panels and small reusable helper functions for repeated UI groups.

Good:

```cpp
if (ui::primaryButton("Save", ctx.theme))
{
    ctx.commands.execute(SaveProjectCommand{});
}
```

Bad:

```cpp
class SaveButton : public ButtonBase
{
    // Avoid this unless there is an exceptional reason.
};
```

Core rule:

> Use OOP at the panel, dialog, and workflow boundary. Keep ordinary ImGui widgets immediate-mode.

---

# 4. Avoid God UI Objects

Do not put all UI work into one large object such as:

```text
EditorUi
MainUi
ApplicationUi
UiManager
GuiManager
```

These objects may coordinate UI, but they must not contain the implementation details of every panel.

Acceptable responsibilities for a top-level UI coordinator:

- draw the dockspace;
- draw the main menu;
- own/register panels;
- forward shared `UiContext`;
- draw global dialogs;
- draw global notifications;
- coordinate global UI state.

Unacceptable responsibilities:

- drawing the full inspector;
- drawing the full asset browser;
- drawing every property editor;
- directly mutating domain objects;
- performing file I/O;
- implementing unrelated panel details;
- containing hundreds or thousands of lines of widget code.

A top-level UI object should orchestrate, not implement every screen.

Good top-level shape:

```cpp
class EditorUi
{
public:
    void draw(UiContext& ctx)
    {
        drawDockspace(ctx);
        drawMainMenu(ctx);

        for (auto& panel : panels_)
        {
            if (panel->isOpen())
            {
                panel->draw(ctx);
            }
        }

        drawModalDialogs(ctx);
        drawNotifications(ctx);
    }

private:
    void drawDockspace(UiContext& ctx);
    void drawMainMenu(UiContext& ctx);
    void drawModalDialogs(UiContext& ctx);
    void drawNotifications(UiContext& ctx);

private:
    std::vector<std::unique_ptr<IPanel>> panels_;
};
```

---

# 5. Required UI Layer Shape

Prefer this general structure:

```text
ui/
    EditorUi.hpp
    EditorUi.cpp
    UiContext.hpp
    UiTheme.hpp
    UiTheme.cpp
    Panel.hpp
    ImGuiScopes.hpp

ui/panels/
    ScenePanel.hpp
    ScenePanel.cpp
    InspectorPanel.hpp
    InspectorPanel.cpp
    AssetBrowserPanel.hpp
    AssetBrowserPanel.cpp
    ConsolePanel.hpp
    ConsolePanel.cpp

ui/widgets/
    PropertyWidgets.hpp
    PropertyWidgets.cpp
    AssetWidgets.hpp
    AssetWidgets.cpp
    TableWidgets.hpp
    TableWidgets.cpp

ui/dialogs/
    ModalDialogs.hpp
    ModalDialogs.cpp
```

For feature-heavy areas, feature-based organization is also acceptable:

```text
features/assets/
    AssetBrowserPanel.hpp
    AssetBrowserPanel.cpp
    AssetWidgets.hpp
    AssetCommands.hpp

features/scene/
    ScenePanel.hpp
    ScenePanel.cpp
    SceneWidgets.hpp
    SceneCommands.hpp

features/inspector/
    InspectorPanel.hpp
    InspectorPanel.cpp
    PropertyWidgets.hpp
```

Do not create one file per tiny widget.

Avoid:

```text
Button.hpp
Checkbox.hpp
Slider.hpp
TextInput.hpp
Label.hpp
IconButton.hpp
```

These files usually indicate retained-mode thinking leaking into an immediate-mode UI codebase.

---

# 6. Panel Interface

Panels should be coarse-grained OOP units with clear ownership and responsibility.

Example:

```cpp
class IPanel
{
public:
    virtual ~IPanel() = default;

    virtual std::string_view id() const noexcept = 0;
    virtual std::string_view title() const noexcept = 0;

    virtual bool isOpen() const noexcept = 0;
    virtual void setOpen(bool open) = 0;

    virtual void draw(UiContext& ctx) = 0;
};
```

Example panel:

```cpp
class ScenePanel final : public IPanel
{
public:
    std::string_view id() const noexcept override { return "scene"; }
    std::string_view title() const noexcept override { return "Scene"; }

    bool isOpen() const noexcept override { return open_; }
    void setOpen(bool open) override { open_ = open; }

    void draw(UiContext& ctx) override
    {
        if (!open_)
        {
            return;
        }

        if (ImGui::Begin("Scene", &open_))
        {
            drawToolbar(ctx);
            drawEntityTree(ctx);
            drawContextMenu(ctx);
        }

        ImGui::End();
    }

private:
    void drawToolbar(UiContext& ctx);
    void drawEntityTree(UiContext& ctx);
    void drawContextMenu(UiContext& ctx);

private:
    bool open_ = true;
    std::string filter_;
    bool showHidden_ = false;
};
```

A panel should answer:

- What data does it display?
- What UI-only state does it own?
- What commands can it emit?
- What application services does it need?
- What should it not know about?

Example:

```text
InspectorPanel

Displays:
    Currently selected entity/component data.

Owns:
    Local filter text.
    Expanded/collapsed UI sections.
    Pending rename buffer.

Emits:
    RenameEntityCommand
    AddComponentCommand
    RemoveComponentCommand
    SetPropertyCommand

Depends on:
    SelectionModel
    IEntityQueries
    ICommandDispatcher

Should not know:
    Renderer internals.
    File system details.
    Serialization format.
    Scene mutation implementation.
```

---

# 7. All Visual Constants Must Come From a Theme/Style Layer

Do not hardcode colors, padding, spacing, rounding, font sizes, or repeated layout numbers directly inside panels.

Bad:

```cpp
ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 4.0f));
ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.4f, 0.9f, 1.0f));
ImGui::Button("Run");
ImGui::PopStyleColor();
ImGui::PopStyleVar();
```

Good:

```cpp
if (ui::primaryButton("Run", ctx.theme))
{
    ctx.commands.execute(RunCommand{});
}
```

Also good:

```cpp
ui::ScopedFramePadding padding{ctx.theme.spacing.buttonPadding};

if (ImGui::Button("Run"))
{
    ctx.commands.execute(RunCommand{});
}
```

All reusable style values should live in a centralized theme/style structure.

Example:

```cpp
struct UiSpacing
{
    float xs = 2.0f;
    float sm = 4.0f;
    float md = 8.0f;
    float lg = 12.0f;
    float xl = 16.0f;

    ImVec2 buttonPadding = {10.0f, 6.0f};
    ImVec2 inputPadding = {8.0f, 5.0f};
    ImVec2 panelPadding = {12.0f, 12.0f};

    float propertyRowGap = 6.0f;
    float sectionGap = 12.0f;
    float toolbarButtonGap = 4.0f;
};

struct UiRadii
{
    float small = 3.0f;
    float medium = 5.0f;
    float large = 8.0f;
};

struct UiColors
{
    ImVec4 primary;
    ImVec4 primaryHovered;
    ImVec4 primaryActive;

    ImVec4 danger;
    ImVec4 dangerHovered;
    ImVec4 dangerActive;

    ImVec4 panelBackground;
    ImVec4 textMuted;
    ImVec4 border;
};

struct UiTypography
{
    float smallFontSize = 12.0f;
    float normalFontSize = 14.0f;
    float headingFontSize = 18.0f;

    ImFont* normalFont = nullptr;
    ImFont* boldFont = nullptr;
    ImFont* monoFont = nullptr;
};

struct UiTheme
{
    UiSpacing spacing;
    UiRadii radii;
    UiColors colors;
    UiTypography typography;
};
```

When adding new visual styling, first check whether an existing theme token can be reused. Add a new token only if it represents a reusable visual concept.

Bad token names:

```cpp
magicPadding1
randomGap2
buttonThingOffset
blueColor3
```

Good token names:

```cpp
theme.spacing.propertyRowGap
theme.spacing.sectionGap
theme.spacing.panelPadding
theme.spacing.toolbarButtonGap
theme.colors.primary
theme.colors.danger
theme.colors.textMuted
```

---

# 8. Reusable UI Helpers: Small Functions, Not Huge Abstraction Layers

Repeated UI patterns should be extracted into helper functions.

Good examples:

```cpp
namespace ui
{
    bool primaryButton(std::string_view label, const UiTheme& theme);
    bool dangerButton(std::string_view label, const UiTheme& theme);
    bool iconButton(std::string_view icon, std::string_view tooltip, const UiTheme& theme);

    void sectionHeader(std::string_view label, const UiTheme& theme);
    void helpMarker(std::string_view text);

    bool searchBox(std::string& text, const UiTheme& theme);
    bool propertyRow(std::string_view label, const UiTheme& theme);
    bool propertyRow(std::string_view label, std::string_view tooltip, const UiTheme& theme);

    bool drawTransformEditor(Transform& transform, const UiTheme& theme);
    bool drawAssetPicker(AssetId& selected, IAssetQueries& assets, const UiTheme& theme);
}
```

Property rows and editable/inspectable panel fields should have explanatory hover tooltips.
The tooltip should say what the property does, what units or ranges matter, and any important
side effects or backend mapping. Prefer a shared helper such as `ui::helpMarker()` or a tooltip
argument on `ui::propertyRow()` so every panel behaves consistently.

When adding new panel properties, include tooltips from the start. When touching an existing panel
or widget group, retrofit missing tooltips for nearby properties in that panel instead of leaving a
mixed UI where some controls explain themselves and adjacent controls do not.

Editable value controls should also use the shared resettable-property pattern unless a panel has a
specific reason not to. A value control is any UI element that owns or edits a property: floats,
integers, colors, checkboxes, combo/select controls, text inputs, asset/reference pickers, and
similar fields. When its current value differs from its default, show a clear changed/default
indicator and a reset button or icon that restores the default.

Numeric value controls should prefer ImGui drag inputs such as `DragFloat` / `DragInt`, normally via
the shared helper, instead of finite slider tracks. Configure ImGui drags for unbounded dragging
(`v_min >= v_max`, e.g. `0, 0`) and then apply project/domain clamps after the edit when the value
has real minimum/maximum bounds. Do not pass the domain bounds directly to the ImGui drag unless
the control is intentionally finite.

Keep this as immediate-mode helper functions such as resettable float, checkbox, combo, text, color,
or picker helpers. Do not introduce retained widget classes for ordinary resettable inputs.

Avoid large abstract widget frameworks.

Bad examples:

```cpp
class Widget;
class ButtonWidget;
class CheckboxWidget;
class LabelWidget;
class WidgetRenderer;
class WidgetFactory;
class AbstractWidgetController;
class PanelFactoryFactory;
class AbstractDockableToolWindowController;
```

Do not replace ImGui with a custom retained-mode framework unless explicitly requested.

Use this escalation order:

```text
inline ImGui code
    ↓
private panel method
    ↓
free helper function
    ↓
small stateful helper object
    ↓
dedicated panel/dialog/workflow class
```

Only promote a helper to a class when it has meaningful persistent state, lifecycle, dependencies, or multiple coordinated operations.

---

# 9. Separate Domain State, UI State, and ImGui State

Always keep these separate.

## 9.1 Domain/Application State

This is the real application data.

Examples:

- project data;
- scene objects;
- assets;
- documents;
- renderer settings;
- simulation state;
- open document;
- selected object in the actual model.

Domain state must not depend on ImGui.

## 9.2 UI View State

This is state needed only for the UI.

Examples:

- panel open/closed state;
- filter text;
- selected table row;
- sort order;
- pending rename buffer;
- expanded/collapsed custom sections;
- active tab;
- temporary dialog inputs;
- column visibility.

This may live in a panel or in a dedicated UI state structure.

## 9.3 ImGui Internal State

This is state Dear ImGui already manages.

Examples:

- active item;
- hovered item;
- window position;
- docking state;
- tree node state;
- input focus state.

Do not duplicate ImGui state unless the application must persist it independently.

Rule:

> Do not store UI state just because you can. Store it only when the application needs to remember it independently of ImGui.

---

# 10. Panels Should Be Thin, Focused, and Command-Oriented

Each panel should have a clear responsibility.

A panel may:

- display data;
- own UI-only state;
- collect user input;
- dispatch commands;
- call query interfaces;
- call reusable UI helpers.

A panel must not:

- contain business rules;
- perform complex domain mutation directly;
- perform file I/O directly;
- know unrelated systems;
- contain unrelated panels;
- become a catch-all for UI logic.

Preferred pattern:

```cpp
class InspectorPanel final : public IPanel
{
public:
    std::string_view id() const noexcept override;
    std::string_view title() const noexcept override;

    bool isOpen() const noexcept override;
    void setOpen(bool open) override;

    void draw(UiContext& ctx) override;

private:
    void drawEntityHeader(UiContext& ctx, EntityId entity);
    void drawComponents(UiContext& ctx, EntityId entity);
    void drawAddComponentPopup(UiContext& ctx);

private:
    bool open_ = true;
    std::string componentFilter_;
    std::string pendingRename_;
};
```

Button/menu actions should usually dispatch commands:

```cpp
if (ImGui::MenuItem("Delete", "Del", false, ctx.selection.hasSelection()))
{
    ctx.commands.execute(DeleteSelectionCommand{
        .ids = ctx.selection.currentIds()
    });
}
```

Avoid:

```cpp
if (ImGui::Button("Delete"))
{
    // Do not perform complex deletion logic directly in the panel.
    // Do not update multiple subsystems manually here.
}
```

Panel responsibility:

```text
Present state.
Collect user intent.
Dispatch commands.
Own UI-only state.
```

Not panel responsibility:

```text
Own business rules.
Perform persistence.
Perform file I/O.
Manually coordinate multiple unrelated subsystems.
Know renderer/engine internals unnecessarily.
```

---

# 11. Use Commands for Meaningful UI Actions

Buttons and menu items should not contain business logic. They should translate user intent into application commands.

Good:

```cpp
if (ImGui::Button("Import Asset"))
{
    ctx.commands.execute(ImportAssetCommand{
        .path = selectedPath_
    });
}
```

Good:

```cpp
if (ImGui::MenuItem("Delete", "Del", false, ctx.selection.hasSelection()))
{
    ctx.commands.execute(DeleteSelectionCommand{
        .ids = ctx.selection.currentIds()
    });
}
```

Bad:

```cpp
if (ImGui::Button("Build"))
{
    // Directly validates project,
    // writes files,
    // updates project state,
    // starts build,
    // handles errors,
    // updates recent files,
    // mutates UI flags.
}
```

Good:

```cpp
if (ImGui::Button("Build"))
{
    ctx.commands.execute(BuildProjectCommand{});
}
```

The command/application layer should handle validation, mutation, I/O, error handling, and cross-system coordination.

---

# 12. Use `UiContext` for Shared UI Dependencies

Do not pass many unrelated services into every UI function individually.

Prefer a narrow shared context:

```cpp
struct UiContext
{
    UiTheme& theme;

    ICommandDispatcher& commands;
    IProjectQueries& projectQueries;
    IAssetQueries& assetQueries;

    SelectionModel& selection;
    NotificationSink& notifications;
    EditorSettings& editorSettings;
};
```

Rules:

- `UiContext` should expose narrow interfaces, not giant concrete subsystems.
- Do not turn `UiContext` into a service locator for the whole application.
- Add dependencies only when multiple UI pieces genuinely need them.
- Feature-specific dependencies should stay close to the feature/panel when possible.
- Domain/application code must not depend on `UiContext`.

---

# 13. Use RAII for ImGui Scope Safety

Use small RAII helpers for paired ImGui calls.

Recommended wrappers:

- `PushID` / `PopID`;
- `PushStyleVar` / `PopStyleVar`;
- `PushStyleColor` / `PopStyleColor`;
- `BeginDisabled` / `EndDisabled`;
- `Begin` / `End`;
- `BeginTable` / `EndTable`;
- `BeginPopup` / `EndPopup`;
- font scopes;
- color scopes;
- style scopes.

Example:

```cpp
class IdScope
{
public:
    explicit IdScope(int id)
    {
        ImGui::PushID(id);
    }

    explicit IdScope(const void* id)
    {
        ImGui::PushID(id);
    }

    explicit IdScope(const char* id)
    {
        ImGui::PushID(id);
    }

    ~IdScope()
    {
        ImGui::PopID();
    }

    IdScope(const IdScope&) = delete;
    IdScope& operator=(const IdScope&) = delete;
};
```

Example:

```cpp
class DisabledScope
{
public:
    explicit DisabledScope(bool disabled)
        : disabled_(disabled)
    {
        if (disabled_)
        {
            ImGui::BeginDisabled();
        }
    }

    ~DisabledScope()
    {
        if (disabled_)
        {
            ImGui::EndDisabled();
        }
    }

    DisabledScope(const DisabledScope&) = delete;
    DisabledScope& operator=(const DisabledScope&) = delete;

private:
    bool disabled_;
};
```

Example:

```cpp
class WindowScope
{
public:
    WindowScope(const char* title, bool* open = nullptr, ImGuiWindowFlags flags = 0)
        : visible_(ImGui::Begin(title, open, flags))
    {
    }

    ~WindowScope()
    {
        ImGui::End();
    }

    explicit operator bool() const noexcept
    {
        return visible_;
    }

private:
    bool visible_;
};
```

Example usage:

```cpp
void InspectorPanel::draw(UiContext& ctx)
{
    WindowScope window{"Inspector", &open_};

    if (!window)
    {
        return;
    }

    if (auto selected = ctx.selection.singleEntity())
    {
        IdScope id{selected->id().value()};

        drawNameEditor(ctx, *selected);
        drawTransformEditor(ctx, *selected);
        drawComponents(ctx, *selected);
    }
}
```

These helpers are encouraged because they reduce imbalance bugs without creating a heavyweight UI framework.

---

# 14. Styling and Layout Rules

Never scatter raw layout constants throughout UI code.

Avoid:

```cpp
ImGui::Dummy(ImVec2(0.0f, 7.0f));
ImGui::SameLine(0.0f, 13.0f);
ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 9.0f);
```

Prefer named helpers or theme values:

```cpp
ui::verticalSpacing(ctx.theme.spacing.md);
ui::sameLine(ctx.theme.spacing.lg);
ui::indent(ctx.theme.spacing.panelPadding.x);
```

Acceptable helper examples:

```cpp
namespace ui
{
    void verticalSpacing(float amount);
    void horizontalSpacing(float amount);
    void sameLine(float spacing);
    void sectionSpacing(const UiTheme& theme);
    void panelPadding(const UiTheme& theme);
}
```

When adding spacing, use semantic names where possible:

```cpp
theme.spacing.propertyRowGap
theme.spacing.sectionGap
theme.spacing.panelPadding
theme.spacing.toolbarButtonGap
```

Do not add meaningless tokens like:

```cpp
magicPadding1
randomGap2
buttonThingOffset
```

---

# 15. UI Functions Should Be Predictable

Reusable UI functions should make ownership and side effects obvious.

Good:

```cpp
bool drawSearchBox(std::string& text, const UiTheme& theme);
bool drawTransformEditor(Transform& transform, const UiTheme& theme);
AssetPickerResult drawAssetPicker(const AssetPickerState& state, IAssetQueries& assets, const UiTheme& theme);
```

Avoid hidden mutations of global state.

Avoid UI helpers that secretly:

- mutate domain objects through singletons;
- open unrelated dialogs;
- dispatch unrelated commands;
- change global theme state;
- access unrelated application systems.

A UI helper should either:

- draw something;
- return whether something changed;
- return a small action/result;
- dispatch a clearly related command.

---

# 16. Prefer Explicit UI Results for Complex Widgets

For complex reusable UI, return a result instead of performing hidden logic.

Example:

```cpp
struct AssetPickerResult
{
    bool changed = false;
    std::optional<AssetId> selectedAsset;
    bool requestedImport = false;
};

AssetPickerResult drawAssetPicker(
    AssetId current,
    IAssetQueries& assets,
    const UiTheme& theme
);
```

Then the panel decides what to do:

```cpp
const auto result = ui::drawAssetPicker(currentAsset, ctx.assetQueries, ctx.theme);

if (result.selectedAsset)
{
    ctx.commands.execute(SetMaterialAssetCommand{
        .asset = *result.selectedAsset
    });
}

if (result.requestedImport)
{
    ctx.commands.execute(OpenImportAssetDialogCommand{});
}
```

This keeps reusable UI clean and avoids hidden coupling.

---

# 17. Before Adding UI Code, Search for Existing Patterns

Before implementing a new panel, widget group, button style, property row, table, or dialog:

1. Search the existing UI code.
2. Reuse the existing helper if one exists.
3. Extend the existing helper if the concept is the same.
4. Add a new helper only if the concept is genuinely new.
5. Do not duplicate similar UI code with small variations.

Duplicated UI is a bug unless there is a clear reason.

If adding a new visual style, check existing theme tokens first.

Bad:

```cpp
// In one file
ImVec4(0.2f, 0.4f, 0.9f, 1.0f)

// In another file
ImVec4(0.21f, 0.39f, 0.88f, 1.0f)

// In another file
ImVec4(0.18f, 0.42f, 0.95f, 1.0f)
```

Good:

```cpp
ctx.theme.colors.primary
ctx.theme.colors.primaryHovered
ctx.theme.colors.primaryActive
```

---

# 18. Refactor When a Panel Gets Too Large

A panel file should remain understandable.

When a panel grows too large, split by responsibility:

```cpp
void InspectorPanel::draw(UiContext& ctx)
{
    drawHeader(ctx);
    drawSearch(ctx);
    drawSelectedEntity(ctx);
    drawComponents(ctx);
    drawFooter(ctx);
}
```

If helper functions become reusable across panels, move them to `ui/widgets/`.

If helper functions are specific to one panel, keep them private in that panel's `.cpp` file.

Do not prematurely create new classes. Prefer this order:

```text
inline ImGui code
    ↓
private panel method
    ↓
free helper function
    ↓
small stateful helper object
    ↓
dedicated panel/dialog/workflow class
```

---

# 19. Docking, Layout, and Large-App Behavior

For editor-like applications, use a central dockspace and let panels be dockable windows.

Keep dockspace setup in one place, usually:

```cpp
EditorUi::drawDockspace(UiContext& ctx)
```

Panels should not know whether they are docked, floating, or tabbed. They should just draw themselves.

Good:

```cpp
void EditorUi::draw(UiContext& ctx)
{
    drawDockspace(ctx);
    drawMainMenu(ctx);
    drawPanels(ctx);
    drawGlobalDialogs(ctx);
}
```

Bad:

```cpp
void InspectorPanel::draw(UiContext& ctx)
{
    // Avoid making every panel manually know global docking policy,
    // global layout restoration, main viewport setup, etc.
}
```

---

# 20. File Count Strategy

Do not create a new file for every tiny widget or micro-abstraction.

Acceptable:

```text
ui/widgets/PropertyWidgets.hpp
ui/widgets/PropertyWidgets.cpp
ui/widgets/AssetWidgets.hpp
ui/widgets/AssetWidgets.cpp
ui/widgets/TableWidgets.hpp
ui/widgets/TableWidgets.cpp
```

Avoid:

```text
ui/widgets/Button.hpp
ui/widgets/Button.cpp
ui/widgets/Checkbox.hpp
ui/widgets/Checkbox.cpp
ui/widgets/Label.hpp
ui/widgets/Label.cpp
ui/widgets/Slider.hpp
ui/widgets/Slider.cpp
```

A new file is justified when there is a meaningful responsibility boundary, not merely because a new function was added.

---

# 21. Decision Rules

Use this table when deciding where new UI code belongs.

| Situation                            | Preferred Solution                  |
| ------------------------------------ | ----------------------------------- |
| One-off button or label              | Inline ImGui code                   |
| Repeated small visual pattern        | `ui/widgets` free function          |
| Repeated style variation             | Theme token + small helper          |
| Panel-specific repeated code         | Private panel method                |
| Cross-panel repeated property editor | Shared widget helper                |
| Dockable window/tool                 | `IPanel` implementation             |
| Modal with temporary state           | Dialog class or dialog state struct |
| Complex multi-step flow              | Dedicated workflow object           |
| Business action                      | Command dispatched from UI          |
| Domain data lookup                   | Query interface                     |
| App mutation                         | Application command handler         |
| Global layout setup                  | Top-level UI coordinator            |
| Theme color/spacing/font change      | `UiTheme` / style layer             |

---

# 22. Required Checklist Before Committing UI Changes

Before finishing any UI change, verify:

- No duplicated widget group already existed elsewhere.
- No new hardcoded colors were added outside the theme/style layer.
- No new hardcoded spacing, padding, rounding, or font-size constants were scattered in panels.
- No god object grew substantially.
- New UI logic is placed in the correct panel, dialog, helper, or feature folder.
- Domain/application code does not depend on ImGui.
- UI-only state is not mixed with domain state.
- Buttons/menu items dispatch commands for meaningful application actions.
- Reusable helpers are small, named clearly, and not over-abstracted.
- New abstractions reduce duplication or clarify responsibility.
- The number of new files is justified by real responsibility boundaries.
- Any new dependency added to `UiContext` is narrow and justified.
- Any new theme token represents a reusable visual concept.

---

# 23. Critical UI Instruction

When working on UI code, do not duplicate existing UI patterns and do not hardcode style values.

Before adding new UI code:

1. Inspect the existing `ui/`, `ui/widgets/`, `ui/panels/`, `ui/dialogs/`, and theme/style files.
2. Reuse existing panels, helpers, theme tokens, and layout utilities.
3. Add new helpers only for reusable patterns.
4. Add new theme tokens only for reusable visual concepts.
5. Keep panels focused and avoid growing god UI objects.
6. Keep business logic outside ImGui rendering code.

Any new color, spacing, padding, rounding, or font-size value must go through the centralized UI theme/style layer unless there is a strong local-only reason documented in the code.
