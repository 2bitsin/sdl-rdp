#include <sdl-rdp/diagnostics/log-sink.hpp>
#include <sdl-rdp/headless-client.test/audio/gate.hpp>
#include <sdl-rdp/headless-client.test/backend/config.hpp>
#include <sdl-rdp/headless-client.test/backend/events.hpp>
#include <sdl-rdp/headless-client.test/backend/status.hpp>
#include <sdl-rdp/headless-client.test/utilities/wall-milliseconds.hpp>
#include <sdl-rdp/link/event.hpp>

#include <oxbox/utilities/number-text.hpp>
#include <cstddef>
#include <cstdint>

namespace sdl_rdp::integration::session_test::detail::trace {
using sdl_rdp::diagnostics::LogLevel;
using sdl_rdp::headless_client_test::audio::AudioGate;
using sdl_rdp::headless_client_test::audio::WriteFrames;
using sdl_rdp::headless_client_test::backend::AllAcknowledged;
using sdl_rdp::headless_client_test::backend::Contains;
using sdl_rdp::headless_client_test::backend::LoopbackConfig;
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::client::Pixels;
using sdl_rdp::headless_client_test::client::SoundClient;
using sdl_rdp::headless_client_test::utilities::WallMilliseconds;
using sdl_rdp::link::Key;
using sdl_rdp::utilities::Required;

class TraceGate : public AudioGate {
protected:
  auto ThenTraceEvents() -> void {
    for (auto const* event :
         { "key", "audio-block", "audio-confirm", "frame", "ack", "present", "connect", "disconnect" })
      EXPECT_TRUE(logs.Contains(LogLevel::Info, std::format("trace {} t=", event))) << logs.Text(true);
    EXPECT_TRUE(logs.Contains(LogLevel::Info, "rms=1234 peak=1234"));
  }
  auto ThenKeyTraced(Client& client) -> void {
    ASSERT_TRUE(Contains<Key>(UntilEvent<Key>(client)));
  }
  static auto ThenTraceTime(LogLevel level, std::string const& line, std::int64_t now) -> void {
    EXPECT_EQ(level, LogLevel::Info);
    auto const time = Required(oxbox::utilities::ParseNumberAfter<std::int64_t>(line, " t="),
                               "trace lines carry a whole-millisecond time");
    EXPECT_LE(std::abs(now - time), 60000) << line;
  }
  auto Exercise() -> void {
    (*backend).Audio().Open();
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
    ASSERT_EQ(WriteFrames(*backend, pcm, 0, frames), frames);
    ASSERT_NO_FATAL_FAILURE(Present(Pixels(320uz * 200, 0xff123456), 320, 200));
    ASSERT_TRUE(client.Until([&] {
      return audio.CaptureState().samples.size() == pcm.size() && AllAcknowledged(*backend);
    })) << logs.Text(true);
  }
  auto CheckTimes() -> void {
    auto const now = WallMilliseconds();
    for (auto const& [level, line] : logs.Entries()) {
      if (!line.starts_with("trace ")) continue;
      ASSERT_NO_FATAL_FAILURE(ThenTraceTime(level, line, now));
    }
  }
};
TEST_F(TraceGate, WallClockEvents) {
  auto config = LoopbackConfig(certificates.Path());
  config.tracing = true;
  ASSERT_NO_FATAL_FAILURE(backend.Open(config, logs));
  ASSERT_TRUE(backend);
  ASSERT_NO_FATAL_FAILURE(Exercise());
  backend.Close();
  ASSERT_NO_FATAL_FAILURE(ThenTraceEvents());
  CheckTimes();
}
TEST_F(TraceGate, DisabledByDefault) {
  ASSERT_NO_FATAL_FAILURE(Open(320, 200));
  ASSERT_NO_FATAL_FAILURE(Exercise());
  backend.Close();

  for (auto const& [level, line] : logs.Entries()) EXPECT_FALSE(line.starts_with("trace ")) << line;
}
}
