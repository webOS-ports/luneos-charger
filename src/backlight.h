/* SPDX-License-Identifier: Apache-2.0 */
#ifndef LUNEOS_CHARGER_BACKLIGHT_H
#define LUNEOS_CHARGER_BACKLIGHT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Find the panel backlight. node: a brightness file to use instead (NULL to
 * discover); level: the "on" value (-1: 5/8 of max_brightness). Returns 0 if
 * there is one. */
int backlight_find(const char *node, int level);

/* The brightness file in use, or "". */
const char *backlight_path(void);

/* The value written for "on". */
int backlight_level(void);

/* Light the panel or put it out. */
void backlight_set(int on);

#ifdef __cplusplus
}
#endif

#endif
