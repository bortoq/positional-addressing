/* test_compile — unit-тесты для каждой функции tools/compile.c */
#define _POSIX_C_SOURCE 200809L
#define UNIT_TEST 1
#include "../tools/compile.c"
#include <limits.h>
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
static void call_die(void){ die("x"); }
static void call_die2(void){ die2("x", "y"); }

static void test_parse_long(void){
  int ok=0;
  CHECK(parse_long("42",&ok)==42 && ok, "parse_long 42");
  CHECK(parse_long("0x10",&ok)==16 && ok, "parse_long hex");
  parse_long("99999999999999999999999999999999999", &ok);
  CHECK(!ok, "parse_long overflow");
  CHECK(parse_long("-5",&ok)==-5 && ok, "parse_long negative");
  parse_long("abc",&ok);
  CHECK(!ok, "parse_long junk");
  parse_long("12x",&ok);
  CHECK(!ok, "parse_long trailing");
  parse_long("",&ok);
  CHECK(!ok, "parse_long empty");
  CHECK(parse_long(NULL,&ok)==-1, "parse_long null s");
  int v=parse_long("1",NULL);
  (void)v;
  CHECK(1, "parse_long null ok (no crash)");
}

static void test_encode_vm1(void){
  long out[4];
  CHECK(encode_vm1(8,65504,65512,out)==0, "encode echo");
  CHECK(out[0]==65504 && out[1]==65512 && out[2]==65512 && out[3]==65520, "encode echo words");
  CHECK(encode_vm1(0,100,200,out)==0 && out[0]==100 && out[1]==100 && out[2]==200 && out[3]==200, "encode n=0");
  CHECK(encode_vm1(8,0,100,out)==0 && out[0]==0 && out[1]==8, "encode dst=0");
  CHECK(encode_vm1(1,65534,0,out)==0 && out[1]==65535 && out[2]==0, "encode maximum endpoint and src=0");
  CHECK(encode_vm1(0,0,0,out)==0 && out[0]==out[1] && out[2]==out[3], "encode zero NOP");
  CHECK(encode_vm1(-1,100,200,out)!=0, "encode negative length");
  CHECK(encode_vm1(1,-1,200,out)!=0, "encode negative destination");
  CHECK(encode_vm1(1,100,-1,out)!=0, "encode negative source");
  CHECK(encode_vm1(LONG_MAX,1,2,out)!=0, "encode LONG_MAX length");
  CHECK(encode_vm1(1,LONG_MAX,2,out)!=0, "encode LONG_MAX address");
  CHECK(encode_vm1(0,65536,0,out)!=0, "encode endpoint does not wrap");
  CHECK(encode_vm1(8,65530,100,out)!=0, "encode dst high overflow");
  CHECK(encode_vm1(8,100,65530,out)!=0, "encode src high overflow");
  CHECK(encode_vm1(70000,100,200,out)!=0, "encode n huge overflow");
  CHECK(encode_vm1(8,100,200,NULL)!=0, "encode null out");
}

static void test_trim(void){
  char a[]="  hi  ";
  CHECK(strcmp(trim(a),"hi")==0, "trim spaces");
  char b[]="\t\n";
  CHECK(strcmp(trim(b),"")==0, "trim all-ws");
  CHECK(trim(NULL)==NULL, "trim null");
}

static void test_cli_roundtrip(void){
  // чёрный ящик: copy3 -> vm1 через собранный бинарник
  FILE *f=fopen("/tmp/tc_in.asm","w");
  fputs("8 65504 65512\n# comment\n\n",f);
  fclose(f);
  int rc = system("./tools/compile --in-copy3 /tmp/tc_in.asm --out-vm1 /tmp/tc_out.vm1 >/dev/null");
  CHECK(rc==0, "compile cli ok");
  FILE *o=fopen("/tmp/tc_out.vm1","r");
  long a,b,c,d;
  CHECK(o && fscanf(o,"%ld %ld %ld %ld",&a,&b,&c,&d)==4, "compile output 4 words");
  CHECK(a==65504&&b==65512&&c==65512&&d==65520, "compile output values");
  if(o) fclose(o);
  unlink("/tmp/tc_in.asm");
  unlink("/tmp/tc_out.vm1");
  CHECK(child_exits(call_die,1), "die exits 1");
  CHECK(child_exits(call_die2,1), "die2 exits 1");
}

int main(void){
  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);
  printf("=== test_compile ===\n");
  test_parse_long();
  test_encode_vm1();
  test_trim();
  test_cli_roundtrip();
  printf("=== summary: pass=%d fail=%d ===\n", passes, fails);
  return fails ? 1 : 0;
}
