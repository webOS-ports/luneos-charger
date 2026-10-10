// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include "fake_tree.h"
#include "leds.h"

namespace {

const std::string LEDS = "/sys/class/leds/";

void led(FakeTree &t, const std::string &name, int max = 255)
{
    t.write(LEDS + name + "/max_brightness", std::to_string(max) + "\n");
    t.write(LEDS + name + "/brightness", "0\n");
    t.write(LEDS + name + "/trigger", "[none] timer\n");
}

class Leds : public ::testing::Test {
protected:
    void SetUp() override { leds_reset(); }
    void TearDown() override { leds_reset(); }
};

TEST_F(Leds, NameParts)
{
    EXPECT_TRUE(leds_name_has_part("red", "red"));
    EXPECT_TRUE(leds_name_has_part("red:status", "red"));
    EXPECT_TRUE(leds_name_has_part("pmi8998:red:charging", "red"));
    EXPECT_FALSE(leds_name_has_part("reddish", "red"));
    EXPECT_FALSE(leds_name_has_part("infrared:x", "red"));
    EXPECT_FALSE(leds_name_has_part("", "red"));
}

TEST_F(Leds, Rank)
{
    EXPECT_EQ(leds_rank("red"), 1);
    EXPECT_EQ(leds_rank("red:status"), 2);
    EXPECT_EQ(leds_rank("green:indicator"), 2);
    EXPECT_EQ(leds_rank("red:charging"), 3);
    EXPECT_EQ(leds_rank("white:flash"), 0);
    EXPECT_EQ(leds_rank("led:torch"), 0);
    EXPECT_EQ(leds_rank("lcd-backlight"), 0);
    EXPECT_EQ(leds_rank("button-backlight"), 0);
    EXPECT_EQ(leds_rank("mmc0::"), 0);
    EXPECT_EQ(leds_rank("white:kbd_backlight"), 0);
}

TEST_F(Leds, BareColours)
{
    FakeTree t;
    led(t, "red");
    led(t, "green");
    led(t, "blue");
    led(t, "lcd-backlight");
    led(t, "white:flash");
    EXPECT_EQ(leds_find(), 3);
    EXPECT_EQ(std::string(leds_dir(LED_RED)), LEDS + "red");
    EXPECT_EQ(std::string(leds_dir(LED_GREEN)), LEDS + "green");
    EXPECT_EQ(std::string(leds_dir(LED_BLUE)), LEDS + "blue");
}

TEST_F(Leds, ChargingBeatsPlainColour)
{
    FakeTree t;
    led(t, "red");
    led(t, "red:charging");
    led(t, "green:status");
    leds_find();
    EXPECT_EQ(std::string(leds_dir(LED_RED)), LEDS + "red:charging");
    EXPECT_EQ(std::string(leds_dir(LED_GREEN)), LEDS + "green:status");
}

TEST_F(Leds, NoneFound)
{
    FakeTree t;
    led(t, "lcd-backlight");
    led(t, "white:torch");
    EXPECT_EQ(leds_find(), 0);
    leds_show(LED_SHOW_CHARGING);       // must be harmless
    EXPECT_EQ(t.read(LEDS + "lcd-backlight/brightness"), "0\n");
}

TEST_F(Leds, ShowRedThenGreen)
{
    FakeTree t;
    led(t, "red", 255);
    led(t, "green", 100);
    led(t, "blue", 255);
    t.write(LEDS + "blue/brightness", "7");
    leds_find();
    leds_show(LED_SHOW_CHARGING);
    EXPECT_EQ(t.read(LEDS + "red/brightness"), "255");
    EXPECT_EQ(t.read(LEDS + "green/brightness"), "0");
    EXPECT_EQ(t.read(LEDS + "blue/brightness"), "0");
    EXPECT_EQ(t.read(LEDS + "red/trigger"), "none");
    leds_show(LED_SHOW_FULL);
    EXPECT_EQ(t.read(LEDS + "red/brightness"), "0");
    EXPECT_EQ(t.read(LEDS + "green/brightness"), "100");
    leds_show(LED_SHOW_OFF);
    EXPECT_EQ(t.read(LEDS + "red/brightness"), "0");
    EXPECT_EQ(t.read(LEDS + "green/brightness"), "0");
}

TEST_F(Leds, ShowIsIdempotent)
{
    FakeTree t;
    led(t, "red");
    leds_find();
    leds_show(LED_SHOW_CHARGING);
    t.write(LEDS + "red/brightness", "untouched");
    leds_show(LED_SHOW_CHARGING);       // same state: no writes
    EXPECT_EQ(t.read(LEDS + "red/brightness"), "untouched");
}

TEST_F(Leds, Multicolour)
{
    FakeTree t;
    led(t, "rgb:status");
    t.write(LEDS + "rgb:status/multi_index", "green blue red\n");
    t.write(LEDS + "rgb:status/multi_intensity", "0 0 0\n");
    EXPECT_EQ(leds_find(), 1);
    EXPECT_EQ(std::string(leds_multi_dir()), LEDS + "rgb:status");
    leds_show(LED_SHOW_CHARGING);
    EXPECT_EQ(t.read(LEDS + "rgb:status/multi_intensity"), "0 0 255");
    EXPECT_EQ(t.read(LEDS + "rgb:status/brightness"), "255");
    leds_show(LED_SHOW_FULL);
    EXPECT_EQ(t.read(LEDS + "rgb:status/multi_intensity"), "255 0 0");
    leds_show(LED_SHOW_OFF);
    EXPECT_EQ(t.read(LEDS + "rgb:status/brightness"), "0");
}

TEST_F(Leds, OverrideWins)
{
    FakeTree t;
    led(t, "red:charging");
    led(t, "my-red");
    leds_override(LED_RED, (LEDS + "my-red").c_str());
    leds_find();
    EXPECT_EQ(std::string(leds_dir(LED_RED)), LEDS + "my-red");
}

} // namespace
