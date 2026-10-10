// SPDX-License-Identifier: Apache-2.0
#include "input.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "util.h"

#define BITS_PER_LONG (8 * sizeof(long))

static int has_key(int fd, int code)
{
    unsigned long bits[KEY_MAX / BITS_PER_LONG + 1];

    memset(bits, 0, sizeof(bits));
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits) < 0)
        return 0;
    return !!(bits[code / BITS_PER_LONG] & (1UL << (code % BITS_PER_LONG)));
}

int input_open_power_keys(struct input *in)
{
    struct dirent *e;
    DIR *d;

    memset(in, 0, sizeof(*in));
    d = opendir("/dev/input");
    if (!d) {
        log_msg("input: /dev/input: %s", strerror(errno));
        return 0;
    }
    while ((e = readdir(d)) && in->n < INPUT_MAX) {
        char path[300], name[64] = "?";
        int fd;
        if (strncmp(e->d_name, "event", 5))
            continue;
        snprintf(path, sizeof(path), "/dev/input/%s", e->d_name);
        fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            continue;
        if (!has_key(fd, KEY_POWER)) {
            close(fd);
            continue;
        }
        if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) < 0)
            snprintf(name, sizeof(name), "?");
        log_msg("power key on %s (%s)", path, name);
        in->fds[in->n].fd = fd;
        in->fds[in->n].events = POLLIN;
        in->n++;
    }
    closedir(d);
    return in->n;
}

void input_dispatch(struct input *in, void (*fn)(void *ctx, int down), void *ctx)
{
    for (int i = 0; i < in->n; i++) {
        struct input_event ev;
        if (!(in->fds[i].revents & POLLIN))
            continue;
        while (read(in->fds[i].fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
            /* value 2 is autorepeat: neither a press nor a release. */
            if (ev.type == EV_KEY && ev.code == KEY_POWER && ev.value != 2)
                fn(ctx, ev.value != 0);
        }
    }
}

void input_close(struct input *in)
{
    for (int i = 0; i < in->n; i++)
        close(in->fds[i].fd);
    in->n = 0;
}
