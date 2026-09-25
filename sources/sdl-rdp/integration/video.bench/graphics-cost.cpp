#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/headless-client.test/backend/await-acknowledged.hpp>
#include <sdl-rdp/headless-client.test/backend/instance.hpp>
#include <sdl-rdp/headless-client.test/graphics/cost.hpp>
#include <sdl-rdp/integration/support.bench/session.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/video/avc/encoder.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sdl_rdp::integration::video_bench::detail::graphics_cost {
using sdl_rdp::headless_client_test::backend::AwaitAllAcknowledged;
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::client::Pixels;
using sdl_rdp::headless_client_test::graphics::GraphicsCost;
using sdl_rdp::headless_client_test::graphics::GraphicsObserver;
using sdl_rdp::integration::support_bench::Check;
using sdl_rdp::integration::support_bench::Fail;
using sdl_rdp::integration::support_bench::Measured;
using sdl_rdp::integration::support_bench::OneSession;
using sdl_rdp::integration::support_bench::Session;
using sdl_rdp::integration::support_bench::Skip;
using sdl_rdp::utilities::Expects;
using sdl_rdp::video::avc::Encoder;
constexpr std::size_t CostGroups = 6;
struct CostLine {
  std::vector<std::string> groups;
  double                   encode_ms = 0;
};

class GraphicsCostSession : public Session<GraphicsCost> {
public:
  using Session::Session;

protected:
  auto EncodeCost(std::string_view pattern) -> std::optional<CostLine>;
};
class FullRandomFrame final : public GraphicsCostSession {
public:
  using GraphicsCostSession::GraphicsCostSession;
  auto TestBody() -> void override;

private:
  auto Connected(Client& client)                                             -> bool;
  auto PresentedRandomFrame(Client& client)                                  -> bool;
  auto ThenProgressiveCost(Client& client, GraphicsObserver const& observer) -> void;
};
class AvcFullFrame final : public GraphicsCostSession {
public:
  using GraphicsCostSession::GraphicsCostSession;
  auto TestBody() -> void override;

private:
  auto PresentedTiles(Client& client, GraphicsObserver const& observer) -> bool;
  auto ThenAvcCost()                                                    -> void;
};
BENCHMARK(Measured<FullRandomFrame>)->Apply(OneSession);
BENCHMARK(Measured<AvcFullFrame>)->Apply(OneSession);

// Group 1 of the pattern is the encode mean, recorded as encode_ms; group 0, the whole line, is the label.
auto GraphicsCostSession::EncodeCost(std::string_view pattern) -> std::optional<CostLine> {
  auto statistics = logs.Statistics(pattern);
  if (!statistics) {
    Fail(std::format("the statistics line is logged: {}", logs.Text(true)));
    return std::nullopt;
  }
  Expects(statistics->size() == CostGroups, "the cost pattern captures five numbers");
  auto const encode_ms = Recorded("encode_ms", (*statistics)[1]);
  if (!encode_ms) return std::nullopt;
  Label((*statistics)[0]);
  return CostLine{ .groups = *std::move(statistics), .encode_ms = *encode_ms };
}

auto FullRandomFrame::TestBody() -> void {
  if (!Holds([this] { Open(); })) return;
  Client client(sdlrdp_port(&*backend), true, 1280, 800);
  client.EnableGraphics();
  GraphicsObserver const observer(client);
  if (Connected(client) && PresentedRandomFrame(client)) ThenProgressiveCost(client, observer);
}
auto FullRandomFrame::Connected(Client& client) -> bool {
  if (!Check(client.Connect(), "the client connects")) return false;
  return Check(client.Until([this] { return logs.Contains("GFX confirmed"); }), "the client confirms GFX");
}
auto FullRandomFrame::PresentedRandomFrame(Client& client) -> bool {
  Pixels       pixels(1280uz * 800);
  std::mt19937 random(17);            // NOLINT(cert-msc32-c, cert-msc51-cpp): Reproducible codec input.
  std::ranges::generate(pixels, [&] { return random() & 0x00ffffff; });
  sdlrdp_rect const full{ 0, 0, 1280, 800 };
  return Check(backend.Present(pixels, 1280, 800, full) == 0, "the backend presents")
         && Holds([&] { AwaitAllAcknowledged(client, backend, logs); });
}
auto FullRandomFrame::ThenProgressiveCost(Client& client, GraphicsObserver const& observer) -> void {
  if (!Check(observer.Observed().frames.size() == 1, "the client observes one frame")) return;
  client.Disconnect();
  backend.Close();
  auto const cost = EncodeCost(R"(Frames: 1 sent, 0 coalesced; encode ([0-9.]+) ms mean, ([0-9.]+) ms max; )"
                               R"(acknowledgement ([0-9.]+) ms mean, ([0-9.]+) ms max, ([0-9]+) over 100 ms, )"
                               R"([0-9]+ timed out\.)");
  if (!cost) return;
  Check(logs.Count(SDLRDP_LOG_INFO, "Frames:") == 1, "one statistics line is logged");
  Check(cost->groups[1] == cost->groups[2], "one frame's encode mean is its maximum");
  Check(cost->groups[3] == cost->groups[4], "one frame's acknowledgement mean is its maximum");
  Check(cost->encode_ms > 0.0, "encoding takes time");
}

auto AvcFullFrame::TestBody() -> void {
  if (!Encoder::Available()) {
    Skip(Encoder::UnavailableReason());
    return;
  }
  if (!Holds([this] { Open(1920, 1080, SDLRDP_CODEC_AVC420); })) return;
  Client client(sdlrdp_port(&*backend), true, 1920, 1080);
  client.EnableGraphics({ .h264 = true });
  GraphicsObserver const observer(client);
  if (!PresentedTiles(client, observer)) return;
  client.Disconnect();
  backend.Close();
  ThenAvcCost();
}
auto AvcFullFrame::PresentedTiles(Client& client, GraphicsObserver const& observer) -> bool {
  return Holds([&] { ConnectGraphics(client); }, [&] { PresentMovingTiles(client, 10); })
         && Check(observer.Observed().avc_nals.size() == 10, "every frame is one AVC NAL unit")
         && Check(observer.Observed().frames.size() == 10, "the client observes every frame");
}
auto AvcFullFrame::ThenAvcCost() -> void {
  auto const cost = EncodeCost(R"(Frames: 10 sent, 0 coalesced; encode ([0-9.]+) ms mean, ([0-9.]+) ms max )"
                               R"(\(convert ([0-9.]+), upload ([0-9.]+), nvenc ([0-9.]+)\); acknowledgement)");
  if (!cost) return;
  auto const& groups = cost->groups;
  if (!Recorded("convert_ms", groups[3]) || !Recorded("upload_ms", groups[4]) || !Recorded("nvenc_ms", groups[5]))
    return;
  // measured 2026-09-23 on an RTX 3090 at 1920x1080: 8.8 ms mean, 16.4 ms max after the row-copy dispatch (45.2 before)
  Check(cost->encode_ms < 20.0, "AVC encodes a 1080p frame within 20 ms");
}
}
