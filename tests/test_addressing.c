/* Independent simultaneous-copy oracle. No expected result is obtained from
 * descriptors, sorting keys, or engine helpers. Compare every bit, including
 * sources, untouched memory and the processor. */
#define UNIT_TEST 1
#ifndef VM1_MACHINE_SOURCE
#define VM1_MACHINE_SOURCE "../src/machine.c"
#endif
#include VM1_MACHINE_SOURCE

typedef struct {
  unsigned n, dst, src;
} copy;

static unsigned cases, failures;
static uint32_t random_state = 0x19231018;

static uint32_t random_word(void)
{
  random_state ^= random_state << 13;
  random_state ^= random_state >> 17;
  random_state ^= random_state << 5;
  return random_state;
}

static unsigned bit_at(const unsigned char *space, unsigned p)
{
  return (space[p / 8] >> (p % 8)) & 1u;
}

static void put_bit(unsigned char *space, unsigned p, unsigned v)
{
  unsigned mask = 1u << (p % 8);
  space[p / 8] = (space[p / 8] & ~mask) | (v ? mask : 0);
}

static void install(machine *vm, unsigned slot, copy c)
{
#ifdef LEGACY_OPEN
  write_word(vm, 4 * slot, c.dst - 1);
  write_word(vm, 4 * slot + 2, c.src - 1);
#else
  write_word(vm, 4 * slot, c.dst);
  write_word(vm, 4 * slot + 2, c.src);
#endif
  write_word(vm, 4 * slot + 1, c.dst + c.n);
  write_word(vm, 4 * slot + 3, c.src + c.n);
}

static void tick(machine *vm)
{
#ifdef LEGACY_OPEN
  void (*inits[])(machine *) = {stage1_init, stage2_init};
  void (*pairs[])(machine *, idx, idx) = {stage1_pair, stage2_pair};
  run_cycle(vm, inits, pairs, 2);
#else
  run_cycle(vm);
#endif
  /* Existing engine commits at the next sync, before fetching instructions.
   * Observe that boundary explicitly, without changing the channel protocol. */
  sync_space_and_subspace(vm);
}

static void check_tick(machine *vm, const copy *ops, unsigned count, const char *name)
{
  /* Reject invalid test fixtures independently of engine descriptors. */
  unsigned char occupied[SPACE_SIZE] = {0};
  for (unsigned i = 0; i < count; i++) {
    copy c = ops[i];
    if (c.n > SPACE_SIZE || c.src > SPACE_SIZE - c.n || c.dst > SPACE_SIZE - c.n) {
      fprintf(stderr, "invalid oracle fixture bounds: %s\n", name);
      exit(2);
    }
    for (unsigned k = 0; k < c.n; k++) {
      if (occupied[c.src + k] || occupied[c.dst + k] || c.src + k == c.dst + k) {
        fprintf(stderr, "overlapping oracle fixture: %s\n", name);
        exit(2);
      }
      occupied[c.src + k] = occupied[c.dst + k] = 1;
    }
  }
  unsigned char before[SPACE_BYTES], expected[SPACE_BYTES];
  memcpy(before, vm->space, sizeof before);
  memcpy(expected, before, sizeof expected);
  for (unsigned i = 0; i < count; i++) {
    copy c = ops[i];
    for (unsigned k = 0; k < c.n; k++) {
      put_bit(expected, c.dst + k, bit_at(before, c.src + k));
    }
  }
  tick(vm);
  cases++;
  if (memcmp(expected, vm->space, sizeof expected) == 0) {
    return;
  }
  failures++;
  if (failures <= 8) {
    unsigned p = 0;
    while (p < SPACE_SIZE && bit_at(expected, p) == bit_at(vm->space, p))
      p++;
    fprintf(stderr, "FAIL %s: bit %u before=%u expected=%u actual=%u;", name, p, bit_at(before, p),
            bit_at(expected, p), bit_at(vm->space, p));
    for (unsigned i = 0; i < count; i++) {
      fprintf(stderr, " copy(%u,%u,%u)", ops[i].n, ops[i].dst, ops[i].src);
    }
    fputc('\n', stderr);
  }
}

static void prepare(machine *vm)
{
  memset(vm->subspace, 0, sizeof vm->subspace);
  init_origin_keys(vm);
  for (unsigned p = 0; p < SPACE_BYTES; p++)
    vm->space[p] = random_word();
  memset(vm->space, 0, program_word_limit(vm) * sizeof(idx));
}

static void run_case(machine *vm, const copy *ops, unsigned count, const char *name)
{
  prepare(vm);
  for (unsigned i = 0; i < count; i++)
    install(vm, i, ops[i]);
  /* Guarantee a visible change even for length-one cases. */
  for (unsigned i = 0; i < count; i++) {
    if (ops[i].src >= program_word_limit(vm) * sizeof(idx) * 8 &&
        ops[i].dst >= program_word_limit(vm) * sizeof(idx) * 8 && ops[i].n) {
      put_bit(vm->space, ops[i].src, 1);
      put_bit(vm->space, ops[i].dst, 0);
    }
  }
  check_tick(vm, ops, count, name);
}

int main(int argc, char **argv)
{
  (void)allocate_machine; /* Included engine entry point is tested elsewhere. */
  if (argc != 2)
    return 2;
  machine *vm = calloc(1, sizeof *vm);
  if (!vm)
    return 2;
  FILE *f = fopen(argv[1], "r");
  if (!f) {
    free(vm);
    return 2;
  }
  sorter_to_network(vm, f);
  fclose(f);
#ifndef LEGACY_OPEN
  /* A hardware layer must never reuse either wire. Sequential C execution
   * alone could conceal an invalid parallel schedule produced by the loader. */
  unsigned char used[SPACE_SIZE];
  for (size_t layer = 0; layer < vm->fabric.count; layer++) {
    memset(used, 0, sizeof used);
    for (size_t k = 0; k < vm->fabric.layers[layer].count; k++) {
      pair link = vm->fabric.layers[layer].pairs[k];
      if (used[link.a] || used[link.b]) {
        fprintf(stderr, "FAIL: reused wire in hardware layer %zu\n", layer);
        network_free(&vm->fabric);
        free(vm);
        return 1;
      }
      used[link.a] = used[link.b] = 1;
    }
  }
#endif
#ifndef LEGACY_OPEN
  /* Sorting correctness alone does not imply this phase-1 routing property. */
  prepare(vm);
  install(vm, 0, (copy){1, SPACE_SIZE - 1, 0});
  stage1_init(vm);
  (void)seed_interval(vm, 0, 0);
  run_network(vm, stage1_pair);
  for (unsigned i = 0; i < SPACE_SIZE; i++) {
    if (!vm->subspace[i].active) {
      fprintf(stderr, "FAIL: broadcast did not reach position %u\n", i);
      network_free(&vm->fabric);
      free(vm);
      return 1;
    }
  }
#endif
#ifdef LEGACY_OPEN
  copy regression = {1, 16385, 8192};
  run_case(vm, &regression, 1, "pre-migration regression");
#else
  run_case(vm, NULL, 0, "all NOP");
  unsigned base = SPACE_SIZE >= 4096 ? 8192 : 128;
  copy edges[] = {{1, 0, SPACE_SIZE - 1},
                  {1, SPACE_SIZE - 1, 0},
                  {1, base, base + 1},
                  {1, base + 1, base},
                  {8, base, base + 8},
                  {8, base + 8, base},
                  {1, SPACE_SIZE - 1, base},
                  {1, base, SPACE_SIZE - 1},
                  {SPACE_SIZE / 2, 0, SPACE_SIZE / 2},
                  {SPACE_SIZE / 2, SPACE_SIZE / 2, 0}};
  for (unsigned i = 0; i < sizeof edges / sizeof edges[0]; i++) {
    run_case(vm, &edges[i], 1, "boundaries and adjacency");
  }
  if (SPACE_SIZE >= 4096) {
    copy regression = {1, 16385, 8192};
    run_case(vm, &regression, 1, "unaligned singleton");
    for (unsigned i = 0; i < 64; i++) {
      unsigned n = 1 + random_word() % 63;
      copy ops[] = {{n, 8192 + random_word() % 256, 16384 + random_word() % 256},
                    {n, 32768 + random_word() % 256, 49152 + random_word() % 256}};
      run_case(vm, ops, 2, "random disjoint parallel copies");
    }
  } else {
    /* All placements of one nonempty copy in an eight-bit window. */
    for (unsigned n = 1; n <= 4; n++) {
      for (unsigned dst = 0; dst + n <= 8; dst++) {
        for (unsigned src = 0; src + n <= 8; src++) {
          if (dst + n > src && src + n > dst)
            continue;
          copy c = {n, base + dst, base + src};
          run_case(vm, &c, 1, "exhaustive single copy");
        }
      }
    }
    /* All ordered placements of two singleton copies with four distinct bits.
     * This includes adjacent intervals and reversed instruction order. */
    for (unsigned a = 0; a < 8; a++)
      for (unsigned b = 0; b < 8; b++)
        for (unsigned c = 0; c < 8; c++)
          for (unsigned d = 0; d < 8; d++) {
            if (a == b || a == c || a == d || b == c || b == d || c == d)
              continue;
            copy ops[] = {{1, base + a, base + b}, {1, base + c, base + d}};
            run_case(vm, ops, 2, "exhaustive parallel copies");
          }
  }
  if (SPACE_SIZE >= 4096) {
    /* Exercise every processor slot; never share a source or destination. */
    copy ops[PROCESSOR_N];
    for (unsigned i = 0; i < PROCESSOR_N; i++) {
      ops[i] = (copy){7, 8192 + 8 * i, 32768 + 8 * (PROCESSOR_N - 1 - i)};
    }
    run_case(vm, ops, PROCESSOR_N, "all processor slots active");
    /* The marked state and scratch token must not leak between cycles. */
    for (unsigned i = 0; i < 3; i++)
      check_tick(vm, ops, PROCESSOR_N, "repeated full processor cycle");
    /* Physical byte/word boundaries and arbitrary interval lengths. */
    for (unsigned n = 1; n <= 127; n++) {
      copy c = {n, 8192 + (n % 17), 32768 + (n % 23)};
      run_case(vm, &c, 1, "unaligned length sweep");
    }
  }
  /* A complete replacement of slot 0 must only execute on the next tick. */
  prepare(vm);
  copy installer = {64, 0, base};
  copy next = {1, SPACE_SIZE - 1, base + 80};
  install(vm, 0, next);
  memcpy(vm->space + base / 8, vm->space, 8);
  install(vm, 0, installer);
  check_tick(vm, &installer, 1, "self-modification install tick");
  check_tick(vm, &next, 1, "self-modification following tick");
#endif
  network_free(&vm->fabric);
  free(vm);
  printf("addressing oracle (%u positions): %u cases, %u failures\n", (unsigned)SPACE_SIZE, cases,
         failures);
  return failures != 0;
}
