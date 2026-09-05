/* test_loader — unit-тесты для каждой функции tools/loader.c */
#define _POSIX_C_SOURCE 200809L
#define UNIT_TEST 1
#include "../tools/loader.c"
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
static void call_parse_null(void){ parse_vm1_file(NULL, NULL); }
static void call_parse_missing(void){ size_t n=0; (void)parse_vm1_file("/nonexistent-xyz.vm1", &n); }
static void call_write_null(void){ write_ldr1(NULL, NULL, 0, 0, NULL); }

static char tmp_path[64];
static char *write_tmp(const char *content){
  snprintf(tmp_path, sizeof tmp_path, "/tmp/test_loader_XXXXXX");
  int fd = mkstemp(tmp_path);
  if(fd < 0) return NULL;
  FILE *f = fdopen(fd, "w");
  if(!f){ close(fd); return NULL; }
  fputs(content,f);
  fclose(f);
  return tmp_path;
}

static void test_parse_u16(void){
  uint16_t v=0;
  CHECK(parse_u16("0",&v)==0 && v==0, "parse_u16 0");
  CHECK(parse_u16("65535",&v)==0 && v==65535, "parse_u16 max");
  CHECK(parse_u16("0x10",&v)==0 && v==16, "parse_u16 hex");
  CHECK(parse_u16("65536",&v)!=0, "parse_u16 overflow");
  CHECK(parse_u16("-1",&v)!=0, "parse_u16 negative");
  CHECK(parse_u16("abc",&v)!=0, "parse_u16 junk");
  CHECK(parse_u16("12x",&v)!=0, "parse_u16 trailing");
  CHECK(parse_u16("",&v)!=0, "parse_u16 empty");
  CHECK(parse_u16(NULL,&v)!=0, "parse_u16 null s");
  CHECK(parse_u16("1",NULL)!=0, "parse_u16 null out");
}

static void test_parse_u16_strict(void){
  long v=0;
  CHECK(parse_u16_strict("42",&v)==0 && v==42, "strict 42");
  CHECK(parse_u16_strict("0x10",&v)==0 && v==16, "strict hex");
  CHECK(parse_u16_strict("abc",&v)!=0, "strict junk");
  CHECK(parse_u16_strict("",&v)!=0, "strict empty");
  CHECK(parse_u16_strict("12 ",&v)!=0, "strict trailing space");
  CHECK(parse_u16_strict(NULL,&v)!=0, "strict null");
}

static void test_trim(void){
  char a[]="  hi  ";
  CHECK(strcmp(trim(a),"hi")==0, "trim spaces");
  char b[]="\t\n";
  CHECK(strcmp(trim(b),"")==0, "trim all-ws");
  char c[]="x";
  CHECK(strcmp(trim(c),"x")==0, "trim single");
  CHECK(trim(NULL)==NULL, "trim null");
}

static void test_parse_vm1_file(void){
  char *p1 = write_tmp("1 2 3 4\n5 6 7 8 # comment\n\n");
  size_t n=0;
  uint16_t *w = parse_vm1_file(p1,&n);
  CHECK(n==8 && w[0]==1 && w[7]==8, "parse_vm1 two lines");
  if(n) free(w);
  unlink(p1);
  char *p2 = write_tmp("# only comments\n\n");
  uint16_t *w2 = parse_vm1_file(p2,&n);
  (void)w2; // sentinel, не free
  CHECK(n==0, "parse_vm1 empty len 0");
  // w2 — sentinel, free только при n!=0
  unlink(p2);
  CHECK(child_exits(call_parse_null,1), "parse_vm1 null exits 1");
  CHECK(child_exits(call_parse_missing,1), "parse_vm1 missing exits 1");
  char *p3 = write_tmp("1 2 3\n");
  size_t n3=0;
  pid_t pid=fork();
  if(pid==0){ (void)parse_vm1_file(p3,&n3); _exit(99); }
  int st=0; waitpid(pid,&st,0);
  CHECK(WIFEXITED(st) && WEXITSTATUS(st)==1, "parse_vm1 3-fields exits 1");
  unlink(p3);
  char *p4 = write_tmp("1 2 3 4 5\n");
  pid=fork();
  if(pid==0){ size_t nn=0; (void)parse_vm1_file(p4,&nn); _exit(99); }
  waitpid(pid,&st,0);
  CHECK(WIFEXITED(st) && WEXITSTATUS(st)==1, "parse_vm1 5-fields exits 1");
  unlink(p4);
  char *p5 = write_tmp("99999 2 3 4\n");
  pid=fork();
  if(pid==0){ size_t nn=0; (void)parse_vm1_file(p5,&nn); _exit(99); }
  waitpid(pid,&st,0);
  CHECK(WIFEXITED(st) && WEXITSTATUS(st)==1, "parse_vm1 oob exits 1");
  unlink(p5);
}

static void test_write_ldr1(void){
  uint16_t words[]={0x1234,0xABCD};
  const char *out="/tmp/test_ldr_out.ldrpkt";
  unlink(out);
  write_ldr1(out, words, 2, 0x0200, NULL);
  FILE *f=fopen(out,"rb");
  uint8_t hdr[8];
  CHECK(f && fread(hdr,1,8,f)==8, "ldr1 header read");
  CHECK(hdr[0]=='L'&&hdr[1]=='D'&&hdr[2]=='R'&&hdr[3]=='1', "ldr1 magic");
  CHECK(hdr[4]==2&&hdr[5]==0&&hdr[6]==0&&hdr[7]==2, "ldr1 count/base");
  uint8_t pay[4];
  CHECK(fread(pay,1,4,f)==4 && pay[0]==0x34&&pay[1]==0x12&&pay[2]==0xCD&&pay[3]==0xAB, "ldr1 payload LE");
  fclose(f);
  // с tail
  const char *tail="/tmp/test_ldr_tail.bin";
  FILE *tf=fopen(tail,"wb"); fwrite("HI",1,2,tf); fclose(tf);
  write_ldr1(out, words, 2, 0, tail);
  f=fopen(out,"rb");
  fseek(f,0,SEEK_END);
  long sz=ftell(f);
  fclose(f);
  CHECK(sz==8+4+2, "ldr1 with tail size");
  unlink(tail);
  // пустая программа
  write_ldr1(out, NULL, 0, 0, NULL);
  f=fopen(out,"rb");
  uint8_t h2[8];
  CHECK(fread(h2,1,8,f)==8 && h2[4]==0 && h2[5]==0, "ldr1 empty ok");
  fclose(f);
  unlink(out);
  CHECK(child_exits(call_write_null,1), "write_ldr1 null exits 1");
  CHECK(child_exits(call_die,1), "die exits 1");
  CHECK(child_exits(call_die2,1), "die2 exits 1");
}

int main(void){
  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);
  printf("=== test_loader ===\n");
  test_parse_u16();
  test_parse_u16_strict();
  test_trim();
  test_parse_vm1_file();
  test_write_ldr1();
  printf("=== summary: pass=%d fail=%d ===\n", passes, fails);
  return fails ? 1 : 0;
}
