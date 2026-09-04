#!/usr/bin/env python3
"""
Generate vm1-compatible sorter from /home/user/work/sorter/gen_network.

The generator builds a pairwise network for 65536 wires and filters
comparators to vm1 address space (indices < 65520).
"""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys


VM1_SPACE_SIZE = 65520
PAIRWISE_N = 65536
DEFAULT_GEN = pathlib.Path("/home/user/work/sorter/gen_network")
ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_OUT = ROOT / "sorter.txt"


def parse_args() -> argparse.Namespace:
    ap = argparse.ArgumentParser()
    ap.add_argument("--generator", type=pathlib.Path, default=DEFAULT_GEN)
    ap.add_argument("--out", type=pathlib.Path, default=DEFAULT_OUT)
    return ap.parse_args()


def main() -> int:
    args = parse_args()
    if not args.generator.exists():
        print(f"missing generator: {args.generator}", file=sys.stderr)
        return 2

    proc = subprocess.run(
        [str(args.generator), "pairwise", str(PAIRWISE_N)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
        text=True,
    )
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr)
        return proc.returncode

    out_lines: list[str] = []
    current_layer: list[str] = []
    for line in proc.stdout.splitlines():
        s = line.strip()
        if not s:
            if current_layer:
                out_lines.extend(current_layer)
                out_lines.append("")
                current_layer = []
            continue
        a_s, b_s = s.split()
        a = int(a_s)
        b = int(b_s)
        if a < VM1_SPACE_SIZE and b < VM1_SPACE_SIZE:
            current_layer.append(f"{a} {b}")
    if current_layer:
        out_lines.extend(current_layer)
        out_lines.append("")

    args.out.write_text("\n".join(out_lines), encoding="ascii")
    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
