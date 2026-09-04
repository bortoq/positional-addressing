#!/usr/bin/env python3
"""External 256-element sort test for current vm1 project state."""

from __future__ import annotations

import random
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GEN = ROOT / "tools" / "gen_pairwise_sorter.py"
SORTER = ROOT / "sorter.txt"


def parse_layers(path: Path) -> list[list[tuple[int, int]]]:
    layers: list[list[tuple[int, int]]] = []
    cur: list[tuple[int, int]] = []
    for raw in path.read_text(encoding="ascii").splitlines():
        s = raw.strip()
        if not s:
            if cur:
                layers.append(cur)
                cur = []
            continue
        a_s, b_s = s.split()
        a, b = int(a_s), int(b_s)
        if a < 256 and b < 256:
            cur.append((a, b))
    if cur:
        layers.append(cur)
    return layers


def apply_layers(arr: list[int], layers: list[list[tuple[int, int]]]) -> list[int]:
    out = arr[:]
    for layer in layers:
        for a, b in layer:
            if out[a] > out[b]:
                out[a], out[b] = out[b], out[a]
    return out


def sorted_nondec(arr: list[int]) -> bool:
    return all(arr[i] <= arr[i + 1] for i in range(len(arr) - 1))


def cases() -> list[list[int]]:
    rng = random.Random(1)
    return [
        list(range(256)),
        list(reversed(range(256))),
        [(i * 73 + 19) & 0xFF for i in range(256)],
        [rng.randrange(0, 256) for _ in range(256)],
    ]


def main() -> int:
    subprocess.run(["python3", str(GEN), "--out", str(SORTER)], check=True)
    layers = parse_layers(SORTER)
    if not layers:
        print("ERROR: empty 256-element layer projection", file=sys.stderr)
        return 2

    failed = 0
    for i, c in enumerate(cases(), start=1):
        out = apply_layers(c, layers)
        if not sorted_nondec(out):
            failed += 1
            print(f"[FAIL] case#{i}", file=sys.stderr)
    if failed:
        print(f"TOTAL: {failed} failed case(s)", file=sys.stderr)
        return 1
    print("TOTAL: all cases passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
