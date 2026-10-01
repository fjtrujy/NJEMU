#!/usr/bin/env python3
"""Compile the production PCM reader against address-derived mock storage."""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/common/cache.c').read_text()
start = source.index('uint8_t *pcm_cache_read(')
reader = source[start:source.index('\n#endif', source.index('\n\t];', start))]
preamble = r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#define MVS_PCM_PARTIAL_READS 1
#define PCM_CACHE_SHIFT 14
#define BLOCK_SHIFT 16
#define BLOCK_NOT_CACHED 65535
static uint8_t memory_region_sound1[2*65536], pcm_valid_parts[2];
static uint16_t pcm_blocks[8];
typedef struct cache { int idx,block,frame; struct cache *prev,*next; } cache_t;
static cache_t pcm_data[2], *pcm_head, *pcm_tail;
static int pcm_fd,pcm_storage_handle,frames_displayed=1,reads,fail;
static int64_t pcm_file_pos;
static int mvs_cache_read_range(int fd,int *h,int64_t *p,uint16_t block,uint8_t *dst,const char *name,unsigned within,unsigned size) {
 (void)fd;(void)h;(void)p;(void)name;
 assert(size==16384 && within%16384==0); reads++;
 if(fail) return 0;
 for(unsigned i=0;i<size;i++) dst[i]=(uint8_t)(block*4+within/16384+i);
 return 1;
}
'''
checks = r'''
int main(void) {
 memset(pcm_blocks,255,sizeof(pcm_blocks));
 pcm_data[0]=(cache_t){0,0,0,0,&pcm_data[1]};
 pcm_data[1]=(cache_t){1,1,0,&pcm_data[0],0};
 pcm_blocks[0]=0;pcm_blocks[1]=1;pcm_head=&pcm_data[0];pcm_tail=&pcm_data[1];
 uint8_t *p=pcm_cache_read(9);assert(reads==1 && p[0]==9 && p[16383]==8);
 assert(pcm_cache_read(9)==p && reads==1);
 p=pcm_cache_read(10);assert(reads==2 && p[0]==10);
 fail=1;p=pcm_cache_read(11);assert(p[0]==0 && p[16383]==0);
 fail=0;p=pcm_cache_read(11);assert(reads==4 && p[0]==11);
 uint32_t rng=123;
 for(int i=0;i<10000;i++) {
  frames_displayed++;rng=rng*1664525u+1013904223u;
  unsigned key=(rng>>16)%32;
  p=pcm_cache_read(key);
  assert(p[0]==key && p[16383]==(uint8_t)(key+16383));
 }
 /* Sequential decoder byte addresses across all 16/64 KiB boundaries. */
 for(unsigned addr=0;addr<8*65536;addr++) {
  if(!(addr&16383)) {frames_displayed++;p=pcm_cache_read(addr>>14);}
  assert(p[addr&16383]==(uint8_t)((addr>>14)+(addr&16383)));
 }
 return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
    c = Path(d) / 'pcm.c'
    c.write_text(preamble + reader + checks)
    exe = Path(d) / 'pcm'
    subprocess.run([sys.argv[1] if len(sys.argv)>1 else 'cc', '-std=c99', '-Wall', '-Wextra', '-Werror', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PCM partial cache checks passed')
