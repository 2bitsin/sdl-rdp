#include "defaults.hpp"
#include <sdl-rdp/configuration/codec.hpp>

namespace sdl3::rdp::settings::detail::defaults {
using sdl_rdp::configuration::Codec;
namespace {
using sdl_rdp::settings::Aspect;
using sdl_rdp::settings::Extent;
using sdl_rdp::settings::Kilobits;
using sdl_rdp::settings::Milliseconds;
using sdl_rdp::settings::Port;
using sdl_rdp::settings::Refresh;
using sdl_rdp::settings::Settings;
auto Built() -> Settings {
  return { .port            = Port{ 3389 },
           .width           = Extent{ 1024 },
           .height          = Extent{ 768 },
           .refresh         = Refresh{ },
           .aspect          = Aspect::None(),
           .codec           = Codec::Auto,
           .avc_bitrate     = Kilobits{ 0 },
           .vsync           = false,
           .wait_for_client = false,
           .audio_latency   = Milliseconds{ 500 },
           .audio_lead      = Milliseconds{ 150 } };
}
}
auto Defaults() -> Settings const& {
  static Settings const defaults = Built();
  return defaults;
}
}
