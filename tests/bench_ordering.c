/* CPU timings and payload-register-write counts are separate builds. No RTL
 * area, FPGA clock, gate depth or joule estimate is inferred from CPU time. */
#define _POSIX_C_SOURCE 200809L
#include <time.h>
#define main ordering_original_cli_main
#include "../src/machine.c"
#undef main
#include "ordering_variants.h"

static const char *const variant_names[] = {"three-phase", "lex", "serial-double", "serial-once", "three-phase-direct"};
static const char *const scenarios[] = {"NOP", "singleton", "long-copy", "64-short", "128-tail", "128-long", "unaligned-copy", "128-unaligned"};
static uint32_t random_state;
static uint32_t random_word(void)
{
  random_state ^= random_state << 13;
  random_state ^= random_state >> 17;
  random_state ^= random_state << 5;
  return random_state;
}
static double cpu_seconds(void)
{
  struct timespec t;
  E(clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t) != 0, "process CPU clock failed");
  return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}
static void prepare_fixture(machine *vm, unsigned scenario, unsigned char *expected)
{
  memset(vm->subspace, 0, sizeof vm->subspace);
  init_origin_keys(vm);
  random_state = UINT32_C(0x19861018);
  for (unsigned p = 0; p < SPACE_BYTES; p++) vm->space[p] = (uint8_t)random_word();
  memset(vm->space, 0, program_word_limit(vm) * sizeof(idx));
  unsigned count = scenario == 0 ? 0 : scenario <= 2 || scenario == 6 ? 1 : scenario == 3 ? 64 : 128;
  unsigned lengths[128], destinations[128], sources[128];
  for (unsigned slot = 0; slot < count; slot++) {
    unsigned n = scenario == 1 ? 1 : scenario == 2 ? 8192 : scenario == 5 ? 128 : scenario == 6 ? 8191 : scenario == 7 ? 63 : 7;
    unsigned dst = 8192 + (scenario == 5 ? 128 : 8) * slot;
    unsigned src = scenario == 4 ? SPACE_SIZE - 8 * count + 8 * slot :
                   32768 + (scenario == 5 ? 128 : 8) * (count - 1 - slot);
    if (scenario == 6 || scenario == 7) {
      dst = 8193 + 64 * slot;
      src = 32773 + 64 * (count - 1 - slot);
    }
    lengths[slot] = n; destinations[slot] = dst; sources[slot] = src;
    write_word(vm, 4 * slot, (idx)dst); write_word(vm, 4 * slot + 1, (idx)(dst + n));
    write_word(vm, 4 * slot + 2, (idx)src); write_word(vm, 4 * slot + 3, (idx)(src + n));
  }
  /* Independent snapshot oracle. No sorting keys or source-origin metadata. */
  memcpy(expected, vm->space, SPACE_BYTES);
  for (unsigned slot = 0; slot < count; slot++)
    for (unsigned k = 0; k < lengths[slot]; k++) {
      unsigned src = sources[slot] + k, dst = destinations[slot] + k;
      unsigned value = (vm->space[src / 8] >> (src % 8)) & 1u;
      unsigned mask = 1u << (dst % 8);
      expected[dst / 8] = (uint8_t)((expected[dst / 8] & ~mask) | (value ? mask : 0));
    }
}
int main(int argc, char **argv)
{
  if (argc != 2) return 2;
  E(SPACE_SIZE != 65520 || PROCESSOR_N != 128, "benchmark fixtures require default configuration");
  machine *vm = calloc(1, sizeof *vm);
  E(!vm, "benchmark allocation failed");
  FILE *f = fopen(argv[1], "r"); E(!f, "cannot open sorter");
  sorter_to_network(vm, f); fclose(f);
#ifdef VM1_ORDERING_STATS
  const unsigned rounds = 1, cycles = 1;
  const char *kind = "writes";
#else
  const unsigned rounds = 7, cycles = 8;
  const char *kind = "timing";
#endif
  fprintf(stderr, "compiler=%s; N=%u; P=%u; clock=process-CPU; warmup=2; rounds=%u; cycles=%u; kind=%s\n",
          __VERSION__, SPACE_SIZE, PROCESSOR_N, rounds, cycles, kind);
  puts("kind,round,scenario,variant,cpu_ms_per_cycle,full_exchanges,restore_exchanges,payload_word_writes,payload_register_bit_flips,flag_bit_flips,memory_hash");
  uint32_t shuffle = UINT32_C(0x20181026);
  for (unsigned round = 0; round < rounds; round++)
    for (unsigned scenario = 0; scenario < 8; scenario++) {
      unsigned order[] = {0,1,2,3,4};
      for (unsigned i = 5; i > 1; i--) {
        shuffle ^= shuffle << 13; shuffle ^= shuffle >> 17; shuffle ^= shuffle << 5;
        unsigned j = shuffle % i, tmp = order[i - 1]; order[i - 1] = order[j]; order[j] = tmp;
      }
      for (unsigned v = 0; v < 5; v++) {
        unsigned variant = order[v];
        unsigned char expected[SPACE_BYTES]; prepare_fixture(vm, scenario, expected);
        ordering_cycle(vm, variant); sync_space_and_subspace(vm);
        E(memcmp(expected, vm->space, SPACE_BYTES), "warmup snapshot oracle mismatch");
        ordering_cycle(vm, variant);
        ordering_full_swaps = ordering_restore_swaps = ordering_bit_flips = ordering_flag_flips = 0;
        double start = cpu_seconds();
        for (unsigned i = 0; i < cycles; i++) ordering_cycle(vm, variant);
        double elapsed = cpu_seconds() - start;
        sync_space_and_subspace(vm);
        E(memcmp(expected, vm->space, SPACE_BYTES), "benchmark snapshot oracle mismatch");
        uint64_t hash = UINT64_C(1469598103934665603);
        for (unsigned i = 0; i < SPACE_BYTES; i++) hash = (hash ^ vm->space[i]) * UINT64_C(1099511628211);
        printf("%s,%u,%s,%s,%.6f,%llu,%llu,%llu,%llu,%llu,%llx\n", kind, round + 1, scenarios[scenario], variant_names[variant],
               elapsed * 1000 / cycles, (unsigned long long)(ordering_full_swaps / cycles),
               (unsigned long long)(ordering_restore_swaps / cycles),
               (unsigned long long)((4 * ordering_full_swaps + 2 * ordering_restore_swaps) / cycles),
               (unsigned long long)(ordering_bit_flips / cycles),
               (unsigned long long)(ordering_flag_flips / cycles), (unsigned long long)hash);
        fflush(stdout);
      }
    }
  network_free(&vm->fabric); free(vm); return 0;
}
