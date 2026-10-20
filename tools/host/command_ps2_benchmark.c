#include "commands.h"
#include "ps2_benchmark_result.h"
#include "file.h"
#include "ps2_benchmark_runner.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CANDIDATES 64
#define MAX_LOGS 512
#define MAX_PATH_LENGTH 1024

typedef struct candidate {
    char core[8];
    char rom[PS2_BENCHMARK_MAX_ROM];
    char scenario[24];
    char status[24];
} candidate_t;

typedef struct trial {
    ps2_benchmark_result_t value;
    char path[MAX_PATH_LENGTH];
    char error[80];
    char environment[32];
    int parsed;
    int candidate_index;
} trial_t;

static candidate_t candidates[MAX_CANDIDATES];
static trial_t trials[MAX_LOGS];

/* Preserve the PCSX2 version visible in the log. Unrecognized logs must not
 * be silently labelled as real PS2 measurements. */
static void identify_environment(const char *log, char *output, size_t size)
{
    const char *start = strstr(log, "PCSX2 v");
    size_t length = 0;
    if (start == NULL) {
        snprintf(output, size, "unknown");
        return;
    }
    start += strlen("PCSX2 ");
    while ((start[length] >= '0' && start[length] <= '9') ||
           (start[length] >= 'a' && start[length] <= 'z') ||
           (start[length] >= 'A' && start[length] <= 'Z') ||
           start[length] == '.' || start[length] == '-')
        ++length;
    if (length == 0 || length > 20) {
        snprintf(output, size, "PCSX2-unknown-version");
        return;
    }
    snprintf(output, size, "PCSX2-%.*s", (int)length, start);
}

static int is_safe_name(const char *s)
{
    if (*s == '\0') return 0;
    while (*s) {
        if ((*s < 'a' || *s > 'z') && (*s < 'A' || *s > 'Z') &&
            (*s < '0' || *s > '9') && *s != '_' && *s != '-')
            return 0;
        ++s;
    }
    return 1;
}

static int load_matrix(const char *path, size_t *count)
{
    host_buffer_t contents;
    char *line;
    size_t seen = 0;
    host_buffer_init(&contents);
    if (!host_read_file(path, &contents)) {
        fprintf(stderr, "ps2-benchmark: cannot read matrix: %s\n", path);
        return 0;
    }
    line = strtok((char *)contents.data, "\r\n");
    while (line) {
        if (*line != '#' && *line != '\0') {
            char *cells[5] = {0};
            char *p = line;
            size_t i;
            cells[0] = p;
            for (i = 1; i < 5; ++i) {
                p = strchr(p, '\t');
                if (!p) break;
                *p++ = '\0';
                cells[i] = p;
            }
            if (i < 5 || seen == MAX_CANDIDATES ||
                !is_safe_name(cells[0]) || !is_safe_name(cells[1]) ||
                !is_safe_name(cells[2]) || !is_safe_name(cells[3]) ||
                strlen(cells[0]) >= sizeof(candidates[seen].core) ||
                strlen(cells[1]) >= sizeof(candidates[seen].rom) ||
                strlen(cells[2]) >= sizeof(candidates[seen].scenario) ||
                strlen(cells[3]) >= sizeof(candidates[seen].status) ||
                (strcmp(cells[0], "CPS1") && strcmp(cells[0], "CPS2") &&
                 strcmp(cells[0], "MVS"))) {
                fprintf(stderr, "ps2-benchmark: invalid matrix row %zu\n", seen + 1);
                host_buffer_free(&contents);
                return 0;
            }
            strcpy(candidates[seen].core, cells[0]);
            strcpy(candidates[seen].rom, cells[1]);
            strcpy(candidates[seen].scenario, cells[2]);
            strcpy(candidates[seen].status, cells[3]);
            ++seen;
        }
        line = strtok(NULL, "\r\n");
    }
    host_buffer_free(&contents);
    *count = seen;
    return seen != 0;
}

int ps2_benchmark_matrix_can_attempt(const char *path,
    const char *core, const char *rom)
{
    size_t count = 0;
    size_t i;
    if (!load_matrix(path, &count))
        return 0;
    for (i = 0; i < count; ++i) {
        if (!strcmp(candidates[i].core, core) &&
            !strcmp(candidates[i].rom, rom))
            return strncmp(candidates[i].status, "ineligible", 10) != 0;
    }
    return 0;
}

static int candidate_lookup(const ps2_benchmark_result_t *r, size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i)
        if (!strcmp(r->core, candidates[i].core) &&
            !strcmp(r->rom, candidates[i].rom))
            return (int)i;
    return -1;
}

static int append_csv_field(host_buffer_t *out, const char *value)
{
    const char *p = value;
    if (!host_buffer_append_string(out, "\"")) return 0;
    while (*p) {
        if (*p == '"' && !host_buffer_append_string(out, "\""))
            return 0;
        if (!host_buffer_append(out, p, 1)) return 0;
        ++p;
    }
    return host_buffer_append_string(out, "\"");
}

int ps2_benchmark_write_reports(const char *matrix_path, const char *output_dir,
    int log_count, char *const *logs)
{
    size_t count = 0;
    host_buffer_t csv;
    host_buffer_t md;
    int i, valid = 0;
    int ok = 1;
    char csv_path[MAX_PATH_LENGTH], md_path[MAX_PATH_LENGTH];
    if (log_count < 0 || log_count > MAX_LOGS ||
        ps2_benchmark_forbidden_output(output_dir) ||
        !load_matrix(matrix_path, &count))
        return 1;
    if (snprintf(csv_path, sizeof(csv_path), "%s/trials.csv", output_dir) >= (int)sizeof(csv_path) ||
        snprintf(md_path, sizeof(md_path), "%s/summary.md", output_dir) >= (int)sizeof(md_path) ||
        !host_make_directories(output_dir)) {
        fprintf(stderr, "ps2-benchmark: cannot create report output directory\n");
        return 1;
    }
    host_buffer_init(&csv);
    host_buffer_init(&md);
    ok &= host_buffer_append_string(&csv, "core,rom,compiler,environment,trial,frames,total_ms,avg_ms_per_emulated_frame,sampled_frame_mean_ms,fps,p95_ms,core_update_total_ms,screen_update_total_ms,ticker_pair_ns,io_ops,cpu_bytes,gfx_bytes,pcm_bytes,resident,result,state,scenario,revision,worktree,config_hash,compiler_version,log\n");
    ok &= host_buffer_append_string(&md, "# PS2 compiler benchmark\n\n");
    ok &= host_buffer_append_string(&md, "PCSX2 measurements can be used for controlled emulator-only GCC/LLVM comparisons once residency, I/O and matching state trajectories are independently validated. PCSX2 timings are **not physical-PS2 performance**. The environment column is inferred from the log; `unknown` does not establish real hardware, and identical emulator versions do not prove identical configuration.\n\n");
    ok &= host_buffer_append_string(&md, "## Trials\n\n| Core | ROM | Environment | Compiler | Trial | Frames | Total ms | Mean ms/frame | FPS | P95 ms | I/O | Status |\n| --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |\n");

    for (i = 0; i < log_count && ok; ++i) {
        host_buffer_t raw;
        trial_t *t = &trials[i];
        char display[80];
        const ps2_benchmark_result_t *r = &t->value;
        double ms, mean, sampled_mean, fps, p95;
        int trial_number = 1;
        memset(t, 0, sizeof(*t));
        if (strlen(logs[i]) >= sizeof(t->path)) {
            fprintf(stderr, "ps2-benchmark: log path too long\n");
            ok = 0;
            break;
        }
        strcpy(t->path, logs[i]);
        host_buffer_init(&raw);
        if (!host_read_file(logs[i], &raw)) {
            snprintf(t->error, sizeof(t->error), "unreadable_log");
        } else {
            identify_environment((char *)raw.data, t->environment, sizeof(t->environment));
        }
        if (t->error[0]) {
            /* Keep unreadable logs as failed rows. */
        } else if (!ps2_benchmark_parse((char *)raw.data, &t->value, t->error, sizeof(t->error))) {
            /* Keep failed/malformed runs as explicit rows. */
        } else {
            t->parsed = 1;
            t->candidate_index = candidate_lookup(r, count);
            if (t->candidate_index < 0)
                snprintf(t->error, sizeof(t->error), "not_in_matrix");
            else if (!strncmp(candidates[t->candidate_index].status, "ineligible", 10))
                snprintf(t->error, sizeof(t->error), "ineligible_rom");
            else if (strcmp(r->scenario, candidates[t->candidate_index].scenario))
                snprintf(t->error, sizeof(t->error), "scenario_mismatch");
            else if (!ps2_benchmark_result_accepted(r))
                snprintf(t->error, sizeof(t->error), "%s",
                    strcmp(r->status, "ok") ? r->status : "unverified");
            else
                ++valid;
        }
        host_buffer_free(&raw);
        /* Trial numbers are local to a core/ROM/compiler/environment group,
         * not global indices in a mixed multi-ROM report. */
        if (t->parsed) {
            int previous;
            for (previous = 0; previous < i; ++previous) {
                const trial_t *prior = &trials[previous];
                if (prior->parsed &&
                    !strcmp(prior->value.core, r->core) &&
                    !strcmp(prior->value.rom, r->rom) &&
                    !strcmp(prior->value.compiler, r->compiler) &&
                    !strcmp(prior->environment, t->environment))
                    ++trial_number;
            }
        } else {
            trial_number = i + 1;
        }
        snprintf(display, sizeof(display), "%s", t->error[0] ? t->error : "accepted");
        ms = (double)r->total_us / 1000.0;
        mean = r->frames ? (double)r->total_us / (1000.0 * r->frames) : 0;
        sampled_mean = r->frames ? (double)r->sample_sum_us / (1000.0 * r->frames) : 0;
        fps = r->total_us ? 1000000.0 * r->frames / r->total_us : 0;
        p95 = (double)r->p95_us / 1000.0;
        ok &= host_buffer_append_format(&csv, "%s,%s,%s,%s,%d,%u,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%u,%u,%u,%u,%u,%s,",
            t->parsed ? r->core : "unknown",
            t->parsed ? r->rom : "unknown",
            t->parsed ? r->compiler : "unknown",
            t->environment[0] ? t->environment : "unknown",
            trial_number, r->frames, ms, mean, sampled_mean, fps, p95,
            (double)r->core_update_us / 1000.0,
            (double)r->screen_update_us / 1000.0,
            r->ticker_pair_ns, r->io_ops,
            r->cpu_bytes, r->gfx_bytes, r->pcm_bytes, r->resident);
        ok &= append_csv_field(&csv, display);
        ok &= host_buffer_append_format(&csv, ",%s,%s,%s,%s,%s,%s,",
            r->state, r->scenario, r->revision, r->worktree_hash,
            r->configuration_hash, r->compiler_version);
        ok &= append_csv_field(&csv, t->path);
        ok &= host_buffer_append_string(&csv, "\n");
        ok &= host_buffer_append_format(&md,
            "| %s | %s | %s | %s | %d | %u | %.3f | %.3f | %.3f | %.3f | %u | %s |\n",
            t->parsed ? r->core : "unknown", t->parsed ? r->rom : "unknown",
            t->environment[0] ? t->environment : "unknown",
            t->parsed ? r->compiler : "unknown", trial_number, r->frames,
            ms, mean, fps, p95, r->io_ops, display);
    }

    ok &= host_buffer_append_string(&md,
        "\n## GCC versus LLVM\n\n| Core | ROM | GCC median ms | LLVM median ms | Speedup | Improvement % | Result |\n| --- | --- | ---: | ---: | ---: | ---: | --- |\n");
    for (size_t j = 0; j < count && ok; ++j) {
        uint64_t durations[2][PS2_BENCHMARK_MAX_TRIALS];
        size_t n[2] = {0,0};
        const ps2_benchmark_result_t *anchor = NULL;
        const char *anchor_environment = NULL;
        int comparable = 1;
        const char *reason = "insufficient_or_unverified_trials";
        for (i = 0; i < log_count; ++i) {
            const trial_t *t = &trials[i];
            const ps2_benchmark_result_t *r = &t->value;
            int k;
            if (!t->parsed) {
                comparable = 0;
                reason = "unparseable_trial";
                continue;
            }
            if (t->candidate_index != (int)j)
                continue;
            if (t->error[0]) {
                comparable = 0;
                reason = "failed_or_incomparable_trial";
                continue;
            }
            k = strcmp(r->compiler, "GCC") == 0 ? 0 : 1;
            if (n[k] == PS2_BENCHMARK_MAX_TRIALS) { comparable = 0; break; }
            durations[k][n[k]++] = r->total_us;
            if (anchor == NULL)
                anchor = r, anchor_environment = t->environment;
            else if (strcmp(anchor->state_hash, r->state_hash) != 0 ||
                     strcmp(anchor_environment, t->environment) != 0 ||
                     strcmp(anchor->revision, r->revision) != 0 ||
                     strcmp(anchor->worktree_hash, r->worktree_hash) != 0 ||
                     strcmp(anchor->configuration_hash, r->configuration_hash) != 0 ||
                     strcmp(anchor->scenario, r->scenario) != 0 ||
                     anchor->cpu_bytes != r->cpu_bytes ||
                     anchor->gfx_bytes != r->gfx_bytes ||
                     anchor->pcm_bytes != r->pcm_bytes ||
                     anchor->frames != r->frames ||
                     anchor->warmup != r->warmup) {
                comparable = 0;
                reason = "divergent_state_or_configuration";
            }
        }
        if (n[0] < 3 || n[1] < 3)
            comparable = 0;
        if (comparable) {
            uint64_t gcc = ps2_benchmark_median(durations[0], n[0]);
            uint64_t llvm = ps2_benchmark_median(durations[1], n[1]);
            if (gcc && llvm) {
                double speedup = (double)gcc / llvm;
                double improvement = ((double)gcc - (double)llvm) * 100.0 / gcc;
                ok &= host_buffer_append_format(&md,
                    "| %s | %s | %.3f | %.3f | %.4f | %.2f | comparable |\n",
                    candidates[j].core, candidates[j].rom,
                    (double)gcc / 1000.0, (double)llvm / 1000.0,
                    speedup, improvement);
                continue;
            }
        }
        ok &= host_buffer_append_format(&md,
            "| %s | %s | - | - | - | - | %s |\n",
            candidates[j].core, candidates[j].rom, reason);
    }

    ok &= host_write_file(csv_path, csv.data, csv.size);
    ok &= host_write_file(md_path, md.data, md.size);
    host_buffer_free(&csv);
    host_buffer_free(&md);
    if (!ok) {
        fprintf(stderr, "ps2-benchmark: could not generate reports\n");
        return 1;
    }
    printf("ps2-benchmark: %d logs, %d accepted for aggregation, reports: %s, %s\n",
        log_count, valid, csv_path, md_path);
    return 0;
}

static void usage(void)
{
    fprintf(stderr,
        "usage:\n"
        "  njemu-tool ps2-benchmark report MATRIX.tsv OUT_DIR [TRIAL.log ...]\n"
        "  njemu-tool ps2-benchmark matrix MATRIX.tsv\n"
        "  njemu-tool ps2-benchmark run MATRIX.tsv CORE ROM COMPILER PS2CLIENT PS2_IP BUILD_DIR ELF_NAME OUT_DIR TRIALS TIMEOUT_SEC FRAMES\n"
        "  njemu-tool ps2-benchmark pcsx2 MATRIX.tsv CORE ROM COMPILER PCSX2_BIN BUILD_DIR ELF_NAME OUT_DIR TRIALS TIMEOUT_SEC FRAMES\n");
}

int command_ps2_benchmark(int argc, char **argv)
{
    size_t count = 0;
    if (argc >= 4 && !strcmp(argv[1], "report"))
        return ps2_benchmark_write_reports(argv[2], argv[3], argc - 4, argv + 4);
    if (argc == 14 && !strcmp(argv[1], "run"))
        return ps2_benchmark_run(argc, argv);
    if (argc == 13 && !strcmp(argv[1], "pcsx2"))
        return ps2_benchmark_run(argc, argv);
    if (argc == 3 && !strcmp(argv[1], "matrix")) {
        if (!load_matrix(argv[2], &count)) return 1;
        for (size_t i = 0; i < count; ++i)
            printf("%s\t%s\t%s\t%s\n", candidates[i].core,
                candidates[i].rom, candidates[i].scenario, candidates[i].status);
        return 0;
    }
    usage();
    return 2;
}
