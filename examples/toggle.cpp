// Minimal two-state toggle machine: Off ↔ On.
// Demonstrates the full declarative API end-to-end — enum class event type,
// M::Root, M::Transitions, M::Tcontroller, update() / on_event().

#include "hfsm.h"
#include <iostream>

enum class Events { POWER };

struct AppCtx { bool powered = false; };
using M = hfsm::Machine<AppCtx&, Events>;

struct RootState : M::state {};

struct Off : M::state
{
    void on_enter(AppCtx& ctx) const override
    {
        ctx.powered = false;
        std::cout << "Off\n";
    }
};

struct On : M::state
{
    void on_enter(AppCtx& ctx) const override
    {
        ctx.powered = true;
        std::cout << "On\n";
    }
};

struct Toggle : M::transition
{
    void on_action(AppCtx&) const override { std::cout << "toggle\n"; }
};

using States = M::Root<hfsm::Composite<RootState, Off, On>>;
using Transitions = M::Transitions<
    M::Transition<Off, On,  Events::POWER, Toggle>,
    M::Transition<On,  Off, Events::POWER, Toggle>
>;
using FSM = M::Tcontroller<States, Transitions>;

int main()
{
    AppCtx ctx;
    FSM machine{ctx};

    machine.update();                      // enters Off; ctx.powered == false
    machine.on_event(Events::POWER);
    machine.update();                      // enters On;  ctx.powered == true
    machine.on_event(Events::POWER);
    machine.update();                      // enters Off; ctx.powered == false
}