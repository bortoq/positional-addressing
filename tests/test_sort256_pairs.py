#!/usr/bin/env python3
"""
Validate 256-element sorting with the project sorter pairs.

This test is external to vm1 runtime code:
  1. Generate vm1-compatible sorter.txt from sort2 (odd-even mergesort).
  2. Parse sorter pairs/layers.
  3. Apply the same layered comparator schedule to 256-byte arrays.
  4. Check nondecreasing order after network execution.
"""

from __future__ import annotations

import random
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SORTER = ROOT / "sorter.txt"
SORT2 = ROOT / "sort2"


def gen_sorter() -> None:
    # Build sort2 if needed and generate sorter.txt for current SPACE_SIZE
    subprocess.run(["make", "-C", str(ROOT), "sorter"], check=True)


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


def run_layers(arr: list[int], layers: list[list[tuple[int, int]]]) -> list[int]:
    out = arr[:]
    for layer in layers:
        for a, b in layer:
            if out[a] > out[b]:
                out[a], out[b] = out[b], out[a]
    return out


def is_sorted_nondec(arr: list[int]) -> bool:
    return all(arr[i] <= arr[i + 1] for i in range(len(arr) - 1))


def make_cases() -> list[list[int]]:
    rng = random.Random(1)
    return [
        list(range(256)),
        list(reversed(range(256))),
        [(i * 73 + 19) & 0xFF for i in range(256)],
        [rng.randrange(0, 256) for _ in range(256)],
    ]


def main() -> int:
    gen_sorter()
    layers = parse_layers(SORTER)
    if not layers:
        print("ERROR: no layers for 256-element projection", file=sys.stderr)
        return 2

    failed = 0
    for i, case in enumerate(make_cases(), start=1):
        out = run_layers(case, layers)
        if not is_sorted_nondec(out):
            failed += 1
            print(f"[FAIL] case#{i}", file=sys.stderr)

    if failed:
        print(f"TOTAL: {failed} failed case(s)", file=sys.stderr)
        return 1

    print("TOTAL: all cases passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
