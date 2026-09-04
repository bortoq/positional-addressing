#!/usr/bin/env python3
"""
Compile copy(n,dst,src) lines into vm1 interval words.

Input line format:
  n dst src
where:
  - n: number of bits to copy (n >= 0)
  - dst: destination start bit index
  - src: source start bit index

vm1 instruction encoding:
  dst_low  = dst - 1
  dst_high = dst + n
  src_low  = src - 1
  src_high = src + n
Each interval is open (bounds excluded), so copied bits are [start, start+n-1].
"""

from __future__ import annotations

import argparse
from pathlib import Path


def parse_copy3(path: Path) -> list[tuple[int, int, int]]:
    out: list[tuple[int, int, int]] = []
    for ln, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        s = raw.split("#", 1)[0].strip()
        if not s:
            continue
        p = s.split()
        if len(p) != 3:
            raise ValueError(f"{path}:{ln}: expected 3 fields 'n dst src'")
        n, dst, src = (int(p[0], 0), int(p[1], 0), int(p[2], 0))
        if n < 0:
            raise ValueError(f"{path}:{ln}: n must be >= 0")
        if dst < 0 or src < 0:
            raise ValueError(f"{path}:{ln}: dst/src must be >= 0")
        out.append((n, dst, src))
    return out


def enc_vm1(n: int, dst: int, src: int) -> tuple[int, int, int, int]:
    dst_low = dst - 1
    dst_high = dst + n
    src_low = src - 1
    src_high = src + n
    for v in (dst_low, dst_high, src_low, src_high):
        if v < 0 or v > 0xFFFF:
            raise ValueError(f"encoded word out of uint16 range: {v}")
    return dst_low, dst_high, src_low, src_high


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--in-copy3", required=True, help="input copy3 text file")
    ap.add_argument("--out-vm1", required=True, help="output vm1 4-word-per-line text")
    args = ap.parse_args()

    src = Path(args.in_copy3)
    dst = Path(args.out_vm1)

    rows = parse_copy3(src)
    lines: list[str] = []
    for n, d, s in rows:
        a, b, c, e = enc_vm1(n, d, s)
        lines.append(f"{a} {b} {c} {e}")
    dst.write_text("\n".join(lines) + ("\n" if lines else ""), encoding="utf-8")
    print(f"compiled {len(rows)} instructions -> {dst}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

