# HFSM — C++ Hierarchical Finite State Machine

Header-only, no-allocation, tick-based HFSM for C++17/20. Designed as a foundation for
embedded control systems where allocation is prohibited, exceptions are off, and execution
order must be deterministic per clock tick.

**C++17+** &nbsp;|&nbsp; **Single header** &nbsp;|&nbsp; **MIT**

---

## Overview

- **Hierarchical**: states nest inside composite states; events bubble up the parent chain.
- **Header-only**: include hfsm.h in your project, no external dependencies needed.
- **No heap, no exceptions**: all tables are `constexpr`; storage lives in the machine object.
- **Tick-based**: update() drives execution — each call runs auto-transitions, then on_update, then the queued event, in that order. Dispatch never re-enters the engine.
- **Context by reference**: every callback receives `Context&`. States and transitions hold
  no data of their own.
- **Compile-time verification**: hierarchy correctness checked with `static_assert`; a typo'd
  state tag is a hard compile error.

---

## Quick Start

A minimal power-toggle machine — two states, one event, one transition type.

```cpp
#include "hfsm.h"
#include <iostream>

enum class Events{
  EVT_POWER
};

struct AppCtx { bool powered = false; };
using M = hfsm::Machine<AppCtx&, Events>;

struct RootState : M::state {};

struct Off : M::state {
    void on_enter(AppCtx& ctx) const override { ctx.powered = false; }
};

struct On : M::state {
    void on_enter(AppCtx& ctx) const override { ctx.powered = true; }
};

struct Toggle : M::transition {
    // bool on_guard(AppCtx& ctx) const override { return true; } // default guard
    void on_action(AppCtx& ctx) const override{
        std::cout << "Toggling power" << std::endl;
    } 
};   

using States = M::Root<hfsm::Composite<RootState, Off, On>>;  // Off is initial child
using Transitions = M::Transitions<
    M::Transition<Off, On,  Events::EVT_POWER, Toggle>,
    M::Transition<On,  Off, Events::EVT_POWER, Toggle>
>;
using FSM = M::Tcontroller<States, Transitions>;

int main() {
    AppCtx ctx;
    FSM machine{ctx};

    machine.update();               // first tick: enters RootState then Off
    // ctx.powered == false

    machine.on_event(Events::EVT_POWER);
    machine.update();               // exits Off, enters On
    // ctx.powered == true

    machine.on_event(Events::EVT_POWER);
    machine.update();               // exits On, enters Off
    // ctx.powered == false
}
```

### Steps

1. Define a `Context` struct — the shared data carrier for all callbacks.
2. Alias `using M = hfsm::Machine<Context&>;` (add `EventType` as a second arg for typed events).
3. Derive states from `M::state`; override `on_enter`, `on_exit`, `on_update` as needed.
4. Derive transitions from `M::transition`; override `on_guard` (return `false` to reject) and `on_action`.
5. Declare `M::Root<Composite<...>>` and `M::Transitions<...>`.
6. Instantiate `M::Tcontroller<States, Transitions>`, then call `update()` every tick.

---

## Concepts

### States and hierarchy

Every machine has at least one `Composite` at the top level. `Composite<Parent, Child1, Child2, ...>`
nests children under a parent state; the first child is the default initial state. Leaf states are plain
(non-composite) types. The hierarchy is arbitrarily deep.

```
Composite<RootState, Idle, Composite<Active, Listening, Transmitting>, Sleep>

  RootState
  ├── Idle        ← initial child of RootState
  ├── Active
  │   ├── Listening      ← initial child of Active
  │   └── Transmitting
  └── Sleep
```

`M::state` is the base; all three callbacks have default no-op implementations.

### Events and typed event IDs

The event type is the second template parameter: `Machine<Context, EventType = int>`. Any integral-like
type works — `int`, a plain `enum`, or an `enum class`. Using an enum makes the event table
self-documenting and prevents silent integer coercions.

```cpp
enum class Events { EVT_START = 1, EVT_STOP = 2 };
using M = hfsm::Machine<AppCtx&, Events>;

// EventId must be exactly Events — passing int here is a compile error:
M::Transition<Idle, Active, Events::EVT_START, StartTransition>
```

`Transition` and `InternalTransition` are **nested inside `Machine`**.

### Transition types

| Type | Effect |
|---|---|
| `M::Transition<From, To, Evt, T, [Kind]>` | External: full exit/enter cascade, UML semantics |
| `M::InternalTransition<State, Evt, T>` | Internal: calls `on_action` only, no exit/enter |
| `M::AutomaticTransition<From, To, T, [Kind]>` | Guard-driven; fires at the top of each `update()` tick |

### Guard and action

```cpp
struct MyTransition : M::transition {
    bool on_guard(AppCtx& ctx) const override { return ctx.ready; }  // return false to block
    void on_action(AppCtx& ctx) const override { ctx.ready = false; }
};
```

If the guard returns `false`, the engine skips the row and continues bubbling up. Multiple rows with
the same source state and event form a priority chain — first guard that passes wins.

State and transition types must be stateless and default-constructible. The engine holds a single
`constexpr` instance of each type — shared across all machines — so member variables would be
silently aliased between instances. All virtual methods are `const`; the context is the only valid
location for side effects.

### Event dispatch and bubbling

Unhandled events walk up the parent chain. A row registered on `RootState` acts as a catch-all
for any descendant that doesn't handle the event itself.

```
on_event(EVT_X) → check leaf → check leaf's parent → ... → check root → discard
```

### Tick-based execution model

```
update()
  1. evaluate auto-transitions (loop until none fires)
  2. call on_update on the active leaf state
  3. dispatch queued event (if any)
```

The first `update()` performs the initial entry cascade. There is no separate `initialize()`.

`on_event(id)` queues exactly one event. If a previous event hasn't been consumed yet, it returns
`false` — the caller decides whether to retry next tick or drop.

### History

`transition_kind` controls how a composite state is re-entered:

| Kind | Behaviour |
|---|---|
| `normal` (default) | Enter initial child chain |
| `shallow` | Re-enter the last direct child that was active; then enter its initial child chain |
| `deep` | Re-enter the exact leaf state that was last active in the subtree |

---

## Reference Example — LoRa Radio Controller

`tests/RadioControllerTests.cpp` is a realistic embedded reference: 16 states, 4 levels deep,
26 transition rows, 18 events. It exercises every dispatch mechanism in the engine.

```
Root
├── Idle                         (initial)
├── Active
│   ├── Listening                (initial)
│   ├── Configuring
│   │   ├── SetFrequency         (initial)
│   │   ├── SetPower
│   │   └── SetSpreadingFactor
│   └── Transmitting
│       ├── Preparing            (initial)
│       ├── Sending
│       └── AwaitingAck
├── Fault
│   ├── Recoverable              (initial)
│   └── Critical
└── Sleep
```

Highlights:
- Internal transitions at two different ancestor scopes (`Configuring` and `RootState`).
- Guarded retry chain: `AckTimeoutRetryTransition` passes until `retryCount == MAX_RETRIES`,
  then falls through to the parent `Transmitting → Idle` row.
- 3-level guard fallthrough on `EVT_EMERGENCY`: leaf → mid-ancestor → root catch-all.
- A permanently-blocked guard (`Critical → Recoverable` on `EVT_RESET`) verified by test.
- Shallow vs. deep history compared side-by-side from the same `Sleep` escape state.
- C++20 `StrLiteral` NTTP gives every state and transition type a unique compile-time name
  for event-log assertions without any runtime string storage.
- Compile-time table verification with `static_assert` against hand-authored baselines:

```cpp
static_assert(States::Size == 16);
static_assert(States::Depth == 4);
static_assert(States::parentTable() == std::array<int, 16>{-1, 0, 0, 2, 2, 4, 4, 4, 2, 8, 8, 8, 0, 12, 12, 0});
```

---

## Design Notes

### Tick vs. reactive dispatch

This engine doesn't implement reactive - synchronous event dispatching — `on_event()` queues one slot and `update()` drains it. The separation gives a
fixed, predictable execution order per clock tick, suits embedded targets with RTOS-tick or game-loop
architectures, and eliminates re-entrant dispatch entirely. There is no `dispatching` guard; the
engine is not called from inside a callback.

### Context by reference

States and transitions store nothing. Every callback receives `Context&` — the entire shared state
of the machine lives there. This makes unit-testing trivial (construct a context, observe it after
`update()`), enables the same transition type to be reused across different machines, and means
states are true singletons with no per-instance cost.

### `instancer<T>` — zero-cost state and transition singletons 

```cpp
template <typename T>
struct instancer { static constexpr T value{}; };
```

One `constexpr` instance per type. The address is stable at link time and serves as the canonical
identity for that state or transition in the table. No allocation, no indirection beyond the vtable
call that was already there.

### `constexpr` tables

`parentTable`, `initialStateTable`, `stateTable`, `eventRows`, and `autoRows` are all `static
constexpr` members of `Tcontroller`. They are evaluated entirely at compile time; the generated
machine object carries only mutable runtime state (current state, history table, pending event).
A `static_assert` fires if a state tag appears twice, a referenced tag isn't in the tree, or the
number of states overflows `int`.

### No RTTI, no `dynamic_cast`

Dispatch goes through the vtable only. The library compiles and runs correctly with `-fno-rtti`.

### C++17 compatibility

The library header requires C++17. Two C++20 constructs were removed to achieve this:
- Bit-field default member initializer (`unsigned internal : 1 = 0`) replaced with an explicit
  constructor.
- `std::array::fill` in constexpr context replaced with a range-for loop.

A `hfsm::span` polyfill is defined for compilers below C++20; in C++20+ it aliases `std::span`.
The test suite stays at C++20 intentionally — it uses class-type NTTPs (`StrLiteral`) that have
no C++17 equivalent.

---

## API Reference

| Entity | Purpose |
|---|---|
| `hfsm::Machine<Context, EventType = int>` | Top-level alias hub; sets event ID type for all transitions |
| `M::state` | Base for user states; override `on_enter`, `on_exit`, `on_update` |
| `M::transition` | Base for user transitions; override `on_guard`, `on_action` |
| `M::Transition<From, To, Evt, T, [Kind]>` | External transition; `Evt` must be `EventType` |
| `M::InternalTransition<State, Evt, T>` | Internal transition (action only); `Evt` must be `EventType` |
| `M::Root<Composite<...>, ...>` | Declares state hierarchy |
| `M::Transitions<...>` | Declares event and automatic transition tables |
| `M::Tcontroller<States, Transitions>` | Concrete machine type to instantiate |
| `hfsm::Composite<State, Children...>` | Nests states; first child = default initial; namespace scope |
| `M::AutomaticTransition<From, To, T, [Kind]>` | Guard-driven automatic transition; no event ID |
| `hfsm::transition_kind` | `normal` / `shallow` / `deep` |
| `Tcontroller::update()` | One execution tick |
| `Tcontroller::on_event(EventType)` | Queue next event; returns `false` if slot busy |
| `Tcontroller::is_active_s<T>()` | `true` if state type `T` is currently active (includes ancestors) |
| `Tcontroller::is_current_s<T>()` | `true` if `T` is the active leaf state |
| `Tcontroller::get_current_state()` | Returns the active leaf state's integer ID |
| `Tcontroller::set_on_init(fn)` | Optional hook called once on the initial state before the first `update()` entry cascade; signature `void(int state)` |
| `Tcontroller::set_on_transition(fn)` | Optional hook called on every fired transition; signature `void(int from, int to)` |

---

## Building

The library is header-only. For the tests:

```sh
cmake -B build
cmake --build build
ctest --test-dir build
```

Tests require C++20. The library itself requires only C++17.

---

## License

MIT — see [LICENSE](LICENSE).