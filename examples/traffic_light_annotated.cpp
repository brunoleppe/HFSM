// Traffic light cycling Green → Yellow → Red → Green.
//
// Demonstrates the S()/T() forward-declaration macros, which let Root<> and
// Transitions<> appear before the struct definitions. This is useful when the
// wiring declarations benefit from being grouped at the top of the file and
// the state/transition implementations below.

#include "hfsm.h"
#include <iostream>
#include "tools/hfsm_generator.h"
#include "tools/hfsm_annotations.h"
using namespace hfsm;

#define S(name) struct name
#define T(name) struct name

enum class Signal { TICK };
using M = Machine<int&, Signal>;

S(RootState); S(Green); S(Yellow); S(Red);
T(Advance);

using States = M::Root<Composite<S(RootState), S(Green), S(Yellow), S(Red)>>;
using Transitions = M::Transitions<
    M::Transition<Green,  Yellow, Signal::TICK, T(AdvanceToYellow)>,
    M::Transition<Yellow, Red,    Signal::TICK, T(AdvanceToRed)>,
    M::Transition<Red,    Green,  Signal::TICK, T(AdvanceToGreen)>
>;
using FSM = M::Tcontroller<States, Transitions>;

struct RootState : M::state {};
struct Green  : M::state { void on_enter(int&) const override { std::cout << "Green\n";  } };
struct Yellow : M::state { void on_enter(int&) const override { std::cout << "Yellow\n"; } };
struct Red    : M::state { void on_enter(int&) const override { std::cout << "Red\n";    } };
struct AdvanceToRed : M::transition
{
    HFSM_ACTION("ActionToRed")
    void on_action(int& ctx_) const override;
    HFSM_GUARD("ToRedGuard")
    bool on_guard(int& ctx_) const override;
};
struct AdvanceToGreen : M::transition
{
    HFSM_ACTION("ActionToGreen")
    void on_action(int& ctx_) const override;
    HFSM_GUARD("ToGreenGuard")
    bool on_guard(int& ctx_) const override;
};
struct AdvanceToYellow : M::transition
{
    HFSM_ACTION("ActionToYellow")
    void on_action(int& ctx_) const override;
    HFSM_GUARD("ToYellowGuard")
    bool on_guard(int& ctx_) const override;
};

int main()
{
    std::cout << hfsm::diagram::to_plantuml<States, Transitions>();
    return 0;
}