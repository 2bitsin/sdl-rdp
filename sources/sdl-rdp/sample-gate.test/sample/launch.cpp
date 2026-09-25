#include <sdl-rdp/sample-gate.test/sample/launch.hpp>

#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_video.h>
#include <oxbox/utilities/number-text.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
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
    if (fs::is_regular_file(directory / "sample")) return directory.parent_path();
  }
  Expects(false, "built sample exists on ctest PATH");
  return { };
}

auto BackendLibrary() -> fs::path {
  auto backend = BuildRoot() / "sources/sdl-rdp/backend/libbackend.so";
  Expects(fs::is_regular_file(backend), "built backend exists");
  return backend;
}

auto SetBackendHints(fs::path const& certificates) -> bool {
  return SDL_SetHint("SDL_RDP_CERT_DIR", certificates.c_str())
         && SDL_SetHint("SDL_RDP_BACKEND", BackendLibrary().c_str());
}

// env applies its assignments in order, so an environment entry overrides the defaults before it.
auto SetLoopbackHints(fs::path const& certificates, std::initializer_list<Hint> hints) -> bool {
  constexpr std::array loopback { Hint{ SDL_HINT_VIDEO_DRIVER, "rdp" }, Hint{ "SDL_RDP_PORT", "0" },
                                  Hint{ "SDL_RDP_BIND", "127.0.0.1" } };
  auto const           set      = [](Hint const& hint) { return SDL_SetHint(hint.first, hint.second); };
  return std::ranges::all_of(loopback, set) && std::ranges::all_of(hints, set) && SetBackendHints(certificates);
}

auto Arguments(fs::path const& certificates, Words const& environment, Words const& options) -> Words {
  Expects(fs::is_directory(certificates), "certificate directory exists");
  Words arguments{ "env",
                   "SDL_VIDEO_DRIVER=rdp",
                   "SDL_RDP_PORT=0",
                   "SDL_RDP_BIND=127.0.0.1",
                   "SDL_RDP_CERT_DIR=" + certificates.string(),
                   "SDL_RDP_BACKEND=" + BackendLibrary().string(),
                   "SDL_RDP_CODEC=planar",
                   "SDL_RDP_WAIT_FOR_CLIENT=0" };
  arguments.append_range(environment);
  arguments.push_back((BuildRoot() / "bin/sample").string());
  arguments.append_range(options);
  return arguments;
}

auto AnnouncedPort(std::string_view line) -> std::uint32_t {
  return utilities::Required(oxbox::utilities::ParseNumberAfter<std::uint32_t>(line, "port "),
                             "the sample announces its port as a whole number");
}
auto PrimaryDisplayPort() -> std::uint32_t {
  auto const port = SDL_GetNumberProperty(SDL_GetDisplayProperties(SDL_GetPrimaryDisplay()),
                                          SDL_PROP_DISPLAY_RDP_PORT_NUMBER, 0);
  return static_cast<std::uint32_t>(port);
}
auto AspectOptions() -> Words {
  return { "--size", "640x350", "--aspect", "4:3" };
}
}
