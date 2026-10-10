// SPDX-License-Identifier: Apache-2.0
/*
 * DRM/KMS backend, on the legacy modesetting ioctls and two dumb buffers -
 * what minui's graphics_drm.cpp does for Android's charger and recovery, and
 * so what downstream SDE and MediaTek DRM drivers are known to accept. It
 * talks to the kernel through the uapi headers directly rather than libdrm,
 * to keep the initramfs small.
 *
 * Frames are shown with a page flip and its completion event; a driver that
 * refuses page flips gets SETCRTC per frame instead. Blanking disables the
 * CRTC and unblanking sets it again, as minui does.
 */
#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "display.h"
#include "util.h"

struct drm_buf {
    uint32_t handle, fb_id, pitch;
    uint64_t size;
    uint8_t *map;
};

struct drm {
    int fd;
    uint32_t crtc_id, conn_id;
    struct drm_mode_modeinfo mode;
    struct drm_buf buf[2];
    int front;
    int use_setcrtc;
    int on;
};

/* XRGB8888, little-endian - what a 32 bpp, depth 24 dumb buffer is. */
static const struct pixel_format xrgb8888 = { 4, 16, 8, 8, 8, 0, 8, 0, 0 };

static int xioctl(int fd, unsigned long req, void *arg)
{
    int r, tries = 0;

    do {
        r = ioctl(fd, req, arg);
    } while (r < 0 && (errno == EINTR || errno == EAGAIN) && ++tries < 100);
    return r;
}

static uint64_t ptr(const void *p)
{
    return (uint64_t)(uintptr_t)p;
}

/* Fill in a connector: counts first, then arrays sized from them. The mode
 * list can change between the two calls (a hotplug), so retry a few times. */
static int get_connector(int fd, uint32_t id, struct drm_mode_get_connector *c,
                         struct drm_mode_modeinfo **modes,
                         uint32_t **encoders)
{
    for (int tries = 0; tries < 4; tries++) {
        uint32_t nmodes, nenc;

        memset(c, 0, sizeof(*c));
        c->connector_id = id;
        if (xioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, c) < 0)
            return -1;
        nmodes = c->count_modes;
        nenc = c->count_encoders;
        free(*modes);
        free(*encoders);
        *modes = calloc(nmodes ? nmodes : 1, sizeof(**modes));
        *encoders = calloc(nenc ? nenc : 1, sizeof(**encoders));
        if (!*modes || !*encoders)
            return -1;
        memset(c, 0, sizeof(*c));
        c->connector_id = id;
        c->count_modes = nmodes;
        c->modes_ptr = ptr(*modes);
        c->count_encoders = nenc;
        c->encoders_ptr = ptr(*encoders);
        if (xioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, c) < 0)
            return -1;
        if (c->count_modes <= nmodes && c->count_encoders <= nenc)
            return 0;
    }
    return -1;
}

static int pick_crtc(int fd, const struct drm_mode_get_connector *c,
                     const uint32_t *encoders, const uint32_t *crtcs,
                     uint32_t ncrtcs, uint32_t *crtc_id)
{
    struct drm_mode_get_encoder enc;

    if (c->encoder_id) {
        memset(&enc, 0, sizeof(enc));
        enc.encoder_id = c->encoder_id;
        if (xioctl(fd, DRM_IOCTL_MODE_GETENCODER, &enc) == 0 && enc.crtc_id) {
            *crtc_id = enc.crtc_id;
            return 0;
        }
    }
    for (uint32_t e = 0; e < c->count_encoders; e++) {
        memset(&enc, 0, sizeof(enc));
        enc.encoder_id = encoders[e];
        if (xioctl(fd, DRM_IOCTL_MODE_GETENCODER, &enc) < 0)
            continue;
        for (uint32_t i = 0; i < ncrtcs && i < 32; i++)
            if (enc.possible_crtcs & (1u << i)) {
                *crtc_id = crtcs[i];
                return 0;
            }
    }
    return -1;
}

static int find_output(struct drm *drm)
{
    struct drm_mode_card_res res;
    uint32_t *conns = NULL, *crtcs = NULL, *encs = NULL, *fbs = NULL;
    struct drm_mode_modeinfo *modes = NULL;
    uint32_t *conn_encs = NULL;
    int found = -1;

    memset(&res, 0, sizeof(res));
    if (xioctl(drm->fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0)
        return -1;
    conns = calloc(res.count_connectors + 1, sizeof(uint32_t));
    crtcs = calloc(res.count_crtcs + 1, sizeof(uint32_t));
    encs = calloc(res.count_encoders + 1, sizeof(uint32_t));
    fbs = calloc(res.count_fbs + 1, sizeof(uint32_t));
    if (!conns || !crtcs || !encs || !fbs)
        goto out;
    res.connector_id_ptr = ptr(conns);
    res.crtc_id_ptr = ptr(crtcs);
    res.encoder_id_ptr = ptr(encs);
    res.fb_id_ptr = ptr(fbs);
    {
        uint32_t nconn = res.count_connectors, ncrtc = res.count_crtcs;
        if (xioctl(drm->fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0)
            goto out;
        /* The kernel copies at most what we offered; take no more. */
        if (res.count_connectors > nconn)
            res.count_connectors = nconn;
        if (res.count_crtcs > ncrtc)
            res.count_crtcs = ncrtc;
    }

    for (uint32_t i = 0; i < res.count_connectors && found < 0; i++) {
        struct drm_mode_get_connector c;
        if (get_connector(drm->fd, conns[i], &c, &modes, &conn_encs) < 0)
            continue;
        if (c.connection != 1 || c.count_modes == 0)   /* 1: connected */
            continue;
        drm->mode = modes[0];
        for (uint32_t m = 0; m < c.count_modes; m++)
            if (modes[m].type & DRM_MODE_TYPE_PREFERRED) {
                drm->mode = modes[m];
                break;
            }
        if (pick_crtc(drm->fd, &c, conn_encs, crtcs, res.count_crtcs,
                      &drm->crtc_id) < 0)
            continue;
        drm->conn_id = conns[i];
        found = 0;
    }
out:
    free(conns);
    free(crtcs);
    free(encs);
    free(fbs);
    free(modes);
    free(conn_encs);
    return found;
}

static int make_buf(struct drm *drm, struct drm_buf *b, uint32_t w, uint32_t h)
{
    struct drm_mode_create_dumb create = { .height = h, .width = w, .bpp = 32 };
    struct drm_mode_fb_cmd fb;
    struct drm_mode_map_dumb map;
    void *p;

    if (xioctl(drm->fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) < 0)
        return -1;
    b->handle = create.handle;
    b->pitch = create.pitch;
    b->size = create.size;
    memset(&fb, 0, sizeof(fb));
    fb.width = w;
    fb.height = h;
    fb.pitch = create.pitch;
    fb.bpp = 32;
    fb.depth = 24;
    fb.handle = create.handle;
    if (xioctl(drm->fd, DRM_IOCTL_MODE_ADDFB, &fb) < 0)
        return -1;
    b->fb_id = fb.fb_id;
    memset(&map, 0, sizeof(map));
    map.handle = create.handle;
    if (xioctl(drm->fd, DRM_IOCTL_MODE_MAP_DUMB, &map) < 0)
        return -1;
    p = mmap(NULL, (size_t)create.size, PROT_READ | PROT_WRITE, MAP_SHARED,
             drm->fd, (off_t)map.offset);
    if (p == MAP_FAILED)
        return -1;
    b->map = p;
    memset(b->map, 0, (size_t)b->size);
    return 0;
}

static void free_buf(struct drm *drm, struct drm_buf *b)
{
    if (b->map)
        munmap(b->map, (size_t)b->size);
    if (b->fb_id) {
        uint32_t id = b->fb_id;
        xioctl(drm->fd, DRM_IOCTL_MODE_RMFB, &id);
    }
    if (b->handle) {
        struct drm_mode_destroy_dumb destroy = { .handle = b->handle };
        xioctl(drm->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
    }
    memset(b, 0, sizeof(*b));
}

static int set_crtc(struct drm *drm, uint32_t fb_id)
{
    struct drm_mode_crtc crtc;

    memset(&crtc, 0, sizeof(crtc));
    crtc.crtc_id = drm->crtc_id;
    if (fb_id) {
        crtc.fb_id = fb_id;
        crtc.set_connectors_ptr = ptr(&drm->conn_id);
        crtc.count_connectors = 1;
        crtc.mode = drm->mode;
        crtc.mode_valid = 1;
    }
    return xioctl(drm->fd, DRM_IOCTL_MODE_SETCRTC, &crtc);
}

static void drm_close(struct display *d);

static int drm_open(struct display *d)
{
    struct drm *drm = calloc(1, sizeof(*drm));
    char node[32];

    if (!drm)
        return -1;
    d->priv = drm;
    drm->fd = -1;
    for (int i = 0; i < 4; i++) {
        struct drm_get_cap cap = { .capability = DRM_CAP_DUMB_BUFFER };
        snprintf(node, sizeof(node), "/dev/dri/card%d", i);
        drm->fd = open(node, O_RDWR | O_CLOEXEC);
        if (drm->fd < 0)
            continue;
        if (xioctl(drm->fd, DRM_IOCTL_GET_CAP, &cap) == 0 && cap.value &&
            find_output(drm) == 0)
            break;
        close(drm->fd);
        drm->fd = -1;
    }
    if (drm->fd < 0) {
        log_msg("drm: no card with a connected output");
        goto fail;
    }
    /* Nobody else is running in the initramfs; this only fails if something
     * already holds master, and then the SETCRTC below says so. */
    if (xioctl(drm->fd, DRM_IOCTL_SET_MASTER, NULL) < 0)
        log_msg("drm: set master: %s", strerror(errno));
    d->phys_w = drm->mode.hdisplay;
    d->phys_h = drm->mode.vdisplay;
    if (d->phys_w <= 0 || d->phys_h <= 0)
        goto fail;
    for (int i = 0; i < 2; i++)
        if (make_buf(drm, &drm->buf[i], drm->mode.hdisplay,
                     drm->mode.vdisplay) < 0) {
            log_msg("drm: dumb buffer: %s", strerror(errno));
            goto fail;
        }
    if (set_crtc(drm, drm->buf[0].fb_id) < 0) {
        log_msg("drm: set crtc: %s", strerror(errno));
        goto fail;
    }
    drm->on = 1;
    log_msg("drm: %s, %.32s %dx%d@%u, connector %u, crtc %u", node,
            drm->mode.name, d->phys_w, d->phys_h, drm->mode.vrefresh,
            drm->conn_id, drm->crtc_id);
    return 0;
fail:
    drm_close(d);
    return -1;
}

/* Wait for the flip's completion event, so the buffer just replaced is free
 * to draw into. A lost event costs one frame, not a hang. */
static void wait_flip(struct drm *drm)
{
    struct pollfd pfd = { drm->fd, POLLIN, 0 };
    char buf[256];

    if (poll(&pfd, 1, 100) <= 0)
        return;
    while (read(drm->fd, buf, sizeof(buf)) < 0 && errno == EINTR)
        ;
}

static void drm_present(struct display *d, const uint32_t *canvas,
                        const struct rect *dirty)
{
    struct drm *drm = d->priv;
    int back = !drm->front;
    struct drm_buf *b = &drm->buf[back];
    struct rect all = { 0, 0, d->w, d->h };

    if (!drm->on)
        return;
    display_blit(d, canvas, d->stale[back] ? &all : dirty, b->map,
                 (int)b->pitch, &xrgb8888);
    d->stale[back] = 0;

    if (!drm->use_setcrtc) {
        struct drm_mode_crtc_page_flip flip;
        memset(&flip, 0, sizeof(flip));
        flip.crtc_id = drm->crtc_id;
        flip.fb_id = b->fb_id;
        flip.flags = DRM_MODE_PAGE_FLIP_EVENT;
        if (xioctl(drm->fd, DRM_IOCTL_MODE_PAGE_FLIP, &flip) == 0) {
            wait_flip(drm);
            drm->front = back;
            return;
        }
        log_msg("drm: page flip: %s - using setcrtc", strerror(errno));
        drm->use_setcrtc = 1;
    }
    if (set_crtc(drm, b->fb_id) < 0)
        log_msg("drm: set crtc: %s", strerror(errno));
    drm->front = back;
}

static void drm_set_power(struct display *d, int on)
{
    struct drm *drm = d->priv;

    if (on == drm->on)
        return;
    if (set_crtc(drm, on ? drm->buf[drm->front].fb_id : 0) < 0)
        log_msg("drm: %s crtc: %s", on ? "enable" : "disable", strerror(errno));
    drm->on = on;
}

static void drm_close(struct display *d)
{
    struct drm *drm = d->priv;

    if (!drm)
        return;
    if (drm->fd >= 0) {
        for (int i = 0; i < 2; i++)
            free_buf(drm, &drm->buf[i]);
        xioctl(drm->fd, DRM_IOCTL_DROP_MASTER, NULL);
        close(drm->fd);
    }
    free(drm);
    d->priv = NULL;
}

const struct display_backend display_drm = {
    "drm", drm_open, drm_present, drm_set_power, drm_close,
};
