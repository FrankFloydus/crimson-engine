"""Hash-locked preparation for developer tools; never imports the application."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import platform
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import urllib.request

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
LOCK = json.loads((HERE / 'sources.lock.json').read_text())


def digest(path: Path, algorithm='sha512'):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, algorithm).hexdigest() if hasattr(hashlib, 'file_digest') else hashlib.new(algorithm, stream.read()).hexdigest()


def check_python():
    if sys.version_info[:2] != (3, 10) or struct.calcsize('P') != 8 or sys.platform != 'win32':
        raise RuntimeError('The locked developer environment requires Windows x64 CPython 3.10; it is independent of the application.')


def cache_source(record: dict, cache: Path, offline: bool):
    archive_dir = ROOT / 'build/vcpkg_downloads' if record['name'] == 'tracy' else cache / 'downloads'
    archive_dir.mkdir(parents=True, exist_ok=True)
    archive = archive_dir / record['archive']
    if not archive.exists():
        if offline:
            raise RuntimeError('Missing offline source archive: ' + str(archive))
        request = urllib.request.Request(record['url'], headers={'User-Agent': 'Crimson-Tracy-Preparation'})
        with urllib.request.urlopen(request, timeout=120) as response, tempfile.NamedTemporaryFile(dir=archive_dir, delete=False) as stream:
            temporary = Path(stream.name)
            shutil.copyfileobj(response, stream)
        if digest(temporary) != record['sha512']:
            raise RuntimeError('Source SHA512 mismatch: ' + str(temporary))
        temporary.replace(archive)
    if digest(archive) != record['sha512']:
        raise RuntimeError('Source SHA512 mismatch; refusing to replace existing evidence: ' + str(archive))
    source = cache / 'sources' / (record['name'] + '-' + record['version'])
    marker = source / '.crimson-source-pin.json'
    if source.exists():
        if not marker.is_file() or json.loads(marker.read_text()) != record:
            raise RuntimeError('Unrecognized source cache; choose a new cache directory: ' + str(source))
        return source
    source.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix='extract-', dir=source.parent))
    with tarfile.open(archive) as bundle:
        members = []
        for member in bundle.getmembers():
            target = (staging / member.name).resolve()
            if not target.is_relative_to(staging.resolve()):
                raise RuntimeError('Unsupported archive member: ' + member.name)
            # Upstream Zstd's CLI test aliases are Unix symlinks. The headless
            # Windows library build needs no symlinks or developer-mode rights.
            if not member.issym() and not member.islnk():
                members.append(member)
        bundle.extractall(staging, members=members)
    entries = list(staging.iterdir())
    if len(entries) != 1 or not entries[0].is_dir():
        raise RuntimeError('Expected one source archive root: ' + str(archive))
    entries[0].rename(source)
    staging.rmdir()
    marker.write_text(json.dumps(record, indent=2) + '\n')
    return source


def prepare(cache: Path, offline: bool):
    check_python()
    cache.mkdir(parents=True, exist_ok=True)
    sources = {item['name']: str(cache_source(item, cache, offline)) for item in LOCK['sources']}
    wheels = cache / 'wheels'
    wheels.mkdir(exist_ok=True)
    wheel_lock = json.loads((HERE / 'wheels.lock.json').read_text())['wheels']
    missing = [entry for entry in wheel_lock if not (wheels / entry['file']).exists()]
    if missing:
        if offline:
            raise RuntimeError('Missing offline wheels: ' + ', '.join(entry['file'] for entry in missing))
        subprocess.run([sys.executable, '-m', 'pip', 'download', '--only-binary=:all:', '--require-hashes',
                        '--dest', str(wheels), '-r', str(HERE / 'python-requirements.lock')], check=True)
    for entry in wheel_lock:
        if digest(wheels / entry['file'], 'sha256') != entry['sha256']:
            raise RuntimeError('Wheel SHA256 mismatch: ' + entry['file'])
    plan = dict(schema=1, tracy_version='0.14.1', python=sys.executable,
                sources=sources, wheelhouse=str(wheels), cache=str(cache))
    (cache / 'prepared.json').write_text(json.dumps(plan, indent=2) + '\n')
    print(json.dumps(plan, indent=2))


def finish(cache: Path, tools: Path, configuration: str, python: str):
    binding = list(tools.glob('TracyServerBindings*.pyd'))
    if len(binding) != 1:
        raise RuntimeError('Expected exactly one paired TracyServerBindings module.')
    files = [tools / 'tracy-capture.exe', tools / 'tracy-csvexport.exe', binding[0],
             tools / 'tracy/extra/mcp/tracy_mcp.py', tools / 'tracy/public/common/TracyProtocol.hpp',
             tools / 'tracy/extra/mcp/eval_guide.md', tools / 'tracy/profiler/src/llm/system.prompt.md',
             tools / 'tracy/patches/server-bindings-correctness.patch']
    for path in files:
        if not path.is_file():
            raise RuntimeError('Incomplete tool installation: ' + str(path))
    manifest = dict(schema=1, tracy_version='0.14.1', source_sha512=LOCK['sources'][0]['sha512'],
                    mcp_version=LOCK['mcp'], pybind11_version=LOCK['pybind11'],
                    python=python, base_python=str(Path(sys._base_executable).resolve()),
                    python_version=platform.python_version(), engine_configuration=configuration,
                    native_tool_configuration='Release', source_cache=str(cache / 'sources/tracy-0.14.1'),
                    analysis_binding_patch_sha256=digest(HERE / 'server-bindings-correctness.patch', 'sha256'),
                    server=str(tools / 'tracy/extra/mcp/tracy_mcp.py'),
                    preparation_inputs={str(path.relative_to(ROOT)): digest(path, 'sha256') for path in
                        [ROOT / 'scripts/Prepare-Tracy.ps1', HERE / 'Build-TracyTools.cmd', HERE / 'CMakeLists.txt',
                         HERE / 'prepare.py', HERE / 'sources.lock.json', HERE / 'wheels.lock.json', HERE / 'python-requirements.lock',
                         HERE / 'server-bindings-correctness.patch']},
                    files={str(path.relative_to(tools)): digest(path, 'sha256') for path in files})
    (tools / 'tracy-tools.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=['prepare', 'finish'])
    parser.add_argument('--cache', type=Path, default=ROOT / 'build/profiling')
    parser.add_argument('--offline', action='store_true')
    parser.add_argument('--tools', type=Path)
    parser.add_argument('--configuration', default='Release')
    parser.add_argument('--python', default=sys.executable)
    args = parser.parse_args()
    if args.action == 'prepare':
        prepare(args.cache.resolve(), args.offline)
    else:
        finish(args.cache.resolve(), args.tools.resolve(), args.configuration, args.python)
