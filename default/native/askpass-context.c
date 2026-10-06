#define _POSIX_C_SOURCE 200809L
#include "askpass-context.h"
#include "askpass-json.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PROC_FILE_LIMIT 16384
#define ARGUMENT_LIMIT 256

const char *askpass_git_operation(size_t argc, const char *const argv[])
{
    size_t i;
    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "-C") == 0 || strcmp(arg, "-c") == 0 ||
            strcmp(arg, "--git-dir") == 0 || strcmp(arg, "--work-tree") == 0 ||
            strcmp(arg, "--namespace") == 0 || strcmp(arg, "--config-env") == 0) {
            if (++i >= argc || !argv[i][0]) return NULL;
        } else if ((strncmp(arg, "-C", 2) == 0 || strncmp(arg, "-c", 2) == 0) && arg[2]) {
            continue;
        } else if (strncmp(arg, "--git-dir=", 10) == 0 || strncmp(arg, "--work-tree=", 12) == 0 ||
                   strncmp(arg, "--namespace=", 12) == 0 || strncmp(arg, "--config-env=", 13) == 0 ||
                   strcmp(arg, "--bare") == 0 || strcmp(arg, "--no-pager") == 0 ||
                   strcmp(arg, "--paginate") == 0 || strcmp(arg, "-p") == 0 ||
                   strcmp(arg, "-P") == 0 || strcmp(arg, "--no-replace-objects") == 0 ||
                   strcmp(arg, "--no-optional-locks") == 0 || strcmp(arg, "--literal-pathspecs") == 0 ||
                   strcmp(arg, "--glob-pathspecs") == 0 || strcmp(arg, "--noglob-pathspecs") == 0 ||
                   strcmp(arg, "--icase-pathspecs") == 0) {
            continue;
        } else {
            static const char *const operations[] = { "clone", "fetch", "pull", "push", "ls-remote" };
            size_t j;
            for (j = 0; j < sizeof operations / sizeof operations[0]; ++j)
                if (strcmp(arg, operations[j]) == 0) return operations[j];
            return NULL;
        }
    }
    return NULL;
}

const char *askpass_ssh_destination(size_t argc, const char *const argv[])
{
    size_t i;
    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        size_t j;
        if (strcmp(arg, "--") == 0)
            return i + 1 < argc && argv[i + 1][0] ? argv[i + 1] : NULL;
        if (arg[0] != '-') return arg[0] ? arg : NULL;
        if (!arg[1]) return NULL;
        for (j = 1; arg[j]; ++j) {
            if (strchr("46AaCfGgKkMNnqsTtVvXxYy", arg[j])) continue;
            if (!strchr("BbcDEeFIiJLlmOopPQRSWw", arg[j])) return NULL;
            if (!arg[j + 1] && (++i >= argc || !argv[i][0])) return NULL;
            break;
        }
    }
    return NULL;
}

static ssize_t read_proc(pid_t pid, const char *name, char *buffer, size_t capacity)
{
    char path[64];
    int fd;
    ssize_t n;
    size_t used = 0;
    if (snprintf(path, sizeof path, "/proc/%ld/%s", (long)pid, name) >= (int)sizeof path) return -1;
    fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return -1;
    while (used < capacity) {
        n = read(fd, buffer + used, capacity - used);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { close(fd); return -1; }
        if (n == 0) break;
        used += (size_t)n;
    }
    close(fd);
    /* At the bound, completeness cannot be established. Do not parse a prefix. */
    if (used == capacity) return -1;
    buffer[used] = '\0';
    return (ssize_t)used;
}

static void read_link(pid_t pid, const char *name, char *output, size_t capacity)
{
    char path[64], buffer[PROC_FILE_LIMIT + 1];
    ssize_t n;
    if (snprintf(path, sizeof path, "/proc/%ld/%s", (long)pid, name) >= (int)sizeof path) return;
    n = readlink(path, buffer, PROC_FILE_LIMIT);
    if (n > 0 && n < PROC_FILE_LIMIT)
        askpass_clean_metadata(output, capacity, buffer, (size_t)n);
}

static const char *basename_of(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static void collect_arguments(pid_t pid, const char *executable, struct askpass_context *context,
                              bool *seen_git, bool *seen_ssh)
{
    char buffer[PROC_FILE_LIMIT + 1];
    const char *argv[ARGUMENT_LIMIT];
    const char *name = basename_of(executable), *value;
    ssize_t n;
    size_t pos = 0, argc = 0;
    bool git = strcmp(name, "git") == 0;
    bool ssh = strcmp(name, "ssh") == 0;
    if ((!git && !ssh) || (git && *seen_git) || (ssh && *seen_ssh)) return;
    /* Do not attribute a more distant invocation when the nearer one is ambiguous. */
    if (git) *seen_git = true;
    if (ssh) *seen_ssh = true;
    if (strlen(executable) == ASKPASS_STRING_LIMIT) return;
    n = read_proc(pid, "cmdline", buffer, PROC_FILE_LIMIT);
    if (n <= 0 || buffer[n - 1] != '\0') goto done;
    while (pos < (size_t)n) {
        if (argc == ARGUMENT_LIMIT) goto done;
        argv[argc++] = buffer + pos;
        pos += strlen(buffer + pos) + 1;
    }
    if (git) {
        value = askpass_git_operation(argc, argv);
        if (value) (void)snprintf(context->operation, sizeof context->operation, "%s", value);
    } else if (ssh) {
        value = askpass_ssh_destination(argc, argv);
        if (value && strlen(value) <= ASKPASS_STRING_LIMIT)
            askpass_clean_metadata(context->destination, sizeof context->destination, value, strlen(value));
    }
done:
    askpass_clear(buffer, sizeof buffer);
}

void askpass_collect_context(pid_t parent, struct askpass_context *context)
{
    pid_t pid = parent;
    bool seen_git = false, seen_ssh = false;
    memset(context, 0, sizeof *context);
    while (pid > 0 && context->count < ASKPASS_PROCESS_LIMIT) {
        char buffer[PROC_FILE_LIMIT + 1];
        char *end, state;
        long ppid;
        size_t i;
        struct askpass_process *process;
        for (i = 0; i < context->count; ++i)
            if (context->processes[i].pid == pid) return;
        if (read_proc(pid, "stat", buffer, PROC_FILE_LIMIT) < 0) return;
        end = strrchr(buffer, ')');
        if (!end || sscanf(end + 1, " %c %ld", &state, &ppid) != 2 || ppid < 0 || ppid > INT_MAX) return;
        process = &context->processes[context->count];
        process->pid = pid;
        process->ppid = (pid_t)ppid;
        read_link(pid, "exe", process->executable, sizeof process->executable);
        if (!process->executable[0]) {
            ssize_t n = read_proc(pid, "comm", buffer, PROC_FILE_LIMIT);
            if (n > 0) {
                if (buffer[n - 1] == '\n') --n;
                askpass_clean_metadata(process->executable, sizeof process->executable, buffer, (size_t)n);
            }
        }
        if (context->count == 0) read_link(pid, "cwd", context->cwd, sizeof context->cwd);
        if (context->count == 0 && strcmp(basename_of(process->executable), "ssh-add") == 0)
            seen_ssh = true;
        collect_arguments(pid, process->executable, context, &seen_git, &seen_ssh);
        ++context->count;
        if (pid == 1) return;
        pid = process->ppid;
    }
}
