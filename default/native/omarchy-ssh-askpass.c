#define _POSIX_C_SOURCE 200809L
#include "askpass-context.h"
#include "askpass-json.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

/* A test build can shorten the deadline; installed builds never consult an override. */
#ifndef ASKPASS_TIMEOUT_MS
#define ASKPASS_TIMEOUT_MS 120000
#endif

static int64_t now_ms(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) < 0) return -1;
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static bool ready(int fd, short events, int64_t deadline)
{
    struct pollfd p = { fd, events, 0 };
    for (;;) {
        int n;
        int64_t now = now_ms(), remaining = deadline - now;
        if (now < 0 || remaining <= 0) return false;
        n = poll(&p, 1, remaining > INT_MAX ? INT_MAX : (int)remaining);
        if (n < 0 && errno == EINTR) continue;
        /* Read pending data even when the peer closed immediately after sending it. */
        return n > 0 && (p.revents & events) != 0;
    }
}

static bool exchange(const char *request, size_t request_length, char *reply,
                     size_t *reply_length)
{
    struct sockaddr_un address;
    char fallback[64];
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    int fd, n, error = 0;
    socklen_t error_size = sizeof error;
    size_t used = 0;
    int64_t now = now_ms(), deadline;
    bool valid = false;
    if (now < 0) return false;
    deadline = now + ASKPASS_TIMEOUT_MS;
    if (!runtime) {
        (void)snprintf(fallback, sizeof fallback, "/run/user/%ld", (long)getuid());
        runtime = fallback;
    }
    memset(&address, 0, sizeof address);
    address.sun_family = AF_UNIX;
    n = snprintf(address.sun_path, sizeof address.sun_path, "%s/omarchy-ssh-askpass.sock", runtime);
    if (n < 0 || n >= (int)sizeof address.sun_path) return false;
    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) return false;
    if (connect(fd, (struct sockaddr *)&address, sizeof address) < 0) {
        if (errno != EINPROGRESS || !ready(fd, POLLOUT, deadline) ||
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &error_size) < 0 || error) goto done;
    }
    while (used < request_length) {
        ssize_t sent;
        if (!ready(fd, POLLOUT, deadline)) goto done;
        sent = send(fd, request + used, request_length - used, MSG_NOSIGNAL);
        if (sent < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (sent <= 0) goto done;
        used += (size_t)sent;
    }
    used = 0;
    while (used < ASKPASS_REPLY_LIMIT) {
        ssize_t received;
        char *newline;
        if (!ready(fd, POLLIN, deadline)) goto done;
        received = recv(fd, reply + used, ASKPASS_REPLY_LIMIT - used, 0);
        if (received < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (received <= 0) goto done;
        newline = memchr(reply + used, '\n', (size_t)received);
        used += (size_t)received;
        if (newline) {
            /* Reject another value or trailing non-whitespace in this reply chunk. */
            size_t end = (size_t)(newline - reply), i;
            for (i = end + 1; i < used; ++i)
                if (reply[i] != ' ' && reply[i] != '\t' && reply[i] != '\r' && reply[i] != '\n') goto done;
            *reply_length = end;
            valid = true;
            goto done;
        }
    }
done:
    close(fd);
    return valid;
}

int main(int argc, char **argv)
{
    struct askpass_context context;
    char request[ASKPASS_REQUEST_LIMIT + 1], reply[ASKPASS_REPLY_LIMIT + 1];
    char response[ASKPASS_REPLY_LIMIT + 1];
    const char *hint = getenv("SSH_ASKPASS_PROMPT");
    size_t request_length = 0, reply_length = 0, response_length = 0, used = 0;
    int result = 1;
    if (hint && (strcmp(hint, "confirm") == 0 || strcmp(hint, "none") == 0)) return 1;
    (void)signal(SIGPIPE, SIG_IGN);
    askpass_collect_context(getppid(), &context);
    if (!askpass_encode_request(argc > 1 ? argv[1] : "SSH passphrase", &context,
                                request, sizeof request, &request_length) ||
        !exchange(request, request_length, reply, &reply_length) ||
        !askpass_decode_reply(reply, reply_length, response, sizeof response, &response_length)) goto done;
    response[response_length++] = '\n';
    while (used < response_length) {
        ssize_t n = write(STDOUT_FILENO, response + used, response_length - used);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) goto done;
        used += (size_t)n;
    }
    result = 0;
done:
    askpass_clear(response, sizeof response);
    askpass_clear(reply, sizeof reply);
    return result;
}
