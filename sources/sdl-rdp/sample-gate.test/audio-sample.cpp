#include <sdl-rdp/sample-gate.test/audio-sample.hpp>

#include <sdl-rdp/sample-gate.test/sample-launch.hpp>

namespace SampleGate {
auto AudioSample::GivenToneProcess(bool tight) -> void {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.begin() + 1, "SDL_AUDIO_DRIVER=rdp");
  arguments.emplace_back("--tone");
  if (tight) arguments.emplace_back("--tight");
  GivenAudioProcess(arguments);
}
}
