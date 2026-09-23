#include "_detail/test-backend.hpp"

namespace BackendGate {
class GraphicsMeasurement : public RoundFive {
protected:
  void RecordGraphicsTiming(Client& client, sdlrdp_codec codec) {
    if (codec != SDLRDP_CODEC_PROGRESSIVE) return;
    ASSERT_TRUE(client.Until([&] {
      std::scoped_lock lock(backend->state->session_guard);
      auto const& peer = *backend->state->current;
      return peer.graphics_qoe.frameId == peer.frame_id;
    }));
    std::scoped_lock lock(backend->state->session_guard);
    auto const& peer = *backend->state->current;
    RecordProperty("activation_to_gfx_ms", std::to_string(
      std::chrono::duration<double, std::milli>(peer.graphics_ready_time).count()));
    RecordProperty("client_decode_ms", peer.graphics_qoe.timeDiffSE);
    RecordProperty("client_render_ms", peer.graphics_qoe.timeDiffEDR);
    RecordProperty("client_qoe_frame", peer.graphics_qoe.frameId);
    EXPECT_FALSE(logs.Contains("GFX QoE"));
  }
  void Measure(sdlrdp_codec codec, bool noise) {
    Open(640, 480, {}, codec);
    Client client(sdlrdp_port(backend.get()), true, 640, 480);
    if (codec == SDLRDP_CODEC_PROGRESSIVE) client.EnableGraphics();
    ASSERT_TRUE(freerdp_settings_set_bool(client.instance->context->settings, FreeRDP_GfxSendQoeAck, TRUE));
    Connect(client);
    if (codec == SDLRDP_CODEC_PROGRESSIVE)
      ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); }));
    Present(GraphicsScene(0, noise), 640, 480);
    ASSERT_TRUE(client.Until([&] { return Acknowledged(); }));
    auto encode = [&] {
      std::scoped_lock lock(backend->state->session_guard);
      return backend->state->current->encoder.encode_time;
    };
    auto initial_encode = encode();
    auto initial_bytes = client.Received();
    auto start = Clock::now();
    unsigned maximum_error = 0;
    double latency = 0;
    for (unsigned frame = 1; frame <= 20; ++frame) {
      auto pixels = GraphicsScene(frame, noise);
      auto presented = Clock::now();
      sdlrdp_rect damage = noise ? sdlrdp_rect{0, 0, 640, 480} : sdlrdp_rect{int(frame - 1), 40, 33, 32};
      ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 640 * 4, 640, 480, &damage, 1), 0);
      ASSERT_TRUE(client.Until([&] { return Acknowledged(); })) << logs.Text(true);
      latency += std::chrono::duration<double, std::milli>(Clock::now() - presented).count();
      maximum_error = std::max(maximum_error, client.MaxError(pixels));
    }
    auto elapsed = std::chrono::duration<double>(Clock::now() - start).count();
    auto bytes = client.Received() - initial_bytes;
    auto milliseconds = std::chrono::duration<double, std::milli>(encode() - initial_encode).count();
    RecordProperty("wire_MB_per_second", std::to_string(bytes / elapsed / 1000000));
    RecordProperty("wire_MB_per_second_at_60fps", std::to_string(bytes * 3.0 / 1000000));
    RecordProperty("encode_ms_per_frame", std::to_string(milliseconds / 20));
    RecordProperty("present_ack_ms_per_frame", std::to_string(latency / 20));
    RecordProperty("maximum_channel_error", maximum_error);
    RecordGraphicsTiming(client, codec);
    EXPECT_LE(maximum_error, noise ? 48u : 24u);
  }
};
TEST_F(GraphicsMeasurement, ProgressiveMovingBlock) { Measure(SDLRDP_CODEC_PROGRESSIVE, false); }
TEST_F(GraphicsMeasurement, RemoteFxMovingBlock) { Measure(SDLRDP_CODEC_REMOTEFX, false); }
TEST_F(GraphicsMeasurement, ProgressiveNoise) { Measure(SDLRDP_CODEC_PROGRESSIVE, true); }
TEST_F(GraphicsMeasurement, RemoteFxNoise) { Measure(SDLRDP_CODEC_REMOTEFX, true); }
}
