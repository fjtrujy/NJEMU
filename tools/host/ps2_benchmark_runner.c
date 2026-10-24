#ifndef _WIN32
/* realpath() is XSI; _POSIX_C_SOURCE alone does not expose it on musl. */
#define _XOPEN_SOURCE 700
#endif
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

int ps2_benchmark_forbidden_output(const char *path)
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

/* PCSX2 resolves ELF symlinks before choosing the host: root. The ELF must be
 * an independent file in the stage directory, not a symlink to the build. */
static int copy_file(const char *source, const char *destination)
{
    char buffer[32768];
    FILE *input = fopen(source, "rb");
    FILE *output;
    size_t n;
    int ok = 1;
    if (!input) return 0;
    output = fopen(destination, "wb");
    if (!output) { fclose(input); return 0; }
    while ((n = fread(buffer, 1, sizeof(buffer), input)) != 0) {
        if (fwrite(buffer, 1, n, output) != n) { ok = 0; break; }
    }
    if (ferror(input) || fclose(output) != 0) ok = 0;
    fclose(input);
    if (!ok) unlink(destination);
    return ok;
}

/* Only the selected ROM's persistent files belong in a benchmark fixture.
 * Never link writable save directories into a shared build, where trial N
 * would otherwise silently change trial N+1's initial emulated state. */
static int stage_writable_dir(const char *source, const char *destination,
    const char *directory, const char *rom)
{
    const char *extension = !strcmp(directory, "config") ? ".ini" :
        !strcmp(directory, "nvram") ? ".nv" :
        !strcmp(directory, "memcard") ? ".bin" : NULL;
    char original_file[PATH_MAX], staged_file[PATH_MAX];
    struct stat info;
    int n;
    if (lstat(source, &info) != 0 || !S_ISDIR(info.st_mode) ||
        mkdir(destination, 0700) != 0)
        return 0;
    /* Save-state playback is not part of the current attract-mode fixture.
     * Give it a private directory for output, but no inherited save files. */
    if (extension == NULL) return 1;
    n = snprintf(original_file, sizeof(original_file), "%s/%s%s",
        source, rom, extension);
    if (n < 0 || (size_t)n >= sizeof(original_file)) return 0;
    if (lstat(original_file, &info) != 0)
        return errno == ENOENT;
    /* Refuse to follow an unexpected symlink into user-owned resources. */
    if (!S_ISREG(info.st_mode)) return 0;
    n = snprintf(staged_file, sizeof(staged_file), "%s/%s%s",
        destination, rom, extension);
    if (n < 0 || (size_t)n >= sizeof(staged_file)) return 0;
    return copy_file(original_file, staged_file);
}

/* Never modify an existing game selector: stage build runtime links and a
 * private selector in a fresh output-owned directory instead. */
static int stage_runtime(const char *source, char *staged, size_t staged_size,
    const char *output_dir, const char *rom, const char *elf_name,
    int pcsx2)
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
    /* PCSX2 resolves -elf relative to its launch cwd. Always use a fully
     * resolved staging path to avoid looking for stage/stage/game.ELF. */
    if (!realpath(templ, link_path) || strlen(link_path) >= staged_size)
        return 0;
    strcpy(staged, link_path);
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
            ((!strcmp(entry->d_name, "config") ||
              !strcmp(entry->d_name, "nvram") ||
              !strcmp(entry->d_name, "memcard") ||
              !strcmp(entry->d_name, "state")) ?
                !stage_writable_dir(original, link_path, entry->d_name, rom) :
              (pcsx2 && strcmp(entry->d_name, elf_name) == 0) ?
                !copy_file(original, link_path) :
                symlink(original, link_path) != 0)) {
            closedir(dir);
            return 0;
        }
    }
    closedir(dir);
    if (pcsx2) {
        /* Refuse a missing ELF rather than launching another build's binary. */
        if (snprintf(link_path, sizeof(link_path), "%s/%s", staged,
                elf_name) >= (int)sizeof(link_path) ||
            lstat(link_path, &info) != 0 || !S_ISREG(info.st_mode))
            return 0;
    }
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
    struct stat info;
    if (!dir) return;
    while ((entry = readdir(dir)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;
        if (snprintf(child, sizeof(child), "%s/%s", path,
                entry->d_name) >= (int)sizeof(child) ||
            lstat(child, &info) != 0)
            continue;
        /* PCSX2 may create PICTURE/ or other output directories inside the
         * private stage. Recurse into real directories only; symlinks to
         * original build resources must never be followed or modified. */
        if (S_ISDIR(info.st_mode))
            remove_staging(child);
        else
            unlink(child);
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
            execlp(binary, binary, "-h", ip, command, argument, (char *)NULL);
        else
            execlp(binary, binary, "-h", ip, command, (char *)NULL);
        _exit(127);
    }
    return pid;
}

static pid_t spawn_pcsx2(const char *binary, const char *elf,
    const char *cwd, const char *log_path)
{
    pid_t pid = fork();
    if (pid == 0) {
        int output = open(log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (output < 0 || dup2(output, STDOUT_FILENO) < 0 ||
            dup2(output, STDERR_FILENO) < 0 || chdir(cwd) != 0)
            _exit(126);
        close(output);
        /* Uncap PCSX2 itself, not only NJEMU's guest-side frame limiter. */
        execlp(binary, binary, "-nogui", "-batch", "-unlimited", "-elf", elf,
            (char *)NULL);
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

/* 1: full capture; -1: explicit failure/incomplete record; 0: still running. */
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
            strcmp(result.compiler, compiler) == 0) {
            if (result.expected != frames || result.frames != frames ||
                !strcmp(result.status, "failed") ||
                !strcmp(result.status, "rejected"))
                ok = -1;
            else
                ok = 1;
        }
    }
    host_buffer_free(&file);
    return ok;
}

int ps2_benchmark_run(int argc, char **argv)
{
    const int pcsx2 = strcmp(argv[1], "pcsx2") == 0;
    const char *matrix = argv[2];
    const char *core = argv[3];
    const char *rom = argv[4];
    const char *compiler = argv[5];
    const char *client = argv[6];
    const char *ip = pcsx2 ? NULL : argv[7];
    const char *build_dir = argv[pcsx2 ? 7 : 8];
    const char *elf_name = argv[pcsx2 ? 8 : 9];
    const char *output_dir = argv[pcsx2 ? 9 : 10];
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
        !parse_bounded(argv[pcsx2 ? 10 : 11], 1,
            PS2_BENCHMARK_MAX_TRIALS, &repeat) ||
        !parse_bounded(argv[pcsx2 ? 11 : 12], 1, 86400, &timeout) ||
        !parse_bounded(argv[pcsx2 ? 12 : 13], 1, 100000, &frames) ||
        ps2_benchmark_forbidden_output(output_dir) ||
        !host_make_directories(output_dir)) {
        fprintf(stderr, "ps2-benchmark: invalid launch arguments\n");
        return 2;
    }
    /* A completed EE program does not necessarily return control to the
     * resident PS2Link server. Never hot-relaunch unattended: each hardware
     * trial requires a separately verified PS2Link session. */
    if (!pcsx2 && repeat != 1) {
        fprintf(stderr, "ps2-benchmark: PS2Link allows one trial per invocation; "
            "re-verify the console session before the next launch\n");
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
            !stage_runtime(build_dir, stage, sizeof(stage), output_dir, rom,
                elf_name, pcsx2)) {
            fprintf(stderr, "ps2-benchmark: staging failed, refusing to overwrite existing log\n");
            failed = 1;
            if (stage[0]) remove_staging(stage);
            break;
        }
        if ((pcsx2 ? snprintf(elf, sizeof(elf), "%s/%s", stage, elf_name) :
                snprintf(elf, sizeof(elf), "host:%s", elf_name)) >= (int)sizeof(elf)) {
            fprintf(stderr, "ps2-benchmark: staged ELF path too long\n");
            failed = 1;
            remove_staging(stage);
            break;
        }
        if (!pcsx2) {
            listener = spawn_client(client, ip, "listen", NULL, stage, log);
            if (listener <= 0) {
                failed = 1;
                remove_staging(stage);
                break;
            }
            nanosleep(&delay, NULL);
        }
        launch = pcsx2 ? spawn_pcsx2(client, elf, stage, log) :
            spawn_client(client, ip, "execee", elf, stage, log);
        if (launch <= 0) {
            failed = 1;
            stop_child(listener);
            remove_staging(stage);
            break;
        }
        started = time(NULL);
        while ((time(NULL) - started) < (time_t)timeout) {
            int child_status;
            int progress = log_result(log, core, rom, compiler, frames);
            if (progress > 0) {
                completed = 1;
                break;
            }
            if (progress < 0)
                break;
            /* Do not burn the full timeout when PCSX2 rejects an ELF or
             * exits before the benchmark record has been written. */
            if (waitpid(launch, &child_status, WNOHANG) == launch)
                break;
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
int ps2_benchmark_forbidden_output(const char *path)
{
    const char *p = path;
    while (*p) {
        const char *start = p;
        size_t n;
        while (*p && *p != '/' && *p != '\\') ++p;
        n = (size_t)(p - start);
        if (n == 9 && !strncmp(start, "resources", n))
            return 1;
        if (*p) ++p;
    }
    return 0;
}

int ps2_benchmark_run(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    fprintf(stderr, "ps2-benchmark run requires a POSIX host\n");
    return 2;
}
#endif
