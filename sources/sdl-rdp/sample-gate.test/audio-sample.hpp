#pragma once
#include <sdl-rdp/headless-client.test/frame-observer.hpp>
#include <sdl-rdp/headless-client.test/sound-client.hpp>
#include <sdl-rdp/sample-gate.test/sample.hpp>

namespace SampleGate {
class AudioSample : public Sample {
protected:
  static auto ReceiveAudio(Client& client, Headless::FrameObserver& observer, auto ready) -> void {
    ASSERT_TRUE(client.Until([&] {
      if (!observer.Frames().empty()) observer.Ack();
      return ready();
    }));
  }
  auto GivenToneProcess(bool tight)                                          -> void;
  auto WhenTonePlayed(bool tight, unsigned rate, auto captured, auto verify) -> void {
    GivenToneProcess(tight);
    if (::testing::Test::HasFatalFailure()) return;
    Client                client(audio_port, true, 640, 480);
    Headless::SoundClient audio(client);
    audio.CaptureState().rate = rate;
    ASSERT_TRUE(freerdp_connect(client.Instance().get())) << ConnectLogs();
    Headless::FrameObserver observer(client);
    ReceiveAudio(client, observer, [&] { return captured(audio, observer); });
    if (::testing::Test::HasFatalFailure()) return;
    ASSERT_EQ(audio.CaptureState().rate, rate);
    verify(audio, observer, tight);
    if (::testing::Test::HasFatalFailure()) return;
    Escape(client);
    if (::testing::Test::HasFatalFailure()) return;
    process.reset();
  }
  auto WhenTonePlayedTwice(auto captured, auto verify) -> void {
    for (bool const tight : { false, true }) {
      WhenTonePlayed(tight, 44100, captured, verify);
      if (::testing::Test::HasFatalFailure()) return;
    }
  }
};
}
