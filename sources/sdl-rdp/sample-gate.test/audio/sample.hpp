#pragma once
#include <sdl-rdp/headless-client.test/client/sound.hpp>
#include <sdl-rdp/headless-client.test/frame/observer.hpp>
#include <sdl-rdp/sample-gate.test/sample/sample.hpp>

#include <cstdint>

namespace sdl_rdp::sample_gate_test::audio::detail::sample {
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::client::SoundClient;
using sdl_rdp::headless_client_test::frame::FrameObserver;
using sdl_rdp::sample_gate_test::sample::Sample;

class AudioSample : public Sample {
protected:
  static auto ReceiveAudio(Client& client, FrameObserver& observer, auto ready) -> void {
    ASSERT_TRUE(client.Until([&] {
      if (!observer.Frames().empty()) observer.Ack();
      return ready();
    }));
  }
  static auto HearTone(Client& client, SoundClient& audio, std::uint32_t rate, auto captured, auto verify) -> void {
    FrameObserver observer(client);
    ASSERT_NO_FATAL_FAILURE(ReceiveAudio(client, observer, [&] { return captured(audio, observer); }));
    ASSERT_EQ(audio.CaptureState().rate, rate);
    ASSERT_NO_FATAL_FAILURE(verify(audio, observer));
  }
  auto GivenToneProcess(bool tight)                                               -> void;
  auto WhenTonePlayed(bool tight, std::uint32_t rate, auto captured, auto verify) -> void {
    ASSERT_NO_FATAL_FAILURE(GivenToneProcess(tight));
    Client      client(audio_port, true, 640, 480);
    SoundClient audio(client);
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

namespace sdl_rdp::sample_gate_test::audio {
using detail::sample::AudioSample;
}
