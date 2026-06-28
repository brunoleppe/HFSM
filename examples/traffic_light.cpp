// Traffic light cycling Green → Yellow → Red → Green.
//
// Demonstrates the S()/T() forward-declaration macros, which let Root<> and
// Transitions<> appear before the struct definitions. This is useful when the
// wiring declarations benefit from being grouped at the top of the file and
// the state/transition implementations below.

#include "hfsm.h"
#include <iostream>

using namespace hfsm;

#define S(name) struct name
#define T(name) struct name

enum class Signal { TICK };
using M = Machine<int&, Signal>;

S(RootState); S(Green); S(Yellow); S(Red);
T(Advance);

using States = M::Root<Composite<S(RootState), S(Green), S(Yellow), S(Red)>>;
using Transitions = M::Transitions<
    M::Transition<Green,  Yellow, Signal::TICK, T(Advance)>,
    M::Transition<Yellow, Red,    Signal::TICK, T(Advance)>,
    M::Transition<Red,    Green,  Signal::TICK, T(Advance)>
>;
using FSM = M::Tcontroller<States, Transitions>;

struct RootState : M::state {};
struct Green  : M::state { void on_enter(int&) const override { std::cout << "Green\n";  } };
struct Yellow : M::state { void on_enter(int&) const override { std::cout << "Yellow\n"; } };
struct Red    : M::state { void on_enter(int&) const override { std::cout << "Red\n";    } };
struct Advance : M::transition {};

int main()
{
    int ctx = 0;
    FSM machine{ctx};

    machine.update();                      // enters Green
    for (int i = 0; i < 6; ++i)
    {
        machine.on_event(Signal::TICK);
        machine.update();                  // Yellow, Red, Green, Yellow, Red, Green
    }
}