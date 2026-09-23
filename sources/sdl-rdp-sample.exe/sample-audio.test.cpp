#include <cstddef>
#include "_detail/sample-fixture.hpp"
#include <sdl-rdp-backend.so/_detail/headless-audio.hpp>
#include <sdl-rdp-backend.so/_detail/headless-tls.hpp>

namespace SampleGate {
TEST_F(Sample, ToneAndVsync) {
  for (bool tight : {false, true}) {
    auto arguments = Arguments(certificates.Path(), false);
    arguments.insert(arguments.begin() + 1, "SDL_AUDIO_DRIVER=rdp");
    arguments.push_back("--tone");
    if (tight) arguments.push_back("--tight");
    process = std::make_unique<Process>(arguments);
    ASSERT_TRUE(Read("port "));
    auto port = Number(std::string_view(line).substr(5));
    ASSERT_TRUE(Read("audio device=RDP client freq=44100"));
    Client client(port, true, 640, 480);
    Headless::SoundClient audio(client);
    ASSERT_TRUE(freerdp_connect(client.instance.get())) << ConnectLogs();
    Headless::FrameObserver observer(client);
    ASSERT_TRUE(client.Until([&] {
      if (!observer.ids.empty()) observer.Ack();
      return !audio.received.empty() && Clock::now() >= audio.received.front() + 3s;
    }));
    ASSERT_EQ(audio.rate, 44100u);
    std::ranges::for_each(std::views::iota(0, 3), [&](int second) {
      auto start  = audio.received.front() + std::chrono::seconds(second);
      auto blocks = std::ranges::count_if(audio.received, [&](auto time) { return time >= start && time < start + 1s; });
      EXPECT_GE(blocks, 45) << "tight=" << tight << " second=" << second;
      SDL_Log("tone tight=%d second=%d blocks=%zu", tight, second, std::size_t(blocks));
    });
    auto [frequency, db] = Headless::ToneMeasurements(audio.samples, audio.rate);
    EXPECT_NEAR(frequency, 440, 8.8);
    EXPECT_NEAR(db, -12, 0.3);
    if (tight) EXPECT_GE(observer.ids.size(), 2u);
    RecordProperty(tight ? "tight_tone_hz" : "tone_hz", std::to_string(frequency));
    RecordProperty(tight ? "tight_tone_dbfs" : "tone_dbfs", std::to_string(db));
    ASSERT_NO_FATAL_FAILURE(Escape(client));
    process.reset();
  }
}


TEST_F(Sample, ToneAtClientRate)
{
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.begin() + 1, "SDL_AUDIO_DRIVER=rdp");
  arguments.emplace_back("--tone");
  ASSERT_NO_FATAL_FAILURE(GivenProcess(arguments));
  auto port = Number(std::string_view(line).substr(5));
  ASSERT_TRUE(Read("audio device=RDP client freq=44100"));
  Client                client(port, true, 640, 480);
  Headless::SoundClient audio(client);
  audio.rate = 48000;
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << ConnectLogs();
  Headless::FrameObserver observer(client);
  ASSERT_TRUE(client.Until([&] {
    if (!observer.ids.empty()) observer.Ack();
    return audio.samples.size() >= audio.rate * 2;
  }));
  ASSERT_TRUE(Read("audio device=RDP client freq=48000"));
  auto [frequency, db] = Headless::ToneMeasurements(audio.samples, audio.rate);
  EXPECT_NEAR(frequency, 440, 8.8);
  EXPECT_NEAR(db, -12, 0.3);
  RecordProperty("device_format", line);
  RecordProperty("tone_hz", std::to_string(frequency));
  RecordProperty("tone_dbfs", std::to_string(db));
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

class AudioDriver : public Sample {
protected:
  void SetUp() override
  {
    SDL_GetLogOutputFunction(&previous_log, &previous_log_user);
    SDL_SetLogOutputFunction([](void* user, int category, SDL_LogPriority priority, char const* text) {
      auto& self = *static_cast<AudioDriver*>(user);
      auto  level = priority >= SDL_LOG_PRIORITY_ERROR  ? SDLRDP_LOG_ERROR
                    : priority == SDL_LOG_PRIORITY_WARN ? SDLRDP_LOG_WARN
                                                        : SDLRDP_LOG_INFO;
      Headless::Logs::Collect(&self.logs, level, text);
      if (self.previous_log) self.previous_log(self.previous_log_user, category, priority, text);
    },
                             this);
    ASSERT_TRUE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "rdp"));
    ASSERT_TRUE(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "rdp"));
    ASSERT_TRUE(SDL_SetHint("SDL_RDP_PORT", "0"));
    ASSERT_TRUE(SDL_SetHint("SDL_RDP_BIND", "127.0.0.1"));
    ASSERT_TRUE(SDL_SetHint("SDL_RDP_CODEC", "planar"));
    ASSERT_TRUE(SDL_SetHint("SDL_RDP_CERT_DIR", certificates.Path().c_str()));
    auto library = BuildRoot() / "sources/sdl-rdp-backend.so/libsdl-rdp-backend.so";
    ASSERT_TRUE(SDL_SetHint("SDL_RDP_BACKEND", library.c_str()));
    ASSERT_TRUE(SDL_Init(SDL_INIT_AUDIO)) << SDL_GetError();
    SDL_AudioSpec const spec{ SDL_AUDIO_S16, 2, 48000 };
    stream.reset(SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr));
    ASSERT_TRUE(stream) << SDL_GetError();
    auto port = ListeningPort(Number(fs::read_symlink("/proc/self").string()));
    ASSERT_GT(port, 0u);
    Headless::InitializeTls(port);
  }
  void TearDown() override
  {
    stream.reset();
    SDL_Quit();
    SDL_SetLogOutputFunction(previous_log, previous_log_user);
    for (const auto* hint : { SDL_HINT_AUDIO_DRIVER, SDL_HINT_VIDEO_DRIVER, "SDL_RDP_PORT", "SDL_RDP_BIND",
                              "SDL_RDP_CERT_DIR", "SDL_RDP_BACKEND", "SDL_RDP_CODEC", SDL_HINT_RDP_AUDIO_LEAD }) SDL_ResetHint(hint);
  }
  SDL_LogOutputFunction previous_log      = nullptr;
  void*                 previous_log_user = nullptr;
  std::unique_ptr<SDL_AudioStream, decltype(&SDL_DestroyAudioStream)> stream{ nullptr, SDL_DestroyAudioStream };
};
TEST_F(AudioDriver, NoClientTenSecondClock)
{
  EXPECT_EQ(SDL_WasInit(SDL_INIT_VIDEO), 0u);
  EXPECT_STREQ(SDL_GetCurrentAudioDriver(), "rdp");
  std::vector<Sint16> frames(480000uz * 2, 1000);
  ASSERT_TRUE(SDL_PutAudioStreamData(stream.get(), frames.data(), frames.size() * sizeof(Sint16)));
  ASSERT_TRUE(SDL_FlushAudioStream(stream.get()));
  auto started = Clock::now();
  ASSERT_TRUE(SDL_ResumeAudioStreamDevice(stream.get()));
  auto deadline = started + 30s;
  while (SDL_GetAudioStreamQueued(stream.get()) > 0 && Clock::now() < deadline) SDL_Delay(5);
  auto elapsed = std::chrono::duration<double>(Clock::now() - started).count();
  EXPECT_EQ(SDL_GetAudioStreamQueued(stream.get()), 0);
  // Consuming ten seconds of PCM may run one lead ahead of real time.
  // SDL may dequeue one buffer ahead; scheduling delays only make this longer.
  int           buffer_frames = 0;
  SDL_AudioSpec format       { };
  ASSERT_TRUE(SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(stream.get()), &format, &buffer_frames));
  EXPECT_GE(elapsed, 10.0 - 0.150 - double(buffer_frames) / format.freq);
  RecordProperty("no_client_ten_seconds_elapsed", std::to_string(elapsed));
}
TEST_F(AudioDriver, ClientReceivesOneLeadOnAttach)
{
  std::vector<Sint16> pcm(static_cast<std::ptrdiff_t>(48000 * 5) * 2, 1234);
  ASSERT_TRUE(SDL_PutAudioStreamData(stream.get(), pcm.data(), pcm.size() * sizeof(Sint16)));
  ASSERT_TRUE(SDL_ResumeAudioStreamDevice(stream.get()));
  SDL_Delay(200);
  Client                client(ListeningPort(Number(fs::read_symlink("/proc/self").string())), true);
  Headless::SoundClient audio(client);
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << ConnectLogs();
  ASSERT_TRUE(client.Until([&] { return !audio.received.empty(); }));
  auto deadline = audio.received.front() + 100ms;
  while (audio.samples.size() / 2 < audio.rate * 140 / 1000 && Clock::now() < deadline) ASSERT_TRUE(client.Pump(1));
  EXPECT_GE(audio.samples.size() / 2, audio.rate * 140 / 1000);
  EXPECT_LE(audio.received.back(), deadline);
  auto first  = audio.received.size();
  auto frames = audio.samples.size() / 2;
  deadline    = Clock::now() + 1s;
  while (Clock::now() < deadline) ASSERT_TRUE(client.Pump(1));
  ASSERT_GT(audio.received.size(), first);
  double maximum_gap = 0;
  for (auto i = first; i < audio.received.size(); ++i)
    maximum_gap = std::max(maximum_gap, std::chrono::duration<double, std::milli>(audio.received[i] - audio.received[i - 1]).count());
  auto block_ms = 1000.0 * (audio.samples.size() / 2 - frames) / (audio.received.size() - first) / audio.rate;
  EXPECT_LE(maximum_gap, 2 * block_ms + 10);
  auto elapsed = std::chrono::duration<double>(audio.received.back() - audio.received[first - 1]).count();
  EXPECT_NEAR(double(audio.samples.size() / 2 - frames) / audio.rate, elapsed, 0.030);
  RecordProperty("maximum_block_gap_ms", std::to_string(maximum_gap));
}
TEST_F(AudioDriver, StallRefillsTheLead)
{
  std::vector<Sint16> pcm(static_cast<std::ptrdiff_t>(48000 * 5) * 2, 1234);
  ASSERT_TRUE(SDL_PutAudioStreamData(stream.get(), pcm.data(), pcm.size() * sizeof(Sint16)));
  ASSERT_TRUE(SDL_ResumeAudioStreamDevice(stream.get()));
  Client                client(ListeningPort(Number(fs::read_symlink("/proc/self").string())), true);
  Headless::SoundClient audio(client);
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << ConnectLogs();
  ASSERT_TRUE(client.Until([&] { return audio.samples.size() / 2 >= audio.rate / 2; }));
  ASSERT_TRUE(SDL_LockAudioStream(stream.get()));
  auto deadline = Clock::now() + 300ms;
  bool pumped   = true;
  while (Clock::now() < deadline && pumped) pumped = client.Pump(1);
  auto frames  = audio.samples.size() / 2;
  auto resumed = Clock::now();
  SDL_UnlockAudioStream(stream.get());
  ASSERT_TRUE(pumped);
  while (audio.samples.size() / 2 - frames < audio.rate * 150 / 1000 && Clock::now() < resumed + 100ms)
    ASSERT_TRUE(client.Pump(1));
  EXPECT_GE(audio.samples.size() / 2 - frames, audio.rate * 150 / 1000);
  EXPECT_LE(audio.received.back(), resumed + 100ms);
}
TEST_F(AudioDriver, LeadAtOrAboveLatencyFailsOpen)
{
  stream.reset();
  SDL_AudioSpec const spec{ SDL_AUDIO_S16, 2, 48000 };
  for (const auto* lead : { "500", "501" }) {
    ASSERT_TRUE(SDL_SetHint(SDL_HINT_RDP_AUDIO_LEAD, lead));
    stream.reset(SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr));
    EXPECT_FALSE(stream);
    EXPECT_STREQ(SDL_GetError(), "RDP audio lead must be below the audio latency window");
  }
}
TEST_F(AudioDriver, ZeroLeadKeepsRealtimeClock)
{
  stream.reset();
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_RDP_AUDIO_LEAD, "0"));
  SDL_AudioSpec const spec{ SDL_AUDIO_S16, 2, 48000 };
  stream.reset(SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr));
  ASSERT_TRUE(stream) << SDL_GetError();
  std::vector<Sint16> pcm(48000uz * 2, 1234);
  ASSERT_TRUE(SDL_PutAudioStreamData(stream.get(), pcm.data(), pcm.size() * sizeof(Sint16)));
  ASSERT_TRUE(SDL_FlushAudioStream(stream.get()));
  auto started = Clock::now();
  ASSERT_TRUE(SDL_ResumeAudioStreamDevice(stream.get()));
  while (SDL_GetAudioStreamQueued(stream.get()) > 0 && Clock::now() < started + 3s) SDL_Delay(1);
  EXPECT_EQ(SDL_GetAudioStreamQueued(stream.get()), 0);
  EXPECT_GE(Clock::now() - started, 990ms);
}
TEST_F(AudioDriver, AudioBeforeVideoSurvivesVideoQuit)
{
  ASSERT_TRUE(SDL_InitSubSystem(SDL_INIT_VIDEO)) << SDL_GetError();
  auto port = SDL_GetNumberProperty(SDL_GetDisplayProperties(SDL_GetPrimaryDisplay()), "SDL.display.rdp.port", 0);
  ASSERT_GT(port, 0);
  SDL_QuitSubSystem(SDL_INIT_VIDEO);
  EXPECT_EQ(SDL_WasInit(SDL_INIT_VIDEO), 0u);
  Client                client(port, true);
  Headless::SoundClient audio(client);
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << ConnectLogs();
  ASSERT_TRUE(client.Until([&] { return audio.ready; }));
  std::vector<Sint16> pcm(4800uz * 2, 1234);
  ASSERT_TRUE(SDL_PutAudioStreamData(stream.get(), pcm.data(), pcm.size() * sizeof(Sint16)));
  ASSERT_TRUE(SDL_ResumeAudioStreamDevice(stream.get()));
  ASSERT_TRUE(client.Until([&] { return std::ranges::count(audio.samples, 1234) >= 960; }));
  ASSERT_TRUE(SDL_InitSubSystem(SDL_INIT_VIDEO)) << SDL_GetError();
  EXPECT_EQ(SDL_GetNumberProperty(SDL_GetDisplayProperties(SDL_GetPrimaryDisplay()), "SDL.display.rdp.port", 0), port);
}

TEST_F(AudioDriver, AudioOnlyPlaysBlackDesktop)
{
  EXPECT_EQ(SDL_WasInit(SDL_INIT_VIDEO), 0u);
  auto pid  = Number(fs::read_symlink("/proc/self").string());
  auto port = ListeningPort(pid);
  ASSERT_GT(port, 0u);
  Client                client(port, true);
  Headless::SoundClient audio(client);
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << ConnectLogs();
  ASSERT_TRUE(client.Until([&] { return audio.ready; }));
  auto* gdi = client.instance->context->gdi;
  std::vector<UINT32> black(std::size_t(gdi->width) * gdi->height);
  ASSERT_TRUE(client.Until([&] { return client.Matches(black); }));
  std::vector<Sint16> pcm(4800uz * 2, 1234);
  ASSERT_TRUE(SDL_PutAudioStreamData(stream.get(), pcm.data(), pcm.size() * sizeof(Sint16)));
  ASSERT_TRUE(SDL_ResumeAudioStreamDevice(stream.get()));
  ASSERT_TRUE(client.Until([&] { return std::ranges::count(audio.samples, 1234) >= 960; }));
  EXPECT_EQ(SDL_WasInit(SDL_INIT_VIDEO), 0u);
}
}
