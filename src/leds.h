/* SPDX-License-Identifier: Apache-2.0 */
#ifndef LUNEOS_CHARGER_LEDS_H
#define LUNEOS_CHARGER_LEDS_H

#ifdef __cplusplus
extern "C" {
#endif

enum led_colour { LED_RED, LED_GREEN, LED_BLUE, LED_N };

enum led_show { LED_SHOW_OFF, LED_SHOW_CHARGING, LED_SHOW_FULL };

/* Use dir (a /sys/class/leds/<name> directory) for colour c instead of
 * discovering one. Call before leds_find(). */
void leds_override(enum led_colour c, const char *dir);

/* Discover the notification LED(s). Returns how many were found. */
int leds_find(void);

/* The directory chosen for a colour ("" if none), and the multicolour LED. */
const char *leds_dir(enum led_colour c);
const char *leds_multi_dir(void);

/* How good a name is as a charge indicator: 0 never, 1..3 better. */
int leds_rank(const char *name);

/* Does the colon-separated LED name have this component? */
int leds_name_has_part(const char *name, const char *part);

/* Red while charging, green when full, everything off. */
void leds_show(enum led_show state);

/* Forget everything (for the tests). */
void leds_reset(void);

#ifdef __cplusplus
}
#endif

#endif
