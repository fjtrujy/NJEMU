#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "sf2_port_projection.h"
#include "ps2/ps2_input_history.h"

static void input_mapping(void)
{
    uint32_t all = PLATFORM_PAD_UP | PLATFORM_PAD_DOWN |
        PLATFORM_PAD_LEFT | PLATFORM_PAD_RIGHT | PLATFORM_PAD_B1 |
        PLATFORM_PAD_B2 | PLATFORM_PAD_B3 | PLATFORM_PAD_B4 |
        PLATFORM_PAD_L | PLATFORM_PAD_R;

    assert(sf2_probe_ports(0) == 0x707ffu);
    assert(sf2_probe_ports(PLATFORM_PAD_RIGHT) == 0x707feu);
    assert(sf2_probe_ports(PLATFORM_PAD_DOWN) == 0x707fbu);
    assert(sf2_probe_ports(PLATFORM_PAD_B3) == 0x707bfu);
    assert(sf2_probe_ports(PLATFORM_PAD_RIGHT | PLATFORM_PAD_B3) == 0x707beu);
    assert(sf2_probe_ports(PLATFORM_PAD_B1) == 0x606ffu);
    assert(sf2_probe_ports(PLATFORM_PAD_R) == 0x505ffu);
    assert(sf2_probe_ports(PLATFORM_PAD_B4) == 0x303ffu);
    assert(sf2_probe_ports(all) == 0x80u);
}

static void port_delivery(void)
{
    ps2_input_history_t h = {0};
    uint32_t previous = sf2_probe_ports(0);
    uint32_t ports[4];
    unsigned i;

    ps2_input_history_set_live(&h, 0);
    ps2_input_history_record(&h, PLATFORM_PAD_RIGHT);
    ps2_input_history_record(&h, 0);
    ps2_input_history_record(&h, PLATFORM_PAD_DOWN);
    ps2_input_history_record(&h, 0);

    for (i = 0; i < 4; i++)
        ports[i] = sf2_probe_ports(ps2_input_history_consume(&h));
    assert(ports[0] == 0x707feu);
    assert(ports[1] == previous);
    assert(ports[2] == 0x707fbu);
    assert(ports[3] == previous);

    ps2_input_history_set_live(&h, 0);
    ps2_input_history_record(&h, PLATFORM_PAD_RIGHT | PLATFORM_PAD_B3);
    ps2_input_history_record(&h, 0);
    assert(sf2_probe_ports(ps2_input_history_consume(&h)) == 0x707beu);
    assert(sf2_probe_ports(ps2_input_history_consume(&h)) == previous);
}

static void legacy_phantom_port_combo(void)
{
    ps2_input_edge_latch_t old = {0};
    ps2_input_history_t h = {0};
    uint32_t old_buttons;

    ps2_input_edge_latch_reset(&old);
    ps2_input_history_set_live(&h, 0);
    ps2_input_edge_latch_record(&old, 0);
    ps2_input_edge_latch_record(&old, PLATFORM_PAD_RIGHT);
    ps2_input_edge_latch_record(&old, 0);
    ps2_input_edge_latch_record(&old, PLATFORM_PAD_DOWN);
    ps2_input_edge_latch_record(&old, 0);
    old_buttons = ps2_input_edge_latch_consume(&old, 0, 0);
    assert(sf2_probe_ports(old_buttons) == 0x707fau);

    ps2_input_history_record(&h, PLATFORM_PAD_RIGHT);
    ps2_input_history_record(&h, 0);
    ps2_input_history_record(&h, PLATFORM_PAD_DOWN);
    ps2_input_history_record(&h, 0);
    assert(sf2_probe_ports(ps2_input_history_consume(&h)) == 0x707feu);
    assert(sf2_probe_ports(ps2_input_history_consume(&h)) == 0x707ffu);
    assert(sf2_probe_ports(ps2_input_history_consume(&h)) == 0x707fbu);
    assert(sf2_probe_ports(ps2_input_history_consume(&h)) == 0x707ffu);
}

int main(void)
{
    input_mapping();
    port_delivery();
    legacy_phantom_port_combo();
    puts("SF2 CPS1 port projection and ordered-history tests passed");
    return 0;
}
