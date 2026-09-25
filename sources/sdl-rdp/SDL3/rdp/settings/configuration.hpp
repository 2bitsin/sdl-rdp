#pragma once
#include "options.hpp"
#include <sdl-rdp/configuration/setup.hpp>
#include <sdl-rdp/settings/aspect.hpp>
#include <sdl-rdp/utilities/aspect-ratio.hpp>
#include <cstdint>
namespace sdl3::rdp::settings::detail::configuration {
using sdl_rdp::configuration::Setup;
using sdl_rdp::settings::Aspect;
using sdl_rdp::utilities::AspectRatio;

// The settings the backend opens with, read once.
class Configuration {
public:
  explicit Configuration(Options const& options);
  auto     Get() const noexcept          -> Setup const&;
  auto     Width() const noexcept        -> std::uint32_t;
  auto     Height() const noexcept       -> std::uint32_t;
  auto     AudioLatency() const noexcept -> std::uint32_t;
private:
  Setup _setup;
};
}

namespace sdl3::rdp::settings {
using detail::configuration::Configuration;
}
