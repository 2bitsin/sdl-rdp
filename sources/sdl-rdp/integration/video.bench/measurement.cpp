#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/headless-client.test/backend/await-acknowledged.hpp>
#include <sdl-rdp/headless-client.test/backend/instance.hpp>
#include <sdl-rdp/headless-client.test/backend/status.hpp>
#include <sdl-rdp/headless-client.test/frame/pattern.hpp>
#include <sdl-rdp/headless-client.test/graphics/round-five.hpp>
#include <sdl-rdp/integration/support.bench/session.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <sdl-rdp/utilities/stopwatch.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <optional>
#include <vector>

namespace sdl_rdp::integration::video_bench::detail::measurement {
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
auto Damage(Scene scene, std::uint32_t frame) -> sdlrdp_rect {
  switch (scene) {
  case Scene::MovingBlock: return { Narrowed<int>(frame - 1), 40, 33, 32 };
  case Scene::Noise:       return { 0, 0, 640, 480 };
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
  auto MeasureCodec(sdlrdp_codec codec, Scene scene) -> void;

private:
  auto Prepared(Client& client, sdlrdp_codec codec, Scene scene)     -> bool;
  auto MeasuredFrames(Client& client, Scene scene)                   -> std::optional<FrameCost>;
  auto RecordFrames(Client& client, sdlrdp_codec codec, Scene scene) -> void;
  auto RecordGraphicsTiming(Client& client)                          -> void;
  auto EncodeDuration()                                              -> std::chrono::nanoseconds;
};
template <sdlrdp_codec CODEC, Scene SCENE>
class CodecMeasurement final : public GraphicsMeasurement {
public:
  using GraphicsMeasurement::GraphicsMeasurement;
  auto TestBody() -> void override {
    MeasureCodec(CODEC, SCENE);
  }
};
using ProgressiveMovingBlock = CodecMeasurement<SDLRDP_CODEC_PROGRESSIVE, Scene::MovingBlock>;
using RemoteFxMovingBlock    = CodecMeasurement<SDLRDP_CODEC_REMOTEFX, Scene::MovingBlock>;
using ProgressiveNoise       = CodecMeasurement<SDLRDP_CODEC_PROGRESSIVE, Scene::Noise>;
using RemoteFxNoise          = CodecMeasurement<SDLRDP_CODEC_REMOTEFX, Scene::Noise>;
BENCHMARK(Measured<ProgressiveMovingBlock>)->Apply(OneSession);
BENCHMARK(Measured<RemoteFxMovingBlock>)->Apply(OneSession);
BENCHMARK(Measured<ProgressiveNoise>)->Apply(OneSession);
BENCHMARK(Measured<RemoteFxNoise>)->Apply(OneSession);

auto GraphicsMeasurement::MeasureCodec(sdlrdp_codec codec, Scene scene) -> void {
  if (!Holds([&] { Open(640, 480, { }, codec); })) return;
  Client client(sdlrdp_port(&*backend), true, 640, 480);
  if (Prepared(client, codec, scene)) RecordFrames(client, codec, scene);
}
auto GraphicsMeasurement::Prepared(Client& client, sdlrdp_codec codec, Scene scene) -> bool {
  auto const progressive = codec == SDLRDP_CODEC_PROGRESSIVE;
  if (progressive) client.EnableGraphics({ .qoe_acknowledgements = true });
  if (!Holds([&] { Connect(client); })) return false;
  if (progressive && !Check(client.Until([this] { return logs.Contains("GFX confirmed"); }), "the client confirms GFX"))
    return false;
  return Holds([&] { Present(ScenePixels(scene, 0), 640, 480); }, [&] { AwaitAllAcknowledged(client, backend, logs); });
}
auto GraphicsMeasurement::MeasuredFrames(Client& client, Scene scene) -> std::optional<FrameCost> {
  FrameCost cost;
  for (std::uint32_t frame = 1; frame <= 20; ++frame) {
    auto const pixels    = ScenePixels(scene, frame);
    auto const presented = Clock::now();
    if (!Check(backend.Present(pixels, 640, 480, Damage(scene, frame)) == 0, "the backend presents"))
      return std::nullopt;
    if (!client.Until([this] { return AllAcknowledged(*backend); })) {
      Fail(std::format("frame {} is acknowledged: {}", frame, logs.Text(true)));
      return std::nullopt;
    }
    cost.latency_ms    += std::chrono::duration<double, std::milli>(Clock::now() - presented).count();
    cost.maximum_error =  std::max(cost.maximum_error, client.MaxError(pixels));
  }
  return cost;
}
auto GraphicsMeasurement::RecordFrames(Client& client, sdlrdp_codec codec, Scene scene) -> void {
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
  if (codec == SDLRDP_CODEC_PROGRESSIVE) RecordGraphicsTiming(client);
  Check(cost->maximum_error <= Tolerance(scene), "the codec stays within its channel error");
}
auto GraphicsMeasurement::RecordGraphicsTiming(Client& client) -> void {
  auto const qoe_arrived = client.Until([this] {
    auto const status = CurrentStatus(*backend);
    return status.has_value() && status->graphics.has_value() && status->graphics->Qoe().frameId == status->frame;
  });
  if (!Check(qoe_arrived, "the client acknowledges the last frame's QoE")) return;
  auto const timing = RequiredGraphics(*backend);
  Record("activation_to_gfx_ms", std::chrono::duration<double, std::milli>(timing.ReadyTime()).count());
  Record("client_decode_ms", timing.Qoe().timeDiffSE);
  Record("client_render_ms", timing.Qoe().timeDiffEDR);
  Record("client_qoe_frame", timing.Qoe().frameId);
  Check(!logs.Contains("GFX QoE"), "no QoE diagnostic is logged");
}
auto GraphicsMeasurement::EncodeDuration() -> std::chrono::nanoseconds {
  return RequiredStatus(*backend).encode_time;
}
}
