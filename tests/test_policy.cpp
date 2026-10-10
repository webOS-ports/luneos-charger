// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include "policy.h"

namespace {

class Policy : public ::testing::Test {
protected:
    void SetUp() override
    {
        cfg = POLICY_CONFIG_DEFAULT;
        policy_init(&p, &cfg, 0);
    }
    enum policy_exit at(long long t, enum charger_state c = CHARGER_ONLINE,
                        int cap = 50)
    {
        return policy_update(&p, t, c, cap);
    }
    struct policy_config cfg;
    struct policy p;
};

TEST_F(Policy, DefaultsAreTheDocumentedOnes)
{
    EXPECT_EQ(cfg.screen_timeout_ms, 10000);
    EXPECT_EQ(cfg.hold_ms, 2000);
    EXPECT_EQ(cfg.unplug_ms, 10000);
    EXPECT_EQ(cfg.discovery_ms, 15000);
    EXPECT_EQ(cfg.boot_min_capacity, -1);
}

TEST_F(Policy, ScreenOnAtStartThenTimesOut)
{
    EXPECT_TRUE(policy_screen_on(&p));
    EXPECT_EQ(at(9999), POLICY_RUN);
    EXPECT_TRUE(policy_screen_on(&p));
    EXPECT_EQ(at(10000), POLICY_RUN);
    EXPECT_FALSE(policy_screen_on(&p));
}

TEST_F(Policy, PressWakesImmediatelyAndReleaseKeepsItOn)
{
    at(10000);
    ASSERT_FALSE(policy_screen_on(&p));
    policy_key(&p, 12000, 1);
    EXPECT_TRUE(policy_screen_on(&p));
    policy_key(&p, 12100, 0);
    EXPECT_TRUE(policy_screen_on(&p));
    at(21999);
    EXPECT_TRUE(policy_screen_on(&p));
    at(22000);
    EXPECT_FALSE(policy_screen_on(&p));
}

TEST_F(Policy, ShortPressWhileOnTurnsItOff)
{
    policy_key(&p, 1000, 1);
    policy_key(&p, 1200, 0);
    EXPECT_FALSE(policy_screen_on(&p));
    EXPECT_EQ(at(1300), POLICY_RUN);
    EXPECT_FALSE(policy_screen_on(&p));
}

TEST_F(Policy, ScreenStaysOnWhileHeld)
{
    policy_key(&p, 9000, 1);
    EXPECT_EQ(at(10500), POLICY_RUN);      // held past the old timeout
    EXPECT_TRUE(policy_screen_on(&p));
}

TEST_F(Policy, HoldBoots)
{
    policy_key(&p, 1000, 1);
    EXPECT_EQ(at(2999), POLICY_RUN);
    EXPECT_EQ(at(3000), POLICY_BOOT);
}

TEST_F(Policy, HoldWhileScreenOffStillBoots)
{
    at(10000);
    policy_key(&p, 20000, 1);
    EXPECT_EQ(at(22000), POLICY_BOOT);
}

TEST_F(Policy, ReleasedBeforeHoldDoesNotBoot)
{
    policy_key(&p, 1000, 1);
    policy_key(&p, 2999, 0);
    EXPECT_EQ(at(5000), POLICY_RUN);
}

TEST_F(Policy, RepeatedDownIsIgnored)
{
    policy_key(&p, 1000, 1);
    policy_key(&p, 2500, 1);    // a second device reporting the same press
    EXPECT_EQ(at(3000), POLICY_BOOT);
}

TEST_F(Policy, ReleaseWithoutPressIsIgnored)
{
    policy_key(&p, 1000, 0);
    EXPECT_TRUE(policy_screen_on(&p));
}

TEST_F(Policy, TooLowToBoot)
{
    cfg.boot_min_capacity = 6;
    policy_init(&p, &cfg, 0);
    policy_key(&p, 1000, 1);
    EXPECT_EQ(at(3000, CHARGER_ONLINE, 5), POLICY_RUN);
    EXPECT_EQ(policy_message(&p, 3000), POLICY_MSG_TOO_LOW);
    EXPECT_TRUE(policy_screen_on(&p));
    // Still held: it does not boot later in the same press either.
    EXPECT_EQ(at(8000, CHARGER_ONLINE, 5), POLICY_RUN);
    policy_key(&p, 8100, 0);
    EXPECT_TRUE(policy_screen_on(&p));     // the release does not blank
    EXPECT_EQ(policy_message(&p, 6001), POLICY_MSG_NONE);
    // A new hold once there is enough charge boots.
    policy_key(&p, 9000, 1);
    EXPECT_EQ(at(11000, CHARGER_ONLINE, 6), POLICY_BOOT);
}

TEST_F(Policy, UnknownCapacityDoesNotBlockBoot)
{
    cfg.boot_min_capacity = 6;
    policy_init(&p, &cfg, 0);
    policy_key(&p, 0, 1);
    EXPECT_EQ(at(2000, CHARGER_ONLINE, -1), POLICY_BOOT);
}

TEST_F(Policy, UnplugPowersOffAfterGrace)
{
    at(1000, CHARGER_ONLINE);
    at(20000, CHARGER_ONLINE);
    ASSERT_FALSE(policy_screen_on(&p));
    EXPECT_EQ(at(21000, CHARGER_OFFLINE), POLICY_RUN);
    EXPECT_TRUE(policy_screen_on(&p));     // woken to say so
    EXPECT_EQ(policy_message(&p, 21000), POLICY_MSG_UNPLUGGED);
    EXPECT_EQ(at(30999, CHARGER_OFFLINE), POLICY_RUN);
    EXPECT_EQ(at(31000, CHARGER_OFFLINE), POLICY_POWEROFF);
}

TEST_F(Policy, ReplugCancelsPowerOff)
{
    at(1000, CHARGER_ONLINE);
    at(2000, CHARGER_OFFLINE);
    EXPECT_EQ(at(11000, CHARGER_ONLINE), POLICY_RUN);
    EXPECT_EQ(policy_message(&p, 11000), POLICY_MSG_NONE);
    EXPECT_EQ(at(13000, CHARGER_OFFLINE), POLICY_RUN);  // the grace restarts
    EXPECT_EQ(at(22999, CHARGER_OFFLINE), POLICY_RUN);
    EXPECT_EQ(at(23000, CHARGER_OFFLINE), POLICY_POWEROFF);
}

TEST_F(Policy, ChargerNeverSeen)
{
    // Started in charger mode but no supply ever reports online (a slow
    // MediaTek charger driver, or a bogus charger boot): wait for discovery,
    // then give up rather than sit here forever.
    EXPECT_EQ(at(1000, CHARGER_OFFLINE), POLICY_RUN);
    EXPECT_EQ(policy_message(&p, 1000), POLICY_MSG_NONE);
    EXPECT_EQ(at(14999, CHARGER_OFFLINE), POLICY_RUN);
    EXPECT_EQ(at(15000, CHARGER_OFFLINE), POLICY_POWEROFF);
}

TEST_F(Policy, ChargerSeenLateIsFine)
{
    EXPECT_EQ(at(14000, CHARGER_OFFLINE), POLICY_RUN);
    EXPECT_EQ(at(14500, CHARGER_ONLINE), POLICY_RUN);
    EXPECT_EQ(at(60000, CHARGER_ONLINE), POLICY_RUN);
}

TEST_F(Policy, UnknownChargerNeverPowersOff)
{
    EXPECT_EQ(at(1000, CHARGER_UNKNOWN), POLICY_RUN);
    EXPECT_EQ(at(3600000, CHARGER_UNKNOWN), POLICY_RUN);
    EXPECT_EQ(policy_message(&p, 3600000), POLICY_MSG_NONE);
}

TEST_F(Policy, HoldBeatsUnplugInTheSameUpdate)
{
    at(0, CHARGER_ONLINE);
    at(1000, CHARGER_OFFLINE);
    policy_key(&p, 9000, 1);
    // At 11000 both the 10 s grace and the 2 s hold expire; powering off wins,
    // because the charger is gone and booting would drain the battery.
    EXPECT_EQ(at(11000, CHARGER_OFFLINE), POLICY_POWEROFF);
}

TEST_F(Policy, NextDeadline)
{
    EXPECT_EQ(policy_next_deadline(&p, 0), 10000);
    policy_key(&p, 1000, 1);
    EXPECT_EQ(policy_next_deadline(&p, 1000), 2000);
    policy_key(&p, 1500, 0);
    at(1500);
    EXPECT_FALSE(policy_screen_on(&p));
    EXPECT_GT(policy_next_deadline(&p, 1500), 1000000000LL);
    at(2000, CHARGER_OFFLINE);
    EXPECT_EQ(policy_next_deadline(&p, 2000), 10000);
    EXPECT_EQ(policy_next_deadline(&p, 50000), 0);
}

} // namespace
