/**
 * @file wf_graph_order_test.cpp
 * @brief Rebuilding a God-Panel workflow from its rows.
 *
 * A workflow built on the canvas is stored as WorkflowNode rows plus
 * WorkflowEdge rows. Stored order is where the boxes were dropped, so the
 * edges are the only thing that says what has to happen first -- and the
 * values come back through a type column, because a step's parameters are
 * not all scalars.
 */

#include <gtest/gtest.h>

#include "workflow/wf_graph_order.hpp"

using dbal::workflow::decodeParam;
using dbal::workflow::topologicalOrder;

using Edges = std::vector<std::pair<std::string, std::string>>;

TEST(WfGraphOrder, RunsASourceBeforeItsTarget) {
    // Stored back to front on purpose: dropping the second box first is an
    // ordinary thing to do on a canvas.
    const std::vector<std::string> keys{"save", "id"};
    const Edges edges{{"id", "save"}};

    EXPECT_EQ(topologicalOrder(keys, edges),
              (std::vector<std::string>{"id", "save"}));
}

TEST(WfGraphOrder, OrdersAWholeChain) {
    const std::vector<std::string> keys{"c", "a", "b"};
    const Edges edges{{"a", "b"}, {"b", "c"}};

    EXPECT_EQ(topologicalOrder(keys, edges),
              (std::vector<std::string>{"a", "b", "c"}));
}

TEST(WfGraphOrder, KeepsStoredOrderForNodesNoEdgeConstrains) {
    const std::vector<std::string> keys{"first", "second", "third"};

    EXPECT_EQ(topologicalOrder(keys, {}), keys);
}

TEST(WfGraphOrder, PlacesAnUnconnectedNodeWithoutDisturbingTheRest) {
    const std::vector<std::string> keys{"loose", "b", "a"};
    const Edges edges{{"a", "b"}};

    const auto ordered = topologicalOrder(keys, edges);
    const auto at = [&](const std::string& k) {
        return std::find(ordered.begin(), ordered.end(), k) - ordered.begin();
    };
    EXPECT_EQ(ordered.size(), 3u);
    EXPECT_LT(at("a"), at("b"));
}

/**
 * A canvas lets anyone draw a loop. Dropping the nodes in it would run
 * half the workflow and say nothing; running them in stored order is
 * wrong in an order the author can actually see and fix.
 */
TEST(WfGraphOrder, FallsBackToStoredOrderOnACycle) {
    const std::vector<std::string> keys{"a", "b"};
    const Edges edges{{"a", "b"}, {"b", "a"}};

    EXPECT_EQ(topologicalOrder(keys, edges), keys);
}

TEST(WfGraphOrder, IgnoresAnEdgeNamingANodeThatIsNotThere) {
    const std::vector<std::string> keys{"a", "b"};
    const Edges edges{{"a", "ghost"}, {"ghost", "b"}, {"a", "b"}};

    EXPECT_EQ(topologicalOrder(keys, edges),
              (std::vector<std::string>{"a", "b"}));
}

TEST(WfGraphOrder, HandlesNoNodesAtAll) {
    EXPECT_TRUE(topologicalOrder({}, {}).empty());
}

TEST(WfGraphParam, ReadsEachStoredType) {
    EXPECT_EQ(decodeParam("42", "number").get<double>(), 42.0);
    EXPECT_TRUE(decodeParam("true", "boolean").get<bool>());
    EXPECT_FALSE(decodeParam("false", "boolean").get<bool>());
    EXPECT_EQ(decodeParam("hello", "string").get<std::string>(), "hello");
}

/**
 * The reason the type column exists: dbal.entity.create takes a nested
 * object naming the columns to write, and that used to be stringified into
 * a plain string row -- so a saved workflow no longer described the step.
 */
TEST(WfGraphParam, RebuildsANestedObjectParameter) {
    const auto value = decodeParam(
        R"({"entity":"FormSubmission","data":{"id":"${new_id}"}})", "json");

    ASSERT_TRUE(value.is_object());
    EXPECT_EQ(value["entity"], "FormSubmission");
    EXPECT_EQ(value["data"]["id"], "${new_id}");
}

TEST(WfGraphParam, LeavesAStringThatMerelyLooksLikeJsonAlone) {
    const auto value = decodeParam(R"({"a":1})", "string");

    ASSERT_TRUE(value.is_string());
    EXPECT_EQ(value.get<std::string>(), R"({"a":1})");
}

TEST(WfGraphParam, FallsBackToTheTextWhenAJsonRowWillNotParse) {
    const auto value = decodeParam("not json", "json");

    ASSERT_TRUE(value.is_string());
    EXPECT_EQ(value.get<std::string>(), "not json");
}

// "12abc" is not 12: a partial parse would invent a value nobody wrote.
TEST(WfGraphParam, DoesNotHalfReadANumber) {
    const auto value = decodeParam("12abc", "number");

    ASSERT_TRUE(value.is_string());
    EXPECT_EQ(value.get<std::string>(), "12abc");
}

TEST(WfGraphParam, TreatsAnUnknownTypeAsText) {
    EXPECT_EQ(decodeParam("x", "something-new").get<std::string>(), "x");
}
