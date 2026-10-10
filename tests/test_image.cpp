// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include <cstring>
#include <png.h>
#include <vector>

#include "fake_tree.h"
#include "image.h"

namespace {

struct Owned : image {
    Owned() { std::memset(static_cast<image *>(this), 0, sizeof(image)); }
    ~Owned() { image_free(this); }
};

void write_png(const std::string &path, int w, int h, png_uint_32 format,
               const std::vector<unsigned char> &px)
{
    png_image png;
    std::memset(&png, 0, sizeof(png));
    png.version = PNG_IMAGE_VERSION;
    png.width = (png_uint_32)w;
    png.height = (png_uint_32)h;
    png.format = format;
    ASSERT_TRUE(png_image_write_to_file(&png, path.c_str(), 0, px.data(), 0, nullptr));
}

TEST(Image, LoadGrey)
{
    FakeTree t;
    t.mkdir("/a");
    write_png(t.root() + "/a/g.png", 3, 2, PNG_FORMAT_GRAY, { 0, 128, 255, 1, 2, 3 });
    Owned img;
    ASSERT_EQ(image_load_png("/a/g.png", 1, &img), 0);
    EXPECT_EQ(img.w, 3);
    EXPECT_EQ(img.h, 2);
    EXPECT_EQ(img.bpp, 1);
    EXPECT_EQ(img.px[1], 128);
    EXPECT_EQ(img.px[5], 3);
}

TEST(Image, LoadRgbaPremultiplies)
{
    FakeTree t;
    t.mkdir("/a");
    write_png(t.root() + "/a/c.png", 2, 1, PNG_FORMAT_RGBA,
              { 255, 128, 0, 255, 200, 100, 50, 0 });
    Owned img;
    ASSERT_EQ(image_load_png("/a/c.png", 4, &img), 0);
    EXPECT_EQ(img.px[0], 255);
    EXPECT_EQ(img.px[1], 128);
    EXPECT_EQ(img.px[3], 255);
    EXPECT_EQ(img.px[4], 0);    // alpha 0: colour premultiplied away
    EXPECT_EQ(img.px[5], 0);
    EXPECT_EQ(img.px[7], 0);
}

TEST(Image, LoadFailures)
{
    FakeTree t;
    Owned img;
    EXPECT_EQ(image_load_png("/missing.png", 1, &img), -1);
    EXPECT_EQ(img.px, nullptr);
    t.write("/bad.png", "this is not a png");
    EXPECT_EQ(image_load_png("/bad.png", 1, &img), -1);
    t.write("/trunc.png", std::string("\x89PNG\r\n\x1a\n\0\0\0\x0dIHDR", 16));
    EXPECT_EQ(image_load_png("/trunc.png", 4, &img), -1);
    EXPECT_EQ(image_load_png("/bad.png", 3, &img), -1);
}

Owned make(int w, int h, int bpp, unsigned char fill)
{
    Owned img;
    img.w = w;
    img.h = h;
    img.bpp = bpp;
    img.px = (unsigned char *)malloc((size_t)w * h * bpp);
    std::memset(img.px, fill, (size_t)w * h * bpp);
    return img;
}

TEST(ImageScale, FlatStaysFlatAtAnyRatio)
{
    Owned src = make(37, 23, 4, 0);
    for (int i = 0; i < 37 * 23 * 4; i++)
        src.px[i] = (unsigned char)(i % 4 == 3 ? 255 : 77);
    for (int dw : { 1, 5, 18, 36, 37, 38, 100 })
        for (int dh : { 1, 7, 23, 50 }) {
            Owned dst;
            ASSERT_EQ(image_scale(&src, 0, 0, 37, 23, dw, dh, &dst), 0);
            ASSERT_EQ(dst.w, dw);
            ASSERT_EQ(dst.h, dh);
            for (int i = 0; i < dw * dh * 4; i++)
                ASSERT_EQ(dst.px[i], i % 4 == 3 ? 255 : 77)
                    << dw << "x" << dh << " at " << i;
        }
}

TEST(ImageScale, IdentityIsACopy)
{
    Owned src = make(4, 3, 1, 0);
    for (int i = 0; i < 12; i++)
        src.px[i] = (unsigned char)(i * 20);
    Owned dst;
    ASSERT_EQ(image_scale(&src, 1, 1, 2, 2, 2, 2, &dst), 0);
    EXPECT_EQ(dst.px[0], 5 * 20);
    EXPECT_EQ(dst.px[1], 6 * 20);
    EXPECT_EQ(dst.px[2], 9 * 20);
    EXPECT_EQ(dst.px[3], 10 * 20);
}

TEST(ImageScale, HalvingAverages)
{
    Owned src = make(2, 2, 1, 0);
    src.px[0] = 255;
    src.px[3] = 255;
    Owned dst;
    ASSERT_EQ(image_scale(&src, 0, 0, 2, 2, 1, 1, &dst), 0);
    EXPECT_NEAR(dst.px[0], 128, 1);
}

TEST(ImageScale, UpscaleInterpolates)
{
    Owned src = make(2, 1, 1, 0);
    src.px[1] = 255;
    Owned dst;
    ASSERT_EQ(image_scale(&src, 0, 0, 2, 1, 4, 1, &dst), 0);
    EXPECT_EQ(dst.px[0], 0);
    EXPECT_LT(dst.px[1], dst.px[2]);
    EXPECT_EQ(dst.px[3], 255);
}

TEST(ImageScale, RejectsBadArguments)
{
    Owned src = make(4, 4, 1, 9);
    Owned dst;
    EXPECT_EQ(image_scale(&src, 0, 0, 5, 4, 2, 2, &dst), -1);
    EXPECT_EQ(image_scale(&src, -1, 0, 2, 2, 2, 2, &dst), -1);
    EXPECT_EQ(image_scale(&src, 0, 0, 4, 4, 0, 2, &dst), -1);
    EXPECT_EQ(image_scale(&src, 0, 0, 0, 4, 2, 2, &dst), -1);
    EXPECT_EQ(image_scale(&src, 0, 0, 4, 4, 100000, 2, &dst), -1);
    EXPECT_EQ(dst.px, nullptr);
}

} // namespace
