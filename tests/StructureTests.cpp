#include <array>
#include <gtest/gtest.h>
#include "hfsm.h"
#include "tools/hfsm_structure.h"

// ============================================================
// Compile-time unit tests for the detail helpers.
// Use plain row / auto_row values — no machine wiring needed.
// ============================================================

struct Ctx {};
using ctrl     = hfsm::controller<Ctx>;
using row      = ctrl::row;
using auto_row = ctrl::auto_row;

struct Noop : ctrl::transition {};
constexpr Noop noop{};

// --- no_duplicate_triggers ---

static_assert(hfsm::structure::detail::no_duplicate_triggers(std::array<row, 0>{}));

static_assert(hfsm::structure::detail::no_duplicate_triggers(std::array{
    row::make_normal(0, 1, 1, &noop),
    row::make_normal(0, 1, 2, &noop), // same from, different eventId
}));

static_assert(!hfsm::structure::detail::no_duplicate_triggers(std::array{
    row::make_normal(0, 1, 1, &noop),
    row::make_normal(0, 2, 1, &noop), // same from + eventId, different to
}));

static_assert(!hfsm::structure::detail::no_duplicate_triggers(std::array{
    row::make_normal(0, 1, 5, &noop),
    row::make_normal(1, 2, 7, &noop),
    row::make_normal(0, 2, 5, &noop), // duplicates row[0]
}));

// --- no_auto_self_loops ---

static_assert(hfsm::structure::detail::no_auto_self_loops(std::array<auto_row, 0>{}));

static_assert(hfsm::structure::detail::no_auto_self_loops(std::array{
    auto_row::make(0, 1, &noop),
}));

static_assert(!hfsm::structure::detail::no_auto_self_loops(std::array{
    auto_row::make(0, 0, &noop), // from == to
}));

static_assert(!hfsm::structure::detail::no_auto_self_loops(std::array{
    auto_row::make(0, 1, &noop),
    auto_row::make(2, 2, &noop), // self-loop anywhere in the table is caught
}));

// ============================================================
// Integration: validate<FSM>() on well-formed machines.
// ============================================================

using M = hfsm::Machine<Ctx>;

struct Root  : M::state {};
struct A     : M::state {};
struct B     : M::state {};
struct C     : M::state {};
struct D     : M::state {};
struct EvT   : M::transition {};
struct AutoT : M::transition {};

// Event-driven only
using EventStates       = M::Root<hfsm::Composite<Root, A, B>>;
using EventTransitions  = M::Transitions<
    M::Transition<A, B, 1, EvT>,
    M::Transition<B, A, 2, EvT>
>;
using EventOnlyFSM = M::Tcontroller<EventStates, EventTransitions>;

// Mixed: event + automatic (auto goes C→D, not a self-loop)
using MixedStates      = M::Root<hfsm::Composite<Root, A, hfsm::Composite<B, C, D>>>;
using MixedTransitions = M::Transitions<
    M::Transition<A, B, 1, EvT>,
    M::AutomaticTransition<C, D, AutoT>
>;
using MixedFSM = M::Tcontroller<MixedStates, MixedTransitions>;

TEST(StructureValidate, EventOnlyMachinePasses)
{
    hfsm::structure::validate<EventOnlyFSM>();
    SUCCEED();
}

TEST(StructureValidate, MixedMachinePasses)
{
    hfsm::structure::validate<MixedFSM>();
    SUCCEED();
}

// ============================================================
// Negative cases — uncomment to verify compiler error messages:
//
// Duplicate (from, eventId):
//   using DupTransitions = M::Transitions<
//       M::Transition<A, B, 1, EvT>,
//       M::Transition<A, B, 1, EvT>
//   >;
//   // error: "duplicate (from, eventId) pair in transition table: second transition is unreachable"
//   hfsm::structure::validate<M::Tcontroller<EventStates, DupTransitions>>();
//
// Automatic self-loop:
//   using LoopTransitions = M::Transitions<M::AutomaticTransition<A, A, AutoT>>;
//   // error: "automatic transition self-loop detected: from == to causes an infinite loop"
//   hfsm::structure::validate<M::Tcontroller<EventStates, LoopTransitions>>();
// ============================================================