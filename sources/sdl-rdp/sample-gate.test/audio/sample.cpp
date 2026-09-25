#include <sdl-rdp/sample-gate.test/audio/sample.hpp>

namespace sdl_rdp::sample_gate_test::audio::detail::sample {
using sdl_rdp::sample_gate_test::sample::Words;

auto AudioSample::GivenToneProcess(bool tight) -> void {
  Words options{ "--tone" };
  if (tight) options.emplace_back("--tight");
  GivenAudioProcess({ "SDL_AUDIO_DRIVER=rdp" }, options);
}
}
