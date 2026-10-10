// SPDX-License-Identifier: Apache-2.0
/*
 * luneos-charger - the off-mode charging screen, run from the initramfs.
 *
 * Plugging a charger into a powered-off phone makes the bootloader start the
 * kernel in charger mode (androidboot.mode=charger and its vendor variants,
 * see detect.c). Halium's script would hand such a boot to the Android image's
 * own init for Android's charger UI, which a LuneOS image cannot run.
 * initramfs-scripts-halium's init.sh asks "luneos-charger --detect" instead
 * and, on a charger boot, runs this:
 *
 *   - the LuneOS logo, a battery that fills to the charge level with a band
 *     sweeping up from it, and the percentage LuneOS itself would show;
 *   - the screen turns off after a timeout; a power-key press wakes it, a
 *     short press while it is on turns it off;
 *   - holding the power key exits EXIT_BOOT (init.sh boots LuneOS), unless
 *     the battery is below --min-capacity;
 *   - unplugging the charger exits EXIT_POWEROFF after a grace period;
 *   - the notification LED, if there is one, is red while charging and green
 *     when charged;
 *   - on a fast charger (Quick Charge, USB PD, or more than 7.5 W offered;
 *     see power_charge_speed()) the label says so, a bolt sits in the battery
 *     and the band sweeps twice as fast.
 *
 * Without a usable display it carries on headless (LED, power key, unplug).
 * Without a power key there would be no way out but unplugging, so it exits
 * EXIT_FAIL and init.sh boots normally instead.
 */
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backlight.h"
#include "detect.h"
#include "display.h"
#include "input.h"
#include "leds.h"
#include "policy.h"
#include "power.h"
#include "render.h"
#include "util.h"

#define EXIT_BOOT 10
#define EXIT_POWEROFF 20
#define EXIT_FAIL 1
#define EXIT_USAGE 2

#ifndef ASSET_DIR
#define ASSET_DIR "/usr/share/luneos-charger"
#endif

#define FRAME_MS 33             /* ~30 fps while animating */
#define POLL_MS 500             /* charger and battery state */
#define CYCLE_MS 1600           /* one sweep of the charging band */
#define FAST_CYCLE_MS 800       /* ... on a fast charger */

struct opts {
    struct policy_config policy;
    const char *display;
    const char *backlight;
    const char *assets;
    int brightness;
    int rotate;
    int animate;
    int level;                  /* fake gauge capacity, for testing */
    long long fast_uw;          /* above this a charger is "fast" */
    int no_leds;
};

static void usage(void)
{
    fprintf(stderr,
            "usage: luneos-charger --detect\n"
            "       luneos-charger [options]\n"
            "  --display auto|drm|fbdev|none   output (default auto)\n"
            "  --rotate 0|90|180|270           panel rotation, clockwise\n"
            "  --static                        no animation (E Ink panels)\n"
            "  --timeout ms                    screen-on time (10000)\n"
            "  --hold ms                       power-key hold to boot (2000)\n"
            "  --unplug ms                     unplugged grace before power off (10000)\n"
            "  --discovery ms                  wait for the charger to appear (15000)\n"
            "  --min-capacity n                gauge %% needed to boot (none)\n"
            "  --fast-watts n                  a charger offering more is fast (7.5)\n"
            "  --backlight file --brightness n panel backlight and its level\n"
            "  --led-red dir --led-green dir --led-blue dir --no-leds\n"
            "  --assets dir                    artwork (" ASSET_DIR ")\n"
            "  --level n                       fake gauge capacity, for testing\n"
            "  --root dir                      prefix for /sys and /proc, for testing\n");
    exit(EXIT_USAGE);
}

static int parse_int(const char *s, int lo, int hi)
{
    char *end;
    long v;

    errno = 0;
    v = strtol(s, &end, 10);
    if (errno || end == s || *end || v < lo || v > hi) {
        fprintf(stderr, "luneos-charger: bad number '%s'\n", s);
        usage();
    }
    return (int)v;
}

static void parse_args(int argc, char **argv, struct opts *o, int *detect)
{
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v;
        if (!strcmp(a, "--detect")) { *detect = 1; continue; }
        if (!strcmp(a, "--static")) { o->animate = 0; continue; }
        if (!strcmp(a, "--no-leds")) { o->no_leds = 1; continue; }
        if (!strcmp(a, "--help") || i + 1 >= argc)
            usage();
        v = argv[++i];
        if (!strcmp(a, "--display")) o->display = v;
        else if (!strcmp(a, "--rotate")) o->rotate = parse_int(v, 0, 270);
        else if (!strcmp(a, "--timeout")) o->policy.screen_timeout_ms = parse_int(v, 1000, 3600000);
        else if (!strcmp(a, "--hold")) o->policy.hold_ms = parse_int(v, 100, 60000);
        else if (!strcmp(a, "--unplug")) o->policy.unplug_ms = parse_int(v, 0, 3600000);
        else if (!strcmp(a, "--discovery")) o->policy.discovery_ms = parse_int(v, 0, 3600000);
        else if (!strcmp(a, "--min-capacity")) o->policy.boot_min_capacity = parse_int(v, -1, 100);
        else if (!strcmp(a, "--fast-watts")) o->fast_uw = (long long)parse_int(v, 1, 1000) * 1000000;
        else if (!strcmp(a, "--backlight")) o->backlight = v;
        else if (!strcmp(a, "--brightness")) o->brightness = parse_int(v, 0, INT_MAX);
        else if (!strcmp(a, "--led-red")) leds_override(LED_RED, v);
        else if (!strcmp(a, "--led-green")) leds_override(LED_GREEN, v);
        else if (!strcmp(a, "--led-blue")) leds_override(LED_BLUE, v);
        else if (!strcmp(a, "--assets")) o->assets = v;
        else if (!strcmp(a, "--level")) o->level = parse_int(v, 0, 100);
        else if (!strcmp(a, "--root")) util_set_root(v);
        else usage();
    }
    if (o->rotate % 90 ||
        (strcmp(o->display, "auto") && strcmp(o->display, "drm") &&
         strcmp(o->display, "fbdev") && strcmp(o->display, "none")))
        usage();
}

struct state {
    struct policy policy;
    struct display disp;
    struct renderer rend;
    struct input in;
    int have_display;
    int screen;                 /* what the panel is actually doing */
};

static void on_key(void *ctx, int down)
{
    struct state *st = ctx;

    policy_key(&st->policy, now_ms(), down);
}

static void apply_screen(struct state *st, int on)
{
    if (on == st->screen)
        return;
    st->screen = on;
    if (st->have_display) {
        if (on) {
            display_set_power(&st->disp, 1);
            renderer_invalidate(&st->rend);
            backlight_set(1);
        } else {
            backlight_set(0);
            display_set_power(&st->disp, 0);
        }
    }
    log_msg("screen %s", on ? "on" : "off");
}

static int read_capacity(const struct opts *o)
{
    return o->level >= 0 ? o->level : power_capacity();
}

/* With a faked level, "full" follows it alone: the real gauge's status=Full
 * would otherwise show "Charged" over any level being tried out. */
static int read_full(const struct opts *o, int capacity)
{
    return o->level >= 0 ? power_ui_percent(capacity) >= 100 : power_full(capacity);
}

static const char *charger_name(enum charger_state c)
{
    return c == CHARGER_ONLINE ? "online" :
           c == CHARGER_OFFLINE ? "offline" : "unknown";
}

int main(int argc, char **argv)
{
    struct opts o = {
        .policy = POLICY_CONFIG_DEFAULT,
        .display = "auto",
        .assets = ASSET_DIR,
        .brightness = -1,
        .animate = 1,
        .level = -1,
        .fast_uw = POWER_FAST_UW_DEFAULT,
    };
    static struct state st;
    enum charger_state charger;
    enum charge_speed speed;
    char why[96];
    long long start, polled;
    int detect = 0, capacity, full, ret;

    parse_args(argc, argv, &o, &detect);

    if (detect) {
        int yes = detect_charger_boot(why, sizeof(why));
        printf("%s%s%s\n", yes ? "charger boot" : "normal boot",
               why[0] ? ": " : "", why);
        return yes ? 0 : 1;
    }

    log_msg("off-mode charging screen starting");
    if (!input_open_power_keys(&st.in)) {
        log_msg("no power key: nothing could end this but unplugging - giving up");
        return EXIT_FAIL;
    }
    if (strcmp(o.display, "none") && display_open(&st.disp, o.display, o.rotate) == 0) {
        struct art design;
        int n = art_load(&design, o.assets);
        log_msg("artwork: %d of %d sprites, logo %s", n, SPR_N,
                design.logo.px ? "loaded" : "missing");
        if (renderer_init(&st.rend, st.disp.w, st.disp.h, &design, o.animate) == 0)
            st.have_display = 1;
        else
            display_close(&st.disp);
    }
    if (!st.have_display)
        log_msg("no usable display: carrying on without a screen");
    if (st.have_display)
        backlight_find(o.backlight, o.brightness);
    if (power_find() == 0)
        log_msg("battery: %s", power_battery_dir());
    else
        log_msg("battery: none found");
    if (!o.no_leds)
        leds_find();

    start = polled = now_ms();
    policy_init(&st.policy, &o.policy, start);
    st.screen = -1;
    charger = power_charger();
    speed = power_charge_speed(o.fast_uw, why, sizeof(why));
    if (speed == CHARGE_SPEED_FAST)
        log_msg("fast charger: %s", why);
    capacity = read_capacity(&o);
    full = read_full(&o, capacity);
    log_msg("battery %d%% (gauge %d%%) %s, charger %s",
            power_ui_percent(capacity), capacity, full ? "full" : "charging",
            charger_name(charger));

    for (;;) {
        long long t = now_ms(), wait;
        enum policy_exit ex;

        if (t - polled >= POLL_MS) {
            enum charger_state c = power_charger();
            int cap = read_capacity(&o), f = read_full(&o, cap);
            polled = t;
            enum charge_speed sp = power_charge_speed(o.fast_uw, why, sizeof(why));
            if (c != charger)
                log_msg("charger %s", charger_name(c));
            if (sp != speed && sp == CHARGE_SPEED_FAST)
                log_msg("fast charger: %s", why);
            else if (sp != speed && speed == CHARGE_SPEED_FAST)
                log_msg("no longer a fast charger");
            speed = sp;
            if (cap != capacity || f != full)
                log_msg("battery %d%% (gauge %d%%) %s", power_ui_percent(cap),
                        cap, f ? "full" : "charging");
            charger = c;
            capacity = cap;
            full = f;
        }

        ex = policy_update(&st.policy, t, charger, capacity);
        if (ex != POLICY_RUN) {
            log_msg(ex == POLICY_BOOT ? "power key held: booting LuneOS"
                                      : "charger removed: powering off");
            ret = ex == POLICY_BOOT ? EXIT_BOOT : EXIT_POWEROFF;
            break;
        }

        apply_screen(&st, policy_screen_on(&st.policy));
        if (!o.no_leds)
            leds_show(charger == CHARGER_OFFLINE ? LED_SHOW_OFF :
                      full ? LED_SHOW_FULL : LED_SHOW_CHARGING);

        wait = policy_next_deadline(&st.policy, t);
        if (wait > POLL_MS)
            wait = POLL_MS;
        if (st.have_display && st.screen) {
            int fast = speed == CHARGE_SPEED_FAST && charger != CHARGER_OFFLINE;
            int cycle = fast ? FAST_CYCLE_MS : CYCLE_MS;
            struct scene sc = {
                power_ui_percent(capacity), full, fast,
                policy_message(&st.policy, t),
            };
            int phase = (int)((t - start) % cycle) * 1000 / cycle;
            int drawn = renderer_frame(&st.rend, &sc, phase);
            if (drawn >= 0) {
                if (drawn)
                    display_invalidate(&st.disp);
                display_present(&st.disp, st.rend.canvas, &st.rend.dirty);
            }
            if (st.rend.animate && !full) {
                long long spent = now_ms() - t;
                wait = spent >= FRAME_MS ? 1 : FRAME_MS - spent;
            }
        }

        if (poll(st.in.fds, (nfds_t)st.in.n, (int)wait) > 0)
            input_dispatch(&st.in, on_key, &st);
    }

    /* Leave the panel dark and the LED off for whatever init.sh does next. */
    apply_screen(&st, 0);
    if (!o.no_leds)
        leds_show(LED_SHOW_OFF);
    if (st.have_display) {
        renderer_free(&st.rend);
        display_close(&st.disp);
    }
    input_close(&st.in);
    return ret;
}
