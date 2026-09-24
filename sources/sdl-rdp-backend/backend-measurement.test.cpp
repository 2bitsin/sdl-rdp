#include <sdl-rdp/headless-client.test/backend-instance.hpp>
#include <sdl-rdp/headless-client.test/peer-status.hpp>
#include <sdl-rdp/headless-client.test/round-five.hpp>

namespace BackendGate {
class GraphicsMeasurement : public RoundFive {
protected:
  auto RecordGraphicsTiming(Client& client, sdlrdp_codec codec) -> void {
    if (codec != SDLRDP_CODEC_PROGRESSIVE) return;
    ASSERT_TRUE(client.Until([&] {
      auto const status = CurrentStatus(*backend);
      if (!status.has_value()) return false;
      auto const& graphics = status->graphics;
      return graphics.has_value() && graphics->Qoe().frameId == status->frame;
    }));
    auto const timing = RequiredGraphics(*backend);
    RecordProperty("activation_to_gfx_ms",
                   std::to_string(std::chrono::duration<double, std::milli>(timing.ReadyTime()).count()));
    RecordProperty("client_decode_ms", timing.Qoe().timeDiffSE);
    RecordProperty("client_render_ms", timing.Qoe().timeDiffEDR);
    RecordProperty("client_qoe_frame", timing.Qoe().frameId);
    EXPECT_FALSE(logs.Contains("GFX QoE"));
  }
  auto EncodeDuration() -> std::chrono::nanoseconds {
    return RequiredStatus(*backend).encode_time;
  }
  auto PrepareMeasurement(Client& client, sdlrdp_codec codec, bool noise) -> void {
    if (codec == SDLRDP_CODEC_PROGRESSIVE) client.EnableGraphics();
    ASSERT_TRUE(freerdp_settings_set_bool(client.Instance()->context->settings, FreeRDP_GfxSendQoeAck, TRUE));
    ASSERT_NO_FATAL_FAILURE(Connect(client));
    if (codec == SDLRDP_CODEC_PROGRESSIVE) ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); }));
    ASSERT_NO_FATAL_FAILURE(Present(GraphicsScene(0, noise), 640, 480));
    ASSERT_TRUE(client.Until([&] { return Acknowledged(); }));
  }
  auto MeasureFrames(Client& client, bool noise, unsigned& maximum_error, double& latency) -> void {
    for (unsigned frame = 1; frame <= 20; ++frame) {
      auto              pixels    = GraphicsScene(frame, noise);
      auto              presented = Clock::now();
      sdlrdp_rect const damage    = noise ? sdlrdp_rect{ 0, 0, 640, 480 } : sdlrdp_rect{ int(frame - 1), 40, 33, 32 };
      ASSERT_EQ(backend.Present(pixels, 640, 480, damage), 0);
      ASSERT_TRUE(client.Until([&] { return Acknowledged(); })) << logs.Text(true);
      latency       += std::chrono::duration<double, std::milli>(Clock::now() - presented).count();
      maximum_error =  std::max(maximum_error, client.MaxError(pixels));
    }
  }
  static auto RecordMeasurement(std::size_t bytes, double elapsed, double milliseconds, double latency,
                                unsigned maximum_error) -> void {
    RecordProperty("wire_MB_per_second", std::to_string(double(bytes) / elapsed / 1000000));
    RecordProperty("wire_MB_per_second_at_60fps", std::to_string(double(bytes) * 3.0 / 1000000));
    RecordProperty("encode_ms_per_frame", std::to_string(milliseconds / 20));
    RecordProperty("present_ack_ms_per_frame", std::to_string(latency / 20));
    RecordProperty("maximum_channel_error", maximum_error);
  }
  auto Measure(sdlrdp_codec codec, bool noise) -> void {
    ASSERT_NO_FATAL_FAILURE(Open(640, 480, { }, codec));
    Client client(sdlrdp_port(backend.get()), true, 640, 480);
    ASSERT_NO_FATAL_FAILURE(PrepareMeasurement(client, codec, noise));
    auto     initial_encode = EncodeDuration();
    auto     initial_bytes  = client.Received();
    auto     start          = Clock::now();
    unsigned maximum_error  = 0;
    double   latency        = 0;
    ASSERT_NO_FATAL_FAILURE(MeasureFrames(client, noise, maximum_error, latency));
    auto elapsed      = std::chrono::duration<double>(Clock::now() - start).count();
    auto bytes        = client.Received() - initial_bytes;
    auto milliseconds = std::chrono::duration<double, std::milli>(EncodeDuration() - initial_encode).count();
    RecordMeasurement(bytes, elapsed, milliseconds, latency, maximum_error);
    ASSERT_NO_FATAL_FAILURE(RecordGraphicsTiming(client, codec));
    EXPECT_LE(maximum_error, noise ? 48u : 24u);
  }
};
TEST_F(GraphicsMeasurement, ProgressiveMovingBlock) {
  Measure(SDLRDP_CODEC_PROGRESSIVE, false);
}
TEST_F(GraphicsMeasurement, RemoteFxMovingBlock) {
  Measure(SDLRDP_CODEC_REMOTEFX, false);
}
TEST_F(GraphicsMeasurement, ProgressiveNoise) {
  Measure(SDLRDP_CODEC_PROGRESSIVE, true);
}
TEST_F(GraphicsMeasurement, RemoteFxNoise) {
  Measure(SDLRDP_CODEC_REMOTEFX, true);
}
}
