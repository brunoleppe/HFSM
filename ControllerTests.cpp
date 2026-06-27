#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "hfsm.h"

// Fixture: Root(0) -> A(1) [default child], Root(0) -> B(2)
// Covers, end-to-end through controller::update:
//   - ancestor walk + guard-based rejection/fallthrough
//   - internal transitions (no exit/enter)
//   - self-transitions (forced full exit/re-enter)
//   - no matching row anywhere up the chain (no-op)

struct log_ctx
{
    std::vector<std::string> events;
};

using fsm = hfsm::controller<log_ctx&>;

struct RootState : fsm::state
{
    void on_enter(log_ctx& ctx) const override { ctx.events.push_back("enter:Root"); }
    void on_exit(log_ctx& ctx) const override { ctx.events.push_back("exit:Root"); }
};

struct AState : fsm::state
{
    void on_enter(log_ctx& ctx) const override { ctx.events.push_back("enter:A"); }
    void on_exit(log_ctx& ctx) const override { ctx.events.push_back("exit:A"); }
};

struct BState : fsm::state
{
    void on_enter(log_ctx& ctx) const override { ctx.events.push_back("enter:B"); }
    void on_exit(log_ctx& ctx) const override { ctx.events.push_back("exit:B"); }
};

struct RejectingTransition : fsm::transition
{
    bool on_guard(log_ctx& ctx) const override { return false; }
    void on_action(log_ctx& ctx) const override { ctx.events.push_back("action:should-not-fire"); }
};

struct ToB : fsm::transition
{
    void on_action(log_ctx& ctx) const override { ctx.events.push_back("action:to-B"); }
};

struct SelfTransition : fsm::transition
{
    void on_action(log_ctx& ctx) const override { ctx.events.push_back("action:self"); }
};

struct PingTransition : fsm::transition
{
    void on_action(log_ctx& ctx) const override { ctx.events.push_back("action:ping"); }
};

constexpr int ROOT = 0, A = 1, B = 2;
constexpr int EVT_TO_B = 1, EVT_PING = 2, EVT_SELF = 3;

constexpr RootState rootState;
constexpr AState aState;
constexpr BState bState;
constexpr const fsm::state* states[] = {&rootState, &aState, &bState};

constexpr RejectingTransition rejecting;
constexpr ToB toB;
constexpr SelfTransition selfT;
constexpr PingTransition ping;

constexpr fsm::row rows[] = {
    fsm::row::make_normal(A, B, EVT_TO_B, &rejecting), // A's guard always fails -> must fall through
    fsm::row::make_normal(ROOT, B, EVT_TO_B, &toB), // ancestor fallback, fires instead
    fsm::row::make_internal(A, EVT_PING, &ping),
    fsm::row::make_normal(B, B, EVT_SELF, &selfT), // self-transition
};

constexpr int parentTable[3] = {-1, ROOT, ROOT};
constexpr int initialStateTable[3] = {A, -1, -1}; // Root's default child is A

class ControllerTest : public ::testing::Test
{
protected:
    int pathBuf[4]{};
    int historyBuf[3]{};
    log_ctx ctx;
    fsm machine{ctx, parentTable, initialStateTable, historyBuf, pathBuf, states, rows, {}};
};

TEST_F(ControllerTest, InitializeEntersRootThenDefaultChild)
{
    machine.update(); // first tick: on_enter cascade
    EXPECT_EQ(ctx.events, (std::vector<std::string>{"enter:Root", "enter:A"}));
}

TEST_F(ControllerTest, InternalTransitionRunsOnlyAction)
{
    machine.update(); // first tick: init
    ctx.events.clear();

    machine.on_event(EVT_PING);
    machine.update(); // second tick: dispatches EVT_PING
    EXPECT_EQ(ctx.events, (std::vector<std::string>{"action:ping"}));
}

TEST_F(ControllerTest, RejectedGuardFallsThroughToAncestorRow)
{
    machine.update(); // first tick: init
    ctx.events.clear();

    machine.on_event(EVT_TO_B);
    machine.update(); // second tick: dispatches EVT_TO_B
    EXPECT_EQ(ctx.events, (std::vector<std::string>{"exit:A", "action:to-B", "enter:B"}));
}

TEST_F(ControllerTest, SelfTransitionForcesFullExitReEnter)
{
    machine.update(); // first tick: init (in A)
    machine.on_event(EVT_TO_B);
    machine.update(); // second tick: move to B
    ctx.events.clear();

    machine.on_event(EVT_SELF);
    machine.update(); // third tick: self-transition on B
    EXPECT_EQ(ctx.events, (std::vector<std::string>{"exit:B", "action:self", "enter:B"}));
}

TEST_F(ControllerTest, NoMatchingRowIsANoOp)
{
    machine.update(); // first tick: init (in A)
    machine.on_event(EVT_TO_B);
    machine.update(); // second tick: move to B
    ctx.events.clear();

    machine.on_event(EVT_PING); // B has no PING row, neither does Root
    machine.update(); // third tick: no match, no-op
    EXPECT_TRUE(ctx.events.empty());
}

TEST_F(ControllerTest, OnEventReturnsFalseWhenSlotOccupied)
{
    machine.update(); // first tick: init
    EXPECT_TRUE(machine.on_event(EVT_TO_B));
    EXPECT_FALSE(machine.on_event(EVT_PING)); // slot occupied
}
