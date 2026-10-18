# Quader native reader

This is the paired Windows x64 Release ABI 7 reader, public C header, runtime
dependencies, licenses and provenance from Quader's native bridge distribution.
The engine loads the complete payload from `quader-reader` beside its executable.
It never locates a developer's Quader checkout at runtime.

`reader-provenance.json` identifies the native source revision and SHA-256 hashes.
The staged header and DLL must be replaced together. The current reader handles
project manifest versions 1 through 5, including the persistent texture catalog,
release requirements and deletion metadata written by Quader 0.1.67. It also handles
native scene schema 17 and older supported formats. ABI compatibility describes
the C function table, not saved-format compatibility: updating Quader's saved
formats can require a freshly built reader even when the ABI remains unchanged.
ABI 7 adds the authored hierarchy, names, sibling order, local visibility and
expanded instance-member parenting. The Crimson-owned reader implementation in
`source/` extends the authorized native reader without modifying Quader's checkout.

To refresh from the authorized Quader workspace, configure its existing CMake source
with a separate binary directory under Crimson's `build/quader-reader-current`,
reusing its configured vcpkg dependency cache with manifest installation disabled.
Then run `scripts/Build-QuaderReader.ps1 -QuaderRoot <native source>` from Crimson.
The wrapper configures the deferred `scripts/QuaderReaderOverride.cmake` source
override, builds only `quader_bridge_reader` in Release and stages the paired
payload. Keeping this binary directory separate preserves the external source
checkout and its build outputs.
The script collects the recursive Release dependency closure, paired header,
licenses, native commit, dirty-worktree diff, source-input fingerprints and the
Crimson-owned implementation/header hashes.
Its default build directory is the isolated directory above; `-BuildRoot` selects
another already configured reader build. CMake's `CC_QUADER_READER_ROOT` can select
an equivalent complete payload. Never downgrade or rewrite an authored project
manifest to accommodate a stale reader.

The CPU regression fixtures in `tests/fixtures/quader` were copied unchanged from
Quader's deterministic `godot-bridge-fixture-v2` export. They cover ordinary,
hidden and mirrored placements, project/embedded materials, all four texture
slots, UV rotation and a deliberately missing texture. Tests use disposable copies
when authoring completed-save records.

`tests/fixtures/quader/hierarchy` is generated through native SceneDocument and
QDR persistence owners by `scripts/quader_reader_hierarchy_fixture.cpp`. It covers
nested and empty groups, sibling order, a hidden group with locally visible
children, a nested direct light and two occurrences of a multi-mesh definition.
To reproduce it, build the isolated `crimson_quader_reader_fixture` Release target
and run it with a fresh output directory named `hierarchy`. The generator verifies
its saved scene through the current reader and prints the concrete hierarchy.
