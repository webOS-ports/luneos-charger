// SPDX-License-Identifier: Apache-2.0
/*
 * The notification LED: red while charging, green when charged.
 *
 * Found by name in /sys/class/leds, covering the conventions in use:
 *   - bare colour names: "red", "green", "blue" (athena, many Qualcomm phones),
 *   - "colour:function" and "device:colour:function"
 *     (Documentation/leds/leds-class.rst): "red:status", "green:charging",
 *   - multicolour class devices ("rgb:status"), which carry multi_index and
 *     multi_intensity and are driven as one LED.
 * Names containing "charg" win over "status"/"indicator", which win over the
 * rest. Flash, torch, keyboard, button and backlight LEDs are never touched.
 *
 * The kernel's battery-* triggers are not used: they follow status=Full, which
 * some gauges never report (see power_full()).
 */
#include "leds.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include "util.h"

#define LEDS "/sys/class/leds"

static const char *const colour_name[LED_N] = { "red", "green", "blue" };
static char led_dir[LED_N][300];
static int led_score[LED_N];
static char led_multi[300];
static int shown = -1;

void leds_reset(void)
{
    memset(led_dir, 0, sizeof(led_dir));
    memset(led_score, 0, sizeof(led_score));
    led_multi[0] = 0;
    shown = -1;
}

void leds_override(enum led_colour c, const char *dir)
{
    snprintf(led_dir[c], sizeof(led_dir[c]), "%s", dir);
    led_score[c] = 100;
}

const char *leds_dir(enum led_colour c)
{
    return led_dir[c];
}

const char *leds_multi_dir(void)
{
    return led_multi;
}

int leds_name_has_part(const char *name, const char *part)
{
    size_t n = strlen(part);
    const char *p = name;

    for (;;) {
        if (!strncmp(p, part, n) && (p[n] == 0 || p[n] == ':'))
            return 1;
        if (!(p = strchr(p, ':')))
            return 0;
        p++;
    }
}

int leds_rank(const char *name)
{
    static const char *const never[] = {
        "flash", "torch", "kbd", "keyboard", "button", "backlight", "lcd",
        "mmc", "wlan", "wifi", "bt", "camera", NULL
    };

    for (int i = 0; never[i]; i++)
        if (strstr(name, never[i]))
            return 0;
    if (strstr(name, "charg"))
        return 3;
    if (strstr(name, "status") || strstr(name, "indicator"))
        return 2;
    return 1;
}

int leds_find(void)
{
    char dir[512];
    const struct dirent *e;
    DIR *d;
    int found = 0;

    if (!root_path(dir, sizeof(dir), LEDS) || !(d = opendir(dir)))
        return 0;
    while ((e = readdir(d))) {
        char path[512], idx[96];
        int rank;
        if (e->d_name[0] == '.' || !(rank = leds_rank(e->d_name)))
            continue;
        snprintf(path, sizeof(path), LEDS "/%s/multi_index", e->d_name);
        if (read_str(path, idx, sizeof(idx)) > 0) {
            if (!led_multi[0])
                snprintf(led_multi, sizeof(led_multi), LEDS "/%s", e->d_name);
            continue;
        }
        for (int c = 0; c < LED_N; c++)
            if (leds_name_has_part(e->d_name, colour_name[c]) &&
                rank > led_score[c]) {
                snprintf(led_dir[c], sizeof(led_dir[c]), LEDS "/%s", e->d_name);
                led_score[c] = rank;
            }
    }
    closedir(d);
    for (int c = 0; c < LED_N; c++)
        if (led_dir[c][0]) {
            log_msg("led %s: %s", colour_name[c], led_dir[c]);
            found++;
        }
    if (led_multi[0]) {
        log_msg("led multicolour: %s", led_multi);
        found++;
    }
    return found;
}

static void led_attr(const char *dir, const char *attr, const char *val)
{
    char path[512];

    snprintf(path, sizeof(path), "%s/%s", dir, attr);
    write_str(path, val);
}

static void led_set(const char *dir, int on)
{
    char path[512], v[16];

    snprintf(path, sizeof(path), "%s/max_brightness", dir);
    snprintf(v, sizeof(v), "%d", on ? read_int(path, 255) : 0);
    led_attr(dir, "trigger", "none");
    led_attr(dir, "brightness", v);
}

static void multi_show(enum led_show state)
{
    /* multi_intensity takes one value per channel, in multi_index order. */
    char idx[96], out[96] = "", path[512], *save = NULL, *tok;
    size_t used = 0;

    snprintf(path, sizeof(path), "%s/multi_index", led_multi);
    if (read_str(path, idx, sizeof(idx)) <= 0)
        return;
    for (tok = strtok_r(idx, " ", &save); tok; tok = strtok_r(NULL, " ", &save)) {
        int v = (state == LED_SHOW_CHARGING && !strcmp(tok, "red")) ||
                (state == LED_SHOW_FULL && !strcmp(tok, "green")) ? 255 : 0;
        int n = snprintf(out + used, sizeof(out) - used, "%s%d",
                         used ? " " : "", v);
        if (n < 0 || (size_t)n >= sizeof(out) - used)
            return;
        used += (size_t)n;
    }
    led_attr(led_multi, "trigger", "none");
    led_attr(led_multi, "multi_intensity", out);
    led_set(led_multi, state != LED_SHOW_OFF);
}

void leds_show(enum led_show state)
{
    const char *r = led_dir[LED_RED], *g = led_dir[LED_GREEN],
               *b = led_dir[LED_BLUE];

    if ((int)state == shown)
        return;
    shown = (int)state;
    if (r[0] || g[0]) {
        if (b[0])
            led_set(b, 0);
        /* Off first, so the two never show together as amber. */
        if (r[0] && state != LED_SHOW_CHARGING)
            led_set(r, 0);
        if (g[0] && state != LED_SHOW_FULL)
            led_set(g, 0);
        if (r[0] && state == LED_SHOW_CHARGING)
            led_set(r, 1);
        if (g[0] && state == LED_SHOW_FULL)
            led_set(g, 1);
    } else if (led_multi[0]) {
        multi_show(state);
    } else {
        return;
    }
    log_msg("led %s", state == LED_SHOW_FULL ? "green" :
                      state == LED_SHOW_CHARGING ? "red" : "off");
}
