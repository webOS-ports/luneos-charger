/* SPDX-License-Identifier: Apache-2.0 */
#ifndef LUNEOS_CHARGER_POWER_H
#define LUNEOS_CHARGER_POWER_H

#include <stddef.h>

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

enum charge_speed {
    CHARGE_SPEED_UNKNOWN = -1,  /* no charger online */
    CHARGE_SPEED_NORMAL = 0,
    CHARGE_SPEED_FAST = 1,
};

/* Android's "charging rapidly": more than 7.5 W, in microwatts
 * (config_chargingFastThreshold in frameworks/base SettingsLib). */
#define POWER_FAST_UW_DEFAULT 7500000LL

/* How fast the online charger can charge: fast when it negotiated a fast
 * protocol, or when it offers more than fast_uw by current_max x voltage_max.
 * why (may be NULL) receives the reason. */
enum charge_speed power_charge_speed(long long fast_uw, char *why, size_t why_len);

/* The pieces, for the tests. */
int power_fast_type(const char *type);
int power_usb_type_fast(const char *usb_type);
long long power_max_uw(long long current_max_ua, long long voltage_max_uv);

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
