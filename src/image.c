// SPDX-License-Identifier: Apache-2.0
#include "image.h"

#include <limits.h>
#include <png.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

/* Our own artwork is a few hundred pixels a side; anything far bigger is not
 * ours and is refused rather than allocated. */
#define IMAGE_MAX_SIDE 8192

void image_free(struct image *img)
{
    free(img->px);
    memset(img, 0, sizeof(*img));
}

int image_load_png(const char *path, int bpp, struct image *img)
{
    char full[PATH_MAX];
    png_image png;
    size_t size;

    memset(img, 0, sizeof(*img));
    if (bpp != 1 && bpp != 4)
        return -1;
    if (!root_path(full, sizeof(full), path))
        return -1;
    memset(&png, 0, sizeof(png));
    png.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&png, full)) {
        log_msg("cannot load %s: %s", path, png.message);
        return -1;
    }
    if (png.width == 0 || png.height == 0 ||
        png.width > IMAGE_MAX_SIDE || png.height > IMAGE_MAX_SIDE) {
        log_msg("%s: unexpected size %ux%u", path, png.width, png.height);
        png_image_free(&png);
        return -1;
    }
    /* PNG_FORMAT_RGBA with PNG_FORMAT_FLAG_ASSOCIATED_ALPHA would premultiply
     * in linear light; blending here is in sRGB, so premultiply below. */
    png.format = bpp == 1 ? PNG_FORMAT_GRAY : PNG_FORMAT_RGBA;
    size = PNG_IMAGE_SIZE(png);
    img->px = malloc(size);
    if (!img->px) {
        png_image_free(&png);
        return -1;
    }
    if (!png_image_finish_read(&png, NULL, img->px, 0, NULL)) {
        log_msg("cannot decode %s: %s", path, png.message);
        image_free(img);
        png_image_free(&png);
        return -1;
    }
    img->w = (int)png.width;
    img->h = (int)png.height;
    img->bpp = bpp;
    if (bpp == 4) {
        for (size_t i = 0; i < size; i += 4) {
            unsigned a = img->px[i + 3];
            for (int c = 0; c < 3; c++)
                img->px[i + c] = (unsigned char)((img->px[i + c] * a + 127) / 255);
        }
    }
    return 0;
}

/*
 * One axis of a separable resample. For each of the dn output samples, the
 * weights of the n input samples are computed once:
 *   shrinking  each output covers n/dn inputs; inputs are weighted by how
 *              much of them falls inside (area averaging);
 *   growing    linear interpolation between the two nearest inputs, with
 *              pixel centres aligned.
 * Weights are 16.16 fixed point and sum to 65536 per output.
 */
struct tap {
    int first, count;
    int32_t *w;
};

static struct tap *make_taps(int n, int dn)
{
    struct tap *taps = calloc((size_t)dn, sizeof(*taps));

    if (!taps)
        return NULL;
    for (int o = 0; o < dn; o++) {
        struct tap *t = &taps[o];
        if (dn < n) {
            double scale = (double)n / dn, x0 = o * scale, x1 = x0 + scale;
            int first = (int)x0, last = (int)x1;
            int32_t sum = 0;
            int big = 0;
            if (last >= n)
                last = n - 1;
            t->first = first;
            t->count = last - first + 1;
            t->w = calloc((size_t)t->count, sizeof(int32_t));
            if (!t->w)
                goto fail;
            for (int i = 0; i < t->count; i++) {
                double a = first + i, b = a + 1;
                double cov = (b < x1 ? b : x1) - (a > x0 ? a : x0);
                t->w[i] = cov > 0 ? (int32_t)(cov / scale * 65536.0 + 0.5) : 0;
                sum += t->w[i];
                if (t->w[i] > t->w[big])
                    big = i;
            }
            /* The rounding error goes to the heaviest tap, which can take
             * it without going negative. */
            t->w[big] += 65536 - sum;
        } else {
            double x = (o + 0.5) * n / dn - 0.5;
            int i0 = x < 0 ? 0 : (int)x;
            double f = x - i0;
            if (i0 >= n - 1) {
                i0 = n - 1;
                f = 0;
            }
            if (f < 0)
                f = 0;
            t->first = i0;
            t->count = i0 + 1 < n ? 2 : 1;
            t->w = calloc(2, sizeof(int32_t));
            if (!t->w)
                goto fail;
            t->w[0] = (int32_t)((1.0 - f) * 65536.0 + 0.5);
            if (t->count == 2)
                t->w[1] = 65536 - t->w[0];
            else
                t->w[0] = 65536;
        }
    }
    return taps;
fail:
    for (int o = 0; o < dn; o++)
        free(taps[o].w);
    free(taps);
    return NULL;
}

static unsigned char clamp8(int64_t acc)
{
    acc = (acc + 32768) >> 16;
    return (unsigned char)(acc < 0 ? 0 : acc > 255 ? 255 : acc);
}

static void free_taps(struct tap *taps, int dn)
{
    if (!taps)
        return;
    for (int o = 0; o < dn; o++)
        free(taps[o].w);
    free(taps);
}

int image_scale(const struct image *src, int x, int y, int w, int h,
                int dw, int dh, struct image *dst)
{
    struct tap *tx = NULL, *ty = NULL;
    unsigned char *tmp = NULL;
    int bpp = src->bpp, ret = -1;

    memset(dst, 0, sizeof(*dst));
    if (!src->px || w <= 0 || h <= 0 || dw <= 0 || dh <= 0 ||
        dw > IMAGE_MAX_SIDE || dh > IMAGE_MAX_SIDE ||
        x < 0 || y < 0 || x + w > src->w || y + h > src->h)
        return -1;
    tx = make_taps(w, dw);
    ty = make_taps(h, dh);
    tmp = malloc((size_t)dw * h * bpp);
    dst->px = malloc((size_t)dw * dh * bpp);
    if (!tx || !ty || !tmp || !dst->px)
        goto out;

    /* Horizontal: src rows -> tmp (dw x h). */
    for (int r = 0; r < h; r++) {
        const unsigned char *in = src->px + ((size_t)(y + r) * src->w + x) * bpp;
        unsigned char *out = tmp + (size_t)r * dw * bpp;
        for (int o = 0; o < dw; o++)
            for (int c = 0; c < bpp; c++) {
                int64_t acc = 0;
                for (int i = 0; i < tx[o].count; i++)
                    acc += (int64_t)tx[o].w[i] * in[(tx[o].first + i) * bpp + c];
                out[o * bpp + c] = clamp8(acc);
            }
    }
    /* Vertical: tmp columns -> dst (dw x dh). */
    for (int o = 0; o < dh; o++) {
        unsigned char *out = dst->px + (size_t)o * dw * bpp;
        for (int col = 0; col < dw * bpp; col++) {
            int64_t acc = 0;
            for (int i = 0; i < ty[o].count; i++)
                acc += (int64_t)ty[o].w[i] *
                       tmp[(size_t)(ty[o].first + i) * dw * bpp + col];
            out[col] = clamp8(acc);
        }
    }
    dst->w = dw;
    dst->h = dh;
    dst->bpp = bpp;
    ret = 0;
out:
    free_taps(tx, dw);
    free_taps(ty, dh);
    free(tmp);
    if (ret)
        image_free(dst);
    return ret;
}
