/* Benchmark controls; production uses serial-once. Not a product option. */
#ifndef VM1_PRODUCTION_CYCLE
#define VM1_PRODUCTION_CYCLE run_cycle
#endif
STATIC uint64_t ordering_full_swaps, ordering_restore_swaps, ordering_bit_flips, ordering_flag_flips;
#ifdef VM1_ORDERING_STATS
#define ORDERING_FULL() (ordering_full_swaps++)
#define ORDERING_RESTORE() (ordering_restore_swaps++)
#else
#define ORDERING_FULL() ((void)0)
#define ORDERING_RESTORE() ((void)0)
#endif
static void ordering_swap(machine *vm, idx a, idx b)
{
  ORDERING_FULL();
#ifdef VM1_ORDERING_STATS
  ordering_bit_flips += 2u * ((unsigned)__builtin_popcount(vm->subspace[a].key.sort ^ vm->subspace[b].key.sort) +
                             (unsigned)__builtin_popcount(vm->subspace[a].key.partner ^ vm->subspace[b].key.partner) +
                             (vm->subspace[a].value ^ vm->subspace[b].value) +
                             (vm->subspace[a].role ^ vm->subspace[b].role));
#endif
  swap_sort_and_partner(vm, a, b);
  swap_value_and_role(vm, a, b);
}
static void ordering_primary(machine *vm, idx a, idx b)
{
  if (vm->subspace[a].key.sort > vm->subspace[b].key.sort)
    ordering_swap(vm, a, b);
}
static void ordering_secondary(machine *vm, idx a, idx b)
{
  if (vm->subspace[a].key.partner > vm->subspace[b].key.partner)
    ordering_swap(vm, a, b);
}
static void ordering_lex(machine *vm, idx a, idx b)
{
  idx ka = vm->subspace[a].key.sort, kb = vm->subspace[b].key.sort;
  if (ka > kb || (ka == kb && vm->subspace[a].key.partner > vm->subspace[b].key.partner))
    ordering_swap(vm, a, b);
}
static void ordering_origin_flag(machine *vm, idx a, idx b)
{
  bit greater = (bit)(vm->subspace[a].key.partner > vm->subspace[b].key.partner);
#ifdef VM1_ORDERING_STATS
  ordering_flag_flips += vm->subspace[a].active ^ greater;
#endif
  vm->subspace[a].active = greater;
}
static void ordering_directional_key(machine *vm, idx a, idx b)
{
  bit reverse = vm->subspace[a].active;
  idx left = reverse ? vm->subspace[b].key.sort : vm->subspace[a].key.sort;
  idx right = reverse ? vm->subspace[a].key.sort : vm->subspace[b].key.sort;
  if ((left > right) != reverse)
    ordering_swap(vm, a, b);
}
static void ordering_restore(machine *vm, idx a, idx b)
{
  if (!vm || !vm->subspace[a].role ||
      vm->subspace[a].key.sort > vm->subspace[b].key.sort ||
      vm->subspace[b].key.sort > vm->subspace[a].key.sort ||
      vm->subspace[a].key.partner <= vm->subspace[b].key.partner)
    return;
  ORDERING_RESTORE();
#ifdef VM1_ORDERING_STATS
  ordering_bit_flips += 2u * ((unsigned)__builtin_popcount(vm->subspace[a].key.partner ^ vm->subspace[b].key.partner) +
                             (vm->subspace[a].value ^ vm->subspace[b].value));
#endif
  idx origin = vm->subspace[a].key.partner;
  vm->subspace[a].key.partner = vm->subspace[b].key.partner;
  vm->subspace[b].key.partner = origin;
  data value = vm->subspace[a].value;
  vm->subspace[a].value = vm->subspace[b].value;
  vm->subspace[b].value = value;
}
static void ordering_cycle(machine *vm, unsigned variant)
{
  if (!variant) {
    stage1_init(vm); seed_intervals_from_program(vm); stage2_init(vm);
    run_network(vm, ordering_primary);
    run_network(vm, ordering_restore);
    return;
  }
#ifndef VM1_ORDERING_STATS
  if (variant == 3) {
    VM1_PRODUCTION_CYCLE(vm);
    return;
  }
#endif
  stage1_init(vm); seed_intervals_from_program(vm); stage2_init(vm);
  if (variant == 3) {
    for (size_t l = 0; l < vm->fabric.count; l++) {
      VM1_LAYER("order-origin-flag");
      for (size_t p = 0; p < vm->fabric.layers[l].count; p++) {
        pair edge = vm->fabric.layers[l].pairs[p];
        VM1_LINK(edge.a, edge.b);
        ordering_origin_flag(vm, edge.a, edge.b);
      }
      VM1_LAYER("order-directional-key");
      for (size_t p = 0; p < vm->fabric.layers[l].count; p++) {
        pair edge = vm->fabric.layers[l].pairs[p];
        VM1_LINK(edge.a, edge.b);
        ordering_directional_key(vm, edge.a, edge.b);
      }
    }
  } else if (variant == 4) {
    /* Compiler-control baseline: unchanged three-phase algorithm, direct
     * calls remove indirect per-pair dispatch without changing hardware. */
    for (size_t l = 0; l < vm->fabric.count; l++) {
      VM1_LAYER("network");
      for (size_t p = 0; p < vm->fabric.layers[l].count; p++) {
        pair edge = vm->fabric.layers[l].pairs[p];
        VM1_LINK(edge.a, edge.b);
        ordering_primary(vm, edge.a, edge.b);
      }
    }
    for (size_t l = 0; l < vm->fabric.count; l++) {
      VM1_LAYER("network");
      for (size_t p = 0; p < vm->fabric.layers[l].count; p++) {
        pair edge = vm->fabric.layers[l].pairs[p];
        VM1_LINK(edge.a, edge.b);
        ordering_restore(vm, edge.a, edge.b);
      }
    }
  } else if (variant == 1) {
    run_network(vm, ordering_lex);
  } else if (variant == 2) {
    for (size_t l = 0; l < vm->fabric.count; l++) {
      VM1_LAYER("order-secondary");
      for (size_t p = 0; p < vm->fabric.layers[l].count; p++) {
        pair edge = vm->fabric.layers[l].pairs[p];
        VM1_LINK(edge.a, edge.b);
        ordering_secondary(vm, edge.a, edge.b);
      }
      VM1_LAYER("order-primary");
      for (size_t p = 0; p < vm->fabric.layers[l].count; p++) {
        pair edge = vm->fabric.layers[l].pairs[p];
        VM1_LINK(edge.a, edge.b);
        ordering_primary(vm, edge.a, edge.b);
      }
    }
  }

}
