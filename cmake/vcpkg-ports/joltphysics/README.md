# Crimson Jolt overlay

This port is copied from `ports/joltphysics` at the project's pinned vcpkg builtin
baseline, `d0c191d142b7bc048141ddfb618aa9ae5dba0233`. It retains Jolt 5.5.0,
the original archive SHA512, feature flags, dependencies, build options and upstream
copyright installation. The manifest baseline and default registry are unchanged.

`character-stair-direction.patch` backports the `ExtendedUpdate` stair-direction
section from upstream commit
[`99b0af7959beb82e06beb7c008be3c5b17171d61`](https://github.com/jrouwe/JoltPhysics/commit/99b0af7959beb82e06beb7c008be3c5b17171d61)
([PR 2058](https://github.com/jrouwe/JoltPhysics/pull/2058)). That section incorporates
the preceding opposing-contact lookahead correction from
[`610fa792392db5dd8299bb448eb48ad56e94fc0d`](https://github.com/jrouwe/JoltPhysics/commit/610fa792392db5dd8299bb448eb48ad56e94fc0d)
and its normalization guard. The patch adapts the upstream `CharacterContact`
type name to 5.5.0's `Contact`. It also excludes virtual-character contacts from
the new direction-selection scan: Crimson's no-pushing character policy resolves
peer velocity separately, so a rear peer's requested velocity must not redirect
terrain probes. Characters still block all actual stair sweeps. The patch does
not backport unrelated changes to cast padding, penetration recovery or stair
progress checks from those commits.

Jolt 5.5.0 directs the actual stair sweep along the requested velocity and derives
its second floor probe from the ground normal. When a capsule slides obliquely
along a riser, those directions can leave it stuck even though the next tread is
reachable. The corrected native helper selects an active steep contact opposing
movement and sends both stair probes toward that riser. `PhysicsWorld` permits
the full forward half-plane so diagonal and nearly tangent approaches can step
without aligning the camera or actor heading to the riser. Pure parallel or
outgoing motion still fails the opposing-contact condition. Existing support,
profile step height, slope and capsule clearance checks remain in the native
helper; the FPS acceleration and accepted-momentum policy are unchanged.

The original vcpkg recipe is covered by `VCPKG-LICENSE.txt`. The backported Jolt
code is MIT licensed, copyright 2021 Jorrit Rouwe; see `JOLT-LICENSE.txt`.
`vcpkg_install_copyright` retains the complete upstream license in dependency
packaging.

Rebuild through the project build/configure wrapper. Do not edit extracted
dependency sources or installed binaries under `build/`.
