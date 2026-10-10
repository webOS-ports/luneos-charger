// SPDX-License-Identifier: Apache-2.0
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static char root[256];

void util_set_root(const char *r)
{
    snprintf(root, sizeof(root), "%s", r ? r : "");
}

const char *util_root(void)
{
    return root;
}

char *root_path(char *buf, size_t len, const char *path)
{
    int n = snprintf(buf, len, "%s%s", root, path);

    return n < 0 || (size_t)n >= len ? NULL : buf;
}

int read_str(const char *path, char *buf, size_t len)
{
    char full[PATH_MAX];
    ssize_t n;
    int fd;

    if (len == 0)
        return -1;
    buf[0] = 0;
    if (!root_path(full, sizeof(full), path))
        return -1;
    fd = open(full, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    n = read(fd, buf, len - 1);
    close(fd);
    if (n < 0)
        return -1;
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == ' '))
        n--;
    buf[n] = 0;
    return (int)n;
}

int read_int(const char *path, int def)
{
    char buf[32], *end;
    long v;

    if (read_str(path, buf, sizeof(buf)) <= 0)
        return def;
    errno = 0;
    v = strtol(buf, &end, 10);
    if (errno || end == buf || *end || v < INT_MIN || v > INT_MAX)
        return def;
    return (int)v;
}

int write_str(const char *path, const char *val)
{
    char full[PATH_MAX];
    size_t len = strlen(val);
    ssize_t n;
    int fd;

    if (!root_path(full, sizeof(full), path))
        return -1;
    /* O_TRUNC as the shell's ">" does: sysfs ignores it, a plain file needs it. */
    fd = open(full, O_WRONLY | O_TRUNC | O_CLOEXEC);
    if (fd < 0) {
        log_msg("open %s: %s", path, strerror(errno));
        return -1;
    }
    n = write(fd, val, len);
    if (n < 0 || (size_t)n != len)
        log_msg("write %s: %s", path, n < 0 ? strerror(errno) : "short write");
    close(fd);
    return n < 0 || (size_t)n != len ? -1 : 0;
}

int write_int(const char *path, int val)
{
    char buf[16];

    snprintf(buf, sizeof(buf), "%d", val);
    return write_str(path, buf);
}

long long now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void log_msg(const char *fmt, ...)
{
    struct timespec ts;
    va_list ap;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    printf("luneos-charger: [%5lld.%03ld] ", (long long)ts.tv_sec,
           ts.tv_nsec / 1000000);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
    fflush(stdout);
}
