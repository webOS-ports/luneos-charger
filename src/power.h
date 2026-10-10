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

/*
 * Time until LuneOS shows 100 % (gauge 95 %, see power_ui_percent()), in
 * seconds, or -1 if it cannot be told. From, in order:
 *   - the driver's own time_to_full_avg or time_to_full_now;
 *   - charge_full against charge_now (or charge_counter) and the current, or
 *     the same in energy (energy_full, energy_now, power_now);
 *   - nothing else here: see struct eta_trend for the fallback that watches
 *     the percentage rise.
 * The current's sign is not trusted (Qualcomm's is negative while charging);
 * status must say Charging.
 */
long long power_time_to_full(void);

/* The counter arithmetic, for the tests: seconds to reach 95 % of full, or
 * -1. Units cancel, so microamp-hours with microamps or microwatt-hours with
 * microwatts both work. */
long long power_eta_from_counters(long long full, long long now, long long rate);

/*
 * The fallback: watch the gauge climb and extrapolate. Needs the level to
 * have risen at least twice, over at least three minutes, and forgets
 * anything older than half an hour, so a charger swapped for a faster one
 * shows within minutes.
 */
struct eta_trend {
    long long t[16];
    int cap[16];
    int n;
};
void eta_trend_reset(struct eta_trend *e);
void eta_trend_add(struct eta_trend *e, long long now_ms, int capacity);
long long eta_trend_seconds(const struct eta_trend *e, int capacity);

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
