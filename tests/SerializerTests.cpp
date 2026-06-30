#include <array>
#include <cstdint>
#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "hfsm.h"

// Exercises controller::save / restore / restore_cascade / layout_digest.
//
// Tree:
//   Root
//   ├── Idle                 (initial)
//   ├── Work                 (composite)
//   │   ├── Step1            (initial)
//   │   ├── Step2
//   │   └── Step3
//   └── Done
//
// The engine only hands the caller the raw position (current state + history); the test plays
// the role of the user's "serializer" by capturing those into a small struct and feeding them
// back through restore().

using namespace hfsm;

struct log_ctx
{
    std::vector<std::string> events;
};

enum class Events
{
    GO = 1,     // Idle  -> Work   (normal: descends to Step1)
    NEXT = 2,   // Step1 -> Step2 -> Step3
    HOME = 3,   // Work  -> Idle
    RESUME = 4, // Idle  -> Work   (shallow: re-enter last active child)
};

using M = Machine<log_ctx&, Events>;

struct RootState : M::state {};
struct Idle : M::state { void on_enter(log_ctx& c) const override { c.events.push_back("enter:Idle"); } };
struct Work : M::state { void on_enter(log_ctx& c) const override { c.events.push_back("enter:Work"); } };
struct Step1 : M::state { void on_enter(log_ctx& c) const override { c.events.push_back("enter:Step1"); } };
struct Step2 : M::state { void on_enter(log_ctx& c) const override { c.events.push_back("enter:Step2"); } };
struct Step3 : M::state { void on_enter(log_ctx& c) const override { c.events.push_back("enter:Step3"); } };
struct Done : M::state { void on_enter(log_ctx& c) const override { c.events.push_back("enter:Done"); } };

struct Noop : M::transition {};

using States = M::Root<Composite<RootState, Idle, Composite<Work, Step1, Step2, Step3>, Done>>;
using Transitions = M::Transitions<
    M::Transition<Idle, Work, Events::GO, Noop>,
    M::Transition<Step1, Step2, Events::NEXT, Noop>,
    M::Transition<Step2, Step3, Events::NEXT, Noop>,
    M::Transition<Work, Idle, Events::HOME, Noop>,
    M::Transition<Idle, Work, Events::RESUME, Noop, transition_kind::shallow>>;
using FSM = M::Tcontroller<States, Transitions>;

static_assert(States::Size == 7);
static_assert(States::Depth == 3);
static_assert(States::parentTable() == std::array<int, 7>{-1, 0, 0, 2, 2, 2, 0});
static_assert(States::initialStateTable() == std::array<int, 7>{1, -1, 3, -1, -1, -1, -1});

// A second, structurally different machine for layout-digest mismatch.
struct AltRoot : M::state {};
struct AltA : M::state {};
struct AltB : M::state {};
using AltStates = M::Root<Composite<AltRoot, AltA, AltB>>;
using AltTransitions = M::Transitions<>;
using AltFSM = M::Tcontroller<AltStates, AltTransitions>;

namespace
{
// The user's "serializer": whatever shape you like. Here it just buffers the values.
struct Snapshot
{
    int current = -1;
    std::vector<int> history;
    std::uint32_t layout = 0;

    void operator()(int cur, const int* hist, std::size_t n)
    {
        current = cur;
        history.assign(hist, hist + n);
    }
};

template <typename Machine>
Snapshot take(const Machine& m)
{
    Snapshot s;
    m.save(s);
    return s;
}
} // namespace

class SerializerTest : public ::testing::Test
{
protected:
    log_ctx ctx;
    FSM machine{ctx};

    // Drive to Work/Step2 and return the index of Step2.
    void drive_to_step2()
    {
        machine.update();             // enter Idle
        machine.on_event(Events::GO); // -> Work/Step1
        machine.update();
        machine.on_event(Events::NEXT); // -> Step2
        machine.update();
    }
};

TEST_F(SerializerTest, SilentRoundTripRestoresPositionWithoutCallbacks)
{
    drive_to_step2();
    const Snapshot snap = take(machine);

    log_ctx ctx2;
    FSM fresh{ctx2};
    ASSERT_TRUE(fresh.restore(snap.current, snap.history.data(), snap.history.size()));

    EXPECT_EQ(fresh.get_current_state(), States::index_of_v<Step2>);
    EXPECT_TRUE(fresh.is_current_s<Step2>());
    EXPECT_TRUE(fresh.is_active_s<Work>());
    EXPECT_TRUE(ctx2.events.empty()); // silent: no on_enter fired
}

TEST_F(SerializerTest, CascadeRestoreReplaysEnterRootToLeaf)
{
    drive_to_step2();
    const Snapshot snap = take(machine);

    log_ctx ctx2;
    FSM fresh{ctx2};
    ASSERT_TRUE(fresh.restore_cascade(snap.current, snap.history.data(), snap.history.size()));

    EXPECT_EQ(ctx2.events, (std::vector<std::string>{"enter:Work", "enter:Step2"}));
    EXPECT_TRUE(fresh.is_current_s<Step2>());
}

TEST_F(SerializerTest, RestoredHistoryDrivesShallowReentry)
{
    // Visit Work/Step2, then leave to Idle. Work's history now remembers Step2.
    drive_to_step2();
    machine.on_event(Events::HOME); // -> Idle (Work history retained)
    machine.update();
    const Snapshot snap = take(machine);
    ASSERT_EQ(snap.current, States::index_of_v<Idle>);

    // Restore into a fresh machine, then shallow-resume Work: must land on Step2, not Step1.
    log_ctx ctx2;
    FSM fresh{ctx2};
    ASSERT_TRUE(fresh.restore(snap.current, snap.history.data(), snap.history.size()));
    fresh.on_event(Events::RESUME);
    fresh.update();
    EXPECT_TRUE(fresh.is_current_s<Step2>());

    // Control: a machine that never visited Work falls back to the initial child Step1.
    log_ctx ctx3;
    FSM virgin{ctx3};
    virgin.update(); // enter Idle
    virgin.on_event(Events::RESUME);
    virgin.update();
    EXPECT_TRUE(virgin.is_current_s<Step1>());
}

TEST_F(SerializerTest, OutOfRangeOrWrongSizeIsRejectedAndMachineUntouched)
{
    drive_to_step2();
    const Snapshot snap = take(machine);
    const int before = machine.get_current_state();

    // Wrong element count.
    EXPECT_FALSE(machine.restore(snap.current, snap.history.data(), snap.history.size() + 1));
    // Out-of-range current state.
    EXPECT_FALSE(machine.restore(States::Size, snap.history.data(), snap.history.size()));
    // Out-of-range history entry.
    std::vector<int> bad = snap.history;
    bad[0] = States::Size + 5;
    EXPECT_FALSE(machine.restore(snap.current, bad.data(), bad.size()));

    EXPECT_EQ(machine.get_current_state(), before); // every failed restore left state intact
    EXPECT_TRUE(machine.is_current_s<Step2>());
}