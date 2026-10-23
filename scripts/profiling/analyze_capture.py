"""Read saved captures through the pinned official Tracy server bindings/MCP."""
from __future__ import annotations
import argparse
import asyncio
import csv
import hashlib
import inspect
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time
import uuid


def worker_summary(ctx, top=40, skip_frames=0):
    def distribution(values):
        values = sorted(float(value) / 1e6 for value in values if value >= 0)
        def percentile(fraction):
            position = (len(values) - 1) * fraction
            low = int(position)
            return values[low] + (values[min(low + 1, len(values) - 1)] - values[low]) * (position - low)
        if not values:
            return {'count': 0}
        return dict(count=len(values), mean_ms=sum(values) / len(values), p50_ms=percentile(.5),
                    p95_ms=percentile(.95), p99_ms=percentile(.99), min_ms=values[0], max_ms=values[-1])
    def zones(stats):
        ordered = sorted(stats.items(), key=lambda item: item[1].total, reverse=True)
        return [dict(label=name, count=int(value.count), total_ns=int(value.total),
                     min_ns=int(value.min), max_ns=int(value.max), average_ns=float(value.avg))
                for name, value in ordered[:top]]
    gpu_contexts = [dict(name=context.name, type=context.type, count=int(context.count), thread=int(context.thread))
                    for context in ctx.get_gpu_contexts()]
    first_time, last_time = int(ctx.get_first_time()), int(ctx.get_last_time())
    boundaries = [(int(begin), int(end)) for begin, end in ctx.get_frame_boundaries()]
    # Pinned Tracy 0.14.1 inserts two synthetic initialization marks. Its
    # on-demand handshake adds a third mark at currentTime (GetFirstTime),
    # before the first real FrameMark. The final end is GetLastTime rather
    # than another recorded mark. Neither partial interval is a full frame.
    on_demand = first_time > 0
    synthetic_prefix = 3 if on_demand else 2
    exclusions = dict(initialization=0, preconnection=0, connection_partial=0,
                      trailing_partial=0, invalid_or_outside_capture=0)
    bounded = []
    for index, (begin, end) in enumerate(boundaries):
        if index < synthetic_prefix:
            reason = ('initialization', 'preconnection', 'connection_partial')[index] if on_demand else 'initialization'
            exclusions[reason] += 1
        elif index == len(boundaries) - 1:
            exclusions['trailing_partial'] += 1
        elif begin < first_time or end > last_time or end <= begin or end != boundaries[index + 1][0]:
            exclusions['invalid_or_outside_capture'] += 1
        else:
            bounded.append((begin, end))
    selected = bounded[skip_frames:]
    return dict(schema=2, capture_name=ctx.get_capture_name(), program=ctx.get_capture_program(),
                first_time_ns=first_time, last_time_ns=last_time,
                raw_frame_count=int(ctx.get_frame_count()), raw_frame_interval_count=len(boundaries),
                bounded_frame_count=len(bounded), frame_skip_requested=skip_frames,
                frame_skip_applied=min(skip_frames, len(bounded)),
                excluded_frame_interval_count=sum(exclusions.values()), frame_interval_exclusions=exclusions,
                frame_interval_policy='Only intervals between two recorded base FrameMarks are used. Tracy 0.14.1 synthetic initialization/on-demand marks and the trailing GetLastTime interval are excluded. Requested frame skipping applies after this automatic filtering.',
                frame_histogram_window_ns=dict(begin=selected[0][0], end=selected[-1][1]) if selected else None,
                frame_times=distribution([end - begin for begin, end in selected]),
                cpu_zone_event_count=int(ctx.get_zone_count()),
                gpu_zone_event_count=sum(context['count'] for context in gpu_contexts),
                cpu_zones=zones(ctx.get_all_zone_stats()), gpu_zones=zones(ctx.get_all_gpu_zone_stats()),
                gpu_contexts=gpu_contexts, threads=ctx.get_threads(),
                timing_scope='Worker times are nanoseconds. Official binding summaries are keyed by zone label; repeated labels at different source locations may replace each other. CPU/GPU zone totals retain the complete saved-capture scope, including recorded startup work, and are inclusive; parent/child and overlapping work must not be summed. Frame filtering and skipping affect only the frame histogram. GPU zones measure timestamp intervals, not hardware occupancy.')


def content_text(result):
    text = ''.join(item.text for item in result.content if getattr(item, 'type', None) == 'text')
    if getattr(result, 'is_error', getattr(result, 'isError', False)):
        raise RuntimeError(text)
    return text


async def summarize_mcp(capture: Path, url: str, top: int, skip: int, timeout: float):
    from mcp import ClientSession
    from mcp.client.streamable_http import streamable_http_client
    alias = 'crimson-analysis-' + uuid.uuid4().hex
    async with streamable_http_client(url) as streams:
        async with ClientSession(streams[0], streams[1]) as session:
            await session.initialize()
            await session.read_resource('tracy://prompt')
            await session.read_resource('tracy://eval-guide')
            loaded = False
            try:
                response = content_text(await session.call_tool('load_capture', {'path': str(capture), 'alias': alias}))
                if not response.startswith('Loaded as '):
                    raise RuntimeError(response)
                loaded = True
                deadline = time.monotonic() + timeout
                while True:
                    ready = content_text(await session.call_tool('eval', {'code': 'ctx.is_background_done()', 'instance_id': alias}))
                    if ready.strip() == 'True':
                        break
                    if time.monotonic() >= deadline:
                        raise TimeoutError('Capture statistics did not become ready.')
                    await asyncio.sleep(.1)
                code = 'import json\n' + inspect.getsource(worker_summary) + f'\nprint(json.dumps(worker_summary(ctx, {top}, {skip})))'
                response = content_text(await session.call_tool('eval', {'code': code, 'instance_id': alias}))
                result = json.loads(response)
                result['analysis_transport'] = url
                return result
            finally:
                if loaded:
                    await session.call_tool('unload_capture', {'instance_id': alias})


def summarize_direct(capture: Path, tools: Path, top: int, skip: int, timeout: float):
    sys.path.insert(0, str(tools))
    import TracyServerBindings as tracy
    source = tracy.open_file(str(capture))
    worker = tracy.create_worker_from_file(source)
    try:
        deadline = time.monotonic() + timeout
        while not worker.is_background_done():
            if time.monotonic() >= deadline:
                raise TimeoutError('Capture statistics did not become ready.')
            time.sleep(.05)
        result = worker_summary(worker, top, skip)
        result['analysis_transport'] = 'official TracyServerBindings (saved file)'
        return result
    finally:
        worker.shutdown()


def sha256(path: Path):
    with path.open('rb') as stream:
        digest = hashlib.sha256()
        while block := stream.read(1024 * 1024):
            digest.update(block)
        return digest.hexdigest()


def archive(capture: Path, destination: Path, metadata: Path | None, analysis: Path | None):
    destination.mkdir(parents=True, exist_ok=True)
    sources = [capture]
    if metadata:
        sources.append(metadata)
    if analysis:
        sources += [path for path in analysis.iterdir() if path.is_file() and path.suffix in ('.json', '.md', '.csv') and path.name != 'archive.json']
    records = []
    for source in sources:
        target = destination / source.name
        digest = sha256(source)
        if target.exists() and sha256(target) != digest:
            raise RuntimeError('Archive already contains different evidence: ' + str(target))
        if not target.exists():
            shutil.copy2(source, target)
        records.append(dict(file=target.name, bytes=target.stat().st_size, sha256=digest))
    manifest = dict(schema=1, original_capture=str(capture), tracy_version='0.14.1', files=records)
    (destination / 'archive.json').write_text(json.dumps(manifest, indent=2) + '\n')
    return manifest


def write_analysis(result: dict, output: Path):
    output.mkdir(parents=True, exist_ok=True)
    (output / 'summary.json').write_text(json.dumps(result, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    lines = [f"# Tracy profile: {result['capture_name']}", '',
             f"Program: {result['program']}", '', f"Capture: `{result['capture_path']}`", '',
             f"SHA256: `{result['capture_sha256']}`", '', result['timing_scope'], '']
    lines += [f"Recorded events: {result['cpu_zone_event_count']} CPU zones; {result['gpu_zone_event_count']} GPU zones across {len(result['gpu_contexts'])} contexts. Tables show at most the requested number of label summaries.", '']
    lines += [result['frame_interval_policy'], '',
              f"Raw frame entries: {result['raw_frame_count']}; automatically excluded intervals: {result['excluded_frame_interval_count']}; fully bounded intervals: {result['bounded_frame_count']}; requested/applied skip: {result['frame_skip_requested']}/{result['frame_skip_applied']}.", '',
              'Automatic exclusions: ' + ', '.join(f'{name}={count}' for name, count in result['frame_interval_exclusions'].items()) + '.', '']
    frames = result['frame_times']
    if frames['count']:
        lines += [f"Frames: {frames['count']}; mean {frames['mean_ms']:.3f} ms; p50 {frames['p50_ms']:.3f} ms; p95 {frames['p95_ms']:.3f} ms; p99 {frames['p99_ms']:.3f} ms.", '']
    else:
        lines += ['No complete frame timings were recorded after the requested skip.', '']
    for key, title in [('cpu_zones', 'CPU zones'), ('gpu_zones', 'GPU zones')]:
        rows = result[key]
        lines += [f'## {title}', '', '| Zone | Count | Inclusive total (ms) | Mean (ms) | Maximum (ms) |', '|---|---:|---:|---:|---:|']
        for row in rows:
            label = row['label'].replace('|', '\\|').replace('\n', ' ')
            lines.append(f"| {label} | {row['count']} | {row['total_ns']/1e6:.3f} | {row['average_ns']/1e6:.3f} | {row['max_ns']/1e6:.3f} |")
        if not rows:
            lines += ['| No recorded zones | 0 | 0 | 0 | 0 |']
        lines += ['']
        with (output / (key.replace('_', '-') + '.csv')).open('w', newline='', encoding='utf-8') as stream:
            writer = csv.DictWriter(stream, fieldnames=['label', 'count', 'total_ns', 'min_ns', 'max_ns', 'average_ns'])
            writer.writeheader()
            writer.writerows(rows)
    (output / 'analysis.md').write_text('\n'.join(lines), encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--tools', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--metadata', type=Path)
    parser.add_argument('--top', type=int, default=40)
    parser.add_argument('--skip-frames', type=int, default=0)
    parser.add_argument('--timeout', type=float, default=120)
    parser.add_argument('--mcp-url')
    parser.add_argument('--export-events', action='store_true')
    parser.add_argument('--archive', type=Path)
    parser.add_argument('--archive-only', action='store_true')
    parser.add_argument('--analysis-directory', type=Path)
    args = parser.parse_args()
    capture = args.capture.resolve(strict=True)
    if capture.suffix.lower() != '.tracy' or args.top < 1 or args.top > 10000 or args.skip_frames < 0:
        raise ValueError('Use a .tracy file, positive bounded --top and nonnegative --skip-frames.')
    if args.archive_only:
        if not args.archive:
            parser.error('--archive-only requires --archive.')
        print(json.dumps(archive(capture, args.archive.resolve(), args.metadata, args.analysis_directory), indent=2))
        return
    if not args.tools:
        parser.error('Capture analysis requires --tools.')
    tools = args.tools.resolve(strict=True)
    manifest = json.loads((tools / 'tracy-tools.json').read_text())
    if manifest['tracy_version'] != '0.14.1':
        raise RuntimeError('Expected paired Tracy0.14.1 tools.')
    output = args.output.resolve() if args.output else capture.parent / (capture.stem + '-analysis')
    result = asyncio.run(summarize_mcp(capture, args.mcp_url, args.top, args.skip_frames, args.timeout)) if args.mcp_url else summarize_direct(capture, tools, args.top, args.skip_frames, args.timeout)
    result.update(capture_path=str(capture), capture_sha256=sha256(capture), tracy_version='0.14.1')
    if args.metadata:
        result['capture_metadata'] = json.loads(args.metadata.read_text(encoding='utf-8-sig'))
    write_analysis(result, output)
    if args.export_events:
        executable = tools / 'tracy-csvexport.exe'
        for name, flags in [('cpu-self.csv', ['-e']), ('gpu-events.csv', ['-g'])]:
            with (output / name).open('wb') as stream, (output / (name + '.log')).open('wb') as errors:
                subprocess.run([str(executable), *flags, str(capture)], stdout=stream, stderr=errors, check=True)
    if args.archive:
        archive(capture, args.archive.resolve(), args.metadata, output)
    print(json.dumps(dict(summary=str(output / 'summary.json'), analysis=str(output / 'analysis.md'),
                          frames=result['frame_times'], cpu_zone_events=result['cpu_zone_event_count'],
                          gpu_zone_events=result['gpu_zone_event_count'],
                          cpu_zone_summary_rows=len(result['cpu_zones']), gpu_zone_summary_rows=len(result['gpu_zones'])), indent=2))


if __name__ == '__main__':
    main()
