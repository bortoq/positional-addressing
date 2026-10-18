# vm1 — Positional Addressing

The mechanism copies bits between positional half-open ranges using only
comparisons and transport of keys. Instruction words are 16-bit:

```text
[dst_low, dst_high, src_low, src_high]
copy(n,dst,src) → [dst,dst+n,src,src+n]
```

The copy3 compiler performs the additions on the host; the addressing
mechanism does not add or subtract addresses.

## Valid programs

An active instruction has equal positive source and destination lengths,
with `0 <= low < high <= SPACE_SIZE`. A source with equal endpoints is NOP,
with ignored destination fields; the compiler emits canonical empty pairs.
All source and destination ranges of a logical cycle must be pairwise
disjoint, including source/source ranges. Adjacent ranges may share endpoints.
These are program obligations, not new mandatory hardware validation circuits.

Memory is `[0,SPACE_SIZE)`. Position zero is addressable. The exclusive end
must fit the existing index: 65520 fits 16 bits; 65536 does not. The network
generator may still test a 65536-wire sorting network independently of vm1.
Processor words start at zero, up to `PROCESSOR_N` four-word slots. Copies
may rewrite them, with the new instructions taking effect on the next cycle.
All instruction reads occur during phase 1, before any new data is committed.

## Why three phases were restored

The old implementation removed the third phase on the assumption that its
pairwise sorting network was stable. This is false even for `sort 4`, whose
pairs are `(0,2), (1,3), (0,1), (2,3), (1,2)`. Records `(key,origin)`:

```text
input:           [(0,0),(1,1),(0,2),(0,3)]
stable result:   [(0,0),(0,2),(0,3),(1,1)]
actual result:   [(0,0),(0,3),(0,2),(1,1)]
```

Sorting by a common interval key can therefore permute its bits. Phase 3
sorts the copied group's records by original source position and carries
their bit values with them. It works without requiring network stability or
widening the primary comparison key to include origin.

Restoring the historical third-phase function alone was insufficient. The
old first phase let distinct interval descriptors overwrite each other.
The old second phase copied selected values while moving their keys without
consistently transporting the associated ownership bits. Independent tests
found lost copies and corruption of source and unrelated positions before
the change of interval format.

The corrected first phase broadcasts descriptors one at a time, retaining
position classifications separately from moving descriptor state. The second
phase swaps complete records. This uses the existing position cells and
network repeatedly. The project owner explicitly accepted extra work within
phase 1 without adding position cells on 2026-10-18. Three phases now mean
three algorithmic stages, not exactly three passes through the network.

## The three phases

Each position has `value`, `active`, `role` and five key words
`{low, high, partner, sort, origin}`. `origin` stays fixed.

1. **Classify.** Commit the preceding cycle at the boundary, clear `role`
   and initialize `sort=origin`. For each non-NOP instruction, broadcast its
   destination descriptor and then its source descriptor, one per network
   pass. Clear only `active` between broadcasts. The broadcast carries
   `low/high/partner` and uses `value` as its direction bit; actual memory
   data remains in `space`. When a position first receives the token, test
   `low <= origin < high`. Matching positions retain `sort=partner` and
   `role=source`; later broadcasts leave these marks intact because valid
   ranges are disjoint. Broadcasts never perform ordinary data copies.
2. **Transpose.** Load actual bits from `space` into `value`, and set
   `partner=origin`. Sort by `sort`, swapping `sort`, `partner`, `value` and
   `role` together. Equal-length paired intervals exchange groups; gaps keep
   their rank. The transported source flag now marks destination writes.
3. **Restore order.** For a writing group with equal `sort` keys, compare
   transported `partner` origins and swap origins with bit values as needed.
   Different groups already have ordered primary keys and never exchange
   records. Non-writing groups need no repair because they are not committed.

At the following cycle boundary, only records with `role=1` write memory.
Original sources and unrelated bits remain in `space`; no full-space host
copy routine substitutes for network addressing. The legacy channel loop
observes this same boundary, so its direct echo retains one-cycle latency.

For K non-NOP instructions, classification takes `2K` network passes, and
transpose/restore take two more: `2K+2` passes, plus initialization and sync.
This deliberately trades time and switching activity for cell reuse. Empty
slots do not issue broadcasts. No per-position fields, extra address bits,
duplicate networks or per-comparator history bits were added.

## Network and loading

`src/network.c` constructs a pairwise network on the next power of two,
filters out-of-space pairs and packs remaining links by wire readiness.
The generated network provides both the sorting property and root-zero
broadcast coverage used above. An arbitrary substitute network must satisfy
both properties; sorting alone is not a certification of phase 1.

`sorter.txt` contains `a b` pairs with `a<b`. A hardware layer cannot use a
wire twice. The loader reconstructs layers while preserving pair order;
tests validate every loaded layer on both machine sizes. A boundary bug
which failed to reserve the first pair's wires in a new layer was fixed.
For 65520 positions the network contains 3,997,039 pairs and 136 layers.

The built-in loader contains half-open words `[64,8192,8192,16320]`.
`LDR1` is still `magic + u16 count + u16 base + little-endian word payload`,
followed by channel input. Old open-interval payloads must be regenerated;
LDR1 does not detect or automatically convert the semantic change.

The current channel loop loads LDR1 if present, reads one input byte, runs
one logical cycle and writes the previously committed output byte. It stops
at EOF or `LIFE=16`; it does not drain pending results. Future channel work
must define a separate contract instead of relying on addressing-phase order.

## Verification and hardware

`make check` runs all units, sorting checks, exact channel echo and an
independent snapshot-based simultaneous-copy oracle on 256 and 65520 positions.
The oracle checks every bit, including sources, untouched memory and processor
self-modification. See [the audit](audit.md) for results and reproduction.
[Hardware notes](hardware.md) distinguish unchanged state-cell counts from
unverified transistor count, controller cost and energy. RTL validation
remains future work; functional tests do not prove physical costs.
