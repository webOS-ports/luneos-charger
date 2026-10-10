// SPDX-License-Identifier: Apache-2.0
/*
 * Charger-boot detection.
 *
 * Bootloaders say "powered on by the charger" in more than one way, and the
 * other Halium-based distributions each check a different subset:
 *
 *   androidboot.mode=charger    AOSP, every Qualcomm and most MediaTek
 *                               bootloaders; Halium's identify_boot_mode().
 *                               On boot header v4 it can be in /proc/bootconfig
 *                               instead, as androidboot.mode = "charger" (the
 *                               form Sailfish OS's get_bootstate checks).
 *   androidboot.bootreason=usb  Ubuntu Touch's charger generator and FuriLabs'
 *                               initramfs (MediaTek KPOC), cmdline or bootconfig.
 *   lpm_boot=1, lpcharge=1,     postmarketOS's former start_charging_mode():
 *   androidboot.bootchg=true,   Samsung, LG and Huawei bootloaders that never
 *   startup=0x00010004,         say androidboot.mode.
 *   androidboot.huawei_type=oem_rtc
 *   boot_mode 8 or 9            /sys/class/BOOT/BOOT/boot/boot_mode on older
 *                               MediaTek kernels (power-off and low-power
 *                               charging), as Halium and Droidian read it.
 *
 * "no_charger_mode" on the cmdline overrides all of them, for a device whose
 * bootloader claims a charger boot when it is not one.
 */
#include "detect.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "util.h"

struct match {
    const char *key, *value;
};

static const struct match cmdline_matches[] = {
    { "androidboot.mode", "charger" },
    { "androidboot.bootreason", "usb" },
    { "lpm_boot", "1" },
    { "lpcharge", "1" },
    { "androidboot.bootchg", "true" },
    { "startup", "0x00010004" },
    { "androidboot.huawei_type", "oem_rtc" },
};

static const struct match bootconfig_matches[] = {
    { "androidboot.mode", "charger" },
    { "androidboot.bootreason", "usb" },
};

#define N_ELEMENTS(a) (sizeof(a) / sizeof((a)[0]))

static void set_why(char *why, size_t len, const char *key, const char *value)
{
    if (why && len)
        snprintf(why, len, "%s=%s", key, value);
}

/* Compare key=value where value may be quoted; [v, v+vlen) is the raw value. */
static int value_is(const char *v, size_t vlen, const char *want)
{
    if (vlen >= 2 && v[0] == '"' && v[vlen - 1] == '"') {
        v++;
        vlen -= 2;
    }
    return vlen == strlen(want) && !memcmp(v, want, vlen);
}

/* Walk the cmdline's space-separated parameters, honouring double quotes the
 * way the kernel's next_arg() does. Calls fn for each; stops when it returns
 * non-zero and returns that. */
static int each_param(const char *s,
                      int (*fn)(const char *p, size_t plen, void *ctx),
                      void *ctx)
{
    while (*s) {
        const char *start;
        int quoted = 0, r;

        while (*s && isspace((unsigned char)*s))
            s++;
        if (!*s)
            break;
        start = s;
        while (*s && (quoted || !isspace((unsigned char)*s))) {
            if (*s == '"')
                quoted = !quoted;
            s++;
        }
        r = fn(start, (size_t)(s - start), ctx);
        if (r)
            return r;
    }
    return 0;
}

struct cmdline_ctx {
    char *why;
    size_t why_len;
};

static int cmdline_param(const char *p, size_t plen, void *vctx)
{
    struct cmdline_ctx *ctx = vctx;
    const char *eq = memchr(p, '=', plen);
    size_t klen;

    if (!eq)
        return 0;
    klen = (size_t)(eq - p);
    for (size_t i = 0; i < N_ELEMENTS(cmdline_matches); i++) {
        const struct match *m = &cmdline_matches[i];
        if (klen == strlen(m->key) && !memcmp(p, m->key, klen) &&
            value_is(eq + 1, plen - klen - 1, m->value)) {
            set_why(ctx->why, ctx->why_len, m->key, m->value);
            return 1;
        }
    }
    return 0;
}

int detect_in_cmdline(const char *cmdline, char *why, size_t why_len)
{
    struct cmdline_ctx ctx = { why, why_len };

    return each_param(cmdline, cmdline_param, &ctx);
}

static int override_param(const char *p, size_t plen, void *ctx)
{
    (void)ctx;
    return plen == strlen("no_charger_mode") && !memcmp(p, "no_charger_mode", plen);
}

int detect_override_in_cmdline(const char *cmdline)
{
    return each_param(cmdline, override_param, NULL);
}

static const char *trim(char *s)
{
    char *e;

    while (isspace((unsigned char)*s))
        s++;
    e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1]))
        *--e = 0;
    return s;
}

/* /proc/bootconfig: one 'key = "value"' per line (fs/proc/bootconfig.c), a
 * multi-value key as 'key = "a", "b"'. Only single values matter here. */
int detect_in_bootconfig(const char *bootconfig, char *why, size_t why_len)
{
    const char *line = bootconfig;

    while (line && *line) {
        const char *nl = strchr(line, '\n');
        size_t len = nl ? (size_t)(nl - line) : strlen(line);
        char buf[512], *eq;

        if (len < sizeof(buf)) {
            memcpy(buf, line, len);
            buf[len] = 0;
            eq = strchr(buf, '=');
            if (eq) {
                const char *key, *value;
                *eq = 0;
                key = trim(buf);
                value = trim(eq + 1);
                for (size_t i = 0; i < N_ELEMENTS(bootconfig_matches); i++) {
                    const struct match *m = &bootconfig_matches[i];
                    if (!strcmp(key, m->key) &&
                        value_is(value, strlen(value), m->value)) {
                        set_why(why, why_len, m->key, m->value);
                        return 1;
                    }
                }
            }
        }
        line = nl ? nl + 1 : NULL;
    }
    return 0;
}

int detect_charger_boot(char *why, size_t why_len)
{
    /* /proc/bootconfig can exceed a page; 16 KiB covers what bootloaders
     * write. */
    static char cmdline[4096], bootconfig[16384];
    int mode;

    if (why && why_len)
        why[0] = 0;
    if (read_str("/proc/cmdline", cmdline, sizeof(cmdline)) < 0)
        cmdline[0] = 0;
    if (detect_override_in_cmdline(cmdline)) {
        if (why && why_len)
            snprintf(why, why_len, "no_charger_mode on the cmdline");
        return 0;
    }
    if (detect_in_cmdline(cmdline, why, why_len))
        return 1;
    if (read_str("/proc/bootconfig", bootconfig, sizeof(bootconfig)) > 0 &&
        detect_in_bootconfig(bootconfig, why, why_len))
        return 1;
    mode = read_int("/sys/class/BOOT/BOOT/boot/boot_mode", -1);
    if (mode == 8 || mode == 9) {
        if (why && why_len)
            snprintf(why, why_len, "boot_mode=%d", mode);
        return 1;
    }
    return 0;
}
