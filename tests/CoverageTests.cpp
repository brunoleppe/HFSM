#include <array>
#include <gtest/gtest.h>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "hfsm.h"

// Verifies enter/exit behavior across the whole hierarchy, two ways:
//   1. Coverage    — a scripted path visits every state; assert all are entered, and all but
//                    the final active path are exited.
//   2. Balance     — over a looping event sequence, every exit is matched by a prior enter and
//                    no state is ever entered twice without an exit (a state's live count stays
//                    in {0, 1}); at the end only the active path is "entered".
//
// Tree:
//   Root
//   ├── A           (initial)        ├── B                    └── C
//   │   ├── A1 (init)                │   ├── B1 (init)
//   │   └── A2                       │   └── B2

using namespace hfsm;

struct Ctx
{
    std::vector<std::string> events;
};

enum class Ev
{
    A1_A2 = 1, // A1 -> A2
    A2_B = 2,  // A2 -> B   (cross-composite; descends to B1)
    B1_B2 = 3, // B1 -> B2
    B2_C = 4,  // B2 -> C
    C_A = 5,   // C  -> A   (descends to A1)
};

using M = Machine<Ctx&, Ev>;

#define STATE(name, label)                                                                                              \
    struct name : M::state                                                                                             \
    {                                                                                                                  \
        void on_enter(Ctx& c) const override { c.events.push_back("enter:" label); }                                  \
        void on_exit(Ctx& c) const override { c.events.push_back("exit:" label); }                                    \
    }

STATE(RootState, "Root");
STATE(A, "A");
STATE(A1, "A1");
STATE(A2, "A2");
STATE(B, "B");
STATE(B1, "B1");
STATE(B2, "B2");
STATE(C, "C");
#undef STATE

struct Noop : M::transition {};

using States =
    M::Root<Composite<RootState, Composite<A, A1, A2>, Composite<B, B1, B2>, C>>;
using Transitions = M::Transitions<
    M::Transition<A1, A2, Ev::A1_A2, Noop>,
    M::Transition<A2, B, Ev::A2_B, Noop>,
    M::Transition<B1, B2, Ev::B1_B2, Noop>,
    M::Transition<B2, C, Ev::B2_C, Noop>,
    M::Transition<C, A, Ev::C_A, Noop>>;
using FSM = M::Tcontroller<States, Transitions>;

static_assert(States::Size == 8);
static_assert(States::Depth == 3);
static_assert(States::parentTable() == std::array<int, 8>{-1, 0, 1, 1, 0, 4, 4, 0});
static_assert(States::initialStateTable() == std::array<int, 8>{1, 2, -1, -1, 5, -1, -1, -1});

namespace
{
const std::set<std::string> kAllStates{"Root", "A", "A1", "A2", "B", "B1", "B2", "C"};

std::set<std::string> names_with_prefix(const std::vector<std::string>& log, const std::string& prefix)
{
    std::set<std::string> out;
    for (const auto& e : log)
        if (e.rfind(prefix, 0) == 0)
            out.insert(e.substr(prefix.size()));
    return out;
}

void step(FSM& m, Ev e)
{
    m.on_event(e);
    m.update();
}
} // namespace

class CoverageTest : public ::testing::Test
{
protected:
    Ctx ctx;
    FSM machine{ctx};
};

TEST_F(CoverageTest, EveryStateIsEnteredAndExited)
{
    machine.update();        // enter Root, A, A1
    step(machine, Ev::A1_A2); // A1 -> A2
    step(machine, Ev::A2_B);  // A2 -> B/B1
    step(machine, Ev::B1_B2); // B1 -> B2
    step(machine, Ev::B2_C);  // B2 -> C  (final active path: Root, C)

    const auto entered = names_with_prefix(ctx.events, "enter:");
    const auto exited = names_with_prefix(ctx.events, "exit:");

    EXPECT_EQ(entered, kAllStates); // every state was entered

    std::set<std::string> expectedExited = kAllStates;
    expectedExited.erase("Root"); // top state is never exited
    expectedExited.erase("C");    // C is the final active leaf
    EXPECT_EQ(exited, expectedExited);
}

TEST_F(CoverageTest, EnterExitStayBalancedAcrossLoops)
{
    machine.update(); // enter Root, A, A1

    // Loop the full A1 -> A2 -> B -> B2 -> C -> A1 cycle several times.
    for (int i = 0; i < 3; ++i)
    {
        step(machine, Ev::A1_A2);
        step(machine, Ev::A2_B);
        step(machine, Ev::B1_B2);
        step(machine, Ev::B2_C);
        step(machine, Ev::C_A); // back to A/A1
    }

    // Replay the log, enforcing the invariant: balance never negative (no exit without a prior
    // enter) and never above 1 (no re-entry without an exit).
    std::map<std::string, int> balance;
    for (const auto& e : ctx.events)
    {
        const bool isEnter = e.rfind("enter:", 0) == 0;
        const std::string name = e.substr(isEnter ? 6 : 5);
        if (isEnter)
        {
            ++balance[name];
            EXPECT_LE(balance[name], 1) << "state entered twice without exit: " << name;
        }
        else
        {
            --balance[name];
            EXPECT_GE(balance[name], 0) << "state exited without a matching enter: " << name;
        }
    }

    // At rest only the active path (Root -> A -> A1) is still "entered".
    EXPECT_EQ(balance["Root"], 1);
    EXPECT_EQ(balance["A"], 1);
    EXPECT_EQ(balance["A1"], 1);
    for (const std::string& s : {"A2", "B", "B1", "B2", "C"})
        EXPECT_EQ(balance[s], 0) << s;
}