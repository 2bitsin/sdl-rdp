#include <sdl-rdp/headless-client.test/audio-gate.hpp>
#include <sdl-rdp/headless-client.test/peer-status.hpp>
#include <sdl-rdp/headless-client.test/wall-milliseconds.hpp>

#include <oxbox/utilities/number-text.hpp>
#include <cstddef>
#include <cstdint>

namespace BackendGate {
class TraceGate : public AudioGate {
protected:
  auto ThenTraceEvents() -> void {
    for (auto const* event :
         { "key", "audio-block", "audio-confirm", "frame", "ack", "present", "connect", "disconnect" })
      EXPECT_TRUE(logs.Contains(SDLRDP_LOG_INFO, std::format("trace {} t=", event))) << logs.Text(true);
    EXPECT_TRUE(logs.Contains(SDLRDP_LOG_INFO, "rms=1234 peak=1234"));
  }
  auto ThenKeyTraced(Client& client) -> void {
    ASSERT_TRUE(std::ranges::contains(UntilEvent(client, SDLRDP_KEY), SDLRDP_KEY, &sdlrdp_event::type));
  }
  static auto ThenTraceTime(sdlrdp_log_level level, std::string const& line, std::int64_t now) -> void {
    EXPECT_EQ(level, SDLRDP_LOG_INFO);
    auto const time = Backend::Required(oxbox::utilities::ParseNumberAfter<std::int64_t>(line, " t="),
                                        "trace lines carry a whole-millisecond time");
    EXPECT_LE(std::abs(now - time), 60000) << line;
  }
  auto Exercise() -> void {
    ASSERT_EQ(sdlrdp_audio_open(backend.Handle()), 0);
    NewSession();
    auto& client = ClientSession();
    auto& audio  = AudioSession();
    ASSERT_NO_FATAL_FAILURE(ConnectAudio(client, audio));
    ASSERT_TRUE(freerdp_input_send_keyboard_event(client.Instance()->context->input, KBD_FLAGS_DOWN, 0x1e));
    ASSERT_NO_FATAL_FAILURE(WhenSoundAndPicture(client, audio));
    ThenKeyTraced(client);
  }
  auto WhenSoundAndPicture(Client& client, SoundClient& audio) -> void {
    auto                      frames = 3 * (audio.CaptureState().rate / 50);
    std::vector<std::int16_t> pcm(std::size_t{ frames } * 2, -1234);
    ASSERT_EQ(sdlrdp_audio_write(backend.Handle(), pcm.data(), frames), frames);
    ASSERT_NO_FATAL_FAILURE(Present(std::vector<std::uint32_t>(320uz * 200, 0xff123456), 320, 200));
    ASSERT_TRUE(client.Until([&] {
      return audio.CaptureState().samples.size() == pcm.size() && AllAcknowledged(*backend);
    })) << logs.Text(true);
  }
  auto CheckTimes() -> void {
    auto const now = Headless::WallMilliseconds();
    for (auto const& [level, line] : logs.Entries()) {
      if (!line.starts_with("trace ")) continue;
      ASSERT_NO_FATAL_FAILURE(ThenTraceTime(level, line, now));
    }
  }
};
TEST_F(TraceGate, WallClockEvents) {
  ASSERT_EQ(setenv("SDL_RDP_TRACE", "1", 1), 0);
  ASSERT_NO_FATAL_FAILURE(Open(320, 200));
  ASSERT_EQ(unsetenv("SDL_RDP_TRACE"), 0);
  ASSERT_TRUE(backend);
  ASSERT_NO_FATAL_FAILURE(Exercise());
  backend.Close();
  ASSERT_NO_FATAL_FAILURE(ThenTraceEvents());
  CheckTimes();
}
TEST_F(TraceGate, DisabledByDefault) {
  ASSERT_EQ(unsetenv("SDL_RDP_TRACE"), 0);
  ASSERT_NO_FATAL_FAILURE(Open(320, 200));
  ASSERT_NO_FATAL_FAILURE(Exercise());
  backend.Close();

  for (auto const& [level, line] : logs.Entries()) EXPECT_FALSE(line.starts_with("trace ")) << line;
}
}
