// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include "fake_tree.h"
#include "power.h"

namespace {

const std::string PSY = "/sys/class/power_supply/";

void supply(FakeTree &t, const std::string &name, const std::string &type,
            const char *online = nullptr)
{
    t.write(PSY + name + "/type", type + "\n");
    if (online)
        t.write(PSY + name + "/online", std::string(online) + "\n");
}

TEST(Power, UiPercentMatchesBatteryd)
{
    // batteryd getUiPercent(): clamp to 0..95, then * 100 / 95.
    EXPECT_EQ(power_ui_percent(-1), -1);
    EXPECT_EQ(power_ui_percent(0), 0);
    EXPECT_EQ(power_ui_percent(1), 1);
    EXPECT_EQ(power_ui_percent(50), 52);
    EXPECT_EQ(power_ui_percent(89), 93);
    EXPECT_EQ(power_ui_percent(95), 100);
    EXPECT_EQ(power_ui_percent(100), 100);
}

TEST(Power, FindsBatteryByType)
{
    FakeTree t;
    supply(t, "usb", "USB", "1");
    supply(t, "bms", "BMS");
    supply(t, "main-battery", "Battery");
    t.write(PSY + "main-battery/capacity", "42\n");
    ASSERT_EQ(power_find(), 0);
    EXPECT_EQ(std::string(power_battery_dir()), PSY + "main-battery");
    EXPECT_EQ(power_capacity(), 42);
}

TEST(Power, NoBattery)
{
    FakeTree t;
    supply(t, "usb", "USB", "1");
    EXPECT_EQ(power_find(), -1);
    EXPECT_EQ(power_capacity(), -1);
    EXPECT_FALSE(power_full(-1));
}

TEST(Power, CapacityRobustness)
{
    FakeTree t;
    supply(t, "battery", "Battery");
    ASSERT_EQ(power_find(), 0);
    t.write(PSY + "battery/capacity", "abc\n");
    EXPECT_EQ(power_capacity(), -1);
    t.write(PSY + "battery/capacity", "120\n");
    EXPECT_EQ(power_capacity(), 100);
    t.write(PSY + "battery/capacity", "-5\n");
    EXPECT_EQ(power_capacity(), -1);
    t.write(PSY + "battery/capacity", "\n");
    EXPECT_EQ(power_capacity(), -1);
}

TEST(Power, ChargerOnlineSkipsBatteryOnline)
{
    // The Galaxy A3 2015: the sec battery's "online" is a cable type (4), and
    // the real chargers are "usb" (USB) and "rt5033-charger" (Unknown).
    FakeTree t;
    supply(t, "battery", "Battery", "4");
    supply(t, "ac", "Mains", "0");
    supply(t, "usb", "USB", "0");
    supply(t, "rt5033-charger", "Unknown", "0");
    supply(t, "rt5033-fuelgauge", "Unknown");
    EXPECT_EQ(power_charger(), CHARGER_OFFLINE);
    t.write(PSY + "usb/online", "1\n");
    EXPECT_EQ(power_charger(), CHARGER_ONLINE);
}

TEST(Power, ChargerUnknownWithoutOnline)
{
    FakeTree t;
    supply(t, "battery", "Battery", "1");
    supply(t, "bms", "BMS", "1");
    supply(t, "gauge", "Unknown");
    EXPECT_EQ(power_charger(), CHARGER_UNKNOWN);
}

TEST(Power, ChargerUnknownWithoutSysfs)
{
    FakeTree t;
    EXPECT_EQ(power_charger(), CHARGER_UNKNOWN);
}

TEST(Power, FullSignals)
{
    FakeTree t;
    supply(t, "battery", "Battery");
    t.write(PSY + "battery/status", "Charging\n");
    ASSERT_EQ(power_find(), 0);
    EXPECT_FALSE(power_full(80));
    EXPECT_TRUE(power_full(95));            // LuneOS shows 100 %
    t.write(PSY + "battery/status", "Full\n");
    EXPECT_TRUE(power_full(80));
    t.write(PSY + "battery/status", "Charging\n");
    t.write(PSY + "battery/charge_done", "1\n");
    EXPECT_TRUE(power_full(80));
    t.write(PSY + "battery/charge_done", "0\n");
    EXPECT_FALSE(power_full(80));
}

} // namespace
