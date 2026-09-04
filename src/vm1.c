/*
 * DEMONSTRATION OF PARALLEL ADDRESSATION
 * uses no arithmetic
 *
 * 2024-11-02, dmitri bortoq (bortoq@gmail.com), mit license
 */
#include "vm1.h"
#include "loader.inc"

/* Purpose: detect repeated position index in the current network layer. */
static int last(idx i)
{
  static char used[SPACE_SIZE];
  if(!used[i])
  {
    used[i] = 1;
    return 0;
  }

  memset(used, 0, SPACE_SIZE);
  return 1;
}

/* Purpose: grow one network layer storage to hold requested pair count. */
static void layer_reserve(layer *l, size_t need)
{
  if(need <= l->cap)
    return;

  size_t cap = l->cap ? l->cap : 8;
  while(cap < need)
    cap *= 2;

  pair *pairs = realloc(l->pairs, cap * sizeof *pairs);
  E(pairs == NULL, "cannot allocate network layer");
  l->pairs = pairs;
  l->cap = cap;
}

/* Purpose: grow network layer-array storage to requested layer count. */
static void network_reserve(network *n, size_t need)
{
  if(need <= n->cap)
    return;

  size_t cap = n->cap ? n->cap : 8;
  while(cap < need)
    cap *= 2;

  layer *layers = realloc(n->layers, cap * sizeof *layers);
  E(layers == NULL, "cannot allocate network");
  n->layers = layers;
  n->cap = cap;
}

/* Purpose: append one empty layer to the sorting network. */
static void network_add_layer(network *n)
{
  network_reserve(n, n->count + 1);
  memset(&n->layers[n->count], 0, sizeof n->layers[n->count]);
  ++n->count;
}

/* Purpose: return the most recent network layer. */
static layer *network_last_layer(network *n)
{
  E(n->count == 0, "internal error: missing network layer");
  return &n->layers[n->count - 1];
}

/* Purpose: append one comparator pair to the current network layer. */
static void network_push_pair(network *n, idx a, idx b)
{
  layer *l = network_last_layer(n);
  layer_reserve(l, l->count + 1);
  l->pairs[l->count++] = (pair){a, b};
}

/* Purpose: release all memory that belongs to the sorting network. */
static void network_free(network *n)
{
  if(!n)
    return;

  for(size_t i = 0; i < n->count; ++i)
    free(n->layers[i].pairs);
  free(n->layers);
  n->layers = NULL;
  n->count = 0;
  n->cap = 0;
}

/* Purpose: read one comparator pair from sorter text stream. */
static int read_pair(FILE *sorter, idx *i, idx *k)
{
  int c = fscanf(sorter, "%hu%hu", i, k);
  return c != 2 || feof(sorter) ? 0 : 1;
}

/* Purpose: finalize network by dropping an empty tail layer if present. */
static void trim_empty_tail_layer(network *net)
{
  if(net->count == 0)
    return;

  layer *tail = &net->layers[net->count - 1];
  if(tail->count == 0)
  {
    free(tail->pairs);
    --net->count;
  }
}

/* Purpose: load sorter pairs into layered network fabric representation. */
static void sorter_to_network(machine *vm1, FILE *sorter)
{
  idx l = 0,
      n = 0,
      i,
      k;

  memset(&vm1->fabric, 0, sizeof vm1->fabric);
  network_add_layer(&vm1->fabric);

  while(read_pair(sorter, &i, &k))
  {
    E(i >= SPACE_SIZE || k >= SPACE_SIZE, "sorter pair out of range");

    if(last(i) || last(k))
    {
      n = 0;
      ++l;
      E(l >= LAYER_N, "number of layer of sorter is too big");
      network_add_layer(&vm1->fabric);
    }

    network_push_pair(&vm1->fabric, i, k);
    ++n;
    E(n >= SPACE_SIZE, "number of pairs of layer is too big");
  }

  trim_empty_tail_layer(&vm1->fabric);
}

/* Purpose: read one byte from input channel in address space. */
static int channel_read(machine *vm1)
{
  return fread(&vm1->space[CHANNEL_IN], 1, 1, stdin) == 1 ? 0 : -1;
}

/* Purpose: write one byte to output channel in address space. */
static int channel_write(machine *vm1)
{
  return fwrite(&vm1->space[CHANNEL_OUT], 1, 1, stdout) == 1 ? 0 : -1;
}

/* Read one byte from stdin stream used by channel protocol. */
static int stream_read_u8(uint8_t *out)
{
  int c = fgetc(stdin);
  if(c == EOF)
    return -1;
  *out = (uint8_t)c;
  return 0;
}

/* Read one 16-bit little-endian value from stdin stream. */
static int stream_read_u16le(uint16_t *out)
{
  uint8_t lo, hi;
  if(stream_read_u8(&lo) != 0 || stream_read_u8(&hi) != 0)
    return -1;
  *out = (uint16_t)lo | (uint16_t)((uint16_t)hi << 8);
  return 0;
}

/* Purpose: read one machine word from packed address space bytes. */
static idx read_word(const machine *vm1, idx word)
{
  idx value;
  memcpy(&value, vm1->space + word * sizeof value, sizeof value);
  return value;
}

/* Purpose: write one machine word into packed address space bytes. */
static void write_word(machine *vm1, idx word, idx value)
{
  memcpy(vm1->space + word * sizeof value, &value, sizeof value);
}

/* Optional external loader protocol from stdin:
 *   magic: 'L' 'D' 'R' '1'
 *   u16le word_count
 *   u16le dst_word_base
 *   payload: word_count * u16le words
 * If magic is absent, stdin is rewound when possible and normal channel mode continues.
 */
static void maybe_load_program_from_stdin(machine *vm1)
{
  uint8_t m[4];
  long pos = ftell(stdin);
  if(stream_read_u8(&m[0]) != 0 || stream_read_u8(&m[1]) != 0
     || stream_read_u8(&m[2]) != 0 || stream_read_u8(&m[3]) != 0)
    return;

  if(!(m[0] == 'L' && m[1] == 'D' && m[2] == 'R' && m[3] == '1'))
  {
    if(pos >= 0)
      (void)fseek(stdin, pos, SEEK_SET);
    return;
  }

  uint16_t words = 0, base = 0;
  E(stream_read_u16le(&words) != 0, "loader protocol: missing word_count");
  E(stream_read_u16le(&base) != 0, "loader protocol: missing dst_word_base");

  idx words_cap = (idx)(sizeof vm1->space / sizeof(idx));
  E((idx)base >= words_cap, "loader protocol: dst base out of range");
  E((idx)words > (idx)(words_cap - (idx)base), "loader protocol: payload out of range");

  for(idx i = 0; i < (idx)words; ++i)
  {
    uint16_t w = 0;
    E(stream_read_u16le(&w) != 0, "loader protocol: truncated payload");
    write_word(vm1, (idx)(base + i), (idx)w);
  }
}

/* Purpose: validate command-line arguments for vm1 run mode. */
static void parse_args_or_die(int argc)
{
  E(argc != 2, HELP);
}

/* Purpose: assign invariant origin index to every address-space position. */
static void init_origin_keys(machine *vm1)
{
  for(idx i = 0; i < SPACE_SIZE; ++i)
    vm1->subspace[i].key.origin = i;
}

/* Purpose: copy built-in loader instruction into processor area start. */
static void install_loader(machine *vm1)
{
  idx words_cap = (idx)(sizeof vm1->space / sizeof(idx));
  idx loader_n = (idx)ARR_SZ(loader_words);
  if(loader_n <= words_cap)
    for(idx i = 0; i < loader_n; ++i)
      write_word(vm1, i, loader_words[i]);
}

/* Purpose: allocate and initialize full machine state from input sorter file. */
static machine *allocate_machine(int argc, char **argv)
{
  parse_args_or_die(argc);

  machine *vm1 = calloc(1, sizeof *vm1);
  E(vm1 == NULL, "cannot allocate machine memory");

  FILE *sorter = fopen(argv[1], "r");
  E(sorter == NULL, "cannot open sorter");

  /* initialize network */
  sorter_to_network(vm1, sorter);
  fclose(sorter);

  init_origin_keys(vm1);
  install_loader(vm1);

  return vm1;
}

/* Purpose: commit destination-bit updates and load current bits into subspace. */
static void sync_space_and_subspace(machine *vm1)
{
  for(idx i = 0; i < SPACE_SIZE; ++i)
    if(vm1->subspace[i].role == 1)
      SETBIT(vm1->space[i / CHAR_BIT], i % CHAR_BIT, vm1->subspace[i].value);
    else
      vm1->subspace[i].value = GETBIT(vm1->space[i / CHAR_BIT], i % CHAR_BIT);
}

/* Purpose: clear per-cycle activity flags and restore default sort keys. */
static void reset_stage1_state(machine *vm1)
{
  for(idx i = 0; i < SPACE_SIZE; ++i)
  {
    vm1->subspace[i].role = 0;
    vm1->subspace[i].active = 0;
    vm1->subspace[i].key.sort = vm1->subspace[i].key.origin;
  }
}

/* Purpose: return bounded processor region length in idx words. */
static idx program_word_limit(const machine *vm1)
{
  idx instruction_words = (idx)(sizeof vm1->space / sizeof(idx));
  idx processor_words = PROCESSOR_N * EDGES * WAYS;
  return processor_words < instruction_words ? processor_words : instruction_words;
}

/* Purpose: seed destination and source interval markers for one instruction. */
static void seed_interval_pair(machine *vm1, idx base_word)
{
  idx dst_low = read_word(vm1, base_word);
  idx dst_high = read_word(vm1, base_word + 1);
  idx src_low = read_word(vm1, base_word + 2);
  idx src_high = read_word(vm1, base_word + 3);

  if(src_low == src_high)
    return;

  vm1->subspace[base_word].key.low = dst_low;
  vm1->subspace[base_word].key.high = dst_high;
  vm1->subspace[base_word].key.partner = src_low;
  vm1->subspace[base_word].role = 0;
  vm1->subspace[base_word].active = 1;

  vm1->subspace[base_word + 1].key.low = src_low;
  vm1->subspace[base_word + 1].key.high = src_high;
  vm1->subspace[base_word + 1].key.partner = dst_low;
  vm1->subspace[base_word + 1].role = 1;
  vm1->subspace[base_word + 1].active = 1;
}

/* Purpose: scan processor program area and seed all interval descriptors. */
static void seed_intervals_from_program(machine *vm1)
{
  idx words = program_word_limit(vm1);
  for(idx i = 0; i + EDGES * WAYS <= words; i += EDGES * WAYS)
    seed_interval_pair(vm1, i);
}

/* Purpose: initialize stage 1 of two-stage addressing for one cycle. */
static void stage1_init(machine *vm1)
{
  sync_space_and_subspace(vm1);
  reset_stage1_state(vm1);
  seed_intervals_from_program(vm1);
}

/* Purpose: copy interval metadata from one position to another position. */
static inline void copy_pos(machine *vm1, idx dst, idx src)
{
  vm1->subspace[dst].key.low = vm1->subspace[src].key.low;
  vm1->subspace[dst].key.high = vm1->subspace[src].key.high;
  vm1->subspace[dst].key.partner = vm1->subspace[src].key.partner;
  vm1->subspace[dst].role = vm1->subspace[src].role;
  vm1->subspace[dst].active = 1;
}

/* Purpose: move interval metadata and clear source position activity flags. */
static inline void move_pos(machine *vm1, idx dst, idx src)
{
  copy_pos(vm1, dst, src);
  vm1->subspace[src].active = 0;
  vm1->subspace[src].role = 0;
}

/* Purpose: apply stage 1 propagation rules for active left position. */
static void stage1_from_left(machine *vm1, idx left, idx right)
{
  if(vm1->subspace[left].active == 0)
    return;

  if(vm1->subspace[left].key.low > left
    && vm1->subspace[left].key.low > right)
  {
    move_pos(vm1, right, left);
  }
  else if(vm1->subspace[left].key.low > left
    && right > vm1->subspace[left].key.low
    && vm1->subspace[left].key.high > right)
  {
    move_pos(vm1, right, left);
    vm1->subspace[right].key.sort = vm1->subspace[left].key.partner;
  }
  else if(left > vm1->subspace[left].key.low
    && vm1->subspace[left].key.high > right)
  {
    copy_pos(vm1, right, left);
    vm1->subspace[right].key.sort = vm1->subspace[left].key.partner;
  }
}

/* Purpose: apply stage 1 propagation rules for active right position. */
static void stage1_from_right(machine *vm1, idx left, idx right)
{
  if(vm1->subspace[right].active == 0)
    return;

  if(right > vm1->subspace[right].key.high
    && left > vm1->subspace[right].key.high)
  {
    move_pos(vm1, left, right);
  }
  else if(right > vm1->subspace[right].key.high
    && vm1->subspace[right].key.low > left
    && left > vm1->subspace[right].key.high)
  {
    move_pos(vm1, left, right);
    vm1->subspace[left].key.sort = vm1->subspace[right].key.partner;
  }
  else if(vm1->subspace[right].key.high > right
    && left > vm1->subspace[right].key.low)
  {
    copy_pos(vm1, left, right);
    vm1->subspace[left].key.sort = vm1->subspace[right].key.partner;
  }
}

/* Purpose: apply stage 1 pair operation across one network comparator pair. */
static void stage1_pair(machine *vm1, idx i, idx k)
{
  if(vm1->subspace[i].active == 1)
    stage1_from_left(vm1, i, k);
  else if(vm1->subspace[k].active == 1)
    stage1_from_right(vm1, i, k);
}

/* Purpose: preload partner field with invariant origin index for stage 2. */
static void stage2_init(machine *vm1)
{
  idx i;
  for(i = 0; i < SPACE_SIZE; ++i)
    vm1->subspace[i].key.partner = vm1->subspace[i].key.origin;
}

/* Purpose: swap sort and partner keys between two positions. */
static inline void swap_sort_and_partner(machine *vm1, idx left, idx right)
{
  idx tmp_sort = vm1->subspace[left].key.sort;
  idx tmp_partner = vm1->subspace[left].key.partner;
  vm1->subspace[left].key.sort = vm1->subspace[right].key.sort;
  vm1->subspace[right].key.sort = tmp_sort;
  vm1->subspace[left].key.partner = vm1->subspace[right].key.partner;
  vm1->subspace[right].key.partner = tmp_partner;
}

/* Purpose: move value ownership along swap direction for active interval cells. */
static inline void move_value_and_role(machine *vm1, idx left, idx right)
{
  if(vm1->subspace[left].active == 0)
    return;

  if(vm1->subspace[left].role == 1)
  {
    vm1->subspace[right].value = vm1->subspace[left].value;
    vm1->subspace[right].role = 1;
    vm1->subspace[left].role = 0;
  }
  else
  {
    vm1->subspace[left].value = vm1->subspace[right].value;
    vm1->subspace[right].role = 0;
    vm1->subspace[left].role = 1;
  }
}

/* Purpose: apply stage 2 pair operation across one network comparator pair. */
static void stage2_pair(machine *vm1, idx i, idx k)
{
  if(vm1->subspace[i].key.sort <= vm1->subspace[k].key.sort)
    return;

  swap_sort_and_partner(vm1, i, k);
  move_value_and_role(vm1, i, k);
}

/* Purpose: apply one pair-function to all comparator pairs in all layers. */
static void run_network(machine *vm1, void (*stage_pair)(machine*, idx, idx))
{
  for(size_t l = 0; l < vm1->fabric.count; ++l)
    for(size_t p = 0; p < vm1->fabric.layers[l].count; ++p)
      stage_pair(vm1, vm1->fabric.layers[l].pairs[p].a, vm1->fabric.layers[l].pairs[p].b);
}

/* Purpose: execute one full two-stage addressing cycle. */
static void run_cycle(machine *vm1,
                      void (**stage_init)(machine*),
                      void (**stage_pair)(machine*, idx, idx),
                      idx n)
{
  for(idx t = 0; t < n; ++t)
  {
    stage_init[t](vm1);
    run_network(vm1, stage_pair[t]);
  }
}

/* Purpose: run channel-driven compute loop with bounded cycle count. */
static void run_channels(machine *vm1,
                         void (**stage_init)(machine*),
                         void (**stage_pair)(machine*, idx, idx),
                         idx n)
{
  maybe_load_program_from_stdin(vm1);

  uint64_t life = LIFE;
  while(life-- && channel_read(vm1) == 0)
  {
    run_cycle(vm1, stage_init, stage_pair, n);
    if(channel_write(vm1) != 0)
      break;
  }
}

/* Purpose: build machine, run channel workload, and release all resources. */
int main(int argc, char **argv)
{
  machine *vm1 = allocate_machine(argc, argv);
  /* stage_init[t] sets per-stage state, stage_pair[t] applies one pair op. */
  void (*stage_init[])(machine*) = {stage1_init, stage2_init};
  void (*stage_pair[])(machine*, idx, idx) = {stage1_pair, stage2_pair};

  run_channels(vm1, stage_init, stage_pair, ARR_SZ(stage_pair));

  network_free(&vm1->fabric);
  free(vm1);
  return 0;
}
