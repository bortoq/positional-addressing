# vm1 — Positional Addressing

Demo 2: intervals are addressed by *position* and moved by sorting, without arithmetic.

## Contents
1. [Idea](#idea) 2. [Layout](#layout) 3. [Build & run](#build--run) 4. [Two-phase addressing](#two-phase-addressing) 5. [Position state](#position-state) 6. [Init & limits](#init--limits) 7. [Sorting network](#sorting-network) 8. [Testing](#testing) 9. [Glossary](#glossary) — [Hardware → hardware.md](hardware.md)

## Idea

Copy `n` bits `src → dst` in parallel. Interval is open `(low,high)` (both bounds excluded) with a `partner` (the paired interval). Addressing key per position: `low/high/partner/sort/origin`. Only operation on keys is `>` compare inside the sorting network. No addition, no indices as numbers.

Current demo: `SPACE_SIZE=65520`, 1 bit per position, `INDEX_SIZE=16`.

## Layout

- `src/vm1.c` — two-phase addressing emulator
- `src/vm1.h` — `machine`/`subspace`/`network`, `SPACE_SIZE`/`LIFE`/`CHANNEL_IN/OUT`
- `src/loader.inc` — built-in loader `[dst_low,dst_high,src_low,src_high]`
- `src/sort2.c` — pairwise generator (stable)
- `tools/mkldr1` — pack `.vm1` → `LDR1` (`LDR1 + u16 count/base + payload`)
- `tools/copy3_to_vm1` — `n dst src` → `4` vm1 words
- `examples/` — `echo.copy3`, `sort256_runtime.vm1`
- `sorter.txt` — generated network (not committed)
- `tests/` — C checks

## Build & run

```sh
make all                 # vm1, sort2, tools
make sorter              # ./sort2 65520 > sorter.txt
./vm1 sorter.txt < in.bin > out.bin
# LDR1 load: 4b magic "LDR1" + u16 count + u16 base + u16 words + tail bytes -> CHANNEL_IN
./tools/copy3_to_vm1 --in-copy3 examples/echo.copy3 --out-vm1 /tmp/e.vm1
./tools/mkldr1 --in-prog /tmp/e.vm1 --out /tmp/p.ldrpkt --dst-word-base 0
cat /tmp/p.ldrpkt in.bin | ./vm1 sorter.txt > out.bin
```
`sort2` guarantees `a,b < SPACE_SIZE`. `LIFE` (in `vm1.h`) bounds cycles.

## Two-phase addressing

```c
while (life-- && !channel_read(vm1)) {
  stage1_init(vm1);  for (pairs) stage1_pair(vm1,a,b);
  stage2_init(vm1);  for (pairs) stage2_pair(vm1,a,b);
  channel_write(vm1);
}
```

**stage1 — mark:** `stage1_init` syncs `space↔subspace`, clears `active/role`, restores `sort=origin`, seeds `dst/src` keys from processor area. `stage1_pair` spreads interval attributes along pairs, sets `sort=partner` where a position lies inside the interval.

**stage2 — transpose & move:** `stage2_init` copies `origin→partner`. `stage2_pair` compares `sort` of the pair, swaps `sort/partner` if needed, moves `value` according to `role`.

`stage` = one phase, `cycle` = both phases.

## Position state

`subspace[i]`:
- `value` (1 bit), `active` (participates), `role` (src/dst)
- `key.low/high` (interval), `key.partner` (paired low), `key.sort` (sortable), `key.origin` (fixed)

## Init & limits

1. load `sorter.txt` → `fabric.layers` (split by reused index), 2. set `origin=i`.
3. write built-in loader (`src/loader.inc`, 8 bytes / 4 `idx` words) to `space[0]` if it fits.
`CHANNEL_IN = SPACE_SIZE/8 -1`, `CHANNEL_OUT = -2` (last two bytes). EOF ends loop.

## Sorting network (pairwise, stable)

Pairwise is stable — equal keys keep order — so two phases are enough (no third reordering). `sort2`:
1. next pow2 `>= SPACE_SIZE`, 2. build full pairwise network for that size, 3. if needed filter `a,b < SPACE_SIZE` and repack by wire readiness.
Text `sorter.txt`: `a b` per line, blank line between layers. Current pairwise 65,520: ~3,997,174 lines (≈136 layers, ≤32,760/layer), filtered from 65,536 and repacked greedily.

## Testing

Tests use only address-space data (program + `CHANNEL_IN`):
```sh
make check   # sort256 + vm1 compat (C, no Python)
```
See `doc/hardware.md` for transistor/RTL estimates.

## Glossary

- **position** — one bit slot, **subspace** — its state (`value/active/role/key`)
- **key** — `low/high/partner/sort/origin`, **interval** — open `(low,high)` → bits `[low+1, high-1]`
- **fabric/layer/pair** — network (`fabric.layers[l].pairs[p]`)
- **stage/cycle** — phase / full `stage1+stage2`
- **processor area** — interval instructions in `space`, **channel** — `CHANNEL_IN/OUT` bytes

License: MIT — see `src/vm1.h`.
