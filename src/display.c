// SPDX-License-Identifier: Apache-2.0
#include "display.h"

#include <string.h>

#include "util.h"

int display_open(struct display *d, const char *want, int rotate)
{
    static const struct display_backend *const all[] = {
        &display_drm, &display_fbdev, NULL
    };

    if (rotate != 0 && rotate != 90 && rotate != 180 && rotate != 270)
        return -1;
    for (int i = 0; all[i]; i++) {
        if (strcmp(want, "auto") && strcmp(want, all[i]->name))
            continue;
        memset(d, 0, sizeof(*d));
        d->be = all[i];
        d->rotate = rotate;
        if (d->be->open(d) == 0) {
            if (rotate == 90 || rotate == 270) {
                d->w = d->phys_h;
                d->h = d->phys_w;
            } else {
                d->w = d->phys_w;
                d->h = d->phys_h;
            }
            display_invalidate(d);
            log_msg("display: %s, %dx%d, rotated %d", d->be->name,
                    d->phys_w, d->phys_h, rotate);
            return 0;
        }
    }
    memset(d, 0, sizeof(*d));
    return -1;
}

void display_present(struct display *d, const uint32_t *canvas,
                     const struct rect *dirty)
{
    d->be->present(d, canvas, dirty);
}

void display_set_power(struct display *d, int on)
{
    d->be->set_power(d, on);
    if (on)
        display_invalidate(d);
}

void display_invalidate(struct display *d)
{
    d->stale[0] = d->stale[1] = 1;
}

void display_close(struct display *d)
{
    if (d->be)
        d->be->close(d);
    memset(d, 0, sizeof(*d));
}

uint32_t display_pack(uint32_t rgb, const struct pixel_format *fmt)
{
    const struct pixel_format *f = fmt;
    uint32_t r = rgb >> 16 & 0xff, g = rgb >> 8 & 0xff, b = rgb & 0xff;
    uint32_t v = (r >> (8 - f->r_len)) << f->r_off |
                 (g >> (8 - f->g_len)) << f->g_off |
                 (b >> (8 - f->b_len)) << f->b_off;

    if (f->a_len)
        v |= ((1u << f->a_len) - 1) << f->a_off;
    return v;
}

static inline void put(uint8_t *p, uint32_t v, int bytes)
{
    switch (bytes) {
    case 4:
        memcpy(p, &v, 4);   /* fb and dumb buffers are host-endian */
        break;
    case 3:
        p[0] = (uint8_t)v;
        p[1] = (uint8_t)(v >> 8);
        p[2] = (uint8_t)(v >> 16);
        break;
    default: {
        uint16_t s = (uint16_t)v;
        memcpy(p, &s, 2);
        break;
    }
    }
}

void display_blit(const struct display *d, const uint32_t *canvas,
                  const struct rect *r, uint8_t *dst, int dst_stride,
                  const struct pixel_format *fmt)
{
    const struct pixel_format *f = fmt;
    int x0 = r->x < 0 ? 0 : r->x, y0 = r->y < 0 ? 0 : r->y;
    int x1 = r->x + r->w > d->w ? d->w : r->x + r->w;
    int y1 = r->y + r->h > d->h ? d->h : r->y + r->h;
    int bytes = f->bytes;

    for (int y = y0; y < y1; y++) {
        const uint32_t *src = canvas + (size_t)y * d->w;
        if (d->rotate == 0) {
            uint8_t *row = dst + (size_t)y * dst_stride + (size_t)x0 * bytes;
            for (int x = x0; x < x1; x++, row += bytes)
                put(row, display_pack(src[x], f), bytes);
            continue;
        }
        for (int x = x0; x < x1; x++) {
            int px, py;
            switch (d->rotate) {
            case 90:  px = d->phys_w - 1 - y; py = x; break;
            case 180: px = d->phys_w - 1 - x; py = d->phys_h - 1 - y; break;
            default:  px = y; py = d->phys_h - 1 - x; break;     /* 270 */
            }
            put(dst + (size_t)py * dst_stride + (size_t)px * bytes,
                display_pack(src[x], f), bytes);
        }
    }
}
