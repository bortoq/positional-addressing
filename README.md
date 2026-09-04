# vm1 — Positional Addressing (Demo 2)

Positional addressing without arithmetic. Intervals are addressed by position, not by numbers — only lexicographic compare of keys via a sorting network.

- **Problem:** copy `n` bits from `src` to `dst` in parallel for many intervals at once.
- **Key:** interval `(low,high)` (open) → `partner` (paired interval), `sort`/`origin`; hardware uses only `>` compare.
- **Mechanism:** two phases over the network: `stage1` marks intervals (`key.sort=partner`), `stage2` transposes and moves `value` by `role`.
- **Network:** pairwise (stable) from `src/sort2.c`, 65,520 positions, 136 layers.

This repo is the *addressing* demo. The address space itself is a separate project.

## Quick start

```sh
make all          # vm1 + sort2 + tools
make sorter       # ./sort2 65520 > sorter.txt
./vm1 sorter.txt < input.bin > output.bin
# with program load (LDR1)
./tools/copy3_to_vm1 --in-copy3 examples/echo.copy3 --out-vm1 /tmp/echo.vm1
./tools/mkldr1 --in-prog /tmp/echo.vm1 --out /tmp/prog.ldrpkt
cat /tmp/prog.ldrpkt input.bin | ./vm1 sorter.txt > out.bin
make check        # C tests: sort256 + compat
```

## Layout

- `src/` — `vm1.c/.h` (2-phase addressing), `sort2.c`, `loader.inc`
- `tools/` — `mkldr1`, `copy3_to_vm1` (C)
- `examples/` — `echo.copy3`, `sort256_runtime.vm1`
- `tests/` — `test_sort256`, `test_compat` (C)
- `doc/` — `README.md` (addressing details), `hardware.md` (cost/RTL)

See `doc/README.md` for the addressing cycle and `doc/hardware.md` for transistor estimates.

License: MIT — Dmitri Bortoq 2024-2025.
