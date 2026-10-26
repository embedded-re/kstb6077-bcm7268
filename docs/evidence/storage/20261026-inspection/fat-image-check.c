#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "fat.h"
uint8_t work[65536];
static int fd;static uint32_t base;static off_t capacity;
static off_t offset(uint32_t lba,uint32_t n){assert(lba>=base);off_t o=(off_t)(lba-base)*512;assert(o+(off_t)n*512<=capacity);return o;}
int fat_blk_read(uint32_t lba,uint32_t n,void *buf){return pread(fd,buf,(size_t)n*512,offset(lba,n))==(ssize_t)n*512?0:-1;}
int fat_blk_write(uint32_t lba,uint32_t n,const void *buf){return pwrite(fd,buf,(size_t)n*512,offset(lba,n))==(ssize_t)n*512?0:-1;}
int main(int argc,char **argv){
 assert(argc>=5);fd=open(argv[1],O_RDWR);assert(fd>=0);struct stat st;assert(!fstat(fd,&st)&&S_ISREG(st.st_mode));capacity=st.st_size;
 base=strtoul(argv[2],NULL,10);assert(!fat_mount(base));uint32_t cluster,clusters,freec;assert(!fat_info(&cluster,&clusters,&freec));
 printf("cluster_bytes=%u clusters=%u free_clusters=%u\n",cluster,clusters,freec);
 for(int i=4;i<argc;i++){
  char path[1024];assert(snprintf(path,sizeof(path),"%s/%s",argv[3],argv[i])<(int)sizeof(path));
  FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>=0);rewind(f);
  uint8_t *expected=malloc(n?n:1),*actual=malloc(n?n:1);assert(expected&&actual);assert(fread(expected,1,n,f)==(size_t)n);assert(!fclose(f));
  uint32_t got=0;assert(!fat_read(argv[i],actual,n,&got)&&got==(uint32_t)n);assert(!memcmp(expected,actual,n));
  printf("read_compare=PASS file=%s bytes=%ld\n",argv[i],n);free(expected);free(actual);
 }
 enum{N=153619};uint8_t *src=malloc(N),*dst=malloc(N);assert(src&&dst);
 for(unsigned i=0;i<N;i++)src[i]=(i*19u)^(i>>7);
 assert(!fat_write("rptest.bin",src,N));uint32_t got;assert(!fat_read("rptest.bin",dst,N,&got)&&got==N&&!memcmp(src,dst,N));assert(!fat_remove("rptest.bin"));
 struct fat_entry e;assert(fat_stat("rptest.bin",&e)==FAT_ENOENT);assert(!fsync(fd));assert(!close(fd));free(src);free(dst);
 puts("RESULT=PASS all source files exact; multi-cluster write/read/delete exact");return 0;
}
