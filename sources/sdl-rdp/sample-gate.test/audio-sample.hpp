#pragma once
#include <sdl-rdp/headless-client.test/frame-observer.hpp>
#include <sdl-rdp/headless-client.test/sound-client.hpp>
#include <sdl-rdp/sample-gate.test/sample.hpp>

#include <cstdint>

namespace SampleGate {
class AudioSample : public Sample {
protected:
  static auto ReceiveAudio(Client& client, Headless::FrameObserver& observer, auto ready) -> void {
    ASSERT_TRUE(client.Until([&] {
      if (!observer.Frames().empty()) observer.Ack();
      return ready();
    }));
  }
  static auto HearTone(Client& client, Headless::SoundClient& audio, std::uint32_t rate, auto captured, auto verify)
      -> void {
    Headless::FrameObserver observer(client);
    ASSERT_NO_FATAL_FAILURE(ReceiveAudio(client, observer, [&] { return captured(audio, observer); }));
    ASSERT_EQ(audio.CaptureState().rate, rate);
    ASSERT_NO_FATAL_FAILURE(verify(audio, observer));
  }
  auto GivenToneProcess(bool tight)                                               -> void;
  auto WhenTonePlayed(bool tight, std::uint32_t rate, auto captured, auto verify) -> void {
    ASSERT_NO_FATAL_FAILURE(GivenToneProcess(tight));
    Client                client(audio_port, true, 640, 480);
    Headless::SoundClient audio(client);
    audio.CaptureState().rate = rate;
    ASSERT_NO_FATAL_FAILURE(Connect(client));
    auto const verify_tight = [&](auto& tone, auto& observer) { verify(tone, observer, tight); };
    ASSERT_NO_FATAL_FAILURE(HearTone(client, audio, rate, captured, verify_tight));
    ASSERT_NO_FATAL_FAILURE(Escape(client));
    process.reset();
  }
  auto WhenTonePlayedTwice(auto captured, auto verify) -> void {
    for (bool const tight : { false, true }) {
      ASSERT_NO_FATAL_FAILURE(WhenTonePlayed(tight, 44100, captured, verify));
    }
  }
};
}
