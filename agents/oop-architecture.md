<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# OOP Architecture

Use these as decision rules, not slogans.

- Search for an existing owner before adding a new class, helper, method, cache, or conversion. If
  similar behavior already exists, merge into that owner instead of creating a parallel abstraction.
- Before adding a class, name its responsibility in one sentence. If the sentence needs "and",
  "misc", "helper", "manager", or "utility", redesign the boundary.
- Prefer high cohesion and low coupling over superficial class count.
- Assign behavior to the information expert: the class/service that already owns the data,
  invariants, policy, or lifetime needed to perform the behavior.
- Use `class` for invariant-bearing behavior and `struct` for passive data records.
- Keep invariant-bearing data `private`.
- Interfaces are contracts: explicit, strongly typed, small, and client-focused.
- Prefer return values over output parameters.
- Use references for required collaborators and pointers only when nullability is part of the
  contract.
- Prefer composition over implementation inheritance.
- Public inheritance means substitutability, not shared implementation.
- Use inheritance when there is a real substitutable family with shared behavior and a stable common
  contract. For example, if `BoxGizmo` and `SphereGizmo` are both gizmos with common lifecycle,
  picking, drawing, or interaction behavior, extract a focused `Gizmo` base and let the concrete
  gizmos inherit from it.
- During OOP audits, flag sibling classes that duplicate lifecycle, routing, state, or algorithms
  because they avoided an appropriate base abstraction. Prefer a base class only when callers can
  treat derived objects uniformly; otherwise keep composition as the default.
- Do not add virtual functions "just in case".
- Split broad interfaces.
- Use State or Strategy objects when behavior changes at runtime.
- Use Command objects for user-triggered operations with execution policy, undo/redo, history,
  preview, or routing semantics.
- Use facades only to simplify access to focused collaborators. A facade must not become the new
  large class.
- Controllers translate input and orchestrate. They do not own domain rules, renderer algorithms,
  long-lived resources, or SDK semantics.
- Services coordinate coherent capabilities. They are not dumping grounds.
- A service must not keep copied logic that belongs to a more focused projection, command, runtime
  map, cache, viewport, render, or document owner.
- Avoid duplicate source-of-truth state. If a class stores derived state, define the canonical owner,
  the sync direction, and the exact invalidation point.
- When two call sites need the same algorithm with different names, extract a typed helper in the
  owner layer instead of keeping two local variants.
- Prefer dependency injection over service locator.
- Avoid hidden globals and singletons.
- Separate commands from queries.
- Use RAII for every owned resource.
- Prefer the rule of zero.
- Keep headers self-contained and dependency-light.
- Put non-trivial algorithms and object definitions in `.cpp` files.
- A `.cpp` implementing a public header includes that header first.
- Do not add empty `.cpp` files, constructor-only `.cpp` files, pass-through subclasses, or giant
  inline catalogs.
- Add source files to `CMakeLists.txt` whenever a new `.cpp` is introduced.
- Add or update guardrail tests when changing architecture boundaries, command routing, catalogs,
  viewport tools, session services, render extraction, or document command ownership.
- Add or update guardrail tests when removing duplicated logic or preventing future drift between
  public API, UI, service, runtime, and low-level owner paths.
