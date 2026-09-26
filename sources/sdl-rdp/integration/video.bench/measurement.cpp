#include <sdl-rdp/configuration/setup.hpp>
#include <sdl-rdp/headless-client.test/backend/await-acknowledged.hpp>
#include <sdl-rdp/headless-client.test/backend/instance.hpp>
#include <sdl-rdp/headless-client.test/backend/status.hpp>
#include <sdl-rdp/headless-client.test/frame/pattern.hpp>
#include <sdl-rdp/headless-client.test/graphics/round-five.hpp>
#include <sdl-rdp/integration/support.bench/session.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/geometry.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <sdl-rdp/utilities/stopwatch.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <optional>
#include <vector>

namespace sdl_rdp::integration::video_bench::detail::measurement {
using sdl_rdp::configuration::Codec;
using sdl_rdp::headless_client_test::backend::AllAcknowledged;
using sdl_rdp::headless_client_test::backend::AwaitAllAcknowledged;
using sdl_rdp::headless_client_test::backend::CurrentStatus;
using sdl_rdp::headless_client_test::backend::RequiredGraphics;
using sdl_rdp::headless_client_test::backend::RequiredStatus;
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::client::Clock;
using sdl_rdp::headless_client_test::client::Pixels;
using sdl_rdp::headless_client_test::frame::GraphicsScene;
using sdl_rdp::headless_client_test::graphics::RoundFive;
using sdl_rdp::integration::support_bench::Check;
using sdl_rdp::integration::support_bench::Fail;
using sdl_rdp::integration::support_bench::Measured;
using sdl_rdp::integration::support_bench::OneSession;
using sdl_rdp::integration::support_bench::Session;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::Rect;
using sdl_rdp::utilities::Timed;
using sdl_rdp::utilities::Unreachable;
struct FrameCost {
  std::uint32_t maximum_error = 0;
  double        latency_ms    = 0;
};
enum class Scene{ MovingBlock, Noise };

namespace {
auto ScenePixels(Scene scene, std::uint32_t frame) -> Pixels {
  return GraphicsScene(frame, scene == Scene::Noise);
}
auto Damage(Scene scene, std::uint32_t frame) -> Rect {
  switch (scene) {
  case Scene::MovingBlock: return { .x = Narrowed<int>(frame - 1), .y = 40, .w = 33, .h = 32 };
  case Scene::Noise:       return { .x = 0, .y = 0, .w = 640, .h = 480 };
  default:                 Unreachable(scene);
  }
}
auto Tolerance(Scene scene) -> std::uint32_t {
  switch (scene) {
  case Scene::MovingBlock: return 24;
  case Scene::Noise:       return 48;
  default:                 Unreachable(scene);
  }
}
}

class GraphicsMeasurement : public Session<RoundFive> {
public:
  using Session::Session;
protected:
  auto MeasureCodec(Codec codec, Scene scene) -> void;

private:
  auto Prepared(Client& client, Codec codec, Scene scene)     -> bool;
  auto MeasuredFrames(Client& client, Scene scene)            -> std::optional<FrameCost>;
  auto RecordFrames(Client& client, Codec codec, Scene scene) -> void;
  auto RecordGraphicsTiming(Client& client)                   -> void;
  auto EncodeDuration()                                       -> std::chrono::nanoseconds;
};
template <Codec CODEC, Scene SCENE>
class CodecMeasurement final : public GraphicsMeasurement {
public:
  using GraphicsMeasurement::GraphicsMeasurement;
  auto TestBody() -> void override {
    MeasureCodec(CODEC, SCENE);
  }
};
using ProgressiveMovingBlock = CodecMeasurement<Codec::Progressive, Scene::MovingBlock>;
using RemoteFxMovingBlock    = CodecMeasurement<Codec::RemoteFx, Scene::MovingBlock>;
using ProgressiveNoise       = CodecMeasurement<Codec::Progressive, Scene::Noise>;
using RemoteFxNoise          = CodecMeasurement<Codec::RemoteFx, Scene::Noise>;
BENCHMARK(Measured<ProgressiveMovingBlock>)->Apply(OneSession);
BENCHMARK(Measured<RemoteFxMovingBlock>)->Apply(OneSession);
BENCHMARK(Measured<ProgressiveNoise>)->Apply(OneSession);
BENCHMARK(Measured<RemoteFxNoise>)->Apply(OneSession);

auto GraphicsMeasurement::MeasureCodec(Codec codec, Scene scene) -> void {
  if (!Passes([&] { Open(640, 480, { }, codec); })) return;
  Client client(backend.Port(), true, 640, 480);
  if (Prepared(client, codec, scene)) RecordFrames(client, codec, scene);
}
auto GraphicsMeasurement::Prepared(Client& client, Codec codec, Scene scene) -> bool {
  auto const progressive = codec == Codec::Progressive;
  if (progressive) client.EnableGraphics({ .qoe_acknowledgements = true });
  if (!Passes([&] { Connect(client); })) return false;
  if (progressive && !Check(client.Until([this] { return logs.Contains("GFX confirmed"); }), "the client confirms GFX"))
    return false;
  return Passes([&] { Present(ScenePixels(scene, 0), 640, 480); },
                [&] { AwaitAllAcknowledged(client, backend, logs); });
}
auto GraphicsMeasurement::MeasuredFrames(Client& client, Scene scene) -> std::optional<FrameCost> {
  FrameCost cost;
  for (std::uint32_t frame = 1; frame <= 20; ++frame) {
    auto const pixels    = ScenePixels(scene, frame);
    auto const presented = Clock::now();
    if (!Passes([&] { backend.Present(pixels, 640, 480, Damage(scene, frame)); })) return std::nullopt;
    if (!client.Until([this] { return AllAcknowledged(*backend); })) {
      Fail(std::format("frame {} is acknowledged: {}", frame, logs.Text(true)));
      return std::nullopt;
    }
    cost.latency_ms    += std::chrono::duration<double, std::milli>(Clock::now() - presented).count();
    cost.maximum_error =  std::max(cost.maximum_error, client.MaxError(pixels));
  }
  return cost;
}
auto GraphicsMeasurement::RecordFrames(Client& client, Codec codec, Scene scene) -> void {
  auto const               initial_encode = EncodeDuration();
  auto const               initial_bytes  = client.Received();
  std::optional<FrameCost> cost;
  auto const               span           = Timed([&] { cost = MeasuredFrames(client, scene); });
  if (!cost) return;
  Measure(span);
  auto const elapsed   = std::chrono::duration<double>(span).count();
  auto const bytes     = static_cast<double>(client.Received() - initial_bytes);
  auto const encode_ms = std::chrono::duration<double, std::milli>(EncodeDuration() - initial_encode).count();
  Record("wire_MB_per_second", bytes / elapsed / 1000000);
  Record("wire_MB_per_second_at_60fps", bytes * 3.0 / 1000000);
  Record("encode_ms_per_frame", encode_ms / 20);
  Record("present_ack_ms_per_frame", cost->latency_ms / 20);
  Record("maximum_channel_error", cost->maximum_error);
  if (codec == Codec::Progressive) RecordGraphicsTiming(client);
  Check(cost->maximum_error <= Tolerance(scene), "the codec stays within its channel error");
}
auto GraphicsMeasurement::RecordGraphicsTiming(Client& client) -> void {
  auto const qoe_arrived = client.Until([this] {
    auto const status = CurrentStatus(*backend);
    return status.has_value() && status->graphics.has_value() && status->graphics->qoe.frameId == status->frame;
  });
  if (!Check(qoe_arrived, "the client acknowledges the last frame's QoE")) return;
  auto const timing = RequiredGraphics(*backend);
  Record("activation_to_gfx_ms", std::chrono::duration<double, std::milli>(timing.ready_time).count());
  Record("client_decode_ms", timing.qoe.timeDiffSE);
  Record("client_render_ms", timing.qoe.timeDiffEDR);
  Record("client_qoe_frame", timing.qoe.frameId);
  Check(!logs.Contains("GFX QoE"), "no QoE diagnostic is logged");
}
auto GraphicsMeasurement::EncodeDuration() -> std::chrono::nanoseconds {
  return RequiredStatus(*backend).encode_time;
}
}
