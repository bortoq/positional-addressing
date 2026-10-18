/*
 * vm1 — two-phase positional addressing, without arithmetic on keys.
 *
 * Instructions specify half-open [low,high) ranges. All active source and
 * destination ranges of a cycle must be pairwise disjoint and paired lengths
 * equal. Ordinary copies may replace the processor for the following cycle.
 *
 * stage1: decode all slots independently and classify intervals collectively;
 * stage2: sort complete records by (paired lower boundary, source origin),
 * using one strict word comparator in two rounds per sorting-network layer.
 * The network need not be stable. Stage1 sorts descriptors, then splits
 * intervals through a butterfly; absent wires reuse existing scratch words.
 * Actual data remains in space and is loaded for stage2.
 *
 * 2024-2026, dmitri bortoq (bortoq@gmail.com), MIT license
 */
#include "machine.h"

#include "loader.h"

#ifndef UNIT_TEST
#define STATIC static
#else
#define STATIC
#endif

/* ---------- helpers ---------- */

/* half-open membership: low <= pos < high */
STATIC int inside_half_open(idx pos, idx low, idx high)
{
  return pos >= low && pos < high;
}

/* ---------- network storage ---------- */

/* Wire reuse detector for sorter_to_network: returns 1 if wire already used in current layer */
static unsigned char wire_used[SPACE_SIZE];
STATIC int is_wire_reused(idx i)
{
  if (!wire_used[i]) {
    wire_used[i] = 1;
    return 0;
  }
  memset(wire_used, 0, sizeof wire_used);
  return 1;
}
STATIC void wire_used_reset(void)
{
  memset(wire_used, 0, sizeof wire_used);
}

STATIC void layer_reserve(layer *l, size_t need)
{
  if (need <= l->cap) {
    return;
  }
  size_t cap = l->cap ? l->cap : 8;
  while (cap < need) {
    cap *= 2;
  }
  pair *p = realloc(l->pairs, cap * sizeof *p);
  E(p == NULL, "cannot allocate network layer");
  l->pairs = p;
  l->cap = cap;
}

STATIC void network_reserve(network *n, size_t need)
{
  if (need <= n->cap) {
    return;
  }
  size_t cap = n->cap ? n->cap : 8;
  while (cap < need) {
    cap *= 2;
  }
  layer *l = realloc(n->layers, cap * sizeof *l);
  E(l == NULL, "cannot allocate network");
  n->layers = l;
  n->cap = cap;
}

STATIC void network_add_layer(network *n)
{
  network_reserve(n, n->count + 1);
  memset(&n->layers[n->count], 0, sizeof n->layers[n->count]);
  n->count++;
}

STATIC layer *network_last_layer(network *n)
{
  E(n->count == 0, "internal error: missing network layer");
  return &n->layers[n->count - 1];
}

STATIC void network_push_pair(network *n, idx a, idx b)
{
  if (!n) {
    return; /* guard */
  }
  layer *l = network_last_layer(n);
  layer_reserve(l, l->count + 1);
  l->pairs[l->count++] = (pair){a, b};
}

STATIC void network_free(network *n)
{
  if (!n) {
    return;
  }
  for (size_t i = 0; i < n->count; i++) {
    free(n->layers[i].pairs);
  }
  free(n->layers);
  n->layers = NULL;
  n->count = 0;
  n->cap = 0;
}

/* ---------- sorter file → fabric ---------- */

STATIC int read_pair(FILE *f, idx *a, idx *b)
{
  if (!f || !a || !b) {
    return 0;
  }
  char line[256];
  while (fgets(line, sizeof line, f)) {
    char *p = line;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
      p++;
    if (*p == '\0' || *p == '#')
      continue;
    char *hash = strchr(p, '#');
    if (hash)
      *hash = '\0';
    unsigned int av, bv;
    char extra;
    int c = sscanf(p, "%u %u %c", &av, &bv, &extra);
    if (c == 2) {
      if (av > 0xFFFFu || bv > 0xFFFFu)
        return 0;
      *a = (idx)av;
      *b = (idx)bv;
      return 1;
    }
    continue;
  }
  return 0;
}

STATIC void trim_empty_tail_layer(network *net)
{
  if (!net || net->count == 0) {
    return;
  }
  layer *tail = &net->layers[net->count - 1];
  if (tail->count == 0) {
    free(tail->pairs);
    net->count--;
  }
}

/* split pairs into layers by wire reuse (like hardware) */
STATIC void sorter_to_network(machine *vm1, FILE *sorter)
{
  if (!vm1 || !sorter) {
    return;
  }
  idx l = 0, n = 0, a, b;
  wire_used_reset();
  memset(&vm1->fabric, 0, sizeof vm1->fabric);
  network_add_layer(&vm1->fabric);
  while (read_pair(sorter, &a, &b)) {
    E(a >= SPACE_SIZE || b >= SPACE_SIZE, "sorter pair out of range");
    E(a == b, "sorter pair has equal wires");
    E(a > b, "sorter pair unordered (expected a<b)");
    if (is_wire_reused(a) || is_wire_reused(b)) {
      n = 0;
      l++;
      E(l >= (idx)LAYER_N, "number of layer of sorter is too big");
      network_add_layer(&vm1->fabric);
      /* A reuse cleared the bitmap; the first pair of the new layer must
       * itself reserve BOTH wires, including the short-circuited operand. */
      wire_used_reset();
      (void)is_wire_reused(a);
      (void)is_wire_reused(b);
    }
    network_push_pair(&vm1->fabric, a, b);
    n++;
    E(n >= SPACE_SIZE, "number of pairs of layer is too big");
  }
  trim_empty_tail_layer(&vm1->fabric);
}

/* ---------- I/O ---------- */

/* Buffered stdin to handle LDR1 lookahead on non-seekable pipes */
static uint8_t stdin_pushback[8];
static size_t stdin_pushback_len = 0;
static size_t stdin_pushback_pos = 0;
STATIC int buffered_getc(void)
{
  if (stdin_pushback_pos < stdin_pushback_len) {
    return stdin_pushback[stdin_pushback_pos++];
  }
  return fgetc(stdin);
}
STATIC void buffered_pushback(const uint8_t *data, size_t len)
{
  if (len > sizeof stdin_pushback)
    len = sizeof stdin_pushback;
  memcpy(stdin_pushback, data, len);
  stdin_pushback_len = len;
  stdin_pushback_pos = 0;
}
STATIC int buffered_read_u8(uint8_t *out)
{
  if (!out)
    return -1;
  int c = buffered_getc();
  if (c == EOF)
    return -1;
  *out = (uint8_t)c;
  return 0;
}
STATIC int channel_read(machine *vm1)
{
  if (!vm1) {
    return -1;
  }
  uint8_t v;
  if (buffered_read_u8(&v) != 0)
    return -1;
  vm1->space[CHANNEL_IN] = v;
  return 0;
}

STATIC int channel_write(machine *vm1)
{
  if (!vm1) {
    return -1;
  }
  return fwrite(&vm1->space[CHANNEL_OUT], 1, 1, stdout) == 1 ? 0 : -1;
}

STATIC int stream_read_u8(uint8_t *out)
{
  return buffered_read_u8(out);
}

STATIC int stream_read_u16le(uint16_t *out)
{
  if (!out) {
    return -1;
  }
  uint8_t lo, hi;
  if (stream_read_u8(&lo) || stream_read_u8(&hi)) {
    return -1;
  }
  *out = (uint16_t)lo | ((uint16_t)hi << 8);
  return 0;
}

/* ---------- space words ---------- */

STATIC idx read_word(const machine *vm1, idx word)
{
  E(!vm1, "read_word: null machine");
  E((size_t)word >= (size_t)SPACE_WORDS, "read_word out of range");
  idx v;
  memcpy(&v, vm1->space + (size_t)word * sizeof v, sizeof v);
  return v;
}

STATIC void write_word(machine *vm1, idx word, idx value)
{
  E(!vm1, "write_word: null machine");
  E((size_t)word >= (size_t)SPACE_WORDS, "write_word out of range");
  memcpy(vm1->space + (size_t)word * sizeof value, &value, sizeof value);
}

/* LDR1 loader with pushback for non-seekable stdin */
STATIC void maybe_load_program_from_stdin(machine *vm1)
{
  if (!vm1) {
    return;
  }
  uint8_t m[4];
  size_t got = 0;
  for (got = 0; got < 4; got++) {
    if (stream_read_u8(&m[got]) != 0)
      break;
  }
  if (got < 4) {
    if (got > 0)
      buffered_pushback(m, got);
    return;
  }
  if (!(m[0] == 'L' && m[1] == 'D' && m[2] == 'R' && m[3] == '1')) {
    buffered_pushback(m, 4);
    return;
  }
  uint16_t words = 0, base = 0;
  E(stream_read_u16le(&words), "loader protocol: missing word_count");
  E(stream_read_u16le(&base), "loader protocol: missing dst_word_base");

  idx cap = (idx)SPACE_WORDS;
  E((idx)base >= cap, "loader protocol: dst base out of range");
  E((idx)words > (idx)(cap - (idx)base), "loader protocol: payload out of range");

  for (idx i = 0; i < (idx)words; i++) {
    uint16_t w = 0;
    E(stream_read_u16le(&w), "loader protocol: truncated payload");
    write_word(vm1, (idx)(base + i), (idx)w);
  }
}

/* ---------- init ---------- */

STATIC void parse_args_or_die(int argc)
{
  E(argc != 2, HELP);
}

STATIC void init_origin_keys(machine *vm1)
{
  if (!vm1) {
    return;
  }
  for (idx i = 0; i < SPACE_SIZE; i++) {
    vm1->subspace[i].key.origin = i;
  }
}

STATIC void install_loader(machine *vm1)
{
  if (!vm1) {
    return;
  }
  idx cap = (idx)SPACE_WORDS;
  idx n = (idx)ARR_SZ(loader_words);
  if (n > cap) {
    return; /* guard: space too small */
  }
  for (idx i = 0; i < n; i++) {
    write_word(vm1, i, loader_words[i]);
  }
}

static machine *allocate_machine(int argc, char **argv)
{
  parse_args_or_die(argc);
  machine *vm1 = calloc(1, sizeof *vm1);
  E(vm1 == NULL, "cannot allocate machine memory");

  FILE *sorter = fopen(argv[1], "r");
  E(sorter == NULL, "cannot open sorter");

  sorter_to_network(vm1, sorter);
  fclose(sorter);

  init_origin_keys(vm1);
  install_loader(vm1);
  return vm1;
}

/* ---------- addressing: stage1 ---------- */

/* sync: dst owns value -> write to space, else read from space */
STATIC void sync_space_and_subspace(machine *vm1)
{
  if (!vm1) {
    return;
  }
  for (idx i = 0; i < SPACE_SIZE; i++) {
    if (vm1->subspace[i].role == 1) {
      SETBIT(vm1->space[i / CHAR_BIT], i % CHAR_BIT, vm1->subspace[i].value);
    } else
      vm1->subspace[i].value = GETBIT(vm1->space[i / CHAR_BIT], i % CHAR_BIT);
  }
}

STATIC void reset_stage1_state(machine *vm1)
{
  if (!vm1) {
    return;
  }
  for (idx i = 0; i < SPACE_SIZE; i++) {
    vm1->subspace[i].role = 0;
    vm1->subspace[i].active = 0;
    vm1->subspace[i].key.sort = vm1->subspace[i].key.origin;
  }
}

STATIC idx program_word_limit(const machine *vm1)
{
  (void)vm1;
  idx total = (idx)SPACE_WORDS;
  idx proc = PROCESSOR_N * EDGES * WAYS;
  return proc < total ? proc : total;
}

/* Descriptor sorting followed by monotone interval splitting. Packed ghost
 * records reuse sort/origin/role; primary records use low/high/partner/value/
 * active. Real bit payloads remain in space until stage2_init. */
#ifndef VM1_LAYER
#define VM1_LAYER(name) ((void)0)
#endif
#ifndef VM1_LINK
#define VM1_LINK(a, b) ((void)0)
#endif
_Static_assert(PROCESSOR_N >= 1 && PROCESSOR_N <= 128,
               "the hardware processor has at most 128 independent slots");
#define GHOST_BASE ((idx)(SPACE_SIZE & ~255u))

typedef struct {
  idx low, high, paired;
  bit source;
} interval_record;

STATIC interval_record interval_get(const machine *vm, idx i)
{
  return (interval_record){vm->subspace[i].key.low, vm->subspace[i].key.high,
                           vm->subspace[i].key.partner, vm->subspace[i].value};
}
STATIC void interval_put(machine *vm, idx i, interval_record r)
{
  vm->subspace[i].key.low = r.low;
  vm->subspace[i].key.high = r.high;
  vm->subspace[i].key.partner = r.paired;
  vm->subspace[i].value = r.source;
  vm->subspace[i].active = (bit)(r.low < r.high);
}
STATIC interval_record empty_interval(void)
{
  return (interval_record){SPACE_SIZE, SPACE_SIZE, 0, 0};
}
STATIC interval_record ghost_get(const machine *vm, idx i)
{
  idx packed = vm->subspace[i].key.sort;
  if (!packed)
    return empty_interval();
  return (interval_record){(idx)(GHOST_BASE | (packed >> 8)),
                           (idx)(GHOST_BASE | (packed & 255u)),
                           vm->subspace[i].key.origin, vm->subspace[i].role};
}
STATIC void ghost_put(machine *vm, idx i, interval_record r)
{
  if (r.low >= r.high) {
    vm->subspace[i].key.sort = 0;
    return;
  }
  /* For at most 256 sorted descriptor inputs every live ghost interval is
   * contained in [GHOST_BASE, SPACE_SIZE), whose endpoints fit in 8 bits.
   * These bit slices are fixed wiring, not addition/subtraction of keys. */
  E(r.low < GHOST_BASE || r.high > SPACE_SIZE ||
        (r.low & ~255u) != GHOST_BASE || (r.high & ~255u) != GHOST_BASE,
    "ghost interval exceeds fixed packed endpoint window");
  vm->subspace[i].key.sort = (idx)(((r.low & 255u) << 8) | (r.high & 255u));
  vm->subspace[i].key.origin = r.paired;
  vm->subspace[i].role = r.source;
}
STATIC interval_record interval_left(interval_record r, idx split)
{
  if (r.high > split)
    r.high = split;
  return r.low < r.high ? r : empty_interval();
}
STATIC interval_record interval_right(interval_record r, idx split)
{
  if (r.low < split)
    r.low = split;
  return r.low < r.high ? r : empty_interval();
}
STATIC interval_record interval_union(interval_record a, interval_record b)
{
  E(a.low < a.high && b.low < b.high, "collision in monotone interval splitter");
  return a.low < a.high ? a : b;
}
STATIC void interval_split_pair(machine *vm, idx a, idx b, idx split, bit ghost)
{
  VM1_LINK(a, b);
  interval_record x = ghost ? ghost_get(vm, a) : interval_get(vm, a);
  interval_record y = ghost ? ghost_get(vm, b) : interval_get(vm, b);
  interval_record left = interval_union(interval_left(x, split), interval_left(y, split));
  interval_record right = interval_union(interval_right(x, split), interval_right(y, split));
  if (ghost) {
    ghost_put(vm, a, left);
    ghost_put(vm, b, right);
  } else {
    interval_put(vm, a, left);
    interval_put(vm, b, right);
  }
}
STATIC void interval_sort_pair(machine *vm, idx a, idx b)
{
  VM1_LINK(a, b);
  interval_record x = interval_get(vm, a), y = interval_get(vm, b);
  if (x.low > y.low) {
    interval_put(vm, a, y);
    interval_put(vm, b, x);
  }
}
STATIC size_t interval_padded_size(void)
{
  size_t padded = 1;
  while (padded < SPACE_SIZE)
    padded *= 2;
  return padded;
}

/* A real/ghost pair a,g is executed through three physical positions:
 * A=a, B=g-H (ghost storage), C=a-H (free scratch), H=padded/2.
 * B--C and A--C are existing links of lengths d and H. The triples for
 * distinct crossing pairs are disjoint. Five sublayers preserve four
 * outgoing branches without adding a persistent register. */
STATIC void split_crossing_pairs(machine *vm, size_t padded, size_t d)
{
  size_t half = padded / 2;
  if (d == half) {
    /* The top-level absent output and its lower input share one cell. */
    VM1_LAYER("split-cross-local");
    for (size_t g = SPACE_SIZE; g < padded; g++) {
      idx a = (idx)(g - half);
      VM1_LINK(a, a);
      interval_record x = interval_get(vm, a), y = ghost_get(vm, a);
      interval_put(vm, a, interval_union(interval_left(x, (idx)half),
                                          interval_left(y, (idx)half)));
      ghost_put(vm, a, interval_union(interval_right(x, (idx)half),
                                       interval_right(y, (idx)half)));
    }
    return;
  }
  for (unsigned step = 0; step < 5; step++) {
    VM1_LAYER("split-cross");
    for (size_t g = SPACE_SIZE; g < padded; g++) {
      if (!(g & d) || g - d >= SPACE_SIZE)
        continue;
      idx a = (idx)(g - d), b = (idx)(g - half), c = (idx)(g - d - half);
      idx split = (idx)((g & ~(2 * d - 1)) + d);
      if (step == 0) {
        VM1_LINK(a, a);
        interval_record x = interval_get(vm, a);
        interval_put(vm, a, interval_left(x, split));
        ghost_put(vm, a, interval_right(x, split));
        VM1_LINK(c, b);
        interval_record y = ghost_get(vm, b);
        ghost_put(vm, c, interval_right(y, split));
        ghost_put(vm, b, interval_left(y, split));
      } else if (step == 1 || step == 3) {
        VM1_LINK(c, a);
        interval_record x = ghost_get(vm, a), y = ghost_get(vm, c);
        ghost_put(vm, a, y);
        ghost_put(vm, c, x);
      } else if (step == 2) {
        VM1_LINK(c, b);
        interval_record x = ghost_get(vm, c), y = ghost_get(vm, b);
        ghost_put(vm, c, y);
        ghost_put(vm, b, x);
      } else {
        VM1_LINK(a, a);
        interval_put(vm, a, interval_union(interval_get(vm, a), ghost_get(vm, a)));
        ghost_put(vm, a, empty_interval());
        VM1_LINK(c, b);
        ghost_put(vm, b, interval_union(ghost_get(vm, b), ghost_get(vm, c)));
        ghost_put(vm, c, empty_interval());
      }
    }
  }
}

STATIC void stage1_init(machine *vm)
{
  if (!vm)
    return;
  VM1_LAYER("cycle-boundary");
  sync_space_and_subspace(vm);
  VM1_LAYER("mark-reset");
  reset_stage1_state(vm);
}
STATIC void sort_intervals(machine *vm)
{
  for (size_t l = 0; l < vm->fabric.count; l++) {
    VM1_LAYER("descriptor-sort");
    for (size_t p = 0; p < vm->fabric.layers[l].count; p++) {
      pair edge = vm->fabric.layers[l].pairs[p];
      interval_sort_pair(vm, edge.a, edge.b);
    }
  }
}

STATIC void distribute_intervals(machine *vm);

STATIC void seed_intervals_from_program(machine *vm)
{
  if (!vm)
    return;
  VM1_LAYER("descriptor-seed");
  for (idx i = 0; i < SPACE_SIZE; i++) {
    interval_record r = empty_interval();
    /* i is a fixed decoder-cell index, never a computed owner address. */
    if (i < (program_word_limit(vm) / 4) * 2) {
      idx base = (idx)((i / 2) * 4);
      idx sl = read_word(vm, base + 2), sh = read_word(vm, base + 3);
      if (sl != sh) {
        idx dl = read_word(vm, base), dh = read_word(vm, base + 1);
        r = i & 1u ? (interval_record){sl, sh, dl, 1}
                   : (interval_record){dl, dh, sl, 0};
      }
    }
    interval_put(vm, i, r);
    vm->subspace[i].key.sort = 0;
  }
  sort_intervals(vm);
  distribute_intervals(vm);
}

STATIC void distribute_intervals(machine *vm)
{
  size_t padded = interval_padded_size(), half = padded / 2;
  size_t inputs = (program_word_limit(vm) / 4) * 2;
  int ghosts_possible = 0;
  for (size_t d = half; d; d /= 2) {
    VM1_LAYER("split-real");
    for (size_t a = 0; a + d < SPACE_SIZE; a++) {
      if (a & d)
        continue;
      idx split = (idx)((a & ~(2 * d - 1)) + d);
      interval_split_pair(vm, (idx)a, (idx)(a + d), split, 0);
    }
    /* All active paths retain the unprocessed input suffix. No ghost can
     * first appear unless N mod d is a nonzero suffix smaller than inputs. */
    if (SPACE_SIZE % d && SPACE_SIZE % d < inputs)
      ghosts_possible = 1;
    if (ghosts_possible) {
      int cross = 0, internal = 0;
      for (size_t g = SPACE_SIZE; g < padded; g++) {
        if (g & d) {
          if (g - d < SPACE_SIZE)
            cross = 1;
          else
            internal = 1;
        }
      }
      if (internal) {
        VM1_LAYER("split-ghost");
        for (size_t g = SPACE_SIZE; g + d < padded; g++)
          if (!(g & d)) {
            idx split = (idx)((g & ~(2 * d - 1)) + d);
            interval_split_pair(vm, (idx)(g - half), (idx)(g + d - half), split, 1);
          }
      }
      if (cross)
        split_crossing_pairs(vm, padded, d);
      if (!(SPACE_SIZE % d))
        ghosts_possible = 0;
    }
  }
  VM1_LAYER("mark-finish");
  for (idx i = 0; i < SPACE_SIZE; i++) {
    interval_record r = interval_get(vm, i);
    vm->subspace[i].key.sort = inside_half_open(i, r.low, r.high) ? r.paired : i;
    vm->subspace[i].role = (bit)(inside_half_open(i, r.low, r.high) && r.source);
    vm->subspace[i].key.origin = i;
  }
}

/* ---------- addressing: stage2 ---------- */

STATIC void stage2_init(machine *vm1)
{
  if (!vm1) {
    return;
  }
  VM1_LAYER("transpose-init");
  for (idx i = 0; i < SPACE_SIZE; i++) {
    vm1->subspace[i].key.partner = vm1->subspace[i].key.origin;
    vm1->subspace[i].value = GETBIT(vm1->space[i / CHAR_BIT], i % CHAR_BIT);
  }
}

STATIC void swap_sort_and_partner(machine *vm1, idx l, idx r)
{
  idx tmp_sort = vm1->subspace[l].key.sort;
  idx tmp_partner = vm1->subspace[l].key.partner;
  vm1->subspace[l].key.sort = vm1->subspace[r].key.sort;
  vm1->subspace[r].key.sort = tmp_sort;
  vm1->subspace[l].key.partner = vm1->subspace[r].key.partner;
  vm1->subspace[r].key.partner = tmp_partner;
}

STATIC void swap_value_and_role(machine *vm1, idx l, idx r)
{
  if (!vm1)
    return;
  data v = vm1->subspace[l].value;
  bit role = vm1->subspace[l].role;
  vm1->subspace[l].value = vm1->subspace[r].value;
  vm1->subspace[l].role = vm1->subspace[r].role;
  vm1->subspace[r].value = v;
  vm1->subspace[r].role = role;
}

/* One strict word comparator, reused in two rounds per fabric layer.
 * The existing active flag is scratch after interval classification. */
STATIC void stage2_origin_pair(machine *vm1, idx a, idx b)
{
  if (vm1)
    vm1->subspace[a].active =
        (bit)(vm1->subspace[a].key.partner > vm1->subspace[b].key.partner);
}
STATIC void stage2_pair(machine *vm1, idx a, idx b)
{
  if (!vm1)
    return;
  bit reverse = vm1->subspace[a].active;
  idx left = reverse ? vm1->subspace[b].key.sort : vm1->subspace[a].key.sort;
  idx right = reverse ? vm1->subspace[a].key.sort : vm1->subspace[b].key.sort;
  /* If origin_a>origin_b, equality of group keys must also cause a swap:
   * !(key_b>key_a) supplies >= using the SAME strict comparator. */
  if ((left > right) == reverse)
    return;
  swap_sort_and_partner(vm1, a, b);
  swap_value_and_role(vm1, a, b);
}

/* ---------- run ---------- */

static inline void run_network(machine *vm1, void (*pair_op)(machine *, idx, idx))
{
  if (!vm1 || !pair_op) {
    return;
  }
  for (size_t l = 0; l < vm1->fabric.count; l++) {
    VM1_LAYER("network");
    for (size_t p = 0; p < vm1->fabric.layers[l].count; p++) {
      idx a = vm1->fabric.layers[l].pairs[p].a, b = vm1->fabric.layers[l].pairs[p].b;
      VM1_LINK(a, b);
      pair_op(vm1, a, b);
    }
  }
}

STATIC void run_data_network(machine *vm1)
{
  if (!vm1)
    return;
  for (size_t l = 0; l < vm1->fabric.count; l++) {
    VM1_LAYER("data-origin");
    for (size_t p = 0; p < vm1->fabric.layers[l].count; p++) {
      pair edge = vm1->fabric.layers[l].pairs[p];
      VM1_LINK(edge.a, edge.b);
      stage2_origin_pair(vm1, edge.a, edge.b);
    }
    VM1_LAYER("data-key");
    for (size_t p = 0; p < vm1->fabric.layers[l].count; p++) {
      pair edge = vm1->fabric.layers[l].pairs[p];
      VM1_LINK(edge.a, edge.b);
      stage2_pair(vm1, edge.a, edge.b);
    }
  }
}

STATIC void run_cycle(machine *vm1)
{
  if (!vm1)
    return;
  stage1_init(vm1);
  seed_intervals_from_program(vm1);
  stage2_init(vm1);
  run_data_network(vm1);
}

STATIC void run_channels(machine *vm1)
{
  if (!vm1)
    return;
  maybe_load_program_from_stdin(vm1);
  uint64_t life = LIFE;
  while (life-- && channel_read(vm1) == 0) {
    run_cycle(vm1);
    if (channel_write(vm1) != 0)
      break;
  }
}

#ifndef UNIT_TEST
int main(int argc, char **argv)
{
  machine *vm1 = allocate_machine(argc, argv);
  run_channels(vm1);

  network_free(&vm1->fabric);
  free(vm1);
  return 0;
}
#endif
