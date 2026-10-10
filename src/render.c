// SPDX-License-Identifier: Apache-2.0
#include "render.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

/* ---------------------------------------------------------------- art */

static const char *const sprite_names[SPR_N] = {
    "glyph_0", "glyph_1", "glyph_2", "glyph_3", "glyph_4",
    "glyph_5", "glyph_6", "glyph_7", "glyph_8", "glyph_9", "glyph_pct",
    "charging", "charged", "hint", "unplugged", "too_low",
};

/* A non-negative decimal of at most five digits (the atlas is far smaller),
 * followed by a space or the end of the line. */
static const char *parse_num(const char *s, int *out)
{
    char *end;
    long v;

    while (*s == ' ')
        s++;
    if (*s < '0' || *s > '9')
        return NULL;
    v = strtol(s, &end, 10);
    if (end - s > 5 || (*end && *end != ' ' && *end != '\n'))
        return NULL;
    *out = (int)v;
    return end;
}

int art_parse_idx_line(const char *line, struct rect *r)
{
    size_t len = strcspn(line, " ");
    int *fields[] = { &r->x, &r->y, &r->w, &r->h };
    const char *p = line + len;

    for (int i = 0; i < 4; i++)
        if (!(p = parse_num(p, fields[i])))
            return -1;
    if (r->w <= 0 || r->h <= 0)
        return -1;
    for (int i = 0; i < SPR_N; i++)
        if (strlen(sprite_names[i]) == len && !strncmp(line, sprite_names[i], len))
            return i;
    return -1;
}

int art_load(struct art *a, const char *dir)
{
    struct image atlas;
    char path[512], line[128];
    FILE *f;
    int loaded = 0;

    memset(a, 0, sizeof(*a));
    snprintf(path, sizeof(path), "%s/logo.png", dir);
    image_load_png(path, 4, &a->logo);
    snprintf(path, sizeof(path), "%s/text.png", dir);
    if (image_load_png(path, 1, &atlas) < 0)
        return 0;
    snprintf(path, sizeof(path), "%s/text.idx", dir);
    {
        char full[600];
        f = root_path(full, sizeof(full), path) ? fopen(full, "re") : NULL;
    }
    if (!f) {
        log_msg("cannot open %s", path);
        image_free(&atlas);
        return 0;
    }
    while (fgets(line, sizeof(line), f)) {
        struct rect r;
        int id = art_parse_idx_line(line, &r);
        if (id < 0 || r.x + r.w > atlas.w || r.y + r.h > atlas.h ||
            a->spr[id].px)
            continue;
        /* A 1:1 "scale" is a plain copy of the sub-image. */
        if (image_scale(&atlas, r.x, r.y, r.w, r.h, r.w, r.h, &a->spr[id]) == 0)
            loaded++;
    }
    fclose(f);
    image_free(&atlas);
    return loaded;
}

static int scaled(int v, double s)
{
    int n = (int)lround(v * s);

    return n < 1 ? 1 : n;
}

int art_scale(const struct art *src, double s, struct art *out)
{
    memset(out, 0, sizeof(*out));
    if (src->logo.px &&
        image_scale(&src->logo, 0, 0, src->logo.w, src->logo.h,
                    scaled(src->logo.w, s), scaled(src->logo.h, s),
                    &out->logo) < 0)
        return -1;
    for (int i = 0; i < SPR_N; i++) {
        const struct image *in = &src->spr[i];
        if (in->px &&
            image_scale(in, 0, 0, in->w, in->h, scaled(in->w, s),
                        scaled(in->h, s), &out->spr[i]) < 0)
            return -1;
    }
    return 0;
}

void art_free(struct art *a)
{
    image_free(&a->logo);
    for (int i = 0; i < SPR_N; i++)
        image_free(&a->spr[i]);
}

/* ------------------------------------------------------------- layout */

/* The artwork is rendered at this multiple of design size (gen_assets.py),
 * so that it is only ever scaled down on panels up to twice the reference. */
#define ASSET_SCALE 2

/* Design sizes, in design pixels. */
#define LOGO 400
#define BAT_W 320
#define BAT_H 533
#define BAT_EDGE 16
#define BAT_GAP 13
#define BAT_R 45
#define NUB_W 128
#define NUB_H 37
#define NUB_R 13
#define GLYPH_H 273
#define LABEL_H 79
#define HINT_H 60

void layout_compute(int w, int h, int glyph_h, struct layout *L)
{
    int dw, dh, logo_cx, logo_y, col_cx, bat_y, hint_y, ox, oy;
    double s;

    if (glyph_h <= 0)
        glyph_h = GLYPH_H;
    memset(L, 0, sizeof(*L));
    L->square = w * 4 <= h * 5 && h * 4 <= w * 5;
    L->landscape = w > h && !L->square;
    if (L->square) {
        /* Logo on top, battery and percentage side by side under it: the
         * portrait stack would leave a square panel mostly empty. */
        dw = dh = DESIGN_SQ;
        logo_cx = DESIGN_SQ / 2;
        logo_y = 110;
        col_cx = DESIGN_SQ / 2 - 260;
        bat_y = 640;
        hint_y = DESIGN_SQ - 110 - HINT_H;
    } else if (L->landscape) {
        /* Logo on the left, battery and percentage on the right. */
        dw = DESIGN_LW;
        dh = DESIGN_LH;
        logo_cx = 780;
        logo_y = (DESIGN_LH - LOGO) / 2 - 60;
        col_cx = 1780;
        bat_y = 150;
        hint_y = DESIGN_LH - 90 - HINT_H;
    } else {
        dw = DESIGN_W;
        dh = DESIGN_H;
        logo_cx = DESIGN_W / 2;
        logo_y = 200;
        col_cx = DESIGN_W / 2;
        bat_y = 760;
        hint_y = DESIGN_H - 160 - HINT_H;
    }
    s = (double)w / dw < (double)h / dh ? (double)w / dw : (double)h / dh;
    ox = (int)lround((w - dw * s) / 2);
    oy = (int)lround((h - dh * s) / 2);
#define X(v) (ox + (int)lround((v) * s))
#define Y(v) (oy + (int)lround((v) * s))
#define S(v) scaled(v, s)
    L->s = s;
    L->logo_size = S(LOGO);
    L->logo_x = X(logo_cx) - L->logo_size / 2;
    L->logo_y = Y(logo_y);
    L->bat_w = S(BAT_W);
    L->bat_h = S(BAT_H);
    L->bat_x = X(col_cx) - L->bat_w / 2;
    L->bat_y = Y(bat_y);
    L->bat_edge = S(BAT_EDGE);
    L->bat_gap = S(BAT_GAP);
    L->bat_r = S(BAT_R);
    L->nub_w = S(NUB_W);
    L->nub_h = S(NUB_H);
    L->nub_r = S(NUB_R);
    L->nub_x = X(col_cx) - L->nub_w / 2;
    /* The nub tucks under the body's top edge by half the edge width. */
    L->nub_y = L->bat_y - L->nub_h + L->bat_edge / 2;
    if (L->square) {
        /* Percentage and label centred on the battery's height, to its
         * right. */
        int text_y = bat_y + (BAT_H - glyph_h - 20 - LABEL_H) / 2;
        L->text_cx = X(DESIGN_SQ / 2 + 300);
        L->pct_y = Y(text_y);
        L->label_y = Y(text_y + glyph_h + 20);
    } else {
        L->text_cx = X(col_cx);
        L->pct_y = Y(bat_y + BAT_H + 50);
        L->label_y = Y(bat_y + BAT_H + 50 + glyph_h + 20);
    }
    L->hint_cx = X(dw / 2.0);
    L->hint_y = Y(hint_y);
#undef X
#undef Y
#undef S
}

/* ------------------------------------------------------------ drawing */

static inline uint32_t rgb(int r, int g, int b)
{
    return (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
}

/* src over dst with coverage a (0..255), straight colour. */
static inline uint32_t blend(uint32_t dst, uint32_t src, int a)
{
    int r = ((int)(src >> 16 & 0xff) * a + (int)(dst >> 16 & 0xff) * (255 - a) + 127) / 255;
    int g = ((int)(src >> 8 & 0xff) * a + (int)(dst >> 8 & 0xff) * (255 - a) + 127) / 255;
    int b = ((int)(src & 0xff) * a + (int)(dst & 0xff) * (255 - a) + 127) / 255;

    return rgb(r, g, b);
}

static inline void plot(struct renderer *r, int x, int y, uint32_t c, int a)
{
    uint32_t *p;

    if (!a || x < 0 || y < 0 || x >= r->w || y >= r->h)
        return;
    p = &r->canvas[(size_t)y * r->w + x];
    *p = a >= 255 ? c : blend(*p, c, a);
}

/* Coverage (0..255) of pixel (x,y) inside a rounded rectangle, with a
 * one-pixel anti-aliased edge at the corners. */
static int rrect_cov(int x, int y, int x0, int y0, int w, int h, int rad)
{
    long dx, dy, d2, rin, rout;
    int cx, cy;

    if (x < x0 || y < y0 || x >= x0 + w || y >= y0 + h)
        return 0;
    if (rad * 2 > w)
        rad = w / 2;
    if (rad * 2 > h)
        rad = h / 2;
    if (rad <= 0)
        return 255;
    cx = x < x0 + rad ? x0 + rad : (x >= x0 + w - rad ? x0 + w - rad - 1 : x);
    cy = y < y0 + rad ? y0 + rad : (y >= y0 + h - rad ? y0 + h - rad - 1 : y);
    if (cx == x && cy == y)
        return 255;
    dx = x - cx;
    dy = y - cy;
    d2 = dx * dx + dy * dy;
    rin = (long)(rad - 1) * (rad - 1);
    rout = (long)rad * rad;
    if (d2 <= rin)
        return 255;
    if (d2 >= rout)
        return 0;
    return (int)(255 * (rout - d2) / (rout - rin));
}

static void fill_rrect(struct renderer *r, int x0, int y0, int w, int h,
                       int rad, uint32_t c)
{
    for (int y = y0; y < y0 + h; y++)
        for (int x = x0; x < x0 + w; x++)
            plot(r, x, y, c, rrect_cov(x, y, x0, y0, w, h, rad));
}

static void draw_sprite(struct renderer *r, const struct image *s, int x0,
                        int y0, uint32_t c)
{
    for (int y = 0; y < s->h; y++)
        for (int x = 0; x < s->w; x++)
            plot(r, x0 + x, y0 + y, c, s->px[(size_t)y * s->w + x]);
}

static void draw_centred(struct renderer *r, const struct image *s, int cx,
                         int y0, uint32_t c)
{
    if (s->px)
        draw_sprite(r, s, cx - s->w / 2, y0, c);
}

/* Premultiplied RGBA over the canvas. */
static void draw_logo(struct renderer *r, int x0, int y0)
{
    const struct image *img = &r->art.logo;

    for (int y = 0; y < img->h; y++)
        for (int x = 0; x < img->w; x++) {
            const unsigned char *s = &img->px[((size_t)y * img->w + x) * 4];
            int px = x0 + x, py = y0 + y, a = s[3];
            uint32_t *p, d;
            if (!a || px < 0 || py < 0 || px >= r->w || py >= r->h)
                continue;
            p = &r->canvas[(size_t)py * r->w + px];
            d = *p;
            *p = rgb(s[0] + ((int)(d >> 16 & 0xff) * (255 - a) + 127) / 255,
                     s[1] + ((int)(d >> 8 & 0xff) * (255 - a) + 127) / 255,
                     s[2] + ((int)(d & 0xff) * (255 - a) + 127) / 255);
        }
}

static void draw_percent(struct renderer *r, int level, int cx, int y0,
                         uint32_t c)
{
    char s[12];
    int width = 0, x;

    snprintf(s, sizeof(s), "%d", level);
    for (const char *p = s; *p; p++) {
        if (!r->art.spr[SPR_DIGIT0 + (*p - '0')].px)
            return;
        width += r->art.spr[SPR_DIGIT0 + (*p - '0')].w;
    }
    if (!r->art.spr[SPR_PCT].px)
        return;
    width += r->art.spr[SPR_PCT].w;
    x = cx - width / 2;
    for (const char *p = s; *p; p++) {
        const struct image *g = &r->art.spr[SPR_DIGIT0 + (*p - '0')];
        draw_sprite(r, g, x, y0, c);
        x += g->w;
    }
    draw_sprite(r, &r->art.spr[SPR_PCT], x, y0, c);
}

int renderer_init(struct renderer *r, int w, int h, struct art *design,
                  int animate)
{
    int glyph_h = design->spr[SPR_DIGIT0].h / ASSET_SCALE;

    memset(r, 0, sizeof(*r));
    r->w = w;
    r->h = h;
    r->animate = animate;
    r->canvas = calloc((size_t)w * h, sizeof(uint32_t));
    if (!r->canvas) {
        art_free(design);
        return -1;
    }
    layout_compute(w, h, glyph_h, &r->L);
    if (art_scale(design, r->L.s / ASSET_SCALE, &r->art) < 0) {
        art_free(&r->art);
        log_msg("cannot scale the artwork; drawing without it");
    }
    art_free(design);
    log_msg("layout: %dx%d %s, scale %.3f", w, h,
            r->L.landscape ? "landscape" : "portrait", r->L.s);
    return 0;
}

void renderer_invalidate(struct renderer *r)
{
    r->have_last = 0;
}

void renderer_free(struct renderer *r)
{
    free(r->canvas);
    art_free(&r->art);
    memset(r, 0, sizeof(*r));
}

int renderer_frame(struct renderer *r, const struct scene *sc, int phase)
{
    const struct layout *L = &r->L;
    int inset = L->bat_edge + L->bat_gap;
    int ix = L->bat_x + inset, iy = L->bat_y + inset;
    int iw = L->bat_w - 2 * inset, ih = L->bat_h - 2 * inset;
    int ir = L->bat_r - L->bat_edge - L->bat_gap / 2;
    int lvl = sc->level < 0 ? 0 : (sc->level > 100 ? 100 : sc->level);
    int low = !sc->full && sc->level >= 0 && sc->level < 15;
    int full_redraw = 0, fill_top, sweep_top;
    uint32_t outline = rgb(0xd8, 0xe2, 0xee), label = rgb(0xa8, 0xb8, 0xcc);

    if (!r->have_last || r->last.level != sc->level ||
        r->last.full != sc->full || r->last.msg != sc->msg) {
        const struct image *msg = NULL;
        r->last = *sc;
        r->have_last = 1;
        full_redraw = 1;
        memset(r->canvas, 0, (size_t)r->w * r->h * sizeof(uint32_t));
        if (r->art.logo.px)
            draw_logo(r, L->logo_x + (L->logo_size - r->art.logo.w) / 2, L->logo_y);
        /* Battery body: outline ring, then the terminal nub. */
        fill_rrect(r, L->bat_x, L->bat_y, L->bat_w, L->bat_h, L->bat_r, outline);
        fill_rrect(r, L->bat_x + L->bat_edge, L->bat_y + L->bat_edge,
                   L->bat_w - 2 * L->bat_edge, L->bat_h - 2 * L->bat_edge,
                   L->bat_r - L->bat_edge, 0);
        fill_rrect(r, L->nub_x, L->nub_y, L->nub_w, L->nub_h, L->nub_r, outline);
        if (sc->level >= 0)
            draw_percent(r, sc->level, L->text_cx, L->pct_y, rgb(0xff, 0xff, 0xff));
        draw_centred(r, &r->art.spr[sc->full ? SPR_CHARGED : SPR_CHARGING],
                     L->text_cx, L->label_y, label);
        if (sc->msg == POLICY_MSG_UNPLUGGED)
            msg = &r->art.spr[SPR_UNPLUGGED];
        else if (sc->msg == POLICY_MSG_TOO_LOW)
            msg = &r->art.spr[SPR_TOO_LOW];
        if (msg && msg->px)
            draw_centred(r, msg, L->hint_cx, L->hint_y, rgb(0xff, 0xb0, 0x60));
        else
            draw_centred(r, &r->art.spr[SPR_HINT], L->hint_cx, L->hint_y,
                         rgb(0x6c, 0x78, 0x88));
    } else if (!r->animate || sc->full) {
        return -1;                  /* nothing moves */
    }

    r->dirty = (struct rect){ ix, iy, iw, ih };
    if (iw <= 0 || ih <= 0)
        return full_redraw;
    for (int y = iy; y < iy + ih; y++)
        if (y >= 0 && y < r->h) {
            int x0 = ix < 0 ? 0 : ix, x1 = ix + iw > r->w ? r->w : ix + iw;
            if (x1 > x0)
                memset(&r->canvas[(size_t)y * r->w + x0], 0,
                       (size_t)(x1 - x0) * sizeof(uint32_t));
        }

    /* Charge in the logo's blues (red-orange when low), and a dimmer band
     * sweeping from the level to the top while it is still charging. */
    fill_top = iy + ih - ih * lvl / 100;
    sweep_top = sc->full || !r->animate ? fill_top
                                        : fill_top - (fill_top - iy) * phase / 1000;
    for (int y = iy; y < iy + ih; y++) {
        int t = (y - iy) * 255 / ih;        /* 0 at the top, 255 at the bottom */
        uint32_t c;
        if (y >= fill_top)
            c = low ? rgb(0xff, 0x6a - t / 6, 0x2a)
                    : rgb(0x4f - t * 0x3b / 255, 0xc8 - t * 0x78 / 255,
                          0xff - t * 0x4b / 255);
        else if (y >= sweep_top)
            c = low ? rgb(0x70, 0x30, 0x18) : rgb(0x1c, 0x46, 0x70);
        else
            continue;
        for (int x = ix; x < ix + iw; x++)
            plot(r, x, y, c, rrect_cov(x, y, ix, iy, iw, ih, ir));
    }
    return full_redraw;
}
