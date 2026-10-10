// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include "detect.h"
#include "fake_tree.h"

namespace {

std::string why_cmdline(const char *cmdline)
{
    char why[64] = "";
    return detect_in_cmdline(cmdline, why, sizeof(why)) ? why : "";
}

std::string why_bootconfig(const char *bc)
{
    char why[64] = "";
    return detect_in_bootconfig(bc, why, sizeof(why)) ? why : "";
}

TEST(DetectCmdline, AndroidbootModeCharger)
{
    EXPECT_EQ(why_cmdline("console=ttyMSM0 androidboot.mode=charger quiet"),
              "androidboot.mode=charger");
    EXPECT_EQ(why_cmdline("androidboot.mode=charger"), "androidboot.mode=charger");
    EXPECT_EQ(why_cmdline("androidboot.mode=\"charger\""), "androidboot.mode=charger");
}

TEST(DetectCmdline, VendorVariants)
{
    EXPECT_EQ(why_cmdline("a=b lpm_boot=1"), "lpm_boot=1");
    EXPECT_EQ(why_cmdline("lpcharge=1"), "lpcharge=1");
    EXPECT_EQ(why_cmdline("androidboot.bootchg=true"), "androidboot.bootchg=true");
    EXPECT_EQ(why_cmdline("startup=0x00010004"), "startup=0x00010004");
    EXPECT_EQ(why_cmdline("androidboot.huawei_type=oem_rtc"),
              "androidboot.huawei_type=oem_rtc");
    EXPECT_EQ(why_cmdline("androidboot.bootreason=usb"), "androidboot.bootreason=usb");
}

TEST(DetectCmdline, NormalBoots)
{
    EXPECT_EQ(why_cmdline(""), "");
    EXPECT_EQ(why_cmdline("androidboot.mode=normal"), "");
    EXPECT_EQ(why_cmdline("androidboot.mode=chargerx"), "");
    EXPECT_EQ(why_cmdline("xandroidboot.mode=charger"), "");
    EXPECT_EQ(why_cmdline("androidboot.mode"), "");
    EXPECT_EQ(why_cmdline("lpm_boot=0 lpcharge=0 androidboot.bootchg=false"), "");
    EXPECT_EQ(why_cmdline("androidboot.bootreason=reboot"), "");
    // Inside a quoted value of another parameter it is not a parameter.
    EXPECT_EQ(why_cmdline("foo=\"androidboot.mode=charger\""), "");
}

TEST(DetectCmdline, Override)
{
    EXPECT_TRUE(detect_override_in_cmdline("a no_charger_mode b"));
    EXPECT_TRUE(detect_override_in_cmdline("no_charger_mode"));
    EXPECT_FALSE(detect_override_in_cmdline("no_charger_modes"));
    EXPECT_FALSE(detect_override_in_cmdline("x=no_charger_mode"));
}

TEST(DetectBootconfig, Forms)
{
    EXPECT_EQ(why_bootconfig("androidboot.mode = \"charger\"\n"),
              "androidboot.mode=charger");
    EXPECT_EQ(why_bootconfig("androidboot.hardware = \"qcom\"\n"
                             "androidboot.mode = \"charger\"\n"
                             "androidboot.serialno = \"1234\"\n"),
              "androidboot.mode=charger");
    EXPECT_EQ(why_bootconfig("androidboot.mode=charger"), "androidboot.mode=charger");
    EXPECT_EQ(why_bootconfig("  androidboot.bootreason   =   \"usb\"  "),
              "androidboot.bootreason=usb");
    EXPECT_EQ(why_bootconfig("androidboot.mode = \"normal\"\n"), "");
    EXPECT_EQ(why_bootconfig("androidboot.mode = \"charger\", \"x\"\n"), "");
    EXPECT_EQ(why_bootconfig(""), "");
    EXPECT_EQ(why_bootconfig("garbage without equals\n\n"), "");
}

TEST(DetectBoot, FromFakeProc)
{
    char why[64];
    {
        FakeTree t;
        t.write("/proc/cmdline", "console=tty0 androidboot.mode=charger\n");
        EXPECT_EQ(detect_charger_boot(why, sizeof(why)), 1);
        EXPECT_STREQ(why, "androidboot.mode=charger");
    }
    {
        FakeTree t;
        t.write("/proc/cmdline", "console=tty0\n");
        t.write("/proc/bootconfig", "androidboot.mode = \"charger\"\n");
        EXPECT_EQ(detect_charger_boot(why, sizeof(why)), 1);
    }
    {
        FakeTree t;
        t.write("/proc/cmdline", "console=tty0\n");
        t.write("/sys/class/BOOT/BOOT/boot/boot_mode", "8\n");
        EXPECT_EQ(detect_charger_boot(why, sizeof(why)), 1);
        EXPECT_STREQ(why, "boot_mode=8");
    }
    {
        FakeTree t;
        t.write("/proc/cmdline", "console=tty0\n");
        t.write("/sys/class/BOOT/BOOT/boot/boot_mode", "0\n");
        EXPECT_EQ(detect_charger_boot(why, sizeof(why)), 0);
    }
    {
        FakeTree t;
        t.write("/proc/cmdline", "androidboot.mode=charger no_charger_mode\n");
        EXPECT_EQ(detect_charger_boot(why, sizeof(why)), 0);
        EXPECT_STREQ(why, "no_charger_mode on the cmdline");
    }
    {
        FakeTree t;     // nothing readable at all: a normal boot
        EXPECT_EQ(detect_charger_boot(why, sizeof(why)), 0);
        EXPECT_EQ(detect_charger_boot(nullptr, 0), 0);
    }
}

} // namespace
