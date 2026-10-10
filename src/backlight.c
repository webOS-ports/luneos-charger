// SPDX-License-Identifier: Apache-2.0
/*
 * The panel backlight. MDSS leaves it at 0 after an unblank (athena), so it
 * has to be set explicitly. The LED-class lcd-backlight is what MDSS and
 * MediaTek's leds_mtk listen to; elsewhere a backlight-class device is the
 * usual one. A backlight-class device with max_brightness 0 is a placeholder
 * (the Galaxy A3 2015's "panel") and is skipped.
 *
 * init.sh passes machine.conf's boot_backlight_node and boot_backlight_level,
 * when a machine sets them, so the charging screen comes up at the same level
 * as the rest of the boot.
 */
#include "backlight.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include "util.h"

static char bl_path[300];
static int bl_level;

static int max_for(const char *brightness_path)
{
    char max[320];
    const char *slash = strrchr(brightness_path, '/');

    if (!slash)
        return -1;
    snprintf(max, sizeof(max), "%.*s/max_brightness",
             (int)(slash - brightness_path), brightness_path);
    return read_int(max, -1);
}

int backlight_find(const char *node, int level)
{
    char dir[512];
    struct dirent *e;
    DIR *d;

    bl_path[0] = 0;
    if (node && node[0]) {
        snprintf(bl_path, sizeof(bl_path), "%s", node);
    } else if (max_for("/sys/class/leds/lcd-backlight/brightness") > 0) {
        snprintf(bl_path, sizeof(bl_path), "/sys/class/leds/lcd-backlight/brightness");
    } else if (root_path(dir, sizeof(dir), "/sys/class/backlight") &&
               (d = opendir(dir))) {
        while ((e = readdir(d)) && !bl_path[0]) {
            char p[300];
            if (e->d_name[0] == '.')
                continue;
            snprintf(p, sizeof(p), "/sys/class/backlight/%s/brightness", e->d_name);
            if (max_for(p) > 0)
                snprintf(bl_path, sizeof(bl_path), "%s", p);
        }
        closedir(d);
    }
    if (!bl_path[0])
        return -1;
    if (level >= 0) {
        bl_level = level;
    } else {
        int max = max_for(bl_path);
        bl_level = max > 0 ? max * 5 / 8 : 160;
        if (bl_level < 1)
            bl_level = 1;
    }
    log_msg("backlight: %s = %d", bl_path, bl_level);
    return 0;
}

const char *backlight_path(void)
{
    return bl_path;
}

int backlight_level(void)
{
    return bl_level;
}

void backlight_set(int on)
{
    if (bl_path[0])
        write_int(bl_path, on ? bl_level : 0);
}
