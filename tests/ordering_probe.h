/* Reuse the unchanged independent snapshot oracle with a candidate cycle. */
#define run_cycle ordering_production_cycle
#include "../src/machine.c"
#undef run_cycle
#define VM1_PRODUCTION_CYCLE ordering_production_cycle
#include "ordering_variants.h"
STATIC void run_cycle(machine *vm)
{
  ordering_cycle(vm, ORDERING_VARIANT);
}
