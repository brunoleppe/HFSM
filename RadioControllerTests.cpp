#include <array>
#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "hfsm.h"

// Simulated LoRa radio controller. See design discussion: 16 states, 18 events,
// 26 rows, deliberately covering every dispatch mechanism the controller supports:
// multi-level hierarchy, internal transitions at two different ancestor scopes,
// guarded retry with mutable context state, a 3-level guard fallthrough chain,
// a permanently-blocked guard, a forced self-transition, and shallow vs deep
// history compared side-by-side from the same Sleep escape state.
//
// Root
// |-- Idle                          (leaf, initial)
// |-- Active                        (composite)
// |   |-- Listening                 (leaf, initial)
// |   |-- Configuring               (composite)
// |   |   |-- SetFrequency          (leaf, initial)
// |   |   |-- SetPower              (leaf)
// |   |   `-- SetSpreadingFactor    (leaf)
// |   `-- Transmitting              (composite)
// |       |-- Preparing             (leaf, initial)
// |       |-- Sending               (leaf)
// |       `-- AwaitingAck           (leaf)
// |-- Fault                         (composite)
// |   |-- Recoverable               (leaf, initial)
// |   `-- Critical                  (leaf)
// `-- Sleep                         (leaf)

struct RadioContext
{
    static constexpr int MAX_RETRIES = 2;

    std::vector<std::string> events;
    int retryCount = 0;
    bool wasActive = false;
    bool highPowerDraw = false;
    bool midTransmission = false;
    int configLogCount = 0;
    int heartbeatCount = 0;
    int recalibrateCount = 0;
    int awaitingAckTicks = 0;
};

using namespace hfsm;
using M = Machine<RadioContext&>;

// --- compile-time string NTTP: lets LogState/LogT share one template, one type per label ---

template <std::size_t N>
struct StrLiteral
{
    char value[N];
    constexpr StrLiteral(const char (&str)[N])
    {
        for (std::size_t i = 0; i < N; i++)
            value[i] = str[i];
    }
};

// --- states ---------------------------------------------------------------

template <StrLiteral Name>
struct LogState : M::state
{
    void on_enter(RadioContext& ctx) const override { ctx.events.push_back(std::string("enter:") + Name.value); }
    void on_exit(RadioContext& ctx) const override { ctx.events.push_back(std::string("exit:") + Name.value); }
};

struct AwaitingAck : M::state
{
    void on_enter(RadioContext& ctx) const override { ctx.events.push_back("enter:AwaitingAck"); }
    void on_exit(RadioContext& ctx) const override { ctx.events.push_back("exit:AwaitingAck"); }
    void on_update(RadioContext& ctx) const override { ctx.awaitingAckTicks++; }
};

using RootState = LogState<"Root">;
using Idle = LogState<"Idle">;
using Active = LogState<"Active">;
using Listening = LogState<"Listening">;
using Configuring = LogState<"Configuring">;
using SetFrequency = LogState<"SetFrequency">;
using SetPower = LogState<"SetPower">;
using SetSpreadingFactor = LogState<"SetSpreadingFactor">;
using Transmitting = LogState<"Transmitting">;
using Preparing = LogState<"Preparing">;
using Sending = LogState<"Sending">;
using Fault = LogState<"Fault">;
using Recoverable = LogState<"Recoverable">;
using Critical = LogState<"Critical">;
using Sleep = LogState<"Sleep">;

// --- transition templates -------------------------------------------------

template <StrLiteral Label, bool Guard = true>
struct LogT : M::transition
{
    bool on_guard(RadioContext&) const override { return Guard; }
    void on_action(RadioContext& ctx) const override { ctx.events.push_back(Label.value); }
};

template <bool ActiveFlag, StrLiteral Label>
struct TimeoutT : M::transition
{
    void on_action(RadioContext& ctx) const override
    {
        ctx.wasActive = ActiveFlag;
        ctx.events.push_back(Label.value);
    }
};

template <bool RequireActive, StrLiteral Label>
struct WasActiveGuardT : M::transition
{
    bool on_guard(RadioContext& ctx) const override { return ctx.wasActive == RequireActive; }
    void on_action(RadioContext& ctx) const override { ctx.events.push_back(Label.value); }
};

// --- transition types with non-trivial logic ------------------------------

struct LogConfigTransition : M::transition
{
    void on_action(RadioContext& ctx) const override
    {
        ctx.configLogCount++;
        ctx.events.push_back("action:log-config");
    }
};

struct HeartbeatTransition : M::transition
{
    void on_action(RadioContext& ctx) const override
    {
        ctx.heartbeatCount++;
        ctx.events.push_back("action:heartbeat");
    }
};

struct AckReceivedTransition : M::transition
{
    void on_action(RadioContext& ctx) const override
    {
        ctx.retryCount = 0;
        ctx.events.push_back("action:ack-received");
    }
};

struct AckTimeoutRetryTransition : M::transition
{
    bool on_guard(RadioContext& ctx) const override { return ctx.retryCount < RadioContext::MAX_RETRIES; }
    void on_action(RadioContext& ctx) const override
    {
        ctx.retryCount++;
        ctx.events.push_back("action:retry");
    }
};

struct RecalibrateTransition : M::transition
{
    void on_action(RadioContext& ctx) const override
    {
        ctx.recalibrateCount++;
        ctx.events.push_back("action:recalibrate");
    }
};

struct EmergencyLeafTransition : M::transition
{
    bool on_guard(RadioContext& ctx) const override { return ctx.highPowerDraw; }
    void on_action(RadioContext& ctx) const override { ctx.events.push_back("action:emergency-critical"); }
};

struct EmergencyMidTransition : M::transition
{
    bool on_guard(RadioContext& ctx) const override { return ctx.midTransmission; }
    void on_action(RadioContext& ctx) const override { ctx.events.push_back("action:emergency-recoverable-mid"); }
};

// --- events ---------------------------------------------------------------

constexpr int EVT_START_LISTENING = 1, EVT_CONFIGURE = 2, EVT_CONFIG_STEP = 3, EVT_CONFIG_DONE = 4,
              EVT_INTERRUPT_CONFIG = 5, EVT_LOG_CONFIG = 6, EVT_HEARTBEAT = 7, EVT_SEND_REQUEST = 8,
              EVT_PACKET_READY = 9, EVT_SENT = 10, EVT_ACK_RECEIVED = 11, EVT_ACK_TIMEOUT = 12, EVT_RECALIBRATE = 13,
              EVT_EMERGENCY = 14, EVT_RESET = 15, EVT_TIMEOUT = 16, EVT_WAKE_UP = 17, EVT_SOFT_RESET = 18;

// --- wiring ---------------------------------------------------------------

using States =
    M::Root<Composite<RootState, Idle,
                      Composite<Active, Listening, Composite<Configuring, SetFrequency, SetPower, SetSpreadingFactor>,
                                Composite<Transmitting, Preparing, Sending, AwaitingAck>>,
                      Composite<Fault, Recoverable, Critical>, Sleep>>;

using Transitions = M::Transitions<
    M::Transition<Idle, Listening, EVT_START_LISTENING, LogT<"action:start-listening">>,
    M::Transition<Listening, Configuring, EVT_CONFIGURE, LogT<"action:configure">>,
    M::Transition<SetFrequency, SetPower, EVT_CONFIG_STEP, LogT<"action:config-step1">>,
    M::Transition<SetPower, SetSpreadingFactor, EVT_CONFIG_STEP, LogT<"action:config-step2">>,
    M::Transition<SetSpreadingFactor, Listening, EVT_CONFIG_DONE, LogT<"action:config-done">>,
    M::Transition<Configuring, Listening, EVT_INTERRUPT_CONFIG, LogT<"action:interrupt-config">>,
    M::InternalTransition<Configuring, EVT_LOG_CONFIG, LogConfigTransition>,
    M::InternalTransition<RootState, EVT_HEARTBEAT, HeartbeatTransition>,
    M::Transition<Listening, Transmitting, EVT_SEND_REQUEST, LogT<"action:send-request">>,
    M::Transition<Preparing, Sending, EVT_PACKET_READY, LogT<"action:packet-ready">>,
    M::Transition<Sending, AwaitingAck, EVT_SENT, LogT<"action:sent">>,
    M::Transition<AwaitingAck, Listening, EVT_ACK_RECEIVED, AckReceivedTransition>,
    M::Transition<AwaitingAck, Sending, EVT_ACK_TIMEOUT, AckTimeoutRetryTransition>,
    M::Transition<Transmitting, Idle, EVT_ACK_TIMEOUT, LogT<"action:give-up">>,
    M::Transition<AwaitingAck, AwaitingAck, EVT_RECALIBRATE, RecalibrateTransition>,
    M::Transition<AwaitingAck, Critical, EVT_EMERGENCY, EmergencyLeafTransition>,
    M::Transition<Transmitting, Recoverable, EVT_EMERGENCY, EmergencyMidTransition>,
    M::Transition<RootState, Recoverable, EVT_EMERGENCY, LogT<"action:emergency-recoverable-root">>,
    M::Transition<Recoverable, Transmitting, EVT_RESET, LogT<"action:reset-resume">, transition_kind::deep>,
    M::Transition<Critical, Recoverable, EVT_RESET, LogT<"action:should-never-fire", false>>,
    M::Transition<Idle, Sleep, EVT_TIMEOUT, TimeoutT<false, "action:timeout-from-idle">>,
    M::Transition<Active, Sleep, EVT_TIMEOUT, TimeoutT<true, "action:timeout-from-active">>,
    M::Transition<Sleep, Idle, EVT_WAKE_UP, WasActiveGuardT<false, "action:wakeup-idle">>,
    M::Transition<Sleep, Active, EVT_WAKE_UP, WasActiveGuardT<true, "action:wakeup-active-deep">, transition_kind::deep>,
    M::Transition<Sleep, Idle, EVT_SOFT_RESET, WasActiveGuardT<false, "action:softreset-idle">>,
    M::Transition<Sleep, Active, EVT_SOFT_RESET, WasActiveGuardT<true, "action:softreset-active-shallow">,
               transition_kind::shallow>>;

using FSM = M::Tcontroller<States, Transitions>;

// The generated tables must match the hand-authored baseline exactly.
static_assert(States::Size == 16);
static_assert(States::Depth == 4);
static_assert(States::parentTable() == std::array<int, 16>{-1, 0, 0, 2, 2, 4, 4, 4, 2, 8, 8, 8, 0, 12, 12, 0});
static_assert(States::initialStateTable() ==
              std::array<int, 16>{1, -1, 3, -1, 5, -1, -1, -1, 9, -1, -1, -1, 13, -1, -1, -1});

// --- fixture --------------------------------------------------------------

class RadioControllerTest : public ::testing::Test
{
protected:
    RadioContext ctx;
    FSM machine{ctx};

    void driveToAwaitingAck()
    {
        machine.update();
        machine.on_event(EVT_START_LISTENING);
        machine.update();
        machine.on_event(EVT_SEND_REQUEST);
        machine.update();
        machine.on_event(EVT_PACKET_READY);
        machine.update();
        machine.on_event(EVT_SENT);
        machine.update();
        ctx.events.clear();
    }
};

// --- tests ----------------------------------------------------------------

TEST_F(RadioControllerTest, Initialize_EntersRootThenIdle)
{
    machine.update();
    EXPECT_EQ(ctx.events, (std::vector<std::string>{"enter:Root", "enter:Idle"}));
}

TEST_F(RadioControllerTest, StartListening_EntersActiveThenListening)
{
    machine.update();
    ctx.events.clear();

    machine.on_event(EVT_START_LISTENING);
    machine.update();
    EXPECT_EQ(ctx.events,
              (std::vector<std::string>{"exit:Idle", "action:start-listening", "enter:Active", "enter:Listening"}));
}

TEST_F(RadioControllerTest, ConfigWizard_StepsThroughAndCompletes)
{
    machine.update();
    machine.on_event(EVT_START_LISTENING);
    machine.update();
    ctx.events.clear();

    machine.on_event(EVT_CONFIGURE);
    machine.update();
    EXPECT_EQ(
        ctx.events,
        (std::vector<std::string>{"exit:Listening", "action:configure", "enter:Configuring", "enter:SetFrequency"}));
    ctx.events.clear();

    machine.on_event(EVT_CONFIG_STEP);
    machine.update();
    EXPECT_EQ(ctx.events, (std::vector<std::string>{"exit:SetFrequency", "action:config-step1", "enter:SetPower"}));
    ctx.events.clear();

    machine.on_event(EVT_CONFIG_STEP);
    machine.update();
    EXPECT_EQ(ctx.events,
              (std::vector<std::string>{"exit:SetPower", "action:config-step2", "enter:SetSpreadingFactor"}));
    ctx.events.clear();

    machine.on_event(EVT_CONFIG_DONE);
    machine.update();
    EXPECT_EQ(ctx.events,
              (std::vector<std::string>{"exit:SetSpreadingFactor", "exit:Configuring", "action:config-done",
                                        "enter:Listening"}));
}

TEST_F(RadioControllerTest, InterruptConfig_FromMidWizard_ReturnsToListening)
{
    machine.update();
    machine.on_event(EVT_START_LISTENING);
    machine.update();
    machine.on_event(EVT_CONFIGURE);
    machine.update();
    machine.on_event(EVT_CONFIG_STEP);
    machine.update(); // now at SetPower
    ctx.events.clear();

    machine.on_event(EVT_INTERRUPT_CONFIG);
    machine.update();
    EXPECT_EQ(
        ctx.events,
        (std::vector<std::string>{"exit:SetPower", "exit:Configuring", "action:interrupt-config", "enter:Listening"}));
}

TEST_F(RadioControllerTest, LogConfig_InternalDuringWizard_NoStateChange)
{
    machine.update();
    machine.on_event(EVT_START_LISTENING);
    machine.update();
    machine.on_event(EVT_CONFIGURE);
    machine.update();
    ctx.events.clear();

    machine.on_event(EVT_LOG_CONFIG);
    machine.update();
    EXPECT_EQ(ctx.events, (std::vector<std::string>{"action:log-config"}));
    EXPECT_EQ(ctx.configLogCount, 1);
}

TEST_F(RadioControllerTest, Heartbeat_InternalFromAnyLeaf)
{
    machine.update();
    machine.on_event(EVT_HEARTBEAT);
    machine.update();
    EXPECT_EQ(ctx.events.back(), "action:heartbeat");
    EXPECT_EQ(ctx.heartbeatCount, 1);
    ctx.events.clear();

    driveToAwaitingAck();
    machine.on_event(EVT_HEARTBEAT);
    machine.update();
    EXPECT_EQ(ctx.events, (std::vector<std::string>{"action:heartbeat"}));
    EXPECT_EQ(ctx.heartbeatCount, 2);
}

TEST_F(RadioControllerTest, SendRequest_EntersTransmittingThenPreparing)
{
    machine.update();
    machine.on_event(EVT_START_LISTENING);
    machine.update();
    ctx.events.clear();

    machine.on_event(EVT_SEND_REQUEST);
    machine.update();
    EXPECT_EQ(
        ctx.events,
        (std::vector<std::string>{"exit:Listening", "action:send-request", "enter:Transmitting", "enter:Preparing"}));
}

TEST_F(RadioControllerTest, SendCycle_PacketReadyThenSent_ReachesAwaitingAck)
{
    machine.update();
    machine.on_event(EVT_START_LISTENING);
    machine.update();
    machine.on_event(EVT_SEND_REQUEST);
    machine.update();
    ctx.events.clear();

    machine.on_event(EVT_PACKET_READY);
    machine.update();
    EXPECT_EQ(ctx.events, (std::vector<std::string>{"exit:Preparing", "action:packet-ready", "enter:Sending"}));
    ctx.events.clear();

    machine.on_event(EVT_SENT);
    machine.update();
    EXPECT_EQ(ctx.events, (std::vector<std::string>{"exit:Sending", "action:sent", "enter:AwaitingAck"}));
}

TEST_F(RadioControllerTest, AckReceived_ReturnsToListeningAndResetsRetryCount)
{
    driveToAwaitingAck();
    ctx.retryCount = 1;

    machine.on_event(EVT_ACK_RECEIVED);
    machine.update();
    EXPECT_EQ(
        ctx.events,
        (std::vector<std::string>{"exit:AwaitingAck", "exit:Transmitting", "action:ack-received", "enter:Listening"}));
    EXPECT_EQ(ctx.retryCount, 0);
}

TEST_F(RadioControllerTest, AckTimeout_RetriesUpToMaxThenGivesUp)
{
    driveToAwaitingAck();

    // retry 1: retryCount 0 < MAX(2) -> retry, back to Sending
    machine.on_event(EVT_ACK_TIMEOUT);
    machine.update();
    EXPECT_EQ(ctx.events, (std::vector<std::string>{"exit:AwaitingAck", "action:retry", "enter:Sending"}));
    EXPECT_EQ(ctx.retryCount, 1);
    ctx.events.clear();
    machine.on_event(EVT_SENT);
    machine.update();
    ctx.events.clear();

    // retry 2: retryCount 1 < MAX(2) -> retry again
    machine.on_event(EVT_ACK_TIMEOUT);
    machine.update();
    EXPECT_EQ(ctx.events, (std::vector<std::string>{"exit:AwaitingAck", "action:retry", "enter:Sending"}));
    EXPECT_EQ(ctx.retryCount, 2);
    ctx.events.clear();
    machine.on_event(EVT_SENT);
    machine.update();
    ctx.events.clear();

    // retries exhausted: retryCount 2 == MAX(2) -> guard fails -> falls through to Transmitting's row
    machine.on_event(EVT_ACK_TIMEOUT);
    machine.update();
    EXPECT_EQ(ctx.events,
              (std::vector<std::string>{"exit:AwaitingAck", "exit:Transmitting", "exit:Active", "action:give-up",
                                        "enter:Idle"}));
}

TEST_F(RadioControllerTest, Recalibrate_ForcesFullSelfExitReenter)
{
    driveToAwaitingAck();

    machine.on_event(EVT_RECALIBRATE);
    machine.update();
    EXPECT_EQ(ctx.events, (std::vector<std::string>{"exit:AwaitingAck", "action:recalibrate", "enter:AwaitingAck"}));
    EXPECT_EQ(ctx.recalibrateCount, 1);
}

TEST_F(RadioControllerTest, Emergency_ImmediateCriticalWhenHighPowerDraw)
{
    driveToAwaitingAck();
    ctx.highPowerDraw = true;

    machine.on_event(EVT_EMERGENCY);
    machine.update();
    EXPECT_EQ(ctx.events,
              (std::vector<std::string>{"exit:AwaitingAck", "exit:Transmitting", "exit:Active",
                                        "action:emergency-critical", "enter:Fault", "enter:Critical"}));
}

TEST_F(RadioControllerTest, Emergency_FallsThroughToRecoverableWhenMidTransmission)
{
    machine.update();
    machine.on_event(EVT_START_LISTENING);
    machine.update();
    machine.on_event(EVT_SEND_REQUEST);
    machine.update(); // now in Preparing
    ctx.events.clear();
    ctx.midTransmission = true;

    machine.on_event(EVT_EMERGENCY);
    machine.update();
    EXPECT_EQ(ctx.events,
              (std::vector<std::string>{"exit:Preparing", "exit:Transmitting", "exit:Active",
                                        "action:emergency-recoverable-mid", "enter:Fault", "enter:Recoverable"}));
}

TEST_F(RadioControllerTest, Emergency_FallsAllTheWayToRootCatchAll)
{
    driveToAwaitingAck();
    // both highPowerDraw and midTransmission stay false -> AwaitingAck's and Transmitting's
    // guards both fail, Active has no Emergency row at all, only Root's catch-all matches.

    machine.on_event(EVT_EMERGENCY);
    machine.update();
    EXPECT_EQ(ctx.events,
              (std::vector<std::string>{"exit:AwaitingAck", "exit:Transmitting", "exit:Active",
                                        "action:emergency-recoverable-root", "enter:Fault", "enter:Recoverable"}));
}

TEST_F(RadioControllerTest, Reset_FromRecoverable_DeepHistoryResumesExactAwaitingAck)
{
    driveToAwaitingAck();
    machine.on_event(EVT_EMERGENCY);
    machine.update(); // both flags false -> root catch-all -> Recoverable
    ctx.events.clear();

    machine.on_event(EVT_RESET);
    machine.update();
    EXPECT_EQ(ctx.events,
              (std::vector<std::string>{"exit:Recoverable", "exit:Fault", "action:reset-resume", "enter:Active",
                                        "enter:Transmitting", "enter:AwaitingAck"}));
}

TEST_F(RadioControllerTest, Reset_FromCritical_IsPermanentlyBlocked)
{
    driveToAwaitingAck();
    ctx.highPowerDraw = true;
    machine.on_event(EVT_EMERGENCY);
    machine.update(); // -> Critical
    ctx.events.clear();

    machine.on_event(EVT_RESET);
    machine.update();
    EXPECT_TRUE(ctx.events.empty());
}

TEST_F(RadioControllerTest, Timeout_FromIdle_ThenWakeUp_ReturnsToIdle)
{
    machine.update();
    ctx.events.clear();

    machine.on_event(EVT_TIMEOUT);
    machine.update();
    EXPECT_EQ(ctx.events, (std::vector<std::string>{"exit:Idle", "action:timeout-from-idle", "enter:Sleep"}));
    EXPECT_FALSE(ctx.wasActive);
    ctx.events.clear();

    machine.on_event(EVT_WAKE_UP);
    machine.update();
    EXPECT_EQ(ctx.events, (std::vector<std::string>{"exit:Sleep", "action:wakeup-idle", "enter:Idle"}));
}

TEST_F(RadioControllerTest, Timeout_FromActive_ThenWakeUp_DeepHistoryResumesAwaitingAck)
{
    driveToAwaitingAck();

    machine.on_event(EVT_TIMEOUT);
    machine.update();
    EXPECT_EQ(ctx.events,
              (std::vector<std::string>{"exit:AwaitingAck", "exit:Transmitting", "exit:Active",
                                        "action:timeout-from-active", "enter:Sleep"}));
    EXPECT_TRUE(ctx.wasActive);
    ctx.events.clear();

    machine.on_event(EVT_WAKE_UP);
    machine.update();
    EXPECT_EQ(ctx.events,
              (std::vector<std::string>{"exit:Sleep", "action:wakeup-active-deep", "enter:Active", "enter:Transmitting",
                                        "enter:AwaitingAck"}));
}

TEST_F(RadioControllerTest, Timeout_FromActive_ThenSoftReset_ShallowHistoryDefaultsToPreparing)
{
    driveToAwaitingAck();
    machine.on_event(EVT_TIMEOUT);
    machine.update();
    ctx.events.clear();

    machine.on_event(EVT_SOFT_RESET);
    machine.update();
    EXPECT_EQ(ctx.events,
              (std::vector<std::string>{"exit:Sleep", "action:softreset-active-shallow", "enter:Active",
                                        "enter:Transmitting", "enter:Preparing"}));
}

TEST_F(RadioControllerTest, Update_IncrementsAwaitingAckTicksOnly_NotOtherStates)
{
    machine.update();
    machine.update();
    machine.update();
    EXPECT_EQ(ctx.awaitingAckTicks, 0); // Idle's on_update is the default no-op

    driveToAwaitingAck();
    machine.update();
    machine.update();
    machine.update();
    EXPECT_EQ(ctx.awaitingAckTicks, 3);
}
