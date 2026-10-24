/* Exercise the real bus-error path in both C68k interpreter configurations. */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cpu/m68000/c68k.h"

static uint16_t rom[64];
static uint64_t write_hash;
static unsigned writes;

static uint16_t read_word(uint32_t a)
{
    if (a == 8) return 0;
    if (a == 10) return 4; /* vector 2 handler at PC=4 */
    return 0;
}
static uint8_t read_byte(uint32_t a) { return (uint8_t)read_word(a); }
static void write_word(uint32_t a, uint16_t v)
{
    write_hash = (write_hash ^ a) * UINT64_C(1099511628211);
    write_hash = (write_hash ^ v) * UINT64_C(1099511628211);
    ++writes;
}
static void write_byte(uint32_t a, uint8_t v) { write_word(a, v); }

int main(void)
{
    c68k_struc cpu;
    int32_t used;
    memset(&cpu, 0, sizeof(cpu));
    memset(rom, 0, sizeof(rom));
    rom[2] = 0x4e71;  /* NOP at bus error vector target */
    write_hash = UINT64_C(14695981039346656037);
    C68k_Init(&cpu);
    C68k_Set_Fetch(&cpu, 0, 0xffff, (uintptr_t)rom);
    C68k_Set_ReadW(&cpu, read_word);
    C68k_Set_ReadB(&cpu, read_byte);
    C68k_Set_WriteW(&cpu, write_word);
    C68k_Set_WriteB(&cpu, write_byte);
    cpu.flag_S = 0;
    cpu.flag_I = 7;
    cpu.A[7] = 0x500;
    cpu.USP = 0x400;
    C68k_Set_Reg(&cpu, C68K_PC, 0);
    BusErrAdr = 0xabcdef;
    BusErrHandling = 1;
    used = C68k_Exec(&cpu, 4);
    printf("used=%d PC=%08x A7=%08x USP=%08x S=%04x bus=%u writes=%u hash=%016" PRIx64 "\n",
           used, C68k_Get_Reg(&cpu, C68K_PC), cpu.A[7], cpu.USP,
           cpu.flag_S, BusErrHandling, writes, write_hash);
    if (BusErrHandling || cpu.flag_S != C68K_SR_S ||
        C68k_Get_Reg(&cpu, C68K_PC) != 6 ||
        cpu.A[7] != 0x3f2 || cpu.USP != 0x500 ||
        writes != 5 || used != 4 ||
        write_hash != UINT64_C(0x45525a8f1990efec))
        return 1;
    return 0;
}
