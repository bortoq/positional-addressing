/* Structural checks of the parallel schedule, independent of payload values. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void trace_layer(const char *name);
static void trace_link(unsigned a, unsigned b);
#define VM1_LAYER(name) trace_layer(name)
#define VM1_LINK(a, b) trace_link(a, b)
#define UNIT_TEST
#include "../src/machine.c"

static unsigned char used[SPACE_SIZE];
static uint32_t links[SPACE_SIZE];
static int tracing;
static size_t depth, calls;
static uint64_t schedule;
static const char *current_layer;
static void require(int ok, const char *why)
{
  if (!ok) {
    fprintf(stderr, "FAIL N=%u: %s\n", SPACE_SIZE, why);
    exit(1);
  }
}
static void trace_layer(const char *name)
{
  if (!tracing)
    return;
  depth++;
  memset(used, 0, sizeof used);
  current_layer = name;
  for (; *name; name++)
    schedule = (schedule ^ (unsigned char)*name) * UINT64_C(1099511628211);
}
static void trace_link(unsigned a, unsigned b)
{
  if (!tracing)
    return;
  require(a < SPACE_SIZE && b < SPACE_SIZE, "link bounds");
  require(!used[a] && (a == b || !used[b]), "physical position reused within a sublayer");
  used[a] = used[b] = 1;
  if (a != b && strcmp(current_layer, "descriptor-sort") && strcmp(current_layer, "network") &&
      strcmp(current_layer, "data-origin") && strcmp(current_layer, "data-key")) {
    unsigned d = b - a;
    require(a < b && !(d & (d - 1)), "constructed link must have power-of-two distance");
    require(links[a] & d, "constructed link absent from existing fabric");
  }
  calls++;
  schedule = (schedule ^ a) * UINT64_C(1099511628211);
  schedule = (schedule ^ b) * UINT64_C(1099511628211);
}

static void check_paint(machine *vm, const interval_record *records, unsigned count)
{
  for (unsigned i = 0; i < SPACE_SIZE; i++) {
    interval_put(vm, (idx)i, i < count ? records[i] : empty_interval());
    vm->subspace[i].key.sort = 0;
  }
  sort_intervals(vm);
  distribute_intervals(vm);
  for (unsigned i = 0; i < SPACE_SIZE; i++) {
    idx paired = (idx)i;
    bit source = 0;
    for (unsigned t = 0; t < count; t++)
      if (i >= records[t].low && i < records[t].high) {
        paired = records[t].paired;
        source = records[t].source;
      }
    require(vm->subspace[i].key.sort == paired && vm->subspace[i].role == source,
            "interval painter differs from independent interval scan");
    require(vm->subspace[i].key.origin == i, "painter restores position labels");
  }
}
static void check_helpers(machine *vm)
{
#if SPACE_SIZE <= 256
  /* All two-interval placements in a 16-position window, including adjacency.
   * Reverse input order as well, so sorting/concentration is exercised. */
  for (unsigned a = 0; a < 16; a++)
    for (unsigned b = a + 1; b <= 16; b++)
      for (unsigned c = b; c < 16; c++)
        for (unsigned d = c + 1; d <= 16; d++) {
          interval_record records[] = {{(idx)c, (idx)d, 100, 1},
                                       {(idx)a, (idx)b, 101, 0}};
          check_paint(vm, records, 2);
        }
#endif
  unsigned inputs = (program_word_limit(vm) / 4) * 2;
  interval_record records[256];
  /* Dense tail output, where naive truncation of the padded butterfly loses
   * paths. Actual input capacity is determined by processor and memory. */
  for (unsigned i = 0; i < inputs; i++)
    records[i] = (interval_record){(idx)(SPACE_SIZE - inputs + i),
                                   (idx)(SPACE_SIZE - inputs + i + 1),
                                   (idx)i, (bit)(i & 1)};
  check_paint(vm, records, inputs);
  if (inputs >= 18) {
    for (unsigned i = 0; i < 17; i++)
      records[i] = (interval_record){(idx)i, (idx)(i + 1), (idx)(i + 1), (bit)(i & 1)};
    records[17] = (interval_record){SPACE_SIZE - 1, SPACE_SIZE, 37, 1};
    check_paint(vm, records, 18);
  }
  uint32_t rng = UINT32_C(0x71532026);
  for (unsigned trial = 0; trial < 24; trial++) {
    for (unsigned i = 0; i < inputs; i++) {
      unsigned start = (unsigned)((size_t)SPACE_SIZE * i / inputs);
      unsigned end = (unsigned)((size_t)SPACE_SIZE * (i + 1) / inputs);
      rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
      unsigned lo = start + rng % (end - start);
      rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
      unsigned hi = lo + 1 + rng % (end - lo);
      records[i] = (interval_record){(idx)lo, (idx)hi, (idx)i, (bit)(rng & 1)};
    }
    for (unsigned i = inputs; i > 1; i--) {
      rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
      unsigned j = rng % i;
      interval_record tmp = records[i - 1]; records[i - 1] = records[j]; records[j] = tmp;
    }
    check_paint(vm, records, inputs);
  }

}

static void set_copies(machine *vm, unsigned count)
{
  memset(vm->space, 0, sizeof vm->space);
  memset(vm->subspace, 0, sizeof vm->subspace);
  init_origin_keys(vm);
  for (unsigned slot = 0; slot < count; slot++) {
    idx dst = (idx)(SPACE_SIZE / 2 + slot);
    idx src = (idx)(SPACE_SIZE - 1 - slot);
    write_word(vm, slot * 4, dst);
    write_word(vm, slot * 4 + 1, dst + 1);
    write_word(vm, slot * 4 + 2, src);
    write_word(vm, slot * 4 + 3, src + 1);
  }
}
static void check_schedule(machine *vm)
{
  unsigned maximum = program_word_limit(vm) / 4;
  if (SPACE_SIZE < 224)
    maximum = 0;
  size_t expected_depth = SPACE_SIZE == 65520 && PROCESSOR_N == 128 ? 454 :
                          SPACE_SIZE == 256 || SPACE_SIZE == 240 ? 121 : 0;
  size_t reference_calls = 0;
  uint64_t reference_hash = 0;
  for (unsigned trial = 0; trial < (maximum ? 3u : 1u); trial++) {
    unsigned count = trial == 0 ? 0 : trial == 1 ? 1 : maximum;
    set_copies(vm, count);
    stage1_init(vm);
    seed_intervals_from_program(vm);
    for (unsigned p = 0; p < SPACE_SIZE; p++) {
      idx expected = (idx)p;
      bit source = 0;
      for (unsigned slot = 0; slot < count; slot++) {
        idx dl = read_word(vm, slot * 4), dh = read_word(vm, slot * 4 + 1);
        idx sl = read_word(vm, slot * 4 + 2), sh = read_word(vm, slot * 4 + 3);
        if (p >= dl && p < dh)
          expected = sl;
        if (p >= sl && p < sh) {
          expected = dl;
          source = 1;
        }
      }
      require(vm->subspace[p].key.sort == expected && vm->subspace[p].role == source,
              "collective marking differs from independent interval classification");
      require(vm->subspace[p].key.origin == p, "marking restores physical origin");
    }
    set_copies(vm, count);
    depth = calls = 0;
    schedule = UINT64_C(1469598103934665603);
    tracing = 1;
    run_cycle(vm);
    tracing = 0;
    for (unsigned i = 1; i < SPACE_SIZE; i++) {
      idx prev = vm->subspace[i - 1].key.sort, next = vm->subspace[i].key.sort;
      require(prev <= next && (prev != next ||
                vm->subspace[i - 1].key.partner < vm->subspace[i].key.partner),
              "stable data pass leaves all groups in source-origin order");
    }
    if (expected_depth)
      require(depth == expected_depth, "two-phase cycle: L marking + 2L comparator rounds + routing + 5 local steps");
    require(depth <= 3 * vm->fabric.count + 7 * CEIL_LOG2(SPACE_SIZE) + 5,
            "cycle depth exceeds 3L+7a+5");
    if (!trial) {
      reference_calls = calls;
      reference_hash = schedule;
    } else
      require(calls == reference_calls && schedule == reference_hash,
              "schedule depends on active instruction count");
    printf("parallel N=%u K=%u: layers=%zu links=%zu schedule=%llx\n", SPACE_SIZE, count, depth,
           calls, (unsigned long long)schedule);
  }
}
int main(int argc, char **argv)
{
  require(argc == 2, "network filename required");
  machine *vm = allocate_machine(argc, argv);
  for (size_t l = 0; l < vm->fabric.count; l++)
    for (size_t p = 0; p < vm->fabric.layers[l].count; p++) {
      pair e = vm->fabric.layers[l].pairs[p];
      unsigned d = e.b - e.a;
      if (!(d & (d - 1)))
        links[e.a] |= d;
    }
  check_helpers(vm);
  check_schedule(vm);
  printf("parallel N=%u: PASS; position=%zu key=%zu machine=%zu bytes\n", SPACE_SIZE,
         sizeof vm->subspace[0], sizeof(position_key), sizeof *vm);
  network_free(&vm->fabric);
  free(vm);
  return 0;
}
