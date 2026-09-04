# vm1 — Parallel Address Space Emulator (Demo 2)

Emulator for a parallel addressable space with position-based addressing.
Current state: 2026-05-23.

## Contents

1. [Purpose](#purpose)
2. [Project layout](#project-layout)
3. [Build and run](#build-and-run)
4. [VM1 architecture](#vm1-architecture)
5. [Position state](#position-state)
6. [Init and limits](#init-and-limits)
7. [Two-phase addressing](#two-phase-addressing)
8. [Sorting network (sort2)](#sorting-network-sort2)
9. [Hardware cost estimate](#hardware-cost-estimate)
10. [Hardware implementation](#hardware-implementation)
11. [Testing](#testing)
12. [Glossary](#glossary)

## Purpose

Demo 2 emulates an address space with positional addressing.

- Addressable value in the current model: one bit.
- Main operation: parallel copy of intervals.
- Addressing is done through a sorting network and comparison of index fields.

The active version implements two-phase addressing:

- phase 1: `stage1_init` / `stage1_pair` — create and mark intervals
- phase 2: `stage2_init` / `stage2_pair` — transpose intervals and move values

## Project layout

- `vm1.c` — emulator for the address space and two-phase addressing
- `vm1.h` — definitions for `machine`, `subspace`, `network` fabric and vm1 constants
- `loader.inc` — built-in loader word table in explicit vm1 format `[dst_low, dst_high, src_low, src_high]`
- `tools/mkldr1.py` — external packer for vm1 programs into LDR1 binary stream
- `tools/copy3_to_vm1.py` — compiler from `copy3` (`n dst src`) to vm1 words
- `tools/gen_pairwise_sorter.py` — wrapper for alternative pairwise generator (optional, needs external `gen_network`)
- `programs/sort256_runtime.vm1` — template runtime program for sorting 256 bytes
- `programs/echo.copy3` — simple echo example (`8 65504 65512`)
- `sort2.c` — sorting network generator (odd-even mergesort). Builds network for the next power of two and prints only pairs inside the working set
- `sorter.txt` — generated text network for current `SPACE_SIZE` (not committed, run `make sorter`)
- `tests/` — integration checks for sorting network and channel copy
- `Makefile` — build, sorter generation, tests
- `README.md` — this file

## Build and run

Build:

```sh
make all
# or manually:
cc -Wall -Wextra -pedantic -std=c11 -O2 sort2.c -o sort2
cc -Wall -Wextra -pedantic -std=c11 -O2 vm1.c -o vm1
```

Generate network for current `SPACE_SIZE` (65520):

```sh
make sorter
# equivalent to:
./sort2 65520 > sorter.txt
```

Run emulator:

```sh
./vm1 sorter.txt < input.bin > output.bin
```

Optional external code loading from stdin (LDR1 protocol):

```
bytes 0..3: ASCII magic "LDR1"
bytes 4..5: word_count (uint16 little-endian)
bytes 6..7: dst_word_base (uint16 little-endian)
then:       payload of word_count words (uint16 little-endian)
```

Remaining bytes of stdin after the LDR1 payload go to the normal `CHANNEL_IN` stream.

Build LDR1 packet:

```sh
python3 tools/mkldr1.py \
  --in-prog programs/sort256_runtime.vm1 \
  --out tmp_sort256.ldrpkt \
  --dst-word-base 0
```

Note: `vm1` expects all indexes in `sorter.txt` to be `< SPACE_SIZE`. `sort2` guarantees this when called as `./sort2 SPACE_SIZE`.

## VM1 architecture

Core structure: `machine`.

It contains:

- `space` — packed bits of the address space
- `subspace` — logical state of each position
- `fabric` — sorting network loaded from `sorter.txt`

Network is stored separately from position state:

```c
fabric.layers[l].pairs[p].a
fabric.layers[l].pairs[p].b
```

This matches hardware separation: network storage apart from position state.

## Position state

Each `subspace` entry holds:

- `value` — data bit
- `active` — flag that position takes part in the current copy phase
- `role` — interval role (source or destination)
- `key` — address key:
  - `low` — lower bound of interval
  - `high` — upper bound of interval
  - `partner` — lower bound of paired interval
  - `sort` — current sortable index
  - `origin` — fixed position index

## Init and limits

On start `vm1`:

1. opens sorter file,
2. reads index pairs,
3. splits pairs into layers by repeated position in a layer,
4. stores network in `fabric`,
5. assigns `key.origin` to each position.

The emulator writes the built-in loader from `loader.inc` into the start of the address space.
The loader is one instruction: 8 bytes (4 `idx` words).
Copy runs only if `space` can hold at least one instruction. This holds for the current config.

If the config changes and this does not hold, the loader is not written. The processor area stays zero: the compute loop still runs but copy instructions are inactive until a program is written to the address space.

Future loader plan (next stage): external stdin protocol (`LDR1`) is done. Next step is to move this logic inside the runtime address space via self-modification without changing vm1 instruction format.

Channels:

```c
CHANNEL_IN  = SPACE_SIZE / CHAR_BIT - 1
CHANNEL_OUT = SPACE_SIZE / CHAR_BIT - 2
```

One `vm1` cycle:

- reads one byte from stdin into `CHANNEL_IN`
- runs both addressing phases
- writes one byte from `CHANNEL_OUT` to stdout

EOF on input stops the emulator. Main time limit is `LIFE` in `vm1.h`.

## Two-phase addressing

Compute loop:

```c
while (life-- && channel_read(vm1) == 0) {
  stage1_init(vm1);
  stage1_pair(...) for all pairs;
  stage2_init(vm1);
  stage2_pair(...) for all pairs;
  channel_write(vm1);
}
```

Phase 1:

- `stage1_init()` — write back processed values from `subspace` to `space`, load `space` bits into `subspace`, clear `active`/`role`, restore `key.sort` from `key.origin`, read interval instructions from processor area and seed initial `dst`/`src` entries.

- `stage1_pair()` — for each pair, spread interval attributes, fill `key.sort` with the paired interval where a position falls inside a copied interval.

Phase 2:

- `stage2_init()` — copy `key.origin` to `key.partner`.

- `stage2_pair()` — compare `key.sort` of the pair, transpose `key.sort`/`key.partner` if needed, move `value` according to `role`.

Note: `stage` = one addressing phase, `cycle` = full compute cycle (`stage1` + `stage2`).

## Sorting network (sort2)

`sort2` builds an odd-even mergesort network:

1. pick nearest power of two `>= SPACE_SIZE`,
2. build full network for that size,
3. print only pairs where `a < SPACE_SIZE` and `b < SPACE_SIZE`.

Text format `sorter.txt`:

```
0 32768
1 32769
2 32770
```

One line = one comparator pair.

Current `sorter.txt` parameters:

- pairs: 3,984,771
- layers: 136
- max pairs per layer: 32,760

This keeps network size and vm1 `fabric` in sync.

## Hardware cost estimate

For the current `vm1` layout. Approximations:

- 1 memory bit: 6 transistors
- 1 bit of `>` comparator: 8 transistors

Current dataset: `SPACE_SIZE=65520`, `INDEX_SIZE=16`, `max pairs per layer <= 32760`.

Per position:

```
value  = 1 bit
active = 1 bit
role   = 1 bit
key    = 5 * 16 = 80 bits
total per position = 83 bits
```

Subspace memory: `65520 * 83 = 5,438,160` bits  
Space memory: `65,520` bits  
Network ROM: `pairs_total * 2 * 16` bits  
For current sorter: `3,984,771 * 32 = 127,512,672` bits

6T memory estimate:

- without ROM: `(5,438,160 + 65,520) * 6 = 33,022,080`
- with ROM: `(5,438,160 + 65,520 + 127,512,672) * 6 = 798,098,112`

Logic per pair unit:

- `stage1_pair`: up to 10 compares of 16-bit indexes → ~10*16*8 = 1,280 transistors
- `stage2_pair`: 1 compare → ~128 transistors

For a fully parallel wide layer (32760 pairs):

- stage1: `32760 * 1280 = 41,932,800`
- stage2: `32760 * 128  = 4,193,280`

Main area cost in the full build is usually network storage. Exact value depends on `pairs_total`.

## Hardware implementation

`vm1.c` is a behavioral spec. Direct C-to-HDL translation is not intended.

Practical blocks:

- `top` — connects `control`, `network_rom`, `subspace_ram`, `datapath`, `io`
- `control` FSM (replaces C loops): `IDLE, CHANNEL_READ, STAGE1_INIT, STAGE1_RUN, STAGE2_INIT, STAGE2_RUN, CHANNEL_WRITE, DONE_OR_REPEAT`
- `network_rom` — stores pairs `(a,b)` and layer bounds
- `subspace_ram` — stores `value, active, role, key`
- `space_mem` — packed address space bits
- `stage1 datapath` — implements `stage1_pair`
- `stage2 datapath` — implements `stage2_pair`
- `io` — `CHANNEL_IN/OUT` and end-of-stream

Suggested path:

1. synthesize small model,
2. check `stage1`/`stage2` against C emulator,
3. add pipelining by layers,
4. optimize network storage/generation.

## Testing

Tests are not part of `vm1` runtime logic. Test cases are address-space data (processor program + `CHANNEL_IN` stream).

Suggested way:

1. prepare address space image with interval program,
2. feed input stream,
3. compare `CHANNEL_OUT` with expected output.

Project helpers:

```sh
make check          # sort2 sanity + vm1 space sort
./tests/run_echo_ldr1.sh
./tests/run_sort256_test.sh
```

## Glossary

- **address space** — packed bit array `space`
- **position** — one addressable slot in the address space
- **subspace** — logical state of all positions (`value, active, role, key`)
- **key** — lexicographic address key: `low/high/partner/sort/origin`
- **interval** — half-open copy interval `[low, high)`
- **sorting network** — comparator pairs organized in layers (`fabric.layers`)
- **layer** — set of pairs that can run in parallel
- **pair** — one index pair `(a,b)` in the network
- **stage** — one addressing phase (two stages in vm1)
- **cycle** — full compute cycle: `stage1` + `stage2`
- **stage1_init** — stage 1 setup: sync `space`/`subspace`, reset flags, load intervals
- **stage1_pair** — stage 1 rule for one comparator pair
- **stage2_init** — stage 2 setup: copy `origin` to `partner`
- **stage2_pair** — stage 2 rule (compare/swap/move)
- **processor area** — region where interval instructions live
- **channel** — byte I/O interface: `CHANNEL_IN` and `CHANNEL_OUT`

## License

MIT — see `vm1.c`/`vm1.h` headers. Copyright 2024-2025 Dmitri Bortoq (bortoq@gmail.com).
