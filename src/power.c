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
#include <stdlib.h>
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
    const struct dirent *e;
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
    const struct dirent *e;
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

/*
 * Fast-charger detection. Two independent signals, either is enough:
 *
 *  - The protocol the charger negotiated. Downstream Qualcomm drivers name it
 *    in the usb supply's "type" or "real_type" (USB_HVDCP for Quick Charge 2,
 *    USB_HVDCP_3 for QC3, USB_PD for Power Delivery); mainline drivers list the
 *    possible types in "usb_type" with the active one in brackets
 *    ("SDP CDP [PD_PPS]").
 *  - What the charger offers: current_max x voltage_max, the same product
 *    Android's healthd and SettingsLib turn into "charging rapidly" above
 *    config_chargingFastThreshold (7.5 W), taking 5 V when voltage_max is
 *    missing. Values far outside microamps and microvolts are ignored rather
 *    than trusted: the Galaxy A3 2015's rt5033-charger reports current_max=700
 *    and voltage_max=112.
 *
 * The battery's charge_type ("Fast", "Trickle", "Standard") is NOT used: it
 * is the charging phase (constant current versus trickle), and reads "Fast"
 * on any charger.
 */
static const char *const fast_types[] = {
    "USB_HVDCP", "USB_HVDCP_3", "USB_HVDCP_3P5", "USB_PD", "USB_PD_DRP",
    "PD", "PD_DRP", "PD_PPS", NULL
};

int power_fast_type(const char *type)
{
    for (int i = 0; fast_types[i]; i++)
        if (!strcmp(type, fast_types[i]))
            return 1;
    return 0;
}

int power_usb_type_fast(const char *usb_type)
{
    const char *open = strchr(usb_type, '['), *close;
    char active[32];
    size_t n;

    if (!open || !(close = strchr(open, ']')))
        return 0;
    n = (size_t)(close - open - 1);
    if (n == 0 || n >= sizeof(active))
        return 0;
    memcpy(active, open + 1, n);
    active[n] = 0;
    return power_fast_type(active);
}

long long power_max_uw(long long current_max_ua, long long voltage_max_uv)
{
    /* 100 mA .. 20 A and 3 V .. 48 V, the span of real USB/wireless/DC
     * chargers in micro-units; anything else is a driver in other units. */
    if (current_max_ua < 100000 || current_max_ua > 20000000)
        return -1;
    if (voltage_max_uv <= 0)
        voltage_max_uv = 5000000;
    else if (voltage_max_uv < 3000000 || voltage_max_uv > 48000000)
        return -1;
    return current_max_ua / 1000 * (voltage_max_uv / 1000);
}

static long long read_ll(const char *path, long long def)
{
    char buf[32], *end;
    long long v;

    if (read_str(path, buf, sizeof(buf)) <= 0)
        return def;
    v = strtoll(buf, &end, 10);
    return end == buf || *end ? def : v;
}

enum charge_speed power_charge_speed(long long fast_uw, char *why, size_t why_len)
{
    enum charge_speed speed = CHARGE_SPEED_UNKNOWN;
    char dir[512];
    const struct dirent *e;
    DIR *d;

    if (why && why_len)
        why[0] = 0;
    if (!root_path(dir, sizeof(dir), PSY) || !(d = opendir(dir)))
        return CHARGE_SPEED_UNKNOWN;
    while ((e = readdir(d)) && speed != CHARGE_SPEED_FAST) {
        static const char *const type_attrs[] = { "real_type", "type", NULL };
        char path[512], val[128];
        long long uw;
        if (e->d_name[0] == '.')
            continue;
        if (supply_type(e->d_name, val, sizeof(val)) > 0 && is_battery_type(val))
            continue;
        snprintf(path, sizeof(path), PSY "/%s/online", e->d_name);
        if (read_int(path, 0) <= 0)
            continue;
        speed = CHARGE_SPEED_NORMAL;
        for (int i = 0; type_attrs[i]; i++) {
            snprintf(path, sizeof(path), PSY "/%s/%s", e->d_name, type_attrs[i]);
            if (read_str(path, val, sizeof(val)) > 0 && power_fast_type(val)) {
                if (why && why_len)
                    snprintf(why, why_len, "%s %s=%s", e->d_name, type_attrs[i], val);
                speed = CHARGE_SPEED_FAST;
                break;
            }
        }
        if (speed == CHARGE_SPEED_FAST)
            break;
        snprintf(path, sizeof(path), PSY "/%s/usb_type", e->d_name);
        if (read_str(path, val, sizeof(val)) > 0 && power_usb_type_fast(val)) {
            if (why && why_len)
                snprintf(why, why_len, "%s usb_type=%s", e->d_name, val);
            speed = CHARGE_SPEED_FAST;
            break;
        }
        snprintf(path, sizeof(path), PSY "/%s/current_max", e->d_name);
        {
            long long ua = read_ll(path, -1);
            snprintf(path, sizeof(path), PSY "/%s/voltage_max", e->d_name);
            uw = power_max_uw(ua, read_ll(path, -1));
        }
        if (uw > fast_uw) {
            if (why && why_len)
                snprintf(why, why_len, "%s offers %lld mW", e->d_name, uw / 1000);
            speed = CHARGE_SPEED_FAST;
        }
    }
    closedir(d);
    return speed;
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
