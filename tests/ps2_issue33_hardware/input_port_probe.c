/* PS2Link-safe CPS1/SF2 port timing probe.
 * The direct frame-boundary read approximates legacy synchronous padRead.
 * Worker snapshot, legacy latch and ordered history use the same PADMAN stream.
 * No IOP reset, no graphics, no OSD exit: START+SELECT finishes.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <tamtypes.h>
#include <kernel.h>
#include <delaythread.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <libpad.h>
#include "ps2/ps2_pad_packet.h"
#include "ps2/ps2_input_edge_latch.h"
#include "ps2/ps2_input_history.h"
#include "sf2_port_projection.h"

#define FRAME_US 16667
#define WORKER_US 8000
#define FRAME_LIMIT 7200
#define TRACE_LIMIT 80

static u8 pad_buf[256] __attribute__((aligned(64)));
static u8 worker_stack[0x2000] __attribute__((aligned(16)));
static volatile int running = 1;
static volatile int stop_requested;
static volatile int published_ready;
static uint32_t published_buttons;
static ps2_input_history_t history;
static ps2_input_edge_latch_t old_latch;
static unsigned worker_valid, worker_invalid;
static unsigned worker_down, worker_up, worker_prior;
static unsigned frame_valid, frame_invalid;
static unsigned base_down, old_down, new_down;
static unsigned base_up, old_up, new_up;
static unsigned base_prior, old_prior, new_prior;
static unsigned worker_diff, old_diff, new_diff;
static unsigned old_port_diff, new_port_diff, max_depth;
static unsigned direct_only_frames, worker_only_frames;
static unsigned onset_miss_old, onset_miss_new;
static unsigned release_lag_old, release_lag_new;
static unsigned anomalous_frames;
static unsigned char first_ready_seen;

typedef struct {
    unsigned frame;
    unsigned baseline, worker, legacy, ordered;
    unsigned port_base, port_worker, port_old, port_new;
} trace_t;
static trace_t trace[TRACE_LIMIT];
static unsigned trace_count;

static unsigned bitcount(uint32_t v)
{
    unsigned n = 0;
    for (; v; v >>= 1)
        n += v & 1u;
    return n;
}

static uint32_t decode(uint16_t raw)
{
    uint32_t p = 0xffffu ^ raw, b = 0;
    if (p & PAD_UP) b |= PLATFORM_PAD_UP;
    if (p & PAD_DOWN) b |= PLATFORM_PAD_DOWN;
    if (p & PAD_LEFT) b |= PLATFORM_PAD_LEFT;
    if (p & PAD_RIGHT) b |= PLATFORM_PAD_RIGHT;
    if (p & PAD_CIRCLE) b |= PLATFORM_PAD_B1;
    if (p & PAD_CROSS) b |= PLATFORM_PAD_B2;
    if (p & PAD_SQUARE) b |= PLATFORM_PAD_B3;
    if (p & PAD_TRIANGLE) b |= PLATFORM_PAD_B4;
    if (p & PAD_L1) b |= PLATFORM_PAD_L;
    if (p & PAD_R1) b |= PLATFORM_PAD_R;
    return b;
}

static void worker(void *unused)
{
    (void)unused;
    while (running) {
        struct padButtonStatus pad;
        int state, len;
        uint32_t now;
        memset(&pad, 0, sizeof(pad));
        state = padGetState(0, 0);
        len = padRead(0, 0, &pad);
        if ((state == PAD_STATE_STABLE || state == PAD_STATE_FINDCTP1) &&
            ps2_pad_packet_valid(len,
                offsetof(struct padButtonStatus, btns) + sizeof(pad.btns),
                pad.mode, pad.btns)) {
            now = decode(pad.btns);
            DIntr();
            if (published_ready) {
                worker_down += bitcount(now & ~worker_prior);
                worker_up += bitcount(worker_prior & ~now);
            }
            worker_prior = now;
            worker_valid++;
            published_buttons = now;
            if (!published_ready) {
                ps2_input_history_set_live(&history, now);
                ps2_input_edge_latch_record(&old_latch, now);
            } else {
                ps2_input_history_record(&history, now);
                ps2_input_edge_latch_record(&old_latch, now);
            }
            if (history.count > max_depth) max_depth = history.count;
            published_ready = 1;
            if (((~pad.btns) & (PAD_START | PAD_SELECT)) ==
                (PAD_START | PAD_SELECT))
                stop_requested = 1;
            EIntr();
        } else {
            DIntr();
            worker_invalid++;
            EIntr();
        }
        DelayThread(WORKER_US);
    }
}

static void print_stats(unsigned frames, int finish)
{
    printf("[SF2PROBE] %s frames=%u direct=%u/%u worker=%u/%u observed=%u/%u base=%u/%u legacy=%u/%u ordered=%u/%u diffWorker=%u diffOld=%u diffNew=%u portOld=%u portNew=%u directOnly=%u workerOnly=%u onsetMissOld=%u onsetMissNew=%u releaseLagOld=%u releaseLagNew=%u peakQ=%u overflow=%u resync=%u trace=%u\n",
        finish ? "DONE" : "STAT", frames,
        frame_valid, frame_invalid, worker_valid, worker_invalid,
        worker_down, worker_up,
        base_down, base_up, old_down, old_up, new_down, new_up,
        worker_diff, old_diff, new_diff, old_port_diff, new_port_diff,
        direct_only_frames, worker_only_frames,
        onset_miss_old, onset_miss_new,
        release_lag_old, release_lag_new, max_depth,
        (unsigned)history.overflows, (unsigned)history.backlog_resyncs,
        trace_count);
    fflush(stdout);
}

int main(void)
{
    int sio, padman, init, opened, state, len, tid;
    int i;
    unsigned frames = 0;
    ee_thread_t thread;
    struct padButtonStatus packet;
    SifInitRpc(0);
    printf("[SF2PROBE] BEGIN: real PS2 direct-vs-worker CPS1 input ports, no IOP reset\n");
    sio = SifLoadModule("rom0:SIO2MAN", 0, NULL);
    padman = SifLoadModule("rom0:PADMAN", 0, NULL);
    init = padInit(0);
    opened = padPortOpen(0, 0, pad_buf);
    printf("[SF2PROBE] modules=%d/%d padInit=%d padOpen=%d\n",
        sio, padman, init, opened);
    fflush(stdout);
    if (init != 1 || opened <= 0) {
        printf("[SF2PROBE] ERROR no controller\n");
        fflush(stdout);
        SleepThread();
    }
    for (i = 0; i < 200; i++) {
        state = padGetState(0, 0);
        if (state == PAD_STATE_STABLE || state == PAD_STATE_FINDCTP1)
            break;
        DelayThread(10000);
    }
    ChangeThreadPriority(GetThreadId(), 0x30);
    memset(&thread, 0, sizeof(thread));
    thread.func = worker;
    thread.stack = worker_stack;
    thread.stack_size = sizeof(worker_stack);
    thread.gp_reg = &_gp;
    thread.initial_priority = 0x28;
    tid = CreateThread(&thread);
    if (tid <= 0 || StartThread(tid, NULL) < 0) {
        printf("[SF2PROBE] ERROR worker thread=%d\n", tid);
        fflush(stdout);
        SleepThread();
    }
    printf("[SF2PROBE] READY: press Square repeatedly, try Right/Down and chords; START+SELECT finishes.\n");
    fflush(stdout);

    while (!stop_requested && frames < FRAME_LIMIT) {
        uint32_t base = 0, snapshot = 0, old = 0, ordered = 0;
        uint32_t pbase, pworker, pold, pnew;
        int frame_ok, have_worker;
        memset(&packet, 0, sizeof(packet));
        /* The main thread direct padRead models the old frame-boundary
         * read. It is deliberately a comparison-only extra reader. */
        DIntr();
        state = padGetState(0, 0);
        len = padRead(0, 0, &packet);
        frame_ok = ((state == PAD_STATE_STABLE ||
                     state == PAD_STATE_FINDCTP1) &&
                    ps2_pad_packet_valid(len,
                        offsetof(struct padButtonStatus, btns) + sizeof(packet.btns),
                        packet.mode, packet.btns));
        if (frame_ok)
            base = decode(packet.btns);
        have_worker = published_ready;
        if (have_worker) {
            snapshot = published_buttons;
            old = ps2_input_edge_latch_consume(&old_latch, snapshot, 0);
            ordered = ps2_input_history_consume(&history);
        }
        EIntr();
        if (frame_ok && have_worker) {
            if (!first_ready_seen) {
                first_ready_seen = 1;
                base_prior = base;
                old_prior = old;
                new_prior = ordered;
            }
            pbase = sf2_probe_ports(base);
            pworker = sf2_probe_ports(snapshot);
            pold = sf2_probe_ports(old);
            pnew = sf2_probe_ports(ordered);
            base_down += bitcount(base & ~base_prior);
            base_up += bitcount(base_prior & ~base);
            old_down += bitcount(old & ~old_prior);
            old_up += bitcount(old_prior & ~old);
            new_down += bitcount(ordered & ~new_prior);
            new_up += bitcount(new_prior & ~ordered);
            if (pworker != pbase) worker_diff++;
            if (old != base) old_diff++;
            if (ordered != base) new_diff++;
            if (pold != pbase) old_port_diff++;
            if (pnew != pbase) new_port_diff++;
            if ((base & ~snapshot) != 0) direct_only_frames++;
            if ((snapshot & ~base) != 0) worker_only_frames++;
            /* A down transition present in the frame-boundary reference but
             * absent from a consumer's level is a candidate late press.
             * It is not proof of a physically missed attack animation. */
            onset_miss_old += bitcount((base & ~base_prior) & ~old);
            onset_miss_new += bitcount((base & ~base_prior) & ~ordered);
            release_lag_old += bitcount((base_prior & ~base) & old);
            release_lag_new += bitcount((base_prior & ~base) & ordered);
            if (pbase != pworker || pbase != pold || pbase != pnew) {
                if (trace_count < TRACE_LIMIT) {
                    trace_t *t = &trace[trace_count++];
                    t->frame = frames;
                    t->baseline = base;
                    t->worker = snapshot;
                    t->legacy = old;
                    t->ordered = ordered;
                    t->port_base = pbase;
                    t->port_worker = pworker;
                    t->port_old = pold;
                    t->port_new = pnew;
                }
                anomalous_frames++;
            }
            base_prior = base;
            old_prior = old;
            new_prior = ordered;
            frame_valid++;
        } else {
            frame_invalid++;
        }
        frames++;
        if ((frames % 600) == 0)
            print_stats(frames, 0);
        DelayThread(FRAME_US);
    }

    running = 0;
    print_stats(frames, 1);
    for (i = 0; i < (int)trace_count; i++) {
        const trace_t *t = &trace[i];
        printf("[SF2TRACE] frame=%u base=%03x worker=%03x old=%03x new=%03x portBase=%06x portWorker=%06x portOld=%06x portNew=%06x\n",
            t->frame, t->baseline, t->worker, t->legacy, t->ordered,
            t->port_base, t->port_worker, t->port_old, t->port_new);
    }
    printf("[SF2PROBE] END mismatchFrames=%u complete=%u\n",
        anomalous_frames, (unsigned)stop_requested);
    fflush(stdout);
    SleepThread();
    return 0;
}
