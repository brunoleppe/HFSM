// Device power management: Active (Running / Saving) ↔ Sleep.
//
// DeviceRoot
// ├── Active (composite, initial)
// │   ├── Running (leaf, initial) ──[auto, guard: savePower]──> Saving
// │   └── Saving  (leaf)
// └── Sleep (leaf)
//
// Demonstrates:
//   - two-level composite hierarchy
//   - automatic guard-driven transition (Running → Saving)
//   - internal transition (POWER_SAVE sets the flag without changing state)
//   - shallow history (WAKE restores Active's last direct child)

#include "hfsm.h"
#include <iostream>
#include <string>
#include <vector>

enum class Cmd { SLEEP, WAKE, WORK, POWER_SAVE };

struct DeviceCtx
{
    bool savePower = false;
    std::vector<std::string> log;
};

using M = hfsm::Machine<DeviceCtx&, Cmd>;

// ── states ────────────────────────────────────────────────────────────────

struct DeviceRoot : M::state
{
    void on_enter(DeviceCtx& ctx) const override { ctx.log.push_back("enter:Root");   }
    void on_exit(DeviceCtx& ctx)  const override { ctx.log.push_back("exit:Root");    }
};

struct Active : M::state
{
    void on_enter(DeviceCtx& ctx) const override { ctx.log.push_back("enter:Active"); }
    void on_exit(DeviceCtx& ctx)  const override { ctx.log.push_back("exit:Active");  }
};

struct Running : M::state
{
    void on_enter(DeviceCtx& ctx) const override { ctx.log.push_back("enter:Running"); }
    void on_exit(DeviceCtx& ctx)  const override { ctx.log.push_back("exit:Running");  }
};

struct Saving : M::state
{
    void on_enter(DeviceCtx& ctx) const override { ctx.log.push_back("enter:Saving"); }
    void on_exit(DeviceCtx& ctx)  const override { ctx.log.push_back("exit:Saving");  }
};

struct Sleep : M::state
{
    void on_enter(DeviceCtx& ctx) const override { ctx.log.push_back("enter:Sleep"); }
    void on_exit(DeviceCtx& ctx)  const override { ctx.log.push_back("exit:Sleep");  }
};

// ── transitions ───────────────────────────────────────────────────────────

struct SetPowerSave : M::transition
{
    void on_action(DeviceCtx& ctx) const override
    {
        ctx.savePower = true;
        ctx.log.push_back("action:power-save");
    }
};

struct EnterSaving : M::transition
{
    bool on_guard(DeviceCtx& ctx) const override { return ctx.savePower; }
    void on_action(DeviceCtx& ctx) const override { ctx.log.push_back("action:enter-saving"); }
};

struct SetFullPower : M::transition
{
    void on_action(DeviceCtx& ctx) const override
    {
        ctx.savePower = false;
        ctx.log.push_back("action:full-power");
    }
};

struct GoSleep  : M::transition { void on_action(DeviceCtx& ctx) const override { ctx.log.push_back("action:sleep"); } };
struct GoActive : M::transition { void on_action(DeviceCtx& ctx) const override { ctx.log.push_back("action:wake");  } };

// ── wiring ────────────────────────────────────────────────────────────────

using States = M::Root<hfsm::Composite<DeviceRoot,
                           hfsm::Composite<Active, Running, Saving>,
                           Sleep>>;

using Transitions = M::Transitions<
    M::InternalTransition<DeviceRoot, Cmd::POWER_SAVE, SetPowerSave>,
    M::Transition<Saving,  Running, Cmd::WORK,  SetFullPower>,
    M::Transition<Active,  Sleep,   Cmd::SLEEP, GoSleep>,
    M::Transition<Sleep,   Active,  Cmd::WAKE,  GoActive, hfsm::transition_kind::shallow>,
    M::AutomaticTransition<Running, Saving, EnterSaving>
>;

using FSM = M::Tcontroller<States, Transitions>;

// ── demo ──────────────────────────────────────────────────────────────────

static void print_and_clear(DeviceCtx& ctx)
{
    for (const auto& e : ctx.log)
        std::cout << "  " << e << "\n";
    ctx.log.clear();
}

int main()
{
    DeviceCtx ctx;
    FSM machine{ctx};

    // Tick 1: initial entry cascade; auto-transition guard is false (savePower=false).
    std::cout << "-- init --\n";
    machine.update();
    print_and_clear(ctx);

    // Tick 2: POWER_SAVE is an internal transition — sets savePower=true, no state change.
    //         Auto-transitions run before the event this tick, so the flag isn't set yet when
    //         they are evaluated; the Running→Saving auto-transition fires on the next tick.
    std::cout << "-- POWER_SAVE (internal, no state change) --\n";
    machine.on_event(Cmd::POWER_SAVE);
    machine.update();
    print_and_clear(ctx);

    // Tick 3: No event queued; auto-transition evaluates first — savePower=true → Running→Saving.
    std::cout << "-- auto-transition: Running→Saving --\n";
    machine.update();
    print_and_clear(ctx);

    // Tick 4: SLEEP — event bubbles from Saving up to Active, which owns the Sleep row.
    std::cout << "-- SLEEP --\n";
    machine.on_event(Cmd::SLEEP);
    machine.update();
    print_and_clear(ctx);

    // Tick 5: WAKE with shallow history — Active's last direct child was Saving, so it restores it.
    std::cout << "-- WAKE (shallow history: restores Saving) --\n";
    machine.on_event(Cmd::WAKE);
    machine.update();
    print_and_clear(ctx);

    // Tick 6: WORK — Saving→Running, clears savePower so the auto-transition won't fire again.
    std::cout << "-- WORK --\n";
    machine.on_event(Cmd::WORK);
    machine.update();
    print_and_clear(ctx);
}