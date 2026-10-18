# Hardware notes — VM1 positional addressing

`src/machine.c` is a behavioral model, not synthesizable RTL. It models three
sorting-network passes plus monotone interval splitting, using existing fields
and connections. See the [algorithm and proofs](README.md).

## State budget

```
value(1) + active(1) + role(1) + five 16-bit words = 83 logical bits/position
```

For N=65520 this is 5,438,160 subspace bits plus 65,520 packed memory bits.
The C position is 14 bytes, key 10 bytes, machine 925496 bytes excluding the
allocated network. C padding and sizes are not physical transistor estimates.

Phase 1 primary records occupy low/high/partner, value/active. The actual bit
remains in `space`. Spare sort/origin/role hold packed ghost records or temporary
cross-pair records. Two ghost bounds occupy one 16-bit word: their common high
byte comes from fixed configuration wires; their low bytes are stored. The
paired boundary occupies another word and the source flag uses the third flag.
The configured processor has 1–128 slots; its maximum 256 sorted descriptors
is essential to the endpoint-packing proof. This limit is asserted explicitly.

`origin` temporarily stores ghost metadata, then returns to the physical index.
A historical optimization using a constant wire instead of an origin register
therefore cannot be retained unchanged on the default incomplete space. The
same caveat already applied to the preceding collective-join implementation.

## Existing links and execution schedule

Descriptor sorting and phases 2/3 traverse the loaded pairwise fabric. Butterfly
real pairs and mirrored ghost pairs use dimension-d links already present.
A mixed pair uses an existing dimension-H link twice and dimension-d link three
times through a scratch cell, with local preparation/merging combined into the
first/last sublayers. The scratch cell's primary state remains intact. Detailed
field allocations and the five-step schedule are in the algorithm document.
The top mixed pair, if present, is entirely local.

Structural tests trace physical endpoint usage, not just abstract logical wires.
They verify each cross/ghost link exists in the loaded fabric and each physical
cell participates at most once per layer. Schedule hashes and layer counts are
identical for K=0,1,maximum. Extra shapes cover wide padding gaps and a first-stage
local mixed pair. A counterexample specifically exercises the last real position
with eighteen concentrated descriptor inputs.

Fixed wire indices select processor words and split boundaries. Operations on
program endpoints are comparisons, selection/copying and fixed byte slicing /
concatenation for packed records. No adder, subtractor, dynamic descriptor-table
lookup, extra packet bank, sorting network per instruction or widened key is
introduced by the algorithm. Guards that diagnose violated invariants in C are
verification checks; they are not requirements to build extra error-detection
circuits into each position.

This specifies unchanged persistent fields and reuse of graph edges. It does
not prove identical mux/control circuitry or transistor/energy costs. Local
record temporaries in the C code describe combinational values; RTL must realize
the listed staged moves in the existing registers.

## Depth and energy

At the default size: 408 layers from three full passes, 16 real butterfly layers,
25 cross-pair layers and five local steps, total 454. The previous algorithm
needed 3234 scheduled steps. The trace visits 12,515,741 pairs/local operations
instead of 95,665,352. These counts are independent of active K at fixed hardware
configuration. They indicate reduced scheduled work, not measured energy.

Word comparisons and selection must be implemented using the chosen comparator /
mux timing. Packing uses fixed bit connections; it requires no stored arithmetic
result. The five cross sublayers are real sequential dependencies. A word-level
step is neither necessarily one FPGA clock nor one transistor gate delay.
Increasing word width also changes comparator delay. Logical depth remains
O(log² N), with bound `3L+7a+5`, while the processor capacity is at most 128.

## Remaining physical acceptance

- Write RTL for the staged allocation and independent processor inputs.
- Compare RTL with the snapshot oracle, including ghost/scratch lifetimes.
- Synthesize and measure register bits, comparators, muxes, routing, controller,
  ROM, area, timing, leakage and switching energy in the same technology.
- Include the mutable-origin requirement when comparing with constant-label
  historical designs. No physical-equivalence claim follows from C sizes.
- Keep exactly-three-pass depth without additional layers as an unachieved
  optional optimization; current software uses 449 network layers plus locals.
