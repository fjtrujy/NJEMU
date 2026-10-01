#!/usr/bin/env python3
"""Exercise CPS2 raw-cache demand reads at each selectable granularity."""
from pathlib import Path
import subprocess, sys, tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'src/common/cache.c').read_text()
start=source.index('static uint32_t read_cache_rawfile(')
reader=source[start:source.index('\n\n/*---',start)]
preamble=r'''
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <unistd.h>
#define SEEK_SET 0
#define CPS2 2
#define MVS 3
#define EMU_SYSTEM CPS2
#define BLOCK_SHIFT 16
#define BLOCK_MASK 65535
#define CACHE_BLOCK_SIZE 65536
#define CACHE_READ_QUARTER_SIZE 16384
#define BLOCK_NOT_CACHED 65535
#define GFX_MEMORY mem
static uint8_t mem[2*65536],gfx_valid_parts[2];
static uint16_t blocks[8]; static uint32_t block_offset[8];
typedef struct cache { int idx,block; struct cache *prev,*next; } cache_t;
static cache_t cache_data[2],*head,*tail; static int cache_fd,reads; static off_t pos;
static size_t demand_size=16384; static size_t cache_resolved_read_size(void){return demand_size;}
static off_t mock_lseek(int fd,off_t p,int whence){(void)fd;(void)whence;pos=p;return p;}
static ssize_t mock_read(int fd,void *dst,size_t n){(void)fd;reads++;for(size_t i=0;i<n;i++)((uint8_t*)dst)[i]=(uint8_t)((pos+i)/16384);pos+=(off_t)n;return (ssize_t)n;}
#define lseek mock_lseek
#define read mock_read
'''
checks=r'''
static void reset(void){memset(blocks,255,sizeof(blocks));memset(gfx_valid_parts,0,sizeof(gfx_valid_parts));for(int i=0;i<8;i++)block_offset[i]=i*65536;cache_data[0]=(cache_t){0,-1,0,&cache_data[1]};cache_data[1]=(cache_t){1,-1,&cache_data[0],0};head=&cache_data[0];tail=&cache_data[1];reads=0;}
int main(void){
 reset();demand_size=16384;unsigned p=read_cache_rawfile(2*65536+16384+128);assert(reads==1&&mem[p]==9&&gfx_valid_parts[0]==2);read_cache_rawfile(2*65536+16384+256);assert(reads==1);
 reset();demand_size=32768;p=read_cache_rawfile(32768+128);assert(reads==1&&gfx_valid_parts[0]==12&&mem[p]==2);read_cache_rawfile(49152);assert(reads==1);
 reset();demand_size=65536;p=read_cache_rawfile(128);assert(reads==1&&gfx_valid_parts[0]==15&&mem[p]==0);read_cache_rawfile(49152);assert(reads==1);
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='njemu-cps2-partial-') as d:
 c=Path(d)/'test.c'; exe=Path(d)/'test'; c.write_text(preamble+reader+checks)
 subprocess.run([sys.argv[1] if len(sys.argv)>1 else 'cc','-std=c99','-Wall','-Wextra','-Werror',str(c),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
print('PASS: CPS2 16/32/64 KiB raw-cache demand reads')
