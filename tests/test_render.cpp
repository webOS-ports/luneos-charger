// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

#include "render.h"
#include "util.h"

namespace {

// Panels LuneOS runs on or may: portrait phones from 480x800 to 1440x3200,
// athena's 1080x1620, square-ish and landscape tablets.
const std::vector<std::pair<int, int>> panels = {
    { 480, 800 }, { 540, 960 }, { 720, 1280 }, { 768, 1024 }, { 1080, 1620 },
    { 1080, 1920 }, { 1080, 2340 }, { 1440, 3040 }, { 1440, 3200 },
    { 800, 1280 }, { 1024, 768 }, { 1280, 800 }, { 1920, 1200 }, { 2000, 1200 },
    { 1080, 1080 }, { 320, 480 }, { 720, 720 }, { 716, 720 }, { 1430, 1440 },
    { 800, 600 }, { 1024, 600 }, { 1080, 1620 }, { 720, 1600 }, { 800, 1340 },
};

// The committed artwork: ASSETS_SRC (set by meson), or $ASSETS_DIR when the
// tests run somewhere the source tree is not, such as on a device.
const char *assets()
{
    const char *env = std::getenv("ASSETS_DIR");
    return env && *env ? env : ASSETS_SRC;
}

bool inside(int x, int y, int w, int h, int W, int H)
{
    return x >= 0 && y >= 0 && w > 0 && h > 0 && x + w <= W && y + h <= H;
}

TEST(Layout, EverythingFitsOnEveryPanel)
{
    for (auto [w, h] : panels) {
        struct layout L;
        layout_compute(w, h, 0, &L);
        SCOPED_TRACE(std::to_string(w) + "x" + std::to_string(h));
        EXPECT_GT(L.s, 0.0);
        bool square = w * 4 <= h * 5 && h * 4 <= w * 5;
        EXPECT_EQ(L.square, square);
        EXPECT_EQ(L.landscape, w > h && !square);
        EXPECT_TRUE(inside(L.logo_x, L.logo_y, L.logo_size, L.logo_size, w, h));
        EXPECT_TRUE(inside(L.bat_x, L.bat_y, L.bat_w, L.bat_h, w, h));
        EXPECT_TRUE(inside(L.nub_x, L.nub_y, L.nub_w, L.nub_h, w, h));
        // Nothing overlaps: the percentage is under the battery, or beside it
        // on a square panel.
        if (L.square) {
            EXPECT_GT(L.text_cx - L.s * 260, L.bat_x + L.bat_w);
            EXPECT_GE(L.pct_y, L.bat_y);
            EXPECT_LE(L.label_y, L.bat_y + L.bat_h);
            EXPECT_LE(L.bat_y + L.bat_h, L.hint_y);
        } else {
            EXPECT_LE(L.bat_y + L.bat_h, L.pct_y);
        }
        EXPECT_LT(L.pct_y, L.label_y);
        EXPECT_LT(L.label_y, L.hint_y);
        EXPECT_LT(L.hint_y, h);
        if (!L.landscape && !L.square) {
            EXPECT_LE(L.logo_y + L.logo_size, L.nub_y);
        }
        // The battery's interior survives the inset.
        EXPECT_GT(L.bat_w - 2 * (L.bat_edge + L.bat_gap), 0);
        EXPECT_GT(L.bat_h - 2 * (L.bat_edge + L.bat_gap), 0);
    }
}

TEST(Layout, ScalesWithTheShorterFit)
{
    struct layout a, b;
    layout_compute(DESIGN_W, DESIGN_H, 0, &a);
    EXPECT_DOUBLE_EQ(a.s, 1.0);
    layout_compute(DESIGN_W / 2, DESIGN_H, 0, &b);
    EXPECT_DOUBLE_EQ(b.s, 0.5);
}

TEST(ArtIdx, ParseLine)
{
    struct rect r;
    EXPECT_EQ(art_parse_idx_line("glyph_0 0 0 114 273\n", &r), SPR_DIGIT0);
    EXPECT_EQ(r.w, 114);
    EXPECT_EQ(r.h, 273);
    EXPECT_EQ(art_parse_idx_line("glyph_9 0 2457 114 273", &r), SPR_DIGIT9);
    EXPECT_EQ(art_parse_idx_line("too_low 0 1 2 3", &r), SPR_TOO_LOW);
    EXPECT_EQ(art_parse_idx_line("fast 0 1 2 3", &r), SPR_FAST);
    EXPECT_EQ(art_parse_idx_line("unknown 0 0 1 1", &r), -1);
    EXPECT_EQ(art_parse_idx_line("hint 0 0 0 1", &r), -1);
    EXPECT_EQ(art_parse_idx_line("hint -1 0 1 1", &r), -1);
    EXPECT_EQ(art_parse_idx_line("hint 0 0 1", &r), -1);
    EXPECT_EQ(art_parse_idx_line("", &r), -1);
    // Out-of-range numbers are rejected, not undefined behaviour.
    EXPECT_EQ(art_parse_idx_line("hint 0 0 99999999999999999999 1", &r), -1);
    EXPECT_EQ(art_parse_idx_line("hint 0 0 123456 1", &r), -1);
    EXPECT_EQ(art_parse_idx_line("hint 0 0 12x 1", &r), -1);
    EXPECT_EQ(art_parse_idx_line("hin 0 0 1 1", &r), -1);
    EXPECT_EQ(art_parse_idx_line("hintx 0 0 1 1", &r), -1);
}

TEST(Art, CommittedAssetsLoad)
{
    struct art a;
    ASSERT_EQ(art_load(&a, assets()), SPR_N);
    EXPECT_EQ(a.logo.w, 800);       // twice design size
    EXPECT_EQ(a.logo.bpp, 4);
    for (int i = SPR_DIGIT0; i <= SPR_DIGIT9; i++)
        EXPECT_EQ(a.spr[i].h, a.spr[SPR_DIGIT0].h);
    art_free(&a);
}

TEST(Art, MissingDirectoryIsNotFatal)
{
    struct art a;
    EXPECT_EQ(art_load(&a, "/nonexistent-luneos-charger-assets"), 0);
    EXPECT_EQ(a.logo.px, nullptr);
    art_free(&a);
}

class Renderer : public ::testing::TestWithParam<std::pair<int, int>> {};

TEST_P(Renderer, FramesOnEveryPanel)
{
    auto [w, h] = GetParam();
    struct art design;
    struct renderer r;
    art_load(&design, assets());
    ASSERT_EQ(renderer_init(&r, w, h, &design, 1), 0);

    struct scene sc = { 42, 0, 0, POLICY_MSG_NONE };
    EXPECT_EQ(renderer_frame(&r, &sc, 0), 1);       // first frame: all of it
    EXPECT_EQ(renderer_frame(&r, &sc, 500), 0);     // then the battery only
    EXPECT_TRUE(inside(r.dirty.x, r.dirty.y, r.dirty.w, r.dirty.h, w, h));
    sc.level = 43;
    EXPECT_EQ(renderer_frame(&r, &sc, 600), 1);     // a new level: all again
    sc.fast = 1;
    EXPECT_EQ(renderer_frame(&r, &sc, 650), 1);     // fast: new label and bolt
    EXPECT_EQ(renderer_frame(&r, &sc, 660), 0);
    sc.msg = POLICY_MSG_UNPLUGGED;
    EXPECT_EQ(renderer_frame(&r, &sc, 700), 1);
    sc.full = 1;
    sc.level = 100;
    EXPECT_EQ(renderer_frame(&r, &sc, 800), 1);
    EXPECT_EQ(renderer_frame(&r, &sc, 900), -1);    // charged: nothing moves
    sc.level = -1;
    sc.full = 0;
    EXPECT_EQ(renderer_frame(&r, &sc, 0), 1);       // unknown level draws too

    // Something was drawn, and the corners stayed black.
    bool lit = false;
    for (int i = 0; i < w * h && !lit; i++)
        lit = r.canvas[i] != 0;
    EXPECT_TRUE(lit);
    EXPECT_EQ(r.canvas[0], 0u);
    EXPECT_EQ(r.canvas[(size_t)w * h - 1], 0u);
    renderer_free(&r);
}

INSTANTIATE_TEST_SUITE_P(Panels, Renderer, ::testing::ValuesIn(panels));

// The logo keeps its share of the screen however dense the panel, and is
// never upscaled up to 2880 pixels across.
TEST(Renderer, LogoScalesWithThePanel)
{
    for (auto [w, h] : std::vector<std::pair<int, int>>{
             { 540, 960 }, { 1080, 1920 }, { 1440, 3200 }, { 2880, 4800 } }) {
        struct art design;
        struct renderer r;
        art_load(&design, assets());
        int src_w = design.logo.w;
        ASSERT_EQ(renderer_init(&r, w, h, &design, 1), 0);
        SCOPED_TRACE(std::to_string(w) + "x" + std::to_string(h));
        EXPECT_NEAR((double)r.art.logo.w / w, 400.0 / DESIGN_W, 0.01);
        EXPECT_EQ(r.art.logo.w, r.L.logo_size);
        EXPECT_LE(r.art.logo.w, src_w);
        renderer_free(&r);
    }
}

// The bolt is drawn on a fast charger and not otherwise: the centre of the
// battery's interior is white with it, blue (charge) without.
TEST(Renderer, BoltOnFastCharger)
{
    struct art design;
    struct renderer r;
    art_load(&design, assets());
    ASSERT_EQ(renderer_init(&r, 1080, 1920, &design, 0), 0);
    ASSERT_NE(r.bolt.px, nullptr);
    int cx = r.L.bat_x + r.L.bat_w / 2, cy = r.L.bat_y + r.L.bat_h / 2;
    struct scene sc = { 90, 0, 0, POLICY_MSG_NONE };
    renderer_frame(&r, &sc, 0);
    uint32_t plain = r.canvas[(size_t)cy * 1080 + cx];
    sc.fast = 1;
    renderer_frame(&r, &sc, 0);
    uint32_t bolt = r.canvas[(size_t)cy * 1080 + cx];
    EXPECT_NE(plain, 0xffffffu);
    EXPECT_EQ(bolt, 0xffffffu);
    sc.full = 1;                                    // charged: no bolt
    renderer_frame(&r, &sc, 0);
    EXPECT_NE(r.canvas[(size_t)cy * 1080 + cx], 0xffffffu);
    renderer_free(&r);
}

// 1 % is still visibly filled: the bottom row inside the battery, a few
// pixels above the rounded corner, carries charge colour.
TEST(Renderer, OnePercentIsVisible)
{
    struct art design;
    struct renderer r;
    art_load(&design, assets());
    ASSERT_EQ(renderer_init(&r, 1080, 1920, &design, 0), 0);
    int inset = r.L.bat_edge + r.L.bat_gap;
    int ih = r.L.bat_h - 2 * inset;
    int cx = r.L.bat_x + r.L.bat_w / 2;
    int y = r.L.bat_y + inset + ih - ih / 50;     // 2 % up from the bottom
    struct scene sc = { 0, 0, 0, POLICY_MSG_NONE };
    renderer_frame(&r, &sc, 0);
    EXPECT_EQ(r.canvas[(size_t)y * 1080 + cx], 0u);    // 0 %: empty
    sc.level = 1;
    renderer_frame(&r, &sc, 0);
    EXPECT_NE(r.canvas[(size_t)y * 1080 + cx], 0u);    // 1 %: visible
    renderer_free(&r);
}

TEST(Renderer, StaticModeOnlyRedrawsOnChange)
{
    struct art design;
    struct renderer r;
    art_load(&design, assets());
    ASSERT_EQ(renderer_init(&r, 540, 960, &design, 0), 0);
    struct scene sc = { 42, 0, 0, POLICY_MSG_NONE };
    EXPECT_EQ(renderer_frame(&r, &sc, 0), 1);
    EXPECT_EQ(renderer_frame(&r, &sc, 500), -1);
    renderer_invalidate(&r);
    EXPECT_EQ(renderer_frame(&r, &sc, 500), 1);
    renderer_free(&r);
}

TEST(Renderer, WithoutArtwork)
{
    struct art design;
    struct renderer r;
    std::memset(&design, 0, sizeof(design));
    ASSERT_EQ(renderer_init(&r, 720, 1280, &design, 1), 0);
    struct scene sc = { 7, 0, 1, POLICY_MSG_TOO_LOW };
    EXPECT_EQ(renderer_frame(&r, &sc, 0), 1);
    renderer_free(&r);
}

} // namespace
