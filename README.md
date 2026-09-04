# vm1 — Parallel Address Space Emulator

Demo 2 of parallel addressable space. One bit per position, parallel interval copies via sorting network.

- **Space:** 65,520 positions, packed bits + per-position state (`value/active/role/key`)
- **Addressing:** two-phase (`stage1` mark intervals → `stage2` sort & move), no arithmetic, only index compare
- **Network:** odd-even mergesort from `src/sort2.c`, filtered to `SPACE_SIZE`

## Quick start

```sh
make all          # build vm1 and sort2
make sorter       # ./src/sort2 65520 > sorter.txt
./vm1 sorter.txt < input.bin > output.bin
# with LDR1 load
python3 tools/mkldr1.py --in-prog programs/echo.copy3 --out prog.ldrpkt
cat prog.ldrpkt input.bin | ./vm1 sorter.txt > out.bin
make check        # tests
```

## Layout

- `src/` — `vm1.c/.h`, `sort2.c`, `loader.inc`
- `tools/` — `mkldr1.py`, `copy3_to_vm1.py`
- `programs/` — `echo.copy3`, `sort256_runtime.vm1`
- `tests/` — `test_sort256_pairs.py`, `test_sorter_compat.py`
- `doc/` — full documentation

See `doc/README.md` for architecture, two-phase addressing, hardware cost and glossary.

License: MIT — Dmitri Bortoq 2024-2025.
