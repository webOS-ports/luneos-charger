/* SPDX-License-Identifier: Apache-2.0 */
#ifndef LUNEOS_CHARGER_POWER_H
#define LUNEOS_CHARGER_POWER_H

#ifdef __cplusplus
extern "C" {
#endif

enum charger_state {
    CHARGER_UNKNOWN = -1,   /* no supply has an "online" attribute */
    CHARGER_OFFLINE = 0,
    CHARGER_ONLINE = 1,
};

/* Find the battery in /sys/class/power_supply. Returns 0 if found. */
int power_find(void);

/* The battery's directory, or "" if there is none. */
const char *power_battery_dir(void);

/* Is any external supply online? */
enum charger_state power_charger(void);

/* The gauge's capacity in percent, or -1. */
int power_capacity(void);

/* The percentage LuneOS shows for a gauge reading (-1 stays -1). */
int power_ui_percent(int capacity);

/* Charged, by any signal the device gives: LuneOS showing 100 %, status=Full
 * or charge_done. */
int power_full(int capacity);

#ifdef __cplusplus
}
#endif

#endif
