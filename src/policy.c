// SPDX-License-Identifier: Apache-2.0
#include "policy.h"

#include <string.h>

#define NEVER (1LL << 62)

static void wake(struct policy *p, long long now)
{
    p->screen_on = 1;
    if (p->wake_until < now + p->cfg.screen_timeout_ms)
        p->wake_until = now + p->cfg.screen_timeout_ms;
}

void policy_init(struct policy *p, const struct policy_config *cfg, long long now)
{
    memset(p, 0, sizeof(*p));
    p->cfg = *cfg;
    p->start = now;
    p->press_at = -1;
    p->offline_since = -1;
    p->message_until = -1;
    p->wake_until = now;
    wake(p, now);
}

void policy_key(struct policy *p, long long now, int down)
{
    if (down) {
        if (p->press_at >= 0)
            return;                 /* autorepeat or a second device */
        p->press_at = now;
        p->hold_handled = 0;
        p->press_woke = !p->screen_on;
        wake(p, now);
        return;
    }
    if (p->press_at < 0)
        return;
    /* A short press that did not just wake the screen puts it out. */
    (void)now;
    if (!p->hold_handled && !p->press_woke && p->screen_on)
        p->screen_on = 0;
    p->press_at = -1;
}

enum policy_exit policy_update(struct policy *p, long long now,
                               enum charger_state charger, int capacity)
{
    switch (charger) {
    case CHARGER_ONLINE:
        if (p->offline_since >= 0)
            wake(p, now);           /* plugged back in: show it */
        p->seen_online = 1;
        p->offline_since = -1;
        break;
    case CHARGER_OFFLINE:
        if (p->offline_since < 0) {
            p->offline_since = now;
            if (p->seen_online)
                wake(p, now);       /* unplugged: say we are powering off */
        }
        if (p->seen_online) {
            if (now - p->offline_since >= p->cfg.unplug_ms)
                return POLICY_POWEROFF;
        } else if (now - p->start >= p->cfg.discovery_ms) {
            /* Never saw the charger at all: whatever started us, there is
             * nothing to charge from. Do not sit here forever. */
            return POLICY_POWEROFF;
        }
        break;
    case CHARGER_UNKNOWN:
        /* No way to tell: never power off a charging phone on a guess. */
        p->offline_since = -1;
        break;
    }

    if (p->press_at >= 0 && !p->hold_handled &&
        now - p->press_at >= p->cfg.hold_ms) {
        p->hold_handled = 1;
        if (p->cfg.boot_min_capacity >= 0 && capacity >= 0 &&
            capacity < p->cfg.boot_min_capacity) {
            p->message_until = now + p->cfg.message_ms;
            wake(p, now);
        } else {
            return POLICY_BOOT;
        }
    }

    if (p->screen_on && p->press_at < 0 && now >= p->wake_until)
        p->screen_on = 0;
    return POLICY_RUN;
}

int policy_screen_on(const struct policy *p)
{
    return p->screen_on;
}

enum policy_message policy_message(const struct policy *p, long long now)
{
    if (p->offline_since >= 0 && p->seen_online)
        return POLICY_MSG_UNPLUGGED;
    if (now < p->message_until)
        return POLICY_MSG_TOO_LOW;
    return POLICY_MSG_NONE;
}

static long long earliest(long long a, long long b)
{
    return a < b ? a : b;
}

long long policy_next_deadline(const struct policy *p, long long now)
{
    long long t = NEVER;

    if (p->screen_on && p->press_at < 0)
        t = earliest(t, p->wake_until);
    if (p->press_at >= 0 && !p->hold_handled)
        t = earliest(t, p->press_at + p->cfg.hold_ms);
    if (p->offline_since >= 0)
        t = earliest(t, p->seen_online ? p->offline_since + p->cfg.unplug_ms
                                       : p->start + p->cfg.discovery_ms);
    if (p->message_until > now)
        t = earliest(t, p->message_until);
    if (t == NEVER)
        return NEVER;
    return t > now ? t - now : 0;
}
