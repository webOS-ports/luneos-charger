/* SPDX-License-Identifier: Apache-2.0 */
#ifndef LUNEOS_CHARGER_IMAGE_H
#define LUNEOS_CHARGER_IMAGE_H

#ifdef __cplusplus
extern "C" {
#endif

/* An 8-bit image: bpp 1 is coverage (alpha only), bpp 4 is RGBA with the
 * colour premultiplied by alpha. Rows are w * bpp bytes, no padding. */
struct image {
    int w, h, bpp;
    unsigned char *px;
};

/* Load a PNG as coverage (bpp 1, from its grey channel) or as premultiplied
 * RGBA (bpp 4). Returns 0 on success; img is zeroed on failure. */
int image_load_png(const char *path, int bpp, struct image *img);

/* Resample the (x, y, w, h) part of src to dw x dh into dst: area averaging
 * when shrinking, bilinear when growing, each axis separately. Returns 0 on
 * success. */
int image_scale(const struct image *src, int x, int y, int w, int h,
                int dw, int dh, struct image *dst);

void image_free(struct image *img);

#ifdef __cplusplus
}
#endif

#endif
