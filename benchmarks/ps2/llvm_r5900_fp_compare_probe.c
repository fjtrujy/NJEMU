/*
 * Standalone LLVM R5900 backend regression probe.
 *
 * Compile to assembly using Clang's R5900 target, then ask the PS2 GNU
 * assembler to validate the emitted instructions for the real processor.
 * This file is intentionally not part of any NJEMU emulator target.
 */
int llvm_r5900_ordered_less(float lhs, float rhs)
{
    return lhs < rhs;
}

int llvm_r5900_ordered_less_equal(float lhs, float rhs)
{
    return lhs <= rhs;
}

int llvm_r5900_not_equal(float lhs, float rhs)
{
    return lhs != rhs;
}
