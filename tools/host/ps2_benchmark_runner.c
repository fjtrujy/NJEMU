#define _POSIX_C_SOURCE 200809L
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#endif
#include "ps2_benchmark_runner.h"
#include "ps2_benchmark_result.h"
#include "file.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef _WIN32
static int parse_bounded(const char *text, unsigned min, unsigned max, unsigned *out)
{
    char *end;
    unsigned long n;
    errno = 0;
    n = strtoul(text, &end, 10);
    if (errno || end == text || *end || n < min || n > max)
        return 0;
    *out = (unsigned)n;
    return 1;
}

static int safe_component(const char *text)
{
    if (!*text) return 0;
    while (*text) {
        if ((*text < 'a' || *text > 'z') &&
            (*text < 'A' || *text > 'Z') &&
            (*text < '0' || *text > '9') &&
            *text != '_' && *text != '-' && *text != '.')
            return 0;
        ++text;
    }
    return 1;
}

static int forbidden_output(const char *path)
{
    char ancestor[PATH_MAX];
    char resolved[PATH_MAX];
    const char *p = path;
    const char *candidate = path;
    if (strlen(path) >= sizeof(ancestor)) return 1;
    strcpy(ancestor, path);
    while (realpath(ancestor, resolved) == NULL) {
        char *slash = strrchr(ancestor, '/');
        if (slash == NULL) break;
        if (slash == ancestor)
            slash[1] = '\0';
        else
            *slash = '\0';
    }
    if (realpath(ancestor, resolved) != NULL)
        candidate = resolved;
    p = candidate;
    while (*p) {
        const char *start = p;
        size_t n;
        while (*p && *p != '/') ++p;
        n = (size_t)(p - start);
        if (n == 9 && !strncmp(start, "resources", n))
            return 1;
        if (*p) ++p;
    }
    p = path;
    while (*p) {
        const char *start = p;
        size_t n;
        while (*p && *p != '/') ++p;
        n = (size_t)(p - start);
        if (n == 9 && !strncmp(start, "resources", n))
            return 1;
        if (*p) ++p;
    }
    return 0;
}

/* Never modify an existing game selector: stage symlinks to the build's
 * runtime files in a fresh output-owned directory instead. */
static int stage_runtime(const char *source, char *staged, size_t staged_size,
    const char *output_dir, const char *rom)
{
    DIR *dir;
    struct dirent *entry;
    char templ[PATH_MAX];
    char original[PATH_MAX];
    char link_path[PATH_MAX];
    char selector[PATH_MAX];
    char absolute[PATH_MAX];
    struct stat info;
    int n;
    if (!realpath(source, absolute) ||
        strstr(absolute, "/resources/") ||
        (strlen(absolute) >= 10 &&
         strcmp(absolute + strlen(absolute) - 10, "/resources") == 0) ||
        !strcmp(absolute, "/") ||
        stat(absolute, &info) != 0 || !S_ISDIR(info.st_mode))
        return 0;
    n = snprintf(templ, sizeof(templ), "%s/ps2-stage-XXXXXX", output_dir);
    if (n < 0 || (size_t)n >= sizeof(templ) || !mkdtemp(templ))
        return 0;
    if (strlen(templ) >= staged_size) return 0;
    strcpy(staged, templ);
    dir = opendir(absolute);
    if (!dir) return 0;
    while ((entry = readdir(dir)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..") ||
            !strcmp(entry->d_name, "game_name.ini"))
            continue;
        if (snprintf(original, sizeof(original), "%s/%s", absolute,
                entry->d_name) >= (int)sizeof(original) ||
            snprintf(link_path, sizeof(link_path), "%s/%s", staged,
                entry->d_name) >= (int)sizeof(link_path) ||
            symlink(original, link_path) != 0) {
            closedir(dir);
            return 0;
        }
    }
    closedir(dir);
    if (snprintf(selector, sizeof(selector), "%s/game_name.ini", staged) >=
        (int)sizeof(selector))
        return 0;
    return host_write_file(selector, rom, strlen(rom));
}

static void remove_staging(const char *path)
{
    DIR *dir = opendir(path);
    struct dirent *entry;
    char child[PATH_MAX];
    if (!dir) return;
    while ((entry = readdir(dir)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;
        if (snprintf(child, sizeof(child), "%s/%s", path,
                entry->d_name) < (int)sizeof(child))
            unlink(child); /* Symlinks only: never traverse them. */
    }
    closedir(dir);
    rmdir(path);
}

static pid_t spawn_client(const char *binary, const char *ip,
    const char *command, const char *argument, const char *cwd,
    const char *log_path)
{
    pid_t pid = fork();
    if (pid == 0) {
        int output = open(log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (output < 0 || dup2(output, STDOUT_FILENO) < 0 ||
            dup2(output, STDERR_FILENO) < 0 ||
            chdir(cwd) != 0)
            _exit(126);
        close(output);
        if (argument)
            execl(binary, binary, "-h", ip, command, argument, (char *)NULL);
        else
            execl(binary, binary, "-h", ip, command, (char *)NULL);
        _exit(127);
    }
    return pid;
}

static void stop_child(pid_t pid)
{
    int status;
    if (pid <= 0) return;
    if (waitpid(pid, &status, WNOHANG) == 0) {
        kill(pid, SIGTERM);
        for (int i = 0; i < 10; ++i) {
            struct timespec delay = {0, 100000000};
            if (waitpid(pid, &status, WNOHANG) != 0)
                return;
            nanosleep(&delay, NULL);
        }
        kill(pid, SIGKILL);
    }
    waitpid(pid, &status, 0);
}

static int log_result(const char *path, const char *core, const char *rom,
    const char *compiler, unsigned frames)
{
    host_buffer_t file;
    ps2_benchmark_result_t result;
    char error[80];
    int ok = 0;
    host_buffer_init(&file);
    if (host_read_file(path, &file)) {
        if (ps2_benchmark_parse((char *)file.data, &result, error, sizeof(error)) &&
            strcmp(result.core, core) == 0 &&
            strcmp(result.rom, rom) == 0 &&
            strcmp(result.compiler, compiler) == 0 &&
            result.expected == frames && result.frames == frames)
            ok = 1;
    }
    host_buffer_free(&file);
    return ok;
}

int ps2_benchmark_run(int argc, char **argv)
{
    const char *matrix = argv[2];
    const char *core = argv[3];
    const char *rom = argv[4];
    const char *compiler = argv[5];
    const char *client = argv[6];
    const char *ip = argv[7];
    const char *build_dir = argv[8];
    const char *elf_name = argv[9];
    const char *output_dir = argv[10];
    unsigned repeat = 0, timeout = 0, frames = 0;
    char stage[PATH_MAX], elf[PATH_MAX], log[PATH_MAX];
    char *logs[PS2_BENCHMARK_MAX_TRIALS];
    int failed = 0;
    int rc;
    (void)argc;
    memset(logs, 0, sizeof(logs));
    if ((strcmp(core, "CPS1") && strcmp(core, "CPS2") && strcmp(core, "MVS")) ||
        (strcmp(compiler, "GCC") && strcmp(compiler, "LLVM")) ||
        !safe_component(rom) || !safe_component(elf_name) ||
        !parse_bounded(argv[11], 3, PS2_BENCHMARK_MAX_TRIALS, &repeat) ||
        !parse_bounded(argv[12], 1, 86400, &timeout) ||
        !parse_bounded(argv[13], 1, 100000, &frames) ||
        forbidden_output(output_dir) ||
        !host_make_directories(output_dir)) {
        fprintf(stderr, "ps2-benchmark: invalid launch arguments\n");
        return 2;
    }
    if (!ps2_benchmark_matrix_can_attempt(matrix, core, rom)) {
        fprintf(stderr, "ps2-benchmark: ROM is absent from matrix or marked ineligible\n");
        return 2;
    }
    for (unsigned trial = 0; trial < repeat; ++trial) {
        pid_t listener = -1, launch = -1;
        time_t started;
        struct timespec delay = {0, 250000000};
        int completed = 0;
        stage[0] = '\0';
        if (snprintf(log, sizeof(log), "%s/%s_%s_%s_%03u.log", output_dir,
                core, rom, compiler, trial + 1) >= (int)sizeof(log) ||
            host_file_exists(log) ||
            !stage_runtime(build_dir, stage, sizeof(stage), output_dir, rom) ||
            snprintf(elf, sizeof(elf), "host:%s", elf_name) >= (int)sizeof(elf)) {
            fprintf(stderr, "ps2-benchmark: staging failed, refusing to overwrite existing log\n");
            failed = 1;
            if (stage[0]) remove_staging(stage);
            break;
        }
        listener = spawn_client(client, ip, "listen", NULL, stage, log);
        if (listener <= 0) {
            failed = 1;
            remove_staging(stage);
            break;
        }
        nanosleep(&delay, NULL);
        launch = spawn_client(client, ip, "execee", elf, stage, log);
        if (launch <= 0) {
            failed = 1;
            stop_child(listener);
            remove_staging(stage);
            break;
        }
        started = time(NULL);
        while ((time(NULL) - started) < (time_t)timeout) {
            if (log_result(log, core, rom, compiler, frames)) {
                completed = 1;
                break;
            }
            nanosleep(&delay, NULL);
        }
        stop_child(launch);
        stop_child(listener);
        remove_staging(stage);
        if (!completed) {
            fprintf(stderr, "ps2-benchmark: trial %u failed or timed out: %s\n",
                trial + 1, log);
            failed = 1;
        }
        logs[trial] = (char *)malloc(strlen(log) + 1);
        if (!logs[trial]) { failed = 1; break; }
        strcpy(logs[trial], log);
        printf("ps2-benchmark: trial %u %s (%s)\n",
            trial + 1, completed ? "captured" : "failed", log);
    }
    {
        int count = 0;
        for (unsigned i = 0; i < repeat && logs[i]; ++i)
            ++count;
        rc = ps2_benchmark_write_reports(matrix, output_dir, count, logs);
    }
    for (unsigned i = 0; i < repeat; ++i)
        free(logs[i]);
    return failed || rc ? 1 : 0;
}
#else
int ps2_benchmark_run(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    fprintf(stderr, "ps2-benchmark run requires a POSIX host\n");
    return 2;
}
#endif
