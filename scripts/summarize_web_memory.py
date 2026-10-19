#!/usr/bin/env python3
"""Validate the resource contracts and summarize bounded before/after runs."""
import json
from pathlib import Path

MIB = 1024*1024
rows = []
for backend in ('webgl2', 'webgpu'):
    for version in ('before', 'after'):
        report = json.loads(Path(f'captures/web-memory-{version}-{backend}.json').read_text())
        stages = report['stages']
        assert len(stages) == (6 if version == 'before' else 7), 'Every isolated feature stage must finish'
        for stage in stages:
            if version == 'after':
                assert stage['assetBytes'] == 0, 'Consumed sources retained in the virtual filesystem'
                assert stage['heap'] <= 768*MIB, 'WASM heap exceeds its hard ceiling'
                assert stage['resources']['bytes'] <= 512*MIB, 'Requested GPU resources exceed the ceiling'
                if stage['name'] in ('minimal','auto-low'):
                    assert stage['assetDownloaded'] < 8*MIB, 'Minimal scene downloaded unused world/sky'
                    assert stage['heap'] < 256*MIB, 'Minimal scene grew an unnecessarily large heap'
            def mib(value):
                return f'{value/MIB:.1f}' if value is not None else '—'
            rows.append(f"| {backend} | {version} | {stage['name']} | {mib(stage['sampledPeak'])} | {mib(stage.get('heap'))} | {mib((stage.get('resources') or {}).get('bytes'))} | {mib(stage.get('assetDownloaded'))} | {stage['cpuMedianMs']:.1f} | {stage['completionP95Ms']:.1f} |")
print('These are headless software-renderer measurements under a CPU/memory cap, not a hardware FPS guarantee. Peak is sampled aggregate cgroup memory, including Node/browser/cache. WASM is reserved capacity; GPU is requested buffer/texture storage, excluding driver memory. Reports also contain per-process RSS/PSS and CPU time.\n')
print('| Backend | Version | Features | Peak MiB | WASM MiB | GPU MiB | Download MiB | CPU median ms | Completion p95 ms |')
print('|---|---|---|---:|---:|---:|---:|---:|---:|')
print('\n'.join(rows))
