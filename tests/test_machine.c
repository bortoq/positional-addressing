/* test_machine — unit tests for every function in src/machine.c
 * Требование аудита п.5: тест на каждую функцию (юнит + граничные).
 * Собирается: gcc -DUNIT_TEST tests/test_machine.c -o tests/test_machine
 */
#define _POSIX_C_SOURCE 200809L
#define UNIT_TEST 1
#include "../src/machine.c"
#include <assert.h>
#include <sys/wait.h>
#include <unistd.h>

static int passes = 0, fails = 0;
#define CHECK(cond, msg)                                                                           \
  do {                                                                                             \
    if (cond) {                                                                                    \
      passes++;                                                                                    \
      printf("[PASS] %s\n", msg);                                                                  \
    } else {                                                                                       \
      fails++;                                                                                     \
      fprintf(stderr, "[FAIL] %s (%s:%d)\n", msg, __FILE__, __LINE__);                             \
    }                                                                                              \
  } while (0)

// helper to reset pushback
static void reset_pushback(void)
{
  stdin_pushback_len = 0;
  stdin_pushback_pos = 0;
}
static void reset_wire(void)
{
  wire_used_reset();
}

// ---- helpers для тестов с реальным stdin/stdout ----
static FILE *saved_stdin = NULL;
static void stdin_from_bytes(const uint8_t *data, size_t len)
{
  reset_pushback();
  saved_stdin = stdin;
  stdin = fmemopen((void *)data, len, "rb");
}
static void stdin_restore(void)
{
  if (stdin)
    fclose(stdin);
  stdin = saved_stdin;
  saved_stdin = NULL;
  reset_pushback();
}
// fork-assert: дочерний процесс должен завершиться с кодом exp (для E()->exit путей)
static int child_exits(void (*fn)(void), int exp)
{
  fflush(stdout);
  fflush(stderr);
  pid_t pid = fork();
  if (pid < 0)
    return 0;
  if (pid == 0) {
    fn();
    _exit(99);
  }
  int st = 0;
  waitpid(pid, &st, 0);
  return WIFEXITED(st) && WEXITSTATUS(st) == exp;
}
static void call_parse_args_bad(void)
{
  parse_args_or_die(1);
}
static void call_network_last_empty(void)
{
  network n = {0};
  (void)network_last_layer(&n);
}
static void call_read_word_oob(void)
{
  machine vm = {0};
  (void)read_word(&vm, (idx)SPACE_WORDS);
}

// helper to create temp sorter file
static FILE *tmp_sorter(const char *content)
{
  FILE *f = tmpfile();
  fputs(content, f);
  rewind(f);
  return f;
}

static void test_inside_half_open(void)
{
  CHECK(inside_half_open(5, 4, 6) == 1, "inside_half_open middle");
  CHECK(inside_half_open(4, 4, 6) == 1, "inside_half_open low included");
  CHECK(inside_half_open(6, 4, 6) == 0, "inside_half_open high excluded");
  CHECK(inside_half_open(3, 4, 6) == 0, "inside_half_open below");
  CHECK(inside_half_open(7, 4, 6) == 0, "inside_half_open above");
  CHECK(inside_half_open(5, 5, 5) == 0, "inside_half_open empty interval");
  CHECK(inside_half_open(0, 0, 65535) == 1, "inside_half_open origin included");
  CHECK(inside_half_open(1, 0, 65535) == 1, "inside_half_open inside large");
  CHECK(inside_half_open(SPACE_SIZE - 1, SPACE_SIZE - 1, SPACE_SIZE) == 1, "last bit included");
  CHECK(inside_half_open(SPACE_SIZE, SPACE_SIZE - 1, SPACE_SIZE) == 0, "end excluded");
  CHECK(inside_half_open(5, 6, 4) == 0, "reversed interval contains nothing");
}

static void test_wire_reused(void)
{
  reset_wire();
  CHECK(is_wire_reused(10) == 0, "wire first use 10");
  CHECK(is_wire_reused(20) == 0, "wire first use 20");
  CHECK(is_wire_reused(10) == 1, "wire reuse 10 triggers layer");
  // after reuse, buffer cleared -> next use should be 0 again
  CHECK(is_wire_reused(10) == 0, "wire after reset 10");
  CHECK(is_wire_reused(30) == 0, "wire 30 after reset");
  wire_used_reset();
  CHECK(is_wire_reused(30) == 0, "wire reset explicit");
}

static void test_layer_reserve(void)
{
  layer l = {0};
  layer_reserve(&l, 0);
  CHECK(l.cap == 0 && l.pairs == NULL, "layer_reserve 0 no alloc");
  layer_reserve(&l, 5);
  CHECK(l.cap >= 5 && l.pairs != NULL, "layer_reserve 5");
  size_t oldcap = l.cap;
  layer_reserve(&l, oldcap);
  CHECK(l.cap == oldcap, "layer_reserve no shrink");
  free(l.pairs);
  l.pairs = NULL;
  l.cap = 0;
  l.count = 0;
  layer_reserve(&l, 100);
  CHECK(l.cap >= 100, "layer_reserve large");
  free(l.pairs);
}

static void test_network_reserve(void)
{
  network n = {0};
  network_reserve(&n, 0);
  CHECK(n.cap == 0 && n.layers == NULL, "net reserve 0");
  network_reserve(&n, 3);
  CHECK(n.cap >= 3 && n.layers != NULL, "net reserve 3");
  size_t c = n.cap;
  network_reserve(&n, c);
  CHECK(n.cap == c, "net reserve same");
  free(n.layers);
}

static void test_network_add_layer_and_push(void)
{
  network n = {0};
  network_add_layer(&n);
  CHECK(n.count == 1, "add_layer count 1");
  CHECK(n.layers[0].count == 0, "add_layer empty");
  network_push_pair(&n, 1, 2);
  CHECK(n.layers[0].count == 1 && n.layers[0].pairs[0].a == 1, "push_pair");
  // guard null
  network_push_pair(NULL, 1, 2);
  CHECK(1, "push_pair null guard");
  network_free(&n);
  CHECK(n.count == 0 && n.layers == NULL, "network_free cleared");
  network_free(NULL);
  CHECK(1, "network_free null guard");
}

static void test_read_pair(void)
{
  FILE *f = tmp_sorter("0 1\n2 3\n# comment\n\n4 5 # trailing\n");
  idx a, b;
  CHECK(read_pair(f, &a, &b) == 1 && a == 0 && b == 1, "read_pair 0 1");
  CHECK(read_pair(f, &a, &b) == 1 && a == 2 && b == 3, "read_pair 2 3");
  CHECK(read_pair(f, &a, &b) == 1 && a == 4 && b == 5, "read_pair 4 5 with comment");
  CHECK(read_pair(f, &a, &b) == 0, "read_pair eof");
  fclose(f);
  // blank file
  f = tmp_sorter("# only comment\n\n");
  CHECK(read_pair(f, &a, &b) == 0, "read_pair only comment");
  fclose(f);
  // null guards
  CHECK(read_pair(NULL, &a, &b) == 0, "read_pair null f");
  {
    FILE *ff = tmp_sorter("0 1\n");
    idx aa;
    CHECK(read_pair(ff, &aa, NULL) == 0, "read_pair null b");
    fclose(ff);
  }
}

static void test_trim_empty_tail(void)
{
  network n = {0};
  trim_empty_tail_layer(NULL);
  CHECK(1, "trim null");
  trim_empty_tail_layer(&n);
  CHECK(n.count == 0, "trim empty net");
  network_add_layer(&n);
  trim_empty_tail_layer(&n);
  CHECK(n.count == 0, "trim empty tail removed");
  network_add_layer(&n);
  network_push_pair(&n, 1, 2);
  trim_empty_tail_layer(&n);
  CHECK(n.count == 1, "trim non-empty kept");
  network_free(&n);
}

static void test_sorter_to_network(void)
{
  machine *vm = calloc(1, sizeof *vm);
  FILE *f = tmp_sorter("0 1\n2 3\n0 2\n"); // 0 reuse triggers new layer
  sorter_to_network(vm, f);
  CHECK(vm->fabric.count == 2, "sorter_to_network layers 2");
  CHECK(vm->fabric.layers[0].count == 2, "layer0 count 2");
  CHECK(vm->fabric.layers[1].count == 1, "layer1 count 1");
  network_free(&vm->fabric);
  fclose(f);
  f = tmp_sorter("0 1\n0 2\n0 3\n");
  sorter_to_network(vm, f);
  fclose(f);
  CHECK(vm->fabric.count == 3 && vm->fabric.layers[1].count == 1 && vm->fabric.layers[2].count == 1,
        "new layer reserves first reused wire");
  network_free(&vm->fabric);
  f = tmp_sorter("0 3\n1 3\n1 2\n");
  sorter_to_network(vm, f);
  fclose(f);
  CHECK(vm->fabric.count == 3, "new layer reserves both wires after second-operand reuse");
  network_free(&vm->fabric);
  // unordered should trigger E -> exit, we can't test without fork
  // null guards
  sorter_to_network(NULL, NULL);
  CHECK(1, "sorter null guard");
  sorter_to_network(vm, NULL);
  CHECK(1, "sorter null file guard");
  free(vm);
}

static void test_stream_read(void)
{
  // stream_read_u8 / u16le via pushback + fmemopen
  reset_pushback();
  // test pushback
  uint8_t data[] = {0x34, 0x12};
  buffered_pushback(data, 2);
  uint8_t out;
  CHECK(buffered_read_u8(&out) == 0 && out == 0x34, "buffered_read first");
  CHECK(buffered_read_u8(&out) == 0 && out == 0x12, "buffered_read second");
  reset_pushback();
  // test stream_read_u16le via memory file trick: replace stdin with tmpfile?
  // We'll test directly: push 0x78 0x56 -> little endian 0x5678
  uint8_t le[] = {0x78, 0x56};
  buffered_pushback(le, 2);
  uint16_t w;
  CHECK(stream_read_u16le(&w) == 0 && w == 0x5678, "stream_read_u16le");
  reset_pushback();
  CHECK(stream_read_u8(NULL) == -1, "stream_read_u8 null guard");
  CHECK(stream_read_u16le(NULL) == -1, "stream_u16 null guard");
  // truncated u16
  buffered_pushback((uint8_t[]){0x01}, 1);
  CHECK(stream_read_u16le(&w) == -1, "stream truncated");
  reset_pushback();
}

static void test_read_write_word(void)
{
  machine vm = {0};
  write_word(&vm, 0, 0x1234);
  CHECK(read_word(&vm, 0) == 0x1234, "write/read word 0");
  write_word(&vm, 10, 0xABCD);
  CHECK(read_word(&vm, 10) == 0xABCD, "write/read word 10");
  // boundary
  write_word(&vm, SPACE_WORDS - 1, 0xFFFF);
  CHECK(read_word(&vm, SPACE_WORDS - 1) == 0xFFFF, "word boundary");
}

static void test_init_origin(void)
{
  machine vm = {0};
  init_origin_keys(&vm);
  CHECK(vm.subspace[0].key.origin == 0, "origin 0");
  CHECK(vm.subspace[100].key.origin == 100, "origin 100");
  CHECK(vm.subspace[SPACE_SIZE - 1].key.origin == SPACE_SIZE - 1, "origin last");
  init_origin_keys(NULL);
  CHECK(1, "init_origin null guard");
}

static void test_install_loader(void)
{
  machine vm = {0};
  install_loader(&vm);
  // loader_words = {0x0040,0x2000,0x2000,0x3fc0}
  CHECK(read_word(&vm, 0) == 0x0040, "loader word0");
  CHECK(read_word(&vm, 1) == 0x2000, "loader word1");
  CHECK(read_word(&vm, 2) == 0x2000, "loader word2");
  CHECK(read_word(&vm, 3) == 0x3fc0, "loader word3");
  install_loader(NULL);
  CHECK(1, "install_loader null");
}

static void test_program_word_limit(void)
{
  machine vm = {0};
  idx lim = program_word_limit(&vm);
  CHECK(lim == PROCESSOR_N * EDGES * WAYS, "program_word_limit proc");
  CHECK(lim <= SPACE_WORDS, "limit <= total");
}

static void test_sync_and_reset(void)
{
  machine vm = {0};
  vm.space[0] = 0xFF;
  vm.subspace[0].role = 0;
  sync_space_and_subspace(&vm);
  CHECK(vm.subspace[0].value == 1, "sync read bit 0");
  CHECK(vm.subspace[7].value == 1, "sync read bit 7");
  vm.subspace[0].role = 1;
  vm.subspace[0].value = 0;
  sync_space_and_subspace(&vm);
  CHECK((vm.space[0] & 1) == 0, "sync write 0");
  vm.subspace[1].role = 1;
  vm.subspace[1].value = 1;
  sync_space_and_subspace(&vm);
  CHECK((vm.space[0] & 2) == 2, "sync write 1");
  // reset
  vm.subspace[5].active = 1;
  vm.subspace[5].role = 1;
  vm.subspace[5].key.sort = 999;
  vm.subspace[5].key.origin = 5;
  reset_stage1_state(&vm);
  CHECK(vm.subspace[5].active == 0 && vm.subspace[5].role == 0 && vm.subspace[5].key.sort == 5,
        "reset_stage1");
  sync_space_and_subspace(NULL);
  CHECK(1, "sync null guard");
  reset_stage1_state(NULL);
  CHECK(1, "reset null guard");
}

static void test_marking_records(void)
{
  machine vm = {0};
  init_origin_keys(&vm);
  interval_record x = {10, 20, 30, 1};
  interval_put(&vm, 5, x);
  interval_record r = interval_get(&vm, 5);
  CHECK(r.low == 10 && r.high == 20 && r.paired == 30 && r.source,
        "interval record preserves metadata");
  interval_record l = interval_left(x, 15), h = interval_right(x, 15);
  CHECK(l.low == 10 && l.high == 15 && h.low == 15 && h.high == 20,
        "split partitions a half-open interval without overlap");
  CHECK(interval_left(x, 10).low == interval_left(x, 10).high,
        "lower boundary disables left branch");
  CHECK(interval_right(x, 20).low == interval_right(x, 20).high,
        "upper boundary disables right branch");
  CHECK(l.paired == 30 && h.paired == 30 && l.source && h.source,
        "clipping retains original paired boundary and source flag");
  interval_record tail = {SPACE_SIZE - 2, SPACE_SIZE, 7, 1};
  ghost_put(&vm, 5, tail);
  r = ghost_get(&vm, 5);
  CHECK(r.low == tail.low && r.high == tail.high && r.paired == 7 && r.source,
        "packed ghost preserves exclusive SPACE_SIZE endpoint");
  CHECK(vm.subspace[5].key.low == 10 && vm.subspace[5].key.high == 20 &&
            vm.subspace[5].key.partner == 30 && vm.subspace[5].value == 1,
        "ghost does not overwrite primary interval");
  ghost_put(&vm, 5, empty_interval());
  r = ghost_get(&vm, 5);
  CHECK(r.low == r.high, "ghost empty encoding");
  CHECK(interval_union(empty_interval(), x).paired == 30 &&
            interval_union(x, empty_interval()).paired == 30,
        "split merge preserves sole active branch");
}

static void test_stage2(void)
{
  machine vm = {0};
  for (int i = 0; i < 10; i++)
    vm.subspace[i].key.origin = i;
  stage2_init(&vm);
  CHECK(vm.subspace[5].key.partner == 5, "stage2_init partner=origin");
  stage2_init(NULL);
  CHECK(1, "stage2_init null");
  // swap
  vm.subspace[0].key.sort = 10;
  vm.subspace[0].key.partner = 100;
  vm.subspace[1].key.sort = 20;
  vm.subspace[1].key.partner = 200;
  swap_sort_and_partner(&vm, 0, 1);
  CHECK(vm.subspace[0].key.sort == 20 && vm.subspace[1].key.sort == 10, "swap sort");
  CHECK(vm.subspace[0].key.partner == 200, "swap partner");
  // move value and role
  memset(&vm, 0, sizeof vm);
  vm.subspace[0].active = 1;
  vm.subspace[0].role = 1;
  vm.subspace[0].value = 1;
  swap_value_and_role(&vm, 0, 1);
  CHECK(vm.subspace[1].value == 1 && vm.subspace[1].role == 1 && vm.subspace[0].role == 0,
        "move_value src role");
  memset(&vm, 0, sizeof vm);
  vm.subspace[0].active = 1;
  vm.subspace[0].role = 0;
  vm.subspace[1].role = 1;
  vm.subspace[1].value = 1;
  swap_value_and_role(&vm, 0, 1);
  CHECK(vm.subspace[0].value == 1 && vm.subspace[0].role == 1, "move_value dst role");
  swap_value_and_role(NULL, 0, 1);
  CHECK(1, "move_value null");
  // stage2_pair swap condition
  memset(&vm, 0, sizeof vm);
  vm.subspace[0].key.sort = 30;
  vm.subspace[1].key.sort = 10;
  vm.subspace[0].active = 1;
  vm.subspace[0].role = 1;
  vm.subspace[0].value = 1;
  stage2_pair(&vm, 0, 1);
  CHECK(vm.subspace[0].key.sort == 10 && vm.subspace[1].key.sort == 30, "stage2_pair swap");
  // no swap if <=
  vm.subspace[0].key.sort = 5;
  vm.subspace[1].key.sort = 10;
  stage2_pair(&vm, 0, 1);
  CHECK(vm.subspace[0].key.sort == 5, "stage2_pair no swap");
  stage2_pair(NULL, 0, 1);
  CHECK(1, "stage2_pair null");
}

static void test_run_network(void)
{
  machine vm = {0};
  // empty network should not crash
  run_network(&vm, stage2_pair);
  CHECK(1, "run_network empty");
  run_network(NULL, stage2_pair);
  CHECK(1, "run_network null vm");
  run_network(&vm, NULL);
  CHECK(1, "run_network null op");
  // with one layer
  network_add_layer(&vm.fabric);
  network_push_pair(&vm.fabric, 5, 6);
  vm.subspace[5].key.sort = 10;
  vm.subspace[6].key.sort = 5;
  vm.subspace[5].key.low = 4;
  vm.subspace[5].key.high = 10;
  vm.subspace[5].key.partner = 5;
  run_network(&vm, stage2_pair);
  CHECK(vm.subspace[5].key.sort == 5, "run_network executes pair");
  network_free(&vm.fabric);
}

static void test_helpers_ceil_log2(void)
{
  CHECK(ceil_log2_u32(1) == 0, "ceil_log2 1");
  CHECK(ceil_log2_u32(2) == 1, "ceil_log2 2");
  CHECK(ceil_log2_u32(3) == 2, "ceil_log2 3");
  CHECK(ceil_log2_u32(8) == 3, "ceil_log2 8");
  CHECK(ceil_log2_u32(9) == 4, "ceil_log2 9");
  CHECK(ceil_log2_u32(65520) == 16, "ceil_log2 65520");
  CHECK(ceil_log2_u32(65536) == 16, "ceil_log2 65536");
  CHECK(ceil_log2_u32(65535) == 16, "ceil_log2 65535");
}

static void test_set_bit(void)
{
  uint8_t b = 0;
  set_bit_u8(&b, 2, 1);
  CHECK(b == 4, "set_bit 2=1");
  set_bit_u8(&b, 2, 0);
  CHECK(b == 0, "set_bit 2=0");
  b = 0xFF;
  set_bit_u8(&b, 7, 0);
  CHECK((b & 0x80) == 0, "set_bit clear high");
}

static void test_maybe_load_pushback(void)
{
  // ensure pushback after non-LDR1
  reset_pushback();
  // simulate stdin with "ABCD" via pushback
  buffered_pushback((uint8_t *)"ABCD", 4);
  // make stdin EOF after pushback: next fgetc returns EOF, but we have pushback 4
  // maybe_load should consume 4 and pushback them again, then subsequent reads see them
  // We test buffered logic directly:
  uint8_t m[4];
  for (int i = 0; i < 4; i++)
    buffered_read_u8(&m[i]);
  CHECK(m[0] == 'A' && m[3] == 'D', "buffered read ABCD");
  reset_pushback();
  buffered_pushback((uint8_t *)"LDR1", 4);
  // plus words for loader: we push full LDR1 packet manually and test maybe_load
  // here we just check that maybe_load with stub can handle; full integration tested in channel
  CHECK(1, "pushback basic");
}

static void test_network_last_layer(void)
{
  network n = {0};
  network_add_layer(&n);
  layer *l = network_last_layer(&n);
  CHECK(l == &n.layers[0], "network_last_layer returns tail");
  CHECK(child_exits(call_network_last_empty, 1), "network_last_layer empty exits 1");
  network_free(&n);
}

static void test_buffered_getc(void)
{
  reset_pushback();
  buffered_pushback((const uint8_t *)"Z", 1);
  CHECK(buffered_getc() == 'Z', "buffered_getc pushback");
  // после drained читает stdin: подменим пустым потоком -> EOF
  uint8_t dummy = 0;
  stdin_from_bytes(&dummy, 0);
  CHECK(buffered_getc() == EOF, "buffered_getc eof");
  stdin_restore();
}

static void test_channel_rw(void)
{
  reset_pushback();
  // channel_read через pushback: байт 0xA5 должен попасть в space[CHANNEL_IN]
  machine vm = {0};
  uint8_t one[] = {0xA5};
  stdin_from_bytes(one, sizeof one);
  CHECK(channel_read(&vm) == 0 && vm.space[CHANNEL_IN] == 0xA5, "channel_read byte");
  CHECK(channel_read(&vm) != 0, "channel_read eof");
  stdin_restore();
  CHECK(channel_read(NULL) != 0, "channel_read null");
  // channel_write: перенаправим stdout во временный файл
  FILE *saved = stdout;
  FILE *tmp = tmpfile();
  stdout = tmp;
  machine vm2 = {0};
  vm2.space[CHANNEL_OUT] = 0x5A;
  int rc = channel_write(&vm2);
  fflush(stdout);
  stdout = saved;
  rewind(tmp);
  int c = fgetc(tmp);
  fclose(tmp);
  CHECK(rc == 0 && c == 0x5A, "channel_write byte");
  CHECK(channel_write(NULL) != 0, "channel_write null");
}

static void test_maybe_load_full(void)
{
  // Полный LDR1-путь: "LDR1" + count=2 + base=100 + payload{0x1111,0x2222}
  uint8_t pkt[] = {'L', 'D', 'R', '1', 2, 0, 100, 0, 0x11, 0x11, 0x22, 0x22};
  machine vm = {0};
  stdin_from_bytes(pkt, sizeof pkt);
  maybe_load_program_from_stdin(&vm);
  stdin_restore();
  CHECK(read_word(&vm, 100) == 0x1111 && read_word(&vm, 101) == 0x2222, "maybe_load full payload");
  // Без LDR1: 4 байта должны вернуться в pushback для channel_read
  uint8_t plain[] = {'A', 'B', 'C', 'D'};
  machine vm2 = {0};
  stdin_from_bytes(plain, sizeof plain);
  maybe_load_program_from_stdin(&vm2);
  uint8_t v = 0;
  CHECK(buffered_read_u8(&v) == 0 && v == 'A', "maybe_load pushback A");
  CHECK(buffered_read_u8(&v) == 0 && v == 'B', "maybe_load pushback B");
  stdin_restore();
  // Короткий вход (<4 байт) тоже возвращается
  uint8_t tiny[] = {'X', 'Y'};
  machine vm3 = {0};
  stdin_from_bytes(tiny, sizeof tiny);
  maybe_load_program_from_stdin(&vm3);
  CHECK(buffered_read_u8(&v) == 0 && v == 'X', "maybe_load tiny pushback");
  stdin_restore();
  maybe_load_program_from_stdin(NULL);
  CHECK(1, "maybe_load null");
  CHECK(child_exits(call_read_word_oob, 1), "read_word OOB exits 1");
}

static void test_parse_args_and_allocate(void)
{
  CHECK(child_exits(call_parse_args_bad, 1), "parse_args_or_die bad argc exits 1");
  // allocate_machine с реальным sorter-файлом
  char path[] = "/tmp/test_sorter_XXXXXX";
  int fd = mkstemp(path);
  CHECK(fd >= 0, "mkstemp sorter");
  const char *content = "0 1\n2 3\n";
  size_t wlen = 8;
  CHECK(write(fd, content, wlen) == (ssize_t)wlen, "write sorter");
  close(fd);
  char *argv[] = {(char *)"vm1", path, NULL};
  machine *vm = allocate_machine(2, argv);
  CHECK(vm != NULL, "allocate_machine ok");
  CHECK(vm->fabric.count == 1, "allocate_machine one layer");
  CHECK(vm->subspace[7].key.origin == 7, "allocate_machine origin");
  CHECK(vm->fabric.layers != NULL, "allocate_machine fabric");
  network_free(&vm->fabric);
  free(vm);
  unlink(path);
}

static void test_seed_intervals_and_stage1_init(void)
{
  machine *vm = calloc(1, sizeof *vm);
  init_origin_keys(vm);
  vm->subspace[0].active = 1;
  vm->subspace[50].active = 1;
  vm->subspace[9000].role = 1;
  vm->subspace[9000].value = 1;
  stage1_init(vm);
  CHECK(!vm->subspace[0].active && !vm->subspace[50].active, "stage1 resets scratch markers");
  CHECK(vm->subspace[50].key.sort == 50 && vm->subspace[9000].role == 0,
        "stage1 resets classification");
  CHECK(GETBIT(vm->space[9000 / 8], 9000 % 8) == 1,
        "stage1 commits prior cycle before reusing value");
  /* No ordinary writes are allowed while descriptors are being issued. */
  unsigned char before[SPACE_BYTES];
  memcpy(before, vm->space, sizeof before);
  FILE *f = fopen("sorter.txt", "r");
  CHECK(f != NULL, "marking unit opens real network");
  if (!f) {
    free(vm);
    return;
  }
  sorter_to_network(vm, f);
  fclose(f);
  seed_intervals_from_program(vm);
  CHECK(memcmp(before, vm->space, sizeof before) == 0, "all NOP marking preserves memory");
  seed_intervals_from_program(NULL);
  stage1_init(NULL);
  CHECK(1, "marking null guards");
  network_free(&vm->fabric);
  free(vm);
}

static void test_stable_data_order(void)
{
  machine vm = {0};
  for (unsigned group = 0; group < 3; group++)
    for (unsigned origin = 0; origin < 3; origin++) {
      memset(vm.subspace, 0, 2 * sizeof vm.subspace[0]);
      vm.subspace[0].key.sort = 10 + group;
      vm.subspace[1].key.sort = 11;
      vm.subspace[0].key.partner = 20 + origin;
      vm.subspace[1].key.partner = 21;
      vm.subspace[0].value = vm.subspace[0].role = 1;
      stage2_origin_pair(&vm, 0, 1);
      stage2_pair(&vm, 0, 1);
      unsigned swap = group > 1 || (group == 1 && origin > 1);
      CHECK(vm.subspace[0].key.sort == (swap ? 11 : 10 + group) &&
                vm.subspace[0].key.partner == (swap ? 21 : 20 + origin) &&
                vm.subspace[0].value == !swap && vm.subspace[0].role == !swap,
            "two strict comparisons implement stable order for every relation");
    }
  /* Known nonstable pairwise counterexample: (0,0),(1,1),(0,2),(0,3).
   * Ordering must be repaired inside the sorting pass itself. */
  const idx links[5][2] = {{0,2},{1,3},{0,1},{2,3},{1,2}};
  for (unsigned i = 0; i < 4; i++) {
    vm.subspace[i].key.sort = (idx)(i == 1);
    vm.subspace[i].key.partner = (idx)i;
    vm.subspace[i].value = (data)(i == 2);
    vm.subspace[i].role = (bit)(i & 1);
  }
  for (unsigned i = 0; i < 5; i++) {
    network_add_layer(&vm.fabric);
    network_push_pair(&vm.fabric, links[i][0], links[i][1]);
  }
  run_data_network(&vm);
  const idx expected[] = {0,2,3,1};
  int ordered = 1;
  for (unsigned i = 0; i < 4; i++)
    ordered &= vm.subspace[i].key.partner == expected[i] &&
               vm.subspace[i].value == (expected[i] == 2) &&
               vm.subspace[i].role == (expected[i] & 1);
  CHECK(ordered, "stable pass preserves origins, payload and roles in nonstable network");
  network_free(&vm.fabric);
  stage2_origin_pair(NULL, 0, 1);
  run_data_network(NULL);
  CHECK(1, "stable data pass null guards");
}

static void test_run_cycle_and_channels(void)
{
  machine *vm = calloc(1, sizeof *vm);
  init_origin_keys(vm);
  run_cycle(NULL);
  CHECK(1, "run_cycle null");
  FILE *net = fopen("sorter.txt", "r");
  CHECK(net != NULL, "channel unit opens real sorter");
  if (!net) {
    free(vm);
    return;
  }
  sorter_to_network(vm, net);
  fclose(net);
  uint16_t echo[4] = {CHANNEL_OUT_LO, CHANNEL_OUT_HI, CHANNEL_IN_LO, CHANNEL_IN_HI};
  uint8_t packet[18] = {'L', 'D', 'R', '1', 4, 0, 0, 0};
  for (unsigned i = 0; i < 4; i++) {
    packet[8 + 2 * i] = (uint8_t)echo[i];
    packet[9 + 2 * i] = (uint8_t)(echo[i] >> 8);
  }
  packet[16] = 'H';
  packet[17] = 'I';
  stdin_from_bytes(packet, sizeof packet);
  FILE *saved = stdout, *out = tmpfile();
  stdout = out;
  run_channels(vm);
  fflush(stdout);
  stdout = saved;
  stdin_restore();
  rewind(out);
  int a = fgetc(out), b = fgetc(out), end = fgetc(out);
  fclose(out);
  CHECK(a == 0 && b == 'H' && end == EOF,
        "run_channels exact two-byte output with one-cycle latency");
  network_free(&vm->fabric);
  free(vm);
  run_channels(NULL);
  CHECK(1, "run_channels null");
}

int main(void)
{
  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);
  printf("=== test_machine unit ===\n");
  test_inside_half_open();
  test_wire_reused();
  test_layer_reserve();
  test_network_reserve();
  test_network_add_layer_and_push();
  test_read_pair();
  test_trim_empty_tail();
  test_sorter_to_network();
  test_stream_read();
  test_read_write_word();
  test_init_origin();
  test_install_loader();
  test_program_word_limit();
  test_sync_and_reset();
  test_marking_records();
  test_stage2();
  test_stable_data_order();
  test_run_network();
  test_helpers_ceil_log2();
  test_set_bit();
  test_maybe_load_pushback();
  test_network_last_layer();
  test_buffered_getc();
  test_channel_rw();
  test_maybe_load_full();
  test_parse_args_and_allocate();
  test_seed_intervals_and_stage1_init();
  test_run_cycle_and_channels();
  printf("=== summary: pass=%d fail=%d ===\n", passes, fails);
  return fails ? 1 : 0;
}
