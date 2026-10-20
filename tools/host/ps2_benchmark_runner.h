#ifndef NJEMU_HOST_PS2_BENCHMARK_RUNNER_H
#define NJEMU_HOST_PS2_BENCHMARK_RUNNER_H

int ps2_benchmark_run(int argc, char **argv);
int ps2_benchmark_matrix_can_attempt(const char *path,
    const char *core, const char *rom);
int ps2_benchmark_write_reports(const char *matrix_path, const char *output_dir,
    int count, char *const *logs);

#endif
