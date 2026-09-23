#include "_detail/test-audio.hpp"

namespace BackendGate {
TEST_F(AudioGate, AudioAbsentDiscards) {
  sdlrdp_audio_close(nullptr);
  EXPECT_EQ(sdlrdp_audio_open(nullptr), -1);
  EXPECT_STREQ(sdlrdp_last_error(), "Invalid audio handle.");
  EXPECT_EQ(sdlrdp_audio_rate(nullptr), 0u);
  Open(320, 200);
  ASSERT_EQ(sdlrdp_audio_open(backend.get()), 0);
  std::vector<INT16> frames(48000 * 10 * 2, 1234);
  EXPECT_EQ(sdlrdp_audio_write(backend.get(), frames.data(), 480000), 480000);
  EXPECT_EQ(sdlrdp_audio_wait(backend.get(), 0), 1);
  sdlrdp_audio_close(backend.get());
}
TEST_F(AudioGate, AudioPcmAndReconnect) {
  Open(320, 200);
  ASSERT_EQ(sdlrdp_audio_open(backend.get()), 0);
  for (unsigned connection = 0; connection < 2; ++connection) {
    Client client(sdlrdp_port(backend.get()), true);
    SoundClient audio(client);
    ASSERT_NO_FATAL_FAILURE(ConnectAudio(client, audio));
    ASSERT_EQ(audio.server_formats.size(), 2u);
    EXPECT_EQ(audio.server_formats[0].nSamplesPerSec, 44100u);
    EXPECT_EQ(audio.server_formats[1].nSamplesPerSec, 48000u);
    auto frames = audio.rate / 50;
    std::vector<INT16> pcm(frames * 2);
    std::iota(pcm.begin(), pcm.end(), -480);
    ASSERT_EQ(sdlrdp_audio_write(backend.get(), pcm.data(), frames), frames);
    ASSERT_TRUE(client.Until([&] { return audio.samples.size() >= pcm.size(); }));
    EXPECT_EQ(audio.samples.size(), pcm.size());
    EXPECT_EQ(audio.samples.front(), pcm.front());
    EXPECT_EQ(audio.samples.back(), pcm.back());
    EXPECT_TRUE(std::ranges::equal(audio.samples, pcm));
    EXPECT_EQ(audio.pending.size(), 0u);
  }
  RecordProperty("audio_diagnostics", logs.Text(true));
}
TEST_F(AudioGate, AudioFormatMissKeepsSessionAndReconnects) {
  Open(320, 200);
  ASSERT_EQ(sdlrdp_audio_open(backend.get()), 0);
  for (bool unmatched : {false, true}) {
    Client client(sdlrdp_port(backend.get()), true);
    SoundClient audio(client);
    audio.rate = 22050;
    audio.advertise_unmatched = unmatched;
    Connect(client);
    auto events = EventsUntil([](auto const& events) {
      return std::ranges::any_of(events, [](auto const& e) { return e.type == SDLRDP_AUDIO; });
    }, true, &client);
    auto event = std::ranges::find(events, SDLRDP_AUDIO, &sdlrdp_event::type);
    ASSERT_NE(event, events.end()) << logs.Text();
    EXPECT_EQ(event->audio.connected, 0u);
    EXPECT_EQ(sdlrdp_audio_rate(backend.get()), 0u);
    {
      std::scoped_lock lock(logs.guard);
      EXPECT_EQ(std::ranges::count_if(logs.lines, [&](auto const& line) {
        return line.first == SDLRDP_LOG_WARN
          && line.second.contains(unmatched ? "rate=22050" : "client formats: none");
      }), 1);
    }
    EXPECT_FALSE(logs.Contains(SDLRDP_LOG_ERROR, "client doesn't support any format"));
    FrameObserver observer(client);
    Present(std::vector<UINT32>(320 * 200, 0x123456), 320, 200);
    ASSERT_TRUE(client.Until([&] { return !observer.ids.empty(); }));
    ASSERT_TRUE(observer.Ack());
    ASSERT_EQ(sdlrdp_wait_frame(backend.get(), 10000), 1);
    ASSERT_TRUE(freerdp_input_send_keyboard_event(client.instance->context->input, KBD_FLAGS_DOWN, 0x1e));
    events = EventsUntil([](auto const& events) {
      return std::ranges::any_of(events, [](auto const& e) { return e.type == SDLRDP_KEY; });
    }, true, &client);
    EXPECT_NE(std::ranges::find(events, SDLRDP_KEY, &sdlrdp_event::type), events.end());
    EXPECT_EQ(std::ranges::find(events, SDLRDP_DISCONNECTED, &sdlrdp_event::type), events.end());
  }
  Client client(sdlrdp_port(backend.get()), true);
  SoundClient audio(client);
  ASSERT_NO_FATAL_FAILURE(ConnectAudio(client, audio));
  EXPECT_EQ(sdlrdp_audio_rate(backend.get()), 44100u);
}
TEST_F(AudioGate, AudioBothRatesPrefer44100) {
  Open(320, 200);
  ASSERT_EQ(sdlrdp_audio_open(backend.get()), 0);
  Client client(sdlrdp_port(backend.get()), true);
  SoundClient audio(client);
  audio.advertise_both_rates = true;
  ASSERT_NO_FATAL_FAILURE(ConnectAudio(client, audio));
  ASSERT_EQ(audio.server_formats.size(), 2u);
  EXPECT_EQ(audio.server_formats[0].nSamplesPerSec, 44100u);
  EXPECT_EQ(audio.server_formats[1].nSamplesPerSec, 48000u);
  EXPECT_EQ(sdlrdp_audio_rate(backend.get()), 44100u);
  auto frames = audio.rate / 50;
  std::vector<INT16> pcm(frames * 2, 1234);
  ASSERT_EQ(sdlrdp_audio_write(backend.get(), pcm.data(), frames), frames);
  ASSERT_TRUE(client.Until([&] { return audio.samples.size() == pcm.size(); }));
  EXPECT_EQ(audio.samples, pcm);
}
TEST_F(AudioGate, AudioInitialVolume) {
  Open(320, 200);
  ASSERT_EQ(sdlrdp_audio_open(backend.get()), 0);
  EXPECT_EQ(sdlrdp_audio_rate(backend.get()), 0u);
  Client client(sdlrdp_port(backend.get()), true);
  SoundClient audio(client);
  audio.rate = 44100;
  audio.volume = 0x8000ffffu;
  ASSERT_NO_FATAL_FAILURE(ConnectAudio(client, audio));
  EXPECT_EQ(sdlrdp_audio_rate(backend.get()), 44100u);
  std::vector<INT16> pcm(882 * 2);
  std::generate(pcm.begin(), pcm.end(), [i = 0]() mutable { return ++i % 2 ? -12000 : 12000; });
  ASSERT_EQ(sdlrdp_audio_write(backend.get(), pcm.data(), 44), 44);
  ASSERT_EQ(sdlrdp_audio_write(backend.get(), pcm.data() + 88, 838), 838);
  ASSERT_TRUE(client.Until([&] { return audio.samples.size() == pcm.size(); }));
  for (auto frame : audio.samples | std::views::chunk(2)) {
    EXPECT_EQ(frame[0], -12000);
    EXPECT_EQ(frame[1], 6000);
  }
  RecordProperty("volume_pcm", "44100 Hz; 44+838 frames; left=-12000 right=6000; volume=0x8000ffff");
}
TEST_F(AudioGate, AudioSlowConfirmsBoundTenSeconds) {
  Open(320, 200);
  ASSERT_EQ(sdlrdp_audio_open(backend.get()), 0);
  Client client(sdlrdp_port(backend.get()), true);
  SoundClient audio(client);
  audio.rate = 48000;
  audio.auto_confirm = false;
  ASSERT_NO_FATAL_FAILURE(ConnectAudio(client, audio));
  ASSERT_NO_FATAL_FAILURE(EstablishConfirmations(client, audio));
  std::vector<INT16> pcm(480000 * 2, 1234);
  auto writing = std::async(std::launch::async, [&] {
    return sdlrdp_audio_write(backend.get(), pcm.data(), 480000);
  });
  auto deadline = Clock::now() + std::chrono::seconds(15);
  while (audio.confirmed_frames < 480000 && Clock::now() < deadline) {
    if (!client.Pump(2)) break;
    while (!audio.pending.empty() && Clock::now() - audio.pending.front().received >= std::chrono::milliseconds(80))
      if (!audio.Confirm()) break;
  }
  if (audio.confirmed_frames < 480000) sdlrdp_audio_close(backend.get());
  EXPECT_EQ(audio.confirmed_frames, 480000u);
  EXPECT_LE(audio.maximum_pending_frames, 24960u);
  EXPECT_TRUE(logs.Contains(SDLRDP_LOG_WARN, "Audio confirmation gate waiting: client is 500.000 ms behind."));
  RecordProperty("audio_diagnostics", logs.Text(true));
  EXPECT_EQ(writing.get(), 480000);
  RecordProperty("maximum_unconfirmed_ms", std::to_string(audio.maximum_pending_frames / 48.0));
}
TEST_F(AudioGate, AudioPlaybackConfirmsKeepRealtimeStreamContinuous) {
  Open(320, 200);
  ASSERT_EQ(sdlrdp_audio_open(backend.get()), 0);
  Client client(sdlrdp_port(backend.get()), true);
  SoundClient audio(client);
  audio.rate = 48000;
  audio.auto_confirm = false;
  ASSERT_NO_FATAL_FAILURE(ConnectAudio(client, audio));
  RunRealtimeAudio(client, audio);
  freerdp_disconnect(client.instance.get());
  backend.reset();
  CheckAudioStatistics(audio);
}
TEST_F(AudioGate, AudioContinuousUnderProgressiveLoad) {
  Open(1280, 800, {}, SDLRDP_CODEC_PROGRESSIVE);
  ASSERT_EQ(sdlrdp_audio_open(backend.get()), 0);
  Client client(sdlrdp_port(backend.get()), true, 1280, 800);
  client.EnableGraphics();
  Headless::GraphicsObserver observer(client);
  SoundClient audio(client);
  audio.rate = 48000;
  audio.auto_confirm = false;
  ASSERT_NO_FATAL_FAILURE(ConnectAudio(client, audio));
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); }));
  // Pixel decoding on the client pump thread would delay audio reception independently of server encoding.
  observer.channel->SurfaceCommand = [](RdpgfxClientContext* channel, RDPGFX_SURFACE_COMMAND const* command) -> UINT {
    Expects(channel && command && command->codecId == RDPGFX_CODECID_CAPROGRESSIVE, "progressive payload received");
    return CHANNEL_RC_OK;
  };
  auto presenting = std::async(std::launch::async, [&] {
    std::vector<UINT32> pixels(1280 * 800);
    sdlrdp_rect full{0, 0, 1280, 800};
    auto deadline = Clock::now() + std::chrono::seconds(2);
    unsigned presented = 0;
    while (Clock::now() < deadline) {
      if (!sdlrdp_wait_frame(backend.get(), 10)) continue;
      Headless::MovingTilePattern(pixels, 1280, 800, presented);
      if (sdlrdp_present(backend.get(), pixels.data(), 5120, 1280, 800, &full, 1)) break;
      ++presented;
    }
    return presented;
  });
  RunRealtimeAudio(client, audio);
  EXPECT_GE(presenting.get(), 10u);
  EXPECT_GE(observer.frames.size(), 10u);
  freerdp_disconnect(client.instance.get());
  backend.reset();
  CheckAudioStatistics(audio);
}
TEST_F(AudioGate, AudioNeverConfirmsUsesServerClock) {
  Open(320, 200);
  ASSERT_EQ(sdlrdp_audio_open(backend.get()), 0);
  Client client(sdlrdp_port(backend.get()), true);
  SoundClient audio(client);
  audio.rate = 48000;
  audio.auto_confirm = false;
  ASSERT_NO_FATAL_FAILURE(ConnectAudio(client, audio));
  std::vector<INT16> pcm(48000 * 2, 1234);
  auto started = Clock::now();
  auto writing = std::async(std::launch::async, [&] {
    return sdlrdp_audio_write(backend.get(), pcm.data(), 48000);
  });
  auto captured = client.Until([&] { return audio.samples.size() == pcm.size(); });
  if (!captured) sdlrdp_audio_close(backend.get());
  EXPECT_TRUE(captured);
  EXPECT_EQ(writing.get(), 48000);
  auto elapsed = std::chrono::duration<double>(Clock::now() - started).count();
  EXPECT_GE(elapsed, 0.9);
  EXPECT_TRUE(logs.Contains("500"));
  RecordProperty("never_confirms_one_second_elapsed", std::to_string(elapsed));
}
TEST_F(AudioGate, AudioDisconnectDuringBlockedWrite) {
  Open(320, 200);
  ASSERT_EQ(sdlrdp_audio_open(backend.get()), 0);
  for (bool reconnect : {false, true}) {
    Client client(sdlrdp_port(backend.get()), true);
    SoundClient audio(client);
    audio.rate = 48000;
    audio.auto_confirm = reconnect;
    ASSERT_NO_FATAL_FAILURE(ConnectAudio(client, audio));
    ASSERT_NO_FATAL_FAILURE(EstablishConfirmations(client, audio));
    unsigned frames = reconnect ? 960 : 480000;
    std::vector<INT16> pcm(frames * 2, 1234);
    auto writing = std::async(std::launch::async, [&] {
      return sdlrdp_audio_write(backend.get(), pcm.data(), frames);
    });
    auto received = client.Until([&] { return audio.samples.size() >= (reconnect ? 1920u : 48000u); });
    EXPECT_TRUE(received);
    if (!reconnect) EXPECT_EQ(writing.wait_for(std::chrono::milliseconds(0)), std::future_status::timeout);
    EXPECT_TRUE(freerdp_disconnect(client.instance.get()));
    EXPECT_EQ(writing.get(), frames);
    if (reconnect) EXPECT_EQ(audio.samples.size(), 1920u);
  }
}
TEST_F(AudioGate, AudioOneMillisecondPartialBlock) {
  Open(320, 200, {}, SDLRDP_CODEC_RAW, 1);
  ASSERT_EQ(sdlrdp_audio_open(backend.get()), 0);
  Client client(sdlrdp_port(backend.get()), true);
  SoundClient audio(client);
  audio.rate = 48000;
  ASSERT_NO_FATAL_FAILURE(ConnectAudio(client, audio));
  std::array<INT16, 1920> pcm{};
  ASSERT_EQ(sdlrdp_audio_write(backend.get(), pcm.data(), 48), 48);
  auto writing = std::async(std::launch::async, [&] {
    return sdlrdp_audio_write(backend.get(), pcm.data() + 96, 912);
  });
  auto captured = client.Until([&] { return audio.samples.size() == pcm.size(); });
  if (!captured) sdlrdp_audio_close(backend.get());
  EXPECT_TRUE(captured);
  EXPECT_EQ(writing.get(), 912);
}
TEST_F(AudioGate, AudioFallbackIdleDoesNotAccumulateCredit) {
  Open(320, 200);
  ASSERT_EQ(sdlrdp_audio_open(backend.get()), 0);
  Client client(sdlrdp_port(backend.get()), true);
  SoundClient audio(client);
  audio.rate = 48000;
  audio.auto_confirm = false;
  ASSERT_NO_FATAL_FAILURE(ConnectAudio(client, audio));
  std::vector<INT16> pcm(48000 * 2, 1234);
  for (unsigned burst = 1; burst <= 2; ++burst) {
    auto started = Clock::now();
    auto writing = std::async(std::launch::async, [&] {
      return sdlrdp_audio_write(backend.get(), pcm.data(), 48000);
    });
    auto captured = client.Until([&] { return audio.samples.size() == burst * pcm.size(); });
    if (!captured) sdlrdp_audio_close(backend.get());
    EXPECT_TRUE(captured);
    EXPECT_EQ(writing.get(), 48000);
    EXPECT_GE(Clock::now() - started, std::chrono::milliseconds(burst == 1 ? 950 : 450));
    if (burst == 1) std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }
}

TEST_F(AudioGate, AudioReorderedConfirmsCreditOnlyTheirBlock) {
  Open(320, 200);
  ASSERT_EQ(sdlrdp_audio_open(backend.get()), 0);
  Client client(sdlrdp_port(backend.get()), true);
  SoundClient audio(client);
  audio.rate = 48000;
  audio.auto_confirm = false;
  ASSERT_NO_FATAL_FAILURE(ConnectAudio(client, audio));
  ASSERT_NO_FATAL_FAILURE(EstablishConfirmations(client, audio));
  std::vector<INT16> pcm(24000 * 2, 1234);
  ASSERT_EQ(sdlrdp_audio_write(backend.get(), pcm.data(), 24000), 24000);
  ASSERT_TRUE(client.Until([&] { return audio.pending.size() == 25; }));
  EXPECT_EQ(sdlrdp_audio_wait(backend.get(), 0), 0);
  ASSERT_TRUE(audio.Confirm(24));
  EXPECT_EQ(sdlrdp_audio_wait(backend.get(), 10000), 1);
  ASSERT_EQ(sdlrdp_audio_write(backend.get(), pcm.data(), 960), 960);
  ASSERT_TRUE(client.Until([&] { return audio.samples.size() == 49920; }));
  EXPECT_EQ(sdlrdp_audio_wait(backend.get(), 0), 0);
  ASSERT_TRUE(audio.Confirm());
  EXPECT_EQ(sdlrdp_audio_wait(backend.get(), 10000), 1);
  std::scoped_lock lock(logs.guard);
  EXPECT_EQ(std::ranges::count_if(logs.lines, [](auto const& line) {
    return line.first == SDLRDP_LOG_WARN && line.second.contains("Audio confirmation gate waiting");
  }), 1);
}
}
