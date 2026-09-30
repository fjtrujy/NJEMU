#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include "common/memory_plan.h"

#define KIB(n) ((uint64_t)(n) * 1024u)

int main(void)
{
    game_memory_requirements_t game = {
        .core = MEMORY_PLAN_CORE_MVS,
        .gfx_or_crom_bytes = KIB(65536),
        .pcm_or_vrom_bytes = KIB(16384),
    };
    /* Same total budget as 8512 KiB C-ROM + 3072 KiB PCM + 2048 KiB reserve. */
    memory_probe_constraints_t limits = {
        .total_cap_bytes = KIB(13632),
        .single_block_cap_bytes = KIB(13632),
    };
    memory_allocation_shape_t shape;
    assert(memory_plan_allocate_shape(&game, &limits, &shape));
    assert(shape.plan.safety_reserve_bytes == KIB(PS2_CACHE_RESERVE_KB));
    assert(shape.plan.pcm_cache_bytes == KIB(3072));
    assert(shape.plan.gfx_cache_bytes == KIB(13632 - 3072 - PS2_CACHE_RESERVE_KB));
    assert(shape.gfx_memory && shape.pcm_memory && shape.reserve_memory);
    memory_allocation_shape_release_reserve(&shape);
    assert(shape.reserve_memory == NULL && shape.gfx_memory && shape.pcm_memory);
    memory_allocation_shape_release(&shape);

    /* Lowering the reserve cannot override a contiguous allocation limit. */
    limits.single_block_cap_bytes = KIB(8192);
    assert(memory_plan_allocate_shape(&game, &limits, &shape));
    assert(shape.plan.gfx_cache_bytes <= limits.single_block_cap_bytes);
    assert(shape.plan.gfx_cache_bytes + shape.plan.pcm_cache_bytes +
        shape.plan.safety_reserve_bytes <= limits.total_cap_bytes);
    memory_allocation_shape_release(&shape);
    printf("memory_reserve_tests: %u KiB reserve OK\n", PS2_CACHE_RESERVE_KB);
    return 0;
}
