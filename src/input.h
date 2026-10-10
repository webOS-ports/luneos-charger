/* SPDX-License-Identifier: Apache-2.0 */
#ifndef LUNEOS_CHARGER_INPUT_H
#define LUNEOS_CHARGER_INPUT_H

#include <poll.h>

#ifdef __cplusplus
extern "C" {
#endif

#define INPUT_MAX 16

struct input {
    struct pollfd fds[INPUT_MAX];
    int n;
};

/* Open every input device that can report KEY_POWER (on athena the PMIC's
 * qpnp_pon and nav_key; on the Galaxy A3 2015 qpnp_pon). Returns how many. */
int input_open_power_keys(struct input *in);

/* Read what is pending; calls fn(ctx, down) for each KEY_POWER edge. */
void input_dispatch(struct input *in, void (*fn)(void *ctx, int down), void *ctx);

void input_close(struct input *in);

#ifdef __cplusplus
}
#endif

#endif
