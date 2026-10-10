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

TEST(PowerSpeed, Types)
{
    EXPECT_TRUE(power_fast_type("USB_HVDCP"));
    EXPECT_TRUE(power_fast_type("USB_HVDCP_3"));
    EXPECT_TRUE(power_fast_type("USB_PD"));
    EXPECT_TRUE(power_fast_type("PD_PPS"));
    EXPECT_FALSE(power_fast_type("USB"));
    EXPECT_FALSE(power_fast_type("USB_DCP"));
    EXPECT_FALSE(power_fast_type("Mains"));
    EXPECT_FALSE(power_fast_type(""));
    EXPECT_TRUE(power_usb_type_fast("Unknown SDP DCP CDP C PD [PD_PPS]"));
    EXPECT_TRUE(power_usb_type_fast("SDP [PD] DCP"));
    EXPECT_FALSE(power_usb_type_fast("[DCP] PD PD_PPS"));   // offered, not active
    EXPECT_FALSE(power_usb_type_fast("SDP DCP PD"));
    EXPECT_FALSE(power_usb_type_fast("[] PD"));
    EXPECT_FALSE(power_usb_type_fast("[PD"));
}

TEST(PowerSpeed, MaxPower)
{
    // Android's formula: mA x mV, 5 V when the voltage is unknown.
    EXPECT_EQ(power_max_uw(1500000, 5000000), 7500000);
    EXPECT_EQ(power_max_uw(2000000, -1), 10000000);
    EXPECT_EQ(power_max_uw(3000000, 9000000), 27000000);
    // Not micro-units: the A3's rt5033-charger says 700 and 112.
    EXPECT_EQ(power_max_uw(700, 112), -1);
    EXPECT_EQ(power_max_uw(2000000, 112), -1);
    EXPECT_EQ(power_max_uw(700, 5000000), -1);   // current in mA
    EXPECT_EQ(power_max_uw(0, 5000000), -1);
    EXPECT_EQ(power_max_uw(99999999, 5000000), -1);
}

TEST(PowerSpeed, FromSysfs)
{
    char why[96];
    {
        FakeTree t;     // nothing online
        supply(t, "usb", "USB", "0");
        EXPECT_EQ(power_charge_speed(POWER_FAST_UW_DEFAULT, why, sizeof(why)),
                  CHARGE_SPEED_UNKNOWN);
    }
    {
        FakeTree t;     // the Galaxy A3 2015 on a USB port
        supply(t, "battery", "Battery", "4");
        t.write(PSY + "battery/charge_type", "Fast\n");    // the phase, ignored
        supply(t, "usb", "USB", "1");
        supply(t, "rt5033-charger", "Unknown", "1");
        t.write(PSY + "rt5033-charger/current_max", "700\n");
        t.write(PSY + "rt5033-charger/voltage_max", "112\n");
        EXPECT_EQ(power_charge_speed(POWER_FAST_UW_DEFAULT, why, sizeof(why)),
                  CHARGE_SPEED_NORMAL);
    }
    {
        FakeTree t;     // downstream Qualcomm on Quick Charge 3
        supply(t, "usb", "USB", "1");
        t.write(PSY + "usb/real_type", "USB_HVDCP_3\n");
        EXPECT_EQ(power_charge_speed(POWER_FAST_UW_DEFAULT, why, sizeof(why)),
                  CHARGE_SPEED_FAST);
        EXPECT_STREQ(why, "usb real_type=USB_HVDCP_3");
    }
    {
        FakeTree t;     // mainline Type-C on PD
        supply(t, "tcpm-source-psy", "USB", "1");
        t.write(PSY + "tcpm-source-psy/usb_type", "C [PD] PD_PPS\n");
        EXPECT_EQ(power_charge_speed(POWER_FAST_UW_DEFAULT, nullptr, 0),
                  CHARGE_SPEED_FAST);
    }
    {
        FakeTree t;     // by power: 9 V 2 A is fast, 5 V 1.5 A is not
        supply(t, "usb", "USB", "1");
        t.write(PSY + "usb/current_max", "2000000\n");
        t.write(PSY + "usb/voltage_max", "9000000\n");
        EXPECT_EQ(power_charge_speed(POWER_FAST_UW_DEFAULT, why, sizeof(why)),
                  CHARGE_SPEED_FAST);
        EXPECT_STREQ(why, "usb offers 18000 mW");
        t.write(PSY + "usb/current_max", "1500000\n");
        t.write(PSY + "usb/voltage_max", "5000000\n");
        EXPECT_EQ(power_charge_speed(POWER_FAST_UW_DEFAULT, why, sizeof(why)),
                  CHARGE_SPEED_NORMAL);     // 7.5 W is not above 7.5 W
        EXPECT_EQ(power_charge_speed(5000000, why, sizeof(why)),
                  CHARGE_SPEED_FAST);       // --fast-watts 5
    }
}

} // namespace
