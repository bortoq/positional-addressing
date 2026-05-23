/*
 * DEMONSTRATION OF PARALLEL ADDRESSATION
 * uses no arithmetic
 *
 * 2024-11-02, dmitri bortoq (bortoq@gmail.com), mit license
 */
#include "vm1.h"

static const uint64_t loader = 0x3fc01fff2000003f; /* src_high src_low dst_high dst_low */
static const uint64_t echo = 0xfff0ffe7ffe8ffdf;   /* CHANNEL_IN CHANNEL_OUT */

/* return 1 if index i already exists in layer */
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

static void network_add_layer(network *n)
{
  network_reserve(n, n->count + 1);
  memset(&n->layers[n->count], 0, sizeof n->layers[n->count]);
  ++n->count;
}

static layer *network_last_layer(network *n)
{
  E(n->count == 0, "internal error: missing network layer");
  return &n->layers[n->count - 1];
}

static void network_push_pair(network *n, idx a, idx b)
{
  layer *l = network_last_layer(n);
  layer_reserve(l, l->count + 1);
  l->pairs[l->count++] = (pair){a, b};
}

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

/* read sorter pair. return 1 if pair read ok */
static int read_pair(FILE *sorter, idx *i, idx *k)
{
  int c = fscanf(sorter, "%hu%hu", i, k);
  return c != 2 || feof(sorter) ? 0 : 1;
}

/* deserialize sorter to a separate network fabric */
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

  if(vm1->fabric.count > 0 && vm1->fabric.layers[vm1->fabric.count - 1].count == 0)
  {
    free(vm1->fabric.layers[vm1->fabric.count - 1].pairs);
    --vm1->fabric.count;
  }
}

/* read one byte from input channel. return 0 if ok */
static int channel_read(machine *vm1)
{
  return fread(&vm1->space[CHANNEL_IN], 1, 1, stdin) == 1 ? 0 : -1;
}

/* write one byte to output channel. return 0 if ok */
static int channel_write(machine *vm1)
{
  return fwrite(&vm1->space[CHANNEL_OUT], 1, 1, stdout) == 1 ? 0 : -1;
}

/* machine hardware construction */
static machine *allocate_machine(int argc, char **argv)
{
  idx i;
  E(argc != 2, HELP);

  machine *vm1 = calloc(1, sizeof *vm1);
  E(vm1 == NULL, "cannot allocate machine memory");

  FILE *sorter = fopen(argv[1], "r");
  E(sorter == NULL, "cannot open sorter");

  /* initialize network */
  sorter_to_network(vm1, sorter);
  fclose(sorter);

  /* initialize indices */
  for(i = 0; i < SPACE_SIZE; ++i)
    vm1->subspace[i].key.origin = i;

  /* HARDCODED: set loader, echo */
#define I (EDGES * WAYS * sizeof(idx))  /* instruction size */
  if(I <= sizeof vm1->space)
    memmove(vm1->space, &loader, I);
  if(PROCESSOR_N * I + I <= sizeof vm1->space)
    memmove(vm1->space + PROCESSOR_N * I, &echo, I);
  return vm1;
}

/* subspace initialization from processor, channels and register */
static void i1(machine *vm1)
{
  idx i;

  /* copy subspace processed data to space and space data to process to subspace */
  for(i = 0; i < SPACE_SIZE; ++i)
    if(vm1->subspace[i].role == 1)
      SETBIT(vm1->space[i / CHAR_BIT], i % CHAR_BIT, vm1->subspace[i].value);
    else
      vm1->subspace[i].value = GETBIT(vm1->space[i / CHAR_BIT], i % CHAR_BIT);

  /* initialize role, active flag, and sortable key */
  for(i = 0; i < SPACE_SIZE; ++i)
  {
    vm1->subspace[i].role = 0;
    vm1->subspace[i].active = 0;
    vm1->subspace[i].key.sort = vm1->subspace[i].key.origin;
  }

  /* initialize search of intervals from processor */
  idx instruction_words = (idx)(sizeof vm1->space / sizeof(idx));
  idx processor_words = PROCESSOR_N * EDGES * WAYS;
  idx words = processor_words < instruction_words ? processor_words : instruction_words;

  for(i = 0; i + EDGES * WAYS <= words; i += EDGES * WAYS)
  {
    /* h/w solution would just be properly conducted wires */
    idx dst_low = ((idx*)vm1->space)[i];
    idx dst_high = ((idx*)vm1->space)[i + 1];
    idx src_low = ((idx*)vm1->space)[i + 2];
    idx src_high = ((idx*)vm1->space)[i + 3];

    /* skip empty instruction */
    if(src_low == src_high)
      continue;

    /* initialize from dst */
    vm1->subspace[i].key.low = dst_low;
    vm1->subspace[i].key.high = dst_high;
    vm1->subspace[i].key.partner = src_low;
    vm1->subspace[i].role = 0;
    vm1->subspace[i].active = 1;

    /* initialize from src */
    vm1->subspace[i + 1].key.low = src_low;
    vm1->subspace[i + 1].key.high = src_high;
    vm1->subspace[i + 1].key.partner = dst_low;
    vm1->subspace[i + 1].role = 1;
    vm1->subspace[i + 1].active = 1;
  }
}

/* copy attributes from position b to position a */
static inline void copy_pos(machine *vm1, idx a, idx b)
{
  vm1->subspace[a].key.low = vm1->subspace[b].key.low;
  vm1->subspace[a].key.high = vm1->subspace[b].key.high;
  vm1->subspace[a].key.partner = vm1->subspace[b].key.partner;
  vm1->subspace[a].role = vm1->subspace[b].role;
  vm1->subspace[a].active = 1;
}

/* move attributes from position b to position a */
static inline void move_pos(machine *vm1, idx a, idx b)
{
  copy_pos(vm1, a, b);
  vm1->subspace[b].active = 0;
  vm1->subspace[b].role = 0;
}

/* intervals creation */
static void f1(machine *vm1, idx i, idx k)
{
  /* up */
  if(vm1->subspace[i].active == 1)
  {
    if(vm1->subspace[i].key.low > i
      && vm1->subspace[i].key.low > k)
    {
      move_pos(vm1, k, i);
    }
    else if(vm1->subspace[i].key.low > i
      && k > vm1->subspace[i].key.low
      && vm1->subspace[i].key.high > k)
    {
      move_pos(vm1, k, i);
      vm1->subspace[k].key.sort = vm1->subspace[i].key.partner;
    }
    else if(i > vm1->subspace[i].key.low
      && vm1->subspace[i].key.high > k)
    {
      copy_pos(vm1, k, i);
      vm1->subspace[k].key.sort = vm1->subspace[i].key.partner;
    }
  }

  /* down */
  else if(vm1->subspace[k].active == 1)
  {
    if(k > vm1->subspace[k].key.high
      && i > vm1->subspace[k].key.high)
    {
      move_pos(vm1, i, k);
    }
    else if(k > vm1->subspace[k].key.high
      && vm1->subspace[k].key.low > i
      && i > vm1->subspace[k].key.high)
    {
      move_pos(vm1, i, k);
      vm1->subspace[i].key.sort = vm1->subspace[k].key.partner;
    }
    else if(vm1->subspace[k].key.high > k
      && i > vm1->subspace[k].key.low)
    {
      copy_pos(vm1, i, k);
      vm1->subspace[i].key.sort = vm1->subspace[k].key.partner;
    }
  }
}

/* move invariant position index to the partner-key field */
static void i2(machine *vm1)
{
  idx i;
  for(i = 0; i < SPACE_SIZE; ++i)
    vm1->subspace[i].key.partner = vm1->subspace[i].key.origin;
}

/* transposition of intervals with copy of values and invariant indices */
static void f2(machine *vm1, idx i, idx k)
{
  /* check if transposition needed */
  if(vm1->subspace[i].key.sort > vm1->subspace[k].key.sort)
  {
    /* transpose sortable keys */
    vm1->subspace[k].key.low = vm1->subspace[i].key.sort;
    vm1->subspace[i].key.low = vm1->subspace[k].key.sort;
    vm1->subspace[i].key.sort = vm1->subspace[i].key.low;
    vm1->subspace[k].key.sort = vm1->subspace[k].key.low;

    /* transpose invariant position indices */
    vm1->subspace[k].key.low = vm1->subspace[i].key.partner;
    vm1->subspace[i].key.low = vm1->subspace[k].key.partner;
    vm1->subspace[i].key.partner = vm1->subspace[i].key.low;
    vm1->subspace[k].key.partner = vm1->subspace[k].key.low;

    /* check if transposition is inside interval */
    if(vm1->subspace[i].active == 0)
      return;

    /* copy value, move role up */
    if(vm1->subspace[i].role == 1)
    {
      vm1->subspace[k].value = vm1->subspace[i].value;
      vm1->subspace[k].role = 1;
      vm1->subspace[i].role = 0;
    }
    /* copy value, move role down */
    else
    {
      vm1->subspace[i].value = vm1->subspace[k].value;
      vm1->subspace[k].role = 0;
      vm1->subspace[i].role = 1;
    }
  }
}

/* mechanism of a computing tact */
static void tact(machine *vm1, void (*f)(machine*, idx, idx))
{
  for(size_t l = 0; l < vm1->fabric.count; ++l)
    for(size_t p = 0; p < vm1->fabric.layers[l].count; ++p)
      f(vm1, vm1->fabric.layers[l].pairs[p].a, vm1->fabric.layers[l].pairs[p].b);
}

int main(int argc, char **argv)
{
  idx t;
  machine *vm1 = allocate_machine(argc, argv);
  void (*ff[])(machine*) = {i1, i2};
  void (*f[])(machine*, idx, idx) = {f1, f2};

  /* initialize processor code before the first channel byte is accepted */
  for(t = 0; t < ARR_SZ(f); ++t)
  {
    ff[t](vm1);
    tact(vm1, f[t]);
  }

  int have_output = 0;

  /* parallel address copy emulation */
  while(channel_read(vm1) == 0)
  {
    for(t = 0; t < ARR_SZ(f); ++t)
    {
      ff[t](vm1);        /* initialize tact */
      tact(vm1, f[t]);   /* perform tact */
    }

    if(have_output && channel_write(vm1) != 0)
      break;
    have_output = 1;
  }

  if(have_output)
  {
    for(int flush = 0; flush < 2; ++flush)
      for(t = 0; t < ARR_SZ(f); ++t)
      {
        ff[t](vm1);
        tact(vm1, f[t]);
      }
    channel_write(vm1);
  }

  network_free(&vm1->fabric);
  free(vm1);
  return 0;
}
