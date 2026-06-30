#ifndef HFSM_ANNOTATIONS_H
#define HFSM_ANNOTATIONS_H

// Optional, opt-in human-readable labels for transition actions/guards, used by
// the diagram generator. Include this header only in files that define the FSM
// and want named edges; the core (hfsm.h) and the generator stay independent of
// it. Place a macro directly above the overridden method inside a transition
// struct — it expands to a sibling constexpr member the generator detects:
//
//   #include "tools/hfsm_annotations.h"
//
//   struct Advance : M::transition {
//       HFSM_ACTION("Move Forward")
//       void on_action(int&) const override {}
//
//       HFSM_GUARD("Is Ready")
//       bool on_guard(int&) const override { return true; }
//   };
//
// Unannotated transitions are unaffected; the generator falls back to its
// generic [Guard]/Action markers for them.

#include <string_view>

#define HFSM_ACTION(name) static constexpr std::string_view action_label = name;
#define HFSM_GUARD(name) static constexpr std::string_view guard_label = name;

#endif // HFSM_ANNOTATIONS_H