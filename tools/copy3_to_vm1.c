/* copy3_to_vm1 - compile copy(n,dst,src) to vm1 words (C port) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static void die(const char *msg){ fprintf(stderr,"copy3_to_vm1: %s\n",msg); exit(1);}
static void die2(const char *a,const char *b){ fprintf(stderr,"copy3_to_vm1: %s: %s\n",a,b); exit(1);}

int main(int argc, char **argv){
    const char *in=NULL, *out=NULL;
    for(int i=1;i<argc;i++){
        if(strcmp(argv[i],"--in-copy3")==0 && i+1<argc) in=argv[++i];
        else if(strcmp(argv[i],"--out-vm1")==0 && i+1<argc) out=argv[++i];
        else if(strncmp(argv[i],"--in-copy3=",11)==0) in=argv[i]+11;
        else if(strncmp(argv[i],"--out-vm1=",10)==0) out=argv[i]+10;
        else { fprintf(stderr,"unknown arg %s\nusage: copy3_to_vm1 --in-copy3 FILE --out-vm1 FILE\n",argv[i]); return 2; }
    }
    if(!in||!out) die("missing --in-copy3 or --out-vm1");
    FILE *fin=fopen(in,"r");
    if(!fin) die2("cannot open in-copy3",in);
    FILE *fout=fopen(out,"w");
    if(!fout) die2("cannot open out-vm1",out);
    char line[4096];
    int lineno=0, count=0;
    while(fgets(line,sizeof line,fin)){
        lineno++;
        char *hash=strchr(line,'#');
        if(hash) *hash='\0';
        char *p=line;
        while(*p==' '||*p=='\t'||*p=='\r'||*p=='\n') p++;
        if(*p=='\0') continue;
        char *end=p+strlen(p)-1;
        while(end>p && (*end==' '||*end=='\t'||*end=='\r'||*end=='\n')) *end--='\0';
        if(*p=='\0') continue;
        char *tok[3];
        int ntok=0;
        char *s=strtok(p," \t");
        while(s && ntok<3){ tok[ntok++]=s; s=strtok(NULL," \t"); }
        if(s){ fprintf(stderr,"copy3_to_vm1: %s:%d: expected 3 fields\n",in,lineno); return 1; }
        if(ntok!=3){ fprintf(stderr,"copy3_to_vm1: %s:%d: expected 3 fields 'n dst src' got %d\n",in,lineno,ntok); return 1; }
        char *e;
        long n=strtol(tok[0],&e,0); if(*e||n<0){ fprintf(stderr,"copy3_to_vm1: %s:%d: n must be >=0\n",in,lineno); return 1; }
        long dst=strtol(tok[1],&e,0); if(*e||dst<0){ fprintf(stderr,"copy3_to_vm1: %s:%d: dst must be >=0\n",in,lineno); return 1; }
        long src=strtol(tok[2],&e,0); if(*e||src<0){ fprintf(stderr,"copy3_to_vm1: %s:%d: src must be >=0\n",in,lineno); return 1; }
        long dl=dst-1, dh=dst+n, sl=src-1, sh=src+n;
        if(dl<0||dl>0xFFFF||dh<0||dh>0xFFFF||sl<0||sl>0xFFFF||sh<0||sh>0xFFFF){
            fprintf(stderr,"copy3_to_vm1: %s:%d: encoded word out of uint16 range\n",in,lineno); return 1;
        }
        fprintf(fout,"%ld %ld %ld %ld\n",dl,dh,sl,sh);
        count++;
    }
    fclose(fin); fclose(fout);
    printf("compiled %d instructions -> %s\n",count,out);
    return 0;
}
