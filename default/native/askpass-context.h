#ifndef ASKPASS_CONTEXT_H
#define ASKPASS_CONTEXT_H

#include <stddef.h>
#include <sys/types.h>

#define ASKPASS_STRING_LIMIT 1024
#define ASKPASS_PROCESS_LIMIT 16

struct askpass_process {
    pid_t pid;
    pid_t ppid;
    char executable[ASKPASS_STRING_LIMIT + 1];
};

struct askpass_context {
    struct askpass_process processes[ASKPASS_PROCESS_LIMIT];
    size_t count;
    char cwd[ASKPASS_STRING_LIMIT + 1];
    char operation[16];
    char destination[ASKPASS_STRING_LIMIT + 1];
};

/* These interpret only identified Git/SSH executables, with argv[0] included. */
const char *askpass_git_operation(size_t argc, const char *const argv[]);
const char *askpass_ssh_destination(size_t argc, const char *const argv[]);
void askpass_collect_context(pid_t parent, struct askpass_context *context);

#endif
