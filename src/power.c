// SPDX-License-Identifier: Apache-2.0
/*
 * Battery and charger state from /sys/class/power_supply.
 *
 * Supplies are told apart by their "type" attribute, as init.sh's
 * battery_capacity() does, because the names differ per vendor: "battery",
 * "usb", "dc" on Qualcomm; "mtk-master-charger", "ac" on MediaTek;
 * "rt5033-charger" next to "usb" on the Galaxy A3 2015. The Battery supply's
 * own "online" is not a charger flag - Samsung's sec battery driver puts the
 * cable type there (4 on the A3) - so Battery and BMS supplies are skipped.
 */
#include "power.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include "util.h"

#define PSY "/sys/class/power_supply"

static char bat_dir[300];

static int supply_type(const char *name, char *type, size_t len)
{
    char path[512];

    snprintf(path, sizeof(path), PSY "/%s/type", name);
    return read_str(path, type, len);
}

static int is_battery_type(const char *type)
{
    return !strcmp(type, "Battery") || !strcmp(type, "BMS");
}

int power_find(void)
{
    char dir[512];
    struct dirent *e;
    DIR *d;

    bat_dir[0] = 0;
    if (!root_path(dir, sizeof(dir), PSY) || !(d = opendir(dir)))
        return -1;
    while ((e = readdir(d)) && !bat_dir[0]) {
        char type[32];
        if (e->d_name[0] == '.')
            continue;
        if (supply_type(e->d_name, type, sizeof(type)) > 0 &&
            !strcmp(type, "Battery"))
            snprintf(bat_dir, sizeof(bat_dir), PSY "/%s", e->d_name);
    }
    closedir(d);
    return bat_dir[0] ? 0 : -1;
}

const char *power_battery_dir(void)
{
    return bat_dir;
}

enum charger_state power_charger(void)
{
    enum charger_state state = CHARGER_UNKNOWN;
    char dir[512];
    struct dirent *e;
    DIR *d;

    if (!root_path(dir, sizeof(dir), PSY) || !(d = opendir(dir)))
        return CHARGER_UNKNOWN;
    while ((e = readdir(d)) && state != CHARGER_ONLINE) {
        char path[512], type[32];
        int online;
        if (e->d_name[0] == '.')
            continue;
        if (supply_type(e->d_name, type, sizeof(type)) > 0 &&
            is_battery_type(type))
            continue;
        snprintf(path, sizeof(path), PSY "/%s/online", e->d_name);
        online = read_int(path, -1);
        if (online < 0)
            continue;
        state = online > 0 ? CHARGER_ONLINE : CHARGER_OFFLINE;
    }
    closedir(d);
    return state;
}

int power_capacity(void)
{
    char path[512];
    int c;

    if (!bat_dir[0])
        return -1;
    snprintf(path, sizeof(path), "%s/capacity", bat_dir);
    c = read_int(path, -1);
    return c < 0 ? -1 : (c > 100 ? 100 : c);
}

/* batteryd's getUiPercent() (com.webos.service.battery,
 * batteryd/charging/battery.c) stretches 0..95 % of the gauge over 0..100 %,
 * and the status bar shows that. Showing the raw capacity here made this
 * screen read 89 % where the status bar read 93 % right after boot. */
int power_ui_percent(int capacity)
{
    if (capacity < 0)
        return -1;
    return (capacity > 95 ? 95 : capacity) * 100 / 95;
}

/* Not every gauge reports Full: athena's stays "Charging" with charge_done=0
 * at 100 %, which is also why the kernel's battery-full LED trigger is no
 * use there. */
int power_full(int capacity)
{
    char path[512], st[32];

    if (power_ui_percent(capacity) >= 100)
        return 1;
    if (!bat_dir[0])
        return 0;
    snprintf(path, sizeof(path), "%s/status", bat_dir);
    if (read_str(path, st, sizeof(st)) > 0 && !strcmp(st, "Full"))
        return 1;
    snprintf(path, sizeof(path), "%s/charge_done", bat_dir);
    return read_int(path, 0) > 0;
}
