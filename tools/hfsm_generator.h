//   _   _  _____  ____   __  __
//  | | | ||  ___|/ ___| |  \/  |
//  | |_| || |_   \___ \ | |\/| |
//  |  _  ||  _|   ___) || |  | |
//  |_| |_||_|    |____/ |_|  |_|
//
//  HFSM — PlantUML diagram generator (dev-time tooling).
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

#ifndef HFSM_GENERATOR_H
#define HFSM_GENERATOR_H

// Optional, dev-time-only PlantUML exporter for an HFSM definition.
//
// It is driven entirely by the compile-time metadata that the machine already
// carries: the state hierarchy from a StateTable (Machine::Root) and the
// transition list from a TransitionTable (Machine::Transitions). State names come
// from hfsm_annotations (nameof); event-enum names from magic_enum.

#include "hfsm.h"
#include "hfsm_annotations.h"

#include <array>
#include <functional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "third_party/magic_enum/magic_enum.hpp"

namespace hfsm::diagram
{
    namespace detail
    {
        template <typename Meta>
        std::string event_name()
        {
            if constexpr (Meta::has_event)
            {
                using ET = std::decay_t<decltype(Meta::eventId)>;
                if constexpr (std::is_enum_v<ET>)
                    return std::string{magic_enum::enum_name(Meta::eventId)};
                else
                    return std::to_string(static_cast<long long>(Meta::eventId));
            }
            else
            {
                return {};
            }
        }

        // The declaring class of a const member function. When a transition type T inherits
        // on_guard/on_action unchanged, &T::member resolves to the base `transition` class;
        // when T overrides it, the pointer-to-member is qualified by T itself.
        template <typename C, typename R, typename... A>
        C member_class_of(R (C::*)(A...) const);

        template <typename TT>
        constexpr bool has_guard()
        {
            return std::is_same_v<TT, decltype(member_class_of(&TT::on_guard))>;
        }

        template <typename TT>
        constexpr bool has_action()
        {
            return std::is_same_v<TT, decltype(member_class_of(&TT::on_action))>;
        }

        // Optional labels from hfsm_annotations.h (or any sibling constexpr member of the
        // same name). Detected with SFINAE so the generator never depends on the annotations.
        template <typename T, typename = void>
        struct has_action_label : std::false_type
        {
        };
        template <typename T>
        struct has_action_label<T, std::void_t<decltype(T::action_label)>> : std::true_type
        {
        };

        template <typename T, typename = void>
        struct has_guard_label : std::false_type
        {
        };
        template <typename T>
        struct has_guard_label<T, std::void_t<decltype(T::guard_label)>> : std::true_type
        {
        };

        template <typename TT>
        std::string action_text()
        {
            if constexpr (has_action_label<TT>::value)
                return std::string{std::string_view{TT::action_label}};
            else
                return "Action";
        }

        template <typename TT>
        std::string guard_text()
        {
            if constexpr (has_guard_label<TT>::value)
                return std::string{std::string_view{TT::guard_label}};
            else
                return "Guard";
        }

        struct edge
        {
            int from = hierarchy::INVALID;
            int to = hierarchy::INVALID;
            std::string label;
            transition_kind kind = transition_kind::normal;
            bool internal = false;
            bool automatic = false;
        };

        template <typename StateTable, typename Meta>
        edge make_edge()
        {
            edge e;
            e.from = StateTable::template index_of_v<typename Meta::FromState>;
            e.to = StateTable::template index_of_v<typename Meta::ToState>;
            e.kind = Meta::kind;
            e.internal = Meta::internal;
            e.automatic = Meta::automatic;

            // UML label: event [guard] / action — each segment only when present.
            using TT = typename Meta::transition_type;
            std::string label = event_name<Meta>();
            if (has_guard<TT>())
            {
                if (!label.empty())
                    label += " ";
                label += "[" + guard_text<TT>() + "]";
            }
            if (has_action<TT>())
                label += "/" + action_text<TT>();
            e.label = label;
            return e;
        }

        template <typename StateTable, typename... Metas>
        std::vector<edge> collect_edges(utils::list<Metas...>)
        {
            std::vector<edge> edges;
            edges.reserve(sizeof...(Metas));
            (edges.push_back(make_edge<StateTable, Metas>()), ...);
            return edges;
        }

        inline std::string history_suffix(transition_kind kind)
        {
            switch (kind)
            {
            case transition_kind::shallow:
                return "[H]";
            case transition_kind::deep:
                return "[H*]";
            default:
                return {};
            }
        }
    } // namespace detail

    // Emits a PlantUML state diagram for the machine described by StateTable + TransitionTable.
    template <typename StateTable, typename TransitionTable>
    std::string to_plantuml(std::string_view title = {})
    {
        constexpr int size = StateTable::Size;
        const auto names = ::hfsm::annotations::get_names<StateTable>();
        constexpr auto parent = StateTable::parentTable();
        constexpr auto initial = StateTable::initialStateTable();

        // children[i] = states whose parent is i, in declaration order.
        std::vector<std::vector<int>> children(size);
        std::vector<int> roots;
        for (int i = 0; i < size; ++i)
        {
            if (parent[i] == hierarchy::TOP_MOST_PARENT)
                roots.push_back(i);
            else
                children[parent[i]].push_back(i);
        }

        std::ostringstream out;
        out << "@startuml\n";
        if (!title.empty())
            out << "title " << title << "\n";

        // index 0 is the controller's starting state.
        if (size > 0)
            out << "[*] --> " << names[0] << "\n";

        std::function<void(int, const std::string&)> emit_state = [&](int i, const std::string& indent)
        {
            const bool composite = initial[i] != hierarchy::INVALID || !children[i].empty();
            if (composite)
            {
                out << indent << "state " << names[i] << " {\n";
                const std::string inner = indent + "    ";
                if (initial[i] != hierarchy::INVALID)
                    out << inner << "[*] --> " << names[initial[i]] << "\n";
                for (int c : children[i])
                    emit_state(c, inner);
                out << indent << "}\n";
            }
            else
            {
                out << indent << "state " << names[i] << "\n";
            }
        };
        for (int r : roots)
            emit_state(r, "");

        const auto edges = detail::collect_edges<StateTable>(typename TransitionTable::metas{});
        for (const auto& e : edges)
        {
            if (e.internal)
            {
                out << names[e.from] << " --> " << names[e.from] << ": " << e.label << "   (internal)\n";
            }
            else
            {
                out << names[e.from] << " --> " << names[e.to] << detail::history_suffix(e.kind);
                if (!e.label.empty())
                    out << " : " << e.label;
                out << "\n";
            }
        }

        out << "@enduml\n";
        return out.str();
    }
} // namespace hfsm::diagram

#endif // HFSM_GENERATOR_H