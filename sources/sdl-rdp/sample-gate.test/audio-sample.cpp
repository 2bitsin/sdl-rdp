#include <sdl-rdp/sample-gate.test/audio-sample.hpp>

namespace SampleGate {
auto AudioSample::GivenToneProcess(bool tight) -> void {
  Words options{ "--tone" };
  if (tight) options.emplace_back("--tight");
  GivenAudioProcess({ "SDL_AUDIO_DRIVER=rdp" }, options);
}
}
