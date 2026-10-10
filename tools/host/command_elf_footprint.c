#include "commands.h"

#include "elf_footprint.h"
#include "file.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static int load_footprint(const char *path, host_elf_footprint_t *footprint)
{
    host_buffer_t bytes;
    char error[128];
    int ok;

    host_buffer_init(&bytes);
    if (!host_read_file(path, &bytes)) {
        fprintf(stderr, "elf-footprint: cannot read %s\n", path);
        return 0;
    }
    ok = host_elf_footprint_parse(&bytes, footprint, error, sizeof(error));
    host_buffer_free(&bytes);
    if (!ok)
        fprintf(stderr, "elf-footprint: %s: %s\n", path, error);
    return ok;
}

static uint64_t sum_alloc(const host_elf_footprint_t *result)
{
    return result->text_bytes + result->rodata_bytes +
        result->data_bytes + result->bss_bytes;
}

static void print_row(const char *label, uint64_t baseline, uint64_t candidate,
    int comparing, int csv)
{
    int saving = baseline >= candidate;
    uint64_t difference = saving ? baseline - candidate : candidate - baseline;

    if (csv) {
        if (comparing)
            printf("%s,%" PRIu64 ",%" PRIu64 ",%s%" PRIu64 "\n",
                label, baseline, candidate, saving ? "" : "-", difference);
        else
            printf("%s,%" PRIu64 "\n", label, baseline);
    } else if (comparing) {
        printf("%-24s %14" PRIu64 " %14" PRIu64 " %13s%" PRIu64 "\n",
            label, baseline, candidate, saving ? "+" : "-", difference);
    } else {
        printf("%-24s %14" PRIu64 "\n", label, baseline);
    }
}

int command_elf_footprint(int argc, char **argv)
{
    const char *baseline_path = NULL;
    const char *candidate_path = NULL;
    host_elf_footprint_t baseline;
    host_elf_footprint_t candidate;
    int comparing;
    int csv = 0;
    int i;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--csv") == 0)
            csv = 1;
        else if (argv[i][0] == '-' || (baseline_path != NULL && candidate_path != NULL))
            break;
        else if (!baseline_path)
            baseline_path = argv[i];
        else
            candidate_path = argv[i];
    }
    if (i != argc || baseline_path == NULL) {
        fprintf(stderr, "usage: njemu-tool elf-footprint BASELINE.elf [CANDIDATE.elf] [--csv]\n");
        return 2;
    }

    comparing = candidate_path != NULL;
    if (!load_footprint(baseline_path, &baseline) ||
        (comparing && !load_footprint(candidate_path, &candidate)))
        return 1;

    if (!comparing)
        memset(&candidate, 0, sizeof(candidate));

    if (csv)
        puts(comparing ? "metric,baseline_bytes,candidate_bytes,saved_bytes" :
             "metric,bytes");
    else if (comparing)
        printf("%-24s %14s %14s %14s\n", "Allocated section class",
            "Baseline", "Candidate", "Bytes saved");
    else
        printf("%-24s %14s\n", "Allocated section class", "Bytes");

    print_row("text_exec", baseline.text_bytes, candidate.text_bytes, comparing, csv);
    print_row("rodata_readonly", baseline.rodata_bytes, candidate.rodata_bytes, comparing, csv);
    print_row("data_writable", baseline.data_bytes, candidate.data_bytes, comparing, csv);
    print_row("bss_zero_init", baseline.bss_bytes, candidate.bss_bytes, comparing, csv);
    print_row("allocated_total", sum_alloc(&baseline), sum_alloc(&candidate), comparing, csv);
    print_row("load_file_total", baseline.load_file_bytes, candidate.load_file_bytes, comparing, csv);
    print_row("load_memory_total", baseline.load_memory_bytes, candidate.load_memory_bytes, comparing, csv);
    print_row("elf_file", baseline.file_bytes, candidate.file_bytes, comparing, csv);
    return 0;
}
