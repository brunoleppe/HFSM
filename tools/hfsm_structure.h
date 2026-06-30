//   _   _  _____  ____   __  __
//  | | | ||  ___|/ ___| |  \/  |
//  | |_| || |_   \___ \ | |\/| |
//  |  _  ||  _|   ___) || |  | |
//  |_| |_||_|    |____/ |_|  |_|
//
//  HFSM — Optional structure validation. Checks for duplicated transitions, self-loops.
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

#ifndef HFSM_STRUCTURE_H
#define HFSM_STRUCTURE_H

namespace hfsm
{
    namespace structure {

        namespace detail {
            template <typename RowArray>
            constexpr bool no_duplicate_triggers(const RowArray& rows) noexcept
            {
                for (std::size_t i = 0; i < rows.size(); ++i)
                    for (std::size_t j = i + 1; j < rows.size(); ++j)
                        if (rows[i].from == rows[j].from && rows[i].eventId == rows[j].eventId)
                            return false;
                return true;
            }

            template <typename RowArray>
            constexpr bool no_auto_self_loops(const RowArray& rows) noexcept
            {
                for (std::size_t i = 0; i < rows.size(); ++i)
                    if (rows[i].from == rows[i].to)
                        return false;
                return true;
            }

            template <typename FSM>
            struct validator : FSM
            {
                static_assert(no_duplicate_triggers(FSM::eventRows),
                              "duplicate (from, eventId) pair in transition table: second transition is unreachable");
                static_assert(no_auto_self_loops(FSM::autoRows),
                              "automatic transition self-loop detected: from == to causes an infinite loop");
            };
        } // namespace detail

        template <typename FSM>
        constexpr void validate() noexcept
        {
            (void)sizeof(detail::validator<FSM>);
        }

    } // namespace structure
}

#endif // HFSM_HFSM_STRUCTURE_H
