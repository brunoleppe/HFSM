#include <array>
#include <gtest/gtest.h>
#include <vector>
#include "hfsm.h"

using namespace hfsm;

// Test fixture tree:
//
// 0 (root, initial child: 1)
// |-- 1 (initial child: 3)
// |   |-- 3 (leaf)
// |   `-- 4 (leaf)
// |-- 2 (initial child: 5)
// |   `-- 5 (initial child: 6)
// |       `-- 6 (initial child: 7)
// |           `-- 7 (leaf)
// `-- 8 (initial child: 9)
//     `-- 9 (leaf)
static constexpr int parents[10] = {
    -1, // 0
    0, // 1
    0, // 2
    1, // 3
    1, // 4
    2, // 5
    5, // 6
    6, // 7
    0, // 8
    8, // 9
};
static constexpr int initialState[10] = {
    1, // 0 -> 1
    3, // 1 -> 3
    5, // 2 -> 5
    -1, // 3 (leaf)
    -1, // 4 (leaf)
    6, // 5 -> 6
    7, // 6 -> 7
    -1, // 7 (leaf)
    9, // 8 -> 9
    -1, // 9 (leaf)
};

static std::vector<int> path_vector(const std::array<int, 10>& path, int len)
{
    return std::vector<int>(path.begin(), path.begin() + len);
}

TEST(ExitPathCompute, WalksUpToLca)
{
    std::array<int, 10> buf{};
    hierarchy h{parents, initialState, {}, buf};
    int len = h.exit_path_compute(7, 2); // 7 -> 6 -> 5 -> (2)
    EXPECT_EQ(path_vector(buf, len), (std::vector<int>{7, 6, 5}));
}

TEST(ExitPathCompute, AsymmetricDepths)
{
    std::array<int, 10> buf{};
    hierarchy h{parents, initialState, {}, buf};
    int len = h.exit_path_compute(4, 0); // parent(4)=1, parent(1)=0
    EXPECT_EQ(path_vector(buf, len), (std::vector<int>{4, 1}));
}

TEST(ExitPathCompute, SingleHop)
{
    std::array<int, 10> buf{};
    hierarchy h{parents, initialState, {}, buf};
    int len = h.exit_path_compute(9, 8);
    EXPECT_EQ(path_vector(buf, len), (std::vector<int>{9}));
}

TEST(ExitPathCompute, SourceEqualsLca)
{
    std::array<int, 10> buf{};
    hierarchy h{parents, initialState, {}, buf};
    EXPECT_EQ(h.exit_path_compute(3, 3), 0);
}

TEST(ExitPathCompute, LongestChain)
{
    std::array<int, 10> buf{};
    hierarchy h{parents, initialState, {}, buf};
    int len = h.exit_path_compute(7, 0);
    EXPECT_EQ(path_vector(buf, len), (std::vector<int>{7, 6, 5, 2}));
}

TEST(EntryPathCompute, ReverseOfExit)
{
    std::array<int, 10> buf{};
    hierarchy h{parents, initialState, {}, buf};
    int len = h.entry_path_compute(7, 2);
    EXPECT_EQ(path_vector(buf, len), (std::vector<int>{5, 6, 7}));
}

TEST(EntryPathCompute, AsymmetricDepths)
{
    std::array<int, 10> buf{};
    hierarchy h{parents, initialState, {}, buf};
    int len = h.entry_path_compute(4, 0);
    EXPECT_EQ(path_vector(buf, len), (std::vector<int>{1, 4}));
}

TEST(EntryPathCompute, SingleElement)
{
    std::array<int, 10> buf{};
    hierarchy h{parents, initialState, {}, buf};
    int len = h.entry_path_compute(9, 8);
    EXPECT_EQ(path_vector(buf, len), (std::vector<int>{9}));
}

TEST(EntryPathCompute, SourceEqualsLca)
{
    std::array<int, 10> buf{};
    hierarchy h{parents, initialState, {}, buf};
    EXPECT_EQ(h.entry_path_compute(3, 3), 0);
}

TEST(EntryPathCompute, LongestChain)
{
    std::array<int, 10> buf{};
    hierarchy h{parents, initialState, {}, buf};
    int len = h.entry_path_compute(7, 0);
    EXPECT_EQ(path_vector(buf, len), (std::vector<int>{2, 5, 6, 7}));
}

TEST(InitialPathCompute, DescendsToLeaf)
{
    std::array<int, 10> buf{};
    hierarchy h{parents, initialState, {}, buf};
    int len = h.initial_path_compute(0);
    EXPECT_EQ(path_vector(buf, len), (std::vector<int>{1, 3}));
}

TEST(InitialPathCompute, DeeperChain)
{
    std::array<int, 10> buf{};
    hierarchy h{parents, initialState, {}, buf};
    int len = h.initial_path_compute(2);
    EXPECT_EQ(path_vector(buf, len), (std::vector<int>{5, 6, 7}));
}

TEST(InitialPathCompute, SingleStep)
{
    std::array<int, 10> buf{};
    hierarchy h{parents, initialState, {}, buf};
    int len = h.initial_path_compute(8);
    EXPECT_EQ(path_vector(buf, len), (std::vector<int>{9}));
}

TEST(InitialPathCompute, LeafHasNoInitialChild)
{
    std::array<int, 10> buf{};
    hierarchy h{parents, initialState, {}, buf};
    EXPECT_EQ(h.initial_path_compute(3), 0);
    EXPECT_EQ(h.initial_path_compute(7), 0);
}

TEST(Enter, StampsParentHistoryAndCurrentState)
{
    std::array<int, 10> buf{};
    std::array<int, 10> hist{};
    hierarchy h{parents, initialState, hist, buf};

    h.enter(3);
    EXPECT_EQ(h.currentState, 3);
    EXPECT_EQ(hist[1], 3);

    h.enter(4);
    EXPECT_EQ(h.currentState, 4);
    EXPECT_EQ(hist[1], 4); // overwritten
}

TEST(History, FallsBackToDefaultWhenUnset)
{
    std::array<int, 10> buf{};
    std::array<int, 10> hist{}; // never entered anything -> all INVALID via constructor

    hierarchy h{parents, initialState, hist, buf};
    int lenShallow = h.shallow_history_path_compute(0);
    EXPECT_EQ(path_vector(buf, lenShallow), (std::vector<int>{1, 3}));

    int lenDeep = h.deep_history_path_compute(2);
    EXPECT_EQ(path_vector(buf, lenDeep), (std::vector<int>{5, 6, 7}));
}

TEST(History, ShallowRestoresOneLevelThenDefaults)
{
    std::array<int, 10> buf{};
    std::array<int, 10> hist{};
    hierarchy h{parents, initialState, hist, buf};

    // simulate having previously been active in leaf 4, entered via composite 0 -> composite 1 -> 4
    // (1's *default* child is 3, not 4 - this is what makes shallow vs deep diverge below)
    h.enter(1);
    h.enter(4);

    int len = h.shallow_history_path_compute(0);
    EXPECT_EQ(path_vector(buf, len), (std::vector<int>{1, 3}));
}

TEST(History, DeepRestoresTheFullActualChain)
{
    std::array<int, 10> buf{};
    std::array<int, 10> hist{};
    hierarchy h{parents, initialState, hist, buf};

    h.enter(1);
    h.enter(4);

    int len = h.deep_history_path_compute(0);
    EXPECT_EQ(path_vector(buf, len), (std::vector<int>{1, 4}));
}

TEST(TransitionLca, MatchesLcaComputeForNonSelfTransition)
{
    std::array<int, 10> buf{};
    std::array<int, 10> hist{};
    hierarchy h{parents, initialState, hist, buf};
    h.enter(7);

    EXPECT_EQ(h.transition_lca(2), h.lca_compute(7, 2));
}

TEST(TransitionLca, UsesParentForSelfTransition)
{
    std::array<int, 10> buf{};
    std::array<int, 10> hist{};
    hierarchy h{parents, initialState, hist, buf};
    h.enter(7);

    EXPECT_EQ(h.transition_lca(7), parents[7]);
}

TEST(DescendPathCompute, NormalUsesDefaultInitialPath)
{
    std::array<int, 10> buf{};
    std::array<int, 10> hist{};
    hierarchy h{parents, initialState, hist, buf};
    h.enter(1);
    h.enter(4);

    int len = h.descend_path_compute(0, transition_kind::normal);
    EXPECT_EQ(path_vector(buf, len), (std::vector<int>{1, 3}));
}

TEST(DescendPathCompute, ShallowHistory)
{
    std::array<int, 10> buf{};
    std::array<int, 10> hist{};
    hierarchy h{parents, initialState, hist, buf};
    h.enter(1);
    h.enter(4);

    int len = h.descend_path_compute(0, transition_kind::shallow);
    EXPECT_EQ(path_vector(buf, len), (std::vector<int>{1, 3}));
}

TEST(DescendPathCompute, DeepHistory)
{
    std::array<int, 10> buf{};
    std::array<int, 10> hist{};
    hierarchy h{parents, initialState, hist, buf};
    h.enter(1);
    h.enter(4);

    int len = h.descend_path_compute(0, transition_kind::deep);
    EXPECT_EQ(path_vector(buf, len), (std::vector<int>{1, 4}));
}

TEST(IsActive, ReflectsAncestorChainOfCurrentState)
{
    std::array<int, 10> buf{};
    std::array<int, 10> hist{};
    hierarchy h{parents, initialState, hist, buf};
    h.enter(1);
    h.enter(4);

    EXPECT_TRUE(h.is_active(4));
    EXPECT_TRUE(h.is_active(1));
    EXPECT_TRUE(h.is_active(0));
    EXPECT_FALSE(h.is_active(3));
    EXPECT_FALSE(h.is_active(2));
}

TEST(IsComposite, ReflectsInitialStateTable)
{
    std::array<int, 10> buf{};
    std::array<int, 10> hist{};
    hierarchy h{parents, initialState, hist, buf};

    EXPECT_TRUE(h.is_composite(0));
    EXPECT_TRUE(h.is_composite(1));
    EXPECT_FALSE(h.is_composite(3));
    EXPECT_FALSE(h.is_composite(4));
}
