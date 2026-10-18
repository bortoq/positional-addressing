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
Processor words start at zero, up to `PROCESSOR_N` four-word slots (1–128). Copies
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

The first repair broadcast descriptors sequentially. It passed the functional
oracle but required `2K+2` passes for K copies, violating the parallel-depth
goal. Descriptor sorting and monotone interval splitting now replace that repair;
the corrected record transport remains. The current stable data comparator
now incorporates source-origin ordering and removes the separate third phase. The owner accepted additional
work within phase 1 without new position cells on 2026-10-18. Three phases
mean algorithmic stages, rather than three network passes or FPGA clocks.

The new two-phase mechanism does not assume that the fabric is stable. It
implements stable order explicitly at each data comparator. Existing origin
words and one existing flag suffice. This is a different fix from deleting
the third phase while retaining scalar comparisons, which remains incorrect.

## The two phases

Each position retains `value`, `active`, `role` and five 16-bit key words
`{low,high,partner,sort,origin}`. There are no extra persistent packet arrays.

| State | Phase 1 | Phase 2 |
|---|---|---|
| `low,high,partner` | Primary interval bounds and paired lower bound | Scratch bounds, moving source origin |
| `value,active` | Primary source flag and nonempty flag | Actual bit and origin-comparison scratch flag |
| `sort,origin,role` | Packed ghost bounds, ghost paired bound, ghost source flag | Group key, physical index, destination write flag |

1. **Classify.** Commit the previous result. Each slot independently reads
   its four fixed input words into two descriptors: destination at cell 2s,
   source at cell 2s+1. Empty descriptors have both bounds N; source NOP
   disables both descriptors regardless of destination words. Sort all N
   primary records by lower bound in one pass of the existing fabric.
   Active records now form a consecutive prefix in increasing interval order.
   Split them through a descending-dimension butterfly. A node at fixed
   boundary m sends `[low,min(high,m))` left and `[max(low,m),high)` right,
   discarding empty branches. Original paired boundaries and source flags
   are retained. Final nonempty primary records belong to their own physical
   positions; assign their paired lower bounds as `sort`, otherwise `sort=i`.
   Restore `origin=i`. Actual data stays in packed `space` throughout phase 1.
2. **Stable data ordering.** Load bits from `space`, set `partner=origin`,
   then sort complete `sort,partner,value,role` records by `(sort,partner)`.
   Each fabric layer has TWO comparator rounds. First compare origins and save
   `r = partner_a > partner_b` in the existing `active` flag of a. Then compare
   group keys with operands reversed iff r, and invert the result iff r:
   `swap = r ? !(sort_b > sort_a) : (sort_a > sort_b)`. Both comparisons are
   strict 16-bit comparisons. Swap the complete record once if needed; `active`
   is physical scratch and does not move. Equal groups are therefore ordered by
   original position during this pass. No separate restoration pass remains.
   Equal paired lengths preserve group placement, and origin ordering maps
   source bits onto destination positions in their original relative order.

Only writing roles commit at the next boundary. Processor modifications
therefore affect the following cycle. The existing channel boundary is unchanged.

## Coverage and collision freedom

The relevant broadcast-banyan property is: concentrated active inputs and
monotone disjoint output sets admit conflict-free interval splitting. See
[Tony T. Lee, Nonblocking Copy Networks, sections 3 and 7](https://uspto.report/patent/grant/4813038).
Initial descriptor sorting supplies both conditions; pairwise nonintersection
alone would not supply the required input order or concentration.

At each butterfly stage a record is responsible for the intersection of its
original half-open interval with the output block identified by its processed
address prefix. Splitting partitions this responsibility into disjoint pieces
whose union is the original responsibility. The banyan property ensures that
at most one input has a nonempty contribution to each output. `interval_union`
checks this invariant in the C model. After all a dimensions, blocks are
singletons, so every covered position receives exactly its interval's metadata,
and gaps receive none. No stable sort or arithmetic translation of addresses
is needed; ordering by original source position is built into phase 2.

## Missing wires without new position storage

Let P be the next power of two, H=P/2, and q the configured maximum number
of descriptor inputs (`q<=256`, from at most 128 instruction slots). N may
be smaller than P. Simply deleting padded butterfly wires loses valid paths,
even though their final destinations are below N. For example, concentrated
inputs 0..17 addressing singleton outputs 0..16 and N-1 lose the last output
under naive truncation at N=65520.

A live path preserves the unprocessed suffix of its original input rank
r<q. If its current wire is at or above N while its destination is below N,
its destination block crosses the N boundary and its residual tail length
is less than q. Thus every live missing-wire record's bounds lie in
`[B,N)`, B=`N & ~255`, of length at most 240 (N is a multiple of 16).
Both endpoints can be represented by their low eight bits, with high bits
supplied by the fixed B wires. Zero encodes an empty record. No subtraction
of a program address, extra address bit or stored arithmetic result is needed.
`ghost_put` verifies the bound invariant. If N is a multiple of 256, no live
ghost record can occur with q<=256.

Missing logical wire g is represented in existing cell b=g-H:
`sort` stores the two endpoint bytes, `origin` the paired lower bound, and
`role` the source flag. Its primary descriptor uses the other three words and
two flags. Data remains in `space`, so these fields are available.

Three cases execute a butterfly stage:

- Real/real: process primary records on the existing dimension-d link.
- Ghost/ghost: process packed records on the mirrored dimension-d link.
- Real a / ghost g=a+d: use cells A=a, B=g-H, C=a-H. For d<H,
  `C < N-H <= B < H <= A < N`; different triples are disjoint. Links C–B
  (length d) and C–A (length H) already exist. Spare packed fields in A and C
  stage the operation, while the primary records in B and C remain intact.
  At d=H the real and ghost records share one cell and are processed locally.

The cross-pair operation takes five disjoint sublayers. First keep the primary
left branch, place its right branch U in spare A, retain the ghost left W in
B and put its right V in C. Swap spare A/C, then C/B, then A/C. This leaves
W in A, U in B, V in C. Finally merge the two left contributions in A and the
two right contributions in B, and clear spare A/C. Collision freedom permits
each merge. Each packet fits its assigned fields; local variables in C denote
combinational pair inputs/outputs, not additional persistent registers.

Ghost handling begins only when `0 < N mod d < q` can first make one reachable.
After a dimension for which `N mod d == 0`, all active paths are real again.
These decisions use configuration/wire constants, never active-copy count K.
All origin fields are restored before phase 2. The logical padding therefore
adds neither memory positions nor physical links.

## Exact depth

Let L be the loaded sorting-network depth, a=`ceil(log2 N)`. Let G be the
number of scheduled ghost/ghost layers, C the number of cross-pair dimensions
below H, and T the number (0 or 1) of top-dimension local cross layers.
The exact instrumented depth is

```
D = L + 2L + a + G + 5C + T + 5 <= 3L + 7a + 5 = O(log² N).
```

The five local steps are boundary commit, reset, decoder seeding (including
clearing ghost fields), final classification and payload loading. The no-op
former phase-3 initialization is absent. All schedules depend on N and the
configured slot capacity, but not on active instruction count K.

For N=65520 and 128 slots: L=136, a=16, G=T=0, C=5 (d=256,128,64,32,16):

```
136 + 2*136 + 16 + 5*5 = 449 network/comparison layers
449 + 5 = 454 total scheduled steps
```

N=240/256 with two slots uses `36+2*36+8+5=121` steps. The earlier
collective-join implementation needed 3234 steps; its three-phase replacement
needed 454 coarse steps. The new TWO-phase implementation also reports 454,
now explicitly including two single-comparator rounds per data layer.
A one-step compound comparator could reduce this to 318, but its hardware
cost cannot be treated as one 16-bit comparator. Exactly two data phases do
not imply two hardware clocks or two unmodified full sorting passes.
The [ordering experiment](ordering.md) gives timings, register activity,
comparison-round normalization and the remaining physical limits.

## Network and loading

`src/network.c` constructs a pairwise network on the next power of two,
filters out-of-space pairs and packs remaining links by wire readiness.
The classifier requires its sorting property and existing butterfly links
between indices differing in one bit. An arbitrary sorting network is not automatically
a compatible replacement.

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
independent snapshot-based simultaneous-copy oracle on 240, 256 and 65520 positions.
`make check-parallel` checks classification, existing-link membership, disjoint
sublayers and K-independent schedules. It covers 240, 256, 1040, 32784, 60000
and 65520 positions, including dense tail destinations and random intervals.
The oracle checks every bit, including sources, untouched memory and processor
self-modification. See [the audit](audit.md) for results and reproduction.
[Hardware notes](hardware.md) distinguish unchanged state-cell counts from
unverified transistor count, controller cost and energy. RTL validation
remains future work; functional tests do not prove physical costs.
