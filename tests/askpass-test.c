#define _POSIX_C_SOURCE 200809L
#include "askpass-context.h"
#include "askpass-json.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static unsigned checks;
static const char *binary, *runtime, *fixture_dir;
static void require(bool ok, const char *name)
{
    ++checks;
    if (!ok) { fprintf(stderr, "FAIL: %s\n", name); exit(1); }
}
static long long milliseconds(void)
{
    struct timespec t;
    require(clock_gettime(CLOCK_MONOTONIC, &t) == 0, "monotonic clock");
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static bool readable(int fd)
{
    struct pollfd p = {fd, POLLIN, 0};
    return poll(&p, 1, 3000) > 0 && (p.revents & (POLLIN | POLLHUP)) != 0;
}
static const char *base(const char *s) { const char *p = strrchr(s, '/'); return p ? p + 1 : s; }
static int parent_fixture(void)
{
    const char *cwd = getenv("ASKPASS_TEST_CWD");
    if (cwd && chdir(cwd) != 0) return 89;
    pid_t child = fork();
    int status;
    if (child < 0) return 90;
    if (!child) {
        const char *prompt = getenv("ASKPASS_TEST_PROMPT");
        if (prompt) execl(getenv("ASKPASS_TEST_BINARY"), "omarchy-ssh-askpass", prompt, (char *)NULL);
        else execl(getenv("ASKPASS_TEST_BINARY"), "omarchy-ssh-askpass", (char *)NULL);
        _exit(91);
    }
    if (waitpid(child, &status, 0) != child) return 92;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 93;
}
static int listener(void)
{
    struct sockaddr_un address;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    require(fd >= 0, "create test socket");
    memset(&address, 0, sizeof address);
    address.sun_family = AF_UNIX;
    require(snprintf(address.sun_path, sizeof address.sun_path, "%s/omarchy-ssh-askpass.sock", runtime) < (int)sizeof address.sun_path, "test socket path");
    (void)unlink(address.sun_path);
    require(bind(fd, (struct sockaddr *)&address, sizeof address) == 0, "bind isolated socket");
    require(listen(fd, 4) == 0, "listen isolated socket");
    return fd;
}
/* Exercise actual stdout/exit and the wire; read one byte at a time to force
 * receiver-side fragmentation without relying on the kernel's packet sizes. */
static void exchange_case(const char *name, const char *prompt, const char *const *parent_args,
                          const char *reply, const char *expected, bool fragmented,
                          const char *operation, const char *destination)
{
    int server = listener(), pipes[2], client, status;
    pid_t child;
    char request[ASKPASS_REQUEST_LIMIT + 2], output[ASKPASS_REPLY_LIMIT + 32], identity[1400];
    size_t used = 0, outlen = 0;
    require(pipe(pipes) == 0, "stdout pipe");
    child = fork();
    require(child >= 0, "fork controlled parent");
    if (!child) {
        char fixture[2048];
        close(server); close(pipes[0]);
        if (dup2(pipes[1], STDOUT_FILENO) < 0) _exit(94);
        close(pipes[1]);
        if (prompt) (void)setenv("ASKPASS_TEST_PROMPT", prompt, 1);
        else (void)unsetenv("ASKPASS_TEST_PROMPT");
        (void)snprintf(fixture, sizeof fixture, "%s/%s", fixture_dir, parent_args[0]);
        execv(fixture, (char *const *)parent_args);
        _exit(95);
    }
    close(pipes[1]);
    require(readable(server), "bridge reaches test socket");
    client = accept(server, NULL, NULL);
    require(client >= 0, "accept request");
    while (used < sizeof request - 1 && readable(client)) {
        ssize_t n = read(client, request + used, 1);
        if (n <= 0) break;
        if (request[used++] == '\n') break;
    }
    request[used] = '\0';
    require(used > 0 && request[used - 1] == '\n' && used <= ASKPASS_REQUEST_LIMIT, "bounded complete request line");
    (void)snprintf(identity, sizeof identity, "\"processes\":[{\"pid\":%ld,\"executable\":\"%s/%s\"}", (long)child, fixture_dir, parent_args[0]);
    require(strstr(request, identity) != NULL, "actual immediate parent PID and executable");
    {
        const char *cwd = getenv("ASKPASS_TEST_CWD");
        char sanitized[ASKPASS_STRING_LIMIT + 1];
        require(cwd != NULL, "controlled cwd supplied");
        askpass_clean_metadata(sanitized, sizeof sanitized, cwd, strlen(cwd));
        (void)snprintf(identity, sizeof identity, "\"cwd\":\"%s\"", sanitized);
        require(strstr(request, identity) != NULL, "actual caller cwd with sanitized controls and UTF8");
        const char *next = request;
        size_t process_count = 0;
        while ((next = strstr(next, "\"pid\":")) != NULL) { ++process_count; ++next; }
        require(process_count > 0 && process_count <= ASKPASS_PROCESS_LIMIT, "ancestry bounded to 16 processes");
    }
    if (prompt && strcmp(prompt, "quoted \"key\" \\ 雪 😀") == 0)
        require(strstr(request, "\"prompt\":\"quoted \\\"key\\\" \\\\ 雪 😀\"") != NULL, "original quoted UTF8 prompt on wire");
    if (!prompt) require(strstr(request, "\"prompt\":\"SSH passphrase\"") != NULL, "fallback prompt");
    require(strstr(request, "arbitrary-remote-secret") == NULL, "unallowlisted arguments excluded");
    if (operation) {
        (void)snprintf(identity, sizeof identity, "\"operation\":\"%s\"", operation);
        require(strstr(request, identity) != NULL, "recognized operation on wire");
    } else require(strstr(request, "\"operation\"") == NULL, "unknown operation omitted");
    if (destination) {
        (void)snprintf(identity, sizeof identity, "\"destination\":\"%s\"", destination);
        require(strstr(request, identity) != NULL, "supplied destination on wire");
    } else require(strstr(request, "\"destination\"") == NULL, "unknown destination omitted");
    if (reply) {
        size_t length = strlen(reply), offset = 0;
        while (offset < length) {
            size_t chunk = fragmented ? 1 : length - offset;
            ssize_t n = send(client, reply + offset, chunk, MSG_NOSIGNAL);
            if (n <= 0) break; /* Oversized/malformed replies may close early. */
            offset += (size_t)n;
            if (fragmented) { struct timespec delay = {0, 1000000}; (void)nanosleep(&delay, NULL); }
        }
    }
    close(client); close(server);
    while (outlen < sizeof output && readable(pipes[0])) {
        ssize_t n = read(pipes[0], output + outlen, sizeof output - outlen);
        if (n <= 0) break;
        outlen += (size_t)n;
    }
    close(pipes[0]);
    require(waitpid(child, &status, 0) == child && WIFEXITED(status), "normal fixture termination");
    if (expected) {
        require(WEXITSTATUS(status) == 0, name);
        require(outlen == strlen(expected) && memcmp(output, expected, outlen) == 0, "stdout is exact secret plus newline");
    } else {
        require(WEXITSTATUS(status) != 0, name);
        require(outlen == 0, "failure emits no stdout");
    }
    printf("PASS socket: %s\n", name);
}
static void parser_cases(void)
{
    struct command { const char *argv[12]; const char *expected; };
    static const struct command git[] = {
        {{"git", "clone", "repo"}, "clone"}, {{"git", "-C", "/tmp", "fetch", "origin"}, "fetch"},
        {{"git", "-c", "x=y", "--no-pager", "pull"}, "pull"},
        {{"git", "--git-dir=/tmp", "push"}, "push"}, {{"git", "--work-tree", "/tmp", "ls-remote"}, "ls-remote"},
        {{"git", "-Cfetch"}, NULL}, {{"git", "-c", "fetch"}, NULL}, {{"git", "status", "fetch"}, NULL},
        {{"git", "--unsupported", "push"}, NULL}, {{"git", "--", "fetch"}, NULL}, {{"git", "-C"}, NULL},
        {{"git", "--config-env=x=y", "--bare", "fetch"}, "fetch"}
    };
    static const struct command ssh[] = {
        {{"ssh", "alias", "arbitrary-remote-secret"}, "alias"}, {{"ssh", "-p", "2222", "user@host"}, "user@host"},
        {{"ssh", "-vvT", "-oBatchMode=yes", "git@host"}, "git@host"},
        {{"ssh", "-J", "jump", "-i", "/tmp/key", "alias"}, "alias"},
        {{"ssh", "--", "alias"}, "alias"}, {{"ssh", "-Fconfig", "-luser", "alias"}, "alias"},
        {{"ssh", "-Q", "cipher"}, NULL}, {{"ssh", "-o"}, NULL},
        {{"ssh", "--unsupported", "host"}, NULL}, {{"ssh", "-Z", "host"}, NULL}, {{"ssh", "-"}, NULL}
    };
    for (size_t set = 0; set < 2; ++set) {
        const struct command *cases = set ? ssh : git;
        size_t count = set ? sizeof ssh / sizeof ssh[0] : sizeof git / sizeof git[0];
        for (size_t i = 0; i < count; ++i) {
            size_t argc = 0;
            const char *actual;
            while (cases[i].argv[argc]) ++argc;
            actual = set ? askpass_ssh_destination(argc, cases[i].argv) : askpass_git_operation(argc, cases[i].argv);
            require(cases[i].expected ? actual && strcmp(actual, cases[i].expected) == 0 : !actual, "conservative Git/SSH parsing");
        }
    }
    puts("PASS focused: supported and ambiguous Git/SSH forms");
}
static void boundary_cases(void)
{
    char output[ASKPASS_REQUEST_LIMIT + 1], response[64], clean[32];
    size_t length = 0;
    struct askpass_context context;
    memset(&context, 0, sizeof context);
    askpass_collect_context(INT_MAX, &context);
    require(context.count == 0 && context.cwd[0] == '\0', "missing process collection stays empty");
    require(askpass_encode_request("dummy", &context, output, sizeof output, &length), "missing metadata still encodes authentication");
    require(askpass_decode_reply("{\"ok\":true,\"response\":\"a\"}", 26, response, sizeof response, &length), "missing metadata independent of reply");
    {
        const char input[] = "雪\n\x7f\xc2\x85\xff";
        askpass_clean_metadata(clean, sizeof clean, input, sizeof input - 1);
        require(strcmp(clean, "雪   �") == 0, "metadata controls and malformed UTF8 sanitized");
        askpass_clean_metadata(clean, 3, "雪", 3);
        require(clean[0] == '\0', "metadata bound never splits UTF8");
    }
    context.count = ASKPASS_PROCESS_LIMIT;
    for (size_t i = 0; i < context.count; ++i) {
        context.processes[i].pid = (pid_t)(i + 1);
        memset(context.processes[i].executable, '"', ASKPASS_STRING_LIMIT);
        context.processes[i].executable[ASKPASS_STRING_LIMIT] = '\0';
    }
    require(askpass_encode_request("keep original prompt", &context, output, sizeof output, &length), "request overflow falls back successfully");
    require(strcmp(output, "{\"prompt\":\"keep original prompt\"}\n") == 0, "overflow omits optional context intact");
    memset(output, 'a', sizeof output - 1); output[sizeof output - 1] = '\0';
    require(!askpass_encode_request(output, NULL, response, sizeof response, &length), "oversized required prompt rejected");
    {
        const char *valid = "{\"ok\":true,\"extra\":{\"a\":[null,false,1.2e-3]},\"response\":\"é\\u96ea\\ud83d\\ude00\"}";
        require(askpass_decode_reply(valid, strlen(valid), response, sizeof response, &length) && strcmp(response, "é雪😀") == 0, "nested extension fields and Unicode scalar decoding");
        require(!askpass_decode_reply(valid, strlen(valid), response, 4, &length), "decoded response capacity bound");
        for (size_t prefix = 0; prefix < strlen(valid); ++prefix)
            require(!askpass_decode_reply(valid, prefix, response, sizeof response, &length), "every incomplete reply prefix rejected");
        require(!askpass_decode_reply(valid, strlen(valid), response, 0, &length), "zero-capacity output rejected");
    }
    for (size_t depth = 16; depth <= 17; ++depth) {
        char nested[200];
        const char *prefix = "{\"ok\":true,\"response\":\"dummy\",\"extra\":";
        size_t offset = strlen(prefix);
        memcpy(nested, prefix, offset);
        memset(nested + offset, '[', depth); offset += depth;
        nested[offset++] = '1';
        memset(nested + offset, ']', depth); offset += depth;
        nested[offset++] = '}'; nested[offset] = '\0';
        require(askpass_decode_reply(nested, offset, response, sizeof response, &length) == (depth == 16), "extension nesting bound");
    }
    puts("PASS focused: missing metadata, sanitation, JSON and bounds");
}
static void absent_or_hint(const char *hint)
{
    int pipes[2], status, server = -1;
    char byte;
    pid_t child;
    char path[2048];
    (void)snprintf(path, sizeof path, "%s/omarchy-ssh-askpass.sock", runtime);
    (void)unlink(path);
    if (hint) server = listener();
    require(pipe(pipes) == 0, "failure stdout pipe");
    child = fork(); require(child >= 0, "fork unsupported/missing socket");
    if (!child) {
        if (server >= 0) close(server);
        close(pipes[0]); if (dup2(pipes[1], 1) < 0) _exit(96); close(pipes[1]);
        if (hint) (void)setenv("SSH_ASKPASS_PROMPT", hint, 1);
        execl(binary, "omarchy-ssh-askpass", "dummy", (char *)NULL); _exit(97);
    }
    close(pipes[1]);
    require(readable(pipes[0]) && read(pipes[0], &byte, 1) == 0, "unsupported/missing socket stdout empty");
    close(pipes[0]);
    require(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) != 0, "unsupported/missing socket fails");
    if (server >= 0) {
        struct pollfd p = {server, POLLIN, 0};
        require(poll(&p, 1, 0) == 0, "unsupported hint never reaches dialog socket");
        close(server);
    }
    printf("PASS socket: %s\n", hint ? hint : "missing socket");
}
static void timeout_case(void)
{
    int server = listener(), pipes[2], client, status;
    pid_t child;
    char byte;
    long long start = milliseconds(), elapsed;
    require(pipe(pipes) == 0, "timeout stdout pipe");
    child = fork(); require(child >= 0, "fork deadline test");
    if (!child) {
        close(server); close(pipes[0]); if (dup2(pipes[1], 1) < 0) _exit(98); close(pipes[1]);
        execl(binary, "omarchy-ssh-askpass", "dummy", (char *)NULL); _exit(99);
    }
    close(pipes[1]); require(readable(server), "deadline client connects");
    client = accept(server, NULL, NULL); require(client >= 0, "accept stalled request");
    require(readable(pipes[0]) && read(pipes[0], &byte, 1) == 0, "deadline fails without stdout");
    elapsed = milliseconds() - start;
    close(client); close(server); close(pipes[0]);
    require(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) != 0, "deadline nonzero exit");
    require(elapsed >= 400 && elapsed < 2500, "500ms build deadline bounds");
    printf("PASS socket: stalled connection deadline (%lld ms)\n", elapsed);
}
int main(int argc, char **argv)
{
    static const char *const git[] = {"git", "-C", "/tmp", "fetch", "origin", NULL};
    static const char *const ssh[] = {"ssh", "-p", "2222", "dummy-alias", "arbitrary-remote-secret", NULL};
    static const char *const agent[] = {"ssh-add", "/tmp/dummy-key", NULL};
    static const char *const ambiguous[] = {"ssh", "--unsupported", "host", NULL};
    static const char *const bad[] = {
        "{\"ok\":false}\n", "{\"ok\":false,\"error\":\"busy\"}\n", "broken\n",
        "{\"ok\":true,\"response\":\"a\",\"response\":\"b\"}\n",
        "{\"ok\":true,\"ok\":false,\"response\":\"a\"}\n",
        "{\"ok\":true,\"response\":\"a\",\"respon\\u0073e\":\"b\"}\n",
        "{\"ok\":\"true\",\"response\":\"a\"}\n", "{\"ok\":true}\n",
        "{\"ok\":true,\"response\":null}\n", "{\"ok\":true,\"response\":\"\\n\"}\n",
        "{\"ok\":true,\"response\":\"\\r\"}\n", "{\"ok\":true,\"response\":\"\\u0000\"}\n",
        "{\"ok\":true,\"response\":\"\\ud800\"}\n", "{\"ok\":true,\"response\":\"\\udc00\"}\n",
        "{\"ok\":true,\"response\":\"\\x41\"}\n", "{\"ok\":true,\"response\":\"\xc0\xaf\"}\n",
        "{\"ok\":true,\"response\":\"a\",}\n", "{\"ok\":true,\"response\":\"a\"}x\n",
        "{\"ok\":true,\"response\":\"a\",\"extra\":01}\n",
        "{\"ok\":true,\"response\":\"a\",\"extra\":[true,]}\n"
    };
    if (strcmp(base(argv[0]), "git") == 0 || strcmp(base(argv[0]), "ssh") == 0 || strcmp(base(argv[0]), "ssh-add") == 0) return parent_fixture();
    require(argc == 4, "test arguments");
    binary = argv[1]; runtime = argv[2]; fixture_dir = argv[3];
    (void)setenv("XDG_RUNTIME_DIR", runtime, 1); (void)setenv("ASKPASS_TEST_BINARY", binary, 1);
    (void)unsetenv("SSH_ASKPASS_PROMPT"); (void)signal(SIGPIPE, SIG_IGN);
    parser_cases(); boundary_cases();
    exchange_case("Git context + fragmented Unicode reply", "quoted \"key\" \\ 雪 😀", git,
                  "{\"response\":\"dummy\\u96ea\\ud83d\\ude00\\\"\\\\\\t\\b\\f\\/\",\"ok\":true}\n", "dummy雪😀\"\\\t\b\f/\n", true, "fetch", NULL);
    exchange_case("SSH alias + excluded remote arguments", "dummy prompt", ssh, "{\"ok\":true,\"response\":\"dummy\"}\n", "dummy\n", false, NULL, "dummy-alias");
    exchange_case("ssh-add and empty response", "Enter passphrase for /tmp/dummy-key:", agent, "{\"ok\":true,\"response\":\"\"}\n", "\n", false, NULL, NULL);
    exchange_case("ambiguous SSH and fallback prompt", NULL, ambiguous, "{\"ok\":true,\"response\":\"dummy\"}\n", "dummy\n", false, NULL, NULL);
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        char name[80]; (void)snprintf(name, sizeof name, "rejected reply case %zu", i + 1);
        exchange_case(name, "dummy prompt", agent, bad[i], NULL, false, NULL, NULL);
    }
    exchange_case("early disconnect", "dummy prompt", agent, NULL, NULL, false, NULL, NULL);
    {
        char oversized[ASKPASS_REPLY_LIMIT + 200];
        memset(oversized, 'x', sizeof oversized - 2); oversized[sizeof oversized - 2] = '\n'; oversized[sizeof oversized - 1] = '\0';
        exchange_case("oversized reply", "dummy prompt", agent, oversized, NULL, false, NULL, NULL);
        char edge[ASKPASS_REPLY_LIMIT + 1];
        const char *prefix = "{\"ok\":true,\"response\":\"";
        size_t prefix_length = strlen(prefix), secret_length = ASKPASS_REPLY_LIMIT - prefix_length - 3;
        memcpy(edge, prefix, prefix_length); memset(edge + prefix_length, 'd', secret_length);
        memcpy(edge + prefix_length + secret_length, "\"}\n", 4);
        char expected[ASKPASS_REPLY_LIMIT + 1]; memset(expected, 'd', secret_length); expected[secret_length] = '\n'; expected[secret_length + 1] = '\0';
        exchange_case("reply at 8192-byte transport bound", "dummy prompt", agent, edge, expected, false, NULL, NULL);
    }
    absent_or_hint(NULL); absent_or_hint("confirm"); absent_or_hint("none"); timeout_case();
    printf("PASS all: %u assertions\n", checks);
    return 0;
}
