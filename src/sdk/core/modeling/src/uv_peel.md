<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Guided UV Peel ownership

`uv_peel.cpp` owns guide ordering, target validation, storage reconstruction and publication.
Structured quad coordinates come from `uv_peel_structured.cpp`; non-quad-only guides retain
the conformal extension in `uv_peel_solver.cpp`. A guide with both quad and non-quad incident
faces still uses structured evaluation, including the lengths of segments without quad stores.

The structured helper owns an immutable face/edge index built once per target and reused by
its guides. It transports both endpoints across opposite quad edges and integrates their
world-space distances separately. Translation is excluded from the metric. Boundaries and
non-quads end a profile; returning to its directed starting occurrence closes it. Ambiguous
incidence, inconsistent winding, nonclosing cycles and exhausted budgets fail atomically.

UVs belong to authored face corners, including distinct occurrences at a seam. Endpoint spans
are normalized against the maximum transverse span for World Density, or a unit span for Fit.
Repeated corner assignments use explicit guide-segment, endpoint, side and occurrence priority.
This preserves the established cyclic seam values without relying on incidental map iteration.
The adapter retains inverse-sampling certification, stored-float reconstruction and exact
repeated-output no-op behavior. Preparation creates no persistent modeling truth.

The replacement helper was authored from a value interface, observable requirements and Quader
tests without consulting the preceding implementation, foreign editor source or binary research.
Integration retained the existing guide ordering, solver and publication machinery. This describes
the helper's engineering inputs, not legal clearance or independence of the entire UV subsystem.

`UvPeelInvarianceTests` establishes storage-order, corner-rotation, guide-order, seam and world-metric
invariants through the public SDK. Its fixtures passed before replacement. Existing
`ModelingSdkConsumerTests.UvPeel*`, `UvPeelSavedSceneTests`, `UvActionReliabilityTests` and native
`UvPeelToolServiceTests` retain numerical, failure, preview and history coverage.
