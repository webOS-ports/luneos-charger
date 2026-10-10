// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include "backlight.h"
#include "fake_tree.h"

namespace {

void node(FakeTree &t, const std::string &dir, int max)
{
    t.write(dir + "/max_brightness", std::to_string(max) + "\n");
    t.write(dir + "/brightness", "0\n");
}

TEST(Backlight, PrefersLcdBacklightLed)
{
    FakeTree t;
    node(t, "/sys/class/leds/lcd-backlight", 255);
    node(t, "/sys/class/backlight/panel0", 1023);
    ASSERT_EQ(backlight_find(nullptr, -1), 0);
    EXPECT_STREQ(backlight_path(), "/sys/class/leds/lcd-backlight/brightness");
    EXPECT_EQ(backlight_level(), 255 * 5 / 8);
    backlight_set(1);
    EXPECT_EQ(t.read("/sys/class/leds/lcd-backlight/brightness"), "159");
    backlight_set(0);
    EXPECT_EQ(t.read("/sys/class/leds/lcd-backlight/brightness"), "0");
}

TEST(Backlight, SkipsPlaceholders)
{
    // The Galaxy A3 2015's backlight-class "panel" has max_brightness 0.
    FakeTree t;
    node(t, "/sys/class/backlight/panel", 0);
    node(t, "/sys/class/backlight/real", 100);
    ASSERT_EQ(backlight_find(nullptr, -1), 0);
    EXPECT_STREQ(backlight_path(), "/sys/class/backlight/real/brightness");
    EXPECT_EQ(backlight_level(), 62);
}

TEST(Backlight, MachineConfWins)
{
    FakeTree t;
    node(t, "/sys/class/leds/lcd-backlight", 80);
    node(t, "/sys/class/leds/other", 80);
    ASSERT_EQ(backlight_find("/sys/class/leds/other/brightness", 3), 0);
    EXPECT_STREQ(backlight_path(), "/sys/class/leds/other/brightness");
    EXPECT_EQ(backlight_level(), 3);
}

TEST(Backlight, NoneAtAll)
{
    FakeTree t;
    EXPECT_EQ(backlight_find(nullptr, -1), -1);
    EXPECT_STREQ(backlight_path(), "");
    backlight_set(1);       // harmless
}

TEST(Backlight, TinyRangeStillLights)
{
    FakeTree t;
    node(t, "/sys/class/backlight/x", 1);
    ASSERT_EQ(backlight_find(nullptr, -1), 0);
    EXPECT_EQ(backlight_level(), 1);
}

} // namespace
