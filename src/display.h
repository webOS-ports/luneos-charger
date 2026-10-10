/* SPDX-License-Identifier: Apache-2.0 */
#ifndef LUNEOS_CHARGER_DISPLAY_H
#define LUNEOS_CHARGER_DISPLAY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Output to the panel, through DRM/KMS dumb buffers or the fbdev framebuffer -
 * the same two routes Android's charger and recovery take (minui's
 * graphics_drm.cpp and graphics_fbdev.cpp), and for the same reason: the
 * initramfs has no compositor, no hwcomposer and no GPU driver. Downstream
 * MDSS kernels (msm8916, msm8953, msm8994, sdm660) offer fbdev; SDE and
 * MediaTek DRM kernels offer KMS, usually with fbdev emulation as well.
 *
 * The caller draws into a canvas of w x h pixels, 0x00RRGGBB, in the logical
 * orientation; the backend converts to the panel's pixel format and rotation.
 */

struct rect {
    int x, y, w, h;
};

/* A packed pixel format: bytes per pixel, and each channel's bit offset and
 * width within the little-endian pixel. */
struct pixel_format {
    int bytes;
    int r_off, r_len, g_off, g_len, b_off, b_len, a_off, a_len;
};

struct display;

struct display_backend {
    const char *name;
    int (*open)(struct display *d);
    /* Make the canvas' dirty rectangle visible (the whole canvas if the
     * buffer about to be shown is stale). */
    void (*present)(struct display *d, const uint32_t *canvas,
                    const struct rect *dirty);
    void (*set_power)(struct display *d, int on);
    void (*close)(struct display *d);
};

struct display {
    const struct display_backend *be;
    int w, h;               /* logical size, after rotation */
    int phys_w, phys_h;     /* the panel's own size */
    int rotate;             /* 0, 90, 180 or 270, clockwise */
    int stale[2];           /* buffer i needs a full copy */
    void *priv;
};

extern const struct display_backend display_drm;
extern const struct display_backend display_fbdev;

/* Open a display. want: "auto" (DRM, then fbdev), "drm" or "fbdev".
 * Returns 0 on success. */
int display_open(struct display *d, const char *want, int rotate);

void display_present(struct display *d, const uint32_t *canvas,
                     const struct rect *dirty);
void display_set_power(struct display *d, int on);

/* The whole canvas changed: every buffer needs a full copy. */
void display_invalidate(struct display *d);

void display_close(struct display *d);

/* Copy part of the canvas (in logical coordinates) into a buffer of the
 * panel's size, converting the format and applying d->rotate. Shared by the
 * backends; exposed for the tests. */
void display_blit(const struct display *d, const uint32_t *canvas,
                  const struct rect *r, uint8_t *dst, int dst_stride,
                  const struct pixel_format *fmt);

/* Pack one 0x00RRGGBB colour in fmt (alpha, if any, opaque). */
uint32_t display_pack(uint32_t rgb, const struct pixel_format *fmt);

#ifdef __cplusplus
}
#endif

#endif
