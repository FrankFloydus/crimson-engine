# Quader native reader

This is the paired Windows x64 Release ABI 6 reader, public C header, runtime
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

To refresh from the authorized Quader workspace, configure its existing CMake source
with a separate binary directory under Crimson's `build/quader-reader-current`,
reusing its configured vcpkg dependency cache with manifest installation disabled.
Build only the native `quader_bridge_reader` target in Release. Keeping this binary
directory separate preserves the external source checkout and its build outputs.
Then run `scripts/Stage-QuaderReader.ps1 -QuaderRoot <native source>` from Crimson.
The script collects the recursive Release dependency closure, paired header,
licenses, native commit, dirty-worktree diff and source-input fingerprints.
Its default build directory is the isolated directory above; `-BuildRoot` selects
another already configured reader build. CMake's `CC_QUADER_READER_ROOT` can select
an equivalent complete payload. Never downgrade or rewrite an authored project
manifest to accommodate a stale reader.

The CPU regression fixtures in `tests/fixtures/quader` were copied unchanged from
Quader's deterministic `godot-bridge-fixture-v2` export. They cover ordinary,
hidden and mirrored placements, project/embedded materials, all four texture
slots, UV rotation and a deliberately missing texture. Tests use disposable copies
when authoring completed-save records.
