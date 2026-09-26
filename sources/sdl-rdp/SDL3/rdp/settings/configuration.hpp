#pragma once
#include "options.hpp"
#include <sdl-rdp/configuration/setup.hpp>
namespace sdl3::rdp::settings::detail::configuration {
using sdl_rdp::configuration::Setup;

// The settings the backend opens with, read once.
auto SetupFrom(Options const& options) -> Setup;
}

namespace sdl3::rdp::settings {
using detail::configuration::SetupFrom;
}
