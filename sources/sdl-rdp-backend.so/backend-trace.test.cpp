#include "_detail/test-audio.hpp"

namespace BackendGate {
class TraceGate : public AudioGate {
protected:
  void Exercise() {
    ASSERT_EQ(sdlrdp_audio_open(backend.get()), 0);
    Client client(sdlrdp_port(backend.get()), true);
    SoundClient audio(client);
    ASSERT_NO_FATAL_FAILURE(ConnectAudio(client, audio));
    ASSERT_TRUE(freerdp_input_send_keyboard_event(client.instance->context->input, KBD_FLAGS_DOWN, 0x1e));
    auto frames = 3 * (audio.rate / 50);
    std::vector<INT16> pcm(frames * 2, -1234);
    ASSERT_EQ(sdlrdp_audio_write(backend.get(), pcm.data(), frames), frames);
    Present(std::vector<UINT32>(320 * 200, 0xff123456), 320, 200);
    ASSERT_TRUE(client.Until([&] { return audio.samples.size() == pcm.size() && Acknowledged(); }));
    auto events = EventsUntil([](auto const& events) {
      return std::ranges::any_of(events, [](auto const& event) { return event.type == SDLRDP_KEY; });
    }, true, &client);
    ASSERT_NE(std::ranges::find(events, SDLRDP_KEY, &sdlrdp_event::type), events.end());
  }
  void CheckTimes() {
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
    std::scoped_lock lock(logs.guard);
    for (auto const& [level, line] : logs.lines) {
      if (!line.starts_with("trace ")) continue;
      EXPECT_EQ(level, SDLRDP_LOG_INFO);
      auto start = line.find(" t=");
      ASSERT_NE(start, std::string::npos) << line;
      auto end = line.find(' ', start + 3);
      auto value = std::string_view(line).substr(start + 3, end == std::string::npos ? end : end - start - 3);
      int64_t time = 0;
      auto parsed = std::from_chars(value.data(), value.data() + value.size(), time);
      EXPECT_EQ(parsed.ec, std::errc()) << line;
      EXPECT_EQ(parsed.ptr, value.data() + value.size()) << line;
      EXPECT_LE(std::abs(now - time), 60000) << line;
    }
  }
};
TEST_F(TraceGate, WallClockEvents) {
  ASSERT_EQ(setenv("SDL_RDP_TRACE", "1", 1), 0);
  Open(320, 200);
  ASSERT_EQ(unsetenv("SDL_RDP_TRACE"), 0);
  ASSERT_NE(backend, nullptr);
  ASSERT_NO_FATAL_FAILURE(Exercise());
  backend.reset();
  for (auto event : {"key", "audio-block", "audio-confirm", "frame", "ack", "present", "connect", "disconnect"})
    EXPECT_TRUE(logs.Contains(SDLRDP_LOG_INFO, std::format("trace {} t=", event))) << logs.Text(true);
  EXPECT_TRUE(logs.Contains(SDLRDP_LOG_INFO, "rms=1234 peak=1234"));
  CheckTimes();
}
TEST_F(TraceGate, DisabledByDefault) {
  ASSERT_EQ(unsetenv("SDL_RDP_TRACE"), 0);
  Open(320, 200);
  ASSERT_NO_FATAL_FAILURE(Exercise());
  backend.reset();
  std::scoped_lock lock(logs.guard);
  for (auto const& [level, line] : logs.lines) EXPECT_FALSE(line.starts_with("trace ")) << line;
}
}
