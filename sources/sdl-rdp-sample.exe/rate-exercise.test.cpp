#include "_detail/rate-exercise.hpp"
#include "support.test/sample.hpp"
#include "_detail/bounded-connect.hpp"
#include "_detail/trace-number.hpp"

#include <sdl-rdp-backend.so/_detail/display-client.hpp>
#include <sdl-rdp-backend.so/_detail/frame-observer.hpp>
#include <string>
#include <utility>

namespace SampleGate {
namespace {
struct Pace {
  int64_t  milliseconds = 0;
  unsigned presents     = 0;
};
struct RateTrace {
  std::size_t cursor               = 0;
  unsigned    blocked              = 0;
  unsigned    floor_blocked        = 0;
  unsigned    timeouts             = 0;
  unsigned    floor_timeouts       = 0;
  unsigned    presents             = 0;
  unsigned    frames               = 0;
  unsigned    acknowledgements     = 0;
  unsigned    floors               = 0;
  unsigned    ceilings             = 0;
  unsigned    pressure_frames      = 0;
  unsigned    presents_at_send     = 0;
  int64_t     sent_id              = -1;
  int64_t     time                 = 0;
  int64_t     rate                 = 60;
  bool        pressure             = false;
  bool        floor_after_pressure = false;
  bool        recovered            = false;
  Pace        floor_pace;
  Pace        recovered_pace;
};
auto ObserveRefresh(RateTrace& trace, std::string const& line) -> void {
  if (!line.contains("trace refresh ")) return;
  trace.rate = TraceNumber(line, " hz=");
  EXPECT_GE(trace.rate, 10);
  EXPECT_LE(trace.rate, 60);
  if (trace.rate == 10 && ++trace.floors == 1) {
    trace.floor_after_pressure = trace.pressure;
    trace.floor_blocked        = trace.blocked;
    trace.floor_timeouts       = trace.timeouts;
  }
  if (trace.rate != 60) return;
  ++trace.ceilings;
  trace.recovered |= trace.floors != 0;
}
auto CurrentPace(RateTrace& trace) -> Pace* {
  if (trace.recovered) return &trace.recovered_pace;
  if (trace.floors && trace.rate == 10) return &trace.floor_pace;
  return nullptr;
}
auto ObserveFrame(RateTrace& trace, std::string const& line) -> void {
  ++trace.frames;
  trace.sent_id          = TraceNumber(line, " id=");
  trace.presents_at_send = trace.presents;
  if (trace.pressure && !trace.floors) ++trace.pressure_frames;
  trace.pressure |= TraceNumber(line, " outq=") > TraceNumber(line, " bytes=");
}
auto ObserveRate(RateTrace& trace, std::string const& line) -> void {
  if (!line.contains("trace ")) return;
  auto       time    = TraceNumber(line, " t=");
  bool const present = line.contains("trace present ");
  auto*      pace    = CurrentPace(trace);
  if (pace && trace.time) pace->milliseconds += time - trace.time;
  if (pace) pace->presents += present;
  trace.time             =  time;
  trace.presents         += present;
  trace.acknowledgements += line.contains("trace ack ");
  trace.timeouts         += line.contains("trace ack-timeout ");
  trace.blocked          += line.contains("trace wire-blocked ");
  if (line.contains("trace frame ")) ObserveFrame(trace, line);
  ObserveRefresh(trace, line);
}
auto AwaitTrace(Headless::Logs& logs, RateTrace& trace, auto ready) -> void {
  auto cursor = logs.WaitAfter(trace.cursor, [&](auto const& line) { ObserveRate(trace, line); }, ready);
  ASSERT_TRUE(cursor.has_value()) << "trace event deadline\n" << logs.Text(true);
  if (cursor) trace.cursor = *cursor;
}
auto ConnectRateClient(Client& client, Backend::RefreshMode mode) -> void {
  if (mode == Backend::RefreshMode::Sender) {
    auto callbacks = *freerdp_get_io_callbacks(client.Instance()->context);
    callbacks.TCPConnect = BoundedConnect;
    freerdp_set_io_callbacks(client.Instance()->context, &callbacks);
  }
  ASSERT_TRUE(freerdp_connect(client.Instance().get()));
}
auto PumpUntil(Client& client, Headless::Logs& logs, RateTrace& trace, auto respond, auto ready) -> void {
  ASSERT_TRUE(client.Until([&] {
    trace.cursor = logs.Follow(trace.cursor, [&](auto const& line) { ObserveRate(trace, line); });
    if (ready()) return true;
    respond();
    return false;
  }, 20s)) << "trace event deadline\n" << logs.Text(true);
}
auto DrainUntil(Client& client, Headless::FrameObserver& frames, Headless::Logs& logs, RateTrace& trace,
                auto ready) -> void {
  std::size_t acknowledged = 0;
  PumpUntil(client, logs, trace, [&] {
    if (frames.Frames().size() == acknowledged) return;
    EXPECT_TRUE(frames.Ack());
    acknowledged = frames.Frames().size();
  }, ready);
}
auto HoldUntil(Client& client, Headless::Logs& logs, RateTrace& trace, auto ready) -> void {
  PumpUntil(client, logs, trace, [] { }, ready);
}
auto PauseForPressure(Headless::Logs& logs, RateTrace& trace) -> void {
  AwaitTrace(logs, trace, [&] { return trace.floors && trace.blocked >= trace.floor_blocked + 200; });
  if (::testing::Test::HasFatalFailure()) return;
  ASSERT_TRUE(trace.floor_after_pressure);
  EXPECT_LE(trace.pressure_frames, 5u);
  testing::Test::RecordProperty("frames_to_floor", trace.pressure_frames);
}
auto HeldLongEnough(RateTrace const& trace, Headless::FrameObserver const& frames) -> bool {
  // SDL paces presents at the published refresh: six outlast the 2/60 s the estimator reads as a slow client.
  return !frames.Frames().empty() && std::cmp_equal(frames.Frames().back(), trace.sent_id) &&
         trace.presents >= trace.presents_at_send + 6;
}
auto DelayUntilFloor(Client& client, Headless::FrameObserver& frames, Headless::Logs& logs, RateTrace& trace) -> void {
  int64_t acknowledged = -1;
  PumpUntil(client, logs, trace, [&] {
    if (!HeldLongEnough(trace, frames) || acknowledged == trace.sent_id) return;
    EXPECT_TRUE(frames.Ack());
    acknowledged = trace.sent_id;
  }, [&] { return trace.floors > 0; });
  if (::testing::Test::HasFatalFailure()) return;
  HoldUntil(client, logs, trace, [&] { return trace.timeouts > trace.floor_timeouts; });
}
auto ReachFloor(Client& client, Headless::FrameObserver& frames, Headless::Logs& logs, RateTrace& trace,
                Backend::RefreshMode mode) -> void {
  switch (mode) {
  case Backend::RefreshMode::Client:
  case Backend::RefreshMode::Average:
    DelayUntilFloor(client, frames, logs, trace);
    break;
  case Backend::RefreshMode::Sender:
    PauseForPressure(logs, trace);
    break;
  case Backend::RefreshMode::Fixed:
  default:
    utilities::Unreachable(mode);
  }
}
auto PresentsPerSecond(Pace const& pace) -> double {
  Expects(pace.milliseconds > 0, "the published rate held for a measurable interval");
  return pace.presents * 1000.0 / double(pace.milliseconds);
}
auto ThenPresentRecovery(RateTrace const& trace) -> void {
  ASSERT_GT(trace.floors, 0u);
  ASSERT_TRUE(trace.recovered);
  ASSERT_GE(trace.floor_pace.presents, 3u);
  ASSERT_GE(trace.recovered_pace.presents, 60u);
  auto slow = PresentsPerSecond(trace.floor_pace);
  auto fast = PresentsPerSecond(trace.recovered_pace);
  EXPECT_LT(slow, fast * 0.85) << "floor presents=" << trace.floor_pace.presents
                              << " recovered presents=" << trace.recovered_pace.presents;
  testing::Test::RecordProperty("present_recovery_ratio", slow / fast);
  testing::Test::RecordProperty("floor_publications", trace.floors);
  testing::Test::RecordProperty("ceiling_publications", trace.ceilings);
}
auto ResizeRateClient(Client& client, Headless::DisplayClient& display, unsigned width) -> void {
  ASSERT_TRUE(display.Layout(width, 480));
  ASSERT_TRUE(client.Until([&] { return std::cmp_equal(client.Instance()->context->gdi->width, width); }));
}
auto AwaitRateClient(Client& client, Headless::FrameObserver& frames) -> void {
  ASSERT_TRUE(client.Until([&] { return Headless::DisplayClient::Ready() && !frames.Frames().empty(); }));
  ASSERT_TRUE(frames.Ack());
}
auto ThenFixedRate(RateTrace const& trace, unsigned held) -> void {
  auto resumed = trace.presents - held;
  EXPECT_GT(held * 8, resumed);
  EXPECT_LT(held, resumed * 8);
  EXPECT_EQ(trace.floors, 0u);
  EXPECT_EQ(trace.ceilings, 0u);
  EXPECT_EQ(trace.rate, 60);
}
auto FixedRate(Client& client, Headless::FrameObserver& frames, Headless::DisplayClient& display,
               Headless::Logs& logs, RateTrace& trace) -> void {
  HoldUntil(client, logs, trace, [&] { return trace.timeouts > 0; });
  if (::testing::Test::HasFatalFailure()) return;
  auto held = trace.presents;
  ResizeRateClient(client, display, 800);
  if (::testing::Test::HasFatalFailure()) return;
  auto target = trace.frames + 60;
  DrainUntil(client, frames, logs, trace, [&] { return trace.frames >= target; });
  if (::testing::Test::HasFatalFailure()) return;
  ThenFixedRate(trace, held);
}
auto RecoverToCeiling(Client& client, Headless::FrameObserver& frames, Headless::Logs& logs, RateTrace& trace) -> void {
  DrainUntil(client, frames, logs, trace, [&] { return trace.recovered && trace.recovered_pace.presents >= 60; });
  if (::testing::Test::HasFatalFailure()) return;
  ThenPresentRecovery(trace);
}
}
auto ExerciseRate(unsigned port, Headless::Logs& logs, Backend::RefreshMode mode, RateRecovery recovery) -> void {
  Expects(port > 0, "sample listener is open");
  Client                  client(port, true, 640, 480);
  Headless::DisplayClient display(client);
  RateTrace               trace  { .cursor = logs.Entries().size() };
  ConnectRateClient(client, mode);
  if (::testing::Test::HasFatalFailure()) return;
  Headless::FrameObserver frames(client);
  ASSERT_NO_FATAL_FAILURE(AwaitRateClient(client, frames));
  if (mode == Backend::RefreshMode::Fixed) {
    FixedRate(client, frames, display, logs, trace);
    return;
  }
  ReachFloor(client, frames, logs, trace, mode);
  if (::testing::Test::HasFatalFailure()) return;
  if (recovery == RateRecovery::AfterResize || mode == Backend::RefreshMode::Average)
    ResizeRateClient(client, display, 800);
  if (::testing::Test::HasFatalFailure()) return;
  RecoverToCeiling(client, frames, logs, trace);
}
}
