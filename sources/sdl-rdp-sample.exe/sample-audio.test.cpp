#include "_detail/sample-fixture.hpp"

#include <cstddef>
#include <sdl-rdp-backend.so/_detail/headless-audio.hpp>
#include <sdl-rdp-backend.so/_detail/headless-tls.hpp>

namespace SampleGate {
namespace {
void ThenBlockCadence(Headless::SoundClient const& audio, bool tight) {
  std::ranges::for_each(std::views::iota(0, 3), [&](int second) {
    auto start = audio.CaptureState().received.front() + std::chrono::seconds(second);
    auto blocks = std::ranges::count_if(audio.CaptureState().received,
                                        [&](auto time) { return time >= start && time < start + 1s; });
    EXPECT_GE(blocks, 45) << "tight=" << tight << " second=" << second;
    SDL_Log("tone tight=%d second=%d blocks=%zu", tight, second, std::size_t(blocks));
  });
}
void ReceiveAudio(Client& client, Headless::FrameObserver& observer, auto ready) {
  ASSERT_TRUE(client.Until([&] {
    if (!observer.Frames().empty()) observer.Ack();
    return ready();
  }));
}
}

namespace {
void ThenTone(Headless::SoundClient const& audio, Headless::FrameObserver const& frames, bool tight) {
  ThenBlockCadence(audio, tight);
  if (::testing::Test::HasFatalFailure()) return;
  auto [frequency, db] = Headless::ToneMeasurements(audio.CaptureState().samples, audio.CaptureState().rate);
  EXPECT_NEAR(frequency, 440, 8.8);
  EXPECT_NEAR(db, -12, 0.3);
  if (tight) EXPECT_GE(frames.Frames().size(), 2u);
  testing::Test::RecordProperty(tight ? "tight_tone_hz" : "tone_hz", std::to_string(frequency));
  testing::Test::RecordProperty(tight ? "tight_tone_dbfs" : "tone_dbfs", std::to_string(db));
}
}

namespace {
void ThenLeadCadence(Headless::SoundClient const& audio, size_t first, size_t frames) {
  ASSERT_GT(audio.CaptureState().received.size(), first);
  double maximum_gap = 0;
  for (auto i = first; i < audio.CaptureState().received.size(); ++i)
    maximum_gap = std::max(maximum_gap, std::chrono::duration<double, std::milli>(audio.CaptureState().received[i] -
                                                                                  audio.CaptureState().received[i - 1])
                                            .count());
  auto sent_frames = (audio.CaptureState().samples.size() / 2) - frames;
  auto block_ms =
      1000.0 * double(sent_frames) / double(audio.CaptureState().received.size() - first) / audio.CaptureState().rate;
  EXPECT_LE(maximum_gap, (2 * block_ms) + 10);
  auto elapsed =
      std::chrono::duration<double>(audio.CaptureState().received.back() - audio.CaptureState().received[first - 1])
          .count();
  EXPECT_NEAR(double(sent_frames) / audio.CaptureState().rate, elapsed, 0.030);
  testing::Test::RecordProperty("maximum_block_gap_ms", std::to_string(maximum_gap));
}
}
namespace {
void ThenDeviceTone(Headless::SoundClient const& audio, std::string const& line) {
  auto [frequency, db] = Headless::ToneMeasurements(audio.CaptureState().samples, audio.CaptureState().rate);
  EXPECT_NEAR(frequency, 440, 8.8);
  EXPECT_NEAR(db, -12, 0.3);
  testing::Test::RecordProperty("device_format", line);
  testing::Test::RecordProperty("tone_hz", std::to_string(frequency));
  testing::Test::RecordProperty("tone_dbfs", std::to_string(db));
}
}
namespace {
class AudioSample : public SampleGate::Sample {
protected:
  void WhenTonePlayed(bool tight) {
    auto arguments = Arguments(certificates.Path(), false);
    arguments.insert(arguments.begin() + 1, "SDL_AUDIO_DRIVER=rdp");
    arguments.emplace_back("--tone");
    if (tight) arguments.emplace_back("--tight");
    GivenAudioProcess(arguments);
    if (::testing::Test::HasFatalFailure()) return;
    auto port = audio_port;
    Client                client(port, true, 640, 480);
    Headless::SoundClient audio(client);
    ASSERT_TRUE(freerdp_connect(client.Instance().get())) << ConnectLogs();
    Headless::FrameObserver observer(client);
    ReceiveAudio(client, observer, [&] {
      return !audio.CaptureState().received.empty() && Clock::now() >= audio.CaptureState().received.front() + 3s;
    });
    if (::testing::Test::HasFatalFailure()) return;
    ASSERT_EQ(audio.CaptureState().rate, 44100u);
    ThenTone(audio, observer, tight);
    if (::testing::Test::HasFatalFailure()) return;
    Escape(client);
    if (::testing::Test::HasFatalFailure()) return;
    process.reset();
  }
};
TEST_F(AudioSample, ToneAndVsync) {
  for (bool const tight : { false, true }) {
    WhenTonePlayed(tight);
    if (::testing::Test::HasFatalFailure()) return;
  }
}

TEST_F(AudioSample, ToneAtClientRate) {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.begin() + 1, "SDL_AUDIO_DRIVER=rdp");
  arguments.emplace_back("--tone");
  GivenProcess(arguments);
  if (::testing::Test::HasFatalFailure()) return;
  auto port = Number(std::string_view(line).substr(5));
  ASSERT_TRUE(Read("audio device=RDP client freq=44100"));
  Client                client(port, true, 640, 480);
  Headless::SoundClient audio(client);
  audio.CaptureState().rate = 48000;
  ASSERT_TRUE(freerdp_connect(client.Instance().get())) << ConnectLogs();
  Headless::FrameObserver observer(client);
  ReceiveAudio(client, observer,
               [&] { return audio.CaptureState().samples.size() >= std::size_t(audio.CaptureState().rate) * 2; });
  if (::testing::Test::HasFatalFailure()) return;
  ASSERT_TRUE(Read("audio device=RDP client freq=48000"));
  ThenDeviceTone(audio, line);
  Escape(client);
}

class AudioDriver : public AudioSample {
protected:
  void ThenAudioSurvivesVideoQuit(Client& client, Headless::SoundClient& audio) {
    ConnectAudio(client, audio);
    if (::testing::Test::HasFatalFailure()) return;
    ThenPcm(client, audio);
    if (::testing::Test::HasFatalFailure()) return;
  }
  static void ThenRefilledLead(Client& client, Headless::SoundClient& audio, std::size_t frames,
                               Clock::time_point resumed) {
    while ((audio.CaptureState().samples.size() / 2) - frames < audio.CaptureState().rate * 150 / 1000 &&
           Clock::now() < resumed + 100ms)
      ASSERT_TRUE(client.Pump(1));
    EXPECT_GE((audio.CaptureState().samples.size() / 2) - frames, audio.CaptureState().rate * 150 / 1000);
    EXPECT_LE(audio.CaptureState().received.back(), resumed + 100ms);
  }
  static void ThenInitialLead(Client& client, Headless::SoundClient& audio) {
    ASSERT_TRUE(client.Until([&] { return !audio.CaptureState().received.empty(); }));
    auto deadline = audio.CaptureState().received.front() + 100ms;
    while (audio.CaptureState().samples.size() / 2 < audio.CaptureState().rate * 140 / 1000 && Clock::now() < deadline)
      ASSERT_TRUE(client.Pump(1));
    EXPECT_GE(audio.CaptureState().samples.size() / 2, audio.CaptureState().rate * 140 / 1000);
    EXPECT_LE(audio.CaptureState().received.back(), deadline);
  }
  void GivenAudioBackend() {
    ASSERT_TRUE(SDL_SetHint("SDL_RDP_CERT_DIR", certificates.Path().c_str()));
    auto library = BuildRoot() / "sources/sdl-rdp-backend.so/libsdl-rdp-backend.so";
    ASSERT_TRUE(SDL_SetHint("SDL_RDP_BACKEND", library.c_str()));
  }
  void GivenAudioHints() {
    ASSERT_TRUE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "rdp"));
    ASSERT_TRUE(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "rdp"));
    ASSERT_TRUE(SDL_SetHint("SDL_RDP_PORT", "0"));
    ASSERT_TRUE(SDL_SetHint("SDL_RDP_BIND", "127.0.0.1"));
    ASSERT_TRUE(SDL_SetHint("SDL_RDP_CODEC", "planar"));
    GivenAudioBackend();
  }
  void GivenSoundClient() {
    sound_client =
        std::make_unique<Client>(ListeningPort(pid_t(Number(fs::read_symlink("/proc/self").string()))), true);
    sound = std::make_unique<Headless::SoundClient>(*sound_client);
    ASSERT_TRUE(freerdp_connect(sound_client->Instance().get())) << ConnectLogs();
  }
  void PlayPcm(std::size_t count) {
    std::vector<Sint16> pcm(count, 1234);
    ASSERT_TRUE(SDL_PutAudioStreamData(stream.get(), pcm.data(), pcm.size() * sizeof(Sint16)));
    ASSERT_TRUE(SDL_ResumeAudioStreamDevice(stream.get()));
  }
  void ConnectAudio(Client& client, Headless::SoundClient& audio) {
    ASSERT_TRUE(freerdp_connect(client.Instance().get())) << ConnectLogs();
    ASSERT_TRUE(client.Until([&] { return audio.CaptureState().ready; }));
  }
  void ThenPcm(Client& client, Headless::SoundClient& audio) {
    PlayPcm(4800uz * 2);
    if (::testing::Test::HasFatalFailure()) return;
    ASSERT_TRUE(client.Until([&] { return std::ranges::count(audio.CaptureState().samples, 1234) >= 960; }));
  }
  void CaptureLogs() {
    SDL_GetLogOutputFunction(&previous_log, &previous_log_user);
    SDL_SetLogOutputFunction(
        [](void* user, int category, SDL_LogPriority priority, char const* text) {
          auto& self = *static_cast<AudioDriver*>(user);
          auto level = priority >= SDL_LOG_PRIORITY_ERROR  ? SDLRDP_LOG_ERROR
                       : priority == SDL_LOG_PRIORITY_WARN ? SDLRDP_LOG_WARN
                                                           : SDLRDP_LOG_INFO;
          Headless::Logs::Collect(&self.logs, level, text);
          if (self.previous_log) self.previous_log(self.previous_log_user, category, priority, text);
        },
        this);
  }
  void SetUp() override {
    CaptureLogs();
    GivenAudioHints();
    if (::testing::Test::HasFatalFailure()) return;
    ASSERT_TRUE(SDL_Init(SDL_INIT_AUDIO)) << SDL_GetError();
    SDL_AudioSpec const spec{ SDL_AUDIO_S16, 2, 48000 };
    stream.reset(SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr));
    ASSERT_TRUE(stream) << SDL_GetError();
    auto port = ListeningPort(pid_t(Number(fs::read_symlink("/proc/self").string())));
    ASSERT_GT(port, 0u);
    Headless::InitializeTls(port);
  }
  void TearDown() override {
    sound.reset();
    sound_client.reset();
    stream.reset();
    SDL_Quit();
    SDL_SetLogOutputFunction(previous_log, previous_log_user);
    for (auto const* hint : { SDL_HINT_AUDIO_DRIVER, SDL_HINT_VIDEO_DRIVER, "SDL_RDP_PORT", "SDL_RDP_BIND",
                              "SDL_RDP_CERT_DIR", "SDL_RDP_BACKEND", "SDL_RDP_CODEC", SDL_HINT_RDP_AUDIO_LEAD })
      SDL_ResetHint(hint);
  }
  std::unique_ptr<Client>                sound_client;
  std::unique_ptr<Headless::SoundClient> sound;
  SDL_LogOutputFunction                  previous_log      = nullptr;
  void*                                  previous_log_user = nullptr;
  std::unique_ptr<SDL_AudioStream, decltype(&SDL_DestroyAudioStream)> stream{ nullptr, SDL_DestroyAudioStream };
};
TEST_F(AudioDriver, NoClientTenSecondClock) {
  EXPECT_EQ(SDL_WasInit(SDL_INIT_VIDEO), 0u);
  EXPECT_STREQ(SDL_GetCurrentAudioDriver(), "rdp");
  std::vector<Sint16> frames(480000uz * 2, 1000);
  ASSERT_TRUE(SDL_PutAudioStreamData(stream.get(), frames.data(), frames.size() * sizeof(Sint16)));
  ASSERT_TRUE(SDL_FlushAudioStream(stream.get()));
  auto started = Clock::now();
  ASSERT_TRUE(SDL_ResumeAudioStreamDevice(stream.get()));
  auto deadline = started + 30s;
  while (SDL_GetAudioStreamQueued(stream.get()) > 0 && Clock::now() < deadline)
    SDL_Delay(5);
  auto elapsed = std::chrono::duration<double>(Clock::now() - started).count();
  EXPECT_EQ(SDL_GetAudioStreamQueued(stream.get()), 0);
  // Consuming ten seconds of PCM may run one lead ahead of real time.
  // SDL may dequeue one buffer ahead; scheduling delays only make this longer.
  int           buffer_frames = 0;
  SDL_AudioSpec format       { };
  ASSERT_TRUE(SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(stream.get()), &format, &buffer_frames));
  EXPECT_GE(elapsed, 10.0 - 0.150 - (double(buffer_frames) / format.freq));
  RecordProperty("no_client_ten_seconds_elapsed", std::to_string(elapsed));
}
TEST_F(AudioDriver, ClientReceivesOneLeadOnAttach) {
  PlayPcm(static_cast<std::ptrdiff_t>(48000 * 5) * 2);
  if (::testing::Test::HasFatalFailure()) return;
  SDL_Delay(200);
  GivenSoundClient();
  if (::testing::Test::HasFatalFailure()) return;
  auto& client = *sound_client;
  auto& audio  = *sound;
  ThenInitialLead(client, audio);
  if (::testing::Test::HasFatalFailure()) return;
  auto first    = audio.CaptureState().received.size();
  auto frames   = audio.CaptureState().samples.size() / 2;
  auto deadline = Clock::now() + 1s;
  while (Clock::now() < deadline)
    ASSERT_TRUE(client.Pump(1));
  ThenLeadCadence(audio, first, frames);
}
TEST_F(AudioDriver, StallRefillsTheLead) {
  PlayPcm(static_cast<std::ptrdiff_t>(48000 * 5) * 2);
  if (::testing::Test::HasFatalFailure()) return;
  GivenSoundClient();
  if (::testing::Test::HasFatalFailure()) return;
  auto& client = *sound_client;
  auto& audio  = *sound;
  ASSERT_TRUE(client.Until([&] { return audio.CaptureState().samples.size() / 2 >= audio.CaptureState().rate / 2; }));
  ASSERT_TRUE(SDL_LockAudioStream(stream.get()));
  auto deadline = Clock::now() + 300ms;
  bool pumped   = true;
  while (Clock::now() < deadline && pumped)
    pumped = client.Pump(1);
  auto frames  = audio.CaptureState().samples.size() / 2;
  auto resumed = Clock::now();
  SDL_UnlockAudioStream(stream.get());
  ASSERT_TRUE(pumped);
  ThenRefilledLead(client, audio, frames, resumed);
}
TEST_F(AudioDriver, LeadAtOrAboveLatencyFailsOpen) {
  stream.reset();
  SDL_AudioSpec const spec{ SDL_AUDIO_S16, 2, 48000 };
  for (auto const* lead : { "500", "501" }) {
    ASSERT_TRUE(SDL_SetHint(SDL_HINT_RDP_AUDIO_LEAD, lead));
    stream.reset(SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr));
    EXPECT_FALSE(stream);
    EXPECT_STREQ(SDL_GetError(), "RDP audio lead must be below the audio latency window");
  }
}
TEST_F(AudioDriver, ZeroLeadKeepsRealtimeClock) {
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
  while (SDL_GetAudioStreamQueued(stream.get()) > 0 && Clock::now() < started + 3s)
    SDL_Delay(1);
  EXPECT_EQ(SDL_GetAudioStreamQueued(stream.get()), 0);
  EXPECT_GE(Clock::now() - started, 990ms);
}
TEST_F(AudioDriver, AudioBeforeVideoSurvivesVideoQuit) {
  ASSERT_TRUE(SDL_InitSubSystem(SDL_INIT_VIDEO)) << SDL_GetError();
  auto port = SDL_GetNumberProperty(SDL_GetDisplayProperties(SDL_GetPrimaryDisplay()), "SDL.display.rdp.port", 0);
  ASSERT_GT(port, 0);
  SDL_QuitSubSystem(SDL_INIT_VIDEO);
  EXPECT_EQ(SDL_WasInit(SDL_INIT_VIDEO), 0u);
  Client                client(port, true);
  Headless::SoundClient audio(client);
  ThenAudioSurvivesVideoQuit(client, audio);
  if (::testing::Test::HasFatalFailure()) return;
  ASSERT_TRUE(SDL_InitSubSystem(SDL_INIT_VIDEO)) << SDL_GetError();
  EXPECT_EQ(SDL_GetNumberProperty(SDL_GetDisplayProperties(SDL_GetPrimaryDisplay()), "SDL.display.rdp.port", 0), port);
}

TEST_F(AudioDriver, AudioOnlyPlaysBlackDesktop) {
  EXPECT_EQ(SDL_WasInit(SDL_INIT_VIDEO), 0u);
  auto pid  = Number(fs::read_symlink("/proc/self").string());
  auto port = ListeningPort(pid_t(pid));
  ASSERT_GT(port, 0u);
  Client                client(port, true);
  Headless::SoundClient audio(client);
  ConnectAudio(client, audio);
  if (::testing::Test::HasFatalFailure()) return;
  auto* gdi = client.Instance()->context->gdi;
  std::vector<UINT32> black(std::size_t(gdi->width) * gdi->height);
  ASSERT_TRUE(client.Until([&] { return client.Matches(black); }));
  ThenPcm(client, audio);
  if (::testing::Test::HasFatalFailure()) return;
  EXPECT_EQ(SDL_WasInit(SDL_INIT_VIDEO), 0u);
}
}
}
