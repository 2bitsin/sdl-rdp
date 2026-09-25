#pragma once
#include <sdl-rdp/settings/settings.hpp>

namespace sdl3::rdp::settings::detail::defaults {
// The value of every field no hint, file or environment variable sets; the fields with no default stay absent.
auto Defaults() -> sdl_rdp::settings::Settings const&;
}
namespace sdl3::rdp::settings {
using detail::defaults::Defaults;
}
