/* SPDX-License-Identifier: Apache-2.0 */
#ifndef LUNEOS_CHARGER_POLICY_H
#define LUNEOS_CHARGER_POLICY_H

#include "power.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * What the charging screen does, as a state machine with no I/O: the main
 * loop feeds it the time, power-key edges and the charger and battery state,
 * and applies what it says. Kept apart so the tests can drive every path.
 */

struct policy_config {
    int screen_timeout_ms;  /* screen on this long after a wake */
    int hold_ms;            /* power key held this long: boot */
    int unplug_ms;          /* charger gone this long: power off */
    int discovery_ms;       /* charger never seen online by then: power off */
    int boot_min_capacity;  /* gauge % needed to boot; -1: no minimum */
    int message_ms;         /* how long "too low to start" stays up */
};

/* Defaults, and where they come from:
 *   10 s screen-on, 2 s hold      as the original luneos-charger; AOSP's
 *                                 charger also boots on a 2 s hold
 *   10 s unplug grace             AOSP charger's UNPLUGGED_SHUTDOWN_TIME
 *   15 s discovery                long enough for a MediaTek charger driver to
 *                                 report online (FuriLabs waits for it)
 */
#define POLICY_CONFIG_DEFAULT { 10000, 2000, 10000, 15000, -1, 3000 }

enum policy_exit {
    POLICY_RUN = 0,         /* keep going */
    POLICY_BOOT,            /* power key held: boot LuneOS */
    POLICY_POWEROFF,        /* charger removed: power off */
};

enum policy_message {
    POLICY_MSG_NONE = 0,
    POLICY_MSG_UNPLUGGED,   /* charger gone, powering off soon */
    POLICY_MSG_TOO_LOW,     /* held to boot, but the battery is too low */
};

struct policy {
    struct policy_config cfg;
    long long start;
    long long wake_until;   /* screen goes off at this time */
    long long press_at;     /* power key down since, -1 if up */
    long long offline_since;/* charger offline since, -1 if not */
    long long message_until;
    int screen_on;
    int press_woke;         /* this press turned the screen on */
    int hold_handled;       /* this press already acted as a hold */
    int seen_online;
};

void policy_init(struct policy *p, const struct policy_config *cfg, long long now);

/* A power-key edge: down != 0 for a press, 0 for a release. */
void policy_key(struct policy *p, long long now, int down);

/* Advance to now with the current charger state and gauge capacity (-1 if
 * unknown). Returns what to do. */
enum policy_exit policy_update(struct policy *p, long long now,
                               enum charger_state charger, int capacity);

/* Should the screen be on right now? */
int policy_screen_on(const struct policy *p);

/* The message to show, if any. */
enum policy_message policy_message(const struct policy *p, long long now);

/* Milliseconds until policy_update() next has something to do by itself
 * (a timeout expiring); a large value if nothing is pending. */
long long policy_next_deadline(const struct policy *p, long long now);

#ifdef __cplusplus
}
#endif

#endif
