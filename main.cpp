#include <iostream>
#include "hfsm.h"

using namespace std;

#include "hfsm.h"

#define S(name) struct name
#define T(name) struct name

using namespace hfsm;

enum class Events
{
    Event1,
    Event2,
    Event3,
    Event4,
};

using M = Machine<int&, Events>;

S(s1);
S(s2);
S(s3);
S(s4);
S(s5);
S(s6);

using states = M::Root<S(s1), Composite<S(s2), Composite<S(s3), S(s4), S(s5)>, S(s6)>>;
using Transitions = M::Transitions<
    M::Transition<s1, s2, Events::Event1, T(t1)>,
    M::Transition<s4, s5, Events::Event2, T(t2)>,
    M::Transition<s5, s6, Events::Event3, T(t3)>,
    M::Transition<s6, s1, Events::Event4, T(t4)>>;

struct s1 : M::state
{
    void on_enter(int& ctx) const override { std::cout << "S1" << std::endl; }
};
struct s2 : M::state
{
    void on_enter(int& ctx) const override { std::cout << "S2" << std::endl; }
};
struct s3 : M::state
{
    void on_enter(int& ctx) const override { std::cout << "S3" << std::endl; }
};
struct s4 : M::state
{
    void on_enter(int& ctx) const override { std::cout << "S4" << std::endl; }
};
struct s5 : M::state
{
    void on_enter(int& ctx) const override { std::cout << "S5" << std::endl; }
};
struct s6 : M::state
{
    void on_enter(int& ctx) const override { std::cout << "S6" << std::endl; }
};

struct t1 : M::transition
{
    void on_action(int& ctx) const override { std::cout << "T1" << std::endl; }
};
struct t2 : M::transition
{
    void on_action(int& ctx) const override { std::cout << "T2" << std::endl; }
};
struct t3 : M::transition
{
    void on_action(int& ctx) const override { std::cout << "T3" << std::endl; }
};
struct t4 : M::transition
{
    void on_action(int& ctx) const override { std::cout << "T4" << std::endl; }
};


static constexpr const char* names[] = {
    "State1", "State2", "State3", "State4", "State5", "State6",
};

void on_init(int n)
{
    assert(n < std::size(names));
    std::cout << "init state machine" << std::endl;
}
void on_transition(int from, int to)
{
    assert(from < std::size(names) && to < std::size(names));
    std::cout << "transition from " << names[from] << " to " << names[to] << std::endl;
}

int main()
{
    int i;
    using Controller = M::Tcontroller<states, Transitions>;
    Controller c(i);
    c.set_on_init(on_init);
    c.set_on_transition(on_transition);

    int count = 0;
    while (count < 4)
    {
        if (count == 0)
            c.on_event(Events::Event1);
        if (count == 1)
            c.on_event(Events::Event2);
        if (count == 2)
            c.on_event(Events::Event3);
        if (count == 3)
            c.on_event(Events::Event4);
        c.update();
        count++;
    }


    return 0;
}
