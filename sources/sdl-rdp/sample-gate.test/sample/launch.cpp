#include <sdl-rdp/sample-gate.test/sample/launch.hpp>

#include <SDL3/SDL_video.h>
#include <oxbox/utilities/number-text.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <ranges>

namespace sdl_rdp::sample_gate_test::sample::detail::launch {
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Required;

auto BuildRoot() -> std::filesystem::path {
  auto* path = std::getenv("PATH");
  Expects(path != nullptr, "ctest supplies PATH");
  for (auto part : std::string_view(path) | std::views::split(':')) {
    auto directory = std::filesystem::path(std::string_view(part));
    if (std::filesystem::is_regular_file(directory / "sample")) return directory.parent_path();
  }
  Expects(false, "built sample exists on ctest PATH");
  return { };
}

// env applies its assignments in order, so an environment entry overrides the defaults before it.
auto Arguments(std::filesystem::path const& certificates, Words const& environment, Words const& options) -> Words {
  Expects(std::filesystem::is_directory(certificates), "certificate directory exists");
  Words arguments{ "env",
                   "SDL_VIDEO_DRIVER=rdp",
                   "SDL_RDP_PORT=0",
                   "SDL_RDP_BIND=127.0.0.1",
                   "SDL_RDP_CERT_DIR=" + certificates.string(),
                   "SDL_RDP_CODEC=planar",
                   "SDL_RDP_WAIT_FOR_CLIENT=0" };
  arguments.append_range(environment);
  arguments.push_back((BuildRoot() / "bin/sample").string());
  arguments.append_range(options);
  return arguments;
}

auto AnnouncedPort(std::string_view line) -> std::uint32_t {
  return Required(oxbox::utilities::ParseNumberAfter<std::uint32_t>(line, "port "),
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
auto SetLoopbackHints(std::filesystem::path const& certificates, std::initializer_list<Hint> extra,
                      SDL_HintPriority port_priority) -> bool {
  std::array const loopback { Hint{ .name = SDL_HINT_VIDEO_DRIVER, .value = "rdp" },
                              Hint{ .name = "SDL_RDP_BIND", .value = "127.0.0.1"  },
                              Hint{ .name = "SDL_RDP_CERT_DIR", .value = certificates.string() } };
  auto const       set      = [](Hint const& hint) { return SDL_SetHint(hint.name.c_str(), hint.value.c_str()); };
  return SDL_SetHintWithPriority("SDL_RDP_PORT", "0", port_priority) && std::ranges::all_of(loopback, set)
         && std::ranges::all_of(extra, set);
}
}
