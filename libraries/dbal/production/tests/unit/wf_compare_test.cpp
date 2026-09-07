/**
 * @file wf_compare_test.cpp
 * @brief The decision behind dbal.stop.unless.
 *
 * Deliberately word-shaped -- "equals", "contains", "not empty" -- because
 * the people writing these are describing what their business does, not
 * writing an expression language.
 */

#include <gtest/gtest.h>

#include "workflow/wf_compare.hpp"

using dbal::workflow::asText;
using dbal::workflow::compareHolds;

using J = nlohmann::json;

TEST(WfCompare, EmptyAndNotEmpty) {
    EXPECT_TRUE(compareHolds("empty", J(""), J()));
    EXPECT_FALSE(compareHolds("empty", J("something"), J()));
    EXPECT_TRUE(compareHolds("not empty", J("something"), J()));
    EXPECT_FALSE(compareHolds("not empty", J(""), J()));
}

// A field a visitor left blank arrives as null, not as "".
TEST(WfCompare, TreatsAMissingValueAsEmpty) {
    EXPECT_TRUE(compareHolds("empty", J(nullptr), J()));
    EXPECT_FALSE(compareHolds("not empty", J(nullptr), J()));
}

TEST(WfCompare, EqualsAndItsNegative) {
    EXPECT_TRUE(compareHolds("equals", J("wheel"), J("wheel")));
    EXPECT_FALSE(compareHolds("equals", J("wheel"), J("brake")));
    EXPECT_TRUE(compareHolds("does not equal", J("wheel"), J("brake")));
}

TEST(WfCompare, ContainsAndItsNegative) {
    EXPECT_TRUE(compareHolds("contains", J("buckled rear wheel"), J("wheel")));
    EXPECT_FALSE(compareHolds("contains", J("brake cable"), J("wheel")));
    EXPECT_TRUE(
        compareHolds("does not contain", J("brake cable"), J("wheel")));
}

TEST(WfCompare, IsTrue) {
    EXPECT_TRUE(compareHolds("is true", J(true), J()));
    EXPECT_FALSE(compareHolds("is true", J(false), J()));
    // A checkbox arriving as text still means what it says.
    EXPECT_TRUE(compareHolds("is true", J("true"), J()));
}

TEST(WfCompare, IgnoresTheCaseOfTheRelation) {
    EXPECT_TRUE(compareHolds("EQUALS", J("a"), J("a")));
    EXPECT_TRUE(compareHolds("Not Empty", J("a"), J()));
}

/**
 * An unknown relation holds for nothing, so the workflow stops rather
 * than carrying on as though a condition had been met. Carrying on is the
 * dangerous default: it would run the rest of the steps on a decision
 * nobody actually made.
 */
TEST(WfCompare, AnUnknownRelationHoldsForNothing) {
    EXPECT_FALSE(compareHolds("is roughly", J("a"), J("a")));
    EXPECT_FALSE(compareHolds("", J("a"), J("a")));
}

TEST(WfCompare, ComparesNumbersAndBooleansAsTheirText) {
    EXPECT_TRUE(compareHolds("equals", J(42), J(42)));
    EXPECT_EQ(asText(J("plain")), "plain");
    EXPECT_EQ(asText(J(nullptr)), "");
}
