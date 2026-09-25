#include <sdl-rdp/headless-client.test/client/clipboard.hpp>
#include <sdl-rdp/headless-client.test/client/sound.hpp>
#include <sdl-rdp/headless-client.test/frame/observer.hpp>
#include <sdl-rdp/headless-client.test/utilities/octets.hpp>
#include <sdl-rdp/headless-client.test/utilities/wall-milliseconds.hpp>
#include <sdl-rdp/integration/support.bench/session.hpp>
#include <sdl-rdp/sample-gate.test/process/process.hpp>
#include <sdl-rdp/sample-gate.test/process/trace-number.hpp>
#include <sdl-rdp/sample-gate.test/sample/sample.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/deadline.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <algorithm>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <ranges>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace sdl_rdp::integration::sample_bench::detail::latency {
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::client::ClipboardClient;
using sdl_rdp::headless_client_test::client::Clock;
using sdl_rdp::headless_client_test::client::KeyState;
using sdl_rdp::headless_client_test::client::SoundClient;
using sdl_rdp::headless_client_test::frame::FrameObserver;
using sdl_rdp::headless_client_test::utilities::UnicodeText;
using sdl_rdp::headless_client_test::utilities::WallMilliseconds;
using sdl_rdp::integration::support_bench::Check;
using sdl_rdp::integration::support_bench::Measured;
using sdl_rdp::integration::support_bench::OneSession;
using sdl_rdp::integration::support_bench::Session;
using sdl_rdp::sample_gate_test::process::Process;
using sdl_rdp::sample_gate_test::process::TraceNumber;
using sdl_rdp::sample_gate_test::sample::Sample;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::Throughout;
using namespace std::chrono_literals;
namespace {
auto DrainTrace(Process& process, std::stop_token const& stop) -> void {
  std::string output;
  while (!stop.stop_requested()) process.Line(output, Clock::now() + 10ms);
}
auto TraceTimes(std::string_view trace, std::string_view prefix, std::predicate<std::string_view> auto measured)
    -> std::vector<std::int64_t> {
  auto const traced = [&](std::string_view line) { return line.contains(prefix) && measured(line); };
  return trace | std::views::split('\n') | std::views::transform([](auto line) { return std::string_view(line); })
         | std::views::filter(traced)
         | std::views::transform([&](std::string_view line) { return TraceNumber(line, prefix); })
         | std::ranges::to<std::vector>();
}
auto KeyA(std::string_view line) -> bool {
  return line.contains(" code=30 ");
}
auto Every(std::string_view /*line*/) -> bool {
  return true;
}
auto Percentile95(std::vector<std::int64_t> latency) -> std::int64_t {
  Expects(!latency.empty(), "latency was sampled");
  std::ranges::sort(latency);
  return latency[((latency.size() * 95 + 99) / 100) - 1];
}
}

class InputAndClipboardUnderTightVideo final : public Session<Sample> {
public:
  using Session::Session;
  auto TestBody() -> void override;

private:
  auto        Exercised()                                                                    -> bool;
  static auto MediaReady(Client& client, FrameObserver& frames, SoundClient const& audio,
                         ClipboardClient const& clipboard) -> bool;
  auto        Sampled(Client& client, FrameObserver& frames, ClipboardClient& clipboard)     -> bool;
  static auto PumpedUntil(Client& client, FrameObserver& frames, Clock::time_point deadline) -> bool;
  auto        Sent(Client& client, ClipboardClient& clipboard, std::size_t index)            -> bool;
  auto        ThenLatency(std::string_view event, std::predicate<std::string_view> auto measured,
                          std::span<std::int64_t const> sent) -> void;

  std::vector<std::int64_t> _keys;
  std::vector<std::int64_t> _clips;
};
BENCHMARK(Measured<InputAndClipboardUnderTightVideo>)->Apply(OneSession);

auto InputAndClipboardUnderTightVideo::TestBody() -> void {
  Expects(process == nullptr, "sample has not started");
  auto const launched = Holds([this] {
    GivenAudioProcess({ "SDL_AUDIO_DRIVER=rdp", "SDL_RDP_TRACE=1", "SDL_LOGGING=video=info" }, { "--tone", "--tight" });
  });
  if (!launched || !Exercised()) return;
  ThenLatency("key", KeyA, _keys);
  ThenLatency("clipboard", Every, _clips);
}
// The drain thread joins on return, so the transcript is complete before it is read.
auto InputAndClipboardUnderTightVideo::Exercised() -> bool {
  std::jthread const drain([this](std::stop_token const& stop) { DrainTrace(*process, stop); });
  Client             client(audio_port, true, 640, 480);
  ClipboardClient    clipboard(client);
  SoundClient const  audio(client);
  if (!Holds([&] { Connect(client); })) return false;
  FrameObserver frames(client);
  if (!MediaReady(client, frames, audio, clipboard)) return false;
  auto const before = frames.Frames().size();
  if (!Sampled(client, frames, clipboard)) return false;
  Check(frames.Frames().size() - before >= 60, "video keeps presenting under input");
  return Holds([&] { Escape(client); });
}
auto InputAndClipboardUnderTightVideo::MediaReady(Client& client, FrameObserver& frames, SoundClient const& audio,
                                                  ClipboardClient const& clipboard) -> bool {
  return Check(client.Until([&] {
    if (!frames.Frames().empty()) frames.Ack();
    return !audio.CaptureState().received.empty() && clipboard.Observed().accepted.load() > 0;
  }),
               "audio and the clipboard reach the client");
}
auto InputAndClipboardUnderTightVideo::Sampled(Client& client, FrameObserver& frames, ClipboardClient& clipboard)
    -> bool {
  auto const start = Clock::now();
  auto const sent  = [&](std::size_t index) {
    return PumpedUntil(client, frames, start + index * 50ms) && Sent(client, clipboard, index);
  };
  return std::ranges::all_of(std::views::iota(0uz, 60uz), sent) && PumpedUntil(client, frames, start + 3s);
}
auto InputAndClipboardUnderTightVideo::PumpedUntil(Client& client, FrameObserver& frames, Clock::time_point deadline)
    -> bool {
  auto const pumped = [&] {
    return Check(client.Pump(1), "the client pumps")
           && (frames.Frames().empty() || Check(frames.Ack(), "the client acknowledges the frame"));
  };
  return Throughout(deadline, pumped);
}
auto InputAndClipboardUnderTightVideo::Sent(Client& client, ClipboardClient& clipboard, std::size_t index) -> bool {
  _keys.push_back(WallMilliseconds());
  if (!Check(client.Key(0x1e, index % 2 == 0 ? KeyState::Down : KeyState::Up), "the key is sent")) return false;
  _clips.push_back(WallMilliseconds());
  return Check(clipboard.Offer(UnicodeText(std::string{ Narrowed<char>('A' + index) })), "the clipboard offer is sent");
}
auto InputAndClipboardUnderTightVideo::ThenLatency(std::string_view event,
                                                   std::predicate<std::string_view> auto measured,
                                                   std::span<std::int64_t const> sent) -> void {
  auto const received = TraceTimes(process->Transcript(), std::format("trace {} t=", event), measured);
  if (!Check(received.size() == sent.size(), std::format("every {} event is traced", event))) return;
  auto const latency = std::views::zip_transform(std::minus{ }, received, sent) | std::ranges::to<std::vector>();
  if (!Check(std::ranges::min(latency) >= 0, std::format("no {} event is traced before it is sent", event))) return;
  auto const p95 = Percentile95(latency);
  Record(std::format("{}_p95_ms", event), static_cast<double>(p95));
  Check(p95 < 40, std::format("{} p95 is under 40 ms", event));
}
}
