#!/usr/bin/env python3
"""Exercise the actual raw-cache reader with mock storage and a two-slot cache.

Extract the function to isolate it from emulator/platform globals. The storage
mock supplies bytes derived from their ROM address; it does not implement LRU
or partial validity, so those behaviors are checked in production code.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/common/cache.c").read_text()
start = source.index("static uint32_t read_cache_rawfile(")
end = source.index("\n\n/*---", start)
reader = source[start:end]
PREAMBLE = r"""
#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#define BLOCK_SHIFT 16
#define BLOCK_MASK 65535
#define CACHE_BLOCK_SIZE 65536
#define CACHE_READ_QUARTER_SIZE 16384
#define MVS 1
#define EMU_SYSTEM MVS
#define BLOCK_NOT_CACHED 65535
#define GFX_MEMORY mem
static uint8_t mem[2*65536], gfx_valid_parts[2];
static size_t demand_size=16384;
static size_t cache_resolved_read_size(void) { return demand_size; }
static uint16_t blocks[8];
typedef struct cache { int idx, block; struct cache *prev, *next; } cache_t;
static cache_t cache_data[2], *head, *tail;
static int cache_fd, cache_storage_handle, reads, fail;
static int64_t cache_file_pos;
static int mvs_cache_read_range(int fd, int *h, int64_t *p, uint16_t block, uint8_t *dst, const char *name, unsigned within, unsigned size) {
 (void)fd;(void)h;(void)p;(void)name;
 assert(size==demand_size && within%demand_size==0);
 reads++; if(fail) return 0;
 memset(dst, block*4+within/16384, size); return 1;
}
"""
CHECKS = r"""

int main(void) {
 memset(blocks,255,sizeof(blocks)); memset(mem,0xa5,sizeof(mem));
 cache_data[0]=(cache_t){0,0,0,&cache_data[1]};
 cache_data[1]=(cache_t){1,1,&cache_data[0],0};
 blocks[0]=0;blocks[1]=1;head=&cache_data[0];tail=&cache_data[1];
 gfx_valid_parts[0]=gfx_valid_parts[1]=15;
 unsigned pos=read_cache_rawfile(2*65536+16384+128);
 assert(reads==1 && mem[pos]==9 && gfx_valid_parts[0]==2);
 assert(mem[0]==0xa5 && mem[32768]==0xa5);
 assert(read_cache_rawfile(2*65536+16384+256)==pos+128 && reads==1);
 pos=read_cache_rawfile(2*65536+49152);assert(reads==2 && mem[pos]==11 && gfx_valid_parts[0]==10);
 read_cache_rawfile(3*65536);assert(reads==3);
 fail=1;pos=read_cache_rawfile(4*65536);assert(reads==4 && mem[pos]==0 && gfx_valid_parts[0]==0);
 fail=0;pos=read_cache_rawfile(4*65536);assert(reads==5 && mem[pos]==16 && gfx_valid_parts[0]==1);
 assert(blocks[2]==BLOCK_NOT_CACHED);
 /* 32/64 KiB selections coalesce validity quarters without changing slots. */
 demand_size=32768; memset(blocks,255,sizeof(blocks)); memset(gfx_valid_parts,0,sizeof(gfx_valid_parts));
 cache_data[0]=(cache_t){0,-1,0,&cache_data[1]};cache_data[1]=(cache_t){1,-1,&cache_data[0],0};head=&cache_data[0];tail=&cache_data[1];reads=0;
 pos=read_cache_rawfile(32768+128);assert(reads==1 && gfx_valid_parts[0]==12);read_cache_rawfile(49152);assert(reads==1);
 demand_size=65536; memset(blocks,255,sizeof(blocks)); memset(gfx_valid_parts,0,sizeof(gfx_valid_parts));
 cache_data[0]=(cache_t){0,-1,0,&cache_data[1]};cache_data[1]=(cache_t){1,-1,&cache_data[0],0};head=&cache_data[0];tail=&cache_data[1];reads=0;
 pos=read_cache_rawfile(128);assert(reads==1 && gfx_valid_parts[0]==15);read_cache_rawfile(49152);assert(reads==1);
 demand_size=16384;
 /* Random aligned sprite tiles, including every quarter and repeated evictions. */
 uint32_t rng=12345;
 for(unsigned i=0;i<10000;i++) {
  rng=rng*1664525u+1013904223u;
  unsigned addr=((rng>>8)%(8*512))*128;
  unsigned loc=read_cache_rawfile(addr);
  for(unsigned j=0;j<128;j++) assert(mem[loc+j]==addr/16384);
 }
 return 0;
}
"""
with tempfile.TemporaryDirectory(prefix="njemu-partial-test-") as directory:
    test = Path(directory) / "test.c"
    binary = Path(directory) / "test"
    test.write_text(PREAMBLE + reader + CHECKS)
    subprocess.run([sys.argv[1] if len(sys.argv) > 1 else "cc", "-std=c99",
                    "-Wall", "-Wextra", "-Werror", str(test), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("PASS: partial C-ROM reads, hits, eviction, failure/retry, 10000 sprite tiles")
