#include <sdl-rdp/sample-gate.test/sample-launch.hpp>

#include <SDL3/SDL_hints.h>
#include <oxbox/utilities/number-text.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <cstdlib>
#include <ranges>

namespace SampleGate {
namespace fs = std::filesystem;
using utilities::Expects;

auto BuildRoot() -> fs::path {
  auto* path = std::getenv("PATH");
  Expects(path != nullptr, "ctest supplies PATH");
  for (auto part : std::string_view(path) | std::views::split(':')) {
    auto directory = fs::path(std::string_view(part));
    if (fs::is_regular_file(directory / "sdl-rdp-sample")) return directory.parent_path();
  }
  Expects(false, "built sample exists on ctest PATH");
  return { };
}

auto BackendLibrary() -> fs::path {
  auto backend = BuildRoot() / "sources/sdl-rdp-backend/libsdl-rdp-backend.so";
  Expects(fs::is_regular_file(backend), "built backend exists");
  return backend;
}

auto SetBackendHints(fs::path const& certificates) -> bool {
  return SDL_SetHint("SDL_RDP_CERT_DIR", certificates.c_str())
         && SDL_SetHint("SDL_RDP_BACKEND", BackendLibrary().c_str());
}

auto Arguments(fs::path const& certificates, bool wait) -> std::vector<std::string> {
  Expects(fs::is_directory(certificates), "certificate directory exists");
  auto root    = BuildRoot();
  auto backend = BackendLibrary();
  return { "env",
           "SDL_VIDEO_DRIVER=rdp",
           "SDL_RDP_PORT=0",
           "SDL_RDP_BIND=127.0.0.1",
           "SDL_RDP_CERT_DIR=" + certificates.string(),
           "SDL_RDP_BACKEND=" + backend.string(),
           "SDL_RDP_CODEC=planar",
           "SDL_RDP_WAIT_FOR_CLIENT=" + std::to_string(wait),
           (root / "bin/sdl-rdp-sample").string() };
}

auto AnnouncedPort(std::string_view line) -> unsigned {
  return utilities::Required(oxbox::utilities::ParseNumberAfter<unsigned>(line, "port "),
                             "the sample announces its port as a whole number");
}
}
