#ifndef HFSM_H
#define HFSM_H

#if __cplusplus < 201703L
#error "hfsm requires C++17 or later"
#endif

#include <array>
#include <cassert>
#include <cstddef>
#include <limits>
#include <type_traits>
#if __cplusplus >= 202002L
#include <span>
#endif

namespace hfsm
{
#if __cplusplus < 202002L
    template <typename T>
    struct span
    {
        constexpr span() noexcept : ptr_(nullptr), size_(0) {}
        constexpr span(T* ptr, std::size_t size) noexcept : ptr_(ptr), size_(size) {}

        template <std::size_t N>
        constexpr span(T (&arr)[N]) noexcept : ptr_(arr), size_(N)
        {
        }

        template <std::size_t N>
        constexpr span(std::array<T, N>& arr) noexcept : ptr_(arr.data()), size_(N)
        {
        }

        // For span<const T const*> from const array<const T const*, N> (T already top-level const)
        template <std::size_t N>
        constexpr span(const std::array<T, N>& arr) noexcept : ptr_(arr.data()), size_(N)
        {
        }

        // For span<const T> from const array<T, N> (e.g. span<const int> from array<int>)
        template <typename U, std::size_t N, std::enable_if_t<std::is_same_v<std::remove_const_t<T>, U>, int> = 0>
        constexpr span(const std::array<U, N>& arr) noexcept : ptr_(arr.data()), size_(N)
        {
        }

        constexpr T* data() const noexcept { return ptr_; }
        constexpr std::size_t size() const noexcept { return size_; }
        constexpr bool empty() const noexcept { return size_ == 0; }
        constexpr T& operator[](std::size_t i) const noexcept { return ptr_[i]; }
        constexpr T* begin() const noexcept { return ptr_; }
        constexpr T* end() const noexcept { return ptr_ + size_; }

    private:
        T* ptr_;
        std::size_t size_;
    };
#else
    template <typename T>
    using span = std::span<T>;
#endif
    enum class transition_kind
    {
        normal,
        shallow,
        deep
    };

    struct hierarchy
    {
        const span<const int> parentTable;
        const span<const int> initialStateTable;
        span<int> historyTable;
        span<int> path;
        int currentState = 0;

        static constexpr int TOP_MOST_PARENT = -1;
        static constexpr int INVALID = -1;

        bool valid_state(int s) const { return s >= 0 && s < static_cast<int>(parentTable.size()); }

        hierarchy(span<const int> parentTable, span<const int> initialStateTable, span<int> historyTable,
                  span<int> path) :
            parentTable(parentTable), initialStateTable(initialStateTable), historyTable(historyTable), path(path)
        {
            for (int& entry : historyTable)
                entry = INVALID;
        }

        int lca_compute(int a, int b) const
        {
            assert(valid_state(a) && valid_state(b));
            int ia = a;
            int ib = b;

            int depthA = 0;
            int depthB = 0;
            while (ia != TOP_MOST_PARENT)
            {
                ia = parentTable[ia];
                depthA++;
            }
            while (ib != TOP_MOST_PARENT)
            {
                ib = parentTable[ib];
                depthB++;
            }
            ia = a;
            ib = b;
            while (depthA > depthB)
            {
                --depthA;
                ia = parentTable[ia];
            }
            while (depthB > depthA)
            {
                ib = parentTable[ib];
                --depthB;
            }
            while (ia != ib)
            {
                ia = parentTable[ia];
                ib = parentTable[ib];
            }
            return ia;
        }

        int exit_path_compute(int a, int lca) const
        {
            assert(valid_state(a));
            int index = 0;
            while (a != lca)
            {
                assert(index < static_cast<int>(path.size()));
                path[index] = a;
                a = parentTable[a];
                index++;
            }
            return index;
        }

        int entry_path_compute(int a, int lca) const
        {
            int index = exit_path_compute(a, lca);
            int p1 = 0;
            int p2 = index - 1;
            while (p1 < p2)
            {
                std::swap(path[p1], path[p2]);
                p1++;
                p2--;
            }
            return index;
        }

        int initial_path_compute(int a, int startIndex = 0) const
        {
            assert(valid_state(a));
            int n = startIndex;
            a = initialStateTable[a];
            while (a != INVALID)
            {
                assert(n < static_cast<int>(path.size()));
                path[n] = a;
                a = initialStateTable[a];
                n++;
            }
            return n - startIndex;
        }

        int shallow_history_path_compute(int a) const
        {
            assert(valid_state(a));
            int s = historyTable[a];
            if (s == INVALID)
                return initial_path_compute(a);
            assert(!path.empty());
            path[0] = s;
            int rest = is_composite(s) ? initial_path_compute(s, 1) : 0;
            return 1 + rest;
        }

        int deep_history_path_compute(int a) const
        {
            assert(valid_state(a));
            int s = historyTable[a];
            if (s == INVALID) // subtree was never entered; fall back to static initial-state chain
                return initial_path_compute(a);
            int n = 0;
            while (s != INVALID)
            {
                assert(valid_state(s));
                assert(n < static_cast<int>(path.size()));
                path[n] = s;
                n++;
                s = historyTable[s];
            }
            return n;
        }

        void enter(int s)
        {
            assert(valid_state(s));
            int parent = parentTable[s];
            if (parent != TOP_MOST_PARENT)
                historyTable[parent] = s;
            currentState = s;
        }

        int transition_lca(int next) const
        {
            assert(valid_state(next));
            // self-transition: use parent as LCA so the state fully exits and re-enters.
            return (next == currentState) ? parentTable[next] : lca_compute(currentState, next);
        }

        int descend_path_compute(int next, transition_kind kind) const
        {
            switch (kind)
            {
            case transition_kind::shallow:
                return shallow_history_path_compute(next);
            case transition_kind::deep:
                return deep_history_path_compute(next);
            default:
                return initial_path_compute(next);
            }
        }

        bool is_active(int s) const
        {
            assert(valid_state(s));
            if (s == currentState)
                return true;
            int a = parentTable[currentState];
            while (a != INVALID)
            {
                if (a == s)
                    return true;
                a = parentTable[a];
            }
            return false;
        }

        bool is_composite(int s) const
        {
            assert(valid_state(s));
            return initialStateTable[s] != INVALID;
        }
    };

    template <typename Context, typename EventType = int>
    struct controller
    {
        struct state
        {
            virtual void on_enter(Context& ctx_) const {}
            virtual void on_exit(Context& ctx_) const {}
            virtual void on_update(Context& ctx_) const {}
        };

        struct transition
        {
            virtual void on_action(Context& ctx_) const {}
            virtual bool on_guard(Context& ctx_) const { return true; }
        };

        struct auto_row
        {
            int from = 0;
            int to = 0;
            transition_kind kind = transition_kind::normal;
            const transition* t = nullptr;

            constexpr auto_row() = default;

            constexpr auto_row(int from, int to, transition_kind kind, const transition* t) :
                from(from), to(to), kind(kind), t(t)
            {
            }

            static constexpr auto_row make(int from, int to, const transition* t,
                                           transition_kind kind = transition_kind::normal)
            {
                return auto_row(from, to, kind, t);
            }
        };

        struct row : auto_row
        {
            EventType eventId{};
            unsigned internal : 1;

            constexpr row() : internal(0) {}

            constexpr row(int from, int to, EventType eventId, bool internal, transition_kind kind,
                          const transition* t) : auto_row(from, to, kind, t), eventId(eventId), internal(internal)
            {
            }

            static constexpr row make_normal(int from, int to, EventType eventId, const transition* t,
                                             transition_kind kind = transition_kind::normal)
            {
                return row(from, to, eventId, false, kind, t);
            }

            static constexpr row make_internal(int from, EventType eventId, const transition* t,
                                               transition_kind kind = transition_kind::normal)
            {
                return row(from, from, eventId, true, kind, t);
            }
        };

        controller(Context ctx, span<const int> parentTable, span<const int> initialStateTable, span<int> historyTable,
                   span<int> path, span<const state* const> stateTable, span<const row> transitionTable,
                   span<const auto_row> autoTransitionTable) :
            h(parentTable, initialStateTable, historyTable, path), ctx(ctx), stateTable(stateTable),
            transitionTable(transitionTable), autoTransitionTable(autoTransitionTable)
        {
        }

        // Returns false if the pending slot is occupied; caller retains the event and retries.
        bool on_event(EventType eventId)
        {
            if (hasPendingEvent)
                return false;
            pendingEvent = eventId;
            hasPendingEvent = true;
            return true;
        }

        // Clock tick: initializes on the first call, then: auto-transitions → on_update → pending event.
        void update()
        {
            if (!initialized)
            {
                if (onInit)
                    onInit(h.currentState);
                stateTable[h.currentState]->on_enter(ctx);
                h.enter(h.currentState);
                enter_path(h.initial_path_compute(h.currentState));
                initialized = true;
            }
            process_automatic();
            stateTable[h.currentState]->on_update(ctx);
            if (hasPendingEvent)
            {
                EventType ev = pendingEvent;
                hasPendingEvent = false;
                dispatch_event(ev);
            }
        }

        void set_on_init(void (*onInit_)(int state)) { onInit = onInit_; }
        void set_on_transition(void (*onTransition_)(int from, int to)) { onTransition = onTransition_; }
        int get_current_state() const { return h.currentState; }
        bool is_active(const int state) const { return h.is_active(state); }

    private:
        void enter_path(int n)
        {
            for (int i = 0; i < n; i++)
            {
                int s = h.path[i];
                stateTable[s]->on_enter(ctx);
                h.enter(s);
            }
        }

        void process_automatic()
        {
            bool fired = true;
            while (fired)
            {
                fired = false;
                int s = h.currentState;
                while (s != hierarchy::TOP_MOST_PARENT)
                {
                    bool found = false;
                    for (const auto_row& r : autoTransitionTable)
                    {
                        if (r.from != s)
                            continue;
                        if (!r.t->on_guard(ctx))
                            continue;
                        fire(r);
                        fired = true;
                        found = true;
                        break;
                    }
                    if (found)
                        break;
                    s = h.parentTable[s];
                }
            }
        }

        void dispatch_event(EventType eventId)
        {
            int s = h.currentState;
            while (s != hierarchy::TOP_MOST_PARENT)
            {
                for (const row& r : transitionTable)
                {
                    if (r.from != s || r.eventId != eventId)
                        continue;
                    if (!r.t->on_guard(ctx))
                        continue;
                    if (r.internal)
                        r.t->on_action(ctx);
                    else
                        fire(r);
                    return;
                }
                s = h.parentTable[s];
            }
        }

        void fire(const auto_row& r)
        {
            const int next = r.to;
            if (onTransition)
                onTransition(h.currentState, next);
            const int lca = h.transition_lca(next);
            const int exitPathLen = h.exit_path_compute(h.currentState, lca);
            for (int i = 0; i < exitPathLen; i++)
                stateTable[h.path[i]]->on_exit(ctx);
            r.t->on_action(ctx);
            enter_path(h.entry_path_compute(next, lca));
            if (h.is_composite(next))
                enter_path(h.descend_path_compute(next, r.kind));
        }

        hierarchy h;
        Context ctx;

        // Optional debug/test hooks - plain function pointers, no string/name knowledge here.
        void (*onInit)(int state) = nullptr;
        void (*onTransition)(int from, int to) = nullptr;

        const span<const state* const> stateTable;
        const span<const row> transitionTable;
        const span<const auto_row> autoTransitionTable;

        EventType pendingEvent{};
        bool hasPendingEvent = false;
        bool initialized = false;
    };

    template <typename Self, typename First, typename... Rest>
    struct Composite
    {
    };

    namespace utils
    {
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////

        template <typename... Ts>
        struct list
        {
        };

        template <typename A, typename B>
        struct list_concat;

        template <typename... As, typename... Bs>
        struct list_concat<list<As...>, list<Bs...>>
        {
            using type = list<As..., Bs...>;
        };

        template <typename A, typename B>
        using list_concat_t = typename list_concat<A, B>::type;

        struct list_none
        {
        };

        template <typename T, typename List>
        struct list_index_of;

        template <typename T, typename... Ts>
        struct list_index_of<T, list<Ts...>>
        {
            static constexpr int find()
            {
                constexpr bool matches[] = {std::is_same_v<T, Ts>..., false};
                for (int i = 0; i < static_cast<int>(sizeof...(Ts)); i++)
                {
                    if (matches[i])
                        return i;
                }
                return -1;
            }

            static constexpr int value = find();
            // a typo'd tag - one not in the tree - lands here as a compile error.
            static_assert(value != -1, "unknown type referenced");
        };

        template <typename T, typename List>
        struct list_resolve_index
        {
            static constexpr int value = list_index_of<T, List>::value;
        };

        // list_none -> -1: a root's parent / a leaf's initial child has no index, and must skip
        // the index_of static_assert above rather than be treated as a missing tag.
        template <typename List>
        struct list_resolve_index<list_none, List>
        {
            static constexpr int value = -1;
        };

        template <typename T, typename List>
        inline constexpr int resolve_index_v = list_resolve_index<T, List>::value;

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////

        template <typename Tag, typename Parent, typename InitialChild = list_none>
        struct flat_entry
        {
        };

        template <typename Parent, typename... Nodes>
        struct flatten_siblings;

        template <typename Parent, typename Node>
        struct flatten_node
        {
            using type = list<flat_entry<Node, Parent>>;
        };

        template <typename Parent>
        struct flatten_siblings<Parent>
        {
            using type = list<>;
        };

        template <typename Parent, typename Node, typename... Rest>
        struct flatten_siblings<Parent, Node, Rest...>
        {
            using type = list_concat_t<typename flatten_node<Parent, Node>::type,
                                       typename flatten_siblings<Parent, Rest...>::type>;
        };

        // a composite's first child can itself be a composite - we need its *tag* (Self), not the
        // whole Composite<...> type, so the initial-child slot resolves against Tags later.
        template <typename Node>
        struct node_tag
        {
            using type = Node;
        };

        template <typename Self, typename First, typename... Rest>
        struct node_tag<Composite<Self, First, Rest...>>
        {
            using type = Self;
        };

        template <typename Node>
        using node_tag_t = typename node_tag<Node>::type;

        template <typename Parent, typename Self, typename First, typename... Rest>
        struct flatten_node<Parent, Composite<Self, First, Rest...>>
        {
            using type = list_concat_t<list<flat_entry<Self, Parent, node_tag_t<First>>>,
                                       typename flatten_siblings<Self, First, Rest...>::type>;
        };

        template <typename... States>
        using flatten_t = typename flatten_siblings<list_none, States...>::type;

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////

        template <typename Entry>
        struct tag_of;

        template <typename Tag, typename Parent, typename InitialChild>
        struct tag_of<flat_entry<Tag, Parent, InitialChild>>
        {
            using type = Tag;
        };

        template <typename Entry>
        using tag_of_t = typename tag_of<Entry>::type;

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////

        template <typename Entry>
        struct parent_of;

        template <typename Tag, typename Parent, typename InitialChild>
        struct parent_of<flat_entry<Tag, Parent, InitialChild>>
        {
            using type = Parent;
        };

        template <typename Entry>
        using parent_of_t = typename parent_of<Entry>::type;

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////

        template <typename Entry>
        struct initial_child_of;

        template <typename Tag, typename Parent, typename InitialChild>
        struct initial_child_of<flat_entry<Tag, Parent, InitialChild>>
        {
            using type = InitialChild;
        };

        template <typename Entry>
        using initial_child_of_t = typename initial_child_of<Entry>::type;

        template <std::size_t N>
        constexpr int compute_depth(const std::array<int, N>& parentTable)
        {
            int maxDepth = 0;
            for (std::size_t i = 0; i < N; i++)
            {
                int depth = 0;
                int p = static_cast<int>(i);
                while (p != -1)
                {
                    p = parentTable[p];
                    depth++;
                }
                if (depth > maxDepth)
                    maxDepth = depth;
            }
            return maxDepth;
        }

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////

        template <typename List>
        struct is_unique;

        template <>
        struct is_unique<list<>> : std::true_type
        {
        };

        template <typename T, typename... Rest>
        struct is_unique<list<T, Rest...>>
            : std::bool_constant<(!std::is_same_v<T, Rest> && ...) && is_unique<list<Rest...>>::value>
        {
        };

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////

        template <typename FlatList>
        struct StateTable;

        template <typename... Entries>
        struct StateTable<list<Entries...>>
        {
            using Tags = list<tag_of_t<Entries>...>;
            static constexpr int Size = sizeof...(Entries);

            static_assert(Size <= std::numeric_limits<int>::max(), "too many states for int-based HFSM indices");
            static_assert(is_unique<Tags>::value, "duplicate state tag in HFSM tree");

            template <typename Tag>
            static constexpr int index_of_v = list_index_of<Tag, Tags>::value;

            static constexpr std::array<int, Size> parentTable()
            {
                return {resolve_index_v<parent_of_t<Entries>, Tags>...};
            }

            static constexpr std::array<int, Size> initialStateTable()
            {
                return {resolve_index_v<initial_child_of_t<Entries>, Tags>...};
            }

            static constexpr int Depth = compute_depth(parentTable());
        };

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////

        template <typename... States>
        using Root = StateTable<flatten_t<States...>>;

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////

        // one shared instance per type - same T always resolves to the same static member, which
        // gives a stable pointer to drop into the state/transition tables.
        template <typename T>
        struct instancer
        {
            static constexpr T value{};
        };
    } // namespace utils

    template <typename Context, typename EventType = int>
    struct Machine
    {
        using state = typename controller<Context, EventType>::state;
        using transition = typename controller<Context, EventType>::transition;
        using row = typename controller<Context, EventType>::row;
        using auto_row = typename controller<Context, EventType>::auto_row;

        template <typename From, typename To, EventType EventId, typename TransitionType,
                  transition_kind Kind = transition_kind::normal>
        struct Transition
        {
            struct Meta
            {
                using FromState = From;
                using ToState = To;
                using transition_type = TransitionType;
                static constexpr transition_kind kind = Kind;
                static constexpr EventType eventId = EventId;
                static constexpr bool internal = false;
                static constexpr bool automatic = false;
                static constexpr bool has_event = true;
            };
        };

        template <typename From, EventType EventId, typename TransitionType>
        struct InternalTransition
        {
            struct Meta
            {
                using FromState = From;
                using ToState = From;
                using transition_type = TransitionType;
                static constexpr transition_kind kind = transition_kind::normal;
                static constexpr EventType eventId = EventId;
                static constexpr bool internal = true;
                static constexpr bool automatic = false;
                static constexpr bool has_event = true;
            };
        };

        template <typename From, typename To, typename TransitionType, transition_kind Kind = transition_kind::normal>
        struct AutomaticTransition
        {
            struct Meta
            {
                using FromState = From;
                using ToState = To;
                using transition_type = TransitionType;
                static constexpr transition_kind kind = Kind;
                static constexpr bool internal = false;
                static constexpr bool automatic = true;
                static constexpr bool has_event = false;
            };
        };

        template <typename... States>
        struct Root : utils::Root<States...>
        {
            static_assert(sizeof...(States) > 0, "Machine::Root requires at least one state");

            using Base = utils::Root<States...>;

            static constexpr std::array<const state* const, Base::Size> states()
            {
                return states_of(typename Base::Tags{});
            }

        private:
            template <typename... Tags>
            static constexpr std::array<const state* const, sizeof...(Tags)> states_of(utils::list<Tags...>)
            {
                static_assert((std::is_base_of_v<state, Tags> && ...),
                              "all state tags must derive from Machine<Context>::state");
                static_assert((std::is_default_constructible_v<Tags> && ...),
                              "all state tags must be default-constructible");
                return {&utils::instancer<Tags>::value...};
            }
        };

        template <typename... Entries>
        struct Transitions
        {
            // Compile-time list of every transition's Meta, for tooling (e.g. diagram export).
            using metas = utils::list<typename Entries::Meta...>;

        private:
            template <typename T>
            struct is_auto : std::false_type
            {
            };

            template <typename From, typename To, typename TT, transition_kind Kind>
            struct is_auto<AutomaticTransition<From, To, TT, Kind>> : std::true_type
            {
            };

            static constexpr std::size_t AutoCount = (0 + ... + (is_auto<Entries>::value ? 1 : 0));
            static constexpr std::size_t EventCount = sizeof...(Entries) - AutoCount;

        public:
            template <typename Tags>
            static constexpr std::array<row, EventCount> event_rows()
            {
                std::array<row, EventCount> result{};
                std::size_t i = 0;
                auto fill = [&](auto entry)
                {
                    using E = decltype(entry);
                    if constexpr (!is_auto<E>::value)
                        result[i++] = make_row<Tags>(entry);
                };
                (fill(Entries{}), ...);
                return result;
            }

            template <typename Tags>
            static constexpr std::array<auto_row, AutoCount> auto_rows()
            {
                std::array<auto_row, AutoCount> result{};
                std::size_t i = 0;
                auto fill = [&](auto entry)
                {
                    using E = decltype(entry);
                    if constexpr (is_auto<E>::value)
                        result[i++] = make_auto_row<Tags>(entry);
                };
                (fill(Entries{}), ...);
                return result;
            }

        private:
            template <typename Tags, typename From, typename To, EventType EventId, typename TransitionType,
                      transition_kind Kind>
            static constexpr row make_row(Transition<From, To, EventId, TransitionType, Kind>)
            {
                static_assert(std::is_base_of_v<transition, TransitionType>,
                              "transition type must derive from Machine<Context, EventType>::transition");
                static_assert(std::is_default_constructible_v<TransitionType>,
                              "transition type must be default-constructible");
                return row::make_normal(utils::resolve_index_v<From, Tags>, utils::resolve_index_v<To, Tags>, EventId,
                                        &utils::instancer<TransitionType>::value, Kind);
            }

            template <typename Tags, typename From, EventType EventId, typename TransitionType>
            static constexpr row make_row(InternalTransition<From, EventId, TransitionType>)
            {
                static_assert(std::is_base_of_v<transition, TransitionType>,
                              "transition type must derive from Machine<Context, EventType>::transition");
                static_assert(std::is_default_constructible_v<TransitionType>,
                              "transition type must be default-constructible");
                return row::make_internal(utils::resolve_index_v<From, Tags>, EventId,
                                          &utils::instancer<TransitionType>::value);
            }

            template <typename Tags, typename From, typename To, typename TransitionType, transition_kind Kind>
            static constexpr auto_row make_auto_row(AutomaticTransition<From, To, TransitionType, Kind>)
            {
                static_assert(std::is_base_of_v<transition, TransitionType>,
                              "transition type must derive from Machine<Context>::transition");
                static_assert(std::is_default_constructible_v<TransitionType>,
                              "transition type must be default-constructible");
                return auto_row::make(utils::resolve_index_v<From, Tags>, utils::resolve_index_v<To, Tags>,
                                      &utils::instancer<TransitionType>::value, Kind);
            }
        };

        template <typename StateTable, typename TransitionTable>
        struct Tcontroller : controller<Context, EventType>
        {
        private:
            // these must be static constexpr, not locals: the controller stores non-owning spans
            // over them, so they need storage that outlives the ctor or the spans dangle.
            static constexpr auto parentTable = StateTable::parentTable();
            static constexpr auto initialStateTable = StateTable::initialStateTable();
            static constexpr auto stateTable = StateTable::states();
            static constexpr auto eventRows = TransitionTable::template event_rows<typename StateTable::Tags>();
            static constexpr auto autoRows = TransitionTable::template auto_rows<typename StateTable::Tags>();

            static constexpr auto make_history_table() noexcept
            {
                std::array<int, StateTable::Size> arr{};
                for (auto& v : arr)
                    v = hierarchy::INVALID;
                return arr;
            }

            int path[StateTable::Depth]{};
            std::array<int, StateTable::Size> historyTable{make_history_table()};

        public:
            explicit Tcontroller(Context ctx) :
                controller<Context, EventType>(ctx, parentTable, initialStateTable, historyTable, path, stateTable,
                                               eventRows, autoRows)
            {
            }

            template <typename T>
            [[nodiscard]] bool is_active_s() const
            {
                return this->is_active(StateTable::template index_of_v<T>);
            }

            template <typename T>
            [[nodiscard]] bool is_current_s() const
            {
                return StateTable::template index_of_v<T> == this->get_current_state();
            }
        };
    };
} // namespace hfsm

#endif // HFSM_H
