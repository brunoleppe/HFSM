#include <array>
#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "hfsm.h"

// Same fixture as ControllerTests.cpp -- Root(0) -> A(1) [default child], Root(0) -> B(2) --
// but authored declaratively and wired by M::Tcontroller instead of by hand. The point is
// to diff the generated parentTable/initialStateTable/states[]/rows[] against the known-good
// hand-authored baseline, end-to-end through controller::on_event.
//
// Composite / AutomaticTransition are at namespace scope; M::Transition and M::InternalTransition
// are nested inside Machine so the EventType NTTP is strongly typed.

struct log_ctx
{
    std::vector<std::string> events;
};
using namespace hfsm;
using M = Machine<log_ctx&>;

struct RootState : M::state
{
    void on_enter(log_ctx& ctx) const override { ctx.events.push_back("enter:Root"); }
    void on_exit(log_ctx& ctx) const override { ctx.events.push_back("exit:Root"); }
};

struct AState : M::state
{
    void on_enter(log_ctx& ctx) const override { ctx.events.push_back("enter:A"); }
    void on_exit(log_ctx& ctx) const override { ctx.events.push_back("exit:A"); }
};

struct BState : M::state
{
    void on_enter(log_ctx& ctx) const override { ctx.events.push_back("enter:B"); }
    void on_exit(log_ctx& ctx) const override { ctx.events.push_back("exit:B"); }
};

struct RejectingTransition : M::transition
{
    bool on_guard(log_ctx& ctx) const override { return false; }
    void on_action(log_ctx& ctx) const override { ctx.events.push_back("action:should-not-fire"); }
};

struct ToB : M::transition
{
    void on_action(log_ctx& ctx) const override { ctx.events.push_back("action:to-B"); }
};

struct SelfTransition : M::transition
{
    void on_action(log_ctx& ctx) const override { ctx.events.push_back("action:self"); }
};

struct PingTransition : M::transition
{
    void on_action(log_ctx& ctx) const override { ctx.events.push_back("action:ping"); }
};

constexpr int EVT_TO_B = 1, EVT_PING = 2, EVT_SELF = 3;

using States = M::Root<Composite<RootState, AState, BState> // RootState's default child is AState (the First arg)
                       >;

using Transitions =
    M::Transitions<M::Transition<AState, BState, EVT_TO_B, RejectingTransition>, // A's guard always fails -> falls through
                   M::Transition<RootState, BState, EVT_TO_B, ToB>, // ancestor fallback, fires instead
                   M::InternalTransition<AState, EVT_PING, PingTransition>,
                   M::Transition<BState, BState, EVT_SELF, SelfTransition> // self-transition
                   >;

using FSM = M::Tcontroller<States, Transitions>;

// The core promise of the design: the generated tables match the hand-authored baseline
// (ControllerTests.cpp) exactly, verified at compile time.
static_assert(States::Size == 3);
static_assert(States::Depth == 2);
static_assert(States::parentTable() == std::array<int, 3>{-1, 0, 0});
static_assert(States::initialStateTable() == std::array<int, 3>{1, -1, -1});

class DeclarativeHfsmTest : public ::testing::Test
{
protected:
    log_ctx ctx;
    FSM machine{ctx};
};

TEST_F(DeclarativeHfsmTest, InitializeEntersRootThenDefaultChild)
{
    machine.update();
    EXPECT_EQ(ctx.events, (std::vector<std::string>{"enter:Root", "enter:A"}));
}

TEST_F(DeclarativeHfsmTest, InternalTransitionRunsOnlyAction)
{
    machine.update();
    ctx.events.clear();

    machine.on_event(EVT_PING);
    machine.update();
    EXPECT_EQ(ctx.events, (std::vector<std::string>{"action:ping"}));
}

TEST_F(DeclarativeHfsmTest, RejectedGuardFallsThroughToAncestorRow)
{
    machine.update();
    ctx.events.clear();

    machine.on_event(EVT_TO_B);
    machine.update();
    EXPECT_EQ(ctx.events, (std::vector<std::string>{"exit:A", "action:to-B", "enter:B"}));
}

TEST_F(DeclarativeHfsmTest, SelfTransitionForcesFullExitReEnter)
{
    machine.update();
    machine.on_event(EVT_TO_B); // move to B first
    machine.update();
    ctx.events.clear();

    machine.on_event(EVT_SELF);
    machine.update();
    EXPECT_EQ(ctx.events, (std::vector<std::string>{"exit:B", "action:self", "enter:B"}));
}

TEST_F(DeclarativeHfsmTest, NoMatchingRowIsANoOp)
{
    machine.update();
    machine.on_event(EVT_TO_B); // move to B
    machine.update();
    ctx.events.clear();

    machine.on_event(EVT_PING); // B has no PING row, neither does Root
    machine.update();
    EXPECT_TRUE(ctx.events.empty());
}
