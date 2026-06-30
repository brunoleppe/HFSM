//   _   _  _____  ____   __  __
//  | | | ||  ___|/ ___| |  \/  |
//  | |_| || |_   \___ \ | |\/| |
//  |  _  ||  _|   ___) || |  | |
//  |_| |_||_|    |____/ |_|  |_|
//
//  HFSM — optional transition action/guard labels for diagram export.
//  https://github.com/brunoleppe/HFSM
//
//  SPDX-License-Identifier: MIT
//  Copyright (c) 2026 Bruno Leppe
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in all
//  copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//  SOFTWARE.

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

#include "hfsm.h"

#include <array>
#include <string_view>

#include "third_party/nameof/nameof.hpp"

#define HFSM_ACTION(name) static constexpr std::string_view action_label = name;
#define HFSM_GUARD(name) static constexpr std::string_view guard_label = name;

namespace hfsm::annotations
{
    namespace detail
    {
        template <typename... Tags>
        constexpr std::array<const char*, sizeof...(Tags)> names_of(utils::list<Tags...>)
        {
            return {nameof::nameof_type<Tags>().data()...};
        }
    } // namespace detail

    // Array of state names (the state type names, via nameof), indexed by the same state
    // index the controller passes to on_init / on_transition. Each entry is a NUL-terminated
    // string with static lifetime, so it can be used directly in those plain callbacks:
    //
    //   static constexpr auto names = hfsm::annotations::get_names<States>();
    //   void log(int from, int to) { std::printf("%s -> %s\n", names[from], names[to]); }
    //   machine.set_on_transition(&log);
    template <typename StateTable>
    constexpr std::array<const char*, StateTable::Size> get_names()
    {
        return detail::names_of(typename StateTable::Tags{});
    }
} // namespace hfsm::annotations

#endif // HFSM_ANNOTATIONS_H