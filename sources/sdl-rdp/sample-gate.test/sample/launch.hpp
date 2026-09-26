#pragma once
#include <SDL3/SDL_hints.h>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace sdl_rdp::sample_gate_test::sample::detail::launch {
using Words = std::vector<std::string>;
struct Hint {
  std::string name;
  std::string value;
};

auto BuildRoot()                          -> std::filesystem::path;
auto Arguments(std::filesystem::path const& certificates, Words const& environment = { }, Words const& options = { })
    -> Words;
auto AnnouncedPort(std::string_view line) -> std::uint32_t;
auto PrimaryDisplayPort()                 -> std::uint32_t;
auto AspectOptions()                      -> Words;
// The port is the one loopback hint a test's environment contests, so only it takes the priority.
auto SetLoopbackHints(std::filesystem::path const& certificates, std::initializer_list<Hint> extra = { },
                      SDL_HintPriority port_priority = SDL_HINT_NORMAL) -> bool;
}

namespace sdl_rdp::sample_gate_test::sample {
using detail::launch::AnnouncedPort;
using detail::launch::Arguments;
using detail::launch::AspectOptions;
using detail::launch::BuildRoot;
using detail::launch::Hint;
using detail::launch::PrimaryDisplayPort;
using detail::launch::SetLoopbackHints;
using detail::launch::Words;
}
