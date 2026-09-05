/* test_network_unit — unit-тесты для каждой функции src/network.c */
#define _POSIX_C_SOURCE 200809L
#define UNIT_TEST 1
#include "../src/network.c"
#include <assert.h>
#include <sys/wait.h>
#include <unistd.h>

static int passes = 0, fails = 0;
#define CHECK(cond, msg) do { \
  if (cond) { passes++; printf("[PASS] %s\n", msg); } \
  else { fails++; fprintf(stderr, "[FAIL] %s (%s:%d)\n", msg, __FILE__, __LINE__); } \
} while(0)

static int child_exits(void (*fn)(void), int exp){
  fflush(stdout);
  fflush(stderr);
  pid_t pid = fork();
  if (pid < 0) return 0;
  if (pid == 0) { fn(); _exit(99); }
  int st = 0;
  waitpid(pid, &st, 0);
  return WIFEXITED(st) && WEXITSTATUS(st) == exp;
}
static void call_die(void){ die("boom"); }
static void call_parse_u32_bad(void){ (void)parse_u32("12x"); }
static void call_parse_u32_empty(void){ (void)parse_u32(""); }
static void call_parse_u32_over(void){ (void)parse_u32("99999999999"); }
static void call_gen_bad0(void){ net_t n = gen_pairwise_pow2(0); (void)n; }
static void call_gen_bad_big(void){ net_t n = gen_pairwise_pow2(70000); (void)n; }

static void test_parse_u32(void){
  CHECK(parse_u32("0")==0, "parse_u32 0");
  CHECK(parse_u32("65520")==65520, "parse_u32 65520");
  CHECK(parse_u32("007")==7, "parse_u32 leading zero");
  CHECK(child_exits(call_parse_u32_bad, 1), "parse_u32 junk exits 1");
  CHECK(child_exits(call_parse_u32_empty, 1), "parse_u32 empty exits 1");
  CHECK(child_exits(call_parse_u32_over, 1), "parse_u32 overflow exits 1");
}

static void test_pow2(void){
  CHECK(is_pow2(1)==1, "is_pow2 1");
  CHECK(is_pow2(2)==1, "is_pow2 2");
  CHECK(is_pow2(3)==0, "is_pow2 3");
  CHECK(is_pow2(65536)==1, "is_pow2 65536");
  CHECK(is_pow2(65520)==0, "is_pow2 65520");
  CHECK(is_pow2(0)==0, "is_pow2 0");
  CHECK(next_pow2(1)==1, "next_pow2 1");
  CHECK(next_pow2(2)==2, "next_pow2 2");
  CHECK(next_pow2(3)==4, "next_pow2 3");
  CHECK(next_pow2(65520)==65536, "next_pow2 65520");
  CHECK(next_pow2(65536)==65536, "next_pow2 65536");
  CHECK(next_pow2(65537)==131072, "next_pow2 65537");
}

static void test_seq(void){
  seq_t s={0};
  seq_add(&s, 5, 5);
  CHECK(s.count==0, "seq_add self-loop skipped");
  seq_add(&s, 1, 2);
  seq_add(&s, 3, 4);
  CHECK(s.count==2 && s.pair[0].left==1 && s.pair[1].right==4, "seq_add two");
  for(uint32_t i=0;i<200;i++) seq_add(&s, i, i+1);
  CHECK(s.count==202 && s.cap>=202, "seq_add growth");
  seq_free(&s);
  CHECK(s.pair==NULL && s.count==0 && s.cap==0, "seq_free cleared");
  seq_free(&s); CHECK(1, "seq_free double");
}

static void test_net_helpers(void){
  net_t n={0};
  net_reserve(&n, 0);
  CHECK(n.cap==0 && n.layer==NULL, "net_reserve 0");
  net_reserve(&n, 3);
  CHECK(n.cap>=3 && n.layer!=NULL, "net_reserve 3");
  net_ensure_layer(&n, 5);
  CHECK(n.layers>=6, "net_ensure_layer grows");
  net_add(&n, 0, 7, 7);
  CHECK(n.layer[0].count==0, "net_add self-loop skipped");
  net_add(&n, 0, 1, 2);
  net_add(&n, 0, 3, 4);
  CHECK(n.layer[0].count==2, "net_add two");
  for(uint32_t i=0;i<50;i++) net_add(&n, 1, i, i+1);
  CHECK(n.layer[1].count==50, "net_add layer growth");
  net_free(&n);
  CHECK(n.layer==NULL && n.layers==0 && n.cap==0, "net_free cleared");
  net_free(NULL); CHECK(1, "net_free null");
  CHECK(child_exits(call_die, 1), "die exits 1");
}

/* прогнать сеть как сортировку */
static int run_net(const net_t *net, int *arr, uint32_t n){
  for(size_t l=0;l<net->layers;l++)
    for(size_t k=0;k<net->layer[l].count;k++){
      uint32_t a=net->layer[l].pair[k].left, b=net->layer[l].pair[k].right;
      if(a>=n || b>=n) return -1;
      if(arr[a]>arr[b]){ int t=arr[a]; arr[a]=arr[b]; arr[b]=t; }
    }
  return 0;
}
static int is_sorted(const int *arr, uint32_t n){
  for(uint32_t i=1;i<n;i++) if(arr[i-1]>arr[i]) return 0;
  return 1;
}

static void test_gen_pairwise(void){
  // n=4: все 24 перестановки должны сортироваться
  net_t n4 = gen_pairwise_pow2(4);
  int ok=1;
  // явный перебор всех 24 перестановок
  int cnt=0;
  int p[4];
  for(p[0]=0;p[0]<4;p[0]++) for(p[1]=0;p[1]<4;p[1]++) for(p[2]=0;p[2]<4;p[2]++) for(p[3]=0;p[3]<4;p[3]++){
    int seen[4]={0}, dup=0;
    for(int i=0;i<4;i++){ if(seen[p[i]]) dup=1; seen[p[i]]=1; }
    if(dup) continue;
    int a[4]; for(int i=0;i<4;i++) a[i]=p[i];
    run_net(&n4, a, 4);
    if(!is_sorted(a,4)) ok=0;
    cnt++;
  }
  CHECK(cnt==24 && ok, "gen_pairwise n=4 sorts all perms");
  // инварианты пар
  int inv=1;
  for(size_t l=0;l<n4.layers;l++) for(size_t k=0;k<n4.layer[l].count;k++){
    uint32_t a=n4.layer[l].pair[k].left, b=n4.layer[l].pair[k].right;
    if(a==b || a>b || a>=4 || b>=4) inv=0;
  }
  CHECK(inv, "gen_pairwise pairs ordered a<b");
  net_free(&n4);
  // n=8: reverse + random выборка
  net_t n8 = gen_pairwise_pow2(8);
  int rev[8]={7,6,5,4,3,2,1,0};
  run_net(&n8, rev, 8);
  CHECK(is_sorted(rev,8), "gen_pairwise n=8 reverse");
  int rnd[8]={3,7,1,5,0,6,2,4};
  run_net(&n8, rnd, 8);
  CHECK(is_sorted(rnd,8), "gen_pairwise n=8 shuffled");
  net_free(&n8);
  CHECK(child_exits(call_gen_bad0, 1), "gen n=0 exits 1");
  CHECK(child_exits(call_gen_bad_big, 1), "gen n>65536 exits 1");
}

static void test_seq_pack(void){
  // seq_pack сохраняет все пары и не допускает конфликта wire в слое
  seq_t s={0};
  seq_add(&s, 0, 1); seq_add(&s, 2, 3); seq_add(&s, 0, 2); seq_add(&s, 1, 3);
  net_t packed = seq_pack(&s, 4);
  size_t total=0;
  for(size_t l=0;l<packed.layers;l++) total+=packed.layer[l].count;
  CHECK(total==s.count, "seq_pack preserves count");
  int ok=1;
  for(size_t l=0;l<packed.layers && ok;l++){
    uint8_t used[4]={0};
    for(size_t k=0;k<packed.layer[l].count && ok;k++){
      uint32_t a=packed.layer[l].pair[k].left, b=packed.layer[l].pair[k].right;
      if(a>=4||b>=4||used[a]||used[b]) ok=0;
      used[a]=used[b]=1;
    }
  }
  CHECK(ok, "seq_pack no wire conflict");
  // упакованная сеть всё ещё сортирует (та же последовательность сравнений)
  int a[4]={3,2,1,0};
  run_net(&packed, a, 4);
  CHECK(is_sorted(a,4), "seq_pack network sorts");
  net_free(&packed);
  seq_free(&s);
  // пустая seq
  seq_t e={0};
  net_t pe = seq_pack(&e, 4);
  CHECK(pe.layers==0, "seq_pack empty");
  net_free(&pe);
}

static void test_net_print(void){
  net_t n={0};
  net_add(&n, 0, 1, 2);
  net_add(&n, 1, 3, 4);
  FILE *saved = stdout;
  FILE *tmp = tmpfile();
  stdout = tmp;
  net_print(&n);
  fflush(stdout);
  stdout = saved;
  rewind(tmp);
  char buf[64]={0};
  size_t r = fread(buf, 1, sizeof buf - 1, tmp);
  fclose(tmp);
  CHECK(r>0 && strstr(buf,"1 2")!=NULL && strstr(buf,"3 4")!=NULL, "net_print output");
  net_free(&n);
}

int main(void){
  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);
  printf("=== test_network_unit ===\n");
  test_parse_u32();
  test_pow2();
  test_seq();
  test_net_helpers();
  test_gen_pairwise();
  test_seq_pack();
  test_net_print();
  printf("=== summary: pass=%d fail=%d ===\n", passes, fails);
  return fails ? 1 : 0;
}
