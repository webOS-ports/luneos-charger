// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "display.h"

namespace {

const struct pixel_format xrgb8888 = { 4, 16, 8, 8, 8, 0, 8, 0, 0 };
const struct pixel_format argb8888 = { 4, 16, 8, 8, 8, 0, 8, 24, 8 };
const struct pixel_format abgr8888 = { 4, 0, 8, 8, 8, 16, 8, 24, 8 };
const struct pixel_format rgb565 = { 2, 11, 5, 5, 6, 0, 5, 0, 0 };
const struct pixel_format rgb888 = { 3, 16, 8, 8, 8, 0, 8, 0, 0 };

TEST(DisplayPack, Formats)
{
    EXPECT_EQ(display_pack(0x123456, &xrgb8888), 0x123456u);
    EXPECT_EQ(display_pack(0x123456, &argb8888), 0xff123456u);
    EXPECT_EQ(display_pack(0x123456, &abgr8888), 0xff563412u);
    EXPECT_EQ(display_pack(0xffffff, &rgb565), 0xffffu);
    EXPECT_EQ(display_pack(0xff0000, &rgb565), 0xf800u);
    EXPECT_EQ(display_pack(0x00ff00, &rgb565), 0x07e0u);
    EXPECT_EQ(display_pack(0x0000ff, &rgb565), 0x001fu);
    EXPECT_EQ(display_pack(0x000000, &rgb565), 0u);
}

struct display make(int pw, int ph, int rotate)
{
    struct display d;
    std::memset(&d, 0, sizeof(d));
    d.phys_w = pw;
    d.phys_h = ph;
    d.rotate = rotate;
    d.w = rotate % 180 ? ph : pw;
    d.h = rotate % 180 ? pw : ph;
    return d;
}

// A 3x2 logical canvas with distinct pixels 1..6.
const uint32_t canvas[] = { 1, 2, 3, 4, 5, 6 };

std::vector<uint32_t> blit(int rotate, const struct rect &r)
{
    struct display d = make(rotate % 180 ? 2 : 3, rotate % 180 ? 3 : 2, rotate);
    std::vector<uint32_t> out((size_t)d.phys_w * d.phys_h, 0);
    display_blit(&d, canvas, &r, (uint8_t *)out.data(), d.phys_w * 4, &xrgb8888);
    return out;
}

TEST(DisplayBlit, Rotations)
{
    const struct rect all = { 0, 0, 3, 2 };
    // 0:   1 2 3 / 4 5 6
    EXPECT_EQ(blit(0, all), (std::vector<uint32_t>{ 1, 2, 3, 4, 5, 6 }));
    // 90 clockwise onto a 2x3 panel: 4 1 / 5 2 / 6 3
    EXPECT_EQ(blit(90, all), (std::vector<uint32_t>{ 4, 1, 5, 2, 6, 3 }));
    // 180: 6 5 4 / 3 2 1
    EXPECT_EQ(blit(180, all), (std::vector<uint32_t>{ 6, 5, 4, 3, 2, 1 }));
    // 270: 3 6 / 2 5 / 1 4
    EXPECT_EQ(blit(270, all), (std::vector<uint32_t>{ 3, 6, 2, 5, 1, 4 }));
}

TEST(DisplayBlit, OnlyTheDirtyRectAndClipped)
{
    EXPECT_EQ(blit(0, { 1, 1, 1, 1 }), (std::vector<uint32_t>{ 0, 0, 0, 0, 5, 0 }));
    EXPECT_EQ(blit(0, { 2, 1, 10, 10 }), (std::vector<uint32_t>{ 0, 0, 0, 0, 0, 6 }));
    EXPECT_EQ(blit(0, { -5, -5, 6, 6 }), (std::vector<uint32_t>{ 1, 0, 0, 0, 0, 0 }));
    EXPECT_EQ(blit(0, { 0, 0, 0, 0 }), (std::vector<uint32_t>(6, 0)));
}

TEST(DisplayBlit, Rgb565WithStridePadding)
{
    struct display d = make(2, 1, 0);
    const uint32_t c[] = { 0xff0000, 0x0000ff };
    uint8_t buf[8];
    std::memset(buf, 0xaa, sizeof(buf));
    const struct rect all = { 0, 0, 2, 1 };
    display_blit(&d, c, &all, buf, 8, &rgb565);
    uint16_t px[2];
    std::memcpy(px, buf, 4);
    EXPECT_EQ(px[0], 0xf800);
    EXPECT_EQ(px[1], 0x001f);
    EXPECT_EQ(buf[4], 0xaa);        // the padding is left alone
}

TEST(DisplayBlit, Rgb888)
{
    struct display d = make(1, 1, 0);
    const uint32_t c[] = { 0x112233 };
    uint8_t buf[3] = { 0, 0, 0 };
    const struct rect all = { 0, 0, 1, 1 };
    display_blit(&d, c, &all, buf, 3, &rgb888);
    EXPECT_EQ(buf[0], 0x33);
    EXPECT_EQ(buf[1], 0x22);
    EXPECT_EQ(buf[2], 0x11);
}

TEST(Display, OpenRejectsBadRotation)
{
    struct display d;
    EXPECT_EQ(display_open(&d, "auto", 45), -1);
}

TEST(Display, OpenUnknownBackendFails)
{
    struct display d;
    EXPECT_EQ(display_open(&d, "vnc", 0), -1);
}

} // namespace
