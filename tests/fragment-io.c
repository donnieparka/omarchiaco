/* Test-only linker wrappers force the production transport to complete short
 * reads and writes. They introduce no production option or runtime dependency. */
#include <stddef.h>
#include <sys/socket.h>
#include <sys/types.h>
ssize_t __real_send(int fd, const void *buffer, size_t length, int flags);
ssize_t __real_recv(int fd, void *buffer, size_t length, int flags);
ssize_t __wrap_send(int fd, const void *buffer, size_t length, int flags);
ssize_t __wrap_recv(int fd, void *buffer, size_t length, int flags);
ssize_t __wrap_send(int fd, const void *buffer, size_t length, int flags)
{
    return __real_send(fd, buffer, length > 3 ? 3 : length, flags);
}
ssize_t __wrap_recv(int fd, void *buffer, size_t length, int flags)
{
    return __real_recv(fd, buffer, length > 3 ? 3 : length, flags);
}
