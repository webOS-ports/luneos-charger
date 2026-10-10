/* SPDX-License-Identifier: Apache-2.0 */
#ifndef LUNEOS_CHARGER_UTIL_H
#define LUNEOS_CHARGER_UTIL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Every sysfs and procfs path goes through root_path(), so the tests can point
 * the whole program at a fake tree (util_set_root("/path/to/fake")). On a
 * device the root is "" and paths are used as they are.
 */
void util_set_root(const char *root);
const char *util_root(void);

/* "<root><path>" into buf; returns buf, or NULL if it did not fit. */
char *root_path(char *buf, size_t len, const char *path);

/* Read a sysfs-style file: trailing newlines and spaces are stripped.
 * Returns the length read, or -1. */
int read_str(const char *path, char *buf, size_t len);

/* read_str() and parse a decimal integer; def if missing or not a number. */
int read_int(const char *path, int def);

/* Write a string or integer to a sysfs file; -1 on failure, logged. */
int write_str(const char *path, const char *val);
int write_int(const char *path, int val);

/* Monotonic milliseconds. */
long long now_ms(void);

/* "[   12.345] message" on stdout, which init.sh sends to /dev/kmsg. */
void log_msg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#ifdef __cplusplus
}
#endif

#endif
