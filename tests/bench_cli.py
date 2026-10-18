#!/usr/bin/env python3
"""Compare unmodified old/new vm1 CLI processes, including loading and I/O."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import random
import resource
import statistics
import struct
import subprocess
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[1])
parser.add_argument('--output', type=Path, default=Path('/tmp/vm1-cli-comparison'))
parser.add_argument('--baseline', default='9886b1c')
parser.add_argument('--rounds', type=int, default=9)
args = parser.parse_args()
ROOT = args.repo.resolve()
OUT = args.output.resolve()
OUT.mkdir(parents=True, exist_ok=True)
REV = args.baseline
FLAGS = ['-Wall', '-Wextra', '-Werror', '-pedantic', '-std=c11', '-O2']
ROUNDS = args.rounds
assert ROUNDS > 0
variants = ['three-phase', 'two-phase']
cpu = min(os.sched_getaffinity(0))
os.sched_setaffinity(0, {cpu})
hashes = {}
for variant in variants:
    src = OUT / variant
    src.mkdir(exist_ok=True)
    for name in ['machine.c', 'machine.h', 'loader.h']:
        data = (subprocess.check_output(['git', 'show', f'{REV}:src/{name}'], cwd=ROOT)
                if variant == 'three-phase' else (ROOT / 'src' / name).read_bytes())
        (src / name).write_bytes(data)
        hashes[f'{variant}/{name}'] = hashlib.sha256(data).hexdigest()
    subprocess.run(['cc', *FLAGS, str(src / 'machine.c'), '-o', str(src / 'vm1')], check=True)

fixtures = {}
for scenario in ['startup-only', 'echo', 'long', '127-short', '127-long', '127-tail', '127-unaligned']:
    rng = random.Random(12345)
    memory = bytearray(rng.randbytes(8190))
    memory[:1024] = bytes(1024)
    memory[-2:] = b'\0\0'
    desc = [(65504, 65512, 65512, 65520)]
    count = 1 if scenario == 'long' else (127 if scenario.startswith('127-') else 0)
    for i in range(count):
        n = 8191 if scenario == 'long' else (128 if scenario == '127-long' else (63 if scenario == '127-unaligned' else 7))
        stride = 128 if scenario == '127-long' else (64 if scenario == '127-unaligned' else 8)
        dst = 8193 + i * stride
        src = 32773 + (count - 1 - i) * stride
        if scenario == '127-tail':
            src = 65504 - 8 * count + i * 8
        desc.append((dst, dst + n, src, src + n))
    occupied = set()
    for d0, d1, s0, s1 in desc:
        assert d1 - d0 == s1 - s0
        for a, b in [(d0, d1), (s0, s1)]:
            positions = set(range(a, b))
            assert not occupied & positions
            occupied.update(positions)
    for i, instruction in enumerate(desc):
        struct.pack_into('<4H', memory, i * 8, *instruction)
    tail = bytes(range(33, 49)) if scenario != 'startup-only' else b''
    packet = b'LDR1' + struct.pack('<HH', 4095, 0) + memory + tail
    expected = b'\0' + tail[:-1] if tail else b''
    fixtures[scenario] = (packet, expected)
    (OUT / f'{scenario}.ldr').write_bytes(packet)

def run(variant, scenario):
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    start = time.perf_counter()
    result = subprocess.run([str(OUT / variant / 'vm1'), str(ROOT / 'sorter.txt')],
                            input=fixtures[scenario][0], capture_output=True, check=True)
    wall = time.perf_counter() - start
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    assert result.stdout == fixtures[scenario][1], (variant, scenario, result.stdout.hex())
    assert not result.stderr, result.stderr
    return wall, (after.ru_utime + after.ru_stime - before.ru_utime - before.ru_stime)

meta = {'baseline': REV, 'compiler': subprocess.check_output(['cc', '--version'], text=True).splitlines()[0],
        'flags': FLAGS, 'cpu_affinity': cpu, 'rounds': ROUNDS, 'cycles': 16,
        'source_sha256': hashes, 'network_sha256': hashlib.sha256((ROOT / 'sorter.txt').read_bytes()).hexdigest()}
(OUT / 'metadata.json').write_text(json.dumps(meta, indent=2) + '\n')
for variant in variants:
    run(variant, 'echo')
print('Built both production CLIs; warmup and channel-output checks passed.', flush=True)
rows = []
rng = random.Random(67890)
with (OUT / 'results.csv').open('w', newline='') as f:
    writer = csv.writer(f, lineterminator='\n')
    writer.writerow(['round', 'fixture', 'variant', 'cycles', 'wall_s', 'cpu_s'])
    for r in range(ROUNDS):
        scenarios = list(fixtures)
        rng.shuffle(scenarios)
        for scenario in scenarios:
            order = variants.copy()
            rng.shuffle(order)
            for variant in order:
                wall, cpu_time = run(variant, scenario)
                row = [r, scenario, variant, 0 if scenario == 'startup-only' else 16, wall, cpu_time]
                rows.append(row)
                writer.writerow(row)
        f.flush()
        print(f'Round {r+1}/{ROUNDS}: all 14 processes and expected outputs passed.', flush=True)
print('fixture,old_wall_ms,new_wall_ms,wall_reduction_percent,old_cpu_ms,new_cpu_ms', flush=True)
for scenario in fixtures:
    vals = {v: [statistics.median([row[col] for row in rows if row[1] == scenario and row[2] == v]) * 1000
                for col in [4, 5]] for v in variants}
    old, new = (vals[v] for v in variants)
    print(f'{scenario},{old[0]:.3f},{new[0]:.3f},{100*(1-new[0]/old[0]):.2f},{old[1]:.3f},{new[1]:.3f}', flush=True)
