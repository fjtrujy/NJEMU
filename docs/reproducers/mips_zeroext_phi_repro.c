/* Standalone C99 reproducer for Clang MIPS zeroext return after multi-path CFG.
 *
 * Compile with:
 *   clang -target mips64el -mcpu=r5900 -mabi=n32 -O3 -fno-pic \
 *         -mno-abicalls -G0 -S mips_zeroext_phi_repro.c -o r5900.s
 *   clang -target mips64el -mcpu=mips3 -mabi=n32 -O3 -fno-pic \
 *         -mno-abicalls -G0 -S mips_zeroext_phi_repro.c -o mips3.s
 *
 * At LLVM 455ff9dd, both targets emit lbu, jr ra, andi result,255
 * on the fast RAM path, even though lbu already zero-extends the byte.
 * GCC 15.2 does not emit that redundant andi for the RAM return path.
 *
 * The simple_read_byte control avoids the redundant mask under Clang.
 * These functions are for compiler codegen inspection, not execution.
 */
extern unsigned char *guest_ram;
extern unsigned char *guest_bios;
extern unsigned short (*guest_io_read)(unsigned, unsigned short);

unsigned char guest_read_byte(unsigned address)
{
    unsigned shift = (~address & 1u) << 3;
    unsigned short mask = (unsigned short)~(0xffu << shift);
    address &= 0xffffffu;
    if (__builtin_expect((address >> 20) == 1u, 1))
        return guest_ram[(address ^ 1u) & 0xffffu];
    switch (address >> 20) {
    case 0: return guest_bios[address ^ 1u];
    case 2: return (unsigned char)(guest_io_read(address >> 1, mask) >> shift);
    default: return 0xff;
    }
}

unsigned char simple_read_byte(unsigned char *p)
{
    return *p;
}
