// SPDX-License-Identifier: Apache-2.0
/*
 * luneos-charger-preview - render the charging screen to a PNG, with the same
 * layout and drawing code the device uses, for a given panel size and state.
 *
 *   luneos-charger-preview <w> <h> <level> <full> <msg> <phase> <out.png> [fast|x] [eta-min]
 *
 * msg: none, unplugged or too_low. level -1 draws no percentage. Not
 * installed; for reviewing the layout on panels nobody has to hand.
 */
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "render.h"

#ifndef ASSETS_SRC
#define ASSETS_SRC "assets"
#endif

int main(int argc, char **argv)
{
    struct renderer r;
    struct art design;
    struct scene sc;
    png_image png;
    unsigned char *rgb;
    int w, h, phase;

    if (argc < 8 || argc > 10) {
        fprintf(stderr, "usage: %s w h level full none|unplugged|too_low phase out.png [fast|x] [eta-min]\n",
                argv[0]);
        return 2;
    }
    w = atoi(argv[1]);
    h = atoi(argv[2]);
    sc.level = atoi(argv[3]);
    sc.full = atoi(argv[4]);
    sc.fast = argc >= 9 && !strcmp(argv[8], "fast");
    sc.eta_min = argc >= 10 ? atoi(argv[9]) : -1;
    sc.msg = !strcmp(argv[5], "unplugged") ? POLICY_MSG_UNPLUGGED :
             !strcmp(argv[5], "too_low") ? POLICY_MSG_TOO_LOW : POLICY_MSG_NONE;
    phase = atoi(argv[6]);
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192)
        return 2;

    art_load(&design, getenv("ASSETS_DIR") ? getenv("ASSETS_DIR") : ASSETS_SRC);
    if (renderer_init(&r, w, h, &design, 1) < 0)
        return 1;
    renderer_frame(&r, &sc, phase);

    rgb = malloc((size_t)w * h * 3);
    if (!rgb)
        return 1;
    for (size_t i = 0; i < (size_t)w * h; i++) {
        rgb[i * 3] = (unsigned char)(r.canvas[i] >> 16);
        rgb[i * 3 + 1] = (unsigned char)(r.canvas[i] >> 8);
        rgb[i * 3 + 2] = (unsigned char)r.canvas[i];
    }
    memset(&png, 0, sizeof(png));
    png.version = PNG_IMAGE_VERSION;
    png.width = (png_uint_32)w;
    png.height = (png_uint_32)h;
    png.format = PNG_FORMAT_RGB;
    if (!png_image_write_to_file(&png, argv[7], 0, rgb, 0, NULL)) {
        fprintf(stderr, "%s: %s\n", argv[7], png.message);
        return 1;
    }
    free(rgb);
    renderer_free(&r);
    return 0;
}
