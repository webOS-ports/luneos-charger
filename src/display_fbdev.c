// SPDX-License-Identifier: Apache-2.0
/*
 * fbdev backend. Double-buffered when the framebuffer's virtual height holds
 * two screens: the next frame goes into the hidden page, then the display is
 * panned to it. On MDSS (msm8916, msm8953, sdm660) the pan is also what
 * commits a frame to the panel - writes to the visible page alone show
 * nothing - so it is done even with a single page.
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "display.h"
#include "util.h"

struct fbdev {
    int fd;
    struct fb_var_screeninfo var;
    struct fb_fix_screeninfo fix;
    struct pixel_format fmt;
    uint8_t *mem;
    size_t mem_len;
    int pages, page;
};

static int fbdev_open(struct display *d)
{
    static const char *const nodes[] = { "/dev/fb0", "/dev/graphics/fb0", NULL };
    struct fbdev *fb = calloc(1, sizeof(*fb));
    size_t need;
    int bpp;

    if (!fb)
        return -1;
    fb->fd = -1;
    for (int i = 0; nodes[i] && fb->fd < 0; i++)
        fb->fd = open(nodes[i], O_RDWR | O_CLOEXEC);
    if (fb->fd < 0) {
        log_msg("fbdev: no framebuffer: %s", strerror(errno));
        goto fail;
    }
    if (ioctl(fb->fd, FBIOGET_VSCREENINFO, &fb->var) < 0 ||
        ioctl(fb->fd, FBIOGET_FSCREENINFO, &fb->fix) < 0) {
        log_msg("fbdev: screen info: %s", strerror(errno));
        goto fail;
    }
    bpp = (int)fb->var.bits_per_pixel;
    if (bpp != 16 && bpp != 24 && bpp != 32) {
        log_msg("fbdev: unsupported %d bpp", bpp);
        goto fail;
    }
    if (fb->var.red.length == 0 || fb->var.red.length > 8 ||
        fb->var.green.length == 0 || fb->var.green.length > 8 ||
        fb->var.blue.length == 0 || fb->var.blue.length > 8 ||
        fb->var.transp.length > 8) {
        log_msg("fbdev: unsupported channel layout");
        goto fail;
    }
    fb->fmt = (struct pixel_format){
        bpp / 8,
        (int)fb->var.red.offset, (int)fb->var.red.length,
        (int)fb->var.green.offset, (int)fb->var.green.length,
        (int)fb->var.blue.offset, (int)fb->var.blue.length,
        (int)fb->var.transp.offset, (int)fb->var.transp.length,
    };
    d->phys_w = (int)fb->var.xres;
    d->phys_h = (int)fb->var.yres;
    if (d->phys_w <= 0 || d->phys_h <= 0 ||
        fb->fix.line_length < fb->var.xres * (unsigned)(bpp / 8)) {
        log_msg("fbdev: bad geometry");
        goto fail;
    }
    fb->pages = fb->var.yres_virtual >= fb->var.yres * 2 ? 2 : 1;
    need = (size_t)fb->fix.line_length * fb->var.yres * fb->pages;
    fb->mem_len = fb->fix.smem_len;
    if (fb->mem_len < need) {
        if (fb->pages == 2 &&
            fb->mem_len >= (size_t)fb->fix.line_length * fb->var.yres) {
            fb->pages = 1;
        } else {
            log_msg("fbdev: framebuffer memory too small");
            goto fail;
        }
    }
    fb->mem = mmap(NULL, fb->mem_len, PROT_READ | PROT_WRITE, MAP_SHARED,
                   fb->fd, 0);
    if (fb->mem == MAP_FAILED) {
        fb->mem = NULL;
        log_msg("fbdev: mmap: %s", strerror(errno));
        goto fail;
    }
    log_msg("fbdev: %.16s %dx%d %d bpp stride %u, %d page(s), "
            "R%u/%u G%u/%u B%u/%u A%u/%u", fb->fix.id, d->phys_w, d->phys_h,
            bpp, fb->fix.line_length, fb->pages,
            fb->var.red.offset, fb->var.red.length,
            fb->var.green.offset, fb->var.green.length,
            fb->var.blue.offset, fb->var.blue.length,
            fb->var.transp.offset, fb->var.transp.length);
    d->priv = fb;
    return 0;
fail:
    if (fb->fd >= 0)
        close(fb->fd);
    free(fb);
    return -1;
}

static void fbdev_present(struct display *d, const uint32_t *canvas,
                          const struct rect *dirty)
{
    struct fbdev *fb = d->priv;
    int page = fb->pages == 2 ? !fb->page : 0;
    uint8_t *base = fb->mem + (size_t)page * fb->var.yres * fb->fix.line_length;
    struct rect all = { 0, 0, d->w, d->h };

    /* With one page the buffer is always the visible one: stale[0] only. */
    display_blit(d, canvas, d->stale[page] ? &all : dirty, base,
                 (int)fb->fix.line_length, &fb->fmt);
    d->stale[page] = 0;

    fb->var.yoffset = (unsigned)page * fb->var.yres;
    fb->var.activate = FB_ACTIVATE_VBL;
    if (ioctl(fb->fd, FBIOPAN_DISPLAY, &fb->var) < 0) {
        /* Some drivers only commit on a full var write. */
        fb->var.activate = FB_ACTIVATE_NOW | FB_ACTIVATE_FORCE;
        if (ioctl(fb->fd, FBIOPUT_VSCREENINFO, &fb->var) < 0)
            log_msg("fbdev: pan: %s", strerror(errno));
    }
    fb->page = page;
}

static void fbdev_set_power(struct display *d, int on)
{
    struct fbdev *fb = d->priv;

    if (ioctl(fb->fd, FBIOBLANK, on ? FB_BLANK_UNBLANK : FB_BLANK_POWERDOWN) < 0)
        log_msg("fbdev: %s: %s", on ? "unblank" : "blank", strerror(errno));
}

static void fbdev_close(struct display *d)
{
    struct fbdev *fb = d->priv;

    if (!fb)
        return;
    if (fb->mem)
        munmap(fb->mem, fb->mem_len);
    close(fb->fd);
    free(fb);
    d->priv = NULL;
}

const struct display_backend display_fbdev = {
    "fbdev", fbdev_open, fbdev_present, fbdev_set_power, fbdev_close,
};
