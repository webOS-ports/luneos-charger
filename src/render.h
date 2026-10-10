/* SPDX-License-Identifier: Apache-2.0 */
#ifndef LUNEOS_CHARGER_RENDER_H
#define LUNEOS_CHARGER_RENDER_H

#include <stdint.h>

#include "display.h"
#include "image.h"
#include "policy.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The screen is designed once, on a reference canvas of DESIGN_W x DESIGN_H
 * design pixels (portrait), DESIGN_LW x DESIGN_LH (landscape) or DESIGN_SQ
 * square (panels within 5:4 of square, with the percentage beside the
 * battery rather than under it), and scaled
 * uniformly to fit the panel, centred: the logo and text take the same share
 * of the screen on a 540x960 panel as on a 1440x3200 one. The artwork is
 * pre-rendered at twice design size (tools/gen_assets.py) and resampled down
 * once at start-up, so it stays sharp up to 2880 pixels across.
 */
#define DESIGN_W 1440
#define DESIGN_H 2400
#define DESIGN_LW 2560
#define DESIGN_LH 1440
#define DESIGN_SQ 1700      /* near-square panels (q25, Titan): 1700 x 1700 */

enum sprite_id {
    SPR_DIGIT0, SPR_DIGIT9 = SPR_DIGIT0 + 9, SPR_PCT,
    SPR_CHARGING, SPR_CHARGED, SPR_FAST, SPR_HINT, SPR_UNPLUGGED, SPR_TOO_LOW,
    SPR_N
};

/* Coverage sprites and the logo, at design size or scaled. */
struct art {
    struct image logo;              /* premultiplied RGBA */
    struct image spr[SPR_N];        /* coverage */
};

/* Load logo.png, text.png and text.idx from dir, as rendered (twice design
 * size).
 * Missing pieces are left empty and simply not drawn. Returns how many of
 * the sprites loaded. */
int art_load(struct art *a, const char *dir);

/* Parse one text.idx line ("name x y w h") into a sprite id and rectangle;
 * returns -1 for an unknown name or a malformed line. Exposed for the tests. */
int art_parse_idx_line(const char *line, struct rect *r);

/* Resample art by s into out. */
int art_scale(const struct art *src, double s, struct art *out);

void art_free(struct art *a);

struct layout {
    double s;                       /* design pixels -> panel pixels */
    int landscape;
    int square;                     /* percentage beside the battery */
    int logo_x, logo_y, logo_size;
    int bat_x, bat_y, bat_w, bat_h, bat_edge, bat_gap, bat_r;
    int nub_x, nub_y, nub_w, nub_h, nub_r;
    int text_cx;                    /* centre of the battery's column */
    int pct_y, label_y;
    int hint_cx, hint_y;
};

/* Lay the screen out for a w x h canvas. glyph_h is the design height of the
 * percentage's digits (0: the default). */
void layout_compute(int w, int h, int glyph_h, struct layout *L);

struct scene {
    int level;                      /* percent shown, -1 unknown */
    int full;
    int fast;                       /* a fast charger: bolt and label */
    enum policy_message msg;
};

struct renderer {
    int w, h;
    uint32_t *canvas;               /* w * h, 0x00RRGGBB */
    struct layout L;
    struct art art;                 /* scaled to L.s */
    struct image bolt;              /* coverage, sized to the battery */
    int animate;
    struct scene last;
    int have_last;
    struct rect dirty;              /* what changed in the last frame */
};

/* Set up for a w x h canvas with the art as loaded (taken over: design is
 * freed). Returns 0 on success. */
int renderer_init(struct renderer *r, int w, int h, struct art *design,
                  int animate);

/* Draw a frame. phase runs 0..999 over the animation cycle. Returns 1 if the
 * whole canvas was redrawn (the display must then copy all of it), 0 if only
 * r->dirty changed, -1 if nothing changed. */
int renderer_frame(struct renderer *r, const struct scene *sc, int phase);

/* Force the next frame to redraw everything. */
void renderer_invalidate(struct renderer *r);

void renderer_free(struct renderer *r);

#ifdef __cplusplus
}
#endif

#endif
