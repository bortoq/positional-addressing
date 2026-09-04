/*
 * DEMONSTRATION OF PARALLEL ADDRESSATION
 * uses no arithmetic
 *
 * 2024-11-02, dmitri bortoq (bortoq@gmail.com), mit license
 */
#include "vm1.h"

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

/* read sorter pair. return 1 if pair read ok */
static int read_pair(FILE *sorter, idx *i, idx *k)
{
  int c = fscanf(sorter, "%hu%hu", i, k);
  return c != 2 || feof(sorter) ? 0 : 1;
}

/* deserialize sorter to links for each subspace of address space */
static void sorter_to_links(machine *vm1, FILE *sorter)
{
  idx  l = 0,
       n = 0,
       i,
       k;

  while(read_pair(sorter, &i, &k))
  {
    if(last(i) || last(k))
    {
      n = 0;
      ++l;
      E(l >= LAYER_N, "number of layer of sorter is too big");
    }

    vm1->subspace[i].link[l] = k;
    vm1->subspace[i].link0[l] = 1;
    ++n;
    E(n >= SPACE_SIZE, "number of pairs of layer is too big");
  }
}

/* copy src and dst channels data. return 0 if ok */
static int channels(machine *vm1)
{
  return
    (fwrite(&vm1->space[CHANNEL_OUT], 1, 1, stdout) - 1) |
    (fread(&vm1->space[CHANNEL_IN], 1, 1, stdin) - 1);
}

/* machine hardware construction */
static machine *allocate_machine(int argc, char **argv)
{
  idx  i;
  E(argc != 2, HELP);
  machine *vm1 = calloc(1, sizeof *vm1);
  E(vm1 == NULL, "cannot allocate machine memory");
  FILE *sorter = fopen(argv[1], "r");
  E(sorter == NULL, "cannot open sorter");

  /* initialize links */
  sorter_to_links(vm1, sorter);
  fclose(sorter);

  /* initialize indices */
  for(i = 0; i < SPACE_SIZE; ++i)
    vm1->subspace[i].index = i;

  /* HARDCODED: set loader, echo */
#define I (EDGES * WAYS * INDEX_SIZE / CHAR_BIT)  /* instruction size */
  memmove(vm1->space, &loader, I);
  memmove(vm1->space + PROCESSOR_N * I, &echo, I);
  return vm1;
}

/* subspace initialization from processor, channels and register */
static void i1(machine *vm1)
{
  idx i;

  /* copy subspace processed data to space and space data to process to subspace */
  for(i = 0; i < SPACE_SIZE; ++i)
    if(vm1->subspace[i].way == 1)
      SETBIT(vm1->space[i / CHAR_BIT], i % CHAR_BIT, vm1->subspace[i].value);
    else
      vm1->subspace[i].value = GETBIT(vm1->space[i / CHAR_BIT], i % CHAR_BIT);

  /* initialize swap, way, copy, value */
  for(i = 0; i < SPACE_SIZE; ++i)
  {
    vm1->subspace[i].way = 0;
    vm1->subspace[i].copy = 0;
    vm1->subspace[i].swap = vm1->subspace[i].index;    
  }

  /* initialize search of intervals from processor */
  for(i = 0; i < PROCESSOR_N * EDGES * WAYS; i += EDGES * WAYS)
  {
    /* h/w solution would just be properly conducted wires */
    idx dst_low = ((idx*)vm1->space)[i];
    idx dst_high = ((idx*)vm1->space)[i + 1];
    idx src_low = ((idx*)vm1->space)[i + 2];
    idx src_high = ((idx*)vm1->space)[i + 3];

    /* skip empty instruction */
    if(src_low == src_high) continue;

    /* initialize from dst */
    vm1->subspace[i].low = dst_low;
    vm1->subspace[i].high = dst_high;
    vm1->subspace[i].mark = src_low;
    vm1->subspace[i].way = 0;
    vm1->subspace[i].copy = 1;

    /* initialize from src */
    vm1->subspace[i + 1].low = src_low;
    vm1->subspace[i + 1].high = src_high;
    vm1->subspace[i + 1].mark = dst_low;
    vm1->subspace[i + 1].way = 1;
    vm1->subspace[i + 1].copy = 1;
  }
}

/* copy attributes from position b to position a */
static inline void copy_pos(machine *vm1, idx a, idx b)
{
  vm1->subspace[a].low = vm1->subspace[b].low;
  vm1->subspace[a].high = vm1->subspace[b].high;
  vm1->subspace[a].mark = vm1->subspace[b].mark;
  vm1->subspace[a].way = vm1->subspace[b].way;
  vm1->subspace[a].copy = 1;
}

/* move attributes from position b to position a */
static inline void move_pos(machine *vm1, idx a, idx b)
{
  copy_pos(vm1, a, b);
  vm1->subspace[b].copy = 0;
  vm1->subspace[b].way = 0;
}

/* intervals creation */
static void f1(machine *vm1, idx i, idx l)
{
  /* link check */
  if(vm1->subspace[i].link0[l] == 0)
    return;

  /* up */
  if(vm1->subspace[i].copy == 1)
  {
    if(vm1->subspace[i].low > i
      && vm1->subspace[i].low > vm1->subspace[i].link[l])
    {
      move_pos(vm1, vm1->subspace[i].link[l], i);
    }
    else if(vm1->subspace[i].low > i
      && vm1->subspace[i].link[l] > vm1->subspace[i].low
      && vm1->subspace[i].high > vm1->subspace[i].link[l])
    {
      move_pos(vm1, vm1->subspace[i].link[l], i);
      vm1->subspace[vm1->subspace[i].link[l]].swap = vm1->subspace[i].mark;
    }
    else if(i > vm1->subspace[i].low
      && vm1->subspace[i].high > vm1->subspace[i].link[l])
    {
      copy_pos(vm1, vm1->subspace[i].link[l], i);
      vm1->subspace[vm1->subspace[i].link[l]].swap = vm1->subspace[i].mark;
    }
  }

  /* down */
  else if(vm1->subspace[vm1->subspace[i].link[l]].copy == 1)
  {
    if(vm1->subspace[i].link[l] > vm1->subspace[vm1->subspace[i].link[l]].high
      && i > vm1->subspace[vm1->subspace[i].link[l]].high)
    {
      move_pos(vm1, i, vm1->subspace[i].link[l]);
    }
    else if(vm1->subspace[i].link[l] > vm1->subspace[vm1->subspace[i].link[l]].high
      && vm1->subspace[vm1->subspace[i].link[l]].low > i
      && i > vm1->subspace[vm1->subspace[i].link[l]].high)
    {
      move_pos(vm1, i, vm1->subspace[i].link[l]);
      vm1->subspace[i].swap = vm1->subspace[vm1->subspace[i].link[l]].mark;
    }
    else if(vm1->subspace[vm1->subspace[i].link[l]].high > vm1->subspace[i].link[l]
      && i > vm1->subspace[vm1->subspace[i].link[l]].low)
    {
      copy_pos(vm1, i, vm1->subspace[i].link[l]);
      vm1->subspace[i].swap = vm1->subspace[vm1->subspace[i].link[l]].mark;
    }
  }
}

/* move index to mark for each position */
static void i2(machine *vm1)
{
  idx i;
  for(i = 0; i < SPACE_SIZE; ++i)
    vm1->subspace[i].mark = vm1->subspace[i].index;
}

/* transposition of intervals with copy of values and invariant indices */
static void f2(machine *vm1, idx i, idx l)
{
  /* check if transposition exists */
  if(vm1->subspace[i].link0[l] == 0)
    return;

  /* check if transposition needed */
  if(vm1->subspace[i].swap > vm1->subspace[vm1->subspace[i].link[l]].swap)
  {
    /* transpose sorting indices (swaps) */
    vm1->subspace[vm1->subspace[i].link[l]].low = vm1->subspace[i].swap;
    vm1->subspace[i].low = vm1->subspace[vm1->subspace[i].link[l]].swap;
    vm1->subspace[i].swap = vm1->subspace[i].low;
    vm1->subspace[vm1->subspace[i].link[l]].swap
      = vm1->subspace[vm1->subspace[i].link[l]].low;

    /* transpose invariant indices */
    vm1->subspace[vm1->subspace[i].link[l]].low = vm1->subspace[i].mark;
    vm1->subspace[i].low = vm1->subspace[vm1->subspace[i].link[l]].mark;
    vm1->subspace[i].mark = vm1->subspace[i].low;
    vm1->subspace[vm1->subspace[i].link[l]].mark
      = vm1->subspace[vm1->subspace[i].link[l]].low;

    /* check if transposition is inside interval */
    if(vm1->subspace[i].copy == 0)
      return;

    /* copy value, move way up */
    if(vm1->subspace[i].way == 1)
    {
      vm1->subspace[vm1->subspace[i].link[l]].value = vm1->subspace[i].value;
      vm1->subspace[vm1->subspace[i].link[l]].way = 1;
      vm1->subspace[i].way = 0;
    }
    /* copy value, move way down */
    else
    {
      vm1->subspace[i].value = vm1->subspace[vm1->subspace[i].link[l]].value;
      vm1->subspace[vm1->subspace[i].link[l]].way = 0;
      vm1->subspace[i].way = 1;
    }
  }
}

/* TODO: remove when sorter will be stable */
static void i3(machine *vm1)
{
  (void)vm1;
}

/* TODO: remove when sorter will be stable */
/* sort intervals to gradients */
static void f3(machine *vm1, idx i, idx l)
{
  /* check if transposition exists */
  if(vm1->subspace[i].link0[l] == 0)
    return;
  
  /* only sort destination interval (after f2 it has way == 1) */
  if(vm1->subspace[i].way == 0)
    return;

  /* check if transposition is inside same interval (swap == linked swap) */
  if(vm1->subspace[i].swap > vm1->subspace[vm1->subspace[i].link[l]].swap)
    return;
  if(vm1->subspace[vm1->subspace[i].link[l]].swap > vm1->subspace[i].swap)
    return;

  if(vm1->subspace[i].mark > vm1->subspace[vm1->subspace[i].link[l]].mark)
  {
    /* transpose marks */
    vm1->subspace[vm1->subspace[i].link[l]].low = vm1->subspace[i].mark;
    vm1->subspace[i].low = vm1->subspace[vm1->subspace[i].link[l]].mark;
    vm1->subspace[i].mark = vm1->subspace[i].low;
    vm1->subspace[vm1->subspace[i].link[l]].mark
      = vm1->subspace[vm1->subspace[i].link[l]].low;

    /* swap values */
    /*
     * TODO: don't use copy attribute (bit type) as buffer (data type).
     * but since whole function is temporary..
     */
    vm1->subspace[vm1->subspace[i].link[l]].copy = vm1->subspace[i].value;
    vm1->subspace[i].copy = vm1->subspace[vm1->subspace[i].link[l]].value;
    vm1->subspace[i].value = vm1->subspace[i].copy;
    vm1->subspace[vm1->subspace[i].link[l]].value
      = vm1->subspace[vm1->subspace[i].link[l]].copy;
  }
}

/* mechanism of a computing tact */
static void tact(machine *vm1, void (*f)(machine*, idx,  idx))
{
  idx i,
      l;
  for(l = 0; l < LAYER_N; ++l)
    for(i = 0; i < SPACE_SIZE; ++i)
      f(vm1, i, l);
}

int main(int argc, char **argv)
{
  idx t;
  machine *vm1 = allocate_machine(argc, argv);
  void (*ff[])(machine*) = {i1, i2, i3};
  void (*f[])(machine*, idx, idx) = {f1, f2, f3};

  /* parallel address copy emulation */
  while(channels(vm1) == 0)
    for(t = 0; t < ARR_SZ(f); ++t)
    {
      ff[t](vm1);       /* initialize tact */
      tact(vm1, f[t]);  /* perform tact */
    }

  free(vm1);
  return 0;
}
