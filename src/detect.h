/* SPDX-License-Identifier: Apache-2.0 */
#ifndef LUNEOS_CHARGER_DETECT_H
#define LUNEOS_CHARGER_DETECT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Did the bootloader start us because a charger was plugged into a powered-off
 * device? Looks at /proc/cmdline, /proc/bootconfig and the MediaTek boot_mode
 * node; see detect.c for the list and where each entry comes from.
 *
 * Returns 1 for a charger boot, 0 otherwise. why (may be NULL) receives the
 * entry that matched, or why it was overridden.
 */
int detect_charger_boot(char *why, size_t why_len);

/* The pieces, exposed for the tests. Each returns 1 on a match and copies the
 * matching entry to why. */
int detect_in_cmdline(const char *cmdline, char *why, size_t why_len);
int detect_in_bootconfig(const char *bootconfig, char *why, size_t why_len);
int detect_override_in_cmdline(const char *cmdline);

#ifdef __cplusplus
}
#endif

#endif
